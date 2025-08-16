// BAE_API_SDL2.c - SDL2 audio backend for miniBAE
// SDL2 platform implementation of subset of BAE_API.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "BAE_API.h"
#include <X_API.h>
#include <X_Assert.h>
#ifdef _WIN32
#include <io.h> // for _chsize / _chsize_s / _fileno
#else
#include <unistd.h> // for ftruncate
#include <sys/types.h>
#endif

#ifndef BAE_PRINTF
#define BAE_PRINTF printf
#endif

static SDL_AudioDeviceID g_audioDevice = 0;
static SDL_AudioSpec g_have;
static int g_initialized = 0;
static unsigned long g_sampleRate = 44100;
static unsigned long g_channels = 2;
static unsigned long g_bits = 16;
static long g_audioByteBufferSize = 0; // bytes per slice
static unsigned long g_framesPerSlice = 0; // frames per slice (approx) (informational only)
static unsigned long g_totalSamplesPlayed = 0; // running counter (approx)
static short int g_unscaled_volume = 256; // 0..256
static short int g_balance = 0; // -256..256
static int g_muted = 0;
static unsigned long g_lastCallbackFrames = 0;
static Uint64 g_perfFreq = 0;
static Uint64 g_startTicks = 0;

// Mutex wrapper
typedef struct { SDL_mutex *mtx; } sSDLMutex;

// Forward from engine
extern void BAE_BuildMixerSlice(void *threadContext, void *pAudioBuffer, long bufferByteLength, long sampleFrames);
extern short BAE_GetMaxSamplePerSlice(void); // ensure visible for sizing

static Uint8 *g_sliceStatic = NULL;
static size_t g_sliceStaticSize = 0;

// Recompute slice size using engine max slice heuristic (avoids runtime realloc and undefined globals)
static void PV_ComputeSliceSizeFromEngine(void) {
    short maxFrames44 = BAE_GetMaxSamplePerSlice();
    if (maxFrames44 <= 0) maxFrames44 = 512; // fallback
    // scale frames for current sample rate relative to 44100 baseline assumption
    unsigned long frames = (unsigned long)maxFrames44;
    if (g_sampleRate != 44100 && g_sampleRate > 0) {
        frames = (unsigned long)(( (unsigned long long)frames * g_sampleRate + 22050) / 44100);
    }
    if (frames < 64) frames = 64;
    g_framesPerSlice = frames;
    g_audioByteBufferSize = (long)(frames * g_channels * (g_bits/8));
    g_audioByteBufferSize = (g_audioByteBufferSize + 63) & ~63;
    if (g_sliceStaticSize < (size_t)g_audioByteBufferSize) {
        free(g_sliceStatic);
        g_sliceStatic = (Uint8*)calloc(1, (size_t)g_audioByteBufferSize);
        if (g_sliceStatic) g_sliceStaticSize = (size_t)g_audioByteBufferSize;
    }
}

/* Query core slice timing lazily so we match engine's configured buffer size */
static void PV_UpdateSliceDefaults(void) {
    if (g_audioByteBufferSize == 0) {
        /* Fallback ~11ms slice */
        unsigned long frames = (unsigned long)((g_sampleRate * 11ULL) / 1000ULL);
        if (frames < 64) frames = 64;
        g_framesPerSlice = frames;
        g_audioByteBufferSize = (long)(frames * g_channels * (g_bits/8));
        g_audioByteBufferSize = (g_audioByteBufferSize + 63) & ~63;
    }
}

/* Removed dependency on BAE_GetSliceTimeInMicroseconds here to avoid dereferencing
 * uninitialized engine globals (MusicGlobals) that caused a crash.
 */
static void PV_UpdateSliceSizeIfNeeded(void) {
    if (g_framesPerSlice == 0 || g_audioByteBufferSize == 0) {
        PV_UpdateSliceDefaults();
    }
}

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    if (g_muted) { memset(stream, 0, len); return; }
    PV_UpdateSliceSizeIfNeeded();
    int remaining = len;
    Uint8 *out = stream;
    const int sampleBytes = (g_bits/8) * (int)g_channels;
    while (remaining > 0) {
        long sliceBytes = g_audioByteBufferSize;
        if (sliceBytes <= 0 || !g_sliceStatic) { memset(out,0,remaining); g_totalSamplesPlayed += (unsigned long)(remaining / sampleBytes); break; }
        long frames = sliceBytes / sampleBytes;
        BAE_BuildMixerSlice(userdata, g_sliceStatic, sliceBytes, frames);
        if (sliceBytes > remaining) {
            memcpy(out, g_sliceStatic, remaining);
            g_totalSamplesPlayed += (unsigned long)(remaining / sampleBytes);
            remaining = 0;
        } else {
            memcpy(out, g_sliceStatic, sliceBytes);
            out += sliceBytes;
            remaining -= sliceBytes;
            g_totalSamplesPlayed += (unsigned long)frames;
        }
        g_lastCallbackFrames = (unsigned long)frames;
    }
}

