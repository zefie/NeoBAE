// BAE_API_SDL3.c - SDL3 audio backend for miniBAE
// SDL3 platform implementation of subset of BAE_API using SDL_AudioStream callback.
// This is largely a mechanical adaptation of BAE_API_SDL2.c keeping engine logic intact.
// NOTE: SDL3 dramatically changes audio APIs; we bind a stream with a callback that fills it.

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <time.h>
#include "BAE_API.h"
#include <X_API.h>
#include <X_Assert.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#include <sys/types.h>
#endif

#ifndef BAE_PRINTF
#define BAE_PRINTF printf
#endif

// SDL3 objects
static SDL_AudioStream *g_audioStream = NULL;          // primary playback stream
static SDL_AudioDeviceID g_playbackDevice = 0;         // bound device id
static SDL_AudioSpec g_deviceSpec = {0};               // device format actually used

static int g_initialized = 0;
static uint32_t g_sampleRate = 44100;
static uint32_t g_channels = 2;
static uint32_t g_bits = 16; // 8 or 16 only here
static int32_t g_audioByteBufferSize = 0; // bytes per slice
static uint32_t g_framesPerSlice = 0;     // frames per slice (informational only)
static uint64_t g_totalSamplesPlayed = 0; // running counter (approx frames produced per channel aggregated)
static int16_t g_unscaled_volume = 256;   // 0..256 (software only placeholder)
static int16_t g_balance = 0;             // -256..256 (software only placeholder)
static int g_muted = 0;
static uint32_t g_lastCallbackFrames = 0;
static Uint64 g_perfFreq = 0;
static Uint64 g_startTicks = 0;

// Engine forward decls
extern void BAE_BuildMixerSlice(void *threadContext, void *pAudioBuffer, int32_t bufferByteLength, int32_t sampleFrames);
extern int16_t BAE_GetMaxSamplePerSlice(void);

static Uint8 *g_sliceStatic = NULL;
static size_t g_sliceStaticSize = 0;

// PCM recorder state (raw WAV)
static FILE *g_pcm_rec_fp = NULL;
static uint64_t g_pcm_rec_data_bytes = 0;
static uint32_t g_pcm_rec_channels = 0;
static uint32_t g_pcm_rec_sample_rate = 0;
static uint32_t g_pcm_rec_bits = 0;

// FLAC recorder callback
typedef void (*FlacRecorderCallback)(int16_t *left, int16_t *right, int frames);
static FlacRecorderCallback g_flac_recorder_callback = NULL;

#if USE_VORBIS_ENCODER == TRUE
typedef void (*VorbisRecorderCallback)(int16_t *left, int16_t *right, int frames);
static VorbisRecorderCallback g_vorbis_recorder_callback = NULL;
#endif

// MP3 encoder state (same structure copied; rename SDL primitives where changed)
typedef struct MP3EncState_s
{
    uint32_t channels;
    uint32_t sample_rate;
    uint32_t bits;
    uint32_t bitrate;
    XFILE out;
    void *enc;
    uint32_t framesPerCall;
    int16_t *encPcmBuf;
    int16_t *ring;
    uint32_t ringFrames;
    uint32_t readPos;
    uint32_t writePos;
    uint32_t usedFrames;
    SDL_Thread *thread;
    SDL_Mutex *mtx;
    SDL_Condition *cond;
    volatile int accepting;
    volatile int running;
    volatile uint64_t droppedFrames;
} MP3EncState;

static MP3EncState *g_mp3enc = NULL;
static char g_mp3rec_mp3_path[1024] = {0};
static uint32_t g_mp3rec_channels = 0;
static uint32_t g_mp3rec_sample_rate = 0;
static uint32_t g_mp3rec_bits = 0;
static uint32_t g_mp3rec_bitrate = 0;

