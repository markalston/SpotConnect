/*
 * AirUPnP - Renderer utils
 *
 * (c) Philippe, philippe_44@outlook.com
 *
 * See LICENSE
 *
 */

#include <string.h>

#include "platform.h"
#include "ixml.h"
#include "ixmlextra.h"
#include "spotupnp.h"
#include "avt_util.h"
#include "upnptools.h"
#include "cross_thread.h"
#include "cross_log.h"
#include "mr_util.h"

extern log_level	util_loglevel;
static log_level 	*loglevel = &util_loglevel;

static IXML_Node*	_getAttributeNode(IXML_Node *node, char *SearchAttr);
int 				_voidHandler(Upnp_EventType EventType, const void *_Event, void *Cookie) { return 0; }

/*----------------------------------------------------------------------------*/
int CalcGroupVolume(struct sMR *Device) {
	int i, n = 0;
	double GroupVolume = 0;

	if (!*Device->Service[GRP_REND_SRV_IDX].ControlURL) return -1;

	for (i = 0; i < glMaxDevices; i++) {
		struct sMR *p = glMRDevices + i;
		if (p->Running && (p == Device || p->Master == Device)) {
			if (p->Volume == -1) p->Volume = CtrlGetVolume(p);
			GroupVolume += p->Volume;
			n++;
		}
	}

	return GroupVolume / n;
}

/*----------------------------------------------------------------------------*/
struct sMR *GetMaster(struct sMR *Device, char **Name)
{
	IXML_Document *ActionNode = NULL, *Response;
	char *Body;
	struct sMR *Master = NULL;
	struct sService *Service = &Device->Service[TOPOLOGY_IDX];
	bool done = false;

	if (!*Service->ControlURL) return NULL;


	ActionNode = UpnpMakeAction("GetZoneGroupState", Service->Type, 0, NULL);

	UpnpSendAction(glControlPointHandle, Service->ControlURL, Service->Type,
								 NULL, ActionNode, &Response);

	if (ActionNode) ixmlDocument_free(ActionNode);

	Body = XMLGetFirstDocumentItem(Response, "ZoneGroupState", true);
	if (Response) ixmlDocument_free(Response);

	Response = ixmlParseBuffer(Body);
	NFREE(Body);

	if (Response) {
		char myUUID[RESOURCE_LENGTH] = "";
		IXML_NodeList *GroupList = ixmlDocument_getElementsByTagName(Response, "ZoneGroup");
		int i;

		sscanf(Device->UDN, "uuid:%s", myUUID);

		// list all ZoneGroups
		for (i = 0; !done && GroupList && i < (int) ixmlNodeList_length(GroupList); i++) {
			IXML_Node *Group = ixmlNodeList_item(GroupList, i);
			const char *Coordinator = ixmlElement_getAttribute((IXML_Element*) Group, "Coordinator");
			IXML_NodeList *MemberList = ixmlDocument_getElementsByTagName((IXML_Document*) Group, "ZoneGroupMember");
			int j;

			// list all ZoneMembers
			for (j = 0; !done && j < (int) ixmlNodeList_length(MemberList); j++) {
				IXML_Node *Member = ixmlNodeList_item(MemberList, j);
				const char *UUID = ixmlElement_getAttribute((IXML_Element*) Member, "UUID");
				int k;

				// get ZoneName
				if (!strcasecmp(myUUID, UUID)) {
					NFREE(*Name);
					*Name = strdup(ixmlElement_getAttribute((IXML_Element*) Member, "ZoneName"));
					if (!strcasecmp(myUUID, Coordinator)) done = true;
				}

				// look for our master (if we are not)
				for (k = 0; !done && k < glMaxDevices; k++) {
					if (glMRDevices[k].Running && strcasestr(glMRDevices[k].UDN, (char*) Coordinator)) {
						Master = glMRDevices + k;
						LOG_DEBUG("Found Master %s %s", myUUID, Master->UDN);
						done = true;
					}
				}
			}

			ixmlNodeList_free(MemberList);
		}

		// our master is not yet discovered, refer to self then
		if (!done) {
			Master = Device;
			LOG_INFO("[%p]: Master not discovered yet, assigning to self", Device);
		}

		ixmlNodeList_free(GroupList);
		ixmlDocument_free(Response);
	}

	return Master;
}

