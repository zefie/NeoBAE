// BAE_API_SDL2.c - SDL2 audio backend for miniBAE
// SDL2 platform implementation of subset of BAE_API.
#define SDL_MAIN_HANDLED
#include <SDL.h>
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
static uint32_t g_sampleRate = 44100;
static uint32_t g_channels = 2;
static uint32_t g_bits = 16;
static int32_t g_audioByteBufferSize = 0; // bytes per slice
static uint32_t g_framesPerSlice = 0; // frames per slice (approx) (informational only)
static uint32_t g_totalSamplesPlayed = 0; // running counter (approx)
static int16_t g_unscaled_volume = 256; // 0..256
static int16_t g_balance = 0; // -256..256
static int g_muted = 0;
static uint32_t g_lastCallbackFrames = 0;
static Uint64 g_perfFreq = 0;
static Uint64 g_startTicks = 0;

// Mutex wrapper
typedef struct { SDL_mutex *mtx; } sSDLMutex;

// Forward from engine
extern void BAE_BuildMixerSlice(void *threadContext, void *pAudioBuffer, int32_t bufferByteLength, int32_t sampleFrames);
extern int16_t BAE_GetMaxSamplePerSlice(void); // ensure visible for sizing

static Uint8 *g_sliceStatic = NULL;
static size_t g_sliceStaticSize = 0;

static void PV_ComputeSliceSizeFromEngine(void) {
    // Engine already returns the per-slice frame count for the CURRENT configured rate.
    // Earlier code incorrectly tried to rescale this assuming a 44.1k baseline which
    // caused buffer size mismatches and potential overruns at other rates.
    int16_t maxFrames = BAE_GetMaxSamplePerSlice();
    BAE_PRINTF("BAE_GetMaxSamplePerSlice returned (engine @ %u Hz): %d\n", g_sampleRate, maxFrames);

    if (maxFrames <= 0) {
        BAE_PRINTF("maxFrames <= 0, using fallback value 512\n");
        maxFrames = 512; // fallback safeguard
    }

    uint32_t frames = (uint32_t)maxFrames;
    if (frames < 64) frames = 64; // sanity floor
    g_framesPerSlice = frames;
    g_audioByteBufferSize = (int32_t)(frames * g_channels * (g_bits/8));
    // align to 64 bytes for SIMD/cache friendliness
    g_audioByteBufferSize = (g_audioByteBufferSize + 63) & ~63;

    BAE_PRINTF("Computed slice (no rescale): %u frames, %ld bytes (channels=%u bits=%u)\n",
               g_framesPerSlice, g_audioByteBufferSize, g_channels, g_bits);

    if (g_sliceStaticSize < (size_t)g_audioByteBufferSize) {
        BAE_PRINTF("Reallocating slice buffer: %zu -> %ld bytes\n", g_sliceStaticSize, g_audioByteBufferSize);
        free(g_sliceStatic);
        g_sliceStatic = (Uint8*)calloc(1, (size_t)g_audioByteBufferSize);
        if (g_sliceStatic) {
            g_sliceStaticSize = (size_t)g_audioByteBufferSize;
            BAE_PRINTF("Slice buffer allocated successfully\n");
        } else {
            BAE_PRINTF("ERROR: Failed to allocate slice buffer!\n");
        }
    }
}