// ---- Helpers ----
static XBOOL pcm_wav_write_header_local(FILE *f, uint32_t channels, uint32_t sample_rate, uint32_t bits, uint64_t data_bytes)
{
    if (!f) return FALSE;
    uint32_t byte_rate = sample_rate * channels * (bits / 8);
    uint16_t block_align = channels * (bits / 8);
    fwrite("RIFF", 1, 4, f);
    uint32_t chunk_size = (uint32_t)(36 + data_bytes);
    fwrite(&chunk_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t subchunk1_size = 16; fwrite(&subchunk1_size, 4, 1, f);
    uint16_t audio_format = 1; fwrite(&audio_format, 2, 1, f);
    uint16_t num_channels = (uint16_t)channels; fwrite(&num_channels, 2, 1, f);
    uint32_t sr = (uint32_t)sample_rate; fwrite(&sr, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    uint16_t bits_per_sample = (uint16_t)bits; fwrite(&bits_per_sample, 2, 1, f);
    fwrite("data", 1, 4, f);
    uint32_t data_size_32 = (uint32_t)data_bytes; fwrite(&data_size_32, 4, 1, f);
    return TRUE;
}

static void PV_ComputeSliceSizeFromEngine(void)
{
    int16_t maxFrames = BAE_GetMaxSamplePerSlice();
    if (maxFrames <= 0) maxFrames = 512;
    uint32_t frames = (uint32_t)maxFrames;
    if (frames < 64) frames = 64;
    g_framesPerSlice = frames;
    g_audioByteBufferSize = (int32_t)(frames * g_channels * (g_bits / 8));
    g_audioByteBufferSize = (g_audioByteBufferSize + 63) & ~63; // align
    if (g_sliceStaticSize < (size_t)g_audioByteBufferSize)
    {
        free(g_sliceStatic);
        g_sliceStatic = (Uint8 *)calloc(1, (size_t)g_audioByteBufferSize);
        if (g_sliceStatic) g_sliceStaticSize = (size_t)g_audioByteBufferSize; else g_sliceStaticSize = 0;
    }
}

static void PV_UpdateSliceDefaults(void)
{
    if (g_audioByteBufferSize == 0)
    {
        uint32_t frames = (uint32_t)((g_sampleRate * 11ULL) / 1000ULL); // ~11 ms
        if (frames < 64) frames = 64;
        g_framesPerSlice = frames;
        g_audioByteBufferSize = (int32_t)(frames * g_channels * (g_bits / 8));
        g_audioByteBufferSize = (g_audioByteBufferSize + 63) & ~63;
    }
}
static void PV_UpdateSliceSizeIfNeeded(void)
{
    if (g_framesPerSlice == 0 || g_audioByteBufferSize == 0) PV_UpdateSliceDefaults();
}

// New SDL3 style callback: supply additional_amount bytes.
static void SDLCALL audio_stream_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
    (void)total_amount;
    if (g_muted || additional_amount <= 0) return;
    PV_UpdateSliceSizeIfNeeded();
    const int sampleBytes = (g_bits / 8) * (int)g_channels; if (sampleBytes <= 0) return;
    static int sliceValidBytes = 0; static int sliceConsumedBytes = 0;
    int bytesNeeded = additional_amount;
    while (bytesNeeded > 0)
    {
        if (!g_sliceStatic || g_audioByteBufferSize <= 0)
        {
            static Uint8 silence[1024]; int push = bytesNeeded < (int)sizeof(silence)? bytesNeeded : (int)sizeof(silence);
            SDL_PutAudioStreamData(stream, silence, push);
            g_totalSamplesPlayed += (uint64_t)(push / sampleBytes);
            bytesNeeded -= push; continue;
        }
        if (sliceConsumedBytes >= sliceValidBytes)
        {
            int32_t sliceBytes = g_audioByteBufferSize; int32_t frames = sliceBytes / sampleBytes; if (frames <= 0) break;
            BAE_BuildMixerSlice(userdata, g_sliceStatic, sliceBytes, frames);
            if (g_pcm_rec_fp)
            { size_t wrote = fwrite(g_sliceStatic,1,(size_t)sliceBytes,g_pcm_rec_fp); if (wrote == (size_t)sliceBytes) g_pcm_rec_data_bytes += (uint64_t)wrote; }
            if (g_flac_recorder_callback && g_bits == 16)
            { uint32_t framesCB=(uint32_t)frames; int16_t *samples=(int16_t*)g_sliceStatic; if (g_channels==1){ g_flac_recorder_callback(samples,samples,framesCB);} else if (g_channels==2){ static int16_t *l=NULL,*r=NULL; static uint32_t tf=0; if(framesCB>tf){ int16_t *nl=(int16_t*)malloc(framesCB*sizeof(int16_t)); int16_t *nr=(int16_t*)malloc(framesCB*sizeof(int16_t)); if(nl&&nr){ free(l); free(r); l=nl; r=nr; tf=framesCB;} else { free(nl); free(nr);} } if(l&&r){ for(uint32_t i=0;i<framesCB;i++){ l[i]=samples[i*2]; r[i]=samples[i*2+1]; } g_flac_recorder_callback(l,r,framesCB);} } }
#if USE_VORBIS_ENCODER == TRUE
            if (g_vorbis_recorder_callback && g_bits == 16)
            { uint32_t framesCB=(uint32_t)frames; int16_t *samples=(int16_t*)g_sliceStatic; if (g_channels==1){ g_vorbis_recorder_callback(samples,samples,framesCB);} else if (g_channels==2){ static int16_t *l2=NULL,*r2=NULL; static uint32_t tf2=0; if(framesCB>tf2){ int16_t *nl2=(int16_t*)malloc(framesCB*sizeof(int16_t)); int16_t *nr2=(int16_t*)malloc(framesCB*sizeof(int16_t)); if(nl2&&nr2){ free(l2); free(r2); l2=nl2; r2=nr2; tf2=framesCB;} else { free(nl2); free(nr2);} } if(l2&&r2){ for(uint32_t i=0;i<framesCB;i++){ l2[i]=samples[i*2]; r2[i]=samples[i*2+1]; } g_vorbis_recorder_callback(l2,r2,framesCB);} } }
#endif
            if (g_mp3enc && g_mp3enc->accepting)
            { MP3EncState *s=g_mp3enc; uint32_t framesCB=(uint32_t)frames; if(framesCB){ static int16_t *scratch=NULL; static uint32_t scratchFrames=0; int16_t *temp=NULL; if(g_bits==16) temp=(int16_t*)g_sliceStatic; else { if(scratchFrames<framesCB){ free(scratch); scratch=(int16_t*)malloc(framesCB*s->channels*sizeof(int16_t)); scratchFrames=scratch?framesCB:0; } if(!scratch){ s->droppedFrames+=framesCB; goto mp3_done; } const uint8_t *src8=(const uint8_t*)g_sliceStatic; for(uint32_t i=0;i<framesCB*s->channels;i++) scratch[i]=((int)src8[i]-128)<<8; temp=scratch; } SDL_LockMutex(s->mtx); uint32_t space=s->ringFrames - s->usedFrames; uint32_t toWrite=(framesCB<=space)?framesCB:space; if(toWrite>0){ uint32_t first=toWrite; uint32_t cont=s->ringFrames - s->writePos; if(first>cont) first=cont; memcpy(s->ring + s->writePos * s->channels, temp, first * s->channels * sizeof(int16_t)); s->writePos = (s->writePos + first) % s->ringFrames; s->usedFrames += first; uint32_t remain=toWrite-first; if(remain){ memcpy(s->ring + s->writePos * s->channels, temp + first * s->channels, remain * s->channels * sizeof(int16_t)); s->writePos = (s->writePos + remain) % s->ringFrames; s->usedFrames += remain; } SDL_SignalCondition(s->cond); } else { s->droppedFrames += framesCB; } SDL_UnlockMutex(s->mtx); } mp3_done: ; }
            sliceValidBytes = sliceBytes; sliceConsumedBytes = 0; g_lastCallbackFrames = (uint32_t)frames;
        }
        int avail = sliceValidBytes - sliceConsumedBytes; if (avail <= 0) break; int toCopy = (avail < bytesNeeded)? avail : bytesNeeded; SDL_PutAudioStreamData(stream, g_sliceStatic + sliceConsumedBytes, toCopy); sliceConsumedBytes += toCopy; bytesNeeded -= toCopy; g_totalSamplesPlayed += (uint64_t)(toCopy / sampleBytes);
    }
}

// ---- Public platform functions (mirroring SDL2 backend) ----
int BAE_Platform_PCMRecorder_Start(const char *path, uint32_t channels, uint32_t sample_rate, uint32_t bits)
{
    if (!path || g_pcm_rec_fp) return -1;
    FILE *f = fopen(path, "wb+"); if (!f) return -1;
    g_pcm_rec_fp = f; g_pcm_rec_channels = channels; g_pcm_rec_sample_rate = sample_rate; g_pcm_rec_bits = bits; g_pcm_rec_data_bytes = 0;
    pcm_wav_write_header_local(g_pcm_rec_fp, channels, sample_rate, bits, 0);
    fflush(g_pcm_rec_fp);
    BAE_PRINTF("SDL3 PCM recorder started: %s (%u Hz, %u ch, %u bits)\n", path, sample_rate, channels, bits);
    return 0;
}
void BAE_Platform_PCMRecorder_Stop(void)
{
    if (!g_pcm_rec_fp) return;
    fseek(g_pcm_rec_fp, 0, SEEK_SET);
    pcm_wav_write_header_local(g_pcm_rec_fp, g_pcm_rec_channels, g_pcm_rec_sample_rate, g_pcm_rec_bits, g_pcm_rec_data_bytes);
    fflush(g_pcm_rec_fp); fclose(g_pcm_rec_fp); g_pcm_rec_fp = NULL; g_pcm_rec_data_bytes = 0; g_pcm_rec_channels = g_pcm_rec_sample_rate = g_pcm_rec_bits = 0;
    BAE_PRINTF("SDL3 PCM recorder stopped\n");
}
void BAE_Platform_SetFlacRecorderCallback(void (*callback)(int16_t *left, int16_t *right, int frames)) { g_flac_recorder_callback = (FlacRecorderCallback)callback; }
void BAE_Platform_ClearFlacRecorderCallback(void){ g_flac_recorder_callback = NULL; }
#if USE_VORBIS_ENCODER == TRUE
void BAE_Platform_SetVorbisRecorderCallback(void (*callback)(int16_t *left, int16_t *right, int frames)) { g_vorbis_recorder_callback = (VorbisRecorderCallback)callback; }
void BAE_Platform_ClearVorbisRecorderCallback(void){ g_vorbis_recorder_callback = NULL; }
#endif

// ---- System setup / cleanup ----
int BAE_Setup(void){ return 0; }
int BAE_Cleanup(void){ return 0; }

// ---- Memory tracking (simple) ----
static uint32_t g_mem_used = 0; static uint32_t g_mem_used_max = 0;
void *BAE_Allocate(uint32_t size){ void *p = size?calloc(1,size):NULL; if(p){ g_mem_used += size; if(g_mem_used>g_mem_used_max) g_mem_used_max = g_mem_used;} return p; }
void BAE_Deallocate(void *memoryBlock){ if(memoryBlock) free(memoryBlock); }
void BAE_AllocDebug(int debug){ (void)debug; }
uint32_t BAE_GetSizeOfMemoryUsed(void){ return g_mem_used; }
uint32_t BAE_GetMaxSizeOfMemoryUsed(void){ return g_mem_used_max; }
int BAE_IsBadReadPointer(void *memoryBlock, uint32_t size){ (void)memoryBlock; (void)size; return 2; }
uint32_t BAE_SizeOfPointer(void *memoryBlock){ (void)memoryBlock; return 0; }
void BAE_BlockMove(void *source, void *dest, uint32_t size){ if(source && dest && size) memmove(dest, source, size); }

// ---- Audio capabilities ----
int BAE_IsStereoSupported(void){ return 1; }
int BAE_Is8BitSupported(void){ return 1; }
int BAE_Is16BitSupported(void){ return 1; }
int16_t BAE_GetHardwareVolume(void){ return g_unscaled_volume; }
void BAE_SetHardwareVolume(int16_t v){ if(v<0)v=0; if(v>256)v=256; g_unscaled_volume=v; }
int16_t BAE_GetHardwareBalance(void){ return g_balance; }
void BAE_SetHardwareBalance(int16_t b){ if(b<-256)b=-256; if(b>256)b=256; g_balance=b; }

// ---- Timing ----
uint32_t BAE_Microseconds(void){ if(!g_perfFreq){ g_perfFreq = SDL_GetPerformanceFrequency(); g_startTicks = SDL_GetPerformanceCounter(); } Uint64 now = SDL_GetPerformanceCounter(); Uint64 delta = now - g_startTicks; double us = (double)delta * 1000000.0 / (double)g_perfFreq; return (uint32_t)us; }
void BAE_WaitMicroseconds(uint32_t wait){ SDL_Delay((wait + 999)/1000); }

// ---- File helpers (duplicate from SDL2 backend) ----
#define MAX_OPEN_FILES 64
static FILE *g_file_table[MAX_OPEN_FILES] = {0};
static int PV_AllocateFileHandle(FILE *f){ if(!f) return -1; for(int i=1;i<MAX_OPEN_FILES;i++){ if(!g_file_table[i]){ g_file_table[i]=f; return i; } } fclose(f); return -1; }
static FILE *PV_GetFileFromHandle(intptr_t handle){ if(handle>0 && handle<MAX_OPEN_FILES) return g_file_table[handle]; return NULL; }
static void PV_FreeFileHandle(intptr_t handle){ if(handle>0 && handle<MAX_OPEN_FILES) g_file_table[handle]=NULL; }
void BAE_CopyFileNameNative(void *src, void *dst){ if(src && dst) strcpy((char*)dst,(char*)src); }
int32_t BAE_FileCreate(void *fileName){ FILE *f=fopen((char*)fileName,"wb"); if(!f) return -1; fclose(f); return 0; }
int32_t BAE_FileDelete(void *fileName){ return remove((char*)fileName)==0?0:-1; }
intptr_t BAE_FileOpenForRead(void *fileName){ FILE *f=fopen((char*)fileName,"rb"); return f?PV_AllocateFileHandle(f):-1; }
intptr_t BAE_FileOpenForWrite(void *fileName){ FILE *f=fopen((char*)fileName,"wb"); return f?PV_AllocateFileHandle(f):-1; }
intptr_t BAE_FileOpenForReadWrite(void *fileName){ FILE *f=fopen((char*)fileName,"rb+"); if(!f) f=fopen((char*)fileName,"wb+"); return f?PV_AllocateFileHandle(f):-1; }
void BAE_FileClose(intptr_t ref){ FILE *f=PV_GetFileFromHandle(ref); if(f){ fclose(f); PV_FreeFileHandle(ref);} }
int32_t BAE_ReadFile(intptr_t ref, void *pBuf, int32_t len){ FILE *f=PV_GetFileFromHandle(ref); if(!f) return -1; size_t r=fread(pBuf,1,(size_t)len,f); return (r==0 && ferror(f))?-1:(int32_t)r; }
int32_t BAE_WriteFile(intptr_t ref, void *pBuf, int32_t len){ FILE *f=PV_GetFileFromHandle(ref); if(!f) return -1; size_t w=fwrite(pBuf,1,(size_t)len,f); fflush(f); return (w==0 && ferror(f))?-1:(int32_t)w; }
int32_t BAE_SetFilePosition(intptr_t ref, uint32_t pos){ FILE *f=PV_GetFileFromHandle(ref); if(!f) return -1; return fseek(f,(int32_t)pos,SEEK_SET)==0?0:-1; }
uint32_t BAE_GetFilePosition(intptr_t ref){ FILE *f=PV_GetFileFromHandle(ref); if(!f) return 0; long p=ftell(f); if(p<0) return 0; if(p>UINT32_MAX) return UINT32_MAX; return (uint32_t)p; }
uint32_t BAE_GetFileLength(intptr_t ref){ FILE *f=PV_GetFileFromHandle(ref); if(!f) return 0; long cur=ftell(f); if(cur<0) return 0; fseek(f,0,SEEK_END); long end=ftell(f); fseek(f,cur,SEEK_SET); if(end<0) return 0; if(end>UINT32_MAX) return UINT32_MAX; return (uint32_t)end; }
int BAE_SetFileLength(intptr_t ref, uint32_t newSize){
    FILE *f = PV_GetFileFromHandle(ref);
    if (!f) return -1;
#if defined(_WIN32)
    int fd = _fileno(f);
    if (fd < 0) return -1;
#if defined(_MSC_VER) && _MSC_VER >= 1400
    return (_chsize_s(fd, (int64_t)newSize) == 0) ? 0 : -1;
#elif defined(_WIN64)
    return (_chsize(fd, (int64_t)newSize) == 0) ? 0 : -1;
#else
    return (_chsize(fd, (int32_t)newSize) == 0) ? 0 : -1;
#endif
#else
    int fd = fileno(f);
    if (fd < 0) return -1;
    return (ftruncate(fd, (off_t)newSize) == 0) ? 0 : -1;
#endif
}

// ---- Audio buffer metrics ----
int BAE_GetAudioBufferCount(void){ return 1; }
int32_t BAE_GetAudioByteBufferSize(void){ return g_audioByteBufferSize; }

int BAE_AcquireAudioCard(void *threadContext, uint32_t sampleRate, uint32_t channels, uint32_t bits)
{
    if (g_audioStream) return 0; // already
    if (!g_initialized)
    {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { BAE_PRINTF("SDL3 audio init failed: %s\n", SDL_GetError()); return -1; }
        g_initialized = 1;
    }
    g_sampleRate = sampleRate; g_channels = channels; g_bits = bits;
    PV_ComputeSliceSizeFromEngine();

    // Desired spec (SDL3 struct order: format, channels, freq)
    SDL_AudioSpec desired = {0};
    desired.format = (bits == 16)? SDL_AUDIO_S16 : SDL_AUDIO_U8;
    desired.channels = (int)channels;
    desired.freq = (int)sampleRate;
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");

    // Open stream with callback; SDL chooses device format automatically if needed
    g_audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, audio_stream_callback, threadContext);
    if (!g_audioStream)
    {
        BAE_PRINTF("SDL_OpenAudioDeviceStream failed: %s\n", SDL_GetError());
        return -1;
    }
    g_playbackDevice = SDL_GetAudioStreamDevice(g_audioStream);
    SDL_GetAudioDeviceFormat(g_playbackDevice, &g_deviceSpec, NULL); // get actual device format
    /* IMPORTANT:
       SDL3 audio streams automatically convert from the format/frequency we push
       (the 'desired' spec) to the device's native format/frequency. Unlike the
       SDL2 backend where we opened the device directly and had to adapt if SDL
       changed the rate, with streams we should KEEP the engine's mixing rate
       (sampleRate) exactly as passed in so pitch and tempo remain correct.
       Overwriting g_sampleRate with the device's native rate causes the engine
       to size slices using a different rate than it was initialized with, which
       results in audible speed/pitch errors when the device (e.g. 48000 Hz)
       differs from the requested engine rate (e.g. 44100 Hz).
       Therefore we do NOT modify g_sampleRate/g_channels here; we only log.
    */
    if ((uint32_t)g_deviceSpec.freq != g_sampleRate || (uint32_t)g_deviceSpec.channels != g_channels)
    {
        /* Unlike earlier attempt, we will adapt our internal rate if device differs, mirroring SDL2 backend behavior.
           The engine slice sizing is tied to the effective output rate; failing to adapt can cause perceived tempo issues
           if engine timing elsewhere assumes device rate. */
        BAE_PRINTF("SDL3 device adjusted: requested %u Hz/%u ch -> device %d Hz/%d ch. Adapting internal slice.\n",
                   g_sampleRate, g_channels, g_deviceSpec.freq, g_deviceSpec.channels);
        g_sampleRate = (uint32_t)g_deviceSpec.freq;
        g_channels = (uint32_t)g_deviceSpec.channels;
        PV_ComputeSliceSizeFromEngine();
    }
    SDL_ResumeAudioDevice(g_playbackDevice);
    BAE_PRINTF("SDL3 audio active: actual %u Hz (%d req), %u ch (%d req), slice %u frames (%d bytes)\n",
               g_sampleRate, (int)desired.freq, g_channels, (int)desired.channels, g_framesPerSlice, g_audioByteBufferSize);
    return 0;
}

