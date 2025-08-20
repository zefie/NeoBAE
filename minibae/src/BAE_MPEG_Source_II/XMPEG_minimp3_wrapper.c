/*
 * XMPEG_minimp3_wrapper.c
 * Thin adapter implementing legacy MPG_* API on top of minimp3
 * so existing miniBAE code paths (XMPEGFilesSun etc) can be enabled
 * without the old proprietary decoder sources.
 *
 * Only the decoder subset of the API is implemented (no encoder).
 */
#include "XMPEG_BAE_API.h"
#include "X_Assert.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* Embed single-file minimp3 implementation */
#define MINIMP3_IMPLEMENTATION
/* MP2 support: remove MINIMP3_ONLY_MP3 so Layer I/II (MP1/MP2) decoding is enabled */
/* If binary size becomes a concern, re-introduce MINIMP3_ONLY_MP3 via build system define. */
#define MINIMP3_NO_SIMD
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wconversion"
#include "minimp3.h"
#pragma GCC diagnostic pop

/* Local read style (can't reuse private enum from legacy implementation) */
typedef enum {
    MM_READ_NOP = 0,
    MM_READ_MEMORY,
    MM_READ_XFILE   /* currently converted to memory (whole file) */
} MM_READ_STYLE;

typedef struct Minimp3Stream {
    /* Source */
    MM_READ_STYLE   readMode;
    XFILE           file;       /* original file handle (if any) */
    XBOOL           closeFile;  /* close on free */
    const uint8_t  *mem;        /* mp3 data buffer */
    size_t          mem_size;   /* total bytes */
    XBOOL           own_mem;    /* we allocated mem and must free */

    /* Decoder */
    mp3dec_t        dec;
    int             sample_rate;
    int             channels;
    int             bitrate_kbps; /* last frame */
    int             frame_samples; /* samples per channel in last decoded frame */
    uint32_t   max_frames_est; /* rough estimate */
    size_t          pcm_frame_bytes; /* last frame decoded bytes */

    /* For seeking we just reposition raw offset and re-init decoder */
    size_t          raw_offset;
} Minimp3Stream;

/* ---- Forward prototypes ---- */
static int mm_probe_first_frame(Minimp3Stream *s);

/* ---- Utility load helpers ---- */
static size_t mm_peek(Minimp3Stream *s, const uint8_t **ptr) {
    if (s->readMode == MM_READ_MEMORY) {
        *ptr = s->mem + s->raw_offset;
        return s->mem_size - s->raw_offset;
    }
    return 0; /* for file we decode streaming; not implemented */
}

/* ---- Public legacy API implementation ---- */

void * MPG_NewStreamXFILE(XFILE file) {
    if (!file) return NULL;
    Minimp3Stream *s = (Minimp3Stream*)XNewPtr(sizeof(Minimp3Stream));
    if (!s) return NULL;
    XSetMemory(s, sizeof(*s), 0);
    s->readMode = MM_READ_XFILE;
    s->file = file;
    s->closeFile = FALSE;
    /* Load entire file into memory (simpler than streaming) */
    {
        uint32_t length = (uint32_t)XFileGetLength(file);
        if (length == 0) { XDisposePtr((XPTR)s); return NULL; }
        uint8_t *buf = (uint8_t*)XNewPtr(length);
        if (!buf) { XDisposePtr((XPTR)s); return NULL; }
        if (XFileSetPosition(file, 0) != 0 || XFileRead(file, buf, (int32_t)length) != 0) {
            XDisposePtr((XPTR)buf);
            XDisposePtr((XPTR)s);
            return NULL;
        }
        s->mem = buf;
        s->mem_size = length;
        s->own_mem = TRUE;
        s->readMode = MM_READ_MEMORY; /* treat as memory now */
    }
    mp3dec_init(&s->dec);
    if (mm_probe_first_frame(s) != 0) { /* failed */
        MPG_FreeStream(s);
        return NULL;
    }
    return s;
}

void * MPG_NewStream(void *fileName_in) {
    XFILENAME xf; 
    if (!fileName_in) return NULL; 
    XConvertNativeFileToXFILENAME((char*)fileName_in, &xf);
    XFILE f = XFileOpenForRead(&xf);
    if (!f) return NULL;
    Minimp3Stream *s = (Minimp3Stream*)MPG_NewStreamXFILE(f);
    if (s) s->closeFile = TRUE; else XFileClose(f);
    return s;
}

void * MPG_NewStreamXFILENAME(XFILENAME *fileName_in) {
    if (!fileName_in) return NULL;
    XFILE f = XFileOpenForRead(fileName_in);
    if (!f) return NULL;
    Minimp3Stream *s = (Minimp3Stream*)MPG_NewStreamXFILE(f);
    if (s) s->closeFile = TRUE; else XFileClose(f);
    return s;
}

