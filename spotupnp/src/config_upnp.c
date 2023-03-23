/*
 * AirUPnP - Config utils
 *
 * (c) Philippe, philippe_44@outlook.com
 *
 * see LICENSE
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "platform.h"
#include "ixmlextra.h"
#include "cross_log.h"
#include "spotupnp.h"
#include "config_upnp.h"

/*----------------------------------------------------------------------------*/
/* locals */
/*----------------------------------------------------------------------------*/
extern log_level 	main_loglevel;
extern log_level 	util_loglevel;
extern log_level	upnp_loglevel;

/*----------------------------------------------------------------------------*/
void SaveConfig(char *name, void *ref, bool full) {
	struct sMR *p;
	IXML_Document *doc = ixmlDocument_createDocument();
	IXML_Document *old_doc = ref;
	IXML_Node *root, *common, *proto;
	IXML_Element* old_root = ixmlDocument_getElementById(old_doc, "airupnp");

	if (!full && old_doc) {
		ixmlDocument_importNode(doc, (IXML_Node*) old_root, true, &root);
		ixmlNode_appendChild((IXML_Node*) doc, root);

		IXML_NodeList* list = ixmlDocument_getElementsByTagName((IXML_Document*) root, "device");
		for (int i = 0; i < (int) ixmlNodeList_length(list); i++) {
			IXML_Node *device = ixmlNodeList_item(list, i);
			ixmlNode_removeChild(root, device, &device);
			ixmlNode_free(device);
		}
		if (list) ixmlNodeList_free(list);
		common = (IXML_Node*) ixmlDocument_getElementById((IXML_Document*) root, "common");
		proto = (IXML_Node*) ixmlDocument_getElementById((IXML_Document*) common, "protocolInfo");
	}
	else {
		root = XMLAddNode(doc, NULL, "airupnp", NULL);
		common = (IXML_Node*) XMLAddNode(doc, root, "common", NULL);
		proto = (IXML_Node*) XMLAddNode(doc, common, "protocolInfo", NULL);
	}

	XMLUpdateNode(doc, root, false, "main_log",level2debug(main_loglevel));
	XMLUpdateNode(doc, root, false, "upnp_log",level2debug(upnp_loglevel));
	XMLUpdateNode(doc, root, false, "util_log",level2debug(util_loglevel));
	XMLUpdateNode(doc, root, false, "log_limit", "%d", (int32_t) glLogLimit);
	XMLUpdateNode(doc, root, false, "max_players", "%d", (int) glMaxDevices);
	XMLUpdateNode(doc, root, false, "binding", glBinding);
	XMLUpdateNode(doc, root, false, "ports", "%hu:%hu", glPortBase, glPortRange);

	XMLUpdateNode(doc, common, false, "enabled", "%d", (int) glMRConfig.Enabled);
	XMLUpdateNode(doc, common, false, "max_volume", "%d", glMRConfig.MaxVolume);
	XMLUpdateNode(doc, common, false, "http_content_length", "%" PRId64, glMRConfig.HTTPContentLength);
	XMLUpdateNode(doc, common, false, "upnp_max", "%d", glMRConfig.UPnPMax);
	XMLUpdateNode(doc, common, false, "codec", glMRConfig.Codec);
	XMLUpdateNode(doc, common, false, "vorbis_rate", "%d", glMRConfig.VorbisRate);
	XMLUpdateNode(doc, common, false, "flow", "%d", glMRConfig.Flow);
	XMLUpdateNode(doc, common, false, "gapless", "%d", glMRConfig.Gapless);
	XMLUpdateNode(doc, common, false, "artwork", "%s", glMRConfig.ArtWork);

	XMLUpdateNode(doc, proto, false, "pcm", "%s", glMRConfig.ProtocolInfo.pcm);
	XMLUpdateNode(doc, proto, false, "wav", "%s", glMRConfig.ProtocolInfo.wav);
	XMLUpdateNode(doc, proto, false, "flac", "%s", glMRConfig.ProtocolInfo.flac);
	XMLUpdateNode(doc, proto, false, "mp3", "%s", glMRConfig.ProtocolInfo.mp3);

	XMLUpdateNode(doc, proto, false, "DLNA_OP", glMRConfig.DLNA.op);
	XMLUpdateNode(doc, proto, false, "DLNA_FLAGS", glMRConfig.DLNA.flags);
	XMLUpdateNode(doc, proto, false, "DLNA_OP_flow", glMRConfig.DLNA_flow.op);
	XMLUpdateNode(doc, proto, false, "DLNA_FLAGS_flow", glMRConfig.DLNA_flow.flags);

	// mutex is locked here so no risk of a player being destroyed in our back
	for (int i = 0; i < glMaxDevices; i++) {
		IXML_Node *dev_node;

		if (!glMRDevices[i].Running) continue;
		else p = &glMRDevices[i];

		// new device, add nodes
		if (!old_doc || !FindMRConfig(old_doc, p->UDN)) {
			dev_node = XMLAddNode(doc, root, "device", NULL);
			XMLAddNode(doc, dev_node, "udn", p->UDN);
			XMLAddNode(doc, dev_node, "name", p->Config.Name);
			XMLAddNode(doc, dev_node, "mac", "%02x:%02x:%02x:%02x:%02x:%02x", p->Config.mac[0],
						p->Config.mac[1], p->Config.mac[2], p->Config.mac[3], p->Config.mac[4], p->Config.mac[5]);
			XMLAddNode(doc, dev_node, "enabled", "%d", (int) p->Config.Enabled);
		}
	}

	// add devices in old XML file that has not been discovered
	IXML_NodeList* list = ixmlDocument_getElementsByTagName((IXML_Document*) old_root, "device");
	for (int i = 0; i < (int) ixmlNodeList_length(list); i++) {
		char *udn;
		IXML_Node *device, *node;

		device = ixmlNodeList_item(list, i);
		node = (IXML_Node*) ixmlDocument_getElementById((IXML_Document*) device, "udn");
		node = ixmlNode_getFirstChild(node);
		udn = (char*) ixmlNode_getNodeValue(node);
		if (!FindMRConfig(doc, udn)) {
			ixmlDocument_importNode(doc, device, true, &device);
			ixmlNode_appendChild((IXML_Node*) root, device);
		}
	}
	if (list) ixmlNodeList_free(list);

	FILE* file = fopen(name, "wb");
	char *s = ixmlDocumenttoString(doc);
	fwrite(s, 1, strlen(s), file);
	fclose(file);
	free(s);

	ixmlDocument_free(doc);
}