// ---- System setup / cleanup ----
int BAE_Setup(void) { return 0; }
int BAE_Cleanup(void) { return 0; }

// ---- Memory ----
static unsigned long g_mem_used = 0;
static unsigned long g_mem_used_max = 0;
void *BAE_Allocate(unsigned long size) {
    void *p = NULL; if (size) { p = calloc(1, size); if (p) { g_mem_used += size; if (g_mem_used > g_mem_used_max) g_mem_used_max = g_mem_used; } }
    return p;
}
void BAE_Deallocate(void *memoryBlock) { if (memoryBlock) free(memoryBlock); }
void BAE_AllocDebug(int debug) { (void)debug; }
unsigned long BAE_GetSizeOfMemoryUsed(void){ return g_mem_used; }
unsigned long BAE_GetMaxSizeOfMemoryUsed(void){ return g_mem_used_max; }
int BAE_IsBadReadPointer(void *memoryBlock, unsigned long size){ (void)memoryBlock; (void)size; return 2; }
unsigned long BAE_SizeOfPointer(void *memoryBlock){ (void)memoryBlock; return 0; }
void BAE_BlockMove(void *source, void *dest, unsigned long size){ if (source && dest && size) memmove(dest, source, size); }

// ---- Audio capabilities ----
int BAE_IsStereoSupported(void){ return 1; }
int BAE_Is8BitSupported(void){ return 1; }
int BAE_Is16BitSupported(void){ return 1; }
short int BAE_GetHardwareVolume(void){ return g_unscaled_volume; }
void BAE_SetHardwareVolume(short int theVolume){ if (theVolume < 0) theVolume = 0; if (theVolume > 256) theVolume = 256; g_unscaled_volume = theVolume; }
short int BAE_GetHardwareBalance(void){ return g_balance; }
void BAE_SetHardwareBalance(short int balance){ if (balance < -256) balance = -256; if (balance > 256) balance = 256; g_balance = balance; }

// ---- Timing ----
unsigned long BAE_Microseconds(void){ if (!g_perfFreq){ g_perfFreq = SDL_GetPerformanceFrequency(); g_startTicks = SDL_GetPerformanceCounter(); } Uint64 now = SDL_GetPerformanceCounter(); Uint64 delta = now - g_startTicks; double us = (double)delta * 1000000.0 / (double)g_perfFreq; return (unsigned long)us; }
void BAE_WaitMicroseconds(unsigned long wait){ SDL_Delay((wait + 999)/1000); }

// ---- File helpers (defer to ANSI stdio for now) ----
void BAE_CopyFileNameNative(void *src, void *dst){ if (src && dst) strcpy((char*)dst,(char*)src); }
long BAE_FileCreate(void *fileName){ FILE *f = fopen((char*)fileName, "wb"); if (!f) return -1; fclose(f); return 0; }
long BAE_FileDelete(void *fileName){ return remove((char*)fileName)==0?0:-1; }
long BAE_FileOpenForRead(void *fileName){ FILE *f = fopen((char*)fileName,"rb"); return (long)f; }
long BAE_FileOpenForWrite(void *fileName){ FILE *f = fopen((char*)fileName,"wb"); return (long)f; }
long BAE_FileOpenForReadWrite(void *fileName){ FILE *f = fopen((char*)fileName,"rb+"); if(!f) f = fopen((char*)fileName,"wb+"); return (long)f; }
void BAE_FileClose(long ref){ if (ref) fclose((FILE*)ref); }
long BAE_ReadFile(long ref, void *pBuf, long len){ size_t r = fread(pBuf,1,(size_t)len,(FILE*)ref); return (r==0 && ferror((FILE*)ref))?-1:(long)r; }
long BAE_WriteFile(long ref, void *pBuf, long len){ size_t w = fwrite(pBuf,1,(size_t)len,(FILE*)ref); fflush((FILE*)ref); return (w==0 && ferror((FILE*)ref))?-1:(long)w; }
long BAE_SetFilePosition(long ref, unsigned long pos){ return fseek((FILE*)ref,(long)pos,SEEK_SET)==0?0:-1; }
unsigned long BAE_GetFilePosition(long ref){ long p = ftell((FILE*)ref); return (unsigned long)((p<0)?0:p); }
unsigned long BAE_GetFileLength(long ref){ long cur = ftell((FILE*)ref); fseek((FILE*)ref,0,SEEK_END); long end = ftell((FILE*)ref); fseek((FILE*)ref,cur,SEEK_SET); return (unsigned long)((end<0)?0:end); }
int BAE_SetFileLength(long ref, unsigned long newSize){
    if (!ref) return -1;
#if defined(_WIN32)
    int fd = _fileno((FILE*)ref);
    if (fd < 0) return -1;
    #if defined(_MSC_VER) && _MSC_VER >= 1400
        return (_chsize_s(fd, (long long)newSize) == 0) ? 0 : -1;
    #elif defined(_WIN64)
        return (_chsize(fd, (long)newSize) == 0) ? 0 : -1; // potential truncation if >2GB
    #else
        return (_chsize(fd, (long)newSize) == 0) ? 0 : -1;
    #endif
#else
    int fd = fileno((FILE*)ref);
    if (fd < 0) return -1;
    return (ftruncate(fd, (off_t)newSize) == 0) ? 0 : -1;
#endif
}