int BAE_ReleaseAudioCard(void *threadContext)
{
    (void)threadContext;
    if (g_audioStream)
    {
        SDL_DestroyAudioStream(g_audioStream);
        g_audioStream = NULL; g_playbackDevice = 0;
    }
    return 0;
}
int BAE_Mute(void){ g_muted = 1; return 0; }
int BAE_Unmute(void){ g_muted = 0; return 0; }
int BAE_IsMuted(void){ return g_muted; }
void BAE_ProcessRouteBus(int currentRoute, int32_t *pChannels, int count){ (void)currentRoute; (void)pChannels; (void)count; }
void BAE_Idle(void *userContext){ (void)userContext; SDL_Delay(1); }
void BAE_UnlockAudioFrameThread(void){}
void BAE_LockAudioFrameThread(void){}
void BAE_BlockAudioFrameThread(void){ SDL_Delay(1); }
uint32_t BAE_GetDeviceSamplesPlayedPosition(void){ return (uint32_t)g_totalSamplesPlayed; }
int32_t BAE_MaxDevices(void){ return 1; }
void BAE_SetDeviceID(int32_t deviceID, void *deviceParameter){ (void)deviceID; (void)deviceParameter; }
int32_t BAE_GetDeviceID(void *deviceParameter){ (void)deviceParameter; return 0; }
void BAE_GetDeviceName(int32_t deviceID, char *cName, uint32_t cNameLength){ (void)deviceID; if(cName && cNameLength) snprintf(cName, cNameLength, "SDL3,stream,callback"); }

