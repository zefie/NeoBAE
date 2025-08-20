/*
 * XMPEG_hmp3_encoder.cpp
 * Helix MP3 encoder adapter implementing legacy MPG_Encode* API
 * Only built when USE_HMP3_ENCODER (and USE_MPEG_ENCODER) are defined.
 */

#if defined(USE_HMP3_ENCODER) && (USE_MPEG_ENCODER!=0)

#include "XMPEG_BAE_API.h" // for prototypes / types
#include "X_API.h"
#include "X_Formats.h"
#include "X_Assert.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Helix public headers */
#include "mp3enc.h"
#include "encapp.h"
#include <new> // for std::nothrow

#define MAX_BITSTREAM_SIZE 4096

/* Refill callback signature reused from legacy API */
typedef XBOOL (*MPEGFillBufferInternalFn)(void *buffer, void *userRef);

struct HMP3EncoderStream {
    CMp3Enc         enc;            /* Helix encoder instance */
    E_CONTROL       ec;             /* control params */
    uint32_t        sampleRate;     /* Hz */
    uint32_t        sourceSampleRate; /* original PCM sample rate provided by caller */
    uint32_t        channels;       /* 1 or 2 */
    uint32_t        encodeRateKbpsPerChan; /* kbps per channel */
    int16_t        *pcmBuffer;      /* points to caller-provided PCM slice buffer */
    uint32_t        pcmFramesPerCall; /* frames per PCM slice (should be >= 1152 for MP3) */
    uint32_t        frameFrames;    /* MP3 frame size (1152) */
    MPEGFillBufferInternalFn refill; 
    void           *refillUser;
    unsigned char  *convertBuf;     /* staging buffer passed to Helix */
    size_t          convertBytes;   /* size of convertBuf (>= frameFrames*channels*2) */
    unsigned char   bitstream[MAX_BITSTREAM_SIZE];
    uint32_t        bitstreamBytes;
    XBOOL           lastFrame;
    /* Leftover handling when pcmFramesPerCall does not align to frameFrames */
    int16_t        *leftoverBuf;    /* stores remainder PCM frames from previous slice */
    uint32_t        leftoverFrames; /* frames currently stored in leftoverBuf */
    /* For simple resampling when source rate is unsupported by Helix
     * (we upscale 8000 and 11025 to 16000). Stored as interleaved int16 samples. */
    int16_t        *srcBuf;         /* accumulated source PCM frames (interleaved) */
    uint32_t        srcBufFrames;   /* number of frames currently in srcBuf */
    uint32_t        srcBufCapacity; /* capacity in frames */
};

/* Compute per-channel kbps (Helix expects total in ec.bitrate later). */
static int map_bps_to_kbps_per_channel(uint32_t bpsPerChan){
    if (bpsPerChan < 8000) bpsPerChan = 8000; /* floor */
    int kc = (int)(bpsPerChan / 1000);
    if(kc < 8) kc = 8; if(kc > 320) kc = 320; /* per-channel guard; total capped separately */
    return kc;
}

/* Public API implementations */