// ---- Audio buffer metrics ----
int BAE_GetAudioBufferCount(void){ return 1; }
long BAE_GetAudioByteBufferSize(void){ return g_audioByteBufferSize; }

// ---- Audio card support ----
static int PV_CalcSliceSize(void){ // 11ms slice
    double sliceSeconds = 0.011; // historically ~11ms
    unsigned long frames = (unsigned long)( (double)g_sampleRate * sliceSeconds );
    if (frames < 64) frames = 64; g_framesPerSlice = frames;
    long bytes = (long)(frames * g_channels * (g_bits/8));
    // align to 64 bytes
    bytes = (bytes + 63) & ~63; return bytes;
}

int BAE_AquireAudioCard(void *threadContext, unsigned long sampleRate, unsigned long channels, unsigned long bits){ (void)threadContext; if (g_audioDevice) return 0; if (!g_initialized){ if (SDL_InitSubSystem(SDL_INIT_AUDIO)!=0){ fprintf(stderr,"SDL audio init fail: %s\n", SDL_GetError()); return -1; } g_initialized=1; }
    g_sampleRate = sampleRate; g_channels = channels; g_bits = bits;
    PV_ComputeSliceSizeFromEngine();
    SDL_AudioSpec want; SDL_zero(want);
    want.freq = (int)sampleRate;
    want.channels = (Uint8)channels;
    want.format = (bits==16)?AUDIO_S16SYS:AUDIO_U8;
    // request buffer roughly equal to one slice; SDL may choose different
    want.samples = (Uint16) (g_framesPerSlice > 4096 ? 4096 : g_framesPerSlice);
    if (want.samples < 256) want.samples = 256; // lower bound
    want.callback = audio_callback;
    want.userdata = threadContext;
    g_audioDevice = SDL_OpenAudioDevice(NULL,0,&want,&g_have,0);
    if (!g_audioDevice){ fprintf(stderr,"SDL_OpenAudioDevice failed: %s\n", SDL_GetError()); return -1; }
    SDL_PauseAudioDevice(g_audioDevice,0);
    BAE_PRINTF("SDL2 audio device opened: %d Hz, %d ch, fmt 0x%x, dev buf %u frames, slice %lu frames (%ld bytes)\n", g_have.freq, g_have.channels, g_have.format, g_have.samples, g_framesPerSlice, g_audioByteBufferSize);
    return 0;
}

int BAE_ReleaseAudioCard(void *threadContext){ (void)threadContext; if (g_audioDevice){ SDL_CloseAudioDevice(g_audioDevice); g_audioDevice=0; } return 0; }
int BAE_Mute(void){ g_muted=1; return 0; }
int BAE_Unmute(void){ g_muted=0; return 0; }
int BAE_IsMuted(void){ return g_muted; }
void BAE_ProcessRouteBus(int currentRoute, long *pChannels, int count){ (void)currentRoute; (void)pChannels; (void)count; }
void BAE_Idle(void *userContext){ (void)userContext; SDL_Delay(1); }
void BAE_UnlockAudioFrameThread(void){}
void BAE_LockAudioFrameThread(void){}
void BAE_BlockAudioFrameThread(void){ SDL_Delay(1); }
unsigned long BAE_GetDeviceSamplesPlayedPosition(void){ return g_totalSamplesPlayed; }