/*----------------------------------------------------------------------------*/
void FlushMRDevices(void)
{
	int i;

	for (i = 0; i < glMaxDevices; i++) {
		struct sMR *p = &glMRDevices[i];
		pthread_mutex_lock(&p->Mutex);
		if (p->Running) {
			// critical to stop the device otherwise libupnp might wait forever
			//if (p->RaopState == RAOP_PLAY) AVTStop(p);
			//raopsr_delete(p->Raop);
			// device's mutex returns unlocked
			DelMRDevice(p);
		} else pthread_mutex_unlock(&p->Mutex);
	}
}

/*----------------------------------------------------------------------------*/
void DelMRDevice(struct sMR *p)
{
	// already locked expect for failed creation which means a trylock is fine
	pthread_mutex_trylock(&p->Mutex);

	// try to unsubscribe but missing players will not succeed and as a result
	// terminating the libupnp takes a while ...
	for (int i = 0; i < NB_SRV; i++) {
		if (p->Service[i].TimeOut) {
			UpnpUnSubscribeAsync(glControlPointHandle, p->Service[i].SID, _voidHandler, NULL);
		}
	}

	p->Running = false;

	// kick-up all sleepers
	crossthreads_wake();

	pthread_mutex_unlock(&p->Mutex);
	pthread_join(p->Thread, NULL);
}

/*----------------------------------------------------------------------------*/
struct sMR* CURL2Device(const UpnpString *CtrlURL) {
	for (int i = 0; i < glMaxDevices; i++) {
		if (!glMRDevices[i].Running) continue;
		for (int j = 0; j < NB_SRV; j++) {
			if (!strcmp(glMRDevices[i].Service[j].ControlURL, UpnpString_get_String(CtrlURL))) {
				return &glMRDevices[i];
			}
		}
	}

	return NULL;
}

/*----------------------------------------------------------------------------*/
struct sMR* SID2Device(const UpnpString *SID) {
	for (int i = 0; i < glMaxDevices; i++) {
		if (!glMRDevices[i].Running) continue;
		for (int j = 0; j < NB_SRV; j++) {
			if (!strcmp(glMRDevices[i].Service[j].SID, UpnpString_get_String(SID))) {
				return &glMRDevices[i];
			}
		}
	}

	return NULL;
}

/*----------------------------------------------------------------------------*/
struct sService *EventURL2Service(const UpnpString *URL, struct sService *s) {
	for (int i = 0; i < NB_SRV; s++, i++) {
		if (strcmp(s->EventURL, UpnpString_get_String(URL))) continue;
		return s;
	}

	return NULL;
}

/*----------------------------------------------------------------------------*/
struct sMR* UDN2Device(const char *UDN) {
	for (int i = 0; i < glMaxDevices; i++) {
		if (!glMRDevices[i].Running) continue;
		if (!strcmp(glMRDevices[i].UDN, UDN)) {
			return &glMRDevices[i];
		}
	}

	return NULL;
}

/*----------------------------------------------------------------------------*/
bool CheckAndLock(struct sMR *Device) {
	if (!Device) {
		LOG_INFO("device is NULL");
		return false;
	}

	pthread_mutex_lock(&Device->Mutex);

	if (Device->Running) return true;

	LOG_INFO("[%p]: device has been removed", Device);
	pthread_mutex_unlock(&Device->Mutex);

	return false;
}


/*----------------------------------------------------------------------------*/
/* 																			  */
/* XML utils															  */
/* 																			  */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static IXML_NodeList *XMLGetNthServiceList(IXML_Document *doc, unsigned int n, bool *contd) {
	IXML_NodeList *ServiceList = NULL;
	*contd = false;

	/*  ixmlDocument_getElementsByTagName()
	 *  Returns a NodeList of all Elements that match the given
	 *  tag name in the order in which they were encountered in a preorder
	 *  traversal of the Document tree.
	 *
	 *  return (NodeList*) A pointer to a NodeList containing the
	 *                      matching items or NULL on an error. 	 */
	LOG_SDEBUG("GetNthServiceList called : n = %d", n);
	IXML_NodeList* servlistnodelist = ixmlDocument_getElementsByTagName(doc, "serviceList");
	if (servlistnodelist &&
		ixmlNodeList_length(servlistnodelist) &&
		n < ixmlNodeList_length(servlistnodelist)) {
		/* Retrieves a Node from a NodeList} specified by a
		 *  numerical index.
		 *
		 *  return (Node*) A pointer to a Node or NULL if there was an
		 *                  error. */
		IXML_Node* servlistnode = ixmlNodeList_item(servlistnodelist, n);
		if (servlistnode) {
			/* create as list of DOM nodes */
			ServiceList = ixmlElement_getElementsByTagName(
				(IXML_Element *)servlistnode, "service");
			*contd = true;
		} else
			LOG_WARN("ixmlNodeList_item(nodeList, n) returned NULL", NULL);
	}
	if (servlistnodelist)
		ixmlNodeList_free(servlistnodelist);

	return ServiceList;
}