extern "C" void * MPG_EncodeNewStream(uint32_t encodeRate /* bits/sec per channel */, uint32_t sampleRate, uint32_t channels, XPTR pSampleData16Bits, uint32_t frames){
    if(channels == 0 || channels > 2) return NULL;
    HMP3EncoderStream *s = new (std::nothrow) HMP3EncoderStream();
    if(!s) return NULL;
    s->sampleRate = sampleRate;
    s->sourceSampleRate = sampleRate; /* remember original requested rate */
    s->channels = channels;
    /*
     * The legacy API comment said encodeRate is "bits/sec per channel",
     * but callers in this tree historically sometimes pass TOTAL bits/sec.
     * Be defensive: detect whether the provided value is a known per-channel
     * bitrate or a total bitrate (per-channel * channels) and normalize to
     * per-channel bits/sec before mapping to kbps per channel.
     */
    {
        uint32_t provided = encodeRate;
        uint32_t perChanBits = 0;
        /* Valid per-channel bitrates used by this codebase (bits/sec). */
        const uint32_t validPerChan[] = {32000,40000,48000,56000,64000,80000,96000,112000,128000,160000,192000,224000,256000,320000};
        size_t i;
        /* If provided exactly matches a per-channel rate, accept it. */
        for(i=0;i<sizeof(validPerChan)/sizeof(validPerChan[0]);++i){ if(provided == validPerChan[i]){ perChanBits = provided; break; } }
        /* If provided matches a total (per-channel * channels), divide to get per-channel. */
        if(perChanBits == 0){ for(i=0;i<sizeof(validPerChan)/sizeof(validPerChan[0]);++i){ if(provided == validPerChan[i] * (uint32_t)channels){ perChanBits = validPerChan[i]; break; } } }
        /* Fallback heuristics: if provided is larger than max per-channel, assume total and divide. */
        if(perChanBits == 0){ if(provided > 320000) perChanBits = provided / (uint32_t)channels; }
        /* Final fallback: treat as per-channel value as given. */
        if(perChanBits == 0) perChanBits = provided;
        s->encodeRateKbpsPerChan = (uint32_t)map_bps_to_kbps_per_channel(perChanBits);
        /* Log interpretation */
        {
            char tmp[64];
            BAE_PRINTF("audio: MPG_EncodeNewStream providedBits=%dbps interpretedPerChan=%dkbps channels=%d\n", provided, s->encodeRateKbpsPerChan, (int)channels);
        }
    }
    s->pcmBuffer = (int16_t*)pSampleData16Bits;
    s->pcmFramesPerCall = frames; /* may be 1152 (preferred) */
    s->frameFrames = 1152;
    s->refill = NULL; s->refillUser = NULL;
    s->bitstreamBytes = 0; s->lastFrame = FALSE;
    s->leftoverBuf = NULL; s->leftoverFrames = 0;
    s->srcBuf = NULL; s->srcBufFrames = 0; s->srcBufCapacity = 0;

    /* If incoming mixer/sample rate is one we prefer to upsample (8000 or 11025),
     * use 16000 as the encoder sample rate and arrange to resample on-the-fly.
     */
    if(s->sourceSampleRate == 8000 || s->sourceSampleRate == 11025){
        s->sampleRate = 16000; /* use 16k for encoder */
        /* Pre-allocate a small src buffer capacity (enough for a few frames). */
        s->srcBufCapacity = s->frameFrames * 2; /* hold up to two frames worth of input */
    s->srcBuf = (int16_t*)XNewPtr((uint32_t)(s->srcBufCapacity * s->channels * sizeof(int16_t)));
        if(!s->srcBuf){ delete s; return NULL; }
    BAE_PRINTF("audio: MPG_EncodeNewStream will resample %d->%d (channels=%d)\n", s->sourceSampleRate, s->sampleRate, (int)s->channels);
    }

    /* Fill control struct */
    E_CONTROL ec; XSetMemory(&ec, sizeof(ec), 0xFF);
    ec.mode = (channels == 1) ? 3 : 0;
    {
        /* E_CONTROL.bitrate expects per-channel kbps (in 1000's). Use the
         * already-normalized per-channel value. Clamp per-channel to 320.
         */
        int perChan = (int)s->encodeRateKbpsPerChan;
        if(perChan > 320) perChan = 320;
        ec.bitrate = perChan;
    }
    /* Use the effective encoder sample rate (may differ from caller's mixer rate
     * if we perform internal upsampling for unsupported rates). */
    ec.samprate = (int)s->sampleRate; ec.nsbstereo = -1; ec.filter_select = -1; ec.freq_limit = 24000; ec.nsb_limit = -1;
    BAE_PRINTF("audio: MPG_EncodeNewStream control.samprate=%d (source=%d)\n", ec.samprate, (int)s->sourceSampleRate);
    ec.layer = 3; ec.cr_bit = 0; ec.original = 1; ec.hf_flag = 0; ec.vbr_flag = 0;
    ec.vbr_mnr = 50; ec.vbr_br_limit = 160; ec.vbr_delta_mnr = 0; ec.chan_add_f0 = 24000; ec.chan_add_f1 = 24000;
    ec.sparse_scale = -1; for(int i=0;i<21;i++) ec.mnr_adjust[i]=0; ec.cpu_select=0; ec.quick=1; ec.test1=-1; ec.test2=0; ec.test3=0; ec.short_block_threshold=700;
    s->ec = ec;

    int initBytes = s->enc.MP3_audio_encode_init(&s->ec, 16, 0, 0, 0);
    if(initBytes <= 0){
        /* Emit diagnostic information to help debug why Helix init failed for
         * certain bitrates/sample-rate/channel combinations. This will print
         * the interpreted per-channel kbps, requested sample rate, channels,
         * pcm slice size and computed frameBytes so users can correlate. */
        char dbg[256];
        size_t frameBytes = (size_t)s->frameFrames * s->channels * sizeof(int16_t);
        BAE_PRINTF("audio: MPG_EncodeNewStream FAILED initBytes=%d\n", initBytes);
        BAE_PRINTF("audio: MPG_EncodeNewStream params: sampleRate=%d channels=%d perChanKbps=%d pcmFramesPerCall=%u frameFrames=%u frameBytes=%lu convertBytes=%lu\n",
                   (int)s->sampleRate, (int)s->channels, (int)s->encodeRateKbpsPerChan, (unsigned int)s->pcmFramesPerCall, (unsigned int)s->frameFrames, (unsigned long)frameBytes, (unsigned long)s->convertBytes);
        if(s->convertBuf) BAE_PRINTF("audio: MPG_EncodeNewStream convertBuf present\n"); else BAE_PRINTF("audio: MPG_EncodeNewStream convertBuf NULL\n");
        delete s; return NULL; }
    size_t frameBytes = (size_t)s->frameFrames * s->channels * sizeof(int16_t); /* 1152 * ch * 2 */
    size_t allocBytes = (initBytes > (int)frameBytes)? (size_t)initBytes : frameBytes;
    s->convertBuf = (unsigned char*)XNewPtr((uint32_t)allocBytes);
    if(!s->convertBuf){ delete s; return NULL; }
    s->convertBytes = allocBytes;
    /* Allocate leftover buffer only if slice size can produce remainder */
    if(s->pcmFramesPerCall % s->frameFrames){
        uint32_t maxLeftFrames = s->pcmFramesPerCall; /* worst-case leftover (almost a full slice) */
        s->leftoverBuf = (int16_t*)XNewPtr(maxLeftFrames * s->channels * sizeof(int16_t));
        if(!s->leftoverBuf){ XDisposePtr((XPTR)s->convertBuf); delete s; return NULL; }
    }
    /* Log */
    char tmp[32];
    BAE_PRINTF("audio: MPG_EncodeNewStream create ch="); XLongToStr(tmp,(int32_t)s->channels); BAE_PRINTF(tmp);
    BAE_PRINTF(" framesPerCall="); XLongToStr(tmp,(int32_t)s->pcmFramesPerCall); BAE_PRINTF(tmp);
    BAE_PRINTF(" sr="); XLongToStr(tmp,(int32_t)s->sampleRate); BAE_PRINTF(tmp);
    BAE_PRINTF(" perChanKbps="); XLongToStr(tmp,(int32_t)s->encodeRateKbpsPerChan); BAE_PRINTF(tmp);
    BAE_PRINTF(" helixTotalKbps="); XLongToStr(tmp,(int32_t)ec.bitrate); BAE_PRINTF(tmp);
    BAE_PRINTF(" initBytes="); XLongToStr(tmp,(int32_t)initBytes); BAE_PRINTF(tmp);
    BAE_PRINTF(" frameBytes="); XLongToStr(tmp,(int32_t)frameBytes); BAE_PRINTF(tmp); BAE_PRINTF("\n");
    return s;
}

