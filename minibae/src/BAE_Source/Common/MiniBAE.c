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
** "MiniBAE.c"
**
**  Generalized Audio Synthesis package presented in an oop fashion
**
**  © Copyright 1999-2001 Beatnik, Inc, All Rights Reserved.
**  Written by Mark Deggeller and Steve Hales
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
** Modification History:
**  1999.11.08      Created.
**  1999.11.16      Everything in place... Ready for testing!
**  1999.01.05  sh  Fixed bug in BAEMixer_GetGroovoidNameFromBank().
**  2000.01.06 MSD  Added NULL_OBJECT and RESOURCE_NOT_FOUND error codes.
**  2000.01.10 MSD  Made sure that BAESong can't get stuck in uninitialized mode
**                  caused during BAESong_Load*().
**                  Added PV_BAESong_InitLiveSong()
**  2000.01.11      Removed the Mac hook support.
**                  Fixed some random error enum translation problems.
**  2000.01.13      Changed XConvertNativeFileToXFILENAME() calls to
**                      XConvertPathToXFILENAME()
**                  Removed some MacOS deadwood I left in...
**  2000.01.14      Added BAEMixer_IsAudioActive()
**                  Added BAESong_AreMidiEventsPending()
**  2000.01.17      Added more parameter checking in BAEMixer_Open()
**  2000.01.18      Added check to see if mixer is allocated in GetMidiVoices,
**                      GetSoundVoices, and GetMixLevel.
**                  Added a linked list to the BAEMixer object to track song and
**                  sound objects associated with it
**  2000.01.19      Fixed problem where BAEMixer->pMixer could could change
**                  behind its back, on BAEMixer_Open() and BAEMixer_Close()
**  2000.01.20      Fixed some error code transistions. OPErr from BAEResult. Caused warnings
**                  Fixed defined structure BAEObjectListElem misspelling.
**  2000.01.31      Removed all direct access to GM_Mixer, GM_Song, and GM_Waveform
**                      structures they all go through a GenAPI function instead.
**                  Removed all references to MusicGlobals
**                  Reworked mechanism to know if the mixer has been allocated.
**  2000.02.01      Added BAE_TranslateQuality(), BAE_TranslateBAEQuality()
**                  Fixed lack of casting result of XNewPtr() in LoadCustomSample()
**  2000.02.25      Changed PV_BAESong_InitLiveSong & BAESong_LoadGroovoid &
**                      BAESong_LoadMidiFromMemory
**                  BAESong_LoadMidiFromFile & BAESong_LoadRmfFromMemory &
**                      BAESong_LoadRmfFromFile to call GM_SetSongMixer, or
**                      pass a GM_Mixer to GM_LoadSong
**  2000.03.01      Added multiple bank support
**  2000.03.02      Added BAEMixer_UnloadBanks(), added typedef BAEBankToken
**  2000.03.06 MSD  Added use of GM_GetProgramBank() in BAE_GetProgramBank()
**                  Added support for 32kHz, 40kHz
**  2000.03.07      Fixed bug in BAEMixer_UnloadBanks in which if there's no bank
**                      open it returns BAE_NO_ERROR rather than an unset variable.
**  2000.03.08      Fixed BAEMixer_GetBankVersion() to do the right thing.
**                  Added PV_BAEMixer_SubmitBankOrder()
**  2000.03.13 msd  cleaned up BAESound loading
**  2000.03.15 msd  fixed bug in PV_BAEMixer_RemoveObject()
**  2000.03.20 msd  fixed bugs in BAEMixer_BringBankToFront(), and BAEMixer_SendBankToBack().
**  2000.03.21 AER  Moved to new caching model where the mixer always caches
**                      and uses unique bank token to differentiate entries
**                  Removed references to GM cache functions
**                  Revised reference to GM_LoadSong to send bank tokens
**  2000.03.23 msd  fixed memory leaks in loading BAESong's; I wasn't freeing pXSong ptrs.
**  2000.03.23 msd  changed ...PitchOffset() functions to ...Transpose()
**  2000.03.28 msd  fixed off-by-one error in BAESong_GetLoopMax()
**                  created mVolume member of struct sBAESong, which shadows
**                      song volume.  this fixes the problem of BAESong_Stop()
**                      with fade=true resulting in the songs volume set to 0.
**                      Now, subsequent calls to BAESong_Start() will playback
**                      at the pre-fade volume.
**  2000.03.29 msd  Changed copyright and modification history format
**                  Fixed bug BAESong_GetBankVersion() when no banks loaded.
**                  Removed BAESong_Set/GetLoopFlag()
**                  Renamed BAESong_Set/GetLoopMax() BAESong_Set/GetLoops()
**                  Added parameter checking to BAEMixer_SetMasterSoundEffectsVolume()
**                  Eliminated MCU-side access to DSP-based GM_Song ptr in
**                      BAEMixer_GetRealtimeStatus().
**  2000.03.29 AER  Added GM_ClearSampleCache to free potential reource leaks
**                      prior to releasing the mixer pointer
**  2000.03.30 AER  Relegated use of GM_ClearSampleCache to release builds
**  2000.03.31 MSD  BAEMixer_SetAudioLatency() is no longer supported in the
**                      dual-cpu build.
**  2000.04.03 sh   Removed some warnings in BAESound_LoadCustomSample
**  2000.10.17  sh  Added BAEMixer_Idle
**  2000.10.18  sh  Added BAEMixer_GetMemoryUsed, BAESound_GetMemoryUsed,
**                  BAESong_GetMemoryUsed
**  2000.11.8   sh  Added copyright.
**  2000.11.29  tom Added BAEMixer_StartOutputToFile, BAEMixer_StopOutputToFile,
**                  BAEMixer_ServiceOutputToFile - ported from BAE.c
**  2000.12.01  tom moved OutputToFile globals from MiniBAE.h to resolve some possible linker conflicts
**  2000.12.01  sh  Fixed linker issues with MCU only. Put test for USE_CREATION_API around
**                  file output write code. Removed C++ stuff. Only need one way to write
**                  files.
**  2001.03.28  sh  Added BAEStream call functions.
**                  Added BAEMixer_SetFadeRate & BAEMixer_GetFadeRate
**  2001.05.01  sh  Fixed some warnings on MacOS
**  2001.05.01  se  Fixed uninitialized memory read.
**  1/24/2002   sh  Removed XGetHardwareVolume/XSetHardwareVolume, they were
**                  duplicated by BAE_GetHardwareVolume/BAE_SetHardwareVolume
**                  Fixed declaration of XDuplicateMemory
**  2/15/2002   sh  Changed BAESong_SetLoops to set meta loops to false if a zero is
**                  passed.
**  2/20/2002   sh  When calling BAEMixer_DisengageAudio, explictly shutdown
**                  all active midi/pcm voices. This will kill any hung voices.
**                  Not a perfect fix, but does help with the problem.
**  10/29/2002  sh  Added mutex locks around managing memory links.
*/
/*****************************************************************************/

#include "MiniBAE.h"
#if USE_MPEG_ENCODER == TRUE
#include "XMPEG_BAE_API.h" /* for MPG_Encode* encoder API prototypes */
#endif
#include "X_API.h"
#include "GenSnd.h"
#include "GenPriv.h"
#include "GenRMI.h"
#include "X_Formats.h"
#include "BAE_API.h"
#include "X_Assert.h"
#include <limits.h>
#include <stdint.h>
#include "bankinfo.h" // embedded bank metadata (hash -> friendly)
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
#include "GenSF2_FluidSynth.h"
#if USE_XMF_SUPPORT == TRUE
#include "GenXMF.h"
#endif
#endif


#if _BUILT_IN_PATCHES == TRUE
    #ifdef __ANDROID__
        #include "../miniBAEDroid/BAEPatches.h"
    #else
        #include "BAEPatches.h"
    #endif
#endif

#if USE_FLAC_ENCODER == TRUE
#include "FLAC/stream_encoder.h"
// Forward declaration of FLAC encoding function from GenSoundFiles.c
OPErr PV_WriteFromMemoryFLACFile(XFILENAME *file, GM_Waveform const *pAudioData, XWORD formatTag);
// Wave format constant
#define X_WAVE_FORMAT_PCM 0x0001
#endif
#include "sha1mini.h" // hashing for friendly name resolution

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

void process_and_send_audio(int16_t *rawAudio, int length)
{
    // Call the JavaScript function "postAudioData" with the pointer
    EM_ASM({ window.miniBAEInstance.postAudioData($0, $1); }, rawAudio, length);
}
#endif

// Fallback helper: scan RMF memory directly for SONG resource if cache lookup fails.
static SongResource *PV_FallbackFindSongInRMFMemory(const void *data, uint32_t totalLen, int16_t desiredIndex, XLongResourceID *outID, int32_t *outSize)
{
    if (!data || totalLen < 16 || desiredIndex < 0)
    {
        return NULL;
    }
    unsigned char *ub = (unsigned char *)data;
    uint32_t mapID = (uint32_t)ub[0] << 24 | (uint32_t)ub[1] << 16 | (uint32_t)ub[2] << 8 | (uint32_t)ub[3];
    if (mapID != 0x4952455A)
    {
        return NULL;
    } // 'IREZ'
    uint32_t resourceCount = (uint32_t)ub[8] << 24 | (uint32_t)ub[9] << 16 | (uint32_t)ub[10] << 8 | (uint32_t)ub[11];
    if (resourceCount == 0 || resourceCount > 4096)
    {
        return NULL;
    }
    uint32_t nextOffset = 12; // start after map header
    int songFoundCount = 0;
    for (uint32_t resIndex = 0; resIndex < resourceCount; resIndex++)
    {
        if (nextOffset + 16 > totalLen)
        {
            return NULL;
        }
        unsigned char *base = ub + nextOffset;
        uint32_t rawNext = (uint32_t)base[0] << 24 | (uint32_t)base[1] << 16 | (uint32_t)base[2] << 8 | (uint32_t)base[3];
        uint32_t rawType = (uint32_t)base[4] << 24 | (uint32_t)base[5] << 16 | (uint32_t)base[6] << 8 | (uint32_t)base[7];
        uint32_t rawID = (uint32_t)base[8] << 24 | (uint32_t)base[9] << 16 | (uint32_t)base[10] << 8 | (uint32_t)base[11];
        uint8_t nameLen = base[12];
        uint32_t lenFieldPos = nextOffset + 13 + nameLen; // after len byte + name
        if (lenFieldPos + 4 > totalLen)
        {
            return NULL;
        }
        unsigned char *lenPtr = ub + lenFieldPos;
        uint32_t rawResLen = (uint32_t)lenPtr[0] << 24 | (uint32_t)lenPtr[1] << 16 | (uint32_t)lenPtr[2] << 8 | (uint32_t)lenPtr[3];
        uint32_t dataStart = lenFieldPos + 4;
        if (dataStart + rawResLen > totalLen)
        {
            return NULL;
        }
        if (rawType == 0x534F4E47)
        { // 'SONG'
            if (songFoundCount == desiredIndex)
            {
                if (outID)
                    *outID = rawID;
                if (outSize)
                    *outSize = (int32_t)rawResLen;
                SongResource *copy = (SongResource *)XNewPtr((int32_t)rawResLen);
                if (copy)
                {
                    XBlockMove(ub + dataStart, copy, (int32_t)rawResLen);
                }
                return copy;
            }
            songFoundCount++;
        }
        uint32_t computedNext = dataStart + rawResLen;
        if (rawNext == 0 || rawNext < nextOffset || rawNext > totalLen)
        {
            rawNext = computedNext;
        }
        if (rawNext <= nextOffset)
        {
            return NULL;
        }
        nextOffset = rawNext;
        if (nextOffset >= totalLen)
        {
            break;
        }
    }
    return NULL;
}

#define TRACKING 0

const char *BAE_GetVersion()
{
    size_t maxStrSize = 64;
    char *versionString = (char *)malloc(sizeof(char) * maxStrSize);
    if (!versionString)
        return "";
#ifdef _VERSION
    snprintf(versionString, maxStrSize, "%s", _VERSION);
#else
    snprintf(versionString, maxStrSize, "built on %s", __DATE__);
#endif
    return versionString;
}

const char *BAE_GetCompileInfo()
{
    size_t maxStrSize = 128;
    char *versionString = (char *)malloc(sizeof(char) * maxStrSize);
    if (!versionString)
        return "";
#ifdef __EMSCRIPTEN__
#ifdef __cplusplus
    snprintf(versionString, maxStrSize, "clang++ v%d.%d, emscripten v%d.%d", __clang_major__, __clang_minor__, __EMSCRIPTEN_major__, __EMSCRIPTEN_minor__);
#else
    snprintf(versionString, maxStrSize, "clang v%d.%d, emscripten v%d.%d", __clang_major__, __clang_minor__, __EMSCRIPTEN_major__, __EMSCRIPTEN_minor__);
#endif
#elif __clang_major_
#ifdef __cplusplus
    snprintf(versionString, maxStrSize, "clang++ v%d.%d", __clang_major__, __clang_minor__);
#else
    snprintf(versionString, maxStrSize, "clang v%d.%d", __clang_major__, __clang_minor__);
#endif
#elif __MINGW32__
#ifdef __cplusplus
    snprintf(versionString, maxStrSize, "mingw32 v%d.%d (g++ v%d.%d)", __MINGW32_MAJOR_VERSION, __MINGW32_MINOR_VERSION, __GNUC__, __GNUC_MINOR__);
#else
    snprintf(versionString, maxStrSize, "mingw32 v%d.%d (gcc v%d.%d)", __MINGW32_MAJOR_VERSION, __MINGW32_MINOR_VERSION, __GNUC__, __GNUC_MINOR__);
#endif
#elif __GNUC__
#ifdef __cplusplus
    snprintf(versionString, maxStrSize, "g++ v%d.%d", __GNUC__, __GNUC_MINOR__);
#else
    snprintf(versionString, maxStrSize, "gcc v%d.%d", __GNUC__, __GNUC_MINOR__);
#endif
#else
    snprintf(versionString, maxStrSize, "UNKNOWN");
#endif
    return versionString;
}

const char *BAE_GetFeatureString()
{
    static char featBuf[512];
    featBuf[0] = '\0';
    XBOOL first = TRUE;

    // Debugging
#ifdef _DEBUG
    const char *build = "Debug Build (Stripped)";
#elif (_FULL_DEBUG == TRUE)
    const char *build = "Debug Build (w/ Symbols)";
#else
    const char *build = "Release Build";
#endif
    if (build && build[0])
    {
        snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", build);
        first = FALSE;
    }
    
    // Audio backend
#if (X_PLATFORM == X_SDL2)
    const char *audio = "SDL2";
#elif (X_PLATFORM == X_SDL3)
    const char *audio = "SDL3";
#elif (X_PLATFORM == X_WIN95)
    const char *audio = "DirectSound";
#else
    const char *audio = NULL;
#endif
    if (audio && audio[0])
    {
        snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", audio);
        first = FALSE;
    }

    // Built-in patches
#ifdef _BUILT_IN_PATCHES
    const char *patches = "Built-in Patches";
    if (patches && patches[0])
    {
        snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", patches);
        first = FALSE;
    }
#endif

    // SF2 support
#if _USING_FLUIDSYNTH == TRUE
    char *sf2supp = NULL;
    if (is_libinstpatch_loaded()) {
        sf2supp = (char *)"SF2/SF3/SFO/DLS (libinstpatch) Support";
    } else {
        sf2supp = (char *)"SF2/SF3/SFO/DLS Support";
    }
#elif USE_SF2_SUPPORT == TRUE && USE_VORBIS_DECODER == TRUE
    char *sf2supp = "SF2/SF3/SFO Support";
#elif USE_SF2_SUPPORT == TRUE
    char *sf2supp = "SF2 Support";
#else
    char *sf2supp = NULL;
#endif

#if USE_SF2_SUPPORT == TRUE
#if _USING_FLUIDSYNTH == TRUE
    // FluidSynth
    if (sf2supp && sf2supp[0]) {
        static char sf2supp_buf[64];
        snprintf(sf2supp_buf, sizeof(sf2supp_buf), "%s (FluidSynth v%s)", sf2supp, fluid_version_str());
        sf2supp = sf2supp_buf;
    }
#endif
#endif

    if (sf2supp && sf2supp[0])
    {
        snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", sf2supp);
        first = FALSE;
    }

#if _ZEFI_GUI == TRUE
#if SUPPORT_KARAOKE == TRUE
    const char *karaoke = "Karaoke Support";
    if (karaoke && karaoke[0])
    {
        snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", karaoke);
        first = FALSE;
    }
#else
    const char *karaoke = "No Karaoke Support";
    if (karaoke && karaoke[0])
    {
        snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", karaoke);
        first = FALSE;
    }
#endif

    // Playlist support
#if SUPPORT_PLAYLIST == TRUE
    const char *playlist = "Playlist Support";
#else
    const char *playlist = NULL;
#endif
    if (playlist && playlist[0])
    {
        snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", playlist);
        first = FALSE;
    }

    // MIDI hardware
#if SUPPORT_MIDI_HW == TRUE
    const char *midi = "MIDI Hardware Support";
#else
    const char *midi = "No MIDI Hardware Support";
#endif
    if (midi && midi[0])
    {
        snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", midi);
        first = FALSE;
    }
#endif

    // MP3 support

#if USE_MPEG_DECODER == TRUE && USE_MPEG_ENCODER == TRUE
    const char *mp3 = "Full MP3 Support";
#elif USE_MPEG_DECODER == TRUE && USE_MPEG_ENCODER != TRUE
    const char *mp3 = "MP3 Decoder Support";
#elif USE_MPEG_DECODER != TRUE && USE_MPEG_ENCODER == TRUE
    const char *mp3 = "MP3 Encoder Support";
#else
    const char *mp3 = "No MP3 Support";
#endif

    if (mp3 && mp3[0])
    {
        snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", mp3);
        first = FALSE;
    }

    // FLAC support
#if USE_FLAC_DECODER == TRUE && USE_FLAC_ENCODER == TRUE
    const char *flac = "Full FLAC Support";
#elif USE_FLAC_DECODER == TRUE && USE_FLAC_ENCODER != TRUE
    const char *flac = "FLAC Decoder Support";
#elif USE_FLAC_DECODER != TRUE && USE_FLAC_ENCODER == TRUE
    const char *flac = "FLAC Encoder Support";
#else
    const char *flac = "No FLAC Support";
#endif
    if (flac && flac[0])
    {
        snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", flac);
        first = FALSE;
    }

    // Vorbis support
#if USE_VORBIS_DECODER == TRUE && USE_VORBIS_ENCODER == TRUE
    const char *vorbis = "Full Vorbis Support";
#elif USE_VORBIS_DECODER == TRUE && USE_VORBIS_ENCODER != TRUE
    const char *vorbis = "Vorbis Decoder Support";
#elif USE_VORBIS_DECODER != TRUE && USE_VORBIS_ENCODER == TRUE
    const char *vorbis = "Vorbis Encoder Support";
#else
    const char *vorbis = "No Vorbis Support";
#endif
    if (vorbis && vorbis[0])
    {
        snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", vorbis);
        first = FALSE;
    }

    // If nothing was added, return an empty string
    if (featBuf[0] == '\0')
        featBuf[0] = '\0';

    return featBuf;
}

const char *BAE_GetCurrentCPUArchitecture()
{ // Get current architecture, detects many architectures. Coded by Freak. Modified to append -SDL when built for SDL2.
  // Append suffix at compile time without allocating new memory.
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
    return "i686";
#elif defined(__ARM_ARCH_2__)
    return "ARM2";
#elif defined(__ARM_ARCH_3__) || defined(__ARM_ARCH_3M__)
    return "ARM3";
#elif defined(__ARM_ARCH_4T__) || defined(__TARGET_ARM_4T)
    return "ARM4T";
#elif defined(__ARM_ARCH_5__) || defined(__ARM_ARCH_5E__)
    return "ARM5";
#elif defined(__ARM_ARCH_6T2__)
    return "ARM6T2";
#elif defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) || defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6Z__) || defined(__ARM_ARCH_6ZK__)
    return "ARM6";
#elif defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__)
    return "ARM7"; // Generic ARMv7
#elif defined(__ARM_ARCH_7A__)
    return "ARM7A";
#elif defined(__ARM_ARCH_7R__)
    return "ARM7R";
#elif defined(__ARM_ARCH_7M__)
    return "ARM7M";
#elif defined(__ARM_ARCH_7S__)
    return "ARM7S";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "ARM64";
#elif defined(mips) || defined(__mips__) || defined(__mips)
    return "MIPS";
#elif defined(__sh__)
    return "SuperH";
#elif defined(__powerpc64__) || defined(__PPC64__) || defined(__ppc64__) || defined(_ARCH_PPC64)
    return "PowerPC64";
#elif defined(__powerpc) || defined(__powerpc__) || defined(__POWERPC__) || defined(__ppc__) || defined(__PPC__) || defined(_ARCH_PPC)
    return "PowerPC";
#elif defined(__sparc__) || defined(__sparc)
    return "SPARC";
#elif defined(__m68k__)
    return "M68K";
#elif defined(__EMSCRIPTEN__)
    return "WebAssembly (Emscripten)";
#else
    return "UNKNOWN";
#endif
}

// Private types/structs
// ----------------------------------------------------------------------------

#if TRACKING
typedef enum
{
    BAE_MIXER_OBJECT = 1,
    BAE_SONG_OBJECT,
    BAE_SOUND_OBJECT,
    BAE_STREAM_OBJECT
} BAE_OBJECT_TYPE;

typedef struct BAEObjectListElem
{
    void *object;
    BAE_OBJECT_TYPE type;
    struct BAEObjectListElem *next;
} BAEObjectListElem;
#endif

#define OBJECT_ID FOUR_CHAR('i', 'g', 'o', 'r') //  'igor'

struct sBAEMixer
{
    int32_t mID;
    GM_Mixer *pMixer; // Don't dereference pMixer, since if you are running
                      // the dual-CPU version of MiniBAE, this will be only a reference.
    BAE_BOOL audioEngaged;
    XFILE *pPatchFiles;
    int16_t numPatchFiles;
#if TRACKING
    BAEObjectListElem *pObjects;
#endif
    BAE_UNSIGNED_FIXED mFadeRate;

    BAE_AudioTaskCallbackPtr pTaskProc;
    void *mTaskReference;

    int mMuteCount;
    int mMutedVolumeLevel;
    BAE_Mutex mLock;
};

struct sBAESong
{
    int32_t mID;
    BAEMixer mixer;
    GM_Song *pSong; // Don't dereference pSong, since if you are running
                    // the dual-CPU version of MiniBAE, this will be only a reference.

    BAE_Mutex mLock;
    BAE_SongCallbackPtr mCallback;
    void *mCallbackReference;

    BAE_SongControllerCallbackPtr
        mControllerCallback;
    void *mControllerCallbackReference;

    BAE_BOOL mInMixer;
#if !TRACKING
    BAE_BOOL mValid;
#endif
    int mInstrumentsLoadedCount;

    XSWORD mVolume;
    int mRouteBus;
    BAE_BOOL mAutoBuzz;
    BAE_BOOL mAutoFlash;
    BAE_BOOL mHasEmbeddedBank;
    char *mTitle;
};

struct sBAESound
{
    int32_t mID;
    BAEMixer mixer;
    GM_Waveform *pWave; // Don't dereference pWave, since if you are running
                        // the dual-CPU version of MiniBAE, this will be only a reference.

    BAE_Mutex mLock;
    VOICE_REFERENCE voiceRef;
    BAE_UNSIGNED_FIXED mPauseVariable;
    BAE_UNSIGNED_FIXED mVolume;

    BAE_SoundCallbackPtr mCallback;
    void *mCallbackReference;

    int mRouteBus;
    BAE_BOOL mAutoBuzz;
    BAE_BOOL mAutoFlash;
    uint32_t mLoopCount;   // loop count for infinite/finite looping
    uint32_t mCurrentLoop; // current loop iteration
#if !TRACKING
    BAE_BOOL mValid;
#endif
};

#if USE_STREAM_API == TRUE
struct sBAEStream
{
    int32_t mID;
    BAEMixer mixer;

    BAE_Mutex mLock;
    STREAM_REFERENCE mSoundStreamVoiceReference;
    unsigned int mLoop : 1;
    unsigned int mPrerolled : 1;
    uint32_t mPlaybackLength;
    BAE_UNSIGNED_FIXED mVolumeState;
    int16_t mPanState;
    BAESampleInfo mStreamSampleInfo;
    BAE_UNSIGNED_FIXED mPauseVariable;
    BAE_AudioStreamCallbackPtr mCallback;
    uint32_t mCallbackReference;
#if !TRACKING
    BAE_BOOL mValid;
#endif
};
#endif

// MiniBAE.c globals
// ----------------------------------------------------------------------------
static XShortResourceID midiSongCount = 0; // everytime a new song is loaded, this is increments
// Friendly name cache for loaded banks (token -> sha1 + friendly)
typedef struct
{
    BAEBankToken token;
    char sha1[41];
    const char *friendly; // points into kEmbeddedBanks name or NULL
} BAE_FriendlyCacheEntry;

static BAE_FriendlyCacheEntry g_bankFriendlyCache[32];
static int g_bankFriendlyCacheCount = 0;

static void PV_RegisterBankFriendly(BAEBankToken token, const char *sha1Hex)
{
    if (!token || !sha1Hex)
        return;
    if (g_bankFriendlyCacheCount >= (int)(sizeof(g_bankFriendlyCache) / sizeof(g_bankFriendlyCache[0])))
        return;
    // Avoid duplicates
    for (int i = 0; i < g_bankFriendlyCacheCount; i++)
    {
        if (g_bankFriendlyCache[i].token == token)
            return;
    }
    BAE_FriendlyCacheEntry *e = &g_bankFriendlyCache[g_bankFriendlyCacheCount];
    e->token = token;
    strncpy(e->sha1, sha1Hex, 40);
    e->sha1[40] = '\0';
    e->friendly = NULL;
    for (int i = 0; i < kBankCount; i++)
    {
        if (strcmp(sha1Hex, kBanks[i].sha1) == 0)
        {
            e->friendly = kBanks[i].name;
            break;
        }
    }
    g_bankFriendlyCacheCount++;
}

static const char *PV_FindBankFriendly(BAEBankToken token)
{
    for (int i = 0; i < g_bankFriendlyCacheCount; i++)
    {
        if (g_bankFriendlyCache[i].token == token)
        {
            return g_bankFriendlyCache[i].friendly;
        }
    }
    return NULL;
}

// Remove a bank's friendly name cache entry when the bank is unloaded so a
// subsequently loaded bank that reuses the same underlying XFILE pointer
// value doesn't inherit the prior bank's friendly name (stale display bug).
static void PV_UnregisterBankFriendly(BAEBankToken token)
{
    if (!token || g_bankFriendlyCacheCount <= 0)
        return;
    for (int i = 0; i < g_bankFriendlyCacheCount; i++)
    {
        if (g_bankFriendlyCache[i].token == token)
        {
            // Compact array in-place
            for (int j = i + 1; j < g_bankFriendlyCacheCount; j++)
            {
                g_bankFriendlyCache[j - 1] = g_bankFriendlyCache[j];
            }
            g_bankFriendlyCacheCount--;
            break;
        }
    }
}

BAEResult BAE_GetBankFriendlyName(BAEMixer mixer, BAEBankToken token, char *outName, uint32_t outNameSize)
{
    if (!outName || outNameSize == 0)
        return BAE_PARAM_ERR;
    outName[0] = '\0';
    if (!mixer || !token)
        return BAE_NULL_OBJECT;
    const char *n = PV_FindBankFriendly(token);
    if (!n)
        return BAE_RESOURCE_NOT_FOUND;
    strncpy(outName, n, outNameSize - 1);
    outName[outNameSize - 1] = '\0';
    return BAE_NO_ERROR;
}
// this is used as an ID for song callbacks and such

// globals for *OutputToFile support. these were BAEMixer class members from BAE

#define DUMP_OUTPUTFILE 0

#if DUMP_OUTPUTFILE
FILE *fp;
#endif

BAE_BOOL mWritingToFile;
BAEFileType mWriteToFileType;
void *mWritingToFileReference;
void *mWritingEncoder;
void *mWritingDataBlock;
uint32_t mWritingDataBlockSize;

#if USE_FLAC_ENCODER != FALSE
// FLAC encoding state for streaming
void *mFLACEncoder;
void *mFLACAccumulatedSamples;
uint32_t mFLACAccumulatedFrames;
uint32_t mFLACMaxAccumulatedFrames;
uint32_t mFLACChannels;
uint32_t mFLACBitsPerSample;
uint32_t mFLACSampleRate;
XFILENAME mFLACOutputFile;
#endif

// Prototypes
// ----------------------------------------------------------------------------
BAEResult BAE_TranslateOPErr(OPErr theErr);
OPErr BAE_TranslateBAErr(BAEResult theErr);

#if USE_HIGHLEVEL_FILE_API != FALSE
AudioFileType BAE_TranslateBAEFileType(BAEFileType fileType);
#endif

#if REVERB_USED != REVERB_DISABLED
ReverbMode BAE_TranslateFromBAEReverb(BAEReverbType igorVerb);
BAEReverbType BAE_TranslateToBAEReverb(ReverbMode r);
#endif

// Vorbis quality helper
float BAE_TranslateVorbisTypeToQuality(BAECompressionType ct);

// Private function prototypes
// ----------------------------------------------------------------------------
#if TRACKING
static BAEResult PV_BAEMixer_AddObject(BAEMixer mixer, void *theObject, BAE_OBJECT_TYPE type);
static BAEResult PV_BAEMixer_RemoveObject(BAEMixer mixer, void *theObject, BAE_OBJECT_TYPE type);
static BAE_BOOL PV_BAEMixer_ValidateObject(BAEMixer mixer, void *theObject, BAE_OBJECT_TYPE type);
#endif

static BAEResult PV_BAEMixer_AddBank(BAEMixer mixer, XFILE newPatchFile);
static void PV_BAEMixer_SubmitBankOrder(BAEMixer mixer);

static BAE_FIXED PV_CalculateTimeDeltaForFade(
    BAE_FIXED sourceVolume,
    BAE_FIXED destVolume,
    BAE_FIXED timeInMiliseconds);

// song related
static BAEResult PV_BAESong_InitLiveSong(BAESong song, BAE_BOOL addToMixer);

static void PV_BAESong_Stop(BAESong song, BAE_BOOL startFade);
static void PV_BAESong_Unload(BAESong song);
static void PV_BAESong_SetCallback(BAESong song, BAE_SongCallbackPtr pCallback,
                                   void *callbackReference);

static BAETerpMode PV_TranslateTerpModeToBAETerpMode(TerpMode tm_in);

// sound related
static void PV_BAESound_SetCallback(BAESound sound, BAE_SoundCallbackPtr pCallback,
                                    void *callbackReference);
static void PV_BAESound_Unload(BAESound sound);
static void PV_BAESound_Stop(BAESound sound, BAE_BOOL startFade);

extern char mCopyright[];
extern char mAboutNames[];

#if 0
#pragma mark -
#pragma mark##### Support functions #####
#pragma mark -
#endif

// Read a file into memory and return an allocated pointer
static XPTR PV_GetFileAsData(XFILENAME *pFile, int32_t *pSize)
{
    XPTR data;

    if (XGetFileAsData(pFile, &data, pSize))
    {
        data = NULL;
    }
    return data;
}

#if REVERB_USED != REVERB_DISABLED
static const ReverbMode translateInternal[] = {
    REVERB_NO_CHANGE,
    REVERB_TYPE_1,
    REVERB_TYPE_2,
    REVERB_TYPE_3,
    REVERB_TYPE_4,
    REVERB_TYPE_5,
    REVERB_TYPE_6,
    REVERB_TYPE_7,
    REVERB_TYPE_8,
    REVERB_TYPE_9,
    REVERB_TYPE_10,
    REVERB_TYPE_11};
static const BAEReverbType translateExternal[] = {
    BAE_REVERB_NO_CHANGE,
    BAE_REVERB_TYPE_1,
    BAE_REVERB_TYPE_2,
    BAE_REVERB_TYPE_3,
    BAE_REVERB_TYPE_4,
    BAE_REVERB_TYPE_5,
    BAE_REVERB_TYPE_6,
    BAE_REVERB_TYPE_7,
    BAE_REVERB_TYPE_8,
    BAE_REVERB_TYPE_9,
    BAE_REVERB_TYPE_10,
    BAE_REVERB_TYPE_11};
