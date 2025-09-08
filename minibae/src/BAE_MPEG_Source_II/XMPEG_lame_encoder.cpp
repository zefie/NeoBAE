/*
 * XMPEG_lame_encoder.cpp
 * Adapter to use libmp3lame as a replacement for the old Helix/HMP3 encoder.
 * Implements the legacy MPG_Encode* API used by the project.
 * Built when USE_LAME_ENCODER and USE_MPEG_ENCODER are defined.
 */

#if defined(USE_LAME_ENCODER) && (USE_MPEG_ENCODER!=0)

#include "XMPEG_BAE_API.h" // for prototypes / types
#include "X_API.h"
#include "X_Formats.h"
#include "X_Assert.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <new>

#include <lame/lame.h>

#define MAX_BITSTREAM_SIZE 8192

typedef XBOOL (*MPEGFillBufferInternalFn)(void *buffer, void *userRef);

struct LAMEEncoderStream {
    lame_t gf;
    uint32_t sampleRate;
    uint32_t sourceSampleRate;
    uint32_t channels;
    uint32_t encodeRateKbpsTotal; /* total kbps to pass to LAME */
    int16_t *pcmBuffer;
    uint32_t pcmFramesPerCall;
    MPEGFillBufferInternalFn refill;
    void *refillUser;
    unsigned char bitstream[MAX_BITSTREAM_SIZE];
    uint32_t bitstreamBytes;
    XBOOL lastFrame;
    /* leftover frames when slice doesn't align to MP3 frame size */
    int16_t *leftoverBuf;
    uint32_t leftoverFrames;
};

// Pick the nearest supported TOTAL kbps value LAME understands (common MPEG1/LAME CBR table)
static int pick_nearest_total_kbps(int targetTotal)
{
    const int table[] = {32,40,48,56,64,80,96,112,128,160,192,224,256,320};
    const int count = sizeof(table)/sizeof(table[0]);
    int best = table[0];
    int bestDiff = abs(targetTotal - best);
    for(int i=1;i<count;i++){
        int d = abs(targetTotal - table[i]);
        if(d < bestDiff){ bestDiff = d; best = table[i]; }
    }
    return best;
}

extern "C" void * MPG_EncodeNewStream(uint32_t encodeRate /* bits/sec total */, uint32_t sampleRate, uint32_t channels, XPTR pSampleData16Bits, uint32_t frames){
    if(channels == 0 || channels > 2) return NULL;
    LAMEEncoderStream *s = new (std::nothrow) LAMEEncoderStream();
    if(!s){ BAE_PRINTF("audio: MPG_EncodeNewStream allocation failed\n"); return NULL; }
    s->sampleRate = sampleRate; s->sourceSampleRate = sampleRate; s->channels = channels;
    s->pcmBuffer = (int16_t*)pSampleData16Bits; s->pcmFramesPerCall = frames;
    s->refill = NULL; s->refillUser = NULL; s->bitstreamBytes = 0; s->lastFrame = FALSE; s->leftoverBuf = NULL; s->leftoverFrames = 0;

    /* Interpret encodeRate as total bits/sec and map to total kbps for LAME */
    uint32_t providedTotalBits = encodeRate; /* assume caller supplies total bits/sec */
    if(providedTotalBits < 8000) providedTotalBits = 8000; /* clamp sensible lower bound */
    /* convert to nearest kbps (round) */
    uint32_t totalKbps = (providedTotalBits + 500) / 1000;
    if(totalKbps < 8) {
        totalKbps = 8;
    }
    if(totalKbps > 320) {
        totalKbps = 320;
    }
    s->encodeRateKbpsTotal = totalKbps;

    /* Create and configure LAME global flags */
    lame_t gf = lame_init();
    if(!gf){ BAE_PRINTF("audio: MPG_EncodeNewStream lame_init() returned NULL\n"); delete s; return NULL; }
    lame_set_in_samplerate(gf, s->sampleRate);
    lame_set_num_channels(gf, (int)s->channels);
    /* LAME expects overall kbps; use the provided total kbps and snap to nearest supported total kbps */
    int total_kbps = (int)s->encodeRateKbpsTotal;
    total_kbps = pick_nearest_total_kbps(total_kbps);
    lame_set_brate(gf, total_kbps);
    /* Disable VBR by default to match simpler Helix behavior unless caller expects VBR. */
    lame_set_VBR(gf, vbr_off);
    /* Default quality / fast mode */
    lame_set_quality(gf, 5);

    if(lame_init_params(gf) < 0){ BAE_PRINTF("audio: MPG_EncodeNewStream lame_init_params() failed\n"); lame_close(gf); delete s; return NULL; }
    s->gf = gf;

    /* Allocate leftover buffer if frames per call may leave remainder; MP3 frame is 1152 samples */
    if(s->pcmFramesPerCall % 1152){
        uint32_t maxLeft = s->pcmFramesPerCall;
        s->leftoverBuf = (int16_t*)XNewPtr(maxLeft * s->channels * sizeof(int16_t));
        if(!s->leftoverBuf){ BAE_PRINTF("audio: MPG_EncodeNewStream leftoverBuf allocation failed (frames=%u channels=%u)\n", (unsigned)maxLeft, (unsigned)s->channels); lame_close(gf); delete s; return NULL; }
    }

    BAE_PRINTF("audio: MPG_EncodeNewStream using LAME sr=%d ch=%d totalKbps=%d\n", (int)s->sampleRate, (int)s->channels, total_kbps);
    return s;
}