long BAE_MaxDevices(void){ return 1; }
void BAE_SetDeviceID(long deviceID, void *deviceParameter){ (void)deviceID; (void)deviceParameter; }
long BAE_GetDeviceID(void *deviceParameter){ (void)deviceParameter; return 0; }
void BAE_GetDeviceName(long deviceID, char *cName, unsigned long cNameLength){ (void)deviceID; if (cName && cNameLength){ snprintf(cName,cNameLength,"SDL2,callback,threaded"); } }
/* NOTE: BAE_GetSliceTimeInMicroseconds is implemented in core (GenSetup.c).
 * Do NOT provide a second definition here to avoid multiple definition link errors. */

// ---- Threading: minimal stub (engine may call; implement if needed) ----
int BAE_CreateFrameThread(void* threadContext, BAE_FrameThreadProc proc){ (void)threadContext; (void)proc; return 0; }
int BAE_SetFrameThreadPriority(void *threadContext, int priority){ (void)threadContext; (void)priority; return 0; }
int BAE_DestroyFrameThread(void* threadContext){ (void)threadContext; return 0; }
int BAE_SleepFrameThread(void* threadContext, long msec){ (void)threadContext; SDL_Delay((Uint32)msec); return 0; }

// ---- Mutex ----
int BAE_NewMutex(BAE_Mutex* lock, char *name, char *file, int lineno){ (void)name; (void)file; (void)lineno; sSDLMutex *m = (sSDLMutex*)BAE_Allocate(sizeof(sSDLMutex)); if (!m) return 0; m->mtx = SDL_CreateMutex(); if(!m->mtx){ BAE_Deallocate(m); return 0; } *lock = (BAE_Mutex)m; return 1; }
void BAE_AcquireMutex(BAE_Mutex lock){ if (!lock) return; sSDLMutex *m = (sSDLMutex*)lock; SDL_LockMutex(m->mtx); }
void BAE_ReleaseMutex(BAE_Mutex lock){ if (!lock) return; sSDLMutex *m = (sSDLMutex*)lock; SDL_UnlockMutex(m->mtx); }
void BAE_DestroyMutex(BAE_Mutex lock){ if (!lock) return; sSDLMutex *m = (sSDLMutex*)lock; if (m->mtx) SDL_DestroyMutex(m->mtx); BAE_Deallocate(m); }

// ---- Capture stubs (not implemented) ----
int BAE_AquireAudioCapture(void *threadContext, unsigned long sampleRate, unsigned long channels, unsigned long bits, unsigned long *pCaptureHandle){ (void)threadContext; (void)sampleRate; (void)channels; (void)bits; (void)pCaptureHandle; return -1; }
int BAE_ReleaseAudioCapture(void *threadContext){ (void)threadContext; return -1; }
int BAE_StartAudioCapture(BAE_CaptureDone done, void *callbackContext){ (void)done; (void)callbackContext; return -1; }
int BAE_StopAudioCapture(void){ return -1; }
int BAE_PauseAudioCapture(void){ return -1; }
int BAE_ResumeAudioCapture(void){ return -1; }
long BAE_MaxCaptureDevices(void){ return 0; }
void BAE_SetCaptureDeviceID(long deviceID, void *deviceParameter){ (void)deviceID; (void)deviceParameter; }
long BAE_GetCaptureDeviceID(void *deviceParameter){ (void)deviceParameter; return -1; }
void BAE_GetCaptureDeviceName(long deviceID, char *cName, unsigned long cNameLength){ (void)deviceID; if (cName && cNameLength) *cName='\0'; }
unsigned long BAE_GetCaptureBufferSizeInFrames(){ return 0; }
int BAE_GetCaptureBufferCount(){ return 0; }
unsigned long BAE_GetDeviceSamplesCapturedPosition(){ return 0; }

// ---- Misc ----
void BAE_DisplayMemoryUsage(int detailLevel){ (void)detailLevel; }
void BAE_PrintHexDump(void *address, long length){ unsigned char *p=(unsigned char*)address; for(long i=0;i<length;i++){ if((i%16)==0) BAE_PRINTF("\n%08lx: ", (unsigned long)i); BAE_PRINTF("%02X ", p[i]); } BAE_PRINTF("\n"); }
