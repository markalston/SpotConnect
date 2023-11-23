#pragma once

#include <vector>
#include <inttypes.h>
#include <mutex>

/****************************************************************************************
 * Ring buffer
 */
class byteBuffer {
private:
    uint8_t* buffer;
    uint8_t* read_p, * write_p, * wrap_p;
    size_t size;
    std::mutex mutex;
    FILE* storage;

    size_t _space(void) { return size - _used() - 1; }
    size_t _used(void) { return write_p >= read_p ? write_p - read_p : size - (read_p - write_p); }

public:
    byteBuffer(FILE* storage = NULL, size_t size = 4 * 1024 * 1024);
    ~byteBuffer(void);
    size_t read(uint8_t* dst, size_t max, size_t min = 0);
    uint8_t* readInner(size_t& size);
    bool write(const uint8_t* src, size_t size);
    size_t space(void) { std::scoped_lock lock(mutex); return _space(); }
    size_t used(void) { std::scoped_lock lock(mutex); return _used(); }
    void flush(void) { std::scoped_lock lock(mutex); read_p = write_p = buffer; }
    void lock(void) { mutex.lock(); }
    void unlock(void) { mutex.unlock(); }
};

class codecSettings {
public:
    typedef enum {PCM, WAV, MP3, FLAC} type;
    uint32_t rate = 44100;
    uint8_t channels = 2, size = 2;
};

class mp3Settings : public codecSettings {
public:
    int bitrate = 160;
};

class flacSettings : public codecSettings {
public:
    int level = 5;
};

/* 
 Note that the whole implementation assumes that every buffer of samples contains 
 a set of full frames (i.e. a multiply of 16 bits L+R = 4 bytes
 */
class baseCodec {
private:
    static uint32_t index;
    codecSettings* settings;

protected:
    std::shared_ptr<byteBuffer> pcm, encoded;
    int total = 0;

public:
    std::string mimeType;

    baseCodec(codecSettings *settings, std::string mimeType, bool store = false);
    virtual ~baseCodec(void) { }
    virtual int getBitrate(void) { return settings->rate * settings->channels * settings->size * 8; }
    virtual bool pcmWrite(const uint8_t* data, size_t size) { return pcm->write(data, size); }
    void unlock(void) { encoded->unlock(); }
    bool isEmpty(void) { return encoded->used(); }
    virtual void flush(void) { total = 0;  pcm->flush(); encoded->flush(); }
    virtual uint64_t initialize(int64_t duration) = 0;
    virtual size_t read(uint8_t* dst, size_t size, size_t min = 0, bool drain = false);
    virtual uint8_t* readInner(size_t& size, bool drain = false);
    virtual void drain(void) { }
};

std::unique_ptr<baseCodec> createCodec(codecSettings::type type, codecSettings *settings = NULL, bool store = false);