// ---- Threading / frame thread stubs ----
int BAE_CreateFrameThread(void *threadContext, BAE_FrameThreadProc proc){ (void)threadContext; (void)proc; return 0; }
int BAE_SetFrameThreadPriority(void *threadContext, int priority){ (void)threadContext; (void)priority; return 0; }
int BAE_DestroyFrameThread(void *threadContext){ (void)threadContext; return 0; }
int BAE_SleepFrameThread(void *threadContext, int32_t msec){ (void)threadContext; SDL_Delay((Uint32)msec); return 0; }

// ---- Mutex wrapper ----
typedef struct { SDL_Mutex *mtx; } sSDLMutex;
int BAE_NewMutex(BAE_Mutex *lock, char *name, char *file, int lineno){ (void)name; (void)file; (void)lineno; sSDLMutex *m = (sSDLMutex*)BAE_Allocate(sizeof(sSDLMutex)); if(!m) return 0; m->mtx = SDL_CreateMutex(); if(!m->mtx){ BAE_Deallocate(m); return 0; } *lock = (BAE_Mutex)m; return 1; }
void BAE_AcquireMutex(BAE_Mutex lock){ if(!lock) return; sSDLMutex *m = (sSDLMutex*)lock; SDL_LockMutex(m->mtx); }
void BAE_ReleaseMutex(BAE_Mutex lock){ if(!lock) return; sSDLMutex *m = (sSDLMutex*)lock; SDL_UnlockMutex(m->mtx); }
void BAE_DestroyMutex(BAE_Mutex lock){ if(!lock) return; sSDLMutex *m=(sSDLMutex*)lock; if(m->mtx) SDL_DestroyMutex(m->mtx); BAE_Deallocate(m); }