// translate reverb types from BAEReverbType to ReverbMode
ReverbMode BAE_TranslateFromBAEReverb(BAEReverbType igorVerb)
{
    ReverbMode r;
    int16_t count;

    r = REVERB_TYPE_1;
    for (count = 0; count < MAX_REVERB_TYPES; count++)
    {
        if (igorVerb == translateExternal[count])
        {
            r = translateInternal[count];
            break;
        }
    }
    return r;
}

// translate reverb types to BAEReverbType from ReverbMode
BAEReverbType BAE_TranslateToBAEReverb(ReverbMode r)
{
    BAEReverbType igorVerb;
    int16_t count;

    igorVerb = BAE_REVERB_TYPE_1;
    for (count = 0; count < MAX_REVERB_TYPES; count++)
    {
        if (r == translateInternal[count])
        {
            igorVerb = translateExternal[count];
            break;
        }
    }
    return igorVerb;
}
#endif

static const BAEResult translateExternalError[] = {
    BAE_NO_ERROR,
    BAE_BUFFER_TOO_SMALL,
    BAE_NOT_SETUP,
    BAE_PARAM_ERR,
    BAE_MEMORY_ERR,
    BAE_BAD_INSTRUMENT,
    BAE_BAD_MIDI_DATA,
    BAE_ALREADY_PAUSED,
    BAE_ALREADY_RESUMED,
    BAE_DEVICE_UNAVAILABLE,
    BAE_STILL_PLAYING,
    BAE_NO_SONG_PLAYING,
    BAE_TOO_MANY_SONGS_PLAYING,
    BAE_NO_VOLUME,
    BAE_NO_FREE_VOICES,
    BAE_STREAM_STOP_PLAY,
    BAE_BAD_FILE_TYPE,
    BAE_GENERAL_BAD,
    BAE_BAD_SAMPLE,
    BAE_BAD_FILE,
    BAE_FILE_NOT_FOUND,
    BAE_NOT_REENTERANT,
    BAE_SAMPLE_TOO_LARGE,
    BAE_UNSUPPORTED_HARDWARE,
    BAE_ABORTED,
    BAE_RESOURCE_NOT_FOUND,
    BAE_NULL_OBJECT};

static const OPErr translateInternalError[] = {
    NO_ERR,
    BUFFER_TO_SMALL,
    NOT_SETUP,
    PARAM_ERR,
    MEMORY_ERR,
    BAD_INSTRUMENT,
    BAD_MIDI_DATA,
    ALREADY_PAUSED,
    ALREADY_RESUMED,
    DEVICE_UNAVAILABLE,
    STILL_PLAYING,
    NO_SONG_PLAYING,
    TOO_MANY_SONGS_PLAYING,
    NO_VOLUME,
    NO_FREE_VOICES,
    STREAM_STOP_PLAY,
    BAD_FILE_TYPE,
    GENERAL_BAD,
    BAD_SAMPLE,
    BAD_FILE,
    FILE_NOT_FOUND,
    NOT_REENTERANT,
    SAMPLE_TO_LARGE,
    UNSUPPORTED_HARDWARE,
    ABORTED_PROCESS,
    RESOURCE_NOT_FOUND,
    NULL_OBJECT};

// Translate from OPErr to BAEResult
BAEResult BAE_TranslateOPErr(OPErr theErr)
{
    BAEResult igorErr;
    int16_t count, max;

    igorErr = BAE_GENERAL_ERR;
    max = sizeof(translateExternalError) / sizeof(BAEResult);
    for (count = 0; count < max; count++)
    {
        if (translateInternalError[count] == theErr)
        {
            igorErr = translateExternalError[count];
            break;
        }
    }
    return igorErr;
}

#if USE_SF2_SUPPORT == TRUE
XBOOL BAESong_IsSF2Song(BAESong song)
{
    if (!song || !song->pSong)
        return FALSE;
    return GM_IsSF2Song(song->pSong);
}
#endif 

// Translate from BAEResult to OPErr
OPErr BAE_TranslateBAErr(BAEResult theErr)
{
    OPErr igorErr;
    int16_t count, max;

    igorErr = GENERAL_BAD;
    max = sizeof(translateExternalError) / sizeof(BAEResult);
    for (count = 0; count < max; count++)
    {
        if (translateExternalError[count] == theErr)
        {
            igorErr = translateInternalError[count];
            break;
        }
    }
    return igorErr;
}

#if USE_HIGHLEVEL_FILE_API != FALSE
AudioFileType BAE_TranslateBAEFileType(BAEFileType fileType)
{
    AudioFileType haeFileType;

    haeFileType = FILE_INVALID_TYPE;
    switch (fileType)
    {
    case BAE_AIFF_TYPE:
        haeFileType = FILE_AIFF_TYPE;
        break;
    case BAE_WAVE_TYPE:
        haeFileType = FILE_WAVE_TYPE;
        break;
#if USE_MPEG_DECODER == TRUE || USE_MPEG_ENCODER == TRUE
    case BAE_MPEG_TYPE:
        haeFileType = FILE_MPEG_TYPE;
        break;
#endif
#if USE_FLAC_DECODER == TRUE || USE_FLAC_ENCODER == TRUE
    case BAE_FLAC_TYPE:
        haeFileType = FILE_FLAC_TYPE;
        break;
#endif
#if USE_VORBIS_DECODER == TRUE || USE_VORBIS_ENCODER == TRUE
    case BAE_VORBIS_TYPE:
        haeFileType = FILE_VORBIS_TYPE;
        break;
#endif
    case BAE_AU_TYPE:
        haeFileType = FILE_AU_TYPE;
        break;
    default:
        break;
    }
    return haeFileType;
}
#endif

// ------------------------------------------------------------------
// BAEMixer Functions
// ------------------------------------------------------------------
// Global default velocity curve (0..4). Applied to songs when they are created or loaded.
static int g_defaultVelocityCurve = 0;

BAEResult BAE_SetDefaultVelocityCurve(int curveType)
{
    if (curveType < 0)
        curveType = 0;
    if (curveType > 4)
        curveType = 4;
    g_defaultVelocityCurve = curveType;
    return BAE_NO_ERROR;
}

BAEResult BAE_GetDefaultVelocityCurve(int *outCurveType)
{
    if (!outCurveType)
        return BAE_PARAM_ERR;
    *outCurveType = g_defaultVelocityCurve;
    return BAE_NO_ERROR;
}
#if 0
#pragma mark -
#pragma mark##### BAEMixer #####
#pragma mark -
#endif

// BAEMixer_New
// ------------------------------------
//
//
BAEMixer BAEMixer_New(void)
{
    BAEMixer mixer;
/*
    char c;

    // must reference these so they stay linked
    c = mCopyright[0];
    c = mAboutNames[0];
*/

    mixer = (BAEMixer)XNewPtr(sizeof(struct sBAEMixer));
    if (mixer)
    {
        if (BAE_NewMutex(&mixer->mLock, "bae", "mix", __LINE__))
        {
            BAE_AcquireMutex(mixer->mLock);

            mixer->mID = OBJECT_ID;
            mixer->pMixer = NULL;
            mixer->audioEngaged = FALSE;
            mixer->pPatchFiles = NULL;
#if TRACKING
            mixer->pObjects = NULL;
#endif
            mixer->pTaskProc = NULL;
            mixer->mTaskReference = NULL;
            mixer->mFadeRate = FLOAT_TO_FIXED(2.2);
            mixer->mMutedVolumeLevel = BAE_GetHardwareVolume();

            BAE_ReleaseMutex(mixer->mLock);
        }
        else
        {
            XDisposePtr(mixer);
            mixer = NULL;
        }
    }
    return mixer;
}

// BAEMixer_Delete()
// ------------------------------------
//
//
BAEResult BAEMixer_Delete(BAEMixer mixer)
{
    BAEResult err;
#if TRACKING
    BAEObjectListElem *elem, *next;
#endif

    err = BAEMixer_Close(mixer);
    if (err == BAE_NO_ERROR)
    {
        BAE_AcquireMutex(mixer->mLock);
#if TRACKING
        elem = mixer->pObjects;
        while (elem)
        {
            next = elem->next;
            switch (elem->type)
            {
            case BAE_SONG_OBJECT:
                ((BAESong)elem->object)->mixer = NULL;
                break;

            case BAE_SOUND_OBJECT:
                ((BAESound)elem->object)->mixer = NULL;
                break;
#if USE_STREAM_API == TRUE
            case BAE_STREAM_OBJECT:
                ((BAEStream)elem->object)->mixer = NULL;
                break;
#endif
            case BAE_MIXER_OBJECT:
                BAE_ASSERT(FALSE);
                break;
            }
            XDisposePtr(elem);
            elem = next;
        }
#endif
        mixer->mID = 0;

        BAEMixer_UnloadBanks(mixer);

        BAE_ReleaseMutex(mixer->mLock);
        BAE_DestroyMutex(mixer->mLock);
        XDisposePtr(mixer);
    }
    return err;
}

#if USE_CALLBACKS
static void PV_TaskCallback(void *context, void *reference)
{
    BAEMixer mixer = (BAEMixer)reference;

    if (mixer)
    {
        (*mixer->pTaskProc)(mixer->mTaskReference);
    }
}
#endif