void * MPG_NewStreamFromMemory(void *mpeg_stream, uint32_t mpeg_stream_length) {
    if (!mpeg_stream || !mpeg_stream_length) return NULL;
    Minimp3Stream *s = (Minimp3Stream*)XNewPtr(sizeof(Minimp3Stream));
    if (!s) return NULL;
    s->readMode = MM_READ_MEMORY;
    s->mem = (const uint8_t*)mpeg_stream;
    s->mem_size = mpeg_stream_length;
    s->own_mem = FALSE;
    s->raw_offset = 0;
    mp3dec_init(&s->dec);
    if (mm_probe_first_frame(s) != 0) { MPG_FreeStream(s); return NULL; }
    return s;
}

void MPG_FreeStream(void *ref) {
    Minimp3Stream *s = (Minimp3Stream*)ref;
    if (!s) return;
    if (s->own_mem && s->mem) {
        XDisposePtr((XPTR)s->mem);
    }
    if (s->readMode == MM_READ_XFILE && s->closeFile && s->file) {
        XFileClose(s->file);
    }
    XDisposePtr((XPTR)s);
}

static int mm_decode_next(Minimp3Stream *s, void *out) {
    if (!s) return -1;
    mp3dec_frame_info_t info; 
    int samples = 0;
    if (s->readMode == MM_READ_MEMORY) {
        const uint8_t *ptr; size_t remain = mm_peek(s, &ptr);
        while (remain) {
            samples = mp3dec_decode_frame(&s->dec, ptr, (int)remain, (int16_t*)out, &info);
            if (info.frame_bytes > 0) {
                s->raw_offset += info.frame_bytes;
                if (samples > 0) {
                    if (info.hz) s->sample_rate = info.hz;
                    if (info.channels) s->channels = info.channels;
                    if (info.bitrate_kbps) s->bitrate_kbps = info.bitrate_kbps;
                    s->frame_samples = samples; /* samples per channel */
                    s->pcm_frame_bytes = (size_t)samples * (size_t)s->channels * sizeof(int16_t);
                    return 0; /* success */
                }
                /* no PCM but a frame consumed, continue */
            } else {
                /* advance 1 byte and continue searching */
                s->raw_offset += 1;
            }
            remain = mm_peek(s, &ptr);
        }
        return -1; /* end */
    }
    /* TODO: streaming from file - for now unsupported */
    return -1;
}

int MPG_FillBuffer(void *stream, void *buffer) {
    Minimp3Stream *s = (Minimp3Stream*)stream; if (!s || !buffer) return -1;
    int r = mm_decode_next(s, buffer);
    return r;
}

int MPG_GetBufferSize(void *reference) {
    Minimp3Stream *s = (Minimp3Stream*)reference; return s ? (int)s->pcm_frame_bytes : 0; }
int MPG_GetFrameBufferSizeInBytes(void *reference) { return MPG_GetBufferSize(reference); }
int MPG_GetChannels(void *reference) { Minimp3Stream *s = (Minimp3Stream*)reference; return s && s->channels? s->channels : 2; }
int MPG_GetBitSize(void *reference) { (void)reference; return 16; }
int MPG_GetBitrate(void *reference) { Minimp3Stream *s=(Minimp3Stream*)reference; return s? s->bitrate_kbps*1000:0; }
int MPG_GetSampleRate(void *reference){ Minimp3Stream *s=(Minimp3Stream*)reference; return s? s->sample_rate:0; }
uint32_t MPG_GetMaxBuffers(void *reference){ Minimp3Stream *s=(Minimp3Stream*)reference; return s? s->max_frames_est:0; }
uint32_t MPG_GetSizeInBytes(void *reference){ Minimp3Stream *s=(Minimp3Stream*)reference; if(!s) return 0; if(s->pcm_frame_bytes && s->max_frames_est) return (uint32_t)(s->pcm_frame_bytes * s->max_frames_est); return (uint32_t)s->mem_size; }
uint32_t MPG_GetNumberOfSamples(void *reference){ uint32_t bytes=MPG_GetSizeInBytes(reference); int ch=MPG_GetChannels(reference); return ch? bytes/ (ch*2):0; }
int MPG_SeekStream(void *reference, uint32_t newPos){ Minimp3Stream *s=(Minimp3Stream*)reference; if(!s) return -1; if(s->readMode!=MM_READ_MEMORY) return -1; if(newPos>=s->mem_size) newPos = (uint32_t)s->mem_size; s->raw_offset=newPos; mp3dec_init(&s->dec); return 0; }