// ---- Capture stubs (not implemented) ----
int BAE_AcquireAudioCapture(void *threadContext, uint32_t sampleRate, uint32_t channels, uint32_t bits, uint32_t *pCaptureHandle){ (void)threadContext; (void)sampleRate; (void)channels; (void)bits; (void)pCaptureHandle; return -1; }
int BAE_ReleaseAudioCapture(void *threadContext){ (void)threadContext; return -1; }
int BAE_StartAudioCapture(BAE_CaptureDone done, void *callbackContext){ (void)done; (void)callbackContext; return -1; }
int BAE_StopAudioCapture(void){ return -1; }
int BAE_PauseAudioCapture(void){ return -1; }
int BAE_ResumeAudioCapture(void){ return -1; }
int32_t BAE_MaxCaptureDevices(void){ return 0; }
void BAE_SetCaptureDeviceID(int32_t deviceID, void *deviceParameter){ (void)deviceID; (void)deviceParameter; }
int32_t BAE_GetCaptureDeviceID(void *deviceParameter){ (void)deviceParameter; return -1; }
void BAE_GetCaptureDeviceName(int32_t deviceID, char *cName, uint32_t cNameLength){ (void)deviceID; if(cName && cNameLength) *cName='\0'; }
uint32_t BAE_GetCaptureBufferSizeInFrames(){ return 0; }
int BAE_GetCaptureBufferCount(){ return 0; }
uint32_t BAE_GetDeviceSamplesCapturedPosition(){ return 0; }