/*----------------------------------------------------------------------------*/
int XMLFindAndParseService(IXML_Document* DescDoc, const char* location,
	const char* serviceTypeBase, char** serviceType, char** serviceId, 
	char** eventURL, char** controlURL, char** serviceURL) {
	int found = 0;
	int ret;
	const char* base = NULL;
	bool contd = true;

	char* baseURL = XMLGetFirstDocumentItem(DescDoc, "URLBase", true);
	if (baseURL) base = baseURL;
	else base = location;

	for (unsigned int sindex = 0; contd; sindex++) {
		char* tempServiceType = NULL;
		char* relcontrolURL = NULL;
		char* releventURL = NULL;
		IXML_Element* service = NULL;
		IXML_NodeList* serviceList = NULL;

		if ((serviceList = XMLGetNthServiceList(DescDoc, sindex, &contd)) == NULL) continue;
		unsigned long length = ixmlNodeList_length(serviceList);
		for (int i = 0; i < length; i++) {
			service = (IXML_Element*)ixmlNodeList_item(serviceList, i);
			tempServiceType = XMLGetFirstElementItem((IXML_Element*)service, "serviceType");
			LOG_SDEBUG("serviceType %s", tempServiceType);

			// remove version from service type
			*strrchr(tempServiceType, ':') = '\0';
			if (tempServiceType && strcmp(tempServiceType, serviceTypeBase) == 0) {
				NFREE(*serviceURL);
				*serviceURL = XMLGetFirstElementItem((IXML_Element*)service, "SCPDURL");
				NFREE(*serviceType);
				*serviceType = XMLGetFirstElementItem((IXML_Element*)service, "serviceType");
				NFREE(*serviceId);
				*serviceId = XMLGetFirstElementItem(service, "serviceId");
				LOG_SDEBUG("Service %s, serviceId: %s", serviceType, *serviceId);
				relcontrolURL = XMLGetFirstElementItem(service, "controlURL");
				releventURL = XMLGetFirstElementItem(service, "eventSubURL");
				NFREE(*controlURL);
				*controlURL = (char*)malloc(strlen(base) + strlen(relcontrolURL) + 1);
				if (*controlURL) {
					ret = UpnpResolveURL(base, relcontrolURL, *controlURL);
					if (ret != UPNP_E_SUCCESS) LOG_ERROR("Error generating controlURL from %s + %s", base, relcontrolURL);
				}
				NFREE(*eventURL);
				*eventURL = (char*)malloc(strlen(base) + strlen(releventURL) + 1);
				if (*eventURL) {
					ret = UpnpResolveURL(base, releventURL, *eventURL);
					if (ret != UPNP_E_SUCCESS) LOG_ERROR("Error generating eventURL from %s + %s", base, releventURL);
				}
				free(relcontrolURL);
				free(releventURL);
				found = 1;
				break;
			}
			free(tempServiceType);
			tempServiceType = NULL;
		}
		free(tempServiceType);
		if (serviceList) ixmlNodeList_free(serviceList);
	}

	free(baseURL);
	return found;
}

/*----------------------------------------------------------------------------*/
bool XMLFindAction(const char* base, char* service, char* action) {
	char* url = malloc(strlen(base) + strlen(service) + 1);
	IXML_Document* AVTDoc = NULL;
	bool res = false;

	UpnpResolveURL(base, service, url);

	if (UpnpDownloadXmlDoc(url, &AVTDoc) == UPNP_E_SUCCESS) {
		IXML_Element* actions = ixmlDocument_getElementById(AVTDoc, "actionList");
		IXML_NodeList* actionList = ixmlDocument_getElementsByTagName((IXML_Document*)actions, "action");

		for (int i = 0; actionList && i < (int)ixmlNodeList_length(actionList); i++) {
			IXML_Node* node = ixmlNodeList_item(actionList, i);
			node = (IXML_Node*)ixmlDocument_getElementById((IXML_Document*)node, "name");
			node = ixmlNode_getFirstChild(node);
			const char* name = ixmlNode_getNodeValue(node);
			if (name && !strcasecmp(name, action)) {
				res = true;
				break;
			}
		}
		ixmlNodeList_free(actionList);
	}

	free(url);
	ixmlDocument_free(AVTDoc);

	return res;
}