/* Encoder stub functions: only compile when encoder support is NOT enabled */
#if !defined(USE_MPEG_ENCODER) || (USE_MPEG_ENCODER==0)
void * MPG_EncodeNewStream(uint32_t a,uint32_t b,uint32_t c,XPTR d,uint32_t e){(void)a;(void)b;(void)c;(void)d;(void)e;return NULL;}
int MPG_EncodeProcess(void *s, XPTR *b, uint32_t *sz, XBOOL *last){(void)s;(void)b;(void)sz;(void)last;return 0;}
void MPG_EncodeFreeStream(void *s){(void)s;}
uint32_t MPG_EncodeMaxFrames(void *s){(void)s; return 0;}
uint32_t MPG_EncodeMaxFrameSize(void *s){(void)s; return 0;}
void MPG_EncodeSetRefillCallback(void *s, MPEGFillBufferFn cb, void *r){(void)s;(void)cb;(void)r;}
#endif

XMPEGEncodeRate XGetMPEGEncodeRate(SndCompressionType type){(void)type;return (XMPEGEncodeRate)0;}
SndCompressionType XGetMPEGCompressionType(XMPEGEncodeRate rate){(void)rate;return (SndCompressionType)0;}
XMPEGEncodeRate XGetClosestMPEGEncodeRate(unsigned int bitrate){(void)bitrate;return (XMPEGEncodeRate)0;}
XFIXED XGetClosestMPEGSampleRate(XFIXED sourceRate, SndCompressionSubType subType){(void)subType;return sourceRate;}
void XGetClosestMPEGSampleRateAndEncodeRate(XFIXED inSampleRate, XMPEGEncodeRate inEncodeRate, XFIXED *outSampleRate, XMPEGEncodeRate *outEncodeRate, SndCompressionSubType subType){if(outSampleRate)*outSampleRate=inSampleRate; if(outEncodeRate)*outEncodeRate=inEncodeRate; (void)subType;}

/* Map bitrate (bps) to legacy SndCompressionType constants (C_MPEG_xx) */
SndCompressionType XGetMPEGBitrateType(uint32_t bitrate) {
    /* Accept bitrate in bits/sec (wrapper calls return kbps*1000) */
    if (bitrate < 36000) return C_MPEG_32;
    if (bitrate < 44000) return C_MPEG_40;
    if (bitrate < 52000) return C_MPEG_48;
    if (bitrate < 60000) return C_MPEG_56;
    if (bitrate < 72000) return C_MPEG_64;
    if (bitrate < 88000) return C_MPEG_80;
    if (bitrate < 104000) return C_MPEG_96;
    if (bitrate < 120000) return C_MPEG_112;
    if (bitrate < 144000) return C_MPEG_128;
    if (bitrate < 176000) return C_MPEG_160;
    if (bitrate < 208000) return C_MPEG_192;
    if (bitrate < 240000) return C_MPEG_224;
    if (bitrate < 288000) return C_MPEG_256;
    return C_MPEG_320;
}

/* ---- Initial probe ---- */
static int mm_skip_id3v2(const uint8_t *data, size_t size) {
    if (size < 10) return 0;
    if (data[0]=='I' && data[1]=='D' && data[2]=='3') {
        size_t tagSize = ((data[6]&0x7F)<<21)|((data[7]&0x7F)<<14)|((data[8]&0x7F)<<7)|(data[9]&0x7F);
        return (int)(10 + tagSize);
    }
    return 0;
}

static int mm_probe_first_frame(Minimp3Stream *s) {
    if(!s) return -1;
    if(s->readMode!=MM_READ_MEMORY) return 0; /* only memory supported */
    const uint8_t *data = s->mem; size_t size = s->mem_size;
    int skip = mm_skip_id3v2(data, size);
    s->raw_offset = skip;
    mp3dec_frame_info_t info; int16_t temp[1152*2];
    size_t remain;
    while ((remain = size - s->raw_offset) > 16) {
        int samples = mp3dec_decode_frame(&s->dec, data + s->raw_offset, (int)remain, temp, &info);
        if (info.frame_bytes > 0) {
            if (samples > 0 && info.channels > 0 && info.hz > 0) {
                s->sample_rate = info.hz;
                s->channels = info.channels;
                s->bitrate_kbps = info.bitrate_kbps;
                s->frame_samples = samples;
                s->pcm_frame_bytes = (size_t)samples * info.channels * sizeof(int16_t);
                s->max_frames_est = info.frame_bytes ? (uint32_t)( (size - s->raw_offset) / info.frame_bytes ) : 0;
                return 0;
            }
            s->raw_offset += info.frame_bytes; /* consumed but no PCM */
        } else {
            s->raw_offset += 1; /* search forward */
        }
    }
    return -1; /* failed */
}

/* NOTE: The wrapper currently only supports memory-based decoding. File streaming can be added later. */