// ---- Misc ----
void BAE_DisplayMemoryUsage(int detailLevel){ (void)detailLevel; }
void BAE_PrintHexDump(void *address, int32_t length){ unsigned char *p=(unsigned char*)address; for(int32_t i=0;i<length;i++){ if((i%16)==0) BAE_PRINTF("\n%08x: ",(uint32_t)i); BAE_PRINTF("%02X ", p[i]); } BAE_PRINTF("\n"); }

#if USE_MPEG_ENCODER != FALSE
#include "XMPEG_BAE_API.h"
static XBOOL MP3Refill_FromRing(void *buffer, void *userRef)
{ if(!buffer || !userRef) return FALSE; MP3EncState *s = (MP3EncState*)userRef; int16_t *dst=(int16_t*)buffer; const uint32_t needFrames = s->framesPerCall; uint32_t copied=0; SDL_LockMutex(s->mtx); while(copied < needFrames){ while(s->usedFrames==0){ if(!s->running){ if(copied>0){ uint32_t pad=(needFrames-copied)*s->channels; memset(dst+copied*s->channels,0,pad*sizeof(int16_t)); SDL_UnlockMutex(s->mtx); return TRUE; } SDL_UnlockMutex(s->mtx); return FALSE; } SDL_WaitCondition(s->cond, s->mtx); } uint32_t cont = s->ringFrames - s->readPos; uint32_t canRead = (s->usedFrames < cont)? s->usedFrames : cont; uint32_t want = needFrames - copied; if (canRead > want) canRead = want; memcpy(dst + copied * s->channels, s->ring + s->readPos * s->channels, canRead * s->channels * sizeof(int16_t)); s->readPos = (s->readPos + canRead) % s->ringFrames; s->usedFrames -= canRead; copied += canRead; } SDL_UnlockMutex(s->mtx); return TRUE; }
static int MP3EncoderThread(void *userdata)
{ MP3EncState *s = (MP3EncState*)userdata; if(!s) return 0; s->encPcmBuf = (int16_t*)XNewPtr(s->framesPerCall * s->channels * sizeof(int16_t)); if(!s->encPcmBuf) return 0; s->enc = MPG_EncodeNewStream(s->bitrate, s->sample_rate, s->channels, (XPTR)s->encPcmBuf, s->framesPerCall); if(!s->enc){ XDisposePtr((XPTR)s->encPcmBuf); s->encPcmBuf=NULL; return 0; } MPG_EncodeSetRefillCallback(s->enc, MP3Refill_FromRing, s); for(;;){ XPTR bitbuf=NULL; uint32_t bitsz=0; XBOOL last=FALSE; (void)MPG_EncodeProcess(s->enc, &bitbuf, &bitsz, &last); if(bitsz>0 && bitbuf){ XFileWrite(s->out, bitbuf, (int32_t)bitsz); } if(last && bitsz==0) break; SDL_Delay(1); } MPG_EncodeFreeStream(s->enc); s->enc=NULL; if(s->encPcmBuf){ XDisposePtr((XPTR)s->encPcmBuf); s->encPcmBuf=NULL; } return 0; }
#endif

