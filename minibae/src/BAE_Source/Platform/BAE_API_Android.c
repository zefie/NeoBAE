/*
    Copyright (c) 2009 Beatnik, Inc All rights reserved.
    
    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:
    
    Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
    
    Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    
    Neither the name of the Beatnik, Inc nor the names of its contributors
    may be used to endorse or promote products derived from this software
    without specific prior written permission.
    
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
    IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
    TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
    TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
    PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
    LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
    NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/*****************************************************************************/
/*
**  BAE_API_Android.c
**
**  This provides platform specfic functions for the Macintosh OS. This interface
**  for BAE is for the Sound Manager and sends buffer slices
**  through the multimedia system.
**
**  © Copyright 1995-2000 Beatnik, Inc, All Rights Reserved.
**  Written by Steve Hales
**
**  Beatnik products contain certain trade secrets and confidential and
**  proprietary information of Beatnik.  Use, reproduction, disclosure
**  and distribution by any means are prohibited, except pursuant to
**  a written license from Beatnik. Use of copyright notice is
**  precautionary and does not imply publication or disclosure.
**
**  Restricted Rights Legend:
**  Use, duplication, or disclosure by the Government is subject to
**  restrictions as set forth in subparagraph (c)(1)(ii) of The
**  Rights in Technical Data and Computer Software clause in DFARS
**  252.227-7013 or subparagraphs (c)(1) and (2) of the Commercial
**  Computer Software--Restricted Rights at 48 CFR 52.227-19, as
**  applicable.
**
**  Confidential-- Internal use only
**
**  History -
**  7/22/97     Created
**  11/11/97    Added BAE_MaxDevices & BAE_SetDeviceID & BAE_GetDeviceID & BAE_GetDeviceName
**  11/14/97    Removed multiple defined variable "mAudioFramesToGenerate"
**  12/16/97    Modified BAE_GetDeviceID and BAE_SetDeviceID to pass a device parameter pointer
**              that is specific for that device.
**  1/9/98      Added BAE_FileDelete
**  2/13/98     Modified BAE_AquireAudioCard to handle different sample rates
**  3/17/98     Added BAE_Is8BitSupported
**  3/23/98     Fixed a bug in BAE_GetDeviceName that wrote the name into space. Thanks DavidZ
**
**  6/5/98      Jim Nitchals RIP    1/15/62 - 6/5/98
**              I'm going to miss your irreverent humor. Your coding style and desire
**              to make things as fast as possible. Your collaboration behind this entire
**              codebase. Your absolute belief in creating the best possible relationships
**              from honesty and integrity. Your ability to enjoy conversation. Your business
**              savvy in understanding the big picture. Your gentleness. Your willingness
**              to understand someone else's way of thinking. Your debates on the latest
**              political issues. Your generosity. Your great mimicking of cartoon voices.
**              Your friendship. - Steve Hales
**
**  12/17/98    Added BAE_GetHardwareBalance & BAE_SetHardwareBalance
**  3/5/99      Changed context to threadContext to reflect what is really is used
**              in the system.
**  3/31/99     Added constant for Netscape plugin memory.
**  5/7/99      Added some post flight memory tests to keep us from the hairy
**              edge of MacOS lala land.
**  8/23/99     MSD:  fixed terminating zero bug in BAE_GetDeviceName()
**  9/3/99      Changed the BAE_FRAMES_PER_BLOCK to 2, in case Apple changes to
**              smaller buffers in the Sound Manager.
**  2/1/2000    Added new MacOS X "approved" sound double buffering. Set
**              USE_MAC_OS_X to TRUE to use the new callback method.
**              Note: its slower.
**  2/2/2000    Added a buffer clear function to eliminate a startup click. Changed
**              name in BAE_GetDeviceName to support new method of playback.
**  7/5/2000    Changed BAE_Allocate/BAE_Deallocate to use MacOS temporary
**              memory. This has the effect of expanding the current application
**              heap.
**              Fixed a recursive bug in BAE_Allocate. oops.
**  7/25/2000   Changed BAE_SizeOfPointer to use the new way to get size of temp
**              handles.
**              Placed a USE_TEMP_MEMORY flag to control if we use the temp memory
**              API instead of allocating through our natural heap.
**              Added PV_IsVirtualMemoryAvailable & PV_LockMemory & PV_UnlockMemory
**  9/29/2000   Added condition compiler flags for support on MacOS X
**  10/2/2000   Set default condition to not use SndDoubleBuffer. This allows us to
**              to run under X without recompiling. We run in the "classic" mode.
**  10/5/2000   Fixed a function name change in BAE_AquireAudioCard
**  2/14/2001   sh  When compiled for Carbon (MacOS9/X) BAE_GetDeviceName now
**                  reports a better name that's not specific to OS9/X
*/
/*****************************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/time.h>
#include <pthread.h>
#include <assert.h>
#include <jni.h>
#include <string.h>
#include <stdint.h>
#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "BAE_API.h"

// Use OpenSL ES for Android audio playback
#if defined(__ANDROID__)
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#endif


#undef FALSE
#undef TRUE
#define TRUE                            1
#define FALSE                           0


#define BAE_FRAMES_PER_BLOCK            1 // how much ms latancy can we handle (in ms)

typedef struct
{
    // hardware volume in BAE scale
    int16_t                       mUnscaledVolume;
    // balance scale -256 to 256 (left to right)
    int16_t                       mBalance;
    
    // size of audio buffers in bytes
    int32_t                            mAudioByteBufferSize;
    
    char                            mDataReady;
    char                            mDonePlaying;
    char                            mShutdownDoubleBuffer;
    
    // number of samples per audio frame to generate
    int32_t                            mAudioFramesToGenerate;
    
    // How many audio frames to generate at one time
    unsigned int                    mSynthFramesPerBlock;
    uint32_t                   mSamplesPlayed;

    //AudioUnit                       mAudioUnit;
    //AudioStreamBasicDescription     mFormat;

} AudioControlData;

static AudioControlData* sHardwareChannel;

// AAudio state (used on API >= 26)
// OpenSL ES state
#if defined(__ANDROID__)
static SLObjectItf gEngineObject = NULL;
static SLEngineItf gEngineEngine = NULL;
static SLObjectItf gOutputMixObject = NULL;
static SLObjectItf gPlayerObject = NULL;
static SLPlayItf gPlayerPlay = NULL;
static SLAndroidSimpleBufferQueueItf gBufferQueue = NULL;
static int16_t *g_audioBufferA = NULL;
static int16_t *g_audioBufferB = NULL;
static int g_currentBuffer = 0;
static int g_bufferFrames = 0;
static uint32_t g_os_sampleRate = 44100;
static uint32_t g_os_channels = 2;
static uint32_t g_os_bits = 16;
static uint32_t g_totalSamplesPlayed = 0;
// Software master volume & balance (range volume 0..256, balance -256..256)
static int16_t g_unscaled_volume = 256;
static int16_t g_balance = 0;
#if defined(__ANDROID__)
// forward declaration for callback (defined below)
static void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context);
#endif
#endif

// This file contains API's that need to be defined in order to get BAE (IgorAudio)
// to link and compile.

// **** System setup and cleanup functions
// Setup function. Called before memory allocation, or anything serious. Can be used to
// load a DLL, library, etc.
// return 0 for ok, or -1 for failure
int BAE_Setup(void)
{
   return(0);
}

// Cleanup function. Called after all memory and the last buffers are deallocated, and
// audio hardware is released. Can be used to unload a DLL, library, etc.
// return 0 for ok, or -1 for failure
int BAE_Cleanup(void)
{
   return(0);
}

// **** Memory management
// allocate a block of locked, zeroed memory. Return a pointer
void *BAE_Allocate(uint32_t size)
{
    assert(size > 0);
    void* data = (void*)calloc(1, size);
    return data;
}

// dispose of memory allocated with BAE_Allocate
void BAE_Deallocate(void *memoryBlock)
{
    free(memoryBlock);
}

// return memory used
uint32_t BAE_GetSizeOfMemoryUsed(void)
{
//  return g_memory_buoy;
   return(0);
}

// return max memory used
uint32_t BAE_GetMaxSizeOfMemoryUsed(void)
{
//  return g_memory_buoy_max;
   return(0);
}

// Given a memory pointer and a size, validate of memory pointer is a valid memory address
// with at least size bytes of data avaiable from the pointer.
// This is used to determine if a memory pointer and size can be accessed without
// causing a memory protection
// fault.
// return 0 for valid, or 1 for bad pointer, or 2 for not supported.
int BAE_IsBadReadPointer(void *memoryBlock, uint32_t size)
{
   memoryBlock = memoryBlock;
   size        = size;
   return(2);           // not supported, so this assumes that we don't have memory protection and will
   // not get an access violation when accessing memory outside of a valid memory block
}

// this will return the size of the memory pointer allocated with BAE_Allocate. Return
// 0 if you don't support this feature
uint32_t BAE_SizeOfPointer(void *memoryBlock)
{
    return 0;
}

// block move memory. This is basicly memmove, but its exposed to take advantage of
// special block move speed ups, various hardware has available.
void BAE_BlockMove(void *source, void *dest, uint32_t size)
{
    assert(dest != NULL && source != NULL);
    memmove(dest, source, size);
}

// **** Audio Card modifiers
// Return 1 if stereo hardware is supported, otherwise 0.
int BAE_IsStereoSupported(void)
{
    return 1;
}

// Return 1, if sound hardware support 16 bit output, otherwise 0.
int BAE_Is16BitSupported(void)
{
    return 1;
}

// Return 1, if sound hardware support 8 bit output, otherwise 0.
int BAE_Is8BitSupported(void)
{
    return 1;
}

// returned balance is in the range of -256 to 256. Left to right. If you're hardware doesn't support this
// range, just scale it.
int16_t BAE_GetHardwareBalance(void)
{
#if defined(__ANDROID__)
    return g_balance;
#else
    if (sHardwareChannel)
    {
        return sHardwareChannel->mBalance;
    }
    return 0;
#endif
}

// 'balance' is in the range of -256 to 256. Left to right. If you're hardware doesn't support this
// range, just scale it.
void BAE_SetHardwareBalance(int16_t balance)
{
    // pin balance to box
    if (balance > 256) { balance = 256; }
    if (balance < -256) { balance = -256; }
#if defined(__ANDROID__)
    g_balance = balance;
    // Re-apply volume scaling with new balance
    BAE_SetHardwareVolume(g_unscaled_volume);
#else
   // pin balance to box
   if (balance > 256)
   {
      balance = 256;
   }
   if (balance < -256)
   {
      balance = -256;
   }
   if (sHardwareChannel)
   {
       sHardwareChannel->mBalance = balance;
       BAE_SetHardwareVolume(sHardwareChannel->mUnscaledVolume);
   }
#endif
}

// returned volume is in the range of 0 to 256
int16_t BAE_GetHardwareVolume(void)
{
#if defined(__ANDROID__)
    return g_unscaled_volume;
#else
    if (sHardwareChannel)
    {
        return sHardwareChannel->mUnscaledVolume;
    }
    return 256;
#endif
}

// theVolume is in the range of 0 to 256
void BAE_SetHardwareVolume(int16_t newVolume)
{
#if defined(__ANDROID__)
    // Clamp volume
    if (newVolume > 256) newVolume = 256;
    if (newVolume < 0)   newVolume = 0;
    g_unscaled_volume = newVolume;
    // Nothing else needed here; scaling happens in callback.
#else
    uint32_t volume;
    int16_t     lbm, rbm;
    if (newVolume > 256) { newVolume = 256; }
    if (newVolume < 0)   { newVolume = 0; }
    sHardwareChannel->mUnscaledVolume = newVolume;
    if (sHardwareChannel->mBalance > 0) { lbm = 256 - sHardwareChannel->mBalance; rbm = 256; }
    else { lbm = 256; rbm = 256 + sHardwareChannel->mBalance; }
    volume = (((newVolume * lbm) / 256) << 16L) | ((newVolume * rbm) / 256);
    // TODO: Apply platform-specific hardware volume if supported.
#endif
}

// **** Timing services
// return microseconds
uint32_t BAE_Microseconds(void)
{
   static int           firstTime = TRUE;
   static uint32_t offset    = 0;
   struct timeval       tv;

   if (firstTime)
   {
      gettimeofday(&tv, NULL);
      offset    = tv.tv_sec;
      firstTime = FALSE;
   }
   gettimeofday(&tv, NULL);
   return(((tv.tv_sec - offset) * 1000000UL) + tv.tv_usec);
}

// wait or sleep this thread for this many microseconds
void BAE_WaitMicroseconds(uint32_t usec)
{
   usleep(usec);
}

int BAE_NewMutex(BAE_Mutex* lock, char *name, char *file, int lineno)
{
    pthread_mutex_t *pMutex = (pthread_mutex_t *) BAE_Allocate(sizeof(pthread_mutex_t));
    pthread_mutexattr_t attrib;
    pthread_mutexattr_init(&attrib);
    pthread_mutexattr_settype(&attrib, PTHREAD_MUTEX_RECURSIVE);
    // Create reentrant (within same thread) mutex.
    pthread_mutex_init(pMutex, &attrib);
    pthread_mutexattr_destroy(&attrib);
    *lock = (BAE_Mutex) pMutex;
    return 1; // ok
}

void BAE_AcquireMutex(BAE_Mutex lock)
{
    pthread_mutex_t *pMutex = (pthread_mutex_t*) lock;
    pthread_mutex_lock(pMutex);
}

void BAE_ReleaseMutex(BAE_Mutex lock)
{
    pthread_mutex_t *pMutex = (pthread_mutex_t*) lock;
    pthread_mutex_unlock(pMutex);
}

void BAE_DestroyMutex(BAE_Mutex lock)
{
    pthread_mutex_t *pMutex = (pthread_mutex_t*) lock;
    pthread_mutex_destroy(pMutex);
    BAE_Deallocate(pMutex);
}


// If no thread support, this will be called during idle times. Used for host
// rendering without threads.
void BAE_Idle(void *userContext)
{
//  userContext = userContext;
}

// **** File support
// Create a file, delete orignal if duplicate file name.
// Return -1 if error

// Given the fileNameSource that comes from the call BAE_FileXXXX, copy the name
// in the native format to the pointer passed fileNameDest.
void BAE_CopyFileNameNative(void *fileNameSource, void *fileNameDest)
{
    char *dest;
    char *src;
    
    if ((fileNameSource) && (fileNameDest))
    {
        dest = (char *)fileNameDest;
        src  = (char *)fileNameSource;
        if (src == NULL)
        {
            src = "";
        }
        if (dest)
        {
            while (*src)
            {
                *dest++ = *src++;
            }
            *dest = 0;
        }
    }
}

int32_t BAE_FileCreate(void *fileName)
{
    int file;

    file = open((char *)fileName, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (file != -1)
    {
        close(file);
    }
    return((file != -1) ? 0 : -1);  
}

int32_t BAE_FileDelete(void *fileName)
{
    if (fileName)
    {
        if (remove(fileName))
        {
            return(0);
        }
    }
    return(-1);
}


// Open a file
// Return -1 if error, otherwise file handle
intptr_t BAE_FileOpenForRead(void *fileName)
{
    if (fileName)
    {
        return (intptr_t)open((char *)fileName, O_RDONLY);
    }
    return -1;
}

intptr_t BAE_FileOpenForWrite(void *fileName)
{
    if (fileName)
    {
        return (intptr_t)open((char *)fileName, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    }
    return -1;
}

intptr_t BAE_FileOpenForReadWrite(void *fileName)
{
    if (fileName)
    {
        return (intptr_t)open((char *)fileName, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    }
    return -1;
}

// Close a file
void BAE_FileClose(intptr_t fileReference)
{
    if (fileReference != -1)
    {
        close((int)fileReference);
    }
}

// Read a block of memory from a file.
// Return -1 if error, otherwise length of data read.
int32_t BAE_ReadFile(intptr_t fileReference, void *pBuffer, int32_t bufferLength)
{
    if (pBuffer && bufferLength)
    {
        ssize_t bytesRead = read((int)fileReference, (char *)pBuffer, (size_t)bufferLength);
        return (int32_t)bytesRead;
    }
    return -1;
}

// Write a block of memory from a file
// Return -1 if error, otherwise length of data written.
int32_t BAE_WriteFile(intptr_t fileReference, void *pBuffer, int32_t bufferLength)
{
    if (pBuffer && bufferLength)
    {
        ssize_t bytesWritten = write((int)fileReference, (char *)pBuffer, (size_t)bufferLength);
        return (int32_t)bytesWritten;
    }
    return -1;
}

// set file position in absolute file byte position
// Return -1 if error, otherwise 0.
int32_t BAE_SetFilePosition(intptr_t fileReference, uint32_t filePosition)
{
    off_t result = lseek((int)fileReference, (off_t)filePosition, SEEK_SET);
    return (result == (off_t)-1) ? -1 : 0;
}

// get file position in absolute file bytes
uint32_t BAE_GetFilePosition(intptr_t fileReference)
{
    off_t pos = lseek((int)fileReference, 0, SEEK_CUR);
    return (pos == (off_t)-1) ? 0 : (uint32_t)pos;
}

// get length of file
uint32_t BAE_GetFileLength(intptr_t fileReference)
{
    off_t currentPos = lseek((int)fileReference, 0, SEEK_CUR);
    if (currentPos == (off_t)-1) return 0;
    
    off_t length = lseek((int)fileReference, 0, SEEK_END);
    if (length == (off_t)-1) return 0;
    
    lseek((int)fileReference, currentPos, SEEK_SET);
    return (uint32_t)length;
}

// set the length of a file. Return 0, if ok, or -1 for error
int BAE_SetFileLength(intptr_t fileReference, uint32_t newSize)
{
    int result = ftruncate((int)fileReference, (off_t)newSize);
    return (result == 0) ? 0 : -1;
}

// This function is called at render time with w route bus flag. If there's
// no change, return currentRoute, other wise return one of audiosys.h route values.
// This will change an active rendered's voice placement.
void BAE_ProcessRouteBus(int currentRoute, int32_t *pChannels, int count)
{
}

static void PV_ClearOutputBuffer(void *pBuffer, int16_t channels, int16_t bits, uint32_t frames)
{
    int16_t count;
    char      *dest8;
    int16_t *dest16;
    
    if (bits == 16)
    {
        // use 16 bit output
        dest16 = (int16_t *)pBuffer;
        for (count = 0; count < frames / 4; count++)
        {
            *dest16++ = 0;
            *dest16++ = 0;
            *dest16++ = 0;
            *dest16++ = 0;
            if (channels == 2)
            {
                // this is a slow way to do this!
                *dest16++ = 0;
                *dest16++ = 0;
                *dest16++ = 0;
                *dest16++ = 0;
            }
        }
    }
    else
    {
        // use 8 bit output
        dest8 = (char *)pBuffer;
        for (count = 0; count < frames / 4; count++)
        {
            *dest8++ = 0x80;
            *dest8++ = 0x80;
            *dest8++ = 0x80;
            *dest8++ = 0x80;
            if (channels == 2)
            {
                // this is a slow way to do this!
                *dest8++ = 0x80;
                *dest8++ = 0x80;
                *dest8++ = 0x80;
                *dest8++ = 0x80;
            }
        }
    }
}


// Return the number of 11 ms buffer blocks that are built at one time.
int BAE_GetAudioBufferCount(void)
{
   return(g_bufferFrames);
}

// Return the number of bytes used for audio buffer for output to card
int32_t BAE_GetAudioByteBufferSize(void)
{
   return(g_bufferFrames * g_os_channels * (g_os_bits / 8));
}

// Mute/unmute audio. Shutdown amps, etc.
// return 0 if ok, -1 if failed
int BAE_Mute(void)
{
   return(0);
}

int BAE_Unmute(void)
{
   return(0);
}


// **** Audio card support
// Acquire and enabled audio card
// return 0 if ok, -1 if failed
int BAE_AcquireAudioCard(void *threadContext, uint32_t sampleRate, uint32_t channels, uint32_t bits)
{
    (void)threadContext;
    SLresult r;
#define MINI_BAE_LOGD(fmt, ...) __android_log_print(ANDROID_LOG_DEBUG, "miniBAE", "BAE_AcquireAudioCard: " fmt, ##__VA_ARGS__)
#define MINI_BAE_LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, "miniBAE", "BAE_AcquireAudioCard: " fmt, ##__VA_ARGS__)
    MINI_BAE_LOGD("enter sampleRate=%u channels=%u bits=%u", sampleRate, channels, bits);
    // if already created, return success
    if (gPlayerObject != NULL) { MINI_BAE_LOGD("already acquired (gPlayerObject=%p)", (void*)gPlayerObject); return 0; }
    g_os_sampleRate = sampleRate;
    g_os_channels = channels;
    g_os_bits = bits;
    // Create engine
    r = slCreateEngine(&gEngineObject, 0, NULL, 0, NULL, NULL); if (r != SL_RESULT_SUCCESS) { MINI_BAE_LOGE("slCreateEngine failed r=%lu", (unsigned long)r); return -1; } else { MINI_BAE_LOGD("slCreateEngine ok"); }
    r = (*gEngineObject)->Realize(gEngineObject, SL_BOOLEAN_FALSE); if (r != SL_RESULT_SUCCESS) { MINI_BAE_LOGE("Engine Realize failed r=%lu", (unsigned long)r); return -1; }
    r = (*gEngineObject)->GetInterface(gEngineObject, SL_IID_ENGINE, &gEngineEngine); if (r != SL_RESULT_SUCCESS) { MINI_BAE_LOGE("GetInterface ENGINE failed r=%lu", (unsigned long)r); return -1; }
    // Create output mix
    r = (*gEngineEngine)->CreateOutputMix(gEngineEngine, &gOutputMixObject, 0, NULL, NULL); if (r != SL_RESULT_SUCCESS) { MINI_BAE_LOGE("CreateOutputMix failed r=%lu", (unsigned long)r); return -1; }
    r = (*gOutputMixObject)->Realize(gOutputMixObject, SL_BOOLEAN_FALSE); if (r != SL_RESULT_SUCCESS) { MINI_BAE_LOGE("OutputMix Realize failed r=%lu", (unsigned long)r); return -1; }
    // Determine frames per buffer using engine hint
    extern int16_t BAE_GetMaxSamplePerSlice(void);
    int16_t maxFrames = BAE_GetMaxSamplePerSlice();
    if (maxFrames <= 0) maxFrames = 512;
    g_bufferFrames = maxFrames;
    int channelsInt = (int)g_os_channels;
    size_t bufBytes = (size_t)g_bufferFrames * channelsInt * (g_os_bits/8);
    // allocate two buffers (zero-initialized to prevent pop on startup)
    g_audioBufferA = (int16_t*)calloc(1, bufBytes);
    g_audioBufferB = (int16_t*)calloc(1, bufBytes);
    if (!g_audioBufferA || !g_audioBufferB) {
        MINI_BAE_LOGE("buffer allocation failed frames=%d bytesPerBuf=%zu", (int)g_bufferFrames, bufBytes);
        if (g_audioBufferA) free(g_audioBufferA);
        if (g_audioBufferB) free(g_audioBufferB);
        return -1;
    }
    MINI_BAE_LOGD("allocated two buffers frames=%d bytesPerBuf=%zu", (int)g_bufferFrames, bufBytes);
    // configure PCM format
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
    SLDataFormat_PCM format_pcm = {SL_DATAFORMAT_PCM,
                                   (SLuint32)channelsInt,
                                   (SLuint32)(g_os_sampleRate * 1000),
                                   SL_PCMSAMPLEFORMAT_FIXED_16,
                                   SL_PCMSAMPLEFORMAT_FIXED_16,
                                   (channelsInt == 2) ? (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT) : SL_SPEAKER_FRONT_CENTER,
                                   SL_BYTEORDER_LITTLEENDIAN};
    SLDataSource audioSrc = { &loc_bufq, &format_pcm };
    SLDataLocator_OutputMix loc_outmix = { SL_DATALOCATOR_OUTPUTMIX, gOutputMixObject };
    SLDataSink audioSnk = { &loc_outmix, NULL };
    const SLInterfaceID ids[1] = { SL_IID_ANDROIDSIMPLEBUFFERQUEUE };
    const SLboolean req[1] = { SL_BOOLEAN_TRUE };
    r = (*gEngineEngine)->CreateAudioPlayer(gEngineEngine, &gPlayerObject, &audioSrc, &audioSnk, 1, ids, req); if (r != SL_RESULT_SUCCESS) { MINI_BAE_LOGE("CreateAudioPlayer failed r=%lu", (unsigned long)r); return -1; }
    r = (*gPlayerObject)->Realize(gPlayerObject, SL_BOOLEAN_FALSE); if (r != SL_RESULT_SUCCESS) { MINI_BAE_LOGE("Player Realize failed r=%lu", (unsigned long)r); return -1; }
    r = (*gPlayerObject)->GetInterface(gPlayerObject, SL_IID_PLAY, &gPlayerPlay); if (r != SL_RESULT_SUCCESS) { MINI_BAE_LOGE("GetInterface PLAY failed r=%lu", (unsigned long)r); return -1; }
    r = (*gPlayerObject)->GetInterface(gPlayerObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &gBufferQueue); if (r != SL_RESULT_SUCCESS) { MINI_BAE_LOGE("GetInterface BUFFERQUEUE failed r=%lu", (unsigned long)r); return -1; }
    // register callback
    r = (*gBufferQueue)->RegisterCallback(gBufferQueue, bqPlayerCallback, NULL); if (r != SL_RESULT_SUCCESS) { MINI_BAE_LOGE("RegisterCallback failed r=%lu", (unsigned long)r); return -1; }
    MINI_BAE_LOGD("AudioPlayer realized; priming buffers");
    // Prime both buffers
    g_currentBuffer = 0;
    bqPlayerCallback(gBufferQueue, NULL);
    bqPlayerCallback(gBufferQueue, NULL);
    MINI_BAE_LOGD("primed 2 buffers, setting play state");
    // Set to playing
    r = (*gPlayerPlay)->SetPlayState(gPlayerPlay, SL_PLAYSTATE_PLAYING); if (r != SL_RESULT_SUCCESS) { MINI_BAE_LOGE("SetPlayState PLAYING failed r=%lu", (unsigned long)r); return -1; }
    MINI_BAE_LOGD("successfully started playback (sampleRate=%u ch=%u bits=%u) gPlayerObject=%p", sampleRate, channels, bits, (void*)gPlayerObject);
    return 0;
}

// Release and free audio card.
// return 0 if ok, -1 if failed.
int BAE_ReleaseAudioCard(void *threadContext)
{
    (void)threadContext;
    // Stop player
    if (gPlayerPlay) { (*gPlayerPlay)->SetPlayState(gPlayerPlay, SL_PLAYSTATE_STOPPED); }
    if (gPlayerObject) { (*gPlayerObject)->Destroy(gPlayerObject); gPlayerObject = NULL; gPlayerPlay = NULL; gBufferQueue = NULL; }
    if (gOutputMixObject) { (*gOutputMixObject)->Destroy(gOutputMixObject); gOutputMixObject = NULL; }
    if (gEngineObject) { (*gEngineObject)->Destroy(gEngineObject); gEngineObject = NULL; gEngineEngine = NULL; }
    if (g_audioBufferA) { free(g_audioBufferA); g_audioBufferA = NULL; }
    if (g_audioBufferB) { free(g_audioBufferB); g_audioBufferB = NULL; }
    g_totalSamplesPlayed = 0;
    return 0;
}

// return device position in samples
uint32_t BAE_GetDeviceSamplesPlayedPosition(void)
{
    return g_totalSamplesPlayed;
}


// number of devices. ie different versions of the BAE connection. DirectSound and waveOut
// return number of devices. ie 1 is one device, 2 is two devices.
// NOTE: This function needs to function before any other calls may have happened.
int32_t BAE_MaxDevices(void)
{
   return(1);
}

// set the current device. device is from 0 to BAE_MaxDevices()
// NOTE:    This function needs to function before any other calls may have happened.
//          Also you will need to call BAE_ReleaseAudioCard then BAE_AcquireAudioCard
//          in order for the change to take place.
void BAE_SetDeviceID(int32_t deviceID, void *deviceParameter)
{
   deviceID;
   deviceParameter;
}

// return current device ID
// NOTE: This function needs to function before any other calls may have happened.
int32_t BAE_GetDeviceID(void *deviceParameter)
{
   deviceParameter;
   return(0);
}

// get deviceID name
// NOTE:    This function needs to function before any other calls may have happened.
//          Format of string is a zero terminated comma delinated C string.
//          "platform,method,misc"
//  example "MacOS,Sound Manager 3.0,SndPlayDoubleBuffer"
//          "WinOS,DirectSound,multi threaded"
//          "WinOS,waveOut,multi threaded"
//          "WinOS,VxD,low level hardware"
//          "WinOS,plugin,Director"
void BAE_GetDeviceName(int32_t deviceID, char *cName, uint32_t cNameLength)
{
   static char id[] =
   {
      "Android,OpenSLES"
   };
   uint32_t length;

   if ((cName) && (cNameLength))
   {
      cName[0] = 0;
      if (deviceID == 0)
      {
         length = sizeof(id) + 1;
         if (length > cNameLength)
         {
            length = cNameLength;
         }
         BAE_BlockMove((void *)id, (void *)cName, length);
         cName[length - 1] = '\0';
      }
   }
}


// EOF of BAE_API_Android.c

// OpenSL buffer queue callback (file-scoped)
#if defined(__ANDROID__)
static void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context)
{
    (void)context;
    int channelsInt = (int)g_os_channels;
    int16_t *buf = g_currentBuffer == 0 ? g_audioBufferA : g_audioBufferB;
    extern void BAE_BuildMixerSlice(void *threadContext, void *pAudioBuffer, int32_t bufferByteLength, int32_t sampleFrames);
    int32_t bytes = (int32_t)(g_bufferFrames * channelsInt * (g_os_bits/8));
    BAE_BuildMixerSlice(NULL, buf, bytes, g_bufferFrames);
    // Apply software master volume & balance (16-bit only)
    if (g_os_bits == 16 && g_unscaled_volume < 256) {
        int16_t vol = g_unscaled_volume;
        int16_t lbm, rbm;
        if (g_balance > 0) { lbm = 256 - g_balance; rbm = 256; }
        else { lbm = 256; rbm = 256 + g_balance; }
        int32_t lMul = (vol * lbm); // scaled by 256*256
        int32_t rMul = (vol * rbm);
        int frames = g_bufferFrames;
        if (channelsInt == 2) {
            int16_t *p = buf;
            for (int i = 0; i < frames; ++i) {
                int32_t L = (*p * lMul) >> 16; // (>>8 twice equivalent to /256/256)
                int32_t R = (*(p+1) * rMul) >> 16;
                if (L > 32767) L = 32767; else if (L < -32768) L = -32768;
                if (R > 32767) R = 32767; else if (R < -32768) R = -32768;
                *p++ = (int16_t)L; *p++ = (int16_t)R;
            }
        } else { // mono
            int16_t *p = buf;
            for (int i = 0; i < frames; ++i) {
                int32_t S = (*p * vol) >> 8; // single /256
                if (S > 32767) S = 32767; else if (S < -32768) S = -32768;
                *p++ = (int16_t)S;
            }
        }
    } else if (g_os_bits == 16 && g_balance != 0) {
        // Balance only (full volume). This keeps symmetry when volume=256 with balance adjustment.
        int16_t lbm, rbm;
        if (g_balance > 0) { lbm = 256 - g_balance; rbm = 256; }
        else { lbm = 256; rbm = 256 + g_balance; }
        int32_t lMul = lbm; // scaled by 256
        int32_t rMul = rbm;
        if (channelsInt == 2) {
            int16_t *p = buf;
            for (int i = 0; i < g_bufferFrames; ++i) {
                int32_t L = (*p * lMul) >> 8;
                int32_t R = (*(p+1) * rMul) >> 8;
                if (L > 32767) L = 32767; else if (L < -32768) L = -32768;
                if (R > 32767) R = 32767; else if (R < -32768) R = -32768;
                *p++ = (int16_t)L; *p++ = (int16_t)R;
            }
        }
    }
    if (gBufferQueue) {
        (*gBufferQueue)->Enqueue(gBufferQueue, buf, bytes);
    }
    g_totalSamplesPlayed += (uint32_t)g_bufferFrames;
    g_currentBuffer ^= 1;
    static int cbCount = 0; cbCount++;
    if ((cbCount & 0xFF) == 0) {
        __android_log_print(ANDROID_LOG_VERBOSE, "miniBAE", "bqPlayerCallback count=%d totalSamples=%u", cbCount, (unsigned)g_totalSamplesPlayed);
    }
}
#endif