// mixer callbacks and tasks
BAEResult BAEMixer_SetAudioTask(BAEMixer mixer, BAE_AudioTaskCallbackPtr pTaskProc, void *taskReference)
{
#if USE_CALLBACKS
    OPErr err = NO_ERR;

    if (mixer)
    {
        mixer->pTaskProc = pTaskProc;
        mixer->mTaskReference = taskReference;
        GM_SetAudioTask(PV_TaskCallback, mixer);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
#else
    return BAE_NOT_SETUP;
#endif
}

BAEResult BAEMixer_GetAudioTask(BAEMixer mixer, BAE_AudioTaskCallbackPtr *pResult)
{
#if USE_CALLBACKS
    BAE_AudioTaskCallbackPtr task = NULL;
    OPErr err = NO_ERR;

    if (mixer)
    {
        task = mixer->pTaskProc;
    }
    else
    {
        err = NULL_OBJECT;
    }
    if (pResult)
    {
        *pResult = task;
    }
    return BAE_TranslateOPErr(err);
#else
    return BAE_NOT_SETUP;
#endif
}

// BAEMixer_GetMemoryUsed()
// --------------------------------------
//
//
BAEResult BAEMixer_GetMemoryUsed(BAEMixer mixer, uint32_t *pOutResult)
{
    uint32_t size;

    size = 0;
    if (mixer)
    {
        // mixer size
        size += XGetPtrSize((XPTR)mixer);
        size += sizeof(GM_Mixer);
    }
    if (pOutResult)
    {
        *pOutResult = size;
    }
    return BAE_NO_ERROR;
}

#if TRACKING
// PV_BAEMixer_AddObject()
// ------------------------------------
//
//
static BAEResult PV_BAEMixer_AddObject(BAEMixer mixer, void *theObject, BAE_OBJECT_TYPE type)
{
    OPErr err;
    BAEObjectListElem *elem;

    err = NO_ERR;

    if (mixer)
    {
        BAE_AcquireMutex(mixer->mLock);
        if (theObject)
        {
            elem = (BAEObjectListElem *)XNewPtr(sizeof(BAEObjectListElem));
            if (elem)
            {
                // insert object at start of list
                elem->object = theObject;
                elem->type = type;
                elem->next = mixer->pObjects;
                mixer->pObjects = elem;
            }
            else
            {
                err = MEMORY_ERR;
            }
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(mixer->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// PV_BAEMixer_RemoveObject()
// ------------------------------------
//
//
static BAEResult PV_BAEMixer_RemoveObject(BAEMixer mixer, void *theObject, BAE_OBJECT_TYPE type)
{
    OPErr err;
    BAEObjectListElem *elem, **prev;

    err = NO_ERR;

    if (mixer)
    {
        BAE_AcquireMutex(mixer->mLock);
        if (theObject)
        {
            elem = mixer->pObjects;
            prev = &(mixer->pObjects);
            while (elem)
            {
                if (elem->object == theObject && elem->type == type)
                {
                    *prev = elem->next;
                    XDisposePtr(elem);
                    break;
                }
                else
                {
                    prev = &(elem->next);
                    elem = elem->next;
                }
            }
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(mixer->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// Given a valid mixer and object that was added with PV_BAEMixer_AddObject
// return TRUE if still in the list, otherwise false.
static BAE_BOOL PV_BAEMixer_ValidateObject(BAEMixer mixer, void *theObject, BAE_OBJECT_TYPE type)
{
    BAE_BOOL ok;
    BAEObjectListElem *elem, **prev;

    ok = FALSE;
    if (mixer)
    {
        BAE_AcquireMutex(mixer->mLock);
        if (theObject)
        {
            elem = mixer->pObjects;
            prev = &(mixer->pObjects);
            while (elem)
            {
                if (elem->object == theObject && elem->type == type)
                {
                    // found it
                    ok = TRUE;
                    break;
                }
                else
                {
                    prev = &(elem->next);
                    elem = elem->next;
                }
            }
        }
        BAE_ReleaseMutex(mixer->mLock);
    }
    return ok;
}
#endif

// get and set the fade time. Will be used for all song/sound fades
BAEResult BAEMixer_SetFadeRate(BAEMixer mixer, BAE_UNSIGNED_FIXED rate)
{
    BAEResult err;

    err = BAE_NO_ERROR;
    if (mixer)
    {
        mixer->mFadeRate = rate;
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// private function. Will return current fade rate or 2.2 if there's an error
static BAE_UNSIGNED_FIXED PV_GetDefaultMixerFadeRate(BAEMixer mixer)
{
    BAE_UNSIGNED_FIXED rate;

    rate = FLOAT_TO_FIXED(2.2);
    BAEMixer_GetFadeRate(mixer, &rate);
    return rate;
}

BAEResult BAEMixer_GetFadeRate(BAEMixer mixer, BAE_UNSIGNED_FIXED *outFadeRate)
{
    BAEResult err;

    err = BAE_NO_ERROR;
    if (mixer)
    {
        if (outFadeRate)
        {
            *outFadeRate = mixer->mFadeRate;
        }
        else
        {
            err = BAE_PARAM_ERR;
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// BAEMixer_GetMaxDeviceCount()
// ------------------------------------
//
//
BAEResult BAEMixer_GetMaxDeviceCount(BAEMixer mixer, int32_t *outMaxDeviceCount)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (outMaxDeviceCount)
        {
#if USE_DEVICE_ENUM_SUPPORT == TRUE
            *outMaxDeviceCount = GM_MaxDevices();
#else
            *outMaxDeviceCount = 0;
#endif
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_SetCurrentDevice()
// ------------------------------------
//
//
BAEResult BAEMixer_SetCurrentDevice(BAEMixer mixer, int32_t deviceID, void *deviceParameter)
{
    OPErr err;
    BAE_BOOL isOpen;
    int32_t deviceCount;

    err = NO_ERR;
    if (mixer)
    {
#if USE_DEVICE_ENUM_SUPPORT == TRUE
        BAEMixer_GetMaxDeviceCount(mixer, &deviceCount);
        if (deviceID < deviceCount)
        {
            BAEMixer_IsOpen(mixer, &isOpen);
            if (isOpen)
            {
                BAEMixer_DisengageAudio(mixer); // shutdown from hardware
            }
            GM_SetDeviceID(deviceID, deviceParameter); // change to new device
            BAEMixer_IsOpen(mixer, &isOpen);
            if (isOpen)
            {
                BAEMixer_ReengageAudio(mixer); // connect back to audio with new device
            }
        }
#else
        deviceID = deviceID;
        deviceParameter = deviceParameter;
        isOpen = isOpen;
        deviceCount = deviceCount;
#endif
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetCurrentDevice()
// ------------------------------------
//
//
BAEResult BAEMixer_GetCurrentDevice(BAEMixer mixer, void *deviceParameter, int32_t *outDeviceID)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (outDeviceID)
        {
            *outDeviceID = GM_GetDeviceID(deviceParameter);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetDeviceName()
// ------------------------------------
//
//
BAEResult BAEMixer_GetDeviceName(BAEMixer mixer, int32_t deviceID, char *cName, uint32_t cNameLength)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (cName && cNameLength)
        {
#if USE_DEVICE_ENUM_SUPPORT == TRUE
            GM_GetDeviceName(deviceID, cName, cNameLength);
#else
            deviceID = deviceID;
            cName = cName;
            cNameLength = cNameLength;
#endif
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_SetDefaultReverb()
// BAEMixer_GetDefaultReverb()
// --------------------------------------
// Sets/Gets the master default reverb
//
static ReverbMode g_defaultReverbType = BAE_REVERB_NONE;
BAEResult BAEMixer_SetDefaultReverb(BAEMixer mixer, BAEReverbType verb)
{
#if REVERB_USED != REVERB_DISABLED
    g_defaultReverbType = BAE_TranslateFromBAEReverb(verb);
    GM_SetReverbType(BAE_TranslateFromBAEReverb(verb));
    return BAE_NO_ERROR;
#else
    return BAE_NOT_SETUP;
#endif
}

BAEResult BAEMixer_GetDefaultReverb(BAEMixer mixer, BAEReverbType *pOutResult)
{
#if REVERB_USED != REVERB_DISABLED
    ReverbMode r = GM_GetReverbType();
    *pOutResult = BAE_TranslateToBAEReverb(r);
    return BAE_NO_ERROR;
#else
    return BAE_NOT_SETUP;
#endif
}

// BAEMixer_IsOpen()
// ------------------------------------
//
//
BAEResult BAEMixer_IsOpen(BAEMixer mixer, BAE_BOOL *outIsOpen)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (outIsOpen)
        {
            *outIsOpen = mixer->audioEngaged;
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_Is16BitSupported()
// ------------------------------------
//
//
BAEResult BAEMixer_Is16BitSupported(BAEMixer mixer, BAE_BOOL *outIsSupported)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (outIsSupported)
        {
            *outIsSupported = (BAE_BOOL)XIs16BitSupported();
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_Is8BitSupported()
// ------------------------------------
//
//
BAEResult BAEMixer_Is8BitSupported(BAEMixer mixer, BAE_BOOL *outIsSupported)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (outIsSupported)
        {
            *outIsSupported = (BAE_BOOL)XIs8BitSupported();
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// PV_GetDefaultTerp()
// ------------------------------------
//
//
static TerpMode PV_GetDefaultTerp(BAETerpMode t)
{
    TerpMode theTerp;

    switch (t)
    {
#if USE_DROP_SAMPLE == TRUE
    case BAE_DROP_SAMPLE:
        theTerp = E_AMP_SCALED_DROP_SAMPLE;
        break;
#endif
#if USE_TERP1 == TRUE
    case BAE_2_POINT_INTERPOLATION:
        theTerp = E_2_POINT_INTERPOLATION;
        break;
#endif
    default:
    case BAE_LINEAR_INTERPOLATION:
#if USE_TERP2 == TRUE
        theTerp = E_LINEAR_INTERPOLATION;
#endif
#if LOOPS_USED == U3232_LOOPS
        theTerp = E_LINEAR_INTERPOLATION_U3232;
#elif LOOPS_USED == FLOAT_LOOPS
        theTerp = E_LINEAR_INTERPOLATION_FLOAT;
#endif
        break;
    }
    return theTerp;
}

// BAEMixer_Open()
// ------------------------------------
//
//
BAEResult BAEMixer_Open(BAEMixer mixer,
                        BAERate q,
                        BAETerpMode t,
                        BAEAudioModifiers am,
                        int16_t maxSongVoices,
                        int16_t maxSoundVoices,
                        int16_t mixLevel,
                        BAE_BOOL engageAudio)
{
    OPErr theErr;
    Rate theRate = Q_RATE_8K;
    TerpMode theTerp = 0;
    AudioModifiers theMods = 0;

    theErr = NO_ERR;
    if (mixer)
    {
        // if we've never setup the audio engine, do that now
        if (mixer->pMixer == NULL)
        {
#if (X_PLATFORM == X_MACINTOSH) && (CPU_TYPE == k68000)
            // we're running on a MacOS 68k, so we've got to restrict the features in order to get decent playback
            q = BAE_RATE_11K;
            am &= ~BAE_USE_STEREO;    // mono only
            am &= ~BAE_STEREO_FILTER; // don't allow
            am |= BAE_DISABLE_REVERB; // don't allow
                                      //          am &= ~BAE_USE_16;
            switch (q)
            {
            case BAE_44K: // no way
            case BAE_48K:
            case BAE_24K:
            case BAE_22K_TERP_44K:
                q = BAE_22K;
                break;
            }
            t = BAE_DROP_SAMPLE;

            switch (t)
            {
            case BAE_LINEAR_INTERPOLATION:
                t = BAE_2_POINT_INTERPOLATION;
                break;
            }
#endif
            theRate = (Rate)q;
            if (theRate == (Rate)BAE_RATE_INVALID)
            {
                theErr = PARAM_ERR;
            }

            switch (t)
            {
            case BAE_DROP_SAMPLE:
            case BAE_2_POINT_INTERPOLATION:
            case BAE_LINEAR_INTERPOLATION:
                theTerp = PV_GetDefaultTerp(t);
                break;
            default:
                theErr = PARAM_ERR;
                break;
            }

            theMods = M_NONE;
            if ((am & BAE_USE_16) && XIs16BitSupported())
            {
                theMods |= M_USE_16;
            }
            else
            {
                am &= BAE_USE_16; // 8 bit
            }

            if ((am & BAE_USE_STEREO) && XIsStereoSupported())
            {
                theMods |= M_USE_STEREO;
                if (am & BAE_STEREO_FILTER)
                {
                    theMods |= M_STEREO_FILTER;
                }
            }
            else
            {
                am &= ~BAE_USE_STEREO; // mono
            }
            if (am & BAE_DISABLE_REVERB)
            {
                theMods |= M_DISABLE_REVERB;
            }

            if (maxSongVoices < 0)
            {
                theErr = PARAM_ERR;
            }

            if (maxSoundVoices < 0)
            {
                theErr = PARAM_ERR;
            }

            if (mixLevel <= 0)
            {
                theErr = PARAM_ERR;
            }
#if 0
//#if X_PLATFORM == X_MACINTOSH
// make sure we have at least 1MB of free memory
            if (FreeMem() < (1024L * 1024L))
            {
                theErr = MEMORY_ERR;
            }
#endif

            // check to see if the version numbers match the header files and
            // the built codebase
            {
                int16_t major, minor, subminor;

                BAEMixer_GetMixerVersion(mixer, &major, &minor, &subminor);
                if ((major != BAE_VERSION_MAJOR) || (minor != BAE_VERSION_MINOR) ||
                    (subminor != BAE_VERSION_SUB_MINOR))
                {
                    theErr = GENERAL_BAD;
                }
            }

            // make sure our internal voice count matches our external one
            if (BAE_MAX_VOICES != MAX_VOICES)
            {
                theErr = GENERAL_BAD;
            }
            if (theErr == NO_ERR)
            {
                theErr = GM_InitGeneralSound(NULL, theRate, theTerp, theMods,
                                             maxSongVoices,
                                             mixLevel,
                                             maxSoundVoices,
                                             &mixer->pMixer);
                if (theErr == NO_ERR)
                {
                    if (engageAudio)
                    {
                        theErr = GM_ResumeGeneralSound(NULL);
                        if (theErr == NO_ERR)
                        {
                            mixer->audioEngaged = TRUE;
                        }
                    }
                }
            }
        }
        else
        {
            theErr = NOT_REENTERANT; // can't be reentrant
        }
    }
    else
    {
        theErr = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(theErr);
}

// BAEMixer_Close()
// ------------------------------------
//
//
BAEResult BAEMixer_Close(BAEMixer mixer)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        // Shut down mixer
        if (mixer->pMixer)
        {
#if USE_CALLBACKS
            GM_SetAudioTask(NULL, NULL);
#endif
            if (mixer->audioEngaged)
            {
                // Close up sound manager BEFORE releasing memory!
                GM_StopHardwareSoundManager(NULL);
                mixer->audioEngaged = FALSE;
            }
            GM_FinisGeneralSound(NULL, mixer->pMixer);
            mixer->pMixer = NULL;
        }
        else
        {
            if (mixer->audioEngaged)
            {
                // Mixer is NULL, but active? This should not occur!
                BAE_ASSERT(FALSE);
                mixer->audioEngaged = FALSE;
            }
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetMixerVersion()
// ------------------------------------
//
//
BAEResult BAEMixer_GetMixerVersion(BAEMixer mixer, int16_t *pVersionMajor, int16_t *pVersionMinor, int16_t *pVersionSubMinor)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (pVersionMajor && pVersionMinor && pVersionSubMinor)
        {
            *pVersionMajor = BAE_VERSION_MAJOR;
            *pVersionMinor = BAE_VERSION_MINOR;
            *pVersionSubMinor = BAE_VERSION_SUB_MINOR;
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// PV_BAEMixer_AddBank()
// ------------------------------------
//
//
static BAEResult PV_BAEMixer_AddBank(BAEMixer mixer, XFILE newPatchFile)
{
    OPErr err;
    XFILE *newList;

    err = NO_ERR;

    if (mixer)
    {
        BAE_AcquireMutex(mixer->mLock);
        newList = (XFILE *)XNewPtr(sizeof(XFILE) * (mixer->numPatchFiles + 1));
        if (newList)
        {
            // copy old list, and append new file to end
            XBlockMove(mixer->pPatchFiles, newList, sizeof(XFILE) * mixer->numPatchFiles);
            newList[mixer->numPatchFiles] = newPatchFile;

            // dispose of old list, and attach new list
            XDisposePtr(mixer->pPatchFiles);
            mixer->pPatchFiles = newList;
            mixer->numPatchFiles++;

            XFileUseThisResourceFile(newPatchFile);            
        }
        else
        {
            err = MEMORY_ERR;
        }
        BAE_ReleaseMutex(mixer->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_AddBankFromMemory()
// ------------------------------------
//
//
BAEResult BAEMixer_AddBankFromMemory(BAEMixer mixer, void *pAudioFile, uint32_t fileSize, BAEBankToken *outToken)
{
    BAEResult theErr;
    XFILE newPatchFile;

    theErr = BAE_NO_ERROR;
    if (mixer)
    {
        newPatchFile = XFileOpenResourceFromMemory(pAudioFile, fileSize, FALSE);
        if (newPatchFile)
        {
            theErr = PV_BAEMixer_AddBank(mixer, newPatchFile);
            if (outToken)
            {
                *outToken = (BAEBankToken)newPatchFile;
            }
            // Compute sha1 of memory bank for friendly name cache
            if (theErr == BAE_NO_ERROR)
            {
                unsigned char digest[20];
                char hex[41];
                sha1mini((const unsigned char *)pAudioFile, fileSize, digest);
                static const char *hexmap = "0123456789abcdef";
                for (int i = 0; i < 20; i++)
                {
                    hex[i * 2] = hexmap[digest[i] >> 4];
                    hex[i * 2 + 1] = hexmap[digest[i] & 15];
                }
                hex[40] = '\0';
                PV_RegisterBankFriendly((BAEBankToken)newPatchFile, hex);
            }
        }
        else
        {
            theErr = BAE_BAD_FILE;
        }
    }
    else
    {
        theErr = BAE_NULL_OBJECT;
    }
    return theErr;
}

#if _BUILT_IN_PATCHES == TRUE
// BAEMixer_AddBankFromFile()
// ------------------------------------
//
// Loads the built in patches via BAEMixer_AddBankFromMemory
//
BAEResult BAEMixer_LoadBuiltinBank(BAEMixer mixer, BAEBankToken *outToken) {
    extern const unsigned char BAE_PATCHES[];
    extern const unsigned long BAE_PATCHES_size;
    return BAEMixer_AddBankFromMemory(mixer, (void *)BAE_PATCHES, BAE_PATCHES_size, outToken);
}
#endif

// BAEMixer_AddBankFromFile()
// ------------------------------------
//
//
BAEResult BAEMixer_AddBankFromFile(BAEMixer mixer, BAEPathName pAudioPathName, BAEBankToken *outToken)
{
    BAEResult theErr;
    XFILE newPatchFile;
    XFILENAME theFile;

    theErr = BAE_NO_ERROR;
    if (mixer)
    {
        XConvertPathToXFILENAME(pAudioPathName, &theFile);
        newPatchFile = XFileOpenResource(&theFile, TRUE);
        if (newPatchFile)
        {
            theErr = PV_BAEMixer_AddBank(mixer, newPatchFile);
            if (outToken)
            {
                *outToken = (BAEBankToken)newPatchFile;
            }
            // After loading from file, read its bytes to compute sha1
            if (theErr == BAE_NO_ERROR)
            {
                // Use XFile routines: open resource already returns handle; need raw data pointer & size
                // Simplest: reopen file normally and read bytes.
                FILE *f = fopen(pAudioPathName, "rb");
                if (f)
                {
                    fseek(f, 0, SEEK_END);
                    long sz = ftell(f);
                    if (sz > 0 && sz < (32 * 1024 * 1024))
                    {
                        fseek(f, 0, SEEK_SET);
                        unsigned char *buf = (unsigned char *)malloc(sz);
                        if (buf)
                        {
                            size_t rd = fread(buf, 1, sz, f);
                            if (rd == (size_t)sz)
                            {
                                unsigned char dg[20];
                                char hx[41];
                                sha1mini(buf, sz, dg);
                                static const char *hm = "0123456789abcdef";
                                for (int i = 0; i < 20; i++)
                                {
                                    hx[i * 2] = hm[dg[i] >> 4];
                                    hx[i * 2 + 1] = hm[dg[i] & 15];
                                }
                                hx[40] = '\0';
                                PV_RegisterBankFriendly((BAEBankToken)newPatchFile, hx);
                            }
                            free(buf);
                        }
                    }
                    fclose(f);
                }
            }
        }
        else
        {
            theErr = BAE_BAD_FILE;
        }
    }
    else
    {
        theErr = BAE_NULL_OBJECT;
    }
    return theErr;
}

// BAEMixer_UnloadBank()
// ------------------------------------
//
//
BAEResult BAEMixer_UnloadBank(BAEMixer mixer, BAEBankToken token)
{
    XFILE *pPatchFiles;
    XFILE patchFile;
    OPErr err;
    BAE_BOOL ok = FALSE;
    int i, j;

    err = NO_ERR;

    if (mixer)
    {
        BAE_AcquireMutex(mixer->mLock);

        pPatchFiles = mixer->pPatchFiles;
        patchFile = (XFILE)token;

        for (i = 0; i < mixer->numPatchFiles; i++)
        {
            if (patchFile == pPatchFiles[i])
            {
                ok = TRUE; // found it!
                // Invalidate friendly name cache entry BEFORE closing to avoid
                // potential pointer reuse mapping to stale friendly string.
                PV_UnregisterBankFriendly(token);
                XFileClose(patchFile);

                // compact the array.
                // This will leave a unused slot on the end, but that's ok.
                for (j = i + 1; j < mixer->numPatchFiles; j++)
                {
                    pPatchFiles[j - 1] = pPatchFiles[j];
                }
                mixer->numPatchFiles--;

                // was that the last one? kill the array.
                // Don't really need to do this, but logically cleaner.
                if (mixer->numPatchFiles == 0)
                {
                    XDisposePtr(mixer->pPatchFiles);
                    mixer->pPatchFiles = NULL;
                }
            }
        }
        BAE_ReleaseMutex(mixer->mLock);

        if (!ok)
        {
            err = RESOURCE_NOT_FOUND;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

BAEResult BAEMixer_UnloadBanks(BAEMixer mixer)
{
    BAEResult err;

    if (mixer)
    {
        err = BAE_NO_ERROR;
        // Close patch files
        while (mixer->numPatchFiles)
        {
            err = BAEMixer_UnloadBank(mixer, (BAEBankToken)mixer->pPatchFiles[mixer->numPatchFiles - 1]);
            if (err)
                break;
        }
#if USE_SF2_SUPPORT == TRUE
        if (mixer->pMixer && mixer->pMixer->isSF2) {
            mixer->pMixer->isSF2 = false;
        }
#endif        
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// BAEMixer_BringBankToFront()
// ------------------------------------
//
//
BAEResult BAEMixer_BringBankToFront(BAEMixer mixer, BAEBankToken token)
{
    int i, j;
    XFILE *pPatchFiles;
    short numPatchFiles;
    XFILE file;
    BAE_BOOL ok;
    OPErr err;

    err = NO_ERR;
    ok = FALSE;

    if (mixer)
    {
        pPatchFiles = mixer->pPatchFiles;
        numPatchFiles = mixer->numPatchFiles;
        file = (XFILE)token;

        for (i = 0; i < numPatchFiles; i++)
        {
            if (file == pPatchFiles[i])
            {
                ok = TRUE; // found it!

                // move higher layers down one, and move the token layer to the end.
                for (j = i + 1; j < numPatchFiles; j++)
                {
                    pPatchFiles[j - 1] = pPatchFiles[j];
                }
                pPatchFiles[numPatchFiles - 1] = file;
                XFileUseThisResourceFile(file);
                break;
            }
        }

        if (!ok)
        {
            err = RESOURCE_NOT_FOUND;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_SendBankToBack()
// ------------------------------------
//
//
BAEResult BAEMixer_SendBankToBack(BAEMixer mixer, BAEBankToken token)
{
    int i, j;
    XFILE *pPatchFiles;
    short numPatchFiles;
    XFILE file;
    BAE_BOOL ok;
    OPErr err;

    err = NO_ERR;
    ok = FALSE;

    if (mixer)
    {
        pPatchFiles = mixer->pPatchFiles;
        numPatchFiles = mixer->numPatchFiles;
        file = (XFILE)token;

        // find the patch file in the array, and reorder.
        for (i = 0; i < numPatchFiles; i++)
        {
            if (file == pPatchFiles[i])
            {
                ok = TRUE; // found it!

                // move lower layers up one, and move the token layer to the start.
                for (j = i; j > 0; j--)
                {
                    pPatchFiles[j] = pPatchFiles[j - 1];
                }
                pPatchFiles[0] = file;
                break;
            }
        }

        if (ok)
        {
            PV_BAEMixer_SubmitBankOrder(mixer);
        }
        else
        {
            err = RESOURCE_NOT_FOUND;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// PV_BAEMixer_SubmitBankOrder()
// ------------------------------------
//
//
static void PV_BAEMixer_SubmitBankOrder(BAEMixer mixer)
{
    int i;

    for (i = 0; i < mixer->numPatchFiles; i++)
    {
        XFileUseThisResourceFile(mixer->pPatchFiles[i]);
    }
}

// BAEMixer_GetBankVersion()
// ------------------------------------
// was BAEMixer_GetVersionFromAudioFile()
//
BAEResult BAEMixer_GetBankVersion(BAEMixer mixer, BAEBankToken token, int16_t *pVersionMajor, int16_t *pVersionMinor, int16_t *pVersionSubMinor)
{
    OPErr err;
    XVersion vers;
    int i;
    XFILE file;
    BAE_BOOL foundBank;

    err = NO_ERR;

    if (mixer)
    {
        file = (XFILE)token;
        foundBank = FALSE;

        for (i = 0; i < mixer->numPatchFiles; i++)
        {
            if (mixer->pPatchFiles[i] == file)
            {
                foundBank = TRUE;
                XFileUseThisResourceFile(file);

                if (pVersionMajor && pVersionMinor && pVersionSubMinor)
                {
                    *pVersionMajor = 0;
                    *pVersionMinor = 0;
                    *pVersionSubMinor = 0;
                    if (mixer->pMixer)
                    {
                        XGetVersionNumber(&vers);
                        *pVersionMajor = vers.versionMajor;
                        *pVersionMinor = vers.versionMinor;
                        *pVersionSubMinor = vers.versionSubMinor;
                    }
                    else
                    {
                        err = NOT_SETUP;
                    }
                }
                else
                {
                    err = PARAM_ERR;
                }
                PV_BAEMixer_SubmitBankOrder(mixer); // restore the bank order;
                break;
            }
        }

        if (!foundBank)
        {
            err = RESOURCE_NOT_FOUND;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetGroovoidNameFromBank()
// -------------------------------------------
// was GetSongNameFromAudioFile()
//
BAEResult BAEMixer_GetGroovoidNameFromBank(BAEMixer mixer, int32_t index, char *cSongName)
{
    XPTR pData;
    OPErr err;
    XLongResourceID id;

    err = NO_ERR;
    if (mixer)
    {
        if (cSongName)
        {
            if (mixer->pMixer)
            {
                cSongName[0] = 0;
                pData = NULL;
                pData = XGetIndexedResource(ID_SONG, &id, index, cSongName, NULL);
                if (pData)
                {
                    XPtoCstr(cSongName);
                    XDisposePtr(pData);
                }
            }
            else
            {
                err = NOT_SETUP;
            }
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_ChangeAudioModes()
// ------------------------------------
//
//
BAEResult BAEMixer_ChangeAudioModes(BAEMixer mixer, BAERate q, BAETerpMode t, BAEAudioModifiers am)
{
    OPErr err;
    Rate theRate = Q_RATE_8K;
    TerpMode theTerp = 0;
    AudioModifiers theMods = 0;

    err = NO_ERR;
    if (mixer)
    {
        theRate = (Rate)q;
        if (theRate == (Rate)BAE_RATE_INVALID)
        {
            BAE_STDERR("BAEMixer_ChangeAudioModes:invalid rate %d\n", (int32_t)q);
            err = PARAM_ERR;
        }

        switch (t)
        {
        case BAE_DROP_SAMPLE:
        case BAE_2_POINT_INTERPOLATION:
        case BAE_LINEAR_INTERPOLATION:
            theTerp = PV_GetDefaultTerp(t);
            break;
        default:
            err = PARAM_ERR;
            break;
        }

        theMods = M_NONE;
        if ((am & BAE_USE_16) && XIs16BitSupported())
        {
            theMods |= M_USE_16;
        }
        else
        {
            am &= ~BAE_USE_16; // 8 bit
        }
        if ((am & BAE_USE_STEREO) && XIsStereoSupported())
        {
            theMods |= M_USE_STEREO;
            if (am & BAE_STEREO_FILTER)
            {
                theMods |= M_STEREO_FILTER;
            }
        }
        else
        {
            am &= ~BAE_USE_STEREO; // mono
        }
        if (am & BAE_DISABLE_REVERB)
        {
            theMods |= M_DISABLE_REVERB;
        }
        if (err == NO_ERR)
        {
            err = GM_ChangeAudioModes(NULL, theRate, theTerp, theMods);
            if (err)
                BAE_STDERR("audio:failed change %d\n", (int32_t)err);
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_ChangeSystemVoices()
// ------------------------------------
//
//
BAEResult BAEMixer_ChangeSystemVoices(BAEMixer mixer, int16_t maxSongVoices, int16_t maxSoundVoices, int16_t mixLevel)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        err = GM_ChangeSystemVoices(maxSongVoices, mixLevel, maxSoundVoices);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetTick()
// ------------------------------------
//
//
BAEResult BAEMixer_GetTick(BAEMixer mixer, uint32_t *outTick)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        *outTick = GM_GetSyncTimeStamp();
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_SetAudioLatency()
// ------------------------------------
//
//
BAEResult BAEMixer_SetAudioLatency(BAEMixer mixer, uint32_t requestedLatency)
{
    BAEResult error;

    error = BAE_NO_ERROR;
    if (mixer)
    {
#if (X_PLATFORM == X_WIN95) && BAE_COMPLETE
        {
            BAEWinOSParameters parms;
            int32_t device;

            error = BAEMixer_GetCurrentDevice(mixer, (void *)&parms, &device);
            if (error == BAE_NO_ERROR) // get current
            {
                parms.synthFramesPerBlock = (requestedLatency / BAE_GetSliceTimeInMicroseconds()) + 1;
                error = BAEMixer_SetCurrentDevice(mixer, device, (void *)&parms); // set modified
            }
        }
#else
        {
            error = BAE_NOT_SETUP;
        }
#endif
    }
    else
    {
        error = BAE_NULL_OBJECT;
    }
    return error;
}

// BAEMixer_GetAudioLatency()
// ------------------------------------
//
//
BAEResult BAEMixer_GetAudioLatency(BAEMixer mixer, uint32_t *outLatency)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (outLatency)
        {
            *outLatency = GM_GetSyncTimeStampQuantizedAhead() - GM_GetSyncTimeStamp();
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

BAEResult BAEMixer_SetRouteBus(BAEMixer mixer, int routeBus)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        mixer->pMixer->routeBus = routeBus;
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_SetMasterVolume()
// ------------------------------------
//
//
BAEResult BAEMixer_SetMasterVolume(BAEMixer mixer, BAE_UNSIGNED_FIXED theVolume)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        GM_SetMasterVolume((INT32)(UNSIGNED_FIXED_TO_LONG_ROUNDED(theVolume * MAX_MASTER_VOLUME)));
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetMasterVolume()
// ------------------------------------
//
//
BAEResult BAEMixer_GetMasterVolume(BAEMixer mixer, BAE_UNSIGNED_FIXED *outVolume)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (outVolume)
        {
            *outVolume = UNSIGNED_RATIO_TO_FIXED(GM_GetMasterVolume(), MAX_MASTER_VOLUME);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_SetHardwareVolume()
// ------------------------------------
//
//
BAEResult BAEMixer_SetHardwareVolume(BAEMixer mixer, BAE_UNSIGNED_FIXED theVolume)
{
    OPErr err;
    short newVol = FIXED_TO_SHORT_ROUNDED(theVolume * X_FULL_VOLUME);

    err = NO_ERR;
    if (mixer)
    {
        if (mixer->mMuteCount != 0) // are we muted?
        {
            mixer->mMutedVolumeLevel = newVol;
            BAE_STDERR("audio:SetHardwareVolume, muted %d\n", newVol);
        }
        else
        {
            BAE_SetHardwareVolume(newVol);
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetHardwareVolume()
// ------------------------------------
//
//
BAEResult BAEMixer_GetHardwareVolume(BAEMixer mixer, BAE_UNSIGNED_FIXED *outVolume)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (outVolume)
        {
            if (mixer->mMuteCount != 0) // are we muted?
            {
                *outVolume = UNSIGNED_RATIO_TO_FIXED(mixer->mMutedVolumeLevel, X_FULL_VOLUME);
            }
            else
            {
                *outVolume = UNSIGNED_RATIO_TO_FIXED(BAE_GetHardwareVolume(), X_FULL_VOLUME);
            }
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_SetMasterSoundEffectsVolume()
// -----------------------------------------
//
//
BAEResult BAEMixer_SetMasterSoundEffectsVolume(BAEMixer mixer, BAE_UNSIGNED_FIXED theVolume)
{
    OPErr err;
    short newVolume;

    err = NO_ERR;
    if (mixer)
    {
        newVolume = FIXED_TO_SHORT_ROUNDED(theVolume * MAX_MASTER_VOLUME);
        if ((newVolume < 0) || (newVolume > MAX_MASTER_VOLUME * 5))
        {
            err = PARAM_ERR;
        }
        else
        {
            GM_SetEffectsVolume(newVolume);
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetMasterSoundEffectsVolume()
// -----------------------------------------
//
//
BAEResult BAEMixer_GetMasterSoundEffectsVolume(BAEMixer mixer, BAE_UNSIGNED_FIXED *outVolume)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (outVolume)
        {
            *outVolume = UNSIGNED_RATIO_TO_FIXED(GM_GetEffectsVolume(), MAX_MASTER_VOLUME);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetAudioSampleFrame()
// ------------------------------------
//
//
BAEResult BAEMixer_GetAudioSampleFrame(BAEMixer mixer, int16_t *pLeft, int16_t *pRight, int16_t *outFrame)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (outFrame)
        {
            *outFrame = GM_GetAudioSampleFrame(pLeft, pRight);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetRealtimeStatus()
// ------------------------------------
//
//
BAEResult BAEMixer_GetRealtimeStatus(BAEMixer mixer, BAEAudioInfo *pStatus)
{
    GM_AudioInfo status;
    int16_t count;
    BAEVoiceType voiceType;
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (pStatus)
        {
            GM_GetRealtimeAudioInformation(&status);
            XSetMemory(pStatus, (int32_t)sizeof(BAEAudioInfo), 0);
            pStatus->voicesActive = status.voicesActive;
            for (count = 0; count < status.voicesActive; count++)
            {
                pStatus->voice[count] = status.voice[count];

                voiceType = BAE_UNKNOWN;
                switch (status.voiceType[count])
                {
                case MIDI_PCM_VOICE:
                    voiceType = BAE_MIDI_PCM_VOICE;
                    break;
                case SOUND_PCM_VOICE:
                    voiceType = BAE_SOUND_PCM_VOICE;
                    break;
                }
                pStatus->voiceType[count] = voiceType;
                pStatus->instrument[count] = status.patch[count];
                pStatus->scaledVolume[count] = status.scaledVolume[count];
                pStatus->midiVolume[count] = status.volume[count];
                pStatus->channel[count] = status.channel[count];
                pStatus->midiNote[count] = status.midiNote[count];
                if (status.pSong[count])
                {
                    pStatus->userReference[count] = status.pSong[count]->userReference;
                }
            }
#if USE_SF2_SUPPORT == TRUE            
            if (mixer->pMixer->isSF2)
            {
                pStatus->voicesActive += GM_SF2_GetActiveVoiceCount();
            }
#endif            
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_IsAudioEngaged()
// ------------------------------------
//
//
BAEResult BAEMixer_IsAudioEngaged(BAEMixer mixer, BAE_BOOL *outIsEngaged)
{
    OPErr err;
    XBOOL isPaused;

    err = NO_ERR;
    if (mixer)
    {
        if (outIsEngaged)
        {
            if (mixer->pMixer)
            {
                err = GM_IsGeneralSoundPaused(&isPaused);
                *outIsEngaged = !isPaused;
            }
            else
            {
                err = NOT_SETUP;
            }
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_DisengageAudio()
// ------------------------------------
//
//
BAEResult BAEMixer_DisengageAudio(BAEMixer mixer)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (mixer->pMixer)
        {
            err = GM_PauseGeneralSound(NULL);
            if (err == NO_ERR)
            {
                mixer->audioEngaged = FALSE;
            }
        }
        else
        {
            err = NOT_SETUP;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_ReengageAudio()
// ------------------------------------
//
//
BAEResult BAEMixer_ReengageAudio(BAEMixer mixer)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (mixer->pMixer)
        {
            err = GM_ResumeGeneralSound(NULL);
            if (err == NO_ERR)
            {
                mixer->audioEngaged = TRUE;
            }
        }
        else
        {
            err = NOT_SETUP;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

BAE_BOOL BAEMixer_IsMuted(BAEMixer mixer)
{
    BAE_BOOL muted = FALSE;

    if (mixer)
    {
        if (mixer->mMuteCount)
        {
            muted = TRUE;
        }
    }
    return muted;
}

// mute/unmute all audio playback. These are reference counted
BAEResult BAEMixer_Mute(BAEMixer mixer)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (mixer->mMuteCount == 0)
        {
            mixer->mMutedVolumeLevel = BAE_GetHardwareVolume();
            BAE_SetHardwareVolume(0);
            BAE_Mute();
        }
        mixer->mMuteCount++;
    }
    else
    {
        err = NULL_OBJECT;
    }
    return (BAEResult)err;
}

BAEResult BAEMixer_Unmute(BAEMixer mixer)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        mixer->mMuteCount--;
        if (mixer->mMuteCount == 0)
        {
            BAE_SetHardwareVolume((short)mixer->mMutedVolumeLevel);
            BAE_Unmute();
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return (BAEResult)err;
}

BAEResult BAEMixer_TestToneFrequency(BAE_UNSIGNED_FIXED freq)
{
    GM_TestToneFrequency(freq);
    return BAE_NO_ERROR;
}

BAEResult BAEMixer_TestTone(BAE_BOOL status)
{
    GM_TestTone((XBOOL)status);
    return BAE_NO_ERROR;
}

// BAEMixer_Idle()
// ------------------------------------
// Called during idle times to process audio, or other events. Optional
// requiment if threads are available.
//
BAEResult BAEMixer_Idle(BAEMixer mixer)
{
    BAE_Idle((void *)mixer);
    return BAE_NO_ERROR;
}

// BAEMixer_IsAudioActive()
// ------------------------------------
// This will check active voices and look at a sub sample of the audio output to
// determine if there's any audio still playing
BAEResult BAEMixer_IsAudioActive(BAEMixer mixer, BAE_BOOL *outIsActive)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (outIsActive)
        {
            BAEMixer_IsAudioEngaged(mixer, outIsActive);
            if (*outIsActive == TRUE)
            {
                *outIsActive = (BAE_BOOL)GM_IsAudioActive();
            }
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetCPULoadInMicroseconds()
// --------------------------------------
//
//
BAEResult BAEMixer_GetCPULoadInMicroseconds(BAEMixer mixer, uint32_t *outLoad)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (outLoad)
        {
            *outLoad = GM_GetMixerUsedTime();
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetCPULoadInPercent()
// --------------------------------------
//
//
BAEResult BAEMixer_GetCPULoadInPercent(BAEMixer mixer, uint32_t *outLoad)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (outLoad)
        {
            *outLoad = GM_GetMixerUsedTimeInPercent();
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetModifiers()
// --------------------------------------
//
//
BAEResult BAEMixer_GetModifiers(BAEMixer mixer, BAEAudioModifiers *outMods)
{
    BAEAudioModifiers theMods;
    OPErr err;
    XBOOL generate16output;
    XBOOL generateStereoOutput;

    err = NO_ERR;
    if (mixer)
    {
        if (outMods)
        {
            if (mixer->pMixer)
            {
                theMods = 0;
                err = GM_Generate16bitOutP(&generate16output);
                err = GM_GenerateStereoOutP(&generateStereoOutput);
                if (generate16output)
                    theMods |= BAE_USE_16;
                if (generateStereoOutput)
                    theMods |= BAE_USE_STEREO;
                *outMods = theMods;
            }
            else
            {
                err = NOT_SETUP;
            }
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetTerpMode()
// --------------------------------------
//
//
BAEResult BAEMixer_GetTerpMode(BAEMixer mixer, BAETerpMode *outTerpMode)
{
    OPErr err;
    TerpMode t;

    err = NO_ERR;
    if (mixer)
    {
        if (outTerpMode)
        {
            if (mixer->pMixer)
            {
                err = GM_GetInterpolationMode(&t);
                if (err == NO_ERR)
                {
                    *outTerpMode = PV_TranslateTerpModeToBAETerpMode(t);
                }
            }
            else
            {
                err = NOT_SETUP;
            }
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetRate()
// --------------------------------------
//
//
BAEResult BAEMixer_GetRate(BAEMixer mixer, BAERate *outRate)
{
    OPErr err;
    Rate q;

    err = NO_ERR;
    if (mixer)
    {
        if (outRate)
        {
            if (mixer->pMixer)
            {
                err = GM_GetRate(&q);
                if (!err)
                {
                    *outRate = (BAERate)q;
                }
            }
            else
            {
                err = NOT_SETUP;
            }
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetMidiVoices()
// --------------------------------------
//
//
BAEResult BAEMixer_GetMidiVoices(BAEMixer mixer, int16_t *outNumMidiVoices)
{
    OPErr err;
    INT16 song, mix, sound;

    err = NO_ERR;
    if (mixer)
    {
        if (outNumMidiVoices)
        {
            if (mixer->pMixer)
            {
                GM_GetSystemVoices(&song, &mix, &sound);
                *outNumMidiVoices = song;
            }
            else
            {
                err = NOT_SETUP;
            }
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetSoundVoices()
// --------------------------------------
//
//
BAEResult BAEMixer_GetSoundVoices(BAEMixer mixer, int16_t *outNumSoundVoices)
{
    OPErr err;
    INT16 song, mix, sound;

    err = NO_ERR;
    if (mixer)
    {
        if (outNumSoundVoices)
        {
            if (mixer->pMixer)
            {
                GM_GetSystemVoices(&song, &mix, &sound);
                *outNumSoundVoices = sound;
            }
            else
            {
                err = NOT_SETUP;
            }
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetMixLevel()
// --------------------------------------
//
//
BAEResult BAEMixer_GetMixLevel(BAEMixer mixer, int16_t *outMixLevel)
{
    OPErr err;
    INT16 song, mix, sound;

    err = NO_ERR;
    if (mixer)
    {
        if (outMixLevel)
        {
            if (mixer->pMixer)
            {
                GM_GetSystemVoices(&song, &mix, &sound);
                *outMixLevel = mix;
            }
            else
            {
                err = NOT_SETUP;
            }
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// ********************** BAEMixer_StartOutputToFile ************************
// ********************** added from BAE 11/28/00 tom ***********************
// ********************** parameter 1 added for MiniBAE *********************
// ********************** method name changed for MiniBAE conformance *******
// **** 'iModifiers' and 'iRate' replaced by calls to accessor methods ***

// start saving audio output to a file
BAEResult BAEMixer_StartOutputToFile(BAEMixer theMixer,
                                     BAEPathName pAudioOutputFile,
                                     BAEFileType outputType,
                                     BAECompressionType compressionType)
{
#if USE_CREATION_API == TRUE
    OPErr theErr;
    XFILENAME theFile;
    BAEResult err;
    // begin block added for MiniBAE 11/29/00 tom
    BAEAudioModifiers theModifiers;
    BAERate theRate;
    // end block added for MiniBAE

#if DUMP_OUTPUTFILE
    fp = fopen("C:\\temp\\test.txt", "w");
    if (fp)
    {
        fprintf(fp, "BAEOutputMixer::StartOutputToFile dump\n");
    }
#endif

    // begin block added for MiniBAE  11/28/00  tom
    err = BAEMixer_GetModifiers(theMixer, &theModifiers);
    if (err != BAE_NO_ERROR)
    {
        return err;
    }
    err = BAEMixer_GetRate(theMixer, &theRate);
    if (err != BAE_NO_ERROR)
    {
        return err;
    }
    // end block added for MiniBAE

    theErr = (OPErr)BAE_NO_ERROR;

    // close old one first
    if (mWritingToFile)
    {
        // StopOutputToFile();
        BAEMixer_StopOutputToFile();
    }

    mWriteToFileType = outputType;
    XConvertPathToXFILENAME(pAudioOutputFile, &theFile);

    // mWritingDataBlock is where we will store the results of BAE_BuildMixerSlice()
    if (mWritingDataBlock)
    {
        XDisposePtr(mWritingDataBlock);
    }
    mWritingDataBlockSize = GM_GetAudioBufferOutputSize();

    mWritingDataBlock = XNewPtr(mWritingDataBlockSize);

#if DUMP_OUTPUTFILE
    if (fp)
    {
        fprintf(fp, "\nmWritingDataBlockSize = %d", mWritingDataBlockSize);

        fprintf(fp, "\noutput rate = %d", GM_ConvertFromOutputRateToRate(theRate));

        fprintf(fp, "\nmaxChunkSize = %d", MusicGlobals->maxChunkSize);
        fprintf(fp, "\noutputRate = %d", MusicGlobals->outputRate);
    }
#endif

    switch (outputType)
    {
#if USE_MPEG_ENCODER != FALSE
    case BAE_MPEG_TYPE:
    {
        if (theModifiers & BAE_USE_16)
        {
            mWritingToFileReference = (void *)XFileOpenForWrite(&theFile, TRUE);
            if (mWritingToFileReference)
            {
                XDWORD channels = (theModifiers & BAE_USE_STEREO) ? 2 : 1;
                // Preserve original slice size (typically ~11ms worth) to maintain correct sequencing tempo.
                // The MP3 encoder will internally accumulate multiple slices to reach a full 1152-frame MP3 frame.
                // helper translates compression enum to per-channel bitrate in bits/sec
                extern uint32_t BAE_TranslateMPEGTypeToBitrate(BAECompressionType ct);
                extern XBOOL PV_RefillMPEGEncodeBuffer(void *buffer, void *userRef);
                mWritingEncoder = MPG_EncodeNewStream(BAE_TranslateMPEGTypeToBitrate(compressionType),
                                                      GM_ConvertFromOutputRateToRate((Rate)theRate),
                                                      channels,
                                                      mWritingDataBlock,
                                                      (uint32_t)(mWritingDataBlockSize / (sizeof(short) * channels)));
                if (mWritingEncoder)
                {
                    BAE_PRINTF("audio: MPG_EncodeNewStream ok ch=");
                    char tmp[16];
                    XLongToStr(tmp, (int32_t)channels);
                    BAE_PRINTF(tmp);
                    BAE_PRINTF(" framesPerCall=");
                    XLongToStr(tmp, (int32_t)(mWritingDataBlockSize / (sizeof(short) * channels)));
                    BAE_PRINTF(tmp);
                    BAE_PRINTF(" rate=");
                    XLongToStr(tmp, (int32_t)GM_ConvertFromOutputRateToRate((Rate)theRate));
                    BAE_PRINTF(tmp);
                    BAE_PRINTF("\n");
                    // Prime first PCM buffer so first service call has audio content
                    PV_RefillMPEGEncodeBuffer(mWritingDataBlock, theMixer);

                    /* Pass mixer as userRef so refill can query modifiers/rate properly */
                    MPG_EncodeSetRefillCallback(mWritingEncoder, PV_RefillMPEGEncodeBuffer, theMixer);

                    GM_StopHardwareSoundManager(NULL); // disengage from hardware
                    mWritingToFile = TRUE;
                }
                else
                {
                    BAE_STDERR("audio: MPG_EncodeNewStream FAILED\n");
                    /* Treat failure to create encoder as an error so caller can abort gracefully. */
                    theErr = BAD_FILE;
                    /* Close file handle we opened to avoid leaving an open/half-written file. */
                    if (mWritingToFileReference)
                    {
                        XFileClose((XFILE)mWritingToFileReference);
                        mWritingToFileReference = NULL;
                    }
                    /* do not set mWritingToFile; cleanup of mWritingDataBlock happens below when theErr != NO_ERR */
                }
            }
            else
            {
                theErr = BAD_FILE;
            }
        }
        else
        {
            // Can only encode 16bit data.
            theErr = PARAM_ERR;
        }
    }
    break;
#endif

#if USE_VORBIS_ENCODER == TRUE
    case BAE_VORBIS_TYPE:
    {
        if (theModifiers & BAE_USE_16)
        {
            mWritingToFileReference = (void *)XFileOpenForWrite(&theFile, TRUE);
            if (mWritingToFileReference)
            {
                XDWORD channels = (theModifiers & BAE_USE_STEREO) ? 2 : 1;

                /* Initialize Vorbis encoder and write headers. */
                extern void *XInitVorbisEncoder(UINT32 sample_rate, UINT32 channels, float quality);
                extern long XWriteVorbisHeader(void *encoder_handle, XFILE output_file);

                float quality = BAE_TranslateVorbisTypeToQuality(compressionType);
                mWritingEncoder = XInitVorbisEncoder(GM_ConvertFromOutputRateToRate((Rate)theRate), channels, quality);
                if (mWritingEncoder)
                {
                    /* write header pages to file */
                    (void)XWriteVorbisHeader(mWritingEncoder, (XFILE)mWritingToFileReference);

                    GM_StopHardwareSoundManager(NULL);
                    mWritingToFile = TRUE;
                }
                else
                {
                    BAE_STDERR("audio: XInitVorbisEncoder FAILED\n");
                    theErr = BAD_FILE;
                    if (mWritingToFileReference)
                    {
                        XFileClose((XFILE)mWritingToFileReference);
                        mWritingToFileReference = NULL;
                    }
                }
            }
            else
            {
                theErr = BAD_FILE;
            }
        }
        else
        {
            /* Can only encode 16bit data. */
            theErr = PARAM_ERR;
        }
    }
    break;
#endif

#if USE_FLAC_ENCODER == TRUE
    case BAE_FLAC_TYPE:
#endif    
    case BAE_WAVE_TYPE:
    case BAE_AIFF_TYPE:
    case BAE_AU_TYPE:
    {
#if USE_FLAC_ENCODER == TRUE
        if (outputType == BAE_FLAC_TYPE)
        {
            // For FLAC, we'll accumulate audio in memory and then encode
            mFLACChannels = (theModifiers & BAE_USE_STEREO) ? 2 : 1;
            mFLACBitsPerSample = (theModifiers & BAE_USE_16) ? 16 : 8;
            mFLACSampleRate = GM_ConvertFromOutputRateToRate((Rate)theRate);
            mFLACAccumulatedFrames = 0;
            // Allocate buffer for about 10 minutes worth of audio (should handle most songs)
            mFLACMaxAccumulatedFrames = mFLACSampleRate * 600; // 10 minutes
            mFLACAccumulatedSamples = XNewPtr(mFLACMaxAccumulatedFrames * mFLACChannels * (mFLACBitsPerSample / 8));
            mFLACEncoder = NULL;       // Will be created when we finish accumulating
            mFLACOutputFile = theFile; // Store file path for later

            // Check if allocation succeeded
            if (!mFLACAccumulatedSamples)
            {
                theErr = MEMORY_ERR;
            }
            else
            {
                // Just open the file for later writing
                mWritingToFileReference = (void *)XFileOpenForWrite(&theFile, TRUE);
                if (mWritingToFileReference)
                {
                    GM_StopHardwareSoundManager(NULL);
                    mWritingToFile = TRUE;
                }
                else
                {
                    theErr = BAD_FILE;
                }
            }
        }
        else
        {
#endif
            GM_Waveform *w = GM_NewWaveform();
            char buf[4] = {0, 0, 0, 0};

            // initialize GM_Waveform with one frame of data, so that GM_WriteFileFromMemory()
            // doesn't complain.

            w->bitSize = (theModifiers /*iModifiers*/ & BAE_USE_16) ? 16 : 8;
            w->channels = (theModifiers /*iModifiers*/ & BAE_USE_STEREO) ? 2 : 1;
            w->sampledRate = LONG_TO_UNSIGNED_FIXED(GM_ConvertFromOutputRateToRate((Rate)theRate /*iRate*/));
            w->compressionType = C_NONE;
            w->theWaveform = &buf;
            w->waveFrames = 1;
            w->waveSize = (w->bitSize / 8) * (w->channels);

            // Write out the header now, we'll add data to it in ServiceAudioOutputToFile()
            theErr = GM_WriteFileFromMemory(&theFile, w, BAE_TranslateBAEFileType(outputType));

            GM_FreeWaveform(w);
            w = NULL;

            // Reopen the file and jump to the end, so we can add data to it later...
            mWritingToFileReference = (void *)XFileOpenForWrite(&theFile, FALSE);
            if (mWritingToFileReference)
            {
                XFileSetPosition((XFILE)mWritingToFileReference, XFileGetLength((XFILE)mWritingToFileReference));

                GM_StopHardwareSoundManager(NULL); // disengage from hardware
                mWritingToFile = TRUE;
            }
            else
            {
                theErr = BAD_FILE;
            }
#if USE_FLAC_ENCODER == TRUE
        }
#endif
    }
    break;

    case BAE_RAW_PCM:
        mWritingToFileReference = (void *)XFileOpenForWrite(&theFile, TRUE);
        if (mWritingToFileReference)
        {
            GM_StopHardwareSoundManager(NULL); // disengage from hardware
            mWritingToFile = TRUE;
        }
        else
        {
            theErr = BAD_FILE;
        }
        break;

    default:
        theErr = BAD_FILE_TYPE;
        break;
    }

    if (theErr != NO_ERR)
    {
        XDisposePtr(mWritingDataBlock);
        mWritingDataBlock = NULL;
    }
    return BAE_TranslateOPErr(theErr);
#else
    pAudioOutputFile = pAudioOutputFile;
    theMixer = theMixer;
    compressionType = compressionType;
    outputType = outputType;
    return BAE_NOT_SETUP;
#endif
}

// *********************** BAEMixer_StopOutputToFile ***********************
// ********************** added from BAE 11/29/00 tom **********************
// ********************** method name changed for MiniBAE conformance ******

// Stop saving audio output to a file
void BAEMixer_StopOutputToFile(void)
{
#if USE_CREATION_API == TRUE
    if (mWritingToFile && mWritingToFileReference)
    {
        switch (mWriteToFileType)
        {
#if USE_MPEG_ENCODER == TRUE
        case BAE_MPEG_TYPE:
            BAE_PRINTF("audio: BAEMixer_StopOutputToFile freeing mWritingEncoder=%p\n", mWritingEncoder);
            MPG_EncodeFreeStream(mWritingEncoder);
            mWritingEncoder = NULL;
            BAE_PRINTF("audio: BAEMixer_StopOutputToFile mWritingEncoder now NULL\n");
            break;
#endif
#if USE_VORBIS_ENCODER == TRUE
        case BAE_VORBIS_TYPE:
            BAE_PRINTF("audio: BAEMixer_StopOutputToFile freeing vorbis encoder=%p\n", mWritingEncoder);
            if (mWritingEncoder)
            {
                extern void XCloseVorbisEncoder(void *encoder_handle);
                XCloseVorbisEncoder(mWritingEncoder);
                mWritingEncoder = NULL;
            }
            break;
#else
            break;
#endif
        case BAE_WAVE_TYPE:
        case BAE_AIFF_TYPE:
        case BAE_AU_TYPE:
            GM_FinalizeFileHeader((XFILE)mWritingToFileReference, BAE_TranslateBAEFileType(mWriteToFileType));
            break;

#if USE_FLAC_ENCODER == TRUE
        case BAE_FLAC_TYPE:
            // Encode accumulated audio data to FLAC and write to file
            if (mFLACAccumulatedSamples && mFLACAccumulatedFrames > 0)
            {
                // Use the existing PV_WriteFromMemoryFLACFile function
                GM_Waveform tempWave;
                tempWave.theWaveform = mFLACAccumulatedSamples;
                tempWave.waveFrames = mFLACAccumulatedFrames;
                tempWave.channels = mFLACChannels;
                tempWave.bitSize = mFLACBitsPerSample;
                tempWave.sampledRate = LONG_TO_UNSIGNED_FIXED(mFLACSampleRate);
                tempWave.compressionType = C_NONE;
                tempWave.waveSize = mFLACAccumulatedFrames * mFLACChannels * (mFLACBitsPerSample / 8);

                // Close current file and rewrite with FLAC encoded data
                XFileClose((XFILE)mWritingToFileReference);
                mWritingToFileReference = NULL;

                // Write FLAC data using stored file path
                PV_WriteFromMemoryFLACFile(&mFLACOutputFile, &tempWave, X_WAVE_FORMAT_PCM);
            }

            // Cleanup FLAC state
            if (mFLACEncoder)
            {
                FLAC__stream_encoder_finish((FLAC__StreamEncoder *)mFLACEncoder);
                FLAC__stream_encoder_delete((FLAC__StreamEncoder *)mFLACEncoder);
                mFLACEncoder = NULL;
            }
            if (mFLACAccumulatedSamples)
            {
                XDisposePtr(mFLACAccumulatedSamples);
                mFLACAccumulatedSamples = NULL;
            }
            mFLACAccumulatedFrames = 0;
            break;
#endif

        default:
            break;
        }
        XFileClose((XFILE)mWritingToFileReference);
        mWritingToFileReference = NULL;

        XDisposePtr(mWritingDataBlock);
        mWritingDataBlock = NULL;

        GM_StartHardwareSoundManager(NULL); // reconnect to hardware
    }
    mWritingToFile = FALSE;
#if DUMP_OUTPUTFILE
    if (fp)
    {
        fclose(fp);
    }
#endif
#endif // #if USE_CREATION_API == TRUE
}

#ifdef __EMSCRIPTEN__
BAEResult BAEMixer_ServiceAudioOutputToWebAudio(BAEMixer theMixer) {
    int32_t sampleSize, channels;
    OPErr theErr;

    // begin block added for MiniBAE
    BAEAudioModifiers theModifiers;
    BAEMixer_GetModifiers(theMixer, &theModifiers);
    // end block added for MiniBAE

    theErr = NO_ERR;

    channels = (theModifiers & BAE_USE_STEREO) ? 2 : 1;
    sampleSize = (theModifiers /*iModifiers*/ & BAE_USE_16) ? 2 : 1;
    uint32_t numSamples = (uint32_t)(mWritingDataBlockSize / sampleSize / channels);

    BAE_BuildMixerSlice(NULL, mWritingDataBlock, mWritingDataBlockSize, numSamples);
    process_and_send_audio(mWritingDataBlock, numSamples);
    return BAE_TranslateOPErr(theErr);
}
#endif

// ********************** BAEMixer_ServiceAudioOutputToFile   ************
//
// ********************** added from BAE 11/29/00 tom ********************
// ********************** method name changed for MiniBAE conformance ****
// ********************** parameter 1 added for MiniBAE ******************
// ***** 'iModifiers'  replaced by call to accessor method  **************

BAEResult BAEMixer_ServiceAudioOutputToFile(BAEMixer theMixer)
{
#if USE_CREATION_API == TRUE
    int32_t sampleSize, channels;
    OPErr theErr;

#ifndef HMP3_ENC_LOG
#define HMP3_ENC_LOG 0
#endif

    // begin block added for MiniBAE
    BAEAudioModifiers theModifiers;
    BAEMixer_GetModifiers(theMixer, &theModifiers);
    // end block added for MiniBAE

    theErr = NO_ERR;

    if (mWritingToFile && mWritingToFileReference)
    {
        // FIX: previous code used bitwise NOT (~BAE_USE_STEREO) causing invalid channel count
        channels = (theModifiers & BAE_USE_STEREO) ? 2 : 1;
        sampleSize = (theModifiers /*iModifiers*/ & BAE_USE_16) ? 2 : 1;
        if (mWritingDataBlockSize)
        {
            if (mWritingDataBlockSize && mWritingDataBlock)
            {
#if DUMP_OUTPUTFILE
#if DUMP_C_PLUS_PLUS
                *file << "\nwite block size = ";
                *file << mWritingDataBlockSize;
                *file << ", ";
                *file << mWritingDataBlockSize / sampleSize / channels;
                *file << "\nsampleSize = " << sampleSize << "channels = " << channels;
#else
                if (fp)
                {
                    fprintf(fp, "\nwite block size = %d", mWritingDataBlockSize);
                    fprintf(fp, ", %d", mWritingDataBlockSize / sampleSize / channels);
                    fprintf(fp, "\nsampleSize = %d, "
                                "channels = %d",
                            sampleSize, channels);
                }
#endif
#endif
                switch (mWriteToFileType)
                {
#if USE_MPEG_ENCODER != FALSE
                case BAE_MPEG_TYPE:
                {
                    XPTR compressedData = NULL;
                    XDWORD compressedLength = 0;
                    XBOOL isDone = FALSE;
                    if (!mWritingEncoder)
                    {
                        BAE_PRINTF("audio: MPEG encode service called with NULL encoder (encoder not built?) aborting export.\n");
                        // Gracefully abort: close file and reset state
                        mWriteToFileType = 0; // invalid
                        XFileClose((XFILE)mWritingToFileReference);
                        mWritingToFileReference = NULL;
                        mWritingToFile = FALSE;
                        return BAE_GENERAL_ERR;
                    }
                    else
                    {
                        MPG_EncodeProcess(mWritingEncoder, &compressedData, &compressedLength, &isDone);
                        if (compressedLength > 0)
                        {
                            if (XFileWrite((XFILE)mWritingToFileReference, compressedData, compressedLength) == -1)
                            {
                                theErr = BAD_FILE;
                            }
                        }
                        // Do NOT free stream here unless encoder signals done explicitly
                        if (isDone)
                        {
                            BAE_PRINTF("audio: MPG_EncodeProcess signaled done, freeing encoder %p\n", mWritingEncoder);
                            if (mWritingEncoder)
                            {
                                MPG_EncodeFreeStream(mWritingEncoder);
                                mWritingEncoder = NULL;
                                BAE_PRINTF("audio: encoder freed, mWritingEncoder=NULL\n");
                            }
                            else
                            {
                                BAE_PRINTF("audio: encoder already NULL when done signaled\n");
                            }
                        }
                    }
                }
                break;
#endif

                case BAE_RAW_PCM:
                {
                    BAE_BuildMixerSlice(NULL, mWritingDataBlock, mWritingDataBlockSize,
                                        (uint32_t)(mWritingDataBlockSize / sampleSize / channels));
                    if (XFileWrite((XFILE)mWritingToFileReference, mWritingDataBlock, mWritingDataBlockSize) == -1)
                    {
                        theErr = BAD_FILE;
                    }
                }
                break;

                case BAE_WAVE_TYPE:
                case BAE_AIFF_TYPE:
                case BAE_AU_TYPE:
                {
                    BAE_BuildMixerSlice(NULL, mWritingDataBlock, mWritingDataBlockSize,
                                        (uint32_t)(mWritingDataBlockSize / sampleSize / channels));
                    theErr = GM_WriteAudioBufferToFile((XFILE)mWritingToFileReference,
                                                       BAE_TranslateBAEFileType(mWriteToFileType),
                                                       mWritingDataBlock,
                                                       mWritingDataBlockSize,
                                                       channels,
                                                       sampleSize);
                }
                break;

#if USE_FLAC_ENCODER != FALSE
                case BAE_FLAC_TYPE:
                {
                    // Accumulate audio samples for FLAC encoding
                    uint32_t framesToProcess = (uint32_t)(mWritingDataBlockSize / sampleSize / channels);

                    BAE_BuildMixerSlice(NULL, mWritingDataBlock, mWritingDataBlockSize, framesToProcess);

                    // Check if we have room in the accumulation buffer
                    if (mFLACAccumulatedFrames + framesToProcess <= mFLACMaxAccumulatedFrames)
                    {
                        // Copy audio data to accumulation buffer
                        char *destPtr = (char *)mFLACAccumulatedSamples +
                                        (mFLACAccumulatedFrames * mFLACChannels * (mFLACBitsPerSample / 8));
                        memcpy(destPtr, mWritingDataBlock, mWritingDataBlockSize);
                        mFLACAccumulatedFrames += framesToProcess;
                    }
                    else
                    {
                        // Buffer is full - this shouldn't happen with 10 minutes of buffer
                        // But if it does, warn and stop accumulating (export will be truncated)
                        static int warned = 0;
                        if (!warned)
                        {
                            BAE_PRINTF("FLAC accumulation buffer full (>10 minutes), export will be truncated\n");
                            warned = 1;
                        }
                    }
                }
                break;
#endif

#if USE_VORBIS_ENCODER == TRUE
                case BAE_VORBIS_TYPE:
                {
                    // Build PCM slice into mWritingDataBlock
                    uint32_t framesToProcess = (uint32_t)(mWritingDataBlockSize / sampleSize / channels);
                    BAE_BuildMixerSlice(NULL, mWritingDataBlock, mWritingDataBlockSize, framesToProcess);

                    // Convert interleaved 16-bit PCM to planar float arrays expected by encoder
                    extern long XEncodeVorbisData(void *encoder_handle, float **pcm_data, long samples, XFILE output_file);
                    int ch = channels;
                    float *chanBufs[2] = {0};
                    // Allocate channel buffers on stack for typical stereo/mono
                    for (int c = 0; c < ch; c++)
                    {
                        chanBufs[c] = (float *)XNewPtr(sizeof(float) * framesToProcess);
                    }

                    // deinterleave and convert
                    int16_t *pcm = (int16_t *)mWritingDataBlock;
                    for (uint32_t i = 0; i < framesToProcess; i++)
                    {
                        for (int c = 0; c < ch; c++)
                        {
                            int16_t s = *pcm++;
                            chanBufs[c][i] = ((float)s) / 32768.0f;
                        }
                    }

                    // Call encoder; passing NULL output file if no file ref
                    XFILE out = (XFILE)mWritingToFileReference;
                    long written = XEncodeVorbisData(mWritingEncoder, chanBufs, (long)framesToProcess, out);

                    // free channel buffers
                    for (int c = 0; c < ch; c++)
                    {
                        if (chanBufs[c])
                            XDisposePtr((XPTR)chanBufs[c]);
                    }

                    if (written < 0)
                    {
                        theErr = BAD_FILE;
                    }
                }
                break;
#endif

                default:
                {
                    theErr = BAD_FILE_TYPE;
                }
                break;
                }
            }
            else
            {
                theErr = BUFFER_TO_SMALL;
            }
        }
        else
        {
            theErr = BUFFER_TO_SMALL;
        }
    }
    else
    {
        theErr = NOT_SETUP;
    }
    return BAE_TranslateOPErr(theErr);
#else
    theMixer = theMixer;
    return BAE_NOT_SETUP;
#endif
}

// ------------------------------------------------------------------
// BAESound Functions
// ------------------------------------------------------------------
#if 0
#pragma mark -
#pragma mark##### BAESound #####
#pragma mark -
#endif

// BAESound_New()
// --------------------------------------
//
//
BAESound BAESound_New(BAEMixer mixer)
{
    BAESound sound;
    sound = NULL;

    if (mixer)
    {
        sound = (BAESound)XNewPtr(sizeof(struct sBAESound));
        if (sound)
        {
            if (BAE_NewMutex(&sound->mLock, "bae", "snd", __LINE__))
            {
                sound->mixer = mixer;
                sound->mVolume = BAE_FIXED_1;
                sound->mID = OBJECT_ID;
                sound->voiceRef = DEAD_VOICE;
                sound->mLoopCount = 0;   // default: no looping
                sound->mCurrentLoop = 0; // initialize current loop counter
#if TRACKING
                PV_BAEMixer_AddObject(mixer, sound, BAE_SOUND_OBJECT);
#else
                sound->mValid = 1;
#endif
            }
            else
            {
                XDisposePtr(sound);
                sound = NULL;
            }
        }
    }
    return sound;
}

// BAESound_Delete()
// --------------------------------------
//
//
BAEResult BAESound_Delete(BAESound sound)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        sound->mID = 0; // do this, to prevent other methods from waiting on a lock
                        // as this object is torn down

        BAE_AcquireMutex(sound->mLock);

        PV_BAESound_Unload(sound);
        PV_BAESound_SetCallback(sound, NULL, NULL);
#if TRACKING
        PV_BAEMixer_RemoveObject(sound->mixer, sound, BAE_SOUND_OBJECT);
#else
        sound->mValid = 0;
#endif
        BAE_ReleaseMutex(sound->mLock);
        BAE_DestroyMutex(sound->mLock);

        XDisposePtr(sound);
    }
    else
    {
        BAE_PRINTF("audio: BAESound_Delete invalid object\n");
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_GetMemoryUsed()
// --------------------------------------
//
//
BAEResult BAESound_GetMemoryUsed(BAESound sound, uint32_t *pOutResult)
{
    uint32_t size;

    size = 0;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        // song size
        size = XGetPtrSize((XPTR)sound);
        size += sound->pWave->waveSize;
        BAE_ReleaseMutex(sound->mLock);
    }
    if (pOutResult)
    {
        *pOutResult = size;
    }
    return BAE_NO_ERROR;
}

// BAESound_SetMixer()
// --------------------------------------
//
//
BAEResult BAESound_SetMixer(BAESound sound, BAEMixer mixer)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID) && mixer)
    {
        BAE_AcquireMutex(sound->mLock);
        sound->mixer = mixer;
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_GetMixer()
// --------------------------------------
//
//
BAEResult BAESound_GetMixer(BAESound sound, BAEMixer *outMixer)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        if (outMixer)
        {
            BAE_AcquireMutex(sound->mLock);
            *outMixer = sound->mixer;
            BAE_ReleaseMutex(sound->mLock);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

static void PV_BAESound_Unload(BAESound sound)
{
    VOICE_REFERENCE voice;

    voice = sound->voiceRef;
    PV_BAESound_Stop(sound, FALSE);

    while (GM_IsSampleProcessing(voice))
    {
        //      BAE_PRINTF("BAE:deleting sound...\n");
        XWaitMicroseconds(BAE_GetSliceTimeInMicroseconds());
    }

    if (sound->pWave)
    {
        GM_FreeWaveform(sound->pWave);
        sound->pWave = NULL;
    }
    //  BAE_PRINTF("BAE:deleting sound done\n");
}

// BAESound_Unload()
// --------------------------------------
//
//
BAEResult BAESound_Unload(BAESound sound)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        PV_BAESound_Unload(sound);
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

BAEResult BAESound_SetSoundFrame(BAESound sound, uint32_t startFrameOffset,
                                 void *sourceSamples, uint32_t sourceFrames)
{
    BAEResult err = BAE_NO_ERROR;

    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);

        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// BAESound_LoadEmptySample()
// --------------------------------------
//
//
BAEResult BAESound_LoadEmptySample(BAESound sound,
                                   uint32_t frames,         // number of frames of audio
                                   uint16_t bitSize,        // bits per sample 8 or 16
                                   uint16_t channels,       // mono or stereo 1 or 2
                                   BAE_UNSIGNED_FIXED rate, // 16.16 fixed sample rate
                                   uint32_t loopStart,      // loop start in frames
                                   uint32_t loopEnd)        // loop end in frames
{
    OPErr theErr;
    GM_Waveform *pWave = NULL;
    int32_t size;
    void *sampleData;

    theErr = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);

        // if sound already loaded, then free it...
        BAESound_Unload(sound);

        pWave = GM_NewWaveform();
        if (pWave)
        {
            size = frames * (bitSize / 8) * channels;
            sampleData = XNewPtr(size);
            if (sampleData != NULL)
            {
                pWave->waveSize = size;
                pWave->waveFrames = frames;
                pWave->startLoop = loopStart;
                pWave->endLoop = loopEnd;
                pWave->baseMidiPitch = 60;
                pWave->bitSize = (unsigned char)bitSize;
                pWave->channels = (unsigned char)channels;
                pWave->sampledRate = rate;
                pWave->theWaveform = sampleData;

                if (bitSize == 8)
                {
                    // 8 bit passed in is signed, but internal engine 8 bit data is unsigned.
                    XPhase8BitWaveform((XBYTE *)pWave->theWaveform, pWave->waveSize);
                }
                sound->pWave = pWave;
            }
            else
            {
                theErr = MEMORY_ERR;
                XDisposePtr(pWave);
                pWave = NULL;
            }
        }
        else
        {
            theErr = MEMORY_ERR;
        }

        if ((sound->pWave == NULL) && (theErr == NO_ERR))
        {
            theErr = BAD_FILE;
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        theErr = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(theErr);
}

// BAESound_LoadCustomSample()
// --------------------------------------
//
//
BAEResult BAESound_LoadCustomSample(BAESound sound,
                                    void *sampleData,        // pointer to audio data
                                    uint32_t frames,         // number of frames of audio
                                    uint16_t bitSize,        // bits per sample 8 or 16
                                    uint16_t channels,       // mono or stereo 1 or 2
                                    BAE_UNSIGNED_FIXED rate, // 16.16 fixed sample rate
                                    uint32_t loopStart,      // loop start in frames
                                    uint32_t loopEnd)        // loop end in frames
{
    OPErr theErr;

    theErr = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);

        // if sound already loaded, then free it...
        BAESound_Unload(sound);

        // load new sound
        sound->pWave = GM_ReadRawAudioIntoMemoryFromMemory(sampleData, frames,
                                                           bitSize, channels,
                                                           (XFIXED)rate, loopStart,
                                                           loopEnd, &theErr);
        if ((sound->pWave == NULL) && (theErr == NO_ERR))
        {
            theErr = BAD_FILE;
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        theErr = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(theErr);
}

// BAESound_LoadMemorySample()
// --------------------------------------
//
//
BAEResult BAESound_LoadMemorySample(BAESound sound, void *pMemoryFile, uint32_t memoryFileSize, BAEFileType fileType)
{
#if USE_HIGHLEVEL_FILE_API
    OPErr theErr;
    AudioFileType type;

    theErr = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        type = BAE_TranslateBAEFileType(fileType);
        if (type != FILE_INVALID_TYPE)
        {
            BAE_AcquireMutex(sound->mLock);

            // if sound already loaded, then free it...
            BAESound_Unload(sound);

            // load new sound
            sound->pWave = GM_ReadFileIntoMemoryFromMemory(pMemoryFile, memoryFileSize,
                                                           type, TRUE, &theErr);
            if ((sound->pWave == NULL) && (theErr == NO_ERR))
            {
                theErr = BAD_FILE;
            }
            //          else
            //          {
            //              BAE_PRINTF("audio::sound loop start %ld end %ld\n", sound->pWave->startLoop,
            //                                                              sound->pWave->endLoop);
            //          }
            BAE_ReleaseMutex(sound->mLock);
        }
        else
        {
            theErr = BAD_FILE_TYPE;
        }
    }
    else
    {
        theErr = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(theErr);
#else
    return BAE_NOT_SETUP;
#endif
}

// BAESound_LoadMemorySample()
// --------------------------------------
//
//
BAEResult BAESound_LoadFileSample(BAESound sound, BAEPathName filePath, BAEFileType fileType)
{
#if USE_HIGHLEVEL_FILE_API
    XFILENAME theFile;
    OPErr theErr;
    AudioFileType type;

    theErr = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        type = BAE_TranslateBAEFileType(fileType);
        if (type != FILE_INVALID_TYPE)
        {
            BAE_AcquireMutex(sound->mLock);

            // if sound already loaded, then free it...
            BAESound_Unload(sound);

            // load new sound...
            XConvertPathToXFILENAME(filePath, &theFile);
            sound->pWave = GM_ReadFileIntoMemory(&theFile, type, TRUE, &theErr);

            if ((sound->pWave == NULL) && (theErr == NO_ERR))
            {
                theErr = BAD_FILE;
            }
            BAE_ReleaseMutex(sound->mLock);
        }
        else
        {
            theErr = BAD_FILE_TYPE;
        }

        if ((sound->pWave == NULL) && (theErr == NO_ERR))
        {
            theErr = BAD_FILE;
        }
    }
    else
    {
        theErr = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(theErr);
#else
    fileType;
    filePath;
    sound;
    return BAE_NOT_SETUP;
#endif
}

// BAESound_IsPaused()
// --------------------------------------
//
//
BAEResult BAESound_IsPaused(BAESound sound, BAE_BOOL *outIsPaused)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        if (outIsPaused)
        {
            BAE_AcquireMutex(sound->mLock);
            *outIsPaused = (sound->mPauseVariable) ? (BAE_BOOL)TRUE : (BAE_BOOL)FALSE;
            BAE_ReleaseMutex(sound->mLock);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_Pause()
// --------------------------------------
//
//
BAEResult BAESound_Pause(BAESound sound)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (sound->mPauseVariable == 0)
        {
            BAESound_GetRate(sound, &sound->mPauseVariable);
            BAESound_SetRate(sound, 0L); // pause samples in their tracks
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_Resume()
// --------------------------------------
//
//
BAEResult BAESound_Resume(BAESound sound)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (sound->mPauseVariable)
        {
            BAESound_SetRate(sound, sound->mPauseVariable);
            sound->mPauseVariable = 0;
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_Fade()
// --------------------------------------
//
//
BAEResult BAESound_Fade(BAESound sound, BAE_FIXED sourceVolume, BAE_FIXED destVolume, BAE_FIXED timeInMiliseconds)
{
    int16_t source, dest;
    INT16 minVolume;
    INT16 maxVolume;
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (sound->voiceRef != DEAD_VOICE)
        {
#if !USE_FLOAT
            BAE_FIXED delta;
            delta = PV_CalculateTimeDeltaForFade(sourceVolume, destVolume, timeInMiliseconds);
            delta = XFixedMultiply(delta, -LONG_TO_FIXED(MAX_NOTE_VOLUME));
#else
            double delta;
            delta = PV_CalculateTimeDeltaForFade(sourceVolume, destVolume, timeInMiliseconds);
            delta = delta * -MAX_NOTE_VOLUME;
#endif
            source = FIXED_TO_SHORT_ROUNDED(sourceVolume * MAX_NOTE_VOLUME);
            dest = FIXED_TO_SHORT_ROUNDED(destVolume * MAX_NOTE_VOLUME);
            minVolume = XMIN(source, dest);
            maxVolume = XMAX(source, dest);
#if !USE_FLOAT
            GM_SetSampleFadeRate(sound->voiceRef, (delta), minVolume, maxVolume, FALSE);
#else
            GM_SetSampleFadeRate(sound->voiceRef, FLOAT_TO_FIXED(delta), minVolume, maxVolume, FALSE);
#endif
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// Sound done callback that handles looping for BAESound objects
static void PV_LoopingSoundDoneCallback(void *reference)
{
    BAESound sound;
    BAE_SoundCallbackPtr userCallback;
    void *userCallbackReference;
    XBOOL shouldRestart = FALSE;

    sound = (BAESound)reference;
    userCallback = NULL;
    userCallbackReference = NULL;

    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (sound->mixer && (sound->mixer->mID == OBJECT_ID))
        {
#if TRACKING
            if (PV_BAEMixer_ValidateObject(sound->mixer, sound, BAE_SOUND_OBJECT))
#else
            if (sound->mValid)
#endif
            {
                userCallback = sound->mCallback;
                userCallbackReference = sound->mCallbackReference;
                sound->voiceRef = DEAD_VOICE;

                // Check if we should loop
                if (sound->mLoopCount == 0xFFFFFFFF)
                {
                    // Infinite looping
                    shouldRestart = TRUE;
                }
                else if (sound->mLoopCount > 0 && sound->mCurrentLoop < sound->mLoopCount)
                {
                    // Finite looping - continue if we haven't reached the limit
                    sound->mCurrentLoop++;
                    shouldRestart = TRUE;
                }

                if (shouldRestart)
                {
                    // Restart the sound from the beginning
                    XSDWORD volume = UNSIGNED_FIXED_TO_LONG_ROUNDED(sound->mVolume * MAX_NOTE_VOLUME);
                    sound->voiceRef = GM_SetupSampleFromInfo(sound->pWave, (void *)sound,
                                                             volume,
                                                             0,
                                                             NULL,
                                                             PV_LoopingSoundDoneCallback,
                                                             0); // start from beginning
                    if (sound->voiceRef != DEAD_VOICE)
                    {
                        GM_SetSampleRouteBus(sound->voiceRef, sound->mRouteBus);
                        GM_ChangeSampleVolume(sound->voiceRef, volume);
                        GM_StartSample(sound->voiceRef);
                    }
                }
            }
        }
        BAE_ReleaseMutex(sound->mLock);

        // If we're not restarting or if this is the final iteration, call the user callback
        if (!shouldRestart && userCallback)
        {
            (*userCallback)(userCallbackReference);
        }
    }
}

static void PV_DefaultSoundDoneCallback(void *reference)
{
    BAESound sound;
    BAE_SoundCallbackPtr callback;
    void *callbackReference;

    sound = (BAESound)reference;
    callback = NULL;
    callbackReference = NULL;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (sound->mixer)
        {
            if (sound->mixer->mID == OBJECT_ID)
            {
#if TRACKING
                if (PV_BAEMixer_ValidateObject(sound->mixer, sound, BAE_SOUND_OBJECT))
#else
                if (sound->mValid)
#endif
                {
                    callback = sound->mCallback;
                    callbackReference = sound->mCallbackReference;
                    sound->voiceRef = DEAD_VOICE;
                }
                else
                {
                    BAE_PRINTF("audio:sound not in mixer list, no callback\n");
                }
            }
        }
        BAE_ReleaseMutex(sound->mLock);
        if (callback)
        {
            (*callback)(callbackReference);
        }
    }
    else
    {
        BAE_PRINTF("audio:sound no longer valid, no callback\n");
    }
}

static void PV_BAESound_SetCallback(BAESound sound, BAE_SoundCallbackPtr pCallback, void *callbackReference)
{
    sound->mCallback = pCallback;
    sound->mCallbackReference = callbackReference;

    if (pCallback == NULL) // going to clear
    {
        if (sound->voiceRef != DEAD_VOICE)
        {
            GM_SetSampleDoneCallback(sound->voiceRef, NULL, NULL);
        }
    }
}

// sample callbacks
BAEResult BAESound_SetCallback(BAESound sound, BAE_SoundCallbackPtr pCallback, void *callbackReference)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        PV_BAESound_SetCallback(sound, pCallback, callbackReference);
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

BAEResult BAESound_GetCallback(BAESound sound, BAE_SoundCallbackPtr *pResult)
{
    OPErr err;

    err = NO_ERR;
    if (sound && pResult)
    {
        BAE_AcquireMutex(sound->mLock);
        *pResult = sound->mCallback;
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_Start()
// --------------------------------------
//
//
BAEResult BAESound_Start(BAESound sound,
                         int16_t priority,
                         BAE_UNSIGNED_FIXED sampleVolume, // sample volume    (1.0)
                         uint32_t startOffsetFrame)       // starting offset in frames
{
    OPErr theErr = NO_ERR;
    int32_t volume;

    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);

        if (sound->pWave == NULL)
        {
            theErr = NOT_SETUP;
        }
#if (LOOPS_USED == LIMITED_LOOPS)
        else
        {
            theErr = GM_GetWaveformNumFrames(sound->pWave, &numFrames);
            if (!theErr && (numFrames > MAX_SAMPLE_FRAMES))
            {
                theErr = SAMPLE_TO_LARGE;
            }
        }
#endif

        if (theErr == NO_ERR)
        {
            // stop if already playing, and call the callback
            if (sound->voiceRef != DEAD_VOICE)
            {
                GM_ChangeSampleVolume(sound->voiceRef, 0);
                GM_EndSample(sound->voiceRef);
            }

            sound->voiceRef = DEAD_VOICE;
            sound->mVolume = sampleVolume;
            sound->mCurrentLoop = 0; // reset loop counter on start
            volume = UNSIGNED_FIXED_TO_LONG_ROUNDED(sampleVolume * MAX_NOTE_VOLUME);

            // Choose callback based on whether looping is enabled
            GM_SoundDoneCallbackPtr doneCallback = (sound->mLoopCount > 0) ? PV_LoopingSoundDoneCallback : PV_DefaultSoundDoneCallback;

            sound->voiceRef = GM_SetupSampleFromInfo(sound->pWave, (void *)sound,
                                                     volume,
                                                     0,
                                                     NULL,
                                                     doneCallback,
                                                     startOffsetFrame);
            if (sound->voiceRef == DEAD_VOICE)
            {
                theErr = NO_FREE_VOICES;
            }
            else
            {
                // Note: callback is already set in GM_SetupSampleFromInfo
                GM_SetSampleRouteBus(sound->voiceRef, sound->mRouteBus);
                GM_ChangeSampleVolume(sound->voiceRef, volume);
                GM_StartSample(sound->voiceRef);
            }
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        theErr = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(theErr);
}

static void PV_BAESound_Stop(BAESound sound, BAE_BOOL startFade)
{
    int16_t sampleVolume;

    sound->mPauseVariable = 0;

    // Reset loop state when stopping to prevent callbacks from restarting the sound
    sound->mCurrentLoop = 0;
    sound->mLoopCount = 0; // Disable looping when explicitly stopped

    if (sound->voiceRef != DEAD_VOICE)
    {
        if (startFade)
        {
            sampleVolume = GM_GetSampleVolume(sound->voiceRef);
            GM_SetSampleFadeRate(sound->voiceRef, PV_GetDefaultMixerFadeRate(sound->mixer),
                                 0, sampleVolume, TRUE);
        }
        else
        {
            GM_EndSample(sound->voiceRef);
#if BAE_NOT_USED
            GM_SetSampleOffsetCallbackLinks(sound->voiceRef, NULL);
#endif
        }
    }
    sound->voiceRef = DEAD_VOICE; // done
}

// BAESound_Stop()
// --------------------------------------
//
//
BAEResult BAESound_Stop(BAESound sound, BAE_BOOL startFade)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        PV_BAESound_Stop(sound, startFade);
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_GetInfo()
// --------------------------------------
//
//
BAEResult BAESound_GetInfo(BAESound sound, BAESampleInfo *outInfo)
{
    GM_Waveform *pWave;
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        if (outInfo)
        {
            BAE_AcquireMutex(sound->mLock);
            pWave = sound->pWave;
            if (pWave)
            {
                if (
                    (err = GM_GetWaveformByteSize(pWave, &outInfo->waveSize)) != NO_ERR ||
                    (err = GM_GetWaveformNumFrames(pWave, &outInfo->waveFrames)) != NO_ERR ||
                    (err = GM_GetWaveformBitDepth(pWave, &outInfo->bitSize)) != NO_ERR ||
                    (err = GM_GetWaveformNumChannels(pWave, &outInfo->channels)) != NO_ERR ||
                    (err = GM_GetWaveformSampleRate(pWave, &outInfo->sampledRate)) != NO_ERR ||
                    (err = GM_GetWaveformLoopPoints(pWave, &outInfo->startLoop, &outInfo->startLoop)) != NO_ERR ||
                    (err = GM_GetWaveformBaseMidiPitch(pWave, &outInfo->baseMidiPitch)) != NO_ERR)
                {
                    // if one of the conditions fails (non-zero error code), it will stop
                    // evaluating the rest and 'err' will store the error code.
                    // otherwise err = NO_ERR.
                }
            }
            else
            {
                err = NOT_SETUP;
            }
            BAE_ReleaseMutex(sound->mLock);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

BAEResult BAESound_GetRawPCMData(BAESound sound, char *outDataPointer,
                                 uint32_t outDataSize)
{
    BAEResult err = BAE_NO_ERROR;
    BAESampleInfo info;
    unsigned char *sampleData;
    uint32_t frames;

    if ((sound) && (sound->mID == OBJECT_ID))
    {
        if (outDataPointer && outDataSize)
        {
            BAESound_GetInfo(sound, &info);
            sampleData = BAESound_GetSamplePlaybackPointer(sound, &frames);

            if (sampleData)
            {
                if (outDataSize > info.waveSize)
                {
                    outDataSize = info.waveSize;
                }
                XBlockMove(sampleData, outDataPointer, outDataSize);
            }
            else
            {
                err = BAE_NOT_SETUP;
            }
        }
        else
        {
            err = BAE_PARAM_ERR;
        }
    }
    else
    {
        err = BAE_NOT_SETUP;
    }
    return err;
}

// BAESound_IsDone()
// --------------------------------------
//
//
BAEResult BAESound_IsDone(BAESound sound, BAE_BOOL *outIsDone)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        if (outIsDone)
        {
            BAE_AcquireMutex(sound->mLock);
            if (sound->voiceRef != DEAD_VOICE)
            {
                *outIsDone = (BAE_BOOL)GM_IsSoundDone(sound->voiceRef);
                if (*outIsDone)
                {
                    sound->voiceRef = DEAD_VOICE;
                }
            }
            else
            {
                *outIsDone = TRUE;
            }
            BAE_ReleaseMutex(sound->mLock);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

BAEResult BAESound_SetRouteBus(BAESound sound, int routeBus)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        sound->mRouteBus = routeBus;
        if (sound->voiceRef != DEAD_VOICE)
        {
            GM_SetSampleRouteBus(sound->voiceRef, routeBus);
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_SetVolume()
// --------------------------------------
//
//
BAEResult BAESound_SetVolume(BAESound sound, BAE_UNSIGNED_FIXED newVolume)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        sound->mVolume = newVolume;
        if (sound->voiceRef != DEAD_VOICE)
        {
            GM_ChangeSampleVolume(sound->voiceRef, FIXED_TO_SHORT_ROUNDED(newVolume * MAX_NOTE_VOLUME));
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_GetVolume()
// --------------------------------------
//
//
BAEResult BAESound_GetVolume(BAESound sound, BAE_UNSIGNED_FIXED *outVolume)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (outVolume)
        {
            if (sound->voiceRef != DEAD_VOICE)
            {
                *outVolume = sound->mVolume;
                //  *outVolume = UNSIGNED_RATIO_TO_FIXED(GM_GetSampleVolume(sound->voiceRef), MAX_NOTE_VOLUME);
            }
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_SetRate()
// --------------------------------------
//
//
BAEResult BAESound_SetRate(BAESound sound, BAE_UNSIGNED_FIXED newRate)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (sound->voiceRef != DEAD_VOICE)
        {
            GM_ChangeSamplePitch(sound->voiceRef, newRate);
        }
        else
        {
            err = GM_SetWaveformSampleRate(sound->pWave, newRate);
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_GetRate()
// --------------------------------------
//
//
BAEResult BAESound_GetRate(BAESound sound, BAE_UNSIGNED_FIXED *outRate)
{
    OPErr err;
    XFIXED f;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (outRate)
        {
            if (sound->voiceRef != DEAD_VOICE)
            {
                f = GM_GetSamplePitch(sound->voiceRef);
                *outRate = f;
            }
            else
            {
                err = GM_GetWaveformSampleRate(sound->pWave, &f);
                if (err == NO_ERR)
                {
                    *outRate = f;
                }
            }
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_SetSamplePlaybackPosition()
// --------------------------------------
//
//
BAEResult BAESound_SetSamplePlaybackPosition(BAESound sound, uint32_t pos)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (sound->voiceRef != DEAD_VOICE)
        {
            GM_SetSamplePlaybackPosition(sound->voiceRef, pos);
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_GetSamplePlaybackPosition()
// --------------------------------------
//
//
BAEResult BAESound_GetSamplePlaybackPosition(BAESound sound, uint32_t *outPos)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (outPos)
        {
            if (sound->voiceRef != DEAD_VOICE)
            {
                *outPos = GM_GetSamplePlaybackPosition(sound->voiceRef);
            }
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

void *BAESound_GetSamplePlaybackPointer(BAESound sound, uint32_t *outLength)
{
    void *sampleData;

    sampleData = NULL;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (outLength)
        {
            if (sound->pWave)
            {
                *outLength = sound->pWave->waveFrames;
                sampleData = sound->pWave->theWaveform;
            }
        }
/*
        else
        {
            err = PARAM_ERR;
        }
*/
        BAE_ReleaseMutex(sound->mLock);
    }
/*    else
    {
        err = NULL_OBJECT;
    }
*/  
    // BAE_TranslateOPErr(err);
    return sampleData;
}

// BAESound_SetLowPassAmountFilter()
// --------------------------------------
//
//
BAEResult BAESound_SetLowPassAmountFilter(BAESound sound, int16_t lowPassAmount)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (sound->voiceRef != DEAD_VOICE)
        {
            GM_SetSampleLowPassAmountFilter(sound->voiceRef, lowPassAmount);
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_GetLowPassAmountFilter()
// --------------------------------------
//
//
BAEResult BAESound_GetLowPassAmountFilter(BAESound sound, int16_t *outLowPassAmount)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (outLowPassAmount)
        {
            if (sound->voiceRef != DEAD_VOICE)
            {
                *outLowPassAmount = GM_GetSampleLowPassAmountFilter(sound->voiceRef);
            }
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_SetResonanceAmountFilter()
// --------------------------------------
//
//
BAEResult BAESound_SetResonanceAmountFilter(BAESound sound, int16_t resonanceAmount)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (sound->voiceRef != DEAD_VOICE)
        {
            GM_SetSampleResonanceFilter(sound->voiceRef, resonanceAmount);
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_GetResonanceAmountFilter()
// --------------------------------------
//
//
BAEResult BAESound_GetResonanceAmountFilter(BAESound sound, int16_t *outResonanceAmount)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (outResonanceAmount)
        {
            if (sound->voiceRef != DEAD_VOICE)
            {
                *outResonanceAmount = GM_GetSampleResonanceFilter(sound->voiceRef);
            }
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_SetFrequencyAmountFilter()
// --------------------------------------
//
//
BAEResult BAESound_SetFrequencyAmountFilter(BAESound sound, int16_t frequencyAmount)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (sound->voiceRef != DEAD_VOICE)
        {
            GM_SetSampleFrequencyFilter(sound->voiceRef, frequencyAmount);
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_GetFrequencyAmountFilter()
// --------------------------------------
//
//
BAEResult BAESound_GetFrequencyAmountFilter(BAESound sound, int16_t *outFrequencyAmount)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (outFrequencyAmount)
        {
            if (sound->voiceRef != DEAD_VOICE)
            {
                *outFrequencyAmount = GM_GetSampleFrequencyFilter(sound->voiceRef);
            }
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_SetSampleLoopPoints()
// --------------------------------------
//
//
BAEResult BAESound_SetSampleLoopPoints(BAESound sound, uint32_t start, uint32_t end)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (sound->pWave)
        {
            err = GM_SetWaveformLoopPoints(sound->pWave, start, end);
            if (err == NO_ERR)
            {
                if (sound->voiceRef != DEAD_VOICE)
                {
                    GM_SetSampleLoopPoints(sound->voiceRef, start, end);
                }
            }
        }
        else
        {
            err = NOT_SETUP;
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_GetSampleLoopPoints()
// --------------------------------------
//
//
BAEResult BAESound_GetSampleLoopPoints(BAESound sound, uint32_t *outStart, uint32_t *outEnd)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        if (outStart && outEnd)
        {
            if (sound->pWave)
            {
                err = GM_GetWaveformLoopPoints(sound->pWave, outStart, outEnd);
            }
            else
            {
                err = NOT_SETUP;
            }
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_SetLoopCount()
// --------------------------------------
//
//
BAEResult BAESound_SetLoopCount(BAESound sound, uint32_t loops)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        sound->mLoopCount = loops;
        BAE_ReleaseMutex(sound->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESound_GetLoopCount()
// --------------------------------------
//
//
BAEResult BAESound_GetLoopCount(BAESound sound, uint32_t *outLoops)
{
    OPErr err;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        if (outLoops)
        {
            BAE_AcquireMutex(sound->mLock);
            *outLoops = sound->mLoopCount;
            BAE_ReleaseMutex(sound->mLock);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// ------------------------------------------------------------------
// BAEStream Functions
// ------------------------------------------------------------------
#if 0
#pragma mark -
#pragma mark##### BAEStream #####
#pragma mark -
#endif

#if USE_STREAM_API == TRUE

// -----------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------
// BAEStream:  Sound effects, linear audio files, streamed
// -----------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------
BAEStream BAEStream_New(BAEMixer mixer)
{
    BAEStream stream;
    stream = NULL;

    if (mixer)
    {
        stream = (BAEStream)XNewPtr(sizeof(struct sBAEStream));
        if (stream)
        {
            if (BAEStream_SetMixer(stream, mixer) != BAE_NO_ERROR)
            {
                XDisposePtr(stream);
                stream = NULL;
            }
            else
            {
                stream->mSoundStreamVoiceReference = DEAD_STREAM;
                stream->mPauseVariable = 0;
                stream->mPrerolled = FALSE;
                stream->mVolumeState = BAE_FIXED_1; // 1.0
                stream->mPanState = BAE_CENTER_PAN; // center;
                stream->mLoop = FALSE;
                stream->mPlaybackLength = 0;
                stream->mID = OBJECT_ID;
            }
#if TRACKING
            PV_BAEMixer_AddObject(mixer, stream, BAE_STREAM_OBJECT);
#else
            if (stream)
                stream->mValid = 1;
#endif
        }
    }
    return stream;
}

// BAEStream_Delete()
// ------------------------------------
// Deactivates the indicated BAEStream, unloads its sample media data, and frees
// its memory.  Call this when done with the BAEStream object.
//
BAEResult BAEStream_Delete(BAEStream stream)
{
    OPErr err;

    err = NO_ERR;
    if (stream)
    {
        stream->mID = 0;
#if TRACKING
        PV_BAEMixer_RemoveObject(stream->mixer, stream, BAE_STREAM_OBJECT);
#else
        stream->mValid = 0;
#endif
        BAEStream_Unload(stream);
        XDisposePtr(stream);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEStream_Unload()
// --------------------------------------
//
//
BAEResult BAEStream_Unload(BAEStream stream)
{
    OPErr err;

    err = NO_ERR;
    if (stream)
    {
        // call callback now because we need for it to happen prior to deleting
        // this object.
        if (stream->mSoundStreamVoiceReference != DEAD_STREAM)
        {
            GM_AudioStreamSetDoneCallback(stream->mSoundStreamVoiceReference, NULL, NULL);
        }

        //  if (sound->pWave)
        {
            BAEStream_Stop(stream, FALSE);
            //  GM_FreeWaveform(sound->pWave);
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEStream_GetMemoryUsed()
// --------------------------------------
// Returns total number of bytes used by this object.
//
BAEResult BAEStream_GetMemoryUsed(BAEStream stream, uint32_t *pOutResult)
{
    uint32_t size;

    size = 0;
    if (stream)
    {
        // song size
        size = XGetPtrSize((XPTR)stream);
        //  size += sound->pWave->waveSize;;
    }
    if (pOutResult)
    {
        *pOutResult = size;
    }
    return BAE_NO_ERROR;
}

// BAEStream_SetMixer()
// BAEResult BAEStream_SetMixer(BAEStream stream, BAEMixer mixer);
// ------------------------------------
// Associates the indicated BAEStream with the indicated BAEMixer, replacing the
// previously associated BAEMixer.
//
BAEResult BAEStream_SetMixer(BAEStream stream,
                             BAEMixer mixer)
{
    OPErr err;

    err = NO_ERR;
    if (stream && mixer)
    {
        stream->mixer = mixer;
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEStream_GetMixer()
// ------------------------------------
// Upon return, the BAEMixer pointed at by parameter outMixer will contain the
// address of the BAEMIxer with which the indicated BAEStream is associated.
//
BAEResult BAEStream_GetMixer(BAEStream stream,
                             BAEMixer *outMixer)
{
    OPErr err;

    err = NO_ERR;
    if (stream)
    {
        if (outMixer)
        {
            *outMixer = stream->mixer;
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEStream_SetVolume()
// --------------------------------------
// Sets the playback volume of the indicated BAEStream object to the indicated
// level.  Normal volume is 1.0.
//
BAEResult BAEStream_SetVolume(BAEStream stream,
                              BAE_UNSIGNED_FIXED newVolume)
{
    BAEResult err;

    err = BAE_NO_ERROR;
    if (stream)
    {
        stream->mVolumeState = newVolume;

        if (stream->mSoundStreamVoiceReference != DEAD_STREAM)
        {
            GM_AudioStreamSetVolume(stream->mSoundStreamVoiceReference,
                                    FIXED_TO_SHORT_ROUNDED(newVolume * MAX_NOTE_VOLUME), FALSE);
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// BAEStream_GetVolume()
// --------------------------------------
// Upon return, the BAE_UNSIGNED_FIXED pointed to by parameter outVolume will hold
// a copy of the indicated BAEStream's current playback volume.
//
BAEResult BAEStream_GetVolume(BAEStream stream,
                              BAE_UNSIGNED_FIXED *outVolume)
{
    BAEResult err;

    err = BAE_NO_ERROR;

    if (stream)
    {
        if (outVolume)
        {
            *outVolume = stream->mVolumeState;
        }
        else
        {
            err = BAE_PARAM_ERR;
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// pass TRUE to entire loop stream, FALSE to not loop
BAEResult BAEStream_SetLoopFlag(BAEStream stream, BAE_BOOL loop)
{
    OPErr err;

    err = NO_ERR;
    if (stream)
    {
        stream->mLoop = loop;
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

BAEResult BAEStream_GetLoopFlag(BAEStream stream, BAE_BOOL *outLoop)
{
    OPErr err;
    err = NO_ERR;
    if (stream)
    {
        if (outLoop)
        {
            *outLoop = stream->mLoop;
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEStream_GetInfo()
// --------------------------------------
// Upon return, the BAESampleInfo pointed to by parameter outInfo will contain a
// copy of the current sample playback property set of the indicated BAEStream
// object.
// ------------------------------------
// BAEResult codes:
//           BAE_NOT_SETUP -- No stream loaded
// ------------------------------------
BAEResult BAEStream_GetInfo(BAEStream stream,
                            BAESampleInfo *outInfo)
{
    BAEResult err = BAE_NO_ERROR;

    if (stream)
    {
        if (outInfo)
        {
            *outInfo = stream->mStreamSampleInfo;
        }
        else
        {
            err = BAE_PARAM_ERR;
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

static void PV_DefaultStreamFileDoneCallback(void *reference)
{
    BAEStream pStream;
    BAE_AudioStreamCallbackPtr doneCallback;

    pStream = (BAEStream)reference;
    if (pStream)
    {
        doneCallback = pStream->mCallback;
        if (doneCallback)
        {
            (*doneCallback)(pStream, pStream->mCallbackReference);
        }
    }
}

BAEResult BAEStream_SetCallback(BAEStream stream, BAE_AudioStreamCallbackPtr callback, uint32_t reference)
{
    if (stream)
    {
        stream->mCallback = callback;
        stream->mCallbackReference = reference;
    }
    else
    {
        return BAE_NULL_OBJECT;
    }
    return BAE_NO_ERROR;
}

// BAEStream_SetupFile()
// --------------------------------------
// prepare to play a formatted file as a stream.
BAEResult BAEStream_SetupFile(BAEStream stream, BAEPathName cFileName,
                              BAEFileType fileType,
                              uint32_t bufferSize, // temp buffer to read file
                              BAE_BOOL loopFile)   // TRUE will loop file
{
    XFILENAME theFile;
    GM_Waveform fileInfo;
    AudioFileType type;
    BAEResult theErr;

    theErr = BAE_NO_ERROR;
    if (stream)
    {
        XConvertNativeFileToXFILENAME(cFileName, &theFile);

        type = BAE_TranslateBAEFileType(fileType);
        if (type != FILE_INVALID_TYPE)
        {
            if (bufferSize >= BAE_MIN_STREAM_BUFFER_SIZE)
            {
                stream->mSoundStreamVoiceReference = GM_AudioStreamFileSetup(NULL, &theFile, type, bufferSize, &fileInfo, loopFile);
                if (stream->mSoundStreamVoiceReference == DEAD_STREAM)
                {
                    theErr = BAE_BAD_FILE;
                }
                else
                {
                    stream->mStreamSampleInfo.bitSize = fileInfo.bitSize;
                    stream->mStreamSampleInfo.channels = fileInfo.channels;
                    stream->mStreamSampleInfo.sampledRate = fileInfo.sampledRate;
                    stream->mStreamSampleInfo.baseMidiPitch = fileInfo.baseMidiPitch;
                    stream->mStreamSampleInfo.waveSize = fileInfo.waveSize;
                    stream->mStreamSampleInfo.waveFrames = fileInfo.waveFrames;
                    stream->mStreamSampleInfo.startLoop = 0;
                    stream->mStreamSampleInfo.endLoop = 0;

                    stream->mPlaybackLength = fileInfo.waveFrames;
                    stream->mLoop = loopFile;

                    // set our default done callback with the object
                    GM_AudioStreamSetDoneCallback(stream->mSoundStreamVoiceReference, PV_DefaultStreamFileDoneCallback, (void *)stream);

                    theErr = BAE_TranslateOPErr(GM_AudioStreamError(stream->mSoundStreamVoiceReference));
                }
            }
            else
            {
                theErr = BAE_BUFFER_TOO_SMALL;
            }
        }
        else
        {
            theErr = BAE_BAD_FILE_TYPE;
        }
    }
    else
    {
        theErr = BAE_NULL_OBJECT;
    }
    return theErr;
}

BAEResult BAEStream_Preroll(BAEStream stream)
{
    BAEResult err;
    OPErr perr;

    err = BAE_NO_ERROR;
    if (stream)
    {
        if (stream->mPrerolled == FALSE)
        {
            if (stream->mSoundStreamVoiceReference)
            {
                GM_AudioStreamSetVolume(stream->mSoundStreamVoiceReference,
                                        FIXED_TO_SHORT_ROUNDED(stream->mVolumeState * MAX_NOTE_VOLUME), TRUE);
                GM_AudioStreamSetStereoPosition(stream->mSoundStreamVoiceReference, stream->mPanState);
                perr = GM_AudioStreamPreroll(stream->mSoundStreamVoiceReference);
                if (perr == NO_ERR)
                {
                    stream->mPrerolled = TRUE;
                }
                err = BAE_TranslateOPErr(perr);
            }
            else
            {
                err = BAE_NOT_SETUP;
            }
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// BAEStream_Start()
// --------------------------------------
// Causes playback of the indicated BAEStream to begin.
// If no voices are available at the indicated priority
// level, this function fails and returns BAE_NO_FREE_VOICES.
// ------------------------------------
// BAEResult codes:
//          BAE_NO_FREE_VOICES -- Couldn't allocate a voice at this priority
//          BAE_NOT_SETUP -- must call BAEStream_SetupFile
// ------------------------------------
BAEResult BAEStream_Start(BAEStream stream)
{
    OPErr theErr;

    if (stream)
    {
        theErr = NO_ERR;
        if (stream->mSoundStreamVoiceReference)
        {
            BAEStream_Preroll(stream);
            theErr = GM_AudioStreamStart(stream->mSoundStreamVoiceReference);
        }
        else
        {
            theErr = NOT_SETUP;
        }
    }
    else
    {
        theErr = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(theErr);
}

// BAEStream_Stop()
// --------------------------------------
// Stops playback of the indicated BAEStream in one of two ways, depending upon the
// value of the startFade parameter: either stop immediately (FALSE), or stop
// after smoothly fading the stream out over a period of about 2.2 seconds (TRUE).
// ------------------------------------
// Note: Returns immediately, not at the end of the fade-out period.
//
BAEResult BAEStream_Stop(BAEStream stream,
                         BAE_BOOL startFade)
{
    BAEResult err;
    int16_t streamVolume;
    BAE_BOOL paused;

    err = BAE_NO_ERROR;
    if (stream)
    {
        stream->mPrerolled = FALSE;
        if (stream->mSoundStreamVoiceReference != DEAD_STREAM)
        {
            BAEStream_IsPaused(stream, &paused);
            if (paused)
            {
                BAEStream_Resume(stream);
            }
            if (startFade)
            {
                streamVolume = GM_AudioStreamGetVolume(stream->mSoundStreamVoiceReference);
                GM_SetAudioStreamFadeRate(stream->mSoundStreamVoiceReference,
                                          PV_GetDefaultMixerFadeRate(stream->mixer),
                                          0, streamVolume, TRUE);
            }
            else
            {
                GM_AudioStreamStop(NULL, stream->mSoundStreamVoiceReference);
                GM_AudioStreamDrain(NULL, stream->mSoundStreamVoiceReference); // wait for it to be finished
            }
            stream->mSoundStreamVoiceReference = DEAD_STREAM;
        }
    }
    return err;
}

// BAEStream_Pause()
// ------------------------------------
// Pauses playback of the indicated BAEStream.  If already paused, this function
// will have no effect. To resume playback, call BAEStream_Resume() or
// BAEStream_Start().
//
BAEResult BAEStream_Pause(BAEStream stream)
{
    BAEResult err;

    err = BAE_NO_ERROR;
    if (stream)
    {
        if (stream->mPauseVariable == 0)
        {
            err = BAEStream_GetRate(stream, &stream->mPauseVariable);
            if (err == BAE_NO_ERROR)
            {
                err = BAEStream_SetRate(stream, 0L);
            }
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// BAEStream_Resume()
// --------------------------------------
// If the indicated BAEStream is paused at the time of this call, causes playback
// to resume from the point at which it was most recently paused. If not paused,
// this function will have no effect. Another way to resume playback after a
// pause is to call BAEStream_Start().
//
BAEResult BAEStream_Resume(BAEStream stream)
{
    BAEResult err;

    err = BAE_NO_ERROR;
    if (stream)
    {
        if (stream->mPauseVariable == 0)
        {
            err = BAEStream_SetRate(stream, stream->mPauseVariable);
            stream->mPauseVariable = 0;
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// BAEStream_IsPaused()
// ------------------------------------
// Upon return, parameter outIsPaused will point to a BAE_BOOL indicating whether
// the indicated BAEStream is currently in a paused state (TRUE) or not (FALSE).
//
BAEResult BAEStream_IsPaused(BAEStream stream,
                             BAE_BOOL *outIsPaused)
{
    BAEResult err;

    err = BAE_NO_ERROR;
    if (stream)
    {
        if (outIsPaused)
        {
            *outIsPaused = (stream->mPauseVariable) ? (BAE_BOOL)TRUE : (BAE_BOOL)FALSE;
        }
        else
        {
            err = (BAEResult)PARAM_ERR;
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// BAEStream_Fade()
// --------------------------------------
// Fades the volume of the indicated BAEStream smoothly from sourceVolume to
// destVolume, over a period of timeInMilliseconds.  Note that this may be either
// a fade up or a fade down.
//
BAEResult BAEStream_Fade(BAEStream stream,
                         BAE_FIXED sourceVolume,
                         BAE_FIXED destVolume,
                         BAE_FIXED timeInMiliseconds)
{
    BAEResult err;

    err = BAE_NO_ERROR;
    if (stream)
    {
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// BAEStream_IsDone()
// --------------------------------------
// Upon return, the BAE_BOOL pointed at by parameter outIsDone will indicate
// whether the indicated BAEStream object has (TRUE) or has not (FALSE) played all
// the way to its end and stopped on its own.
//
BAEResult BAEStream_IsDone(BAEStream stream,
                           BAE_BOOL *outIsDone)
{
    BAEResult err;
    BAE_BOOL playing;

    err = BAE_NO_ERROR;
    if (stream)
    {
        if (outIsDone)
        {
            if (stream->mSoundStreamVoiceReference != DEAD_STREAM)
            {
                playing = GM_IsAudioStreamPlaying(stream->mSoundStreamVoiceReference);
                *outIsDone = (playing) ? FALSE : TRUE;
                if (*outIsDone)
                {
                    stream->mSoundStreamVoiceReference = DEAD_STREAM;
                }
            }
        }
        else
        {
            err = BAE_PARAM_ERR;
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// BAEStream_SetRate()
// --------------------------------------
// Sets the playback sample rate of the indicated BAEStream object to the indicated
// rate, in Hertz.
//
BAEResult BAEStream_SetRate(BAEStream stream,
                            BAE_UNSIGNED_FIXED newRate)
{
    BAEResult err;

    err = BAE_NO_ERROR;
    if (stream)
    {
        if (stream->mSoundStreamVoiceReference != DEAD_STREAM)
        {
            GM_AudioStreamSetRate(stream->mSoundStreamVoiceReference, newRate);
        }
        else
        {
            err = BAE_NOT_SETUP;
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// BAEStream_GetRate()
// --------------------------------------
// Upon return, the BAE_UNSIGNED_FIXED pointed to by parameter outRate will hold a
// copy of the indicated BAEStream's current sample rate, in Hertz.
//
BAEResult BAEStream_GetRate(BAEStream stream,
                            BAE_UNSIGNED_FIXED *outRate)
{
    BAEResult err;

    err = BAE_NO_ERROR;
    if (stream)
    {
        if (stream->mSoundStreamVoiceReference != DEAD_STREAM)
        {
            if (outRate)
            {
                *outRate = GM_AudioStreamGetRate(stream->mSoundStreamVoiceReference);
            }
            else
            {
                err = BAE_PARAM_ERR;
            }
        }
        else
        {
            err = BAE_NOT_SETUP;
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// BAEStream_SetLowPassAmountFilter()
// --------------------------------------
// Sets the depth of the lowpass filter effect for the indicated
// BAEStream object.
//
BAEResult BAEStream_SetLowPassAmountFilter(BAEStream stream,
                                           int16_t lowPassAmount)
{
    BAEResult err;

    err = BAE_NO_ERROR;
    if (stream)
    {
        if (stream->mSoundStreamVoiceReference != DEAD_STREAM)
        {
            GM_AudioStreamSetLowPassAmountFilter(stream->mSoundStreamVoiceReference, lowPassAmount);
        }
        else
        {
            err = BAE_NOT_SETUP;
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// BAEStream_GetLowPassAmountFilter()
// --------------------------------------
// Upon return, the int16_t pointed to by parameter outLowPassAmount will hold a
// copy of the indicated BAEStream object's current lowpass filter effect's depth
// setting.
//
BAEResult BAEStream_GetLowPassAmountFilter(BAEStream stream,
                                           int16_t *outLowPassAmount)
{
    BAEResult err;

    err = BAE_NO_ERROR;
    if (stream)
    {
        if (stream->mSoundStreamVoiceReference != DEAD_STREAM)
        {
            if (outLowPassAmount)
            {
                *outLowPassAmount = GM_AudioStreamGetLowPassAmountFilter(stream->mSoundStreamVoiceReference);
            }
            else
            {
                err = BAE_PARAM_ERR;
            }
        }
        else
        {
            err = BAE_NOT_SETUP;
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// BAEStream_SetResonanceAmountFilter()
// --------------------------------------
// Sets the resonance of the lowpass filter effect for the indicated BAEStream
// object.
//
BAEResult BAEStream_SetResonanceAmountFilter(BAEStream stream,
                                             int16_t resonanceAmount)
{
    BAEResult err;

    err = BAE_NO_ERROR;
    if (stream)
    {
        if (stream->mSoundStreamVoiceReference != DEAD_STREAM)
        {
            GM_AudioStreamSetResonanceFilter(stream->mSoundStreamVoiceReference, resonanceAmount);
        }
        else
        {
            err = BAE_NOT_SETUP;
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// BAEStream_GetResonanceAmountFilter()
// ------------------------------------
// Upon return, the int16_t pointed to by parameter outResonanceAmount will hold
// a copy of the indicated BAEStream object's current lowpass filter effect's
// resonance setting.
//
BAEResult BAEStream_GetResonanceAmountFilter(BAEStream stream,
                                             int16_t *outResonanceAmount)
{
    BAEResult err;

    err = BAE_NO_ERROR;
    if (stream)
    {
        if (stream->mSoundStreamVoiceReference != DEAD_STREAM)
        {
            if (outResonanceAmount)
            {
                *outResonanceAmount = GM_AudioStreamGetResonanceFilter(stream->mSoundStreamVoiceReference);
            }
            else
            {
                err = BAE_PARAM_ERR;
            }
        }
        else
        {
            err = BAE_NOT_SETUP;
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// BAEStream_SetFrequencyAmountFilter()
// --------------------------------------
// Sets the frequency of the lowpass filter effect for the indicated BAEStream
// object.
//
BAEResult BAEStream_SetFrequencyAmountFilter(BAEStream stream,
                                             int16_t frequencyAmount)
{
    BAEResult err;

    err = BAE_NO_ERROR;
    if (stream)
    {
        if (stream->mSoundStreamVoiceReference != DEAD_STREAM)
        {
            GM_AudioStreamSetFrequencyFilter(stream->mSoundStreamVoiceReference, frequencyAmount);
        }
        else
        {
            err = BAE_NOT_SETUP;
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

// BAEStream_GetFrequencyAmountFilter()
// --------------------------------------
// Upon return, the int16_t pointed to by parameter outFrequencyAmount will hold
// a copy of the indicated BAEStream object's current lowpass filter effect's
// frequency setting.
//
BAEResult BAEStream_GetFrequencyAmountFilter(BAEStream stream,
                                             int16_t *outFrequencyAmount)
{
    BAEResult err;

    err = BAE_NO_ERROR;
    if (stream)
    {
        if (stream->mSoundStreamVoiceReference != DEAD_STREAM)
        {
            if (outFrequencyAmount)
            {
                *outFrequencyAmount = GM_AudioStreamGetFrequencyFilter(stream->mSoundStreamVoiceReference);
            }
            else
            {
                err = BAE_PARAM_ERR;
            }
        }
        else
        {
            err = BAE_NOT_SETUP;
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

BAEResult BAEMixer_ServiceStreams(BAEMixer theMixer)
{
    GM_AudioStreamService(NULL);
    return BAE_NO_ERROR;
}

#endif // #if USE_STREAM_API == TRUE

// ------------------------------------------------------------------
// BAESong Functions
// ------------------------------------------------------------------
#if 0
#pragma mark -
#pragma mark##### BAESong #####
#pragma mark -
#endif

// BAESong_New()
// --------------------------------------
//
//
BAESong BAESong_New(BAEMixer mixer)
{
    BAESong song;
    BAEResult result;

    song = NULL;
    if (mixer)
    {
        song = (BAESong)XNewPtr(sizeof(struct sBAESong));
        if (song)
        {
            if (BAE_NewMutex(&song->mLock, "bae", "seq", __LINE__))
            {

                song->mVolume = MAX_SONG_VOLUME;
                song->mixer = mixer;
                song->mInMixer = FALSE;
                song->mHasEmbeddedBank = FALSE;
                result = PV_BAESong_InitLiveSong(song, FALSE);
                if (result == BAE_NO_ERROR)
                {
#if TRACKING
                    PV_BAEMixer_AddObject(mixer, song, BAE_SONG_OBJECT);
#else
                    song->mValid = 1;
#endif
                    song->mID = OBJECT_ID;
                    if (song->pSong)
                    {
                        memset(song->pSong->channelActiveNotes, 0, sizeof(song->pSong->channelActiveNotes));
#if USE_SF2_SUPPORT == TRUE
                        memset(song->pSong->lastThreeControl, 0, sizeof(song->pSong->lastThreeControl));
#endif
                    }
                }

                if (result)
                {
                    BAESong_Delete(song);
                    song = NULL;
                }
            }
            else
            {
                XDisposePtr(song);
                song = NULL;
            }
        }
    }
    return song;
}

void BAESong_DisplayInfo(BAESong song)
{
    GM_Song *pSong;
    int count;

    BAE_STDERR("MiniBAE::Display Song info\n");

    if ((song) && (song->mID == OBJECT_ID))
    {
        pSong = song->pSong;

        BAE_STDERR("    seqType: ");
        if (pSong->seqType == SEQ_MIDI)
        {
            BAE_STDERR("MIDI\n");
        }
        else
        {
#if (X_PLATFORM == X_DANGER)
            if (pSong->seqType == SEQ_RTX)
            {
                BAE_STDERR("RTX\n");
            }
            else
#endif
            {
                BAE_STDERR("UNKNOWN\n");
            }
        }
        BAE_STDERR("    sequenceDataSize %u\n", pSong->sequenceDataSize);

        BAE_STDERR("    songID %d\n", pSong->songID);
        BAE_STDERR("    maxSongVoices %d\n", pSong->maxSongVoices);
        BAE_STDERR("    mixLevel %d\n", pSong->mixLevel);
        BAE_STDERR("    maxEffectVoices %d\n", pSong->maxEffectVoices);
        BAE_STDERR("    MasterTempo %u\n", pSong->MasterTempo);
        BAE_STDERR("    songTempo %d\n", pSong->songTempo);
        BAE_STDERR("    songPitchShift %d\n", pSong->songPitchShift);
        BAE_STDERR("    songPaused %s\n", pSong->songPaused ? "TRUE" : "FALSE");
        BAE_STDERR("    songPrerolled %s\n", pSong->songPrerolled ? "TRUE" : "FALSE");
        BAE_STDERR("    songPriority %d\n", pSong->songPriority);
        BAE_STDERR("    songVolume %d\n", pSong->songVolume);

        if (pSong->seqType == SEQ_MIDI)
        {
            BAE_STDERR("    ignoreBadInstruments %s\n", pSong->ignoreBadInstruments ? "TRUE" : "FALSE");
            BAE_STDERR("    allowProgramChanges %s\n", pSong->allowProgramChanges ? "TRUE" : "FALSE");
            BAE_STDERR("    loopSong %s\n", pSong->loopSong ? "TRUE" : "FALSE");
            BAE_STDERR("    metaLoopDisabled %s\n", pSong->metaLoopDisabled ? "TRUE" : "FALSE");
            BAE_STDERR("    disposeSongDataWhenDone %s\n", pSong->disposeSongDataWhenDone ? "TRUE" : "FALSE");
            BAE_STDERR("    SomeTrackIsAlive %s\n", pSong->SomeTrackIsAlive ? "TRUE" : "FALSE");
            BAE_STDERR("    songFinished %s\n", pSong->songFinished ? "TRUE" : "FALSE");
            BAE_STDERR("    songLoopCount %d\n", pSong->songLoopCount);
            BAE_STDERR("    songMaxLoopCount %d\n", pSong->songMaxLoopCount);
            BAE_STDERR("    songMidiTickLength %f\n", pSong->songMidiTickLength);
            BAE_STDERR("    songMicrosecondLength %f\n", pSong->songMicrosecondLength);

            for (count = 0; count < 16; count++)
            {
                BAE_STDERR("    channelVolume[%d] %d\n", count, pSong->channelVolume[count]);
            }
        }

        int inst = 0;

        BAE_STDERR("    Instruments loaded: ");
        for (count = 0; count < MAX_INSTRUMENTS * MAX_BANKS; count++)
        {
            if (pSong->instrumentData[count])
            {
                BAE_STDERR("%d ", count);
                inst++;
            }
        }
        BAE_STDERR("\n    total loaded %d\n", inst);
    }
    else
    {
        BAE_STDERR("    null song\n");
    }
}

// BAESong_GetMemoryUsed()
// --------------------------------------
//
//
BAEResult BAESong_GetMemoryUsed(BAESong song, uint32_t *pOutResult)
{
    uint32_t size;
    int16_t count, splitCount;
    GM_Instrument *pI, *pSI;

    size = 0;
    if ((song) && (song->mID == OBJECT_ID))
    {
        // song size
        size = XGetPtrSize((XPTR)song);
        size += song->pSong->sequenceDataSize;
        size += sizeof(GM_Song);

        // instruments size
        for (count = 0; count < MAX_INSTRUMENTS * MAX_BANKS; count++)
        {
            pI = song->pSong->instrumentData[count];
            if (pI)
            {
                size += XGetPtrSize((XPTR)pI);
                if (pI->doKeymapSplit)
                {
                    for (splitCount = 0; splitCount < pI->u.k.KeymapSplitCount; ++splitCount)
                    {
                        pSI = pI->u.k.keySplits[splitCount].pSplitInstrument;
                        size += XGetPtrSize((XPTR)pSI);
                    }
                }
                else
                {
                    size += pI->u.w.waveSize;
                }
            }
        }
    }
    if (pOutResult)
    {
        *pOutResult = size;
    }
    return BAE_NO_ERROR;
}

// PV_BAESong_InitLiveSong()
// --------------------------------------
//
//
static BAEResult PV_BAESong_InitLiveSong(BAESong song, BAE_BOOL addToMixer)
{
    OPErr err;
    int16_t maxSongVoices, maxEffectVoices, mixLevel;

    err = NO_ERR;
    if (song)
    {
        song->pSong = GM_CreateLiveSong(NULL, midiSongCount++);
        if (song->pSong)
        {
            GM_SetSongMixer(song->pSong, song->mixer->pMixer); // associate mixer to song
                                                               // other we can't load instruments

            BAEMixer_GetMidiVoices(song->mixer, &maxSongVoices);
            BAEMixer_GetSoundVoices(song->mixer, &maxEffectVoices);
            BAEMixer_GetMixLevel(song->mixer, &mixLevel);
            GM_ChangeSongVoices(song->pSong, maxSongVoices, mixLevel, maxEffectVoices);
            GM_SetVelocityCurveType(song->pSong, (VelocityCurveType)g_defaultVelocityCurve);
#if USE_SF2_SUPPORT == TRUE
            if (GM_SF2_IsActive()) { GM_EnableSF2ForSong(song->pSong, TRUE); }
#endif
            GM_SetReverbType(g_defaultReverbType);

            if (addToMixer)
            {
                err = GM_StartLiveSong(song->pSong, FALSE, CreateBankToken());
                if (err)
                {
                    song->mInMixer = TRUE;
                    while (GM_FreeSong(NULL, song->pSong) == STILL_PLAYING)
                    {
                        XWaitMicroseconds(BAE_GetSliceTimeInMicroseconds());
                    }
                    song->pSong = NULL;
                }
            }
        }
        else
        {
            err = MEMORY_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }

    return BAE_TranslateOPErr(err);
}

// PV_BAESong_Unload()
// --------------------------------------
//
//
void PV_BAESong_Unload(BAESong song)
{
    BAE_ASSERT(song);

    PV_BAESong_Stop(song, FALSE);

    XDisposePtr(song->mTitle);
    GM_KillSongNotes(song->pSong);

    while (GM_FreeSong(NULL, song->pSong) == STILL_PLAYING)
    {
        XWaitMicroseconds(BAE_GetSliceTimeInMicroseconds());
    }
    song->pSong = NULL;
}

// BAESong_Delete()
// --------------------------------------
//
//
BAEResult BAESong_Delete(BAESong song)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        song->mID = 0;

        BAE_AcquireMutex(song->mLock);

#if TRACKING
        PV_BAEMixer_RemoveObject(song->mixer, song, BAE_SONG_OBJECT);
#else
        song->mValid = 0;
#endif

        PV_BAESong_Unload(song);
        PV_BAESong_SetCallback(song, NULL, NULL);

        BAE_ReleaseMutex(song->mLock);
        BAE_DestroyMutex(song->mLock);
        XDisposePtr(song);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_GetMixer()
// --------------------------------------
//
//
BAEResult BAESong_GetMixer(BAESong song, BAEMixer *outMixer)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        if (outMixer)
        {
            *outMixer = song->mixer;
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_SetMixer()
// --------------------------------------
//
//
BAEResult BAESong_SetMixer(BAESong song, BAEMixer mixer)
{
    OPErr err;

    err = NO_ERR;
    if (song && mixer)
    {
        song->mixer = mixer;
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

BAEResult BAESong_GetTitle(BAESong song, char *cName, int maxSize)
{
    OPErr err = NO_ERR;

    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (song->mTitle == NULL)
        {
            char numbers[10];

            // make title
            song->mTitle = XDuplicateStr("Untitled");
            if (song->mTitle)
            {
                XLongToStr(numbers, song->pSong->songID);
                XStrCat(song->mTitle, " ");
                XStrCat(song->mTitle, numbers);
            }
        }
        if (song->mTitle)
        {
            if (XStrLen(song->mTitle) > maxSize)
            {
                song->mTitle[maxSize - 1] = 0;
            }
            XStrCpy(cName, song->mTitle);
        }
        else
        {
            err = MEMORY_ERR;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

BAE_BOOL BAESong_HasEmbeddedBank(BAESong song)
{
    if ((song) && (song->mID == OBJECT_ID))
    {
        return song->mHasEmbeddedBank;
    }
    return FALSE;
}

// BAESong_LoadGroovoid()
// --------------------------------------
//
//
BAEResult BAESong_LoadGroovoid(BAESong song, char *cName, BAE_BOOL ignoreBadInstruments) // was LoadFromBank
{
    SongResource *pXSong;
    int32_t size;
    OPErr theErr;
    XShortResourceID theID;
    GM_Song *pSong;

    theErr = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);

#if X_PLATFORM != X_MACINTOSH_9
        // on all platforms except MacOS9 we need a valid open resource file. BAE's resource manager is designed
        // to fall back into the MacOS resource manager if no valid BAE file is open. So this test is removed
        // MacOS.
        if (song->mixer && song->mixer->pPatchFiles)
#endif
        {
            pXSong = (SongResource *)XGetNamedResource(ID_SONG, cName, &size); // look for song
            if (pXSong)
            {
                if (song->pSong)
                {
                    PV_BAESong_Unload(song);
                    theID = midiSongCount++; // runtime midi ID
                    pSong = GM_LoadSong(song->mixer->pMixer,
                                        NULL,
                                        song,
                                        theID,
                                        (void *)pXSong,
                                        NULL,
                                        0L,
                                        NULL, // no callback
                                        TRUE, // load instruments
                                        ignoreBadInstruments,
                                        CreateBankToken(),
                                        &theErr);
                    if (pSong)
                    {
                        // things are cool
                        GM_SetDisposeSongDataWhenDoneFlag(pSong, TRUE); // dispose of midi data
                        GM_SetSongLoopFlag(pSong, FALSE);               // don't loop song
                        GM_SetVelocityCurveType(pSong, (VelocityCurveType)g_defaultVelocityCurve);
                        song->pSong = pSong;                            // preserve for use later
#if USE_SF2_SUPPORT == TRUE
                        if (GM_SF2_IsActive()) { GM_EnableSF2ForSong(song->pSong, TRUE); }
#endif
                        theErr = NO_ERR;
                    }
                    else
                    {
                        // need to re-initialize
                        PV_BAESong_InitLiveSong(song, FALSE);
                        theErr = BAD_FILE;
                    }
                }
                else
                {
                    theErr = (OPErr)BAE_GENERAL_BAD; // a BAESong must always have a pSong...
                }
                XDisposePtr(pXSong);
            }
            else
            {
                theErr = RESOURCE_NOT_FOUND;
            }
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        theErr = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(theErr);
}

// BAESong_LoadMidiFromMemory()
// --------------------------------------
//
//  pMidiData is copied in this function and can be disposed of upon return.
BAEResult BAESong_LoadMidiFromMemory(BAESong song, void const *pMidiData, uint32_t midiSize, BAE_BOOL ignoreBadInstruments)
{
    SongResource *pXSong;
    OPErr theErr;
    XShortResourceID theID;
    GM_Song *pSong;
    short soundVoices, midiVoices, mixLevel;
    char *title;
    unsigned char *extractedMidi = NULL;
    uint32_t extractedMidiLen = 0;
    XBOOL wasRMI = FALSE;

    theErr = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
        // Check if this is an RMI file and extract MIDI + DLS
        if (GM_IsRMIFile((const unsigned char *)pMidiData, midiSize))
        {
            BAE_PRINTF("[BAE] Detected RMI file format\n");
            BAEMixer_UnloadBanks(song->mixer);
            theErr = GM_LoadRMIFromMemory((const unsigned char *)pMidiData, midiSize,
                                          &extractedMidi, &extractedMidiLen, TRUE);
            if (theErr == NO_ERR && extractedMidi)
            {
                wasRMI = TRUE;
                pMidiData = extractedMidi;
                midiSize = extractedMidiLen;
            }
            else
            {
                BAE_PRINTF("[BAE] Failed to parse RMI file (error %d)\n", theErr);
                BAE_ReleaseMutex(song->mLock);
                return BAE_TranslateOPErr(theErr);
            }
        }
        else
#endif
        {
            pMidiData = XDuplicateMemory((XPTRC)pMidiData, (XDWORD)midiSize);
        }
        
        // Note: extractedMidi is already allocated, don't duplicate again
        if (pMidiData && midiSize)
        {
            theID = midiSongCount++; // runtime midi ID
            BAEMixer_GetMidiVoices(song->mixer, &midiVoices);
            BAEMixer_GetMixLevel(song->mixer, &mixLevel);
            BAEMixer_GetSoundVoices(song->mixer, &soundVoices);
            pXSong = XNewSongPtr(SONG_TYPE_SMS,
                                 theID,
                                 midiVoices,
                                 mixLevel,
                                 soundVoices,
                                 REVERB_TYPE_1);
            if (pXSong)
            {
                if (song->pSong)
                {
                    PV_BAESong_Unload(song);
#if USE_SF2_SUPPORT == TRUE
                    // if (GM_SF2_IsActive()) {
                    //    GM_ResetSF2();
                    // }
#endif                     
                    pSong = GM_LoadSong(song->mixer->pMixer,
                                        NULL,
                                        song,
                                        theID,
                                        (void *)pXSong,
                                        (void *)pMidiData,
                                        (int32_t)midiSize,
                                        NULL, // no callback
                                        TRUE, // load instruments
                                        ignoreBadInstruments,
                                        CreateBankToken(),
                                        &theErr);
                    if (pSong)
                    {
                        // things are cool
                        GM_SetDisposeSongDataWhenDoneFlag(pSong, TRUE); // dispose of midi data
                        GM_SetSongLoopFlag(pSong, FALSE);               // don't loop song
                        GM_SetVelocityCurveType(pSong, (VelocityCurveType)g_defaultVelocityCurve);
                        song->pSong = pSong;                            // preserve for use later
#if USE_SF2_SUPPORT == TRUE
                        if (GM_SF2_IsActive()) { GM_EnableSF2ForSong(song->pSong, TRUE); }
#endif

                        if (pSong->titleOffset)
                        {
                            title = XNewPtr(pSong->titleLength + 1);
                            if (title)
                            {
                                XBlockMove(((XBYTE *)pSong->sequenceData) + pSong->titleOffset,
                                           title, pSong->titleLength);
                                title[pSong->titleLength] = 0;
                            }
                            song->mTitle = title;
                        }
                    }
                    else
                    {
                        // need to re-initialize
                        PV_BAESong_InitLiveSong(song, FALSE);
                        theErr = BAD_FILE;
                    }
                }
                else
                {
                    theErr = GENERAL_BAD; // a BAESong must always have a pSong...
                }
                XDisposePtr(pXSong);
            }
            else
            {
                theErr = MEMORY_ERR;
            }
        }
        else
        {
            theErr = PARAM_ERR;
        }
        
        // Clean up RMI extraction if it was used
        if (wasRMI && extractedMidi && theErr != NO_ERR)
        {
            XDisposePtr(extractedMidi);
        }
        
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        theErr = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(theErr);
}


#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
BAEResult BAESong_LoadRmiFromMemory(BAESong song, void const *pRmiData, uint32_t rmiSize, BAE_BOOL ignoreBadInstruments, BAE_BOOL useEmbeddedBank)
{
    OPErr theErr;
    unsigned char *extractedMidi = NULL;
    uint32_t extractedMidiLen = 0;

    theErr = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        
        if (pRmiData)
        {
            // Check if this is an RMI file and extract MIDI + optional DLS
            if (GM_IsRMIFile((const unsigned char *)pRmiData, rmiSize))
            {
                BAE_PRINTF("[BAE] Detected RMI file format, useEmbeddedBank=%d\n", useEmbeddedBank);
                
                // Load RMI and extract MIDI, optionally loading embedded DLS
                theErr = GM_LoadRMIFromMemory((const unsigned char *)pRmiData, rmiSize,
                                              &extractedMidi, &extractedMidiLen, useEmbeddedBank);
                
                if (theErr == NO_ERR && extractedMidi)
                {
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
                    if (useEmbeddedBank && GM_LastRMIHadEmbeddedSoundbank())
                    {
                        song->mHasEmbeddedBank = TRUE;
                    }
#endif
                    BAE_PRINTF("[BAE] RMI processing complete, extracted %u bytes of MIDI\n", extractedMidiLen);
                    
                    // Now load the extracted MIDI data using LoadMidiFromMemory
                    BAE_ReleaseMutex(song->mLock);
                    BAEResult result = BAESong_LoadMidiFromMemory(song, extractedMidi, extractedMidiLen, ignoreBadInstruments);
                    XDisposePtr(extractedMidi); // Clean up extracted MIDI
                    return result;
                }
                else
                {
                    BAE_PRINTF("[BAE] Failed to parse RMI file (error %d)\n", theErr);
                    theErr = BAD_FILE;
                }
            }
            else
            {
                BAE_PRINTF("[BAE] Data is not RMI format\n");
                theErr = BAD_FILE;
            }
        }
        else
        {
            theErr = PARAM_ERR;
        }
        
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        theErr = NULL_OBJECT;
    }
    
    return BAE_TranslateOPErr(theErr);
}

BAEResult BAESong_LoadRmiFromFile(BAESong song, BAEPathName filePath, BAE_BOOL ignoreBadInstruments, BAE_BOOL useEmbeddedBank)
{
    XFILENAME name;
    XPTR pMidiData;
    int32_t midiSize;
    OPErr theErr;
    unsigned char *extractedMidi = NULL;
    uint32_t extractedMidiLen = 0;

    theErr = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        XConvertPathToXFILENAME(filePath, &name);
        pMidiData = PV_GetFileAsData(&name, &midiSize);
        
        if (pMidiData)
        {
            // Check if this is an RMI file and extract MIDI + optional DLS
            if (GM_IsRMIFile((const unsigned char *)pMidiData, midiSize))
            {
                BAE_PRINTF("[BAE] Detected RMI file format, useEmbeddedBank=%d\n", useEmbeddedBank);
                
                // Load RMI and extract MIDI, optionally loading embedded DLS
                theErr = GM_LoadRMIFromMemory((const unsigned char *)pMidiData, midiSize,
                                              &extractedMidi, &extractedMidiLen, useEmbeddedBank);
                XDisposePtr(pMidiData); // Free original file data
                
                if (theErr == NO_ERR && extractedMidi)
                {
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
                    if (useEmbeddedBank && GM_LastRMIHadEmbeddedSoundbank())
                    {
                        song->mHasEmbeddedBank = TRUE;
                    }
#endif
                    BAE_PRINTF("[BAE] RMI processing complete, extracted %u bytes of MIDI\n", extractedMidiLen);
                    
                    // Now load the extracted MIDI data using LoadMidiFromMemory
                    BAE_ReleaseMutex(song->mLock);
                    BAEResult result = BAESong_LoadMidiFromMemory(song, extractedMidi, extractedMidiLen, ignoreBadInstruments);
                    XDisposePtr(extractedMidi); // Clean up extracted MIDI
                    return result;
                }
                else
                {
                    BAE_PRINTF("[BAE] Failed to parse RMI file (error %d)\n", theErr);
                    theErr = BAD_FILE;
                }
            }
            else
            {
                XDisposePtr(pMidiData);
                BAE_PRINTF("[BAE] File is not RMI format\n");
                theErr = BAD_FILE;
            }
        }
        else
        {
            theErr = BAD_FILE;
        }
        
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        theErr = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(theErr);
}
#endif

// BAESong_LoadMidiFromFile()
// --------------------------------------
//
//
BAEResult BAESong_LoadMidiFromFile(BAESong song, BAEPathName filePath, BAE_BOOL ignoreBadInstruments)
{
    XFILENAME name;
    XPTR pMidiData;
    SongResource *pXSong;
    int32_t midiSize;
    OPErr theErr;
    XShortResourceID theID;
    GM_Song *pSong;
    int16_t soundVoices, midiVoices, mixLevel;

    theErr = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        XConvertPathToXFILENAME(filePath, &name);
        pMidiData = PV_GetFileAsData(&name, &midiSize);
        
        if (pMidiData)
        {
            theID = midiSongCount++; // runtime midi ID
            BAEMixer_GetMidiVoices(song->mixer, &midiVoices);
            BAEMixer_GetMixLevel(song->mixer, &mixLevel);
            BAEMixer_GetSoundVoices(song->mixer, &soundVoices);
            pXSong = XNewSongPtr(SONG_TYPE_SMS,
                                 theID,
                                 midiVoices,
                                 mixLevel,
                                 soundVoices,
                                 BAE_REVERB_TYPE_1);

            if (pXSong)
            {
                if (song->pSong)
                {                    
                    PV_BAESong_Unload(song);
#if USE_SF2_SUPPORT == TRUE
                    if (GM_SF2_IsActive()) {
                        GM_ResetSF2();
                    }
#endif                    
                    pSong = GM_LoadSong(song->mixer->pMixer,
                                        NULL,
                                        song,
                                        theID,
                                        (void *)pXSong,
                                        pMidiData,
                                        midiSize,
                                        NULL, // no callback
                                        TRUE, // load instruments
                                        ignoreBadInstruments,
                                        CreateBankToken(),
                                        &theErr);
                    if (pSong)
                    {
                        // things are cool
                        GM_SetDisposeSongDataWhenDoneFlag(pSong, TRUE); // dispose of midi data
                        GM_SetSongLoopFlag(pSong, FALSE);               // don't loop song
                        GM_SetVelocityCurveType(pSong, (VelocityCurveType)g_defaultVelocityCurve);
                        song->pSong = pSong;                            // preserve for use later
#if USE_SF2_SUPPORT == TRUE
                        if (GM_SF2_IsActive()) { GM_EnableSF2ForSong(song->pSong, TRUE); }
#endif
                    }
                    else
                    {
                        // need to re-initialize
                        PV_BAESong_InitLiveSong(song, FALSE);
                        theErr = BAD_FILE;
                        XDisposePtr(pMidiData);
                    }
                }
                else
                {
                    theErr = (OPErr)BAE_GENERAL_BAD; // a BAESong must always have a pSong...
                }
                XDisposePtr(pXSong);
            }
            else
            {
                XDisposePtr(pMidiData);
                theErr = MEMORY_ERR;
            }
        }
        else
        {
            theErr = BAD_FILE;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        theErr = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(theErr);
}

// BAESong_LoadRmfFromMemory()
// --------------------------------------
// was BAERmfSong::LoadFromMemory()
//
BAEResult BAESong_LoadRmfFromMemory(BAESong song, void const *pRMFData, uint32_t rmfSize, int16_t songIndex, BAE_BOOL ignoreBadInstruments)
{
#if USE_FULL_RMF_SUPPORT == TRUE
    XFILE fileRef;
    SongResource *pXSong;
    GM_Song *pSong;
    OPErr theErr;
    XLongResourceID theID;
    int32_t size;

    theErr = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (pRMFData && rmfSize)
        {
            fileRef = XFileOpenResourceFromMemory((XPTR)pRMFData, rmfSize, TRUE);
            if (fileRef)
            {
                pXSong = (SongResource *)XGetIndexedFileResource(fileRef, ID_SONG, &theID, songIndex, NULL, &size);
                if (pXSong)
                {
                    if (song->pSong)
                    {
                        PV_BAESong_Unload(song);
#if USE_SF2_SUPPORT == TRUE
                        // if (GM_SF2_IsActive()) {
                        //    GM_ResetSF2();
                        // }
#endif                         
                        pSong = GM_LoadSong(song->mixer->pMixer,
                                            NULL,
                                            song,
                                            (XShortResourceID)theID,
                                            (void *)pXSong,
                                            NULL,
                                            0L,
                                            NULL, // no callback
                                            TRUE, // load instruments
                                            ignoreBadInstruments,
                                            CreateBankToken(),
                                            &theErr);
                        if (pSong)
                        {
                            // things are cool
                            GM_SetDisposeSongDataWhenDoneFlag(pSong, TRUE); // dispose of midi data
                            GM_SetSongLoopFlag(pSong, FALSE);               // don't loop song
#if USE_SF2_SUPPORT == TRUE
                            uint32_t instBuf[MAX_INSTRUMENTS];
                            uint32_t totalInst = 0;
                            BAEUtil_GetRmfInstrumentList(fileRef, rmfSize, songIndex, instBuf, MAX_INSTRUMENTS, &totalInst);
                            pSong->RMFInstrumentIDs[0] = totalInst; // Store count first
                            for (uint32_t i = 1; i <= totalInst; i++)
                            {
                                pSong->RMFInstrumentIDs[i] = instBuf[i-1];
                            }
                            BAE_PRINTF("Found %u Instruments in RMF (stored=%u)\n", totalInst, (unsigned)XMIN(totalInst, MAX_INSTRUMENTS));
                            for (uint32_t i = 1; i <= totalInst; i++) {
                                BAE_PRINTF("    %u - INST: %u\n", i, pSong->RMFInstrumentIDs[i]);
                            }
                            pSong->songFlags = SONG_FLAG_IS_RMF;
                            if (GM_SF2_IsActive()) { GM_EnableSF2ForSong(pSong, TRUE); }
#endif
                            GM_SetVelocityCurveType(pSong, (VelocityCurveType)g_defaultVelocityCurve);
                            song->pSong = pSong;                            // preserve for use later
                        }
                        else
                        {
                            // need to re-initialize
                            PV_BAESong_InitLiveSong(song, FALSE);
                            theErr = BAD_FILE;
                        }
                    }
                    else
                    {
                        theErr = GENERAL_BAD; // a BAESong must always have a pSong...
                    }
                    XDisposePtr(pXSong);
                }
                else
                {
                    theErr = RESOURCE_NOT_FOUND;
                    // Fallback attempt: direct memory scan if primary lookup failed.
                    SongResource *fallbackSong = PV_FallbackFindSongInRMFMemory(pRMFData, rmfSize, songIndex, &theID, &size);
                    if (fallbackSong)
                    {
                        pXSong = fallbackSong; // treat as found
                        theErr = NO_ERR;
                        if (song->pSong)
                        {
                            PV_BAESong_Unload(song);
                            // if (GM_SF2_IsActive()) { GM_ResetSF2(); }
                            pSong = GM_LoadSong(song->mixer->pMixer,
                                                NULL,
                                                song,
                                                (XShortResourceID)theID,
                                                (void *)pXSong,
                                                NULL,
                                                0L,
                                                NULL, // no callback
                                                TRUE, // load instruments
                                                ignoreBadInstruments,
                                                CreateBankToken(),
                                                &theErr);
                            if (pSong)
                            {
                                GM_SetDisposeSongDataWhenDoneFlag(pSong, TRUE);
                                GM_SetSongLoopFlag(pSong, FALSE);
                                GM_SetVelocityCurveType(pSong, (VelocityCurveType)g_defaultVelocityCurve);
                                song->pSong = pSong;
#if USE_SF2_SUPPORT == TRUE
                                if (GM_SF2_IsActive()) { GM_EnableSF2ForSong(song->pSong, TRUE); }
#endif
                            }
                            else
                            {
                                PV_BAESong_InitLiveSong(song, FALSE);
                                theErr = BAD_FILE;
                            }
                        }
                        else
                        {
                            theErr = GENERAL_BAD;
                        }
                        XDisposePtr(pXSong); // free fallback copy
                    }
                }
                XFileClose(fileRef);
            }
            else
            {
                theErr = BAD_FILE;
            }
        }
        else
        {
            theErr = PARAM_ERR;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        theErr = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(theErr);
#else
    return BAE_NOT_SETUP;
#endif // #if USE_FULL_RMF_SUPPORT == TRUE
}

#if USE_SF2_SUPPORT == TRUE
// Forward declaration
// Enumerate INST resource IDs in an RMF/IREZ image. If pOutInstruments is non-NULL, up to maxInstruments
// IDs are written into that array. pOutNumInstruments always receives the total number of INST resources
// discovered (may be > maxInstruments causing truncation). Pass pOutInstruments=NULL to just count.
BAEResult BAEUtil_GetRmfInstrumentList(void *pRMFData, uint32_t rmfSize, int16_t songIndex,
                                       uint32_t *pOutInstruments, uint32_t maxInstruments,
                                       uint32_t *pOutNumInstruments);

BAEResult TranslateInstrumentToBankProgram(uint32_t rmfInstId, uint32_t *bankId, uint32_t *progId, uint32_t *noteId)
{
    unsigned int multiplier = (rmfInstId / 128);
    unsigned int bank = (multiplier * 128) / 256;
    unsigned int program = rmfInstId - (multiplier * 128);
    if (
        (rmfInstId >= 128 && rmfInstId < 256) ||
        (rmfInstId >= 384 && rmfInstId < 512) ||
        (rmfInstId >= 640 && rmfInstId < 768)
    )
    {
        // percussion
        *bankId = (bank > 0) ? bank : 128;
        *progId = 0;
        *noteId = program;
    } else {
        *bankId = bank;
        *progId = program;
        *noteId = 0;
    }
    return BAE_NO_ERROR;
}
#endif

// BAESong_LoadRmfFromFile()
// --------------------------------------
//
//
BAEResult BAESong_LoadRmfFromFile(BAESong song, BAEPathName filePath, int16_t songIndex, BAE_BOOL ignoreBadInstruments)
{
#if USE_FULL_RMF_SUPPORT == TRUE
    XFILE fileRef;
    XFILENAME name;
    SongResource *pXSong;
    GM_Song *pSong;
    OPErr theErr;
    XLongResourceID theID;
    int32_t size;

    theErr = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        XConvertPathToXFILENAME(filePath, &name);
        fileRef = XFileOpenResource(&name, TRUE);
        if (fileRef)
        {
            pXSong = (SongResource *)XGetIndexedFileResource(fileRef, ID_SONG, &theID, songIndex, NULL, &size);
            if (pXSong)
            {
                if (song->pSong)
                {
                    PV_BAESong_Unload(song);
#if USE_SF2_SUPPORT == TRUE
                    // if (GM_SF2_IsActive()) {
                    //    GM_ResetSF2();
                    // }
#endif                    
                    pSong = GM_LoadSong(song->mixer->pMixer,
                                        NULL,
                                        song,
                                        (XShortResourceID)theID,
                                        (void *)pXSong,
                                        NULL,
                                        0L,
                                        NULL, // no callback
                                        TRUE, // load instruments
                                        ignoreBadInstruments,
                                        CreateBankToken(),
                                        &theErr);
                    if (pSong)
                    {
#if USE_SF2_SUPPORT == TRUE
                        uint32_t instBuf[MAX_INSTRUMENTS];
                        uint32_t totalInst = 0;
                        uint32_t fileSize = (uint32_t)XFileGetLength(fileRef);
                        BAEUtil_GetRmfInstrumentList(fileRef, fileSize, songIndex, instBuf, MAX_INSTRUMENTS, &totalInst);
                        BAE_PRINTF("pSong = %p\n", pSong);
                        BAE_PRINTF("instBuf[0] = %d\n", instBuf[0]);
                        pSong->RMFInstrumentIDs[0] = totalInst; // Store count first
                        for (uint32_t i = 1; i <= totalInst; i++)
                        {
                            pSong->RMFInstrumentIDs[i] = instBuf[i-1];
                        }
                        BAE_PRINTF("Found %u Instruments in RMF (stored=%u)\n", totalInst, (unsigned)XMIN(totalInst, MAX_INSTRUMENTS));
                        for (uint32_t i = 1; i <= totalInst; i++) {
                            BAE_PRINTF("    %u - INST: %u\n", i, pSong->RMFInstrumentIDs[i]);
                        }
                        pSong->songFlags = SONG_FLAG_IS_RMF;                      
#endif
                        // things are cool
                        GM_SetDisposeSongDataWhenDoneFlag(pSong, TRUE); // dispose of midi data
                        GM_SetSongLoopFlag(pSong, FALSE);               // don't loop song
                        GM_SetVelocityCurveType(pSong, (VelocityCurveType)g_defaultVelocityCurve);
                        song->pSong = pSong;                            // preserve for use later
                        
#if USE_SF2_SUPPORT == TRUE
                        if (GM_SF2_IsActive()) { GM_EnableSF2ForSong(song->pSong, TRUE); }
#endif
                    }
                    else
                    {
                        // need to re-initialize
                        PV_BAESong_InitLiveSong(song, FALSE);
                        theErr = BAD_FILE;
                    }                    
                }
                else
                {
                    theErr = GENERAL_BAD; // a BAESong should always have a pSong...
                }
                XDisposePtr(pXSong);
            }
            else
            {
                theErr = RESOURCE_NOT_FOUND;
            }
            XFileClose(fileRef);
        }
        else
        {
            theErr = BAD_FILE;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        theErr = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(theErr);
#else
    return BAE_NOT_SETUP;
#endif // #if USE_FULL_RMF_SUPPORT == TRUE
}

BAEResult BAESong_SetRouteBus(BAESong song, int routeBus)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        song->mRouteBus = routeBus;
        GM_SetSongRouteBus(song->pSong, routeBus);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

BAEResult BAESong_SetVelocityCurve(BAESong song, int curveType)
{
    OPErr err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        if (curveType < 0)
            curveType = 0;
        if (curveType > 4)
            curveType = 4; // engine currently supports 0..4
        BAE_AcquireMutex(song->mLock);
        if (song->pSong)
        {
            GM_SetVelocityCurveType(song->pSong, (VelocityCurveType)curveType);
        }
        else
        {
            err = NOT_SETUP;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_SetVolume()
// --------------------------------------
//
//
BAEResult BAESong_SetVolume(BAESong song, BAE_UNSIGNED_FIXED volume)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        song->mVolume = FIXED_TO_SHORT_ROUNDED(volume * MAX_SONG_VOLUME);
        GM_SetSongVolume(song->pSong, song->mVolume);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_GetVolume()
// --------------------------------------
//
//
BAEResult BAESong_GetVolume(BAESong song, BAE_UNSIGNED_FIXED *outVolume)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        if (outVolume)
        {
            BAE_AcquireMutex(song->mLock);
            song->mVolume = GM_GetSongVolume(song->pSong);
            *outVolume = UNSIGNED_RATIO_TO_FIXED(song->mVolume, MAX_SONG_VOLUME);
            BAE_ReleaseMutex(song->mLock);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_SetTranspose()
// --------------------------------------
//
//
BAEResult BAESong_SetTranspose(BAESong song, int32_t semitones)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        semitones *= -1;
        if ((semitones > -128) && (semitones < 128))
        {
            BAE_AcquireMutex(song->mLock);
            GM_SetSongPitchOffset(song->pSong, semitones);
            BAE_ReleaseMutex(song->mLock);
        }
    }
    else
    {
        err = PARAM_ERR;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_GetTranspose()
// --------------------------------------
//
//
BAEResult BAESong_GetTranspose(BAESong song, int32_t *outSemitones)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        if (outSemitones)
        {
            BAE_AcquireMutex(song->mLock);
            *outSemitones = -GM_GetSongPitchOffset(song->pSong);
            BAE_ReleaseMutex(song->mLock);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_AllowChannelTranspose()
// --------------------------------------
//
//
BAEResult BAESong_AllowChannelTranspose(BAESong song, uint16_t channel, BAE_BOOL allowTranspose)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        GM_AllowChannelPitchOffset(song->pSong, channel, allowTranspose);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_DoesChannelAllowTranspose()
// --------------------------------------
//
//
BAEResult BAESong_DoesChannelAllowTranspose(BAESong song, uint16_t channel, BAE_BOOL *outAllowTranspose)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        if (outAllowTranspose)
        {
            BAE_AcquireMutex(song->mLock);
            *outAllowTranspose = GM_DoesChannelAllowPitchOffset(song->pSong, channel);
            BAE_ReleaseMutex(song->mLock);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_MuteChannel()
// --------------------------------------
//
//
BAEResult BAESong_MuteChannel(BAESong song, uint16_t channel)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        GM_MuteChannel(song->pSong, channel);
        memset(song->pSong->channelActiveNotes[channel], 0, sizeof(song->pSong->channelActiveNotes[channel]));
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_UnmuteChannel()
// --------------------------------------
//
//
BAEResult BAESong_UnmuteChannel(BAESong song, uint16_t channel)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        GM_UnmuteChannel(song->pSong, channel);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_GetChannelMuteStatus()
// --------------------------------------
//
//
BAEResult BAESong_GetChannelMuteStatus(BAESong song, BAE_BOOL *outChannels)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        if (outChannels)
        {
            BAE_AcquireMutex(song->mLock);
            GM_GetChannelMuteStatus(song->pSong, outChannels);
            BAE_ReleaseMutex(song->mLock);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_SoloChannel()
// --------------------------------------
//
//
BAEResult BAESong_SoloChannel(BAESong song, uint16_t channel)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        GM_SoloChannel(song->pSong, channel);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_UnSoloChannel()
// --------------------------------------
//
//
BAEResult BAESong_UnSoloChannel(BAESong song, uint16_t channel)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        GM_UnsoloChannel(song->pSong, channel);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_GetChannelSoloStatus()
// --------------------------------------
//
//
BAEResult BAESong_GetChannelSoloStatus(BAESong song, BAE_BOOL *outChannels)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        if (outChannels)
        {
            BAE_AcquireMutex(song->mLock);
            GM_GetChannelSoloStatus(song->pSong, outChannels);
            BAE_ReleaseMutex(song->mLock);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_GetActiveNotes()
// --------------------------------------
// Thread-safe copy of current active note velocities for a channel.
BAEResult BAESong_GetActiveNotes(BAESong song, unsigned char channel, unsigned char *outNotes)
{
    OPErr err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        if (outNotes && channel < 16)
        {
            memset(outNotes, 0, 128);
            BAE_AcquireMutex(song->mLock);
            if (song->pSong)
            {
                memcpy(outNotes, song->pSong->channelActiveNotes[channel], 128);
            }
            else
            {
                err = NOT_SETUP;
            }
            BAE_ReleaseMutex(song->mLock);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_LoadInstrument()
// --------------------------------------
//
//
BAEResult BAESong_LoadInstrument(BAESong song, BAE_INSTRUMENT instrument)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (song->pSong) // MOVE THIS CHECK INTO ENGINE
        {
            if (song->mInMixer == FALSE)
            {
                song->mInMixer = TRUE;
                err = BAE_TranslateBAErr(PV_BAESong_InitLiveSong(song, TRUE));
            }
            if (err == NO_ERR)
            {
                song->mInstrumentsLoadedCount++;
                err = GM_LoadSongInstrument(song->pSong,
                                            (XLongResourceID)instrument,
                                            CreateBankToken());
            }
        }
        else
        {
            err = NOT_SETUP;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_UnloadInstrument()
// --------------------------------------
//
//
BAEResult BAESong_UnloadInstrument(BAESong song, BAE_INSTRUMENT instrument)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (song->pSong) // MOVE THIS CHECK INTO ENGINE
        {
            err = GM_UnloadSongInstrument(song->pSong, (XLongResourceID)instrument);
            if (song->mInstrumentsLoadedCount)
            {
                song->mInstrumentsLoadedCount--;
            }
            else
            {
                song->mInMixer = FALSE;
                PV_BAESong_Stop(song, FALSE); // remove from bae mixer
            }
        }
        else
        {
            err = NOT_SETUP;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_IsInstrumentLoaded()
// --------------------------------------
//
//
BAEResult BAESong_IsInstrumentLoaded(BAESong song, BAE_INSTRUMENT instrument, BAE_BOOL *outIsLoaded)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        if (outIsLoaded)
        {
            BAE_AcquireMutex(song->mLock);
            *outIsLoaded = (BAE_BOOL)GM_IsSongInstrumentLoaded(song->pSong, (XLongResourceID)instrument);
            BAE_ReleaseMutex(song->mLock);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_GetControlValue()
// --------------------------------------
//
//
BAEResult BAESong_GetControlValue(BAESong song, unsigned char channel, unsigned char controller, char *outValue)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outValue)
        {
            if (song->pSong) // MOVE THIS CHECK INTO THE ENGINE
            {
                *outValue = GM_GetControllerValue(song->pSong, channel, controller);
            }
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_GetProgramBank()
// --------------------------------------
//
//
BAEResult BAESong_GetProgramBank(BAESong song,
                                 unsigned char channel,
                                 unsigned char *outProgram,
                                 unsigned char *outBank)
{
    OPErr err;
    XSWORD bank, program;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outBank && outProgram)
        {
            if (song->pSong)
            {
                GM_GetProgramBank(song->pSong, channel, &program, &bank);
                *outProgram = (unsigned char)program;
                *outBank = (unsigned char)bank;

                //              *outProgram = (unsigned char)(song->pSong)->channelProgram[channel];
                //              *outBank = (unsigned char)(song->pSong)->channelBank[channel];
            }
            else
            {
                err = NOT_SETUP;
            }
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_GetPitchBend()
// --------------------------------------
//
//
BAEResult BAESong_GetPitchBend(BAESong song,
                               unsigned char channel,
                               unsigned char *outLSB,
                               unsigned char *outMSB)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outLSB && outMSB)
        {
            GM_GetPitchBend(song->pSong, channel, outLSB, outMSB);
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_NoteOff()
// --------------------------------------
//
//
BAEResult BAESong_NoteOff(BAESong song,
                          unsigned char channel,
                          unsigned char note,
                          unsigned char velocity,
                          uint32_t time)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (time == 0)
        {
            time = GM_GetSyncTimeStamp();
        }

        QGM_NoteOff(song->pSong, time, channel, note, velocity);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_NoteOnWithLoad()
// --------------------------------------
//
//
BAEResult BAESong_NoteOnWithLoad(BAESong song,
                                 unsigned char channel,
                                 unsigned char note,
                                 unsigned char velocity,
                                 uint32_t time)
{
    BAE_INSTRUMENT inst;
    unsigned char program, bank;
    BAEMixer mixer = NULL;
    OPErr err;
    BAE_BOOL isLoaded;
    uint32_t latency = 0;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        BAESong_GetMixer(song, &mixer);
        // wait around for at least one slice to let events catch up
        BAEMixer_GetAudioLatency(mixer, &latency);
        XWaitMicroseconds(latency / 1000);
        XWaitMicroseconds(latency / 1000);

        // pull the current program, bank from the current state. Should be valid by this time.
        BAESong_GetProgramBank(song, channel, &program, &bank);
        inst = TranslateBankProgramToInstrument(bank, program, channel, note);
        if (BAESong_IsInstrumentLoaded(song, inst, &isLoaded) == BAE_NO_ERROR)
        {
            if (isLoaded == FALSE)
            {
                BAESong_LoadInstrument(song, inst);
            }
            if (time == 0)
            {
                time = GM_GetSyncTimeStamp();
            }

            QGM_NoteOn(song->pSong, time, channel, note, velocity);
        }
        else
        {
            err = (OPErr)BAE_GENERAL_BAD;
        }

        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_NoteOn()
// --------------------------------------
//
//
BAEResult BAESong_NoteOn(BAESong song,
                         unsigned char channel,
                         unsigned char note,
                         unsigned char velocity,
                         uint32_t time)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (time == 0)
        {
            time = GM_GetSyncTimeStamp();
        }

        QGM_NoteOn(song->pSong, time, channel, note, velocity);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_KeyPressure()
// --------------------------------------
//
//
BAEResult BAESong_KeyPressure(BAESong song,
                              unsigned char channel,
                              unsigned char note,
                              unsigned char pressure,
                              uint32_t time)
{
    return BAE_NO_ERROR;
}

// BAESong_ControlChange()
// --------------------------------------
//
//

BAEResult BAESong_ControlChange(BAESong song,
                                unsigned char channel,
                                unsigned char controlNumber,
                                unsigned char controlValue,
                                uint32_t time)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (time == 0)
        {
            time = GM_GetSyncTimeStamp();
        }

        QGM_Controller(song->pSong, time, channel, controlNumber, controlValue);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_ProgramBankChange()
// --------------------------------------
//
//
BAEResult BAESong_ProgramBankChange(BAESong song,
                                    unsigned char channel,
                                    unsigned char programNumber,
                                    unsigned char bankNumber,
                                    uint32_t time)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (time == 0)
        {
            time = GM_GetSyncTimeStamp();
        }

        QGM_Controller(song->pSong, time, channel, 0, bankNumber);
        QGM_ProgramChange(song->pSong, time, channel, programNumber);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_ProgramChange()
// --------------------------------------
//
//
BAEResult BAESong_ProgramChange(BAESong song,
                                unsigned char channel,
                                unsigned char programNumber,
                                uint32_t time)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (time == 0)
        {
            time = GM_GetSyncTimeStamp();
        }

        QGM_ProgramChange(song->pSong, time, channel, programNumber);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_ChannelPressure()
// --------------------------------------
//
//
BAEResult BAESong_ChannelPressure(BAESong song,
                                  unsigned char channel,
                                  unsigned char pressure,
                                  uint32_t time)
{
    return BAE_NO_ERROR;
}

// BAESong_PitchBend()
// --------------------------------------
//
//
BAEResult BAESong_PitchBend(BAESong song,
                            unsigned char channel,
                            unsigned char lsb,
                            unsigned char msb,
                            uint32_t time)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (time == 0)
        {
            time = GM_GetSyncTimeStamp();
        }

        QGM_PitchBend(song->pSong, time, channel, msb, lsb);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_AllNotesOff()
// --------------------------------------
//
//
BAEResult BAESong_AllNotesOff(BAESong song, uint32_t time)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (time == 0)
        {
            time = GM_GetSyncTimeStamp();
        }

        QGM_AllNotesOff(song->pSong, time);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_ParseMidiData()
// --------------------------------------
//
//
BAEResult BAESong_ParseMidiData(BAESong song, unsigned char commandByte, unsigned char data1Byte,
                                unsigned char data2Byte, unsigned char data3Byte,
                                uint32_t time)
{
    BAEResult theErr;
    unsigned char channel;

    theErr = BAE_NO_ERROR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        channel = commandByte & 0x0F;
        switch (commandByte & 0xF0)
        {
        case NOTE_OFF: // Note off
            theErr = BAESong_NoteOff(song, channel, data1Byte, data2Byte, time);
            break;
        case NOTE_ON: // Note on
            theErr = BAESong_NoteOn(song, channel, data1Byte, data2Byte, time);
            break;
        case POLY_AFTERTOUCH: // key pressure (aftertouch)
            theErr = BAESong_KeyPressure(song, channel, data1Byte, data2Byte, time);
            break;
        case CONTROL_CHANGE: // controllers
            theErr = BAESong_ControlChange(song, channel, data1Byte, data2Byte, time);
            break;
        case PROGRAM_CHANGE: // Program change
            theErr = BAESong_ProgramChange(song, channel, data1Byte, time);
            break;
        case CHANNEL_AFTERTOUCH: // channel pressure (aftertouch)
            theErr = BAESong_ChannelPressure(song, channel, data1Byte, time);
            break;
        case PITCH_BEND: // SetPitchBend
            theErr = BAESong_PitchBend(song, channel, data1Byte, data2Byte, time);
            break;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        theErr = BAE_NULL_OBJECT;
    }
    return theErr;
}

// BAESong_InjectMidiMessage
// Injects an arbitrary raw MIDI message into the song's raw MIDI event path.
BAEResult BAESong_InjectMidiMessage(BAESong song, const unsigned char *message, int16_t length, uint32_t time)
{
    BAEResult theErr = BAE_NO_ERROR;

    if (!song || song->mID != OBJECT_ID)
        return BAE_NULL_OBJECT;
    if (!message || length <= 0)
        return BAE_PARAM_ERR;

    BAE_AcquireMutex(song->mLock);
#ifdef _DEBUG
    // Debug: descriptive log for injected raw MIDI message
    {
        char desc[256];
        desc[0] = '\0';
        unsigned char st = message[0];
        if (st < 0xF0)
        {
            unsigned char mtype = st & 0xF0;
            unsigned char mch = st & 0x0F;
            switch (mtype)
            {
            case 0x80:
                if (length >= 3)
                    snprintf(desc, sizeof(desc), "NoteOff ch=%u note=%u vel=%u", mch, message[1], message[2]);
                else
                    snprintf(desc, sizeof(desc), "NoteOff ch=%u (truncated)", mch);
                break;
            case 0x90:
                if (length >= 3)
                    snprintf(desc, sizeof(desc), "NoteOn ch=%u note=%u vel=%u", mch, message[1], message[2]);
                else
                    snprintf(desc, sizeof(desc), "NoteOn ch=%u (truncated)", mch);
                break;
            case 0xA0:
                if (length >= 3)
                    snprintf(desc, sizeof(desc), "PolyAftertouch ch=%u note=%u pressure=%u", mch, message[1], message[2]);
                else
                    snprintf(desc, sizeof(desc), "PolyAftertouch ch=%u (truncated)", mch);
                break;
            case 0xB0:
                if (length >= 3)
                    snprintf(desc, sizeof(desc), "ControlChange ch=%u cc=%u val=%u", mch, message[1], message[2]);
                else
                    snprintf(desc, sizeof(desc), "ControlChange ch=%u (truncated)", mch);
                break;
            case 0xC0:
                if (length >= 2)
                    snprintf(desc, sizeof(desc), "ProgramChange ch=%u prog=%u", mch, message[1]);
                else
                    snprintf(desc, sizeof(desc), "ProgramChange ch=%u (truncated)", mch);
                break;
            case 0xD0:
                if (length >= 2)
                    snprintf(desc, sizeof(desc), "ChannelPressure ch=%u pressure=%u", mch, message[1]);
                else
                    snprintf(desc, sizeof(desc), "ChannelPressure ch=%u (truncated)", mch);
                break;
            case 0xE0:
                if (length >= 3)
                    snprintf(desc, sizeof(desc), "PitchBend ch=%u lsb=%u msb=%u", mch, message[1], message[2]);
                else
                    snprintf(desc, sizeof(desc), "PitchBend ch=%u (truncated)", mch);
                break;
            default:
                snprintf(desc, sizeof(desc), "ChannelMessage 0x%02X ch=%u", mtype, mch);
                break;
            }
        }
        else
        {
            switch (st)
            {
            case 0xF0:
                snprintf(desc, sizeof(desc), "SysEx start len=%d", (int)length);
                break;
            case 0xF7:
                snprintf(desc, sizeof(desc), "SysEx continuation/EOX len=%d", (int)length);
                break;
            case 0xF1:
                snprintf(desc, sizeof(desc), "MTC Quarter Frame");
                break;
            case 0xF2:
                snprintf(desc, sizeof(desc), "Song Position Pointer");
                break;
            case 0xF3:
                snprintf(desc, sizeof(desc), "Song Select");
                break;
            case 0xF6:
                snprintf(desc, sizeof(desc), "Tune Request");
                break;
            case 0xF8:
                snprintf(desc, sizeof(desc), "Timing Clock");
                break;
            case 0xFA:
                snprintf(desc, sizeof(desc), "Start");
                break;
            case 0xFB:
                snprintf(desc, sizeof(desc), "Continue");
                break;
            case 0xFC:
                snprintf(desc, sizeof(desc), "Stop");
                break;
            case 0xFE:
                snprintf(desc, sizeof(desc), "Active Sensing");
                break;
            case 0xFF:
                snprintf(desc, sizeof(desc), "System Reset/Meta");
                break;
            default:
                snprintf(desc, sizeof(desc), "System 0x%02X len=%d", st, (int)length);
                break;
            }
        }

        int dump_len = (length > 64) ? 64 : length;
        char hexdump[64 * 3 + 16];
        hexdump[0] = '\0';
        for (int i = 0; i < dump_len; i++)
        {
            char tmp[8];
            snprintf(tmp, sizeof(tmp), "%02X ", message[i]);
            strncat(hexdump, tmp, sizeof(hexdump) - strlen(hexdump) - 1);
        }
        if (length > dump_len)
            strncat(hexdump, "...", sizeof(hexdump) - strlen(hexdump) - 1);
        BAE_PRINTF("BAE-INJECT: time=%u %s (len=%d) hex=%s\n", time, desc, (int)length, hexdump);
    }
#endif
    // If the GM_Song has a registered raw MIDI event callback, call it directly.
    if (song->pSong && song->pSong->midiEventCallbackPtr)
    {
        GM_MidiEventCallbackPtr cb = song->pSong->midiEventCallbackPtr;
        void *cbRef = song->pSong->midiEventCallbackReference;
        uint32_t t_us = 0;
        if (song->pSong)
            t_us = (uint32_t)song->pSong->songMicroseconds;
        (*cb)(NULL, song->pSong, message, length, t_us, cbRef);
    }
    BAE_ReleaseMutex(song->mLock);

    return theErr;
}

// BAESong_Preroll()
// --------------------------------------
//
//
BAEResult BAESong_Preroll(BAESong song)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        // auto level engaged
        err = GM_PrerollSong(song->pSong, NULL, FALSE, TRUE);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

static void PV_DefaultSongDoneCallback(void *threadContext, GM_Song *pSong, void *reference)
{
    BAESong song;
    BAE_SongCallbackPtr callback = NULL;
    void *callbackReference = NULL;

    song = (BAESong)reference;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (song->mixer)
        {
            if (song->mixer->mID == OBJECT_ID)
            {
#if TRACKING
                if (PV_BAEMixer_ValidateObject(song->mixer, song, BAE_SONG_OBJECT))
#else
                if (song->mValid)
#endif
                {
                    callback = song->mCallback;
                    callbackReference = song->mCallbackReference;
                    song->mInMixer = FALSE;
                }
                else
                {
                    BAE_STDERR("audio:song not in mixer list, no callback\n");
                }
            }
        }
        BAE_ReleaseMutex(song->mLock);
        if (callback)
        {
            (*callback)(callbackReference);
        }
    }
    else
    {
        BAE_STDERR("audio:song no longer valid, no callback\n");
    }
}

// song callbacks
static void PV_BAESong_SetMetaEventCallback(BAESong song, GM_SongMetaCallbackProcPtr pCallback, void *callbackReference)
{
    GM_SetSongMetaEventCallback(song->pSong, pCallback, callbackReference);
}
#ifdef SUPPORT_KARAOKE
static void PV_BAESong_SetLyricCallback(BAESong song, GM_SongLyricCallbackProcPtr pCallback, void *callbackReference)
{
    if (song && song->pSong)
    {
        song->pSong->lyricCallbackPtr = pCallback;
        song->pSong->lyricCallbackReference = callbackReference;
    }
}
#endif
BAEResult BAESong_SetMetaEventCallback(BAESong song, GM_SongMetaCallbackProcPtr pCallback, void *callbackReference)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        PV_BAESong_SetMetaEventCallback(song, pCallback, callbackReference);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}
#ifdef SUPPORT_KARAOKE
BAEResult BAESong_SetLyricCallback(BAESong song, GM_SongLyricCallbackProcPtr pCallback, void *callbackReference)
{
    OPErr err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        PV_BAESong_SetLyricCallback(song, pCallback, callbackReference);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

BAEResult BAESong_ResetLyricState(BAESong song)
{
    OPErr err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (song->pSong)
        {
            GM_ResetSongLyricState(song->pSong);
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}
#endif

static void PV_BAESong_SetCallback(BAESong song, BAE_SongCallbackPtr pCallback, void *callbackReference)
{
    song->mCallback = pCallback;
    song->mCallbackReference = callbackReference;
}

BAEResult BAESong_SetCallback(BAESong song, BAE_SongCallbackPtr pCallback, void *callbackReference)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        PV_BAESong_SetCallback(song, pCallback, callbackReference);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

BAEResult BAESong_GetCallback(BAESong song, BAE_SongCallbackPtr *pResult)
{
    OPErr err;

    err = NO_ERR;
    if (song && pResult)
    {
        BAE_AcquireMutex(song->mLock);
        *pResult = song->mCallback;
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

static void PV_DefaultSongControllerCallback(void *threadContext, struct GM_Song *pSong, void *reference, int16_t channel, int16_t track, int16_t controler, int16_t value)
{
    BAESong song;
    BAE_SongControllerCallbackPtr
        callback = NULL;
    void *callbackReference = NULL;

    song = (BAESong)reference;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (song->mixer)
        {
            if (song->mixer->mID == OBJECT_ID)
            {
#if TRACKING
                if (PV_BAEMixer_ValidateObject(song->mixer, song, BAE_SONG_OBJECT))
#else
                if (song->mValid)
#endif
                {
                    callback = song->mControllerCallback;
                    callbackReference = song->mControllerCallbackReference;
                    song->mInMixer = FALSE;
                }
                else
                {
                    BAE_STDERR("audio:song not in mixer list, no callback\n");
                }
            }
        }
        BAE_ReleaseMutex(song->mLock);
        if (callback)
        {
            (*callback)(song, callbackReference, channel, track, controler, value);
        }
    }
    else
    {
        BAE_STDERR("audio:song no longer valid, no callback\n");
    }
}

static void PV_BAESong_SetControllerCallback(BAESong song, BAE_SongControllerCallbackPtr pCallback, void *callbackReference)
{
    song->mControllerCallback = pCallback;
    song->mControllerCallbackReference = callbackReference;
}

static void PV_BAESong_SetMidiEventCallback(BAESong song, GM_MidiEventCallbackPtr pCallback, void *callbackReference)
{
    if (song && song->pSong)
    {
        song->pSong->midiEventCallbackPtr = pCallback;
        song->pSong->midiEventCallbackReference = callbackReference;
    }
}

BAEResult BAESong_SetMidiEventCallback(BAESong song, GM_MidiEventCallbackPtr pCallback, void *callbackReference)
{
    OPErr err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        PV_BAESong_SetMidiEventCallback(song, pCallback, callbackReference);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// song callbacks
BAEResult BAESong_SetControllerCallback(BAESong song, BAE_SongControllerCallbackPtr pCallback, void *callbackReference, int16_t controller)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        PV_BAESong_SetControllerCallback(song, pCallback, callbackReference);

        GM_SetControllerCallback(song->pSong, song,
                                 PV_DefaultSongControllerCallback, controller);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

BAEResult BAESong_GetControllerCallback(BAESong song, BAE_SongControllerCallbackPtr *pResult)
{
    OPErr err;

    err = NO_ERR;
    if (song && pResult)
    {
        BAE_AcquireMutex(song->mLock);
        *pResult = song->mControllerCallback;
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_Start()
// --------------------------------------
//
//
BAEResult BAESong_Start(BAESong song, int16_t priority)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (song->mixer)
        {
            GM_SetSongPriority(song->pSong, priority);
            GM_SetSongVolume(song->pSong, song->mVolume);
            GM_SetSongRouteBus(song->pSong, song->mRouteBus);

            // auto level engaged
            err = GM_BeginSong(song->pSong, NULL, FALSE, TRUE);
            if (err == NO_ERR)
            {
                song->mInMixer = TRUE;
                GM_SetSongCallback(song->pSong, PV_DefaultSongDoneCallback, (void *)song);
            }
        }
        else
        {
            err = NOT_SETUP;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }

    return BAE_TranslateOPErr(err);
}

static void PV_BAESong_Stop(BAESong song, BAE_BOOL startFade)
{
    if (GM_IsSongPaused(song->pSong))
    {
        GM_ResumeSong(song->pSong);
    }

    if (startFade)
    {
        song->mVolume = GM_GetSongVolume(song->pSong);
        GM_SetSongFadeRate(song->pSong, PV_GetDefaultMixerFadeRate(song->mixer),
                           0, song->mVolume, TRUE);
    }
    else
    {
        GM_KillSongNotes(song->pSong);

        // End the song, and remove it from the mixer
        GM_EndSong(NULL, song->pSong);
        song->mInMixer = FALSE;
    }
}

// BAESong_Stop()
// --------------------------------------
//
//
BAEResult BAESong_Stop(BAESong song, BAE_BOOL startFade)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        PV_BAESong_Stop(song, startFade);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// PV_CalculateTimeDeltaForFade()
// --------------------------------------
//
//
static BAE_FIXED PV_CalculateTimeDeltaForFade(BAE_FIXED sourceVolume, BAE_FIXED destVolume, BAE_FIXED timeInMiliseconds)
{
#if USE_FLOAT
    double delta;
    double source, dest;
    double time;

    source = FIXED_TO_FLOAT(sourceVolume);
    dest = FIXED_TO_FLOAT(destVolume);
    time = FIXED_TO_FLOAT(timeInMiliseconds) * 1000;

    delta = (dest - source) / (time / BAE_GetSliceTimeInMicroseconds());
    return delta;
#else
    BAE_FIXED delta;
    BAE_FIXED source, dest;
    BAE_FIXED time;

    source = (sourceVolume);
    dest = (destVolume);
    time = XFixedMultiply(timeInMiliseconds, LONG_TO_FIXED(1000));

    delta = XFixedDivide((dest - source), XFixedDivide(time, BAE_GetSliceTimeInMicroseconds()));
    return delta;
#endif
}

// BAESong_Fade()
// --------------------------------------
//
//
BAEResult BAESong_Fade(BAESong song, BAE_FIXED sourceVolume, BAE_FIXED destVolume, BAE_FIXED timeInMiliseconds)
{
    int16_t source, dest;
    INT16 minVolume;
    INT16 maxVolume;
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (song->pSong)
        {
#if !USE_FLOAT
            BAE_FIXED delta;
            delta = PV_CalculateTimeDeltaForFade(sourceVolume, destVolume, timeInMiliseconds);
            delta = XFixedMultiply(delta, LONG_TO_FIXED(MAX_SONG_VOLUME));
#else
            double delta;
            delta = PV_CalculateTimeDeltaForFade(sourceVolume, destVolume, timeInMiliseconds);
            delta = delta * -MAX_SONG_VOLUME;
#endif
            source = FIXED_TO_SHORT_ROUNDED(sourceVolume * MAX_SONG_VOLUME);
            dest = FIXED_TO_SHORT_ROUNDED(destVolume * MAX_SONG_VOLUME);
            minVolume = XMIN(source, dest);
            maxVolume = XMAX(source, dest);
#if !USE_FLOAT
            GM_SetSongFadeRate(song->pSong, (delta), minVolume, maxVolume, FALSE);
#else
            GM_SetSongFadeRate(song->pSong, FLOAT_TO_FIXED(delta), minVolume, maxVolume, FALSE);
#endif
        }
        else
        {
            err = NOT_SETUP;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_Pause()
// --------------------------------------
//
//
BAEResult BAESong_Pause(BAESong song)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        GM_PauseSong(song->pSong, TRUE); // pause midi, but don't kill voices
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_Resume()
// --------------------------------------
//
//
BAEResult BAESong_Resume(BAESong song)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        GM_ResumeSong(song->pSong);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_IsPaused()
// --------------------------------------
//
//
BAEResult BAESong_IsPaused(BAESong song, BAE_BOOL *outIsPaused)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outIsPaused)
        {
            *outIsPaused = GM_IsSongPaused(song->pSong);
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_SetLoops()
// --------------------------------------
//
//
BAEResult BAESong_SetLoops(BAESong song, int16_t numLoops)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (numLoops >= 0)
        {
            BAE_PRINTF("Setting song loop count to %d\n", numLoops);
            GM_SetSongLoopMax(song->pSong, numLoops);
            GM_SetSongLoopFlag(song->pSong, (numLoops) ? TRUE : FALSE);
            GM_SetSongMetaLoopFlag(song->pSong, (numLoops) ? TRUE : FALSE);
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_GetLoops()
// --------------------------------------
//
//
BAEResult BAESong_GetLoops(BAESong song, int16_t *outNumLoops)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outNumLoops)
        {
            *outNumLoops = GM_GetSongLoopMax(song->pSong);
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_GetMicrosecondLength()
// --------------------------------------
//
//
BAEResult BAESong_GetMicrosecondLength(BAESong song, uint32_t *outLength)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outLength)
        {
            *outLength = GM_GetSongMicrosecondLength(song->pSong, &err);
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_SetMicrosecondPosition()
// --------------------------------------
//
//
BAEResult BAESong_SetMicrosecondPosition(BAESong song, uint32_t ticks)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (song->pSong) // MOVE THIS CHECK INTO THE ENGINE
        {
            err = GM_SetSongMicrosecondPosition(song->pSong, ticks);
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_GetMicrosecondPosition()
// --------------------------------------
//
//
BAEResult BAESong_GetMicrosecondPosition(BAESong song, uint32_t *outTicks)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outTicks)
        {
            *outTicks = GM_SongMicroseconds(song->pSong);
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_IsDone()
// --------------------------------------
//
//
BAEResult BAESong_IsDone(BAESong song, BAE_BOOL *outIsDone)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outIsDone)
        {
            *outIsDone = (BAE_BOOL)GM_IsSongDone(song->pSong);
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_AreMidiEventsPending()
// --------------------------------------
// returns TRUE if there are midi events pending
//
BAEResult BAESong_AreMidiEventsPending(BAESong song, BAE_BOOL *outPending)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outPending)
        {
            *outPending = FALSE;
            if (song->pSong)
            {
                // first check to see if there are any events posted into the queue
                *outPending = (BAE_BOOL)GM_AreEventsPending(song->pSong);
                if (*outPending == FALSE)
                {
                    // none there, so see if this is a midi file playing via the sequencer
                    *outPending = ((BAE_BOOL)GM_IsSongDone(song->pSong) ? FALSE : TRUE);
                }
            }
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_SetMasterTempo()
// --------------------------------------
//
//
BAEResult BAESong_SetMasterTempo(BAESong song, BAE_UNSIGNED_FIXED tempoFactor)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        GM_SetMasterSongTempo(song->pSong, tempoFactor);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_GetMasterTempo()
// --------------------------------------
//
//
BAEResult BAESong_GetMasterTempo(BAESong song, BAE_UNSIGNED_FIXED *outTempoFactor)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outTempoFactor)
        {
            *outTempoFactor = GM_GetMasterSongTempo(song->pSong);
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_MuteTrack()
// --------------------------------------
//
//
BAEResult BAESong_MuteTrack(BAESong song, uint16_t track)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        GM_MuteTrack(song->pSong, track);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_UnmuteTrack()
// --------------------------------------
//
//
BAEResult BAESong_UnmuteTrack(BAESong song, uint16_t track)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        GM_UnmuteTrack(song->pSong, track);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_GetTrackMuteStatus()
// --------------------------------------
//
//
BAEResult BAESong_GetTrackMuteStatus(BAESong song, BAE_BOOL *outTracks)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outTracks)
        {
            GM_GetTrackMuteStatus(song->pSong, outTracks);
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_SoloTrack()
// --------------------------------------
//
//
BAEResult BAESong_SoloTrack(BAESong song, uint16_t track)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        GM_SoloTrack(song->pSong, track);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_UnSoloTrack()
// --------------------------------------
//
//
BAEResult BAESong_UnSoloTrack(BAESong song, uint16_t track)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        GM_UnsoloTrack(song->pSong, track);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

//  BAESong_GetSoloTrackStatus()
// --------------------------------------
//
//
BAEResult BAESong_GetSoloTrackStatus(BAESong song, BAE_BOOL *outTracks)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outTracks)
        {
            GM_GetTrackSoloStatus(song->pSong, outTracks);
        }
        else
        {
            err = PARAM_ERR;
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// ------------------------------------------------------------------
// Utility Functions
// ------------------------------------------------------------------
#if 0
#pragma mark -
#pragma mark>>>>> Utility Functions <<<<<
#endif

#if USE_FULL_RMF_SUPPORT == TRUE
// PV_TranslateInfoType()
// --------------------------------------
//
//
static SongInfo PV_TranslateInfoType(BAEInfoType infoType)
{
    SongInfo info;

    switch (infoType)
    {
    default:
        info = I_INVALID;
        break;
    case TITLE_INFO:
        info = I_TITLE;
        break;
    case PERFORMED_BY_INFO:
        info = I_PERFORMED_BY;
        break;
    case COMPOSER_INFO:
        info = I_COMPOSER;
        break;
    case COPYRIGHT_INFO:
        info = I_COPYRIGHT;
        break;
    case PUBLISHER_CONTACT_INFO:
        info = I_PUBLISHER_CONTACT;
        break;
    case USE_OF_LICENSE_INFO:
        info = I_USE_OF_LICENSE;
        break;
    case LICENSE_TERM_INFO:
        info = I_LICENSE_TERM;
        break;
    case LICENSED_TO_URL_INFO:
        info = I_LICENSED_TO_URL;
        break;
    case EXPIRATION_DATE_INFO:
        info = I_EXPIRATION_DATE;
        break;
    case COMPOSER_NOTES_INFO:
        info = I_COMPOSER_NOTES;
        break;
    case INDEX_NUMBER_INFO:
        info = I_INDEX_NUMBER;
        break;
    case GENRE_INFO:
        info = I_GENRE;
        break;
    case SUB_GENRE_INFO:
        info = I_SUB_GENRE;
        break;
    case TEMPO_DESCRIPTION_INFO:
        info = I_TEMPO;
        break;
    case ORIGINAL_SOURCE_INFO:
        info = I_ORIGINAL_SOURCE;
        break;
    }
    return info;
}
#endif // #if USE_FULL_RMF_SUPPORT == TRUE

// TranslateBankProgramToInstrument()
// --------------------------------------
//
//
BAE_INSTRUMENT TranslateBankProgramToInstrument(uint16_t bank,
                                                uint16_t program,
                                                uint16_t channel,
                                                uint16_t note)
{
    BAE_INSTRUMENT instrument;

    instrument = program;
    if (channel == PERCUSSION_CHANNEL)
    {
        bank = (bank * 2) + 1; // odd banks are percussion
    }
    else
    {
        bank = bank * 2 + 0; // even banks are for instruments
        note = 0;
    }

    if (bank < MAX_BANKS)
    {
        instrument = (bank * 128) + program + note;
    }

    return instrument;
}

// PV_GetRmfSongResource()
// --------------------------------------
//
//
#if USE_FULL_RMF_SUPPORT == TRUE
static OPErr PV_GetRmfSongResource(void *pRMFData, uint32_t rmfSize, int16_t index,
                                   SongResource **ppOutResource, int32_t *pOutResourceSize)
{
    XFILE fileRef;
    XLongResourceID theID;
    OPErr err;

    err = NO_ERR;
    if (pRMFData && rmfSize && ppOutResource && pOutResourceSize)
    {
        fileRef = XFileOpenResourceFromMemory((XPTR)pRMFData, rmfSize, FALSE);
        if (fileRef)
        {
            *ppOutResource = (SongResource *)XGetIndexedResource(ID_SONG, &theID, index, NULL, pOutResourceSize);
            if (*ppOutResource)
            {
                err = NO_ERR;
            }
            else
            {
                err = PARAM_ERR;
            }
            XFileClose(fileRef);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = PARAM_ERR;
    }
    return err;
}
#endif // #if USE_FULL_RMF_SUPPORT == TRUE

// BAEUtil_GetInfoSizeFromFile()
// --------------------------------------
// If the file at filePath contains a song with index
// songIndex, and that song includes a text info field of type infoType,
// then returns the size in bytes of that text info field.
//
// Returns: Text info field size in bytes
//
BAEResult BAEUtil_GetInfoSizeFromFile(BAEPathName filePath,
                                      int16_t songIndex,
                                      BAEInfoType infoType,
                                      uint32_t *pOutResourceSize)
{
#if USE_FULL_RMF_SUPPORT == TRUE
    SongInfo info;
    BAEResult theErr;
    SongResource *pSongRes;
    int32_t songResSize = 0;

    theErr = BAE_NO_ERROR;
    info = PV_TranslateInfoType(infoType);

    if (pOutResourceSize && (info != I_INVALID))
    {
        XFILE fileRef;
        XFILENAME name;
        XLongResourceID theID;

        theErr = BAE_NO_ERROR;
        *pOutResourceSize = 0;
        XConvertPathToXFILENAME(filePath, &name);
        fileRef = XFileOpenResource(&name, TRUE);
        if (fileRef)
        {
            pSongRes = (SongResource *)XGetIndexedFileResource(fileRef, ID_SONG, &theID, songIndex, NULL, &songResSize);
            if (pSongRes)
            {
                *pOutResourceSize = XGetSongInformationSize(pSongRes, songResSize, info);
            }
            XDisposePtr((XPTR)pSongRes);
            XFileClose(fileRef);
        }
    }
    else
    {
        theErr = BAE_PARAM_ERR;
    }
    return theErr;
#else
    infoType;
    return BAE_NOT_SETUP;
#endif
}

// BAEUtil_GetRmfSongInfoFromFile()
// --------------------------------------
//
//
BAEResult BAEUtil_GetRmfSongInfoFromFile(BAEPathName filePath, int16_t songIndex,
                                         BAEInfoType infoType, char *targetBuffer, uint32_t bufferBytes)
{
#if USE_FULL_RMF_SUPPORT == TRUE
    SongInfo info;
    BAEResult theErr;
    SongResource *pSongRes;
    int32_t songResSize;

    theErr = BAE_NO_ERROR;
    targetBuffer[0] = 0;
    info = PV_TranslateInfoType(infoType);

    if (info != I_INVALID)
    {
        XFILE fileRef;
        XFILENAME name;
        XLongResourceID theID;

        theErr = BAE_NO_ERROR;
        XConvertPathToXFILENAME(filePath, &name);
        fileRef = XFileOpenResource(&name, TRUE);
        if (fileRef)
        {
            pSongRes = (SongResource *)XGetIndexedFileResource(fileRef, ID_SONG, &theID, songIndex, NULL, &songResSize);
            if (pSongRes)
            {
                XGetSongInformation(pSongRes, songResSize, info, targetBuffer, bufferBytes);

#if (X_PLATFORM != X_MACINTOSH_9)
                // data stored in the copyright fields is Mac ASCII, any other platform should translate
                while (*targetBuffer)
                {
                    *targetBuffer = XTranslateMacToWin(*targetBuffer);
                    targetBuffer++;
                }
#endif
            }
            XDisposePtr((XPTR)pSongRes);
            XFileClose(fileRef);
        }
    }
    else
    {
        theErr = BAE_PARAM_ERR;
    }
    return theErr;
#else
    infoType;
    targetBuffer[0] = 0;
    return BAE_NOT_SETUP;
#endif
}

#if USE_SF2_SUPPORT == TRUE
// BAEUtil_GetRmfInstrumentList()
// --------------------------------------
//
// Gets the list of instruments used by the RMF
BAEResult BAEUtil_GetRmfInstrumentList(void *pRMFData, uint32_t rmfSize, int16_t songIndex,
                                       uint32_t *pOutInstruments, uint32_t maxInstruments,
                                       uint32_t *pOutNumInstruments)
{
    (void)songIndex; // current implementation ignores song filtering; could refine later
    if (!pRMFData || rmfSize < 12 || !pOutNumInstruments) return BAE_PARAM_ERR;

    XFILE fileRef = (XFILE)pRMFData; // caller must pass actual XFILE (not &fileRef)
    char *rmfData = (char *)XNewPtr(rmfSize);
    if (!rmfData) return BAE_MEMORY_ERR;
    XFileSetPosition(fileRef, 0);

    if (XFileRead(fileRef, rmfData, (int32_t)rmfSize) != 0) {
        XDisposePtr(rmfData);
        return BAE_FILE_IO_ERROR;
    }
    if (memcmp(rmfData, "IREZ", 4) != 0) {
        XDisposePtr(rmfData);
        return BAE_PARAM_ERR;
    }

    const uint8_t headerMaxLen = 24;
    uint32_t numResources = (rmfData[8] << 24) | (rmfData[9] << 16) | (rmfData[10] << 8) | rmfData[11];
    uint32_t instrumentCount = 0;
    size_t offset = 12;

    // big-endian 32-bit reader (avoid sign extension)
#define READ_BE32(p) (((uint32_t)(uint8_t)(p)[0] << 24) | ((uint32_t)(uint8_t)(p)[1] << 16) | ((uint32_t)(uint8_t)(p)[2] << 8) | (uint32_t)(uint8_t)(p)[3])
    for (uint32_t i = 0; i < numResources && offset + 13 <= rmfSize; i++) {
        if (offset + 13 > rmfSize) break;
        uint32_t nextOffset = READ_BE32(&rmfData[offset]);
        char type[5]; memcpy(type, &rmfData[offset + 4], 4); type[4] = '\0';
        uint32_t id = READ_BE32(&rmfData[offset + 8]);
        uint8_t nameLen = (uint8_t)rmfData[offset + 12];
        size_t bodyLenOffset = offset + 13 + nameLen;
        if (nameLen >= headerMaxLen || bodyLenOffset + 4 > rmfSize) break;
        uint32_t bodyLen = READ_BE32(&rmfData[bodyLenOffset]); (void)bodyLen;
        if (type[0]=='I' && type[1]=='N' && type[2]=='S' && type[3]=='T') {
            if (pOutInstruments && instrumentCount < maxInstruments) pOutInstruments[instrumentCount] = id;
            instrumentCount++;
        }
        if (nextOffset == 0xFFFFFFFF) break;
        if (nextOffset <= offset || nextOffset > rmfSize) break;
        offset = nextOffset;
    }
#undef READ_BE32

    *pOutNumInstruments = instrumentCount; // total discovered (may exceed maxInstruments)
    XDisposePtr(rmfData);
    return BAE_NO_ERROR;
}
#endif

// BAEUtil_GetRmfSongInfo()
// --------------------------------------
//
//
BAEResult BAEUtil_GetRmfSongInfo(void *pRMFData, uint32_t rmfSize, int16_t songIndex,
                                 BAEInfoType infoType, char *targetBuffer, uint32_t bufferBytes)
{
#if USE_FULL_RMF_SUPPORT == TRUE
    SongInfo info;
    BAEResult theErr;
    SongResource *pSongRes;
    int32_t songResSize;

    theErr = BAE_NO_ERROR;
    targetBuffer[0] = 0;
    info = PV_TranslateInfoType(infoType);

    if (info != I_INVALID)
    {
        if (PV_GetRmfSongResource(pRMFData, rmfSize, songIndex, &pSongRes, &songResSize) == NO_ERR)
        {
            XGetSongInformation(pSongRes, songResSize, info, targetBuffer, bufferBytes);

#if (X_PLATFORM != X_MACINTOSH_9)
            // data stored in the copyright fields is Mac ASCII, any other platform should translate
            while (*targetBuffer)
            {
                *targetBuffer = XTranslateMacToWin(*targetBuffer);
                targetBuffer++;
            }
#endif
            XDisposePtr((XPTR)pSongRes);
        }
    }
    else
    {
        theErr = BAE_PARAM_ERR;
    }
    return theErr;
#else
    infoType;
    targetBuffer[0] = 0;
    return BAE_NOT_SETUP;
#endif
}

// BAEUtil_GetInfoSize()
// --------------------------------------
//
//
uint32_t BAEUtil_GetInfoSize(void *pRMFData, uint32_t rmfSize, int16_t songIndex, BAEInfoType infoType)
{
#if USE_FULL_RMF_SUPPORT == TRUE
    SongInfo info;
    uint32_t size;
    SongResource *pSongRes;
    int32_t songResSize;

    size = 0;
    info = PV_TranslateInfoType(infoType);
    if (info != I_INVALID)
    {
        if (PV_GetRmfSongResource(pRMFData, rmfSize, songIndex, &pSongRes, &songResSize) == NO_ERR)
        {
            size = XGetSongInformationSize(pSongRes, songResSize, info);
            XDisposePtr((XPTR)pSongRes);
        }
    }
    return size;
#else
    infoType;
    return 0;
#endif
}

#if USE_FULL_RMF_SUPPORT == TRUE
// BAEUtil_IsRmfSongEncrypted()
// --------------------------------------
//
//
BAE_BOOL BAEUtil_IsRmfSongEncrypted(void *pRMFData, uint32_t rmfSize, int16_t songIndex)
{
    SongResource *pSongRes;
    int32_t songResSize;
    BAE_BOOL locked;

    pSongRes = NULL;
    songResSize = 0;
    locked = FALSE;

    if (PV_GetRmfSongResource(pRMFData, rmfSize, songIndex, &pSongRes, &songResSize) == NO_ERR)
    {
        locked = (BAE_BOOL)XIsSongLocked(pSongRes);
        XDisposePtr((XPTR)pSongRes);
    }
    return locked;
}

// BAEUtil_IsRmfSongCompressed()
// --------------------------------------
//
//
BAE_BOOL BAEUtil_IsRmfSongCompressed(void *pRMFData, uint32_t rmfSize, int16_t songIndex)
{
    SongResource *pSongRes;
    int32_t songResSize;
    BAE_BOOL compressed;

    pSongRes = NULL;
    songResSize = 0;
    compressed = FALSE;

    if (PV_GetRmfSongResource(pRMFData, rmfSize, songIndex, &pSongRes, &songResSize) == NO_ERR)
    {
        compressed = (BAE_BOOL)XIsSongCompressed(pSongRes);
        XDisposePtr((XPTR)pSongRes);
    }
    return compressed;
}
#endif // #if USE_FULL_RMF_SUPPORT == TRUE

#if USE_FULL_RMF_SUPPORT == TRUE
// BAEUtil_GetRmfVersion()
// --------------------------------------
//
//
BAEResult BAEUtil_GetRmfVersion(void *pRMFData, uint32_t rmfSize,
                                int16_t *pVersionMajor, int16_t *pVersionMinor, int16_t *pVersionSubMinor)
{
    XFILE fileRef;
    OPErr err;
    XVersion vers;

    err = NO_ERR;
    vers.versionMajor = 0;
    vers.versionMinor = 0;
    vers.versionSubMinor = 0;

    if (pRMFData && rmfSize && pVersionMajor && pVersionMinor && pVersionSubMinor)
    {
        fileRef = XFileOpenResourceFromMemory((XPTR)pRMFData, rmfSize, FALSE);
        if (fileRef)
        {
            XGetVersionNumber(&vers);
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = PARAM_ERR;
    }
    *pVersionMajor = vers.versionMajor;
    *pVersionMinor = vers.versionMinor;
    *pVersionSubMinor = vers.versionMinor;
    return BAE_TranslateOPErr(err);
}
#endif // #if USE_FULL_RMF_SUPPORT == TRUE

// PV_TranslateTerpModeToBAETerpMode()
// -------------------------------------------
//
//
static BAETerpMode PV_TranslateTerpModeToBAETerpMode(TerpMode mode_in)
{
    BAETerpMode mode_out = BAE_LINEAR_INTERPOLATION;

    switch (mode_in)
    {
    case E_AMP_SCALED_DROP_SAMPLE:
        mode_out = BAE_DROP_SAMPLE;
        break;
    case E_2_POINT_INTERPOLATION:
        mode_out = BAE_2_POINT_INTERPOLATION;
        break;
    case E_LINEAR_INTERPOLATION:
    case E_LINEAR_INTERPOLATION_FLOAT:
    case E_LINEAR_INTERPOLATION_U3232:
        mode_out = BAE_LINEAR_INTERPOLATION;
        break;
    default:
        BAE_ASSERT(FALSE);
    }
    return mode_out;
}

char mCopyright[] =
    {
        "\
(c) Copyright 1996-2001 Beatnik, Inc, All Rights Reserved\r\
Beatnik products contain certain trade secrets and confidential and \
proprietary information of Beatnik.  Use, reproduction, disclosure \
and distribution by any means are prohibited, except pursuant to \
a written license from Beatnik. Use of copyright notice is \
precautionary and does not imply publication or disclosure.\
\r\
Restricted Rights Legend:\r\
Use, duplication, or disclosure by the Government is subject to \
restrictions as set forth in subparagraph (c)(1)(ii) of The \
Rights in Technical Data and Computer Software clause in DFARS \
252.227-7013 or subparagraphs (c)(1) and (2) of the Commercial \
Computer Software--Restricted Rights at 48 CFR 52.227-19, as \
applicable."};

char mAboutNames[] =
    {
        "\
Audio Engine Programming: \
Steve Hales, \
Mark Deggeller, \
Doug Scott, \
Andrew Rostaing, \
John Cooper, \
Jim Nitchals, \
Chris Rogers, \
Chris Schardt \
QA: \
Elizabeth Smolgovsky, \
Kari Reynolds, \
Chris Muir, \
Chris van Rensburg, \
Chris Ticknor, \
Sean Echevarria, \
Tim Maroney, \
Sal Orlando \
Music: \
Brian Salter \
Documentation: \
Chris Grigg \
In memory of Jim Nitchals, 1962-1998.  A subtle genius and original thinker."};

// EOF MiniBAE.c

#if USE_MPEG_ENCODER == TRUE
// Translate BAECompressionType (per-channel kbps enum naming) to bits/sec per channel.
uint32_t BAE_TranslateMPEGTypeToBitrate(BAECompressionType ct)
{
    switch (ct)
    {
    case BAE_COMPRESSION_MPEG_16:
        return 16000;
    case BAE_COMPRESSION_MPEG_24:
        return 24000;
    case BAE_COMPRESSION_MPEG_32:
        return 32000;
    case BAE_COMPRESSION_MPEG_40:
        return 40000;
    case BAE_COMPRESSION_MPEG_48:
        return 48000;
    case BAE_COMPRESSION_MPEG_56:
        return 56000;
    case BAE_COMPRESSION_MPEG_64:
        return 64000;
    case BAE_COMPRESSION_MPEG_80:
        return 80000;
    case BAE_COMPRESSION_MPEG_96:
        return 96000;
    case BAE_COMPRESSION_MPEG_112:
        return 112000;
    case BAE_COMPRESSION_MPEG_128:
        return 128000;
    case BAE_COMPRESSION_MPEG_160:
        return 160000;
    case BAE_COMPRESSION_MPEG_192:
        return 192000;
    case BAE_COMPRESSION_MPEG_224:
        return 224000;
    case BAE_COMPRESSION_MPEG_256:
        return 256000;
    case BAE_COMPRESSION_MPEG_320:
        return 320000;
    default:
        return 128000; // safe default
    }
}
#endif

#if USE_VORBIS_ENCODER == TRUE
// Map Vorbis compression enum to libvorbis quality parameter (approximate)
float BAE_TranslateVorbisTypeToQuality(BAECompressionType ct)
{
    switch (ct)
    {
    case BAE_COMPRESSION_VORBIS_96:
        return 0.1f; // low quality
    case BAE_COMPRESSION_VORBIS_128:
        return 0.3f;
    case BAE_COMPRESSION_VORBIS_256:
        return 0.7f;
    case BAE_COMPRESSION_VORBIS_320:
        return 0.95f; // near transparent
    default:
        return 0.4f; // safe default
    }
}
#endif

#if USE_MPEG_ENCODER == TRUE
// Refill callback: build next mixer slice of PCM into provided buffer.
XBOOL PV_RefillMPEGEncodeBuffer(void *buffer, void *userRef)
{
    if (!buffer || !mWritingDataBlock || !mWritingDataBlockSize)
        return FALSE;
    BAEMixer mixer = (BAEMixer)userRef;
    BAEAudioModifiers mods;
    if (BAEMixer_GetModifiers(mixer, &mods) != BAE_NO_ERROR)
    {
        mods = BAE_USE_16;
    }
    int channels = (mods & BAE_USE_STEREO) ? 2 : 1;
    int sampleSize = (mods & BAE_USE_16) ? 2 : 1;
    uint32_t frames = (uint32_t)(mWritingDataBlockSize / (sampleSize * channels));
    // Build directly into destination buffer so encoder reads fresh PCM
    BAE_BuildMixerSlice(mixer, buffer, mWritingDataBlockSize, frames);
    return TRUE;
}
#endif

// -----------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------
// Universal file loader
// -----------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------

// BAEMixer_LoadFromFile()
// --------------------------------------
// Universal file loader that automatically detects file type and loads the appropriate
// BAESong or BAESound object based on file content
BAEResult BAEMixer_LoadFromFile(BAEMixer mixer, BAEPathName filePath, BAELoadResult *result)
{
    if (!mixer || !filePath || !result)
        return BAE_PARAM_ERR;

    // Initialize result structure
    result->type = BAE_LOAD_TYPE_NONE;
    result->result = BAE_NO_ERROR;
    result->fileType = BAE_INVALID_TYPE;
    result->data.song = NULL;
    result->data.sound = NULL;

    // Detect file type using existing file type detection
    BAEFileType ftype = X_DetermineFileType(filePath);
    result->fileType = ftype;
    
    if (ftype == BAE_INVALID_TYPE)
    {
        result->result = BAE_BAD_FILE;
        return BAE_BAD_FILE;
    }

    // Determine if this is an audio file or a song file
    BAE_BOOL isAudio = FALSE;
    if (ftype == BAE_WAVE_TYPE || ftype == BAE_AIFF_TYPE || ftype == BAE_AU_TYPE
#if USE_MPEG_DECODER == TRUE
        || ftype == BAE_MPEG_TYPE
#endif
#if USE_FLAC_DECODER == TRUE
        || ftype == BAE_FLAC_TYPE
#endif
#if USE_VORBIS_DECODER == TRUE
        || ftype == BAE_VORBIS_TYPE
#endif
    )
    {
        isAudio = TRUE;
    }

    if (isAudio)
    {
        // Load as audio file using BAESound
        result->data.sound = BAESound_New(mixer);
        if (!result->data.sound)
        {
            result->result = BAE_MEMORY_ERR;
            return BAE_MEMORY_ERR;
        }

        BAEResult sr = BAESound_LoadFileSample(result->data.sound, filePath, ftype);
        if (sr != BAE_NO_ERROR)
        {
            BAESound_Delete(result->data.sound);
            result->data.sound = NULL;
            result->result = sr;
            return sr;
        }

        result->type = BAE_LOAD_TYPE_SOUND;
        result->result = BAE_NO_ERROR;
        return BAE_NO_ERROR;
    }
    else
    {
        // Load as song file using BAESong
        result->data.song = BAESong_New(mixer);
        if (!result->data.song)
        {
            result->result = BAE_MEMORY_ERR;
            return BAE_MEMORY_ERR;
        }

        BAEResult sr = BAE_NO_ERROR;

        if (ftype == BAE_RMF)
        {
            sr = BAESong_LoadRmfFromFile(result->data.song, filePath, 0, TRUE);
        }
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUJE
        else if (ftype == BAE_RMI)
        {
            sr = BAESong_LoadRmiFromFile(result->data.song, filePath, 0, TRUE);
        }
#endif                
#if USE_XMF_SUPPORT == TRUE
        else if (ftype == BAE_XMF)
        {
            sr = BAESong_LoadXmfFromFile(result->data.song, filePath, TRUE);
        }
#endif
        else if (ftype == BAE_MIDI_TYPE || ftype == BAE_RMI)
        {
            sr = BAESong_LoadMidiFromFile(result->data.song, filePath, TRUE);
        }
        else
        {
            // Default to standard MIDI for any remaining cases
            sr = BAESong_LoadMidiFromFile(result->data.song, filePath, TRUE);
        }

        if (sr != BAE_NO_ERROR)
        {
            BAESong_Delete(result->data.song);
            result->data.song = NULL;
            result->result = sr;
            return sr;
        }

        result->type = BAE_LOAD_TYPE_SONG;
        result->result = BAE_NO_ERROR;
        return BAE_NO_ERROR;
    }
}

// BAELoadResult_Cleanup()
// --------------------------------------
// Cleans up resources allocated by BAEMixer_LoadFromFile
BAEResult BAELoadResult_Cleanup(BAELoadResult *result)
{
    if (!result)
        return BAE_PARAM_ERR;

    if (result->data.song)
    {
        BAESong_Delete(result->data.song);
        result->data.song = NULL;
    }
    else if (result->data.sound)
    {
        BAESound_Delete(result->data.sound);
        result->data.sound = NULL;
    }

    result->type = BAE_LOAD_TYPE_NONE;
    result->result = BAE_NO_ERROR;

    return BAE_NO_ERROR;
}

// BAEMixer_LoadFromMemory()
// --------------------------------------
// Universal memory loader that automatically detects file type and loads the appropriate
// BAESong or BAESound object based on file content
BAEResult BAEMixer_LoadFromMemory(BAEMixer mixer, void const *pData, uint32_t dataSize, BAELoadResult *result)
{
    if (!mixer || !pData || dataSize == 0 || !result)
        return BAE_PARAM_ERR;

    // Initialize result structure
    result->type = BAE_LOAD_TYPE_NONE;
    result->result = BAE_NO_ERROR;
    result->fileType = BAE_INVALID_TYPE;
    result->data.song = NULL;
    result->data.sound = NULL;

    // Detect file type from content
    int32_t probeSize = (int32_t)dataSize;
    if (probeSize > 64) probeSize = 64; // Only need first 64 bytes for detection
    
    BAEFileType ftype = X_DetermineFileTypeByData((const unsigned char *)pData, probeSize);
    result->fileType = ftype;
    
    BAE_PRINTF("[BAEMixer_LoadFromMemory] Detected file type: %s\n", X_GetFileTypeString(ftype));
    
    if (ftype == BAE_INVALID_TYPE)
    {
        result->result = BAE_BAD_FILE;
        return BAE_BAD_FILE;
    }

    // Determine if this is an audio file or a song file
    BAE_BOOL isAudio = FALSE;
    if (ftype == BAE_WAVE_TYPE || ftype == BAE_AIFF_TYPE || ftype == BAE_AU_TYPE
#if USE_MPEG_DECODER == TRUE
        || ftype == BAE_MPEG_TYPE
#endif
#if USE_FLAC_DECODER == TRUE
        || ftype == BAE_FLAC_TYPE
#endif
#if USE_VORBIS_DECODER == TRUE
        || ftype == BAE_VORBIS_TYPE
#endif
    )
    {
        isAudio = TRUE;
    }

    if (isAudio)
    {
        // Load as audio file using BAESound
        result->data.sound = BAESound_New(mixer);
        if (!result->data.sound)
        {
            result->result = BAE_MEMORY_ERR;
            return BAE_MEMORY_ERR;
        }

        BAEResult sr = BAESound_LoadMemorySample(result->data.sound, (void *)pData, dataSize, ftype);
        if (sr != BAE_NO_ERROR)
        {
            BAESound_Delete(result->data.sound);
            result->data.sound = NULL;
            result->result = sr;
            return sr;
        }

        result->type = BAE_LOAD_TYPE_SOUND;
        result->result = BAE_NO_ERROR;
        return BAE_NO_ERROR;
    }
    else
    {
        // Load as song file using BAESong
        result->data.song = BAESong_New(mixer);
        if (!result->data.song)
        {
            result->result = BAE_MEMORY_ERR;
            return BAE_MEMORY_ERR;
        }

        BAEResult sr = BAE_NO_ERROR;

        if (ftype == BAE_RMF)
        {
            sr = BAESong_LoadRmfFromMemory(result->data.song, pData, dataSize, 0, TRUE);
        }
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
        else if (ftype == BAE_RMI)
        {
            sr = BAESong_LoadRmiFromMemory(result->data.song, pData, dataSize, TRUE, TRUE);
        }
#if USE_XMF_SUPPORT == TRUE
        else if (ftype == BAE_XMF)
        {
            sr = BAESong_LoadXmfFromMemory(result->data.song, pData, dataSize, TRUE);
        }
#endif
#endif
        else if (ftype == BAE_MIDI_TYPE || ftype == BAE_RMI)
        {
            sr = BAESong_LoadMidiFromMemory(result->data.song, pData, dataSize, TRUE);
        }
        else
        {
            // Default to standard MIDI for any remaining cases
            sr = BAESong_LoadMidiFromMemory(result->data.song, pData, dataSize, TRUE);
        }

        if (sr != BAE_NO_ERROR)
        {
            BAESong_Delete(result->data.song);
            result->data.song = NULL;
            result->result = sr;
            return sr;
        }

        result->type = BAE_LOAD_TYPE_SONG;
        result->result = BAE_NO_ERROR;
        return BAE_NO_ERROR;
    }
}