extern "C" void MPG_EncodeSetRefillCallback(void *stream, MPEGFillBufferFn cb, void *userRef){
    LAMEEncoderStream *s = (LAMEEncoderStream*)stream; if(!s) return; s->refill = (MPEGFillBufferInternalFn)cb; s->refillUser = userRef; }

extern "C" uint32_t MPG_EncodeMaxFrames(void *stream){ (void)stream; return 0; }
extern "C" uint32_t MPG_EncodeMaxFrameSize(void *stream){ return MAX_BITSTREAM_SIZE; }

/* Process: call refill, assemble PCM frames into a buffer sized for lame_encode_buffer_interleaved,
   invoke LAME and return produced bytes. */
extern "C" int MPG_EncodeProcess(void *stream, XPTR *pReturnedBuffer, uint32_t *pReturnedSize, XBOOL *pLastFrame){
    if(pLastFrame) *pLastFrame = FALSE;
    if(!stream){ if(pReturnedBuffer) *pReturnedBuffer=NULL; if(pReturnedSize) *pReturnedSize=0; return 0; }
    LAMEEncoderStream *s = (LAMEEncoderStream*)stream;
    if(s->lastFrame){ if(pReturnedBuffer) *pReturnedBuffer=NULL; if(pReturnedSize) *pReturnedSize=0; if(pLastFrame) *pLastFrame=TRUE; return 0; }

    /* Build an interleaved PCM buffer sized to at most 1152 frames (MP3 frame size) */
    const uint32_t targetFrames = 1152U;
    int16_t *workBuf = (int16_t*)XNewPtr((uint32_t)(targetFrames * s->channels * sizeof(int16_t)));
    if(!workBuf){ if(pReturnedBuffer) *pReturnedBuffer=NULL; if(pReturnedSize) *pReturnedSize=0; return 0; }
    uint32_t filled = 0;

    /* consume leftovers first */
    if(s->leftoverFrames){
        uint32_t use = s->leftoverFrames; if(use > targetFrames) use = targetFrames;
        XBlockMove((XPTR)s->leftoverBuf, workBuf, use * s->channels * 2);
        filled += use;
        if(use < s->leftoverFrames){ uint32_t rem = s->leftoverFrames - use; XBlockMove((XPTR)(s->leftoverBuf + use * s->channels), s->leftoverBuf, rem * s->channels * 2); s->leftoverFrames = rem; } else s->leftoverFrames = 0;
    }

    while(filled < targetFrames && !s->lastFrame){
        if(s->refill){
            BAE_PRINTF("audio: MPG_EncodeProcess calling refill (pcmFramesPerCall=%u)\n", (unsigned)s->pcmFramesPerCall);
            XBOOL ok = s->refill(s->pcmBuffer, s->refillUser);
            BAE_PRINTF("audio: MPG_EncodeProcess refill returned %d\n", (int)ok);
            if(!ok){ s->lastFrame = TRUE; break; }
        }
        uint32_t sliceFrames = s->pcmFramesPerCall;
        uint32_t need = targetFrames - filled;
        if(sliceFrames <= need){ XBlockMove((XPTR)s->pcmBuffer, workBuf + filled * s->channels, sliceFrames * s->channels * 2); filled += sliceFrames; }
        else { /* partial use and stash remainder */ XBlockMove((XPTR)s->pcmBuffer, workBuf + filled * s->channels, need * s->channels * 2); filled += need; if(s->leftoverBuf){ uint32_t rem = sliceFrames - need; XBlockMove((XPTR)(s->pcmBuffer + need * s->channels), s->leftoverBuf, rem * s->channels * 2); s->leftoverFrames = rem; } }
    }

    /* pad if short final frame */
    if(filled < targetFrames) { uint32_t pad = targetFrames - filled; XSetMemory((unsigned char*)(workBuf + filled * s->channels), pad * s->channels * 2, 0); }

    int bytesOut = 0;
    if(s->channels == 2){
        BAE_PRINTF("audio: MPG_EncodeProcess calling lame_encode_buffer_interleaved filled=%u\n", filled);
        bytesOut = lame_encode_buffer_interleaved(s->gf, workBuf, (int)targetFrames, s->bitstream, MAX_BITSTREAM_SIZE);
    } else {
        /* mono: pass left channel only expanding to expected API */
        BAE_PRINTF("audio: MPG_EncodeProcess calling lame_encode_buffer mono filled=%u\n", filled);
        bytesOut = lame_encode_buffer(s->gf, workBuf, NULL, (int)targetFrames, s->bitstream, MAX_BITSTREAM_SIZE);
    }

    if (s->lastFrame) {
        /* caller signaled no more refill data; flush final frames */
        BAE_PRINTF("audio: MPG_EncodeProcess lastFrame set, calling lame_encode_flush\n");
        bytesOut = lame_encode_flush(s->gf, s->bitstream, MAX_BITSTREAM_SIZE);
        s->bitstreamBytes = (bytesOut > 0)? (uint32_t)bytesOut : 0;
        BAE_PRINTF("audio: MPG_EncodeProcess flush produced bytes=%u lastFrame=1 leftoverFrames=%u\n", (unsigned)s->bitstreamBytes, (unsigned)s->leftoverFrames);
    } else if (bytesOut < 0) {
        /* LAME returned an error; flush and mark last frame */
        BAE_PRINTF("audio: MPG_EncodeProcess lame error %d, flushing\n", bytesOut);
        bytesOut = lame_encode_flush(s->gf, s->bitstream, MAX_BITSTREAM_SIZE);
        s->bitstreamBytes = (bytesOut > 0)? (uint32_t)bytesOut : 0;
        s->lastFrame = TRUE;
        BAE_PRINTF("audio: MPG_EncodeProcess error flush produced bytes=%u lastFrame=1\n", (unsigned)s->bitstreamBytes);
    } else {
        /* Normal case: bytesOut may be zero (no output yet) or positive */
        s->bitstreamBytes = (bytesOut > 0)? (uint32_t)bytesOut : 0;
        BAE_PRINTF("audio: MPG_EncodeProcess produced bytes=%u lastFrame=0 leftoverFrames=%u\n", (unsigned)s->bitstreamBytes, (unsigned)s->leftoverFrames);
    }
    if(pReturnedBuffer) *pReturnedBuffer = (s->bitstreamBytes? (XPTR)s->bitstream: NULL);
    if(pReturnedSize) *pReturnedSize = s->bitstreamBytes;
    if(pLastFrame) *pLastFrame = s->lastFrame;

    XDisposePtr((XPTR)workBuf);
    return (int) (s->pcmFramesPerCall); /* return consumed input frames (approx) */
}

