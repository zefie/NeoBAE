/*
 * bae_platform_wasm.c
 *
 * Platform-specific implementations for WebAssembly
 * Provides stubs and implementations for BAE platform layer
 * Updated for zefie/miniBAE fork with int32_t/uint32_t types
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <emscripten.h>

#include "X_API.h"
#include "BAE_API.h"

// ============================================
// Memory Management
// ============================================

void* BAE_Allocate(uint32_t size) {
    return calloc(1, size); // ensure zero-initialized memory (required by miniBAE)
}

void BAE_Deallocate(void* ptr) {
    free(ptr);
}

void BAE_BlockMove(void* src, void* dest, uint32_t size) {
    memmove(dest, src, size);
}

int BAE_IsBadReadPointer(void* ptr, uint32_t size) {
    (void)size;
    return (ptr == NULL) ? 1 : 0;
}

// ============================================
// Mutex (single-threaded stubs)
// ============================================

int BAE_NewMutex(BAE_Mutex* lock, char *name, char *file, int lineno) {
    (void)name;
    (void)file;
    (void)lineno;
    if (lock) {
        *lock = malloc(sizeof(int));
        if (*lock) {
            *((int*)*lock) = 0;  // unlocked
            return 1;  // success
        }
    }
    return 0;  // failure
}

void BAE_DestroyMutex(BAE_Mutex mutex) {
    if (mutex) {
        free(mutex);
    }
}

void BAE_AcquireMutex(BAE_Mutex mutex) {
    // Single-threaded - no-op
    (void)mutex;
}

void BAE_ReleaseMutex(BAE_Mutex mutex) {
    // Single-threaded - no-op
    (void)mutex;
}

// ============================================
// Audio Hardware (stubs - we use JS audio)
// ============================================

int BAE_AquireAudioCard(void* context, uint32_t sampleRate,
                        uint32_t channels, uint32_t bits) {
    (void)context;
    (void)sampleRate;
    (void)channels;
    (void)bits;
    return 0;  // Success
}

// Alternate spelling used by zefie fork
int BAE_AcquireAudioCard(void* context, uint32_t sampleRate,
                         uint32_t channels, uint32_t bits) {
    return BAE_AquireAudioCard(context, sampleRate, channels, bits);
}

int BAE_ReleaseAudioCard(void* context) {
    (void)context;
    return 0;  // Success
}

int BAE_Setup(void) {
    return 0;  // Success
}

int BAE_Cleanup(void) {
    return 0;  // Success
}

// ============================================
// Audio Processing
// ============================================

void BAE_ProcessRouteBus(int currentRoute, int32_t *pChannels, int count) {
    // No-op for WASM - audio routing handled in JS
    (void)currentRoute;
    (void)pChannels;
    (void)count;
}

// ============================================
// Time Functions
// ============================================

uint32_t BAE_GetMicroseconds(void) {
    static double startMs = 0.0;
    double nowMs = emscripten_get_now();
    if (startMs == 0.0) {
        startMs = nowMs;
    }
    double deltaUs = (nowMs - startMs) * 1000.0;
    if (deltaUs < 0) {
        deltaUs = 0;
    }
    return (uint32_t)deltaUs;
}

void BAE_WaitMicroseconds(uint32_t wait) {
    // Busy wait not appropriate for WASM
    (void)wait;
}

// ============================================
// File I/O (not used - we load from memory)
// ============================================

int32_t BAE_FileCreate(void* fileName) {
    (void)fileName;
    return -1;
}

int32_t BAE_FileDelete(void* fileName) {
    (void)fileName;
    return -1;
}

intptr_t BAE_FileOpen(void* fileName, int32_t mode) {
    (void)fileName;
    (void)mode;
    return -1;
}

intptr_t BAE_FileOpenForRead(void* fileName) {
    (void)fileName;
    return -1;
}

intptr_t BAE_FileOpenForReadWrite(void* fileName) {
    (void)fileName;
    return -1;
}

void BAE_CopyFileNameNative(void* fileNameSource, void* fileNameDest) {
    (void)fileNameSource;
    (void)fileNameDest;
}


void BAE_FileClose(intptr_t fileRef) {
    (void)fileRef;
}

int32_t BAE_ReadFile(intptr_t fileRef, void* buffer, int32_t size) {
    (void)fileRef;
    (void)buffer;
    (void)size;
    return 0;
}

int32_t BAE_WriteFile(intptr_t fileRef, void* buffer, int32_t size) {
    (void)fileRef;
    (void)buffer;
    (void)size;
    return 0;
}

uint32_t BAE_GetFileLength(intptr_t fileRef) {
    (void)fileRef;
    return 0;
}

int32_t BAE_SetFilePosition(intptr_t fileRef, uint32_t offset) {
    (void)fileRef;
    (void)offset;
    return -1;
}

uint32_t BAE_GetFilePosition(intptr_t fileRef) {
    (void)fileRef;
    return 0;
}

// ============================================
// Hardware Volume (stubs)
// ============================================

int16_t BAE_GetHardwareVolume(void) {
    return 256;  // Max
}

void BAE_SetHardwareVolume(int16_t volume) {
    (void)volume;
}

// ============================================
// Misc
// ============================================

void BAE_Idle(void *userContext) {
    (void)userContext;
}

// BAE_Microseconds - alternate name for BAE_GetMicroseconds
uint32_t BAE_Microseconds(void) {
    return BAE_GetMicroseconds();
}

// ============================================
// Threading stubs (single-threaded WASM)
// ============================================

int BAE_CreateFrameThread(void* threadContext, void (*proc)(void*)) {
    (void)threadContext;
    (void)proc;
    return 0;  // Success - but we don't actually create a thread
}

int BAE_DestroyFrameThread(void* threadContext) {
    (void)threadContext;
    return 0;
}

int BAE_SleepFrameThread(void* threadContext, int32_t msec) {
    (void)threadContext;
    (void)msec;
    return 0;
}

int BAE_SetFrameThreadPriority(void *threadContext, int priority) {
    (void)threadContext;
    (void)priority;
    return 0;
}

void BAE_UnlockAudioFrameThread(void) {
}

void BAE_LockAudioFrameThread(void) {
}

void BAE_BlockAudioFrameThread(void) {
}

uint32_t BAE_GetDeviceSamplesPlayedPosition(void) {
    return 0;
}

int32_t BAE_MaxDevices(void) {
    return 1;
}

void BAE_SetDeviceID(int32_t deviceID, void *deviceParameter) {
    (void)deviceID;
    (void)deviceParameter;
}

int BAE_Is8BitSupported(void) {
    return 0;  // We only support 16-bit
}

int BAE_Is16BitSupported(void) {
    return 1;
}

int BAE_IsStereoSupported(void) {
    return 1;
}

// BAE_GetMaxSamplePerSlice is defined in GenSynth.c

int BAE_IsMuted(void) {
    return 0;
}

int BAE_GetAudioBufferCount(void) {
    return 1;  // Single buffer for WASM (we generate audio on-demand)
}