int BAE_Platform_MP3Recorder_Start(const char *path, uint32_t channels, uint32_t sample_rate, uint32_t bits, uint32_t bitrate)
{
    if(!path || g_mp3enc) return -1;
    snprintf(g_mp3rec_mp3_path,sizeof(g_mp3rec_mp3_path),"%s", path);
    g_mp3rec_channels = channels; g_mp3rec_sample_rate = sample_rate; g_mp3rec_bits = bits; g_mp3rec_bitrate = bitrate;
#if USE_MPEG_ENCODER == FALSE
    BAE_PRINTF("MP3 encode skipped: encoder not built\n"); return -1;
#else
    MP3EncState *s = (MP3EncState*)calloc(1,sizeof(MP3EncState)); if(!s) return -1;
    s->channels = channels; s->sample_rate = sample_rate; s->bits = bits; s->bitrate = bitrate; s->framesPerCall = 1152; s->ringFrames = (sample_rate?sample_rate:44100)*2; s->ring = (int16_t*)calloc((size_t)s->ringFrames * channels, sizeof(int16_t)); s->mtx = SDL_CreateMutex(); s->cond = SDL_CreateCondition(); s->running = 1; s->accepting = 1;
    if(!s->ring || !s->mtx || !s->cond){ if(s->ring) free(s->ring); if(s->mtx) SDL_DestroyMutex(s->mtx); if(s->cond) SDL_DestroyCondition(s->cond); free(s); return -1; }
    XFILENAME xfOut; XConvertPathToXFILENAME((void*)g_mp3rec_mp3_path, &xfOut); s->out = XFileOpenForWrite(&xfOut, TRUE); if(!s->out){ SDL_DestroyCondition(s->cond); SDL_DestroyMutex(s->mtx); free(s->ring); free(s); return -1; }
    s->thread = SDL_CreateThread(MP3EncoderThread, "mp3enc", s); if(!s->thread){ XFileClose(s->out); SDL_DestroyCondition(s->cond); SDL_DestroyMutex(s->mtx); free(s->ring); free(s); return -1; }
    g_mp3enc = s; BAE_PRINTF("SDL3 MP3 recorder started: %s (%u Hz, %u ch, %u bits, %u bps)\n", g_mp3rec_mp3_path, sample_rate, channels, bits, bitrate); return 0;
#endif
}
void BAE_Platform_MP3Recorder_Stop(void)
{
    MP3EncState *s = g_mp3enc; if(!s) return; s->accepting = 0; SDL_LockMutex(s->mtx); s->running = 0; SDL_BroadcastCondition(s->cond); SDL_UnlockMutex(s->mtx); if(s->thread){ SDL_WaitThread(s->thread, NULL); s->thread=NULL; }
    unsigned long long dropped = (unsigned long long)s->droppedFrames; if(s->out){ XFileClose(s->out); s->out=NULL; } if(s->cond){ SDL_DestroyCondition(s->cond); s->cond=NULL; } if(s->mtx){ SDL_DestroyMutex(s->mtx); s->mtx=NULL; } if(s->ring){ free(s->ring); s->ring=NULL; } free(s); g_mp3enc=NULL; g_mp3rec_mp3_path[0]='\0'; g_mp3rec_channels=g_mp3rec_sample_rate=g_mp3rec_bits=g_mp3rec_bitrate=0; BAE_PRINTF("SDL3 MP3 recorder stopped. Dropped frames: %llu\n", dropped);
}

// (End of SDL3 backend)