/* Query core slice timing lazily so we match engine's configured buffer size */
static void PV_UpdateSliceDefaults(void) {
    if (g_audioByteBufferSize == 0) {
        /* Fallback ~11ms slice */
        uint32_t frames = (uint32_t)((g_sampleRate * 11ULL) / 1000ULL);
        if (frames < 64) frames = 64;
        g_framesPerSlice = frames;
        g_audioByteBufferSize = (int32_t)(frames * g_channels * (g_bits/8));
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
    if (g_muted) { 
        memset(stream, 0, len); 
        return; 
    }
    
    PV_UpdateSliceSizeIfNeeded();
    
    int remaining = len;
    Uint8 *out = stream;
    const int sampleBytes = (g_bits/8) * (int)g_channels;
    
    if (sampleBytes <= 0) {
        BAE_PRINTF("ERROR: Invalid sample byte size: %d\n", sampleBytes);
        memset(stream, 0, len);
        return;
    }
    
    while (remaining > 0) {
        int32_t sliceBytes = g_audioByteBufferSize;
        if (sliceBytes <= 0 || !g_sliceStatic) { 
            if (sliceBytes <= 0) {
                BAE_PRINTF("ERROR: Invalid slice size: %ld\n", sliceBytes);
            }
            if (!g_sliceStatic) {
                BAE_PRINTF("ERROR: No slice buffer allocated\n");
            }
            memset(out,0,remaining); 
            g_totalSamplesPlayed += (uint32_t)(remaining / sampleBytes); 
            break; 
        }
        
        int32_t frames = sliceBytes / sampleBytes;
        if (frames <= 0) {
            BAE_PRINTF("ERROR: Invalid frame count: %ld\n", frames);
            memset(out,0,remaining);
            break;
        }
        
        // Call the engine to generate audio
        BAE_BuildMixerSlice(userdata, g_sliceStatic, sliceBytes, frames);
        
        if (sliceBytes > remaining) {
            memcpy(out, g_sliceStatic, remaining);
            g_totalSamplesPlayed += (uint32_t)(remaining / sampleBytes);
            remaining = 0;
        } else {
            memcpy(out, g_sliceStatic, sliceBytes);
            out += sliceBytes;
            remaining -= sliceBytes;
            g_totalSamplesPlayed += (uint32_t)frames;
        }
        g_lastCallbackFrames = (uint32_t)frames;
    }
}

// ---- System setup / cleanup ----
int BAE_Setup(void) { return 0; }
int BAE_Cleanup(void) { return 0; }

// ---- Memory ----
static uint32_t g_mem_used = 0;
static uint32_t g_mem_used_max = 0;
void *BAE_Allocate(uint32_t size) {
    void *p = NULL; 
    if (size) { 
        p = calloc(1, size); 
        if (p) { 
            g_mem_used += size; 
            if (g_mem_used > g_mem_used_max) g_mem_used_max = g_mem_used; 
        } 
    }
    return p;
}
void BAE_Deallocate(void *memoryBlock) { 
    if (memoryBlock) {
        // Note: We can't easily track the size being freed without additional overhead
        // This is a limitation of the current API design
        free(memoryBlock); 
    }
}
void BAE_AllocDebug(int debug) { (void)debug; }
uint32_t BAE_GetSizeOfMemoryUsed(void){ return g_mem_used; }
uint32_t BAE_GetMaxSizeOfMemoryUsed(void){ return g_mem_used_max; }
int BAE_IsBadReadPointer(void *memoryBlock, uint32_t size){ (void)memoryBlock; (void)size; return 2; }
uint32_t BAE_SizeOfPointer(void *memoryBlock){ (void)memoryBlock; return 0; }
void BAE_BlockMove(void *source, void *dest, uint32_t size){ if (source && dest && size) memmove(dest, source, size); }

// ---- Audio capabilities ----
int BAE_IsStereoSupported(void){ return 1; }
int BAE_Is8BitSupported(void){ return 1; }
int BAE_Is16BitSupported(void){ return 1; }
int16_t BAE_GetHardwareVolume(void){ return g_unscaled_volume; }
void BAE_SetHardwareVolume(int16_t theVolume){ if (theVolume < 0) theVolume = 0; if (theVolume > 256) theVolume = 256; g_unscaled_volume = theVolume; }
int16_t BAE_GetHardwareBalance(void){ return g_balance; }
void BAE_SetHardwareBalance(int16_t balance){ if (balance < -256) balance = -256; if (balance > 256) balance = 256; g_balance = balance; }

// ---- Timing ----
uint32_t BAE_Microseconds(void){ if (!g_perfFreq){ g_perfFreq = SDL_GetPerformanceFrequency(); g_startTicks = SDL_GetPerformanceCounter(); } Uint64 now = SDL_GetPerformanceCounter(); Uint64 delta = now - g_startTicks; double us = (double)delta * 1000000.0 / (double)g_perfFreq; return (uint32_t)us; }
void BAE_WaitMicroseconds(uint32_t wait){ SDL_Delay((wait + 999)/1000); }

// ---- File helpers - Fixed for 64-bit compatibility ----
// We need to map FILE* pointers to small integer handles for compatibility
// with existing code that assumes file handles are small integers

#define MAX_OPEN_FILES 64
static FILE* g_file_table[MAX_OPEN_FILES] = {0};
static int g_next_handle = 1;

static int PV_AllocateFileHandle(FILE* f) {
    if (!f) return -1;
    for (int i = 1; i < MAX_OPEN_FILES; i++) {
        if (g_file_table[i] == NULL) {
            g_file_table[i] = f;
            return i;
        }
    }
    // No free slots, close the file
    fclose(f);
    return -1;
}

static FILE* PV_GetFileFromHandle(intptr_t handle) {
    if (handle > 0 && handle < MAX_OPEN_FILES) {
        return g_file_table[handle];
    }
    return NULL;
}

static void PV_FreeFileHandle(intptr_t handle) {
    if (handle > 0 && handle < MAX_OPEN_FILES) {
        g_file_table[handle] = NULL;
    }
}

void BAE_CopyFileNameNative(void *src, void *dst){ if (src && dst) strcpy((char*)dst,(char*)src); }
int32_t BAE_FileCreate(void *fileName){ FILE *f = fopen((char*)fileName, "wb"); if (!f) return -1; fclose(f); return 0; }
int32_t BAE_FileDelete(void *fileName){ return remove((char*)fileName)==0?0:-1; }

intptr_t BAE_FileOpenForRead(void *fileName){ 
    FILE *f = fopen((char*)fileName,"rb"); 
    return f ? PV_AllocateFileHandle(f) : -1;
}

intptr_t BAE_FileOpenForWrite(void *fileName){ 
    FILE *f = fopen((char*)fileName,"wb"); 
    return f ? PV_AllocateFileHandle(f) : -1;
}

intptr_t BAE_FileOpenForReadWrite(void *fileName){ 
    FILE *f = fopen((char*)fileName,"rb+"); 
    if(!f) f = fopen((char*)fileName,"wb+"); 
    return f ? PV_AllocateFileHandle(f) : -1;
}

void BAE_FileClose(intptr_t ref){ 
    FILE* f = PV_GetFileFromHandle(ref);
    if (f) {
        fclose(f);
        PV_FreeFileHandle(ref);
    }
}

int32_t BAE_ReadFile(intptr_t ref, void *pBuf, int32_t len){ 
    FILE* f = PV_GetFileFromHandle(ref);
    if (!f) return -1;
    size_t r = fread(pBuf,1,(size_t)len,f); 
    return (r==0 && ferror(f))?-1:(int32_t)r; 
}

int32_t BAE_WriteFile(intptr_t ref, void *pBuf, int32_t len){ 
    FILE* f = PV_GetFileFromHandle(ref);
    if (!f) return -1;
    size_t w = fwrite(pBuf,1,(size_t)len,f); 
    fflush(f); 
    return (w==0 && ferror(f))?-1:(int32_t)w; 
}

int32_t BAE_SetFilePosition(intptr_t ref, uint32_t pos){ 
    FILE* f = PV_GetFileFromHandle(ref);
    if (!f) return -1;
    return fseek(f,(int32_t)pos,SEEK_SET)==0?0:-1; 
}
uint32_t BAE_GetFilePosition(intptr_t ref){ 
    FILE* f = PV_GetFileFromHandle(ref);
    if (!f) return 0;
    long p = ftell(f); 
    if (p < 0) return 0;
    // Ensure we don't overflow uint32_t on 64-bit systems
    if (p > UINT32_MAX) return UINT32_MAX;
    return (uint32_t)p; 
}
uint32_t BAE_GetFileLength(intptr_t ref){ 
    FILE* f = PV_GetFileFromHandle(ref);
    if (!f) return 0;
    long cur = ftell(f); 
    if (cur < 0) return 0;
    fseek(f,0,SEEK_END); 
    long end = ftell(f); 
    fseek(f,cur,SEEK_SET); 
    if (end < 0) return 0;
    // Ensure we don't overflow uint32_t on 64-bit systems
    if (end > UINT32_MAX) return UINT32_MAX;
    return (uint32_t)end; 
}
int BAE_SetFileLength(intptr_t ref, uint32_t newSize){
    FILE* f = PV_GetFileFromHandle(ref);
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

// ---- Audio card support ----
static int PV_CalcSliceSize(void){ // 11ms slice
    double sliceSeconds = 0.011; // historically ~11ms
    uint32_t frames = (uint32_t)( (double)g_sampleRate * sliceSeconds );
    if (frames < 64) frames = 64; g_framesPerSlice = frames;
    int32_t bytes = (int32_t)(frames * g_channels * (g_bits/8));
    // align to 64 bytes
    bytes = (bytes + 63) & ~63; return bytes;
}

int BAE_AcquireAudioCard(void *threadContext, uint32_t sampleRate, uint32_t channels, uint32_t bits){ 
    (void)threadContext; 
    BAE_PRINTF("BAE_AcquireAudioCard called: %u Hz, %u ch, %u bits\n", sampleRate, channels, bits);
    
    if (g_audioDevice) {
        BAE_PRINTF("Audio device already acquired\n");
        return 0; 
    }
    
    if (!g_initialized){ 
        BAE_PRINTF("Initializing SDL audio subsystem...\n");
        if (SDL_InitSubSystem(SDL_INIT_AUDIO)!=0){ 
            BAE_PRINTF("SDL audio init fail: %s\n", SDL_GetError()); 
            return -1; 
        } 
        g_initialized=1; 
        BAE_PRINTF("SDL audio subsystem initialized successfully\n");
    }
    g_sampleRate = sampleRate; g_channels = channels; g_bits = bits;
    BAE_PRINTF("Computing provisional slice size from engine (pre SDL_OpenAudioDevice)...\n");
    PV_ComputeSliceSizeFromEngine();
    
    SDL_AudioSpec want; SDL_zero(want);
    want.freq = (int)sampleRate;
    want.channels = (Uint8)channels;
    want.format = (bits==16)?AUDIO_S16SYS:AUDIO_U8;
    // request buffer roughly equal to one slice; SDL may choose different
    // Request a device buffer roughly equal to one engine slice (capped) so callback cadence
    // matches engine timing closely even at very low sample rates.
    want.samples = (Uint16)(g_framesPerSlice > 4096 ? 4096 : g_framesPerSlice);
    // Enforce a practical minimum of 64 frames (our slice floor); SDL can handle small sizes.
    if (want.samples < 64) want.samples = 64;
    want.callback = audio_callback;
    want.userdata = threadContext;
    
    BAE_PRINTF("Opening SDL audio device: freq=%d, channels=%d, format=0x%x, samples=%d\n", 
               want.freq, want.channels, want.format, want.samples);
    
    g_audioDevice = SDL_OpenAudioDevice(NULL,0,&want,&g_have,0);
    if (!g_audioDevice){ 
        BAE_PRINTF("SDL_OpenAudioDevice failed: %s\n", SDL_GetError()); 
        return -1; 
    }
    
    BAE_PRINTF("Audio device opened successfully. SDL actual: %d Hz, %d ch, fmt 0x%x, dev buf %u frames\n",
               g_have.freq, g_have.channels, g_have.format, g_have.samples);

    // If SDL adjusted the frequency or channel count, update and recompute slice.
    if ((uint32_t)g_have.freq != g_sampleRate || (uint32_t)g_have.channels != g_channels) {
        BAE_PRINTF("SDL adjusted audio format (requested %u Hz/%u ch -> got %d Hz/%d ch). Recomputing slice.\n",
                   g_sampleRate, g_channels, g_have.freq, g_have.channels);
        g_sampleRate = (uint32_t)g_have.freq;
        g_channels = (uint32_t)g_have.channels;
        PV_ComputeSliceSizeFromEngine();
    }

    SDL_PauseAudioDevice(g_audioDevice,0);
    BAE_PRINTF("SDL2 audio device active: %u Hz, %u ch, dev buf %u frames, slice %u frames (%ld bytes)\n",
               g_sampleRate, g_channels, g_have.samples, g_framesPerSlice, g_audioByteBufferSize);
    return 0;
}

int BAE_ReleaseAudioCard(void *threadContext){ (void)threadContext; if (g_audioDevice){ SDL_CloseAudioDevice(g_audioDevice); g_audioDevice=0; } return 0; }
int BAE_Mute(void){ g_muted=1; return 0; }
int BAE_Unmute(void){ g_muted=0; return 0; }
int BAE_IsMuted(void){ return g_muted; }
void BAE_ProcessRouteBus(int currentRoute, int32_t *pChannels, int count){ (void)currentRoute; (void)pChannels; (void)count; }
void BAE_Idle(void *userContext){ (void)userContext; SDL_Delay(1); }
void BAE_UnlockAudioFrameThread(void){}
void BAE_LockAudioFrameThread(void){}
void BAE_BlockAudioFrameThread(void){ SDL_Delay(1); }
uint32_t BAE_GetDeviceSamplesPlayedPosition(void){ return g_totalSamplesPlayed; }

int32_t BAE_MaxDevices(void){ return 1; }
void BAE_SetDeviceID(int32_t deviceID, void *deviceParameter){ (void)deviceID; (void)deviceParameter; }
int32_t BAE_GetDeviceID(void *deviceParameter){ (void)deviceParameter; return 0; }
void BAE_GetDeviceName(int32_t deviceID, char *cName, uint32_t cNameLength){ (void)deviceID; if (cName && cNameLength){ snprintf(cName,cNameLength,"SDL2,callback,threaded"); } }
/* NOTE: BAE_GetSliceTimeInMicroseconds is implemented in core (GenSetup.c).
 * Do NOT provide a second definition here to avoid multiple definition link errors. */

// ---- Threading: minimal stub (engine may call; implement if needed) ----
int BAE_CreateFrameThread(void* threadContext, BAE_FrameThreadProc proc){ (void)threadContext; (void)proc; return 0; }
int BAE_SetFrameThreadPriority(void *threadContext, int priority){ (void)threadContext; (void)priority; return 0; }
int BAE_DestroyFrameThread(void* threadContext){ (void)threadContext; return 0; }
int BAE_SleepFrameThread(void* threadContext, int32_t msec){ (void)threadContext; SDL_Delay((Uint32)msec); return 0; }

// ---- Mutex ----
int BAE_NewMutex(BAE_Mutex* lock, char *name, char *file, int lineno){ (void)name; (void)file; (void)lineno; sSDLMutex *m = (sSDLMutex*)BAE_Allocate(sizeof(sSDLMutex)); if (!m) return 0; m->mtx = SDL_CreateMutex(); if(!m->mtx){ BAE_Deallocate(m); return 0; } *lock = (BAE_Mutex)m; return 1; }
void BAE_AcquireMutex(BAE_Mutex lock){ if (!lock) return; sSDLMutex *m = (sSDLMutex*)lock; SDL_LockMutex(m->mtx); }
void BAE_ReleaseMutex(BAE_Mutex lock){ if (!lock) return; sSDLMutex *m = (sSDLMutex*)lock; SDL_UnlockMutex(m->mtx); }
void BAE_DestroyMutex(BAE_Mutex lock){ if (!lock) return; sSDLMutex *m = (sSDLMutex*)lock; if (m->mtx) SDL_DestroyMutex(m->mtx); BAE_Deallocate(m); }

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
void BAE_GetCaptureDeviceName(int32_t deviceID, char *cName, uint32_t cNameLength){ (void)deviceID; if (cName && cNameLength) *cName='\0'; }
uint32_t BAE_GetCaptureBufferSizeInFrames(){ return 0; }
int BAE_GetCaptureBufferCount(){ return 0; }
uint32_t BAE_GetDeviceSamplesCapturedPosition(){ return 0; }

// ---- Misc ----
void BAE_DisplayMemoryUsage(int detailLevel){ (void)detailLevel; }
void BAE_PrintHexDump(void *address, int32_t length){ unsigned char *p=(unsigned char*)address; for(int32_t i=0;i<length;i++){ if((i%16)==0) BAE_PRINTF("\n%08lx: ", (uint32_t)i); BAE_PRINTF("%02X ", p[i]); } BAE_PRINTF("\n"); }