/*----------------------------------------------------------------------------*/
static void LoadConfigItem(tMRConfig *Conf, char *name, char *val) {
	if (!val) return;

	if (!strcmp(name, "enabled")) Conf->Enabled = atoi(val);
	if (!strcmp(name, "max_volume")) Conf->MaxVolume = atoi(val);
	if (!strcmp(name, "http_content_length")) Conf->HTTPContentLength = atoll(val);
	if (!strcmp(name, "upnp_max")) Conf->UPnPMax = atoi(val);
	if (!strcmp(name, "use_flac")) strcpy(Conf->Codec, "flac");  // temporary
	if (!strcmp(name, "codec")) strcpy(Conf->Codec, val);
	if (!strcmp(name, "vorbis_rate")) Conf->VorbisRate = atoi(val);
	if (!strcmp(name, "flow")) Conf->Flow = atoi(val);
	if (!strcmp(name, "gapless")) Conf->Gapless = atoi(val);
	if (!strcmp(name, "artwork")) strcpy(Conf->ArtWork, val);
	if (!strcmp(name, "name")) strcpy(Conf->Name, val);
	if (!strcmp(name, "mac"))  {
		unsigned mac[6];
		// seems to be a Windows scanf buf, cannot support %hhx
		sscanf(val,"%2x:%2x:%2x:%2x:%2x:%2x", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
		for (int i = 0; i < 6; i++) Conf->mac[i] = mac[i];
	}
	if (!strcmp(name, "pcm")) strcpy(Conf->ProtocolInfo.pcm, val);
	if (!strcmp(name, "wav")) strcpy(Conf->ProtocolInfo.wav, val);
	if (!strcmp(name, "flac")) strcpy(Conf->ProtocolInfo.flac, val);
	if (!strcmp(name, "mp3")) strcpy(Conf->ProtocolInfo.mp3, val);

	if (!strcmp(name, "DLNA_OP")) strcpy(Conf->DLNA.op, val);
	if (!strcmp(name, "DLNA_FLAGS")) strcpy(Conf->DLNA.flags, val);
	if (!strcmp(name, "DLNA_OP_flow")) strcpy(Conf->DLNA_flow.op, val);
	if (!strcmp(name, "DLNA_FLAGS_flow")) strcpy(Conf->DLNA_flow.flags, val);
}

/*----------------------------------------------------------------------------*/
static void LoadGlobalItem(char *name, char *val) {
	if (!val) return;

	if (!strcmp(name, "main_log")) main_loglevel = debug2level(val);
	if (!strcmp(name, "upnp_log")) upnp_loglevel = debug2level(val);
	if (!strcmp(name, "util_log")) util_loglevel = debug2level(val);
	if (!strcmp(name, "log_limit")) glLogLimit = atol(val);
	if (!strcmp(name, "max_players")) glMaxDevices = atol(val);
	if (!strcmp(name, "binding")) strcpy(glBinding, val);
	if (!strcmp(name, "ports")) sscanf(val, "%hu:%hu", &glPortBase, &glPortRange);
 }

/*----------------------------------------------------------------------------*/
void *FindMRConfig(void *ref, char *UDN) {
	IXML_Node	*device = NULL;
	IXML_Document *doc = (IXML_Document*) ref;
	IXML_Element* elm = ixmlDocument_getElementById(doc, "airupnp");
	IXML_NodeList* l1_node_list = ixmlDocument_getElementsByTagName((IXML_Document*) elm, "udn");

	for (unsigned i = 0; i < ixmlNodeList_length(l1_node_list); i++) {
		IXML_Node* l1_node = ixmlNodeList_item(l1_node_list, i);
		IXML_Node* l1_1_node = ixmlNode_getFirstChild(l1_node);
		char* v = (char*) ixmlNode_getNodeValue(l1_1_node);
		if (v && !strcmp(v, UDN)) {
			device = ixmlNode_getParentNode(l1_node);
			break;
		}
	}
	if (l1_node_list) ixmlNodeList_free(l1_node_list);
	return device;
}

/*----------------------------------------------------------------------------*/
void *LoadMRConfig(void *ref, char *UDN, tMRConfig *Conf) {
	IXML_Document *doc = (IXML_Document*) ref;
	IXML_Node* node = (IXML_Node*) FindMRConfig(doc, UDN);

	if (node) {
		IXML_NodeList* node_list = ixmlNode_getChildNodes(node);
		for (unsigned i = 0; i < ixmlNodeList_length(node_list); i++) {
			IXML_Node* l1_node = ixmlNodeList_item(node_list, i);
			char* n = (char*) ixmlNode_getNodeName(l1_node);
			IXML_Node* l1_1_node = ixmlNode_getFirstChild(l1_node);
			char *v = (char*) ixmlNode_getNodeValue(l1_1_node);
			LoadConfigItem(Conf, n, v);
		}
		if (node_list) ixmlNodeList_free(node_list);
	}

	return node;
}

/*----------------------------------------------------------------------------*/
void *LoadConfig(char *name, tMRConfig *Conf) {
	IXML_Document* doc = ixmlLoadDocument(name);
	if (!doc) return NULL;

	IXML_Element* elm = ixmlDocument_getElementById(doc, "airupnp");
	if (elm) {
		IXML_NodeList* l1_node_list = ixmlNode_getChildNodes((IXML_Node*) elm);
		for (unsigned i = 0; i < ixmlNodeList_length(l1_node_list); i++) {
			IXML_Node* l1_node = ixmlNodeList_item(l1_node_list, i);
			char* n = (char*) ixmlNode_getNodeName(l1_node);
			IXML_Node* l1_1_node = ixmlNode_getFirstChild(l1_node);
			char *v = (char*) ixmlNode_getNodeValue(l1_1_node);
			LoadGlobalItem(n, v);
		}
		if (l1_node_list) ixmlNodeList_free(l1_node_list);
	}

	elm = ixmlDocument_getElementById((IXML_Document	*)elm, "common");
	if (elm) {
		IXML_NodeList* l1_node_list = ixmlNode_getChildNodes((IXML_Node*) elm);
		for (unsigned i = 0; i < ixmlNodeList_length(l1_node_list); i++) {
			IXML_Node* l1_node = ixmlNodeList_item(l1_node_list, i);
			char* n = (char*) ixmlNode_getNodeName(l1_node);
			IXML_Node* l1_1_node = ixmlNode_getFirstChild(l1_node);
			char *v = (char*) ixmlNode_getNodeValue(l1_1_node);
			LoadConfigItem(&glMRConfig, n, v);
		}
		if (l1_node_list) ixmlNodeList_free(l1_node_list);
	}

	elm = ixmlDocument_getElementById((IXML_Document*)elm, "protocolInfo");
	if (elm) {
		IXML_NodeList* l1_node_list = ixmlNode_getChildNodes((IXML_Node*) elm);
		for (unsigned i = 0; i < ixmlNodeList_length(l1_node_list); i++) {
			IXML_Node* l1_node = ixmlNodeList_item(l1_node_list, i);
			char* n = (char*) ixmlNode_getNodeName(l1_node);
			IXML_Node* l1_1_node = ixmlNode_getFirstChild(l1_node);
			char* v = (char*) ixmlNode_getNodeValue(l1_1_node);
			LoadConfigItem(&glMRConfig, n, v);
		}
		if (l1_node_list) ixmlNodeList_free(l1_node_list);
	}


	return doc;
}