extern "C" void MPG_EncodeFreeStream(void *stream){
    LAMEEncoderStream *s = (LAMEEncoderStream*)stream; if(!s) return;
    if(s->leftoverBuf) XDisposePtr((XPTR)s->leftoverBuf);
    if(s->gf) lame_close(s->gf);
    delete s;
}

#if USE_MPEG_DECODER == 0
// Provide stub implementations of decoder functions needed by encoder code
// when decoder is disabled. These are used just to get buffer size estimates.
extern "C" XMPEGDecodedData * XOpenMPEGStreamFromMemory(XPTR pBlock, uint32_t blockSize, OPErr *pErr) {
    // Simple stub: create a minimal structure with reasonable estimates
    XMPEGDecodedData *stream = (XMPEGDecodedData*)XNewPtr(sizeof(XMPEGDecodedData));
    if (!stream) {
        if (pErr) *pErr = MEMORY_ERR;
        return NULL;
    }
    
    // Provide reasonable defaults for buffer sizing
    stream->frameBufferSize = 1152 * 2 * 2; // 1152 samples * 2 channels * 2 bytes (16-bit)
    stream->maxFrameBuffers = (blockSize / (stream->frameBufferSize / 8)) + 2; // Rough estimate + padding
    stream->sampleRate = 44100; // Common default
    stream->bitSize = 16;
    stream->channels = 2;
    stream->bitrate = 128000; // 128 kbps default
    stream->lengthInBytes = stream->frameBufferSize * stream->maxFrameBuffers;
    stream->lengthInSamples = stream->lengthInBytes / (stream->channels * 2);
    stream->stream = NULL; // No actual decoder stream
    
    if (pErr) *pErr = NO_ERR;
    return stream;
}