/*----------------------------------------------------------------------------*/
char *XMLGetChangeItem(IXML_Document *doc, char *Tag, char *SearchAttr, char *SearchVal, char *RetAttr) {
	char *ret = NULL;

	IXML_Element* LastChange = ixmlDocument_getElementById(doc, "LastChange");
	if (!LastChange) return NULL;

	IXML_Node* node = ixmlNode_getFirstChild((IXML_Node*) LastChange);
	if (!node) return NULL;

	char* buf = (char*) ixmlNode_getNodeValue(node);
	if (!buf) return NULL;

	IXML_Document* ItemDoc = ixmlParseBuffer(buf);
	if (!ItemDoc) return NULL;

	IXML_NodeList* List = ixmlDocument_getElementsByTagName(ItemDoc, Tag);
	if (!List) {
		ixmlDocument_free(ItemDoc);
		return NULL;
	}

	for (unsigned i = 0; i < ixmlNodeList_length(List); i++) {
		IXML_Node *node = ixmlNodeList_item(List, i);
		IXML_Node *attr = _getAttributeNode(node, SearchAttr);

		if (!attr) continue;

		if (!strcasecmp(ixmlNode_getNodeValue(attr), SearchVal)) {
			if ((node = ixmlNode_getNextSibling(attr)) == NULL)
				if ((node = ixmlNode_getPreviousSibling(attr)) == NULL) continue;
			if (!strcasecmp(ixmlNode_getNodeName(node), "val")) {
				ret = strdup(ixmlNode_getNodeValue(node));
				break;
			}
		}
	}

	ixmlNodeList_free(List);
	ixmlDocument_free(ItemDoc);

	return ret;
}

/*----------------------------------------------------------------------------*/
static IXML_Node *_getAttributeNode(IXML_Node *node, char *SearchAttr) {
	IXML_Node *ret = NULL;
	IXML_NamedNodeMap *map = ixmlNode_getAttributes(node);
	
	/*
	supposed to act like but case insensitive
	ixmlElement_getAttributeNode((IXML_Element*) node, SearchAttr);
	*/

	for (int i = 0; i < ixmlNamedNodeMap_getLength(map); i++) {
		ret = ixmlNamedNodeMap_item(map, i);
		if (strcasecmp(ixmlNode_getNodeName(ret), SearchAttr)) ret = NULL;
		else break;
	}

	ixmlNamedNodeMap_free(map);

	return ret;
}

/*----------------------------------------------------------------------------*/
char *uPNPEvent2String(Upnp_EventType S) {
	switch (S) {
	/* Discovery */
	case UPNP_DISCOVERY_ADVERTISEMENT_ALIVE:
		return "UPNP_DISCOVERY_ADVERTISEMENT_ALIVE";
	case UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE:
		return "UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE";
	case UPNP_DISCOVERY_SEARCH_RESULT:
		return "UPNP_DISCOVERY_SEARCH_RESULT";
	case UPNP_DISCOVERY_SEARCH_TIMEOUT:
		return "UPNP_DISCOVERY_SEARCH_TIMEOUT";
	/* SOAP */
	case UPNP_CONTROL_ACTION_REQUEST:
		return "UPNP_CONTROL_ACTION_REQUEST";
	case UPNP_CONTROL_ACTION_COMPLETE:
		return "UPNP_CONTROL_ACTION_COMPLETE";
	case UPNP_CONTROL_GET_VAR_REQUEST:
		return "UPNP_CONTROL_GET_VAR_REQUEST";
	case UPNP_CONTROL_GET_VAR_COMPLETE:
		return "UPNP_CONTROL_GET_VAR_COMPLETE";
	case UPNP_EVENT_SUBSCRIPTION_REQUEST:
		return "UPNP_EVENT_SUBSCRIPTION_REQUEST";
	case UPNP_EVENT_RECEIVED:
		return "UPNP_EVENT_RECEIVED";
	case UPNP_EVENT_RENEWAL_COMPLETE:
		return "UPNP_EVENT_RENEWAL_COMPLETE";
	case UPNP_EVENT_SUBSCRIBE_COMPLETE:
		return "UPNP_EVENT_SUBSCRIBE_COMPLETE";
	case UPNP_EVENT_UNSUBSCRIBE_COMPLETE:
		return "UPNP_EVENT_UNSUBSCRIBE_COMPLETE";
	case UPNP_EVENT_AUTORENEWAL_FAILED:
		return "UPNP_EVENT_AUTORENEWAL_FAILED";
	case UPNP_EVENT_SUBSCRIPTION_EXPIRED:
		return "UPNP_EVENT_SUBSCRIPTION_EXPIRED";
	}

	return "";
}