extern "C" void MPG_EncodeSetRefillCallback(void *stream, MPEGFillBufferFn cb, void *userRef){
    HMP3EncoderStream *s = (HMP3EncoderStream*)stream; if(!s) return; s->refill = (MPEGFillBufferInternalFn)cb; s->refillUser = userRef; }

extern "C" uint32_t MPG_EncodeMaxFrames(void *stream){ (void)stream; return 0; }
extern "C" uint32_t MPG_EncodeMaxFrameSize(void *stream){ return MAX_BITSTREAM_SIZE; }

/* Process: call refill, convert PCM to bytes for Helix, run encode, return produced bytes. */
extern "C" int MPG_EncodeProcess(void *stream, XPTR *pReturnedBuffer, uint32_t *pReturnedSize, XBOOL *pLastFrame){
    if(pLastFrame) *pLastFrame = FALSE;
    if(!stream){ if(pReturnedBuffer) *pReturnedBuffer=NULL; if(pReturnedSize) *pReturnedSize=0; return 0; }
    HMP3EncoderStream *s = (HMP3EncoderStream*)stream;
    if(!s->convertBuf || s->convertBytes==0){ if(pReturnedBuffer) *pReturnedBuffer=NULL; if(pReturnedSize) *pReturnedSize=0; return 0; }
    if(s->lastFrame){ if(pReturnedBuffer) *pReturnedBuffer=NULL; if(pReturnedSize) *pReturnedSize=0; if(pLastFrame) *pLastFrame=TRUE; return 0; }
    uint32_t frameFrames = s->frameFrames;
    uint32_t collected = 0;
    /* Step 1: consume any leftover frames from previous call */
    if(s->leftoverFrames){
        uint32_t use = s->leftoverFrames;
        if(use > frameFrames) use = frameFrames; /* safety */
        XBlockMove((XPTR)s->leftoverBuf, s->convertBuf, use * s->channels * 2);
        collected += use;
        if(use < s->leftoverFrames){
            /* Shift remaining leftovers down */
            uint32_t rem = s->leftoverFrames - use;
            XBlockMove((XPTR)(s->leftoverBuf + use * s->channels), (XPTR)s->leftoverBuf, rem * s->channels * 2);
            s->leftoverFrames = rem;
        } else {
            s->leftoverFrames = 0;
        }
    }
    /* Step 2: fetch new slices until frame filled */
    while(collected < frameFrames && !s->lastFrame){
        if(s->refill){
            XBOOL ok = s->refill(s->pcmBuffer, s->refillUser);
            if(!ok){ s->lastFrame = TRUE; break; }
        }
        uint32_t sliceFrames = s->pcmFramesPerCall;
        uint32_t need = frameFrames - collected;
        if(s->sourceSampleRate == s->sampleRate || s->srcBuf == NULL){
            /* No resampling required: copy into convertBuf directly (legacy path) */
            if(sliceFrames <= need){
                /* Copy whole slice */
                XBlockMove((XPTR)s->pcmBuffer, s->convertBuf + collected * s->channels * 2, sliceFrames * s->channels * 2);
                collected += sliceFrames;
            } else {
                /* Copy partial slice and stash remainder */
                XBlockMove((XPTR)s->pcmBuffer, s->convertBuf + collected * s->channels * 2, need * s->channels * 2);
                collected += need;
                if(s->leftoverBuf){
                    uint32_t rem = sliceFrames - need;
                    XBlockMove((XPTR)(s->pcmBuffer + need * s->channels), s->leftoverBuf, rem * s->channels * 2);
                    s->leftoverFrames = rem;
                }
            }
        } else {
            /* Resampling path: append incoming slice into srcBuf (expanding capacity if needed) */
            uint32_t needFramesForSrc = sliceFrames;
            uint32_t curFrames = s->srcBufFrames;
            if(curFrames + needFramesForSrc > s->srcBufCapacity){
                /* grow capacity */
                uint32_t newCap = (curFrames + needFramesForSrc) * 2;
                int16_t *newBuf = (int16_t*)XNewPtr((uint32_t)(newCap * s->channels * sizeof(int16_t)));
                if(!newBuf){ /* allocation failed: abort encoding by marking lastFrame */ s->lastFrame = TRUE; break; }
                if(s->srcBuf && curFrames) XBlockMove((XPTR)s->srcBuf, newBuf, curFrames * s->channels * 2);
                XDisposePtr((XPTR)s->srcBuf);
                s->srcBuf = newBuf; s->srcBufCapacity = newCap;
            }
            /* copy the slice into srcBuf */
            XBlockMove((XPTR)s->pcmBuffer, s->srcBuf + curFrames * s->channels, sliceFrames * s->channels * 2);
            s->srcBufFrames = curFrames + sliceFrames;
            /* Now attempt to produce enough resampled frames to fill convertBuf */
            /* compute resample ratio: srcRate -> dstRate (s->sourceSampleRate -> s->sampleRate) */
            double srcRate = (double)s->sourceSampleRate;
            double dstRate = (double)s->sampleRate;
            double ratio = dstRate / srcRate; /* e.g., 16000/11025 ~= 1.451 */
            /* We need 'need' frames of dst; compute required src frames (ceil) */
            uint32_t wantDst = need;
            uint32_t reqSrc = (uint32_t)ceil((wantDst) / ratio) + 2; /* +2 for interpolation guard */
            if(s->srcBufFrames >= reqSrc){
                /* perform linear resampling from srcBuf into convertBuf+collected */
                for(uint32_t di=0; di < wantDst; ++di){
                    double srcPos = di / ratio; /* fractional source frame index */
                    uint32_t i0 = (uint32_t)floor(srcPos);
                    double frac = srcPos - (double)i0;
                    if(i0 + 1 >= s->srcBufFrames) { /* guard */ frac = 0.0; }
                    for(uint32_t ch=0; ch < s->channels; ++ch){
                        int16_t s0 = s->srcBuf[(i0 * s->channels) + ch];
                        int16_t s1 = s->srcBuf[((i0+1) * s->channels) + ch];
                        int32_t val = (int32_t)((1.0 - frac) * s0 + frac * s1 + 0.5);
                        /* write into convertBuf (bytes) */
                        int16_t *dst = (int16_t*)(s->convertBuf + (collected + di) * s->channels * sizeof(int16_t));
                        dst[ch] = (int16_t)val;
                    }
                }
                /* consume used src frames from srcBuf by shifting remaining */
                uint32_t consumedSrc = (uint32_t)floor(((double)wantDst) / ratio);
                if(consumedSrc > s->srcBufFrames) consumedSrc = s->srcBufFrames;
                uint32_t rem = s->srcBufFrames - consumedSrc;
                if(rem){ XBlockMove((XPTR)(s->srcBuf + consumedSrc * s->channels), s->srcBuf, rem * s->channels * 2); }
                s->srcBufFrames = rem;
                collected += wantDst;
            } else {
                /* Not enough source data yet; continue refill loop */
                continue;
            }
        }
    }
    /* Step 3: pad if end-of-stream produced short final frame */
    if(collected < frameFrames){
        uint32_t pad = frameFrames - collected;
        XSetMemory(s->convertBuf + collected * s->channels * 2, pad * s->channels * 2, 0);
    }
    /* Zero any extra tail beyond exact frameBytes inside convertBuf (Helix requested larger) */
    size_t frameBytes = (size_t)frameFrames * s->channels * 2;
    if(s->convertBytes > frameBytes){ XSetMemory(s->convertBuf + frameBytes, (uint32_t)(s->convertBytes - frameBytes), 0); }
    IN_OUT io = s->enc.MP3_audio_encode(s->convertBuf, s->bitstream);
    s->bitstreamBytes = (uint32_t)io.out_bytes;
    if(pReturnedBuffer) *pReturnedBuffer = (s->bitstreamBytes? (XPTR)s->bitstream: NULL);
    if(pReturnedSize) *pReturnedSize = s->bitstreamBytes;
    return (int)io.in_bytes;
}

extern "C" void MPG_EncodeFreeStream(void *stream){
    HMP3EncoderStream *s = (HMP3EncoderStream*)stream; if(!s) return;
    if(s->convertBuf) XDisposePtr((XPTR)s->convertBuf);
    if(s->leftoverBuf) XDisposePtr((XPTR)s->leftoverBuf);
    if(s->srcBuf) XDisposePtr((XPTR)s->srcBuf);
    delete s;
}

/* Legacy unused in this adapter */
extern "C" void MPG_EncodeSetRefillCallback(void *stream, MPEGFillBufferFn callback, void *userRef);

#endif /* USE_HMP3_ENCODER && USE_MPEG_ENCODER */