extern "C" OPErr XCloseMPEGStream(XMPEGDecodedData *stream) {
    if (stream) {
        XDisposePtr((XPTR)stream);
    }
    return NO_ERR;
}

// Stub implementation of XFillMPEGStreamBuffer for encoder-only builds
extern "C" OPErr XFillMPEGStreamBuffer(XMPEGDecodedData *stream, void *pcmAudioBuffer, XBOOL *pDone) {
    // Simple stub - this shouldn't be called in normal encoder-only operation
    if (pDone) *pDone = TRUE;
    return PARAM_ERR; // Signal that this is not a real decoder
}

// Additional encoder helper functions needed by XMPEGFilesSun.c
extern "C" XMPEGEncodeRate XGetMPEGEncodeRate(SndCompressionType type) {
    // Map compression types to bitrates
    switch (type) {
        case C_MPEG_32: return (XMPEGEncodeRate)32;
        case C_MPEG_40: return (XMPEGEncodeRate)40;
        case C_MPEG_48: return (XMPEGEncodeRate)48;
        case C_MPEG_56: return (XMPEGEncodeRate)56;
        case C_MPEG_64: return (XMPEGEncodeRate)64;
        case C_MPEG_80: return (XMPEGEncodeRate)80;
        case C_MPEG_96: return (XMPEGEncodeRate)96;
        case C_MPEG_112: return (XMPEGEncodeRate)112;
        case C_MPEG_128: return (XMPEGEncodeRate)128;
        case C_MPEG_160: return (XMPEGEncodeRate)160;
        case C_MPEG_192: return (XMPEGEncodeRate)192;
        case C_MPEG_224: return (XMPEGEncodeRate)224;
        case C_MPEG_256: return (XMPEGEncodeRate)256;
        case C_MPEG_320: return (XMPEGEncodeRate)320;
        default: return (XMPEGEncodeRate)128; // Default to 128 kbps
    }
}

extern "C" SndCompressionType XGetMPEGCompressionType(XMPEGEncodeRate rate) {
    // Map bitrates back to compression types
    switch ((int)rate) {
        case 32: return C_MPEG_32;
        case 40: return C_MPEG_40;
        case 48: return C_MPEG_48;
        case 56: return C_MPEG_56;
        case 64: return C_MPEG_64;
        case 80: return C_MPEG_80;
        case 96: return C_MPEG_96;
        case 112: return C_MPEG_112;
        case 128: return C_MPEG_128;
        case 160: return C_MPEG_160;
        case 192: return C_MPEG_192;
        case 224: return C_MPEG_224;
        case 256: return C_MPEG_256;
        case 320: return C_MPEG_320;
        default: return C_MPEG_128; // Default to 128 kbps
    }
}

extern "C" XFIXED XGetClosestMPEGSampleRate(XFIXED sourceRate, SndCompressionSubType subType) {
    // For simplicity, return common sample rates
    (void)subType; // Unused
    unsigned long rate = XFIXED_TO_UNSIGNED_LONG(sourceRate);
    
    // Common MPEG sample rates
    if (rate <= 8000) return UNSIGNED_LONG_TO_XFIXED(8000);
    if (rate <= 11025) return UNSIGNED_LONG_TO_XFIXED(11025);
    if (rate <= 12000) return UNSIGNED_LONG_TO_XFIXED(12000);
    if (rate <= 16000) return UNSIGNED_LONG_TO_XFIXED(16000);
    if (rate <= 22050) return UNSIGNED_LONG_TO_XFIXED(22050);
    if (rate <= 24000) return UNSIGNED_LONG_TO_XFIXED(24000);
    if (rate <= 32000) return UNSIGNED_LONG_TO_XFIXED(32000);
    if (rate <= 44100) return UNSIGNED_LONG_TO_XFIXED(44100);
    return UNSIGNED_LONG_TO_XFIXED(48000); // Default to 48kHz for higher rates
}

extern "C" void XGetClosestMPEGSampleRateAndEncodeRate(XFIXED inSampleRate, 
                                                      XMPEGEncodeRate inEncodeRate,
                                                      XFIXED *outSampleRate,
                                                      XMPEGEncodeRate *outEncodeRate,
                                                      SndCompressionSubType subType) {
    if (outSampleRate) {
        *outSampleRate = XGetClosestMPEGSampleRate(inSampleRate, subType);
    }
    if (outEncodeRate) {
        *outEncodeRate = inEncodeRate; // Just pass through the encode rate
    }
}
#endif // USE_MPEG_DECODER == 0

#endif /* USE_LAME_ENCODER && USE_MPEG_ENCODER */
