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
/*
    Additional modifications © 2021-2026 zefie
    Licensed under the GNU Lesser General Public License v3.0 or later.
*/
/*****************************************************************************/
/*
** "NeoBAE.c"
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

#include "NeoBAE.h"
#if USE_MPEG_ENCODER == TRUE
#include "XMPEG_BAE_API.h" /* for MPG_Encode* encoder API prototypes */
#endif
#include "X_API.h"
#include "GenSnd.h"
#include "GenPriv.h"
#include "GenBankBalance.h"
#if USE_NATIVE_DLS == TRUE
#include "GenDLS_MobileBAE.h"
#endif
#include "GenRMI.h"
#include "X_Formats.h"
#include "BAE_API.h"
#include "BAE_Override.h"
#include "X_Assert.h"
#include "GenRingtone.h"
#if USE_MTHC_SUPPORT == TRUE
#include "../../mthc/mthc_decomp.h"
#endif
#include "../../adp2wav/adp2wav_decode.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include "bankinfo.h" // embedded bank metadata (hash -> friendly)

#if defined(_MSC_VER)
    #include <direct.h>
#else
    #include <sys/stat.h>
    #include <sys/types.h>
#endif
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDLITE == TRUE
#include "GenSF2_FluidLite.h"
#if USE_XMF_SUPPORT == TRUE
#include "GenXMF.h"
#endif
#endif

#if USE_VORBIS_DECODER == TRUE || USE_VORBIS_ENCODER == TRUE
#include "vorbis/codec.h"
#endif

#if USE_OPUS_DECODER == TRUE || USE_OPUS_ENCODER == TRUE
#include <opus.h>
#endif

#if USE_MPEG_ENCODER == TRUE
#include <lame.h>
#endif

#ifndef BAE_DISABLE_ROLLED_MIDI_DETECTION
#define BAE_DISABLE_ROLLED_MIDI_DETECTION FALSE
#endif

#if BAE_DISABLE_ROLLED_MIDI_DETECTION == TRUE
#undef BAE_DISABLE_ROLLED_MIDI_DETECTION
#define BAE_DISABLE_ROLLED_MIDI_DETECTION FALSE
#endif

static uint16_t PV_ReadBigEndianU16(const unsigned char *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static uint32_t PV_ReadBigEndianU32(const unsigned char *data)
{
    return ((uint32_t)data[0] << 24)
         | ((uint32_t)data[1] << 16)
         | ((uint32_t)data[2] << 8)
         | (uint32_t)data[3];
}

static BAE_BOOL PV_ReadMidiVariableLength(const unsigned char **ppData,
                                          const unsigned char *end,
                                          uint32_t *outValue)
{
    const unsigned char *data;
    uint32_t value;
    int count;

    if ((ppData == NULL) || (*ppData == NULL) || (outValue == NULL))
    {
        return FALSE;
    }

    data = *ppData;
    value = 0;

    for (count = 0; count < 4; count++)
    {
        unsigned char byteValue;

        if (data >= end)
        {
            return FALSE;
        }
        byteValue = *data++;
        value = (value << 7) | (uint32_t)(byteValue & 0x7F);
        if ((byteValue & 0x80) == 0)
        {
            *ppData = data;
            *outValue = value;
            return TRUE;
        }
    }
    return FALSE;
}

static const unsigned char *PV_FindMThdStart(const unsigned char *data,
                                             uint32_t size,
                                             uint32_t *outRemainingSize)
{
    uint32_t i;

    if ((data == NULL) || (size < 14))
    {
        return NULL;
    }
    for (i = 0; i + 4 <= size; i++)
    {
        if ((data[i] == 'M') && (data[i + 1] == 'T') && (data[i + 2] == 'h') && (data[i + 3] == 'd'))
        {
            if (outRemainingSize)
            {
                *outRemainingSize = size - i;
            }
            return data + i;
        }
    }
    return NULL;
}

static BAE_BOOL PV_IsRolledMIDIBytes(const unsigned char *rawData, uint32_t rawSize)
{
#if 1
    const unsigned char *data;
    const unsigned char *end;
    uint32_t size;
    uint32_t headerLength;
    uint16_t trackCount;
    uint16_t trackIndex;
    uint32_t muteEvents;
    uint32_t muteSetEvents;
    uint32_t muteClearEvents;
    uint32_t loopEvents;
    uint32_t distinctMuteValues;
    unsigned char seenMuteValue[128];
    uint16_t tracksWithMute;
    uint16_t tracksWithNotes;
    uint32_t minNoteEnd;
    uint32_t maxNoteEnd;

    data = PV_FindMThdStart(rawData, rawSize, &size);
    if ((data == NULL) || (size < 14))
    {
        return FALSE;
    }

    end = data + size;
    if (memcmp(data, "MThd", 4) != 0)
    {
        return FALSE;
    }

    headerLength = PV_ReadBigEndianU32(data + 4);
    if ((headerLength < 6) || ((size_t)(end - data) < (size_t)(8 + headerLength)))
    {
        return FALSE;
    }

    trackCount = PV_ReadBigEndianU16(data + 10);
    data += 8 + headerLength;

    muteEvents = 0;
    muteSetEvents = 0;
    muteClearEvents = 0;
    loopEvents = 0;
    distinctMuteValues = 0;
    XSetMemory(seenMuteValue, sizeof(seenMuteValue), 0);
    tracksWithMute = 0;
    tracksWithNotes = 0;
    minNoteEnd = 0xFFFFFFFFUL;
    maxNoteEnd = 0;

    for (trackIndex = 0; trackIndex < trackCount; trackIndex++)
    {
        const unsigned char *trackData;
        const unsigned char *trackEnd;
        unsigned char runningStatus;
        uint32_t absTick;
        uint32_t noteEndTick;
        BAE_BOOL trackHasMute;
        BAE_BOOL trackHasNotes;

        if ((size_t)(end - data) < 8)
        {
            return FALSE;
        }
        if (memcmp(data, "MTrk", 4) != 0)
        {
            return FALSE;
        }

        trackData = data + 8;
        trackEnd = trackData + PV_ReadBigEndianU32(data + 4);
        if ((trackEnd < trackData) || (trackEnd > end))
        {
            return FALSE;
        }

        runningStatus = 0;
        absTick = 0;
        noteEndTick = 0;
        trackHasMute = FALSE;
        trackHasNotes = FALSE;

        while (trackData < trackEnd)
        {
            uint32_t deltaTime;
            unsigned char status;

            if (PV_ReadMidiVariableLength(&trackData, trackEnd, &deltaTime) == FALSE)
            {
                return FALSE;
            }
            absTick += deltaTime;

            if (trackData >= trackEnd)
            {
                break;
            }

            status = *trackData++;
            if ((status & 0x80) == 0)
            {
                if (runningStatus == 0)
                {
                    return FALSE;
                }
                trackData--;
                status = runningStatus;
            }
            else if (status < 0xF0)
            {
                runningStatus = status;
            }

            if (status == 0xFF)
            {
                uint32_t metaLength;
                if (trackData >= trackEnd)
                {
                    return FALSE;
                }
                /* Skip meta type byte before reading meta payload length. */
                trackData++;
                if (PV_ReadMidiVariableLength(&trackData, trackEnd, &metaLength) == FALSE)
                {
                    return FALSE;
                }
                if ((size_t)(trackEnd - trackData) < metaLength)
                {
                    return FALSE;
                }
                trackData += metaLength;
                runningStatus = 0;
                continue;
            }

            if ((status == 0xF0) || (status == 0xF7))
            {
                uint32_t sysexLength;
                if (PV_ReadMidiVariableLength(&trackData, trackEnd, &sysexLength) == FALSE)
                {
                    return FALSE;
                }
                if ((size_t)(trackEnd - trackData) < sysexLength)
                {
                    return FALSE;
                }
                trackData += sysexLength;
                runningStatus = 0;
                continue;
            }

            if ((status >= 0xF1) && (status <= 0xFE))
            {
                uint32_t sysLen;

                switch (status)
                {
                case 0xF1:
                case 0xF3:
                    sysLen = 1;
                    break;
                case 0xF2:
                    sysLen = 2;
                    break;
                default:
                    sysLen = 0;
                    break;
                }
                if ((size_t)(trackEnd - trackData) < sysLen)
                {
                    return FALSE;
                }
                trackData += sysLen;
                runningStatus = 0;
                continue;
            }

            switch (status & 0xF0)
            {
            case 0x80:
            case 0x90:
            case 0xA0:
            case 0xE0:
                if ((size_t)(trackEnd - trackData) < 2)
                {
                    return FALSE;
                }
                if (((status & 0xF0) == 0x80) ||
                    (((status & 0xF0) == 0x90) && (trackData[1] > 0)) ||
                    (((status & 0xF0) == 0x90) && (trackData[1] == 0)))
                {
                    trackHasNotes = TRUE;
                    noteEndTick = absTick;
                }
                trackData += 2;
                break;

            case 0xB0:
                if ((size_t)(trackEnd - trackData) < 2)
                {
                    return FALSE;
                }
                if ((trackData[0] == 86) || (trackData[0] == 87))
                {
                    muteEvents++;
                    if (trackData[0] == 86)
                    {
                        muteSetEvents++;
                    }
                    else
                    {
                        muteClearEvents++;
                    }
                    trackHasMute = TRUE;
                    if (trackData[1] < 128)
                    {
                        if (seenMuteValue[trackData[1]] == 0)
                        {
                            seenMuteValue[trackData[1]] = 1;
                            distinctMuteValues++;
                        }
                    }
                }
                else if (trackData[0] == 85)
                {
                    loopEvents++;
                }
                trackData += 2;
                break;

            case 0xC0:
            case 0xD0:
                if ((size_t)(trackEnd - trackData) < 1)
                {
                    return FALSE;
                }
                trackData += 1;
                break;

            default:
                return FALSE;
            }
        }

        if (trackHasNotes)
        {
            tracksWithNotes++;
            if (noteEndTick < minNoteEnd)
            {
                minNoteEnd = noteEndTick;
            }
            if (noteEndTick > maxNoteEnd)
            {
                maxNoteEnd = noteEndTick;
            }
        }
        if (trackHasMute)
        {
            tracksWithMute++;
        }

        data = trackEnd;
    }

    if ((muteSetEvents > 0) && (muteClearEvents > 0) && (loopEvents > 0) && (distinctMuteValues >= 3) && (tracksWithMute >= 4))
    {
        return TRUE;
    }

    if ((muteSetEvents > 0) && (muteClearEvents > 0) && (muteEvents >= 16) && (distinctMuteValues >= 3) && (tracksWithMute >= 4))
    {
        return TRUE;
    }

    debug_message("[BAE] Rolled detect metrics: mute=%u set=%u clear=%u loop=%u distinctMuteVals=%u tracksWithMute=%u tracksWithNotes=%u noteSpan=%u\n",
                  muteEvents,
                  muteSetEvents,
                  muteClearEvents,
                  loopEvents,
                  distinctMuteValues,
                  (unsigned)tracksWithMute,
                  (unsigned)tracksWithNotes,
                  (unsigned)((maxNoteEnd >= minNoteEnd) ? (maxNoteEnd - minNoteEnd) : 0));
#else
    (void)rawData;
    (void)rawSize;
#endif
    return FALSE;
}

static BAE_BOOL PV_IsRolledMIDIRawControllerScan(const unsigned char *rawData, uint32_t rawSize)
{
    uint32_t i;
    uint32_t cc85;
    uint32_t cc86_87;
    uint32_t distinctMuteValues;
    unsigned char seenMuteValue[128];

    if ((rawData == NULL) || (rawSize < 3))
    {
        return FALSE;
    }

    XSetMemory(seenMuteValue, sizeof(seenMuteValue), 0);
    cc85 = 0;
    cc86_87 = 0;
    distinctMuteValues = 0;

    for (i = 0; i + 2 < rawSize; i++)
    {
        unsigned char st;
        st = rawData[i];
        if ((st >= 0xB0) && (st <= 0xBF))
        {
            unsigned char cc;
            unsigned char val;

            cc = rawData[i + 1];
            val = rawData[i + 2];
            if (cc == 85)
            {
                cc85++;
            }
            else if ((cc == 86) || (cc == 87))
            {
                cc86_87++;
                if ((val < 128) && (seenMuteValue[val] == 0))
                {
                    seenMuteValue[val] = 1;
                    distinctMuteValues++;
                }
            }
        }
    }

    if ((cc86_87 >= 16) && (distinctMuteValues >= 3))
    {
        return TRUE;
    }
    if ((cc85 > 0) && (cc86_87 >= 4) && (distinctMuteValues >= 2))
    {
        return TRUE;
    }
    return FALSE;
}

static BAE_BOOL PV_IsRolledMIDISequence(const GM_Song *pSong)
{
#if BAE_DISABLE_ROLLED_MIDI_DETECTION != TRUE
    if ((pSong == NULL) || (pSong->seqType != SEQ_MIDI) || (pSong->sequenceData == NULL) || (pSong->sequenceDataSize < 14))
    {
        return FALSE;
    }
    return PV_IsRolledMIDIBytes((const unsigned char *)pSong->sequenceData,
                                (uint32_t)pSong->sequenceDataSize);
#endif
    return FALSE;
}

static BAE_BOOL PV_IsRolledMIDIMemory(const void *pMidiData, uint32_t midiSize)
{
    uint32_t smfSize;
    const unsigned char *smfData;

    smfData = PV_FindMThdStart((const unsigned char *)pMidiData, midiSize, &smfSize);
    if ((smfData != NULL) && (smfSize >= 14))
    {
        return PV_IsRolledMIDIBytes((const unsigned char *)pMidiData, midiSize);
    }

    return PV_IsRolledMIDIRawControllerScan((const unsigned char *)pMidiData, midiSize);
}

#ifndef BAE_ENABLE_ROLLED_MIDI_UNROLL
#define BAE_ENABLE_ROLLED_MIDI_UNROLL TRUE
#endif

#if BAE_ENABLE_ROLLED_MIDI_UNROLL == TRUE

#define PV_UNROLL_EVENT_META 0
#define PV_UNROLL_EVENT_SYSEX 1
#define PV_UNROLL_EVENT_MIDI 2
#define PV_UNROLL_MAX_PASSES 1024

typedef struct PV_UnrollParsedEvent
{
    uint64_t tick;
    uint32_t seq;
    uint16_t trackIndex;
    uint8_t kind;
    uint8_t status;
    uint8_t metaType;
    uint8_t midiDataLen;
    unsigned char midiData[2];
    const unsigned char *payload;
    uint32_t payloadLen;
} PV_UnrollParsedEvent;

typedef struct PV_UnrollEventArray
{
    PV_UnrollParsedEvent *items;
    uint32_t count;
    uint32_t capacity;
} PV_UnrollEventArray;

typedef struct PV_UnrollOutputTrack
{
    PV_UnrollParsedEvent *events;
    uint32_t eventCount;
    uint32_t eventCapacity;
} PV_UnrollOutputTrack;

typedef struct PV_UnrollKey
{
    uint8_t channel;
    uint8_t bankMSB;
    uint8_t bankLSB;
    uint8_t program;
    int16_t drumNote;
} PV_UnrollKey;

typedef struct PV_UnrollKeyTrack
{
    PV_UnrollKey key;
    PV_UnrollOutputTrack track;
} PV_UnrollKeyTrack;

typedef struct PV_UnrollKeyTrackArray
{
    PV_UnrollKeyTrack *items;
    uint32_t count;
    uint32_t capacity;
} PV_UnrollKeyTrackArray;

typedef struct PV_UnrollByteBuffer
{
    unsigned char *data;
    uint32_t size;
    uint32_t capacity;
} PV_UnrollByteBuffer;

static uint16_t PV_UnrollReadBE16(const unsigned char *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static uint32_t PV_UnrollReadBE32(const unsigned char *data)
{
    return ((uint32_t)data[0] << 24)
         | ((uint32_t)data[1] << 16)
         | ((uint32_t)data[2] << 8)
         | (uint32_t)data[3];
}

static void PV_UnrollWriteBE16(unsigned char *data, uint16_t value)
{
    data[0] = (unsigned char)((value >> 8) & 0xFF);
    data[1] = (unsigned char)(value & 0xFF);
}

static void PV_UnrollWriteBE32(unsigned char *data, uint32_t value)
{
    data[0] = (unsigned char)((value >> 24) & 0xFF);
    data[1] = (unsigned char)((value >> 16) & 0xFF);
    data[2] = (unsigned char)((value >> 8) & 0xFF);
    data[3] = (unsigned char)(value & 0xFF);
}

static BAE_BOOL PV_UnrollReadVLQ(const unsigned char **ppData, const unsigned char *end, uint32_t *outValue)
{
    uint32_t value;
    int count;
    const unsigned char *data;

    if ((ppData == NULL) || (*ppData == NULL) || (outValue == NULL))
    {
        return FALSE;
    }
    value = 0;
    data = *ppData;
    for (count = 0; count < 4; count++)
    {
        unsigned char b;
        if (data >= end)
        {
            return FALSE;
        }
        b = *data++;
        value = (value << 7) | (uint32_t)(b & 0x7F);
        if ((b & 0x80) == 0)
        {
            *ppData = data;
            *outValue = value;
            return TRUE;
        }
    }
    return FALSE;
}

static uint32_t PV_UnrollWriteVLQ(unsigned char out[4], uint32_t value)
{
    unsigned char tmp[4];
    uint32_t count;

    count = 0;
    tmp[count++] = (unsigned char)(value & 0x7F);
    value >>= 7;
    while (value && count < 4)
    {
        tmp[count++] = (unsigned char)(0x80 | (value & 0x7F));
        value >>= 7;
    }
    {
        uint32_t i;
        for (i = 0; i < count; i++)
        {
            out[i] = tmp[count - 1 - i];
        }
    }
    return count;
}

static BAE_BOOL PV_UnrollEnsureEvents(PV_UnrollEventArray *arr, uint32_t needed)
{
    if (arr->capacity < needed)
    {
        uint32_t newCap;
        PV_UnrollParsedEvent *newItems;

        newCap = arr->capacity ? arr->capacity * 2 : 1024;
        if (newCap < needed)
        {
            newCap = needed;
        }
        newItems = (PV_UnrollParsedEvent *)realloc(arr->items, (size_t)newCap * sizeof(PV_UnrollParsedEvent));
        if (newItems == NULL)
        {
            return FALSE;
        }
        arr->items = newItems;
        arr->capacity = newCap;
    }
    return TRUE;
}

static BAE_BOOL PV_UnrollAppendEvent(PV_UnrollEventArray *arr, const PV_UnrollParsedEvent *event)
{
    if (PV_UnrollEnsureEvents(arr, arr->count + 1) == FALSE)
    {
        return FALSE;
    }
    arr->items[arr->count++] = *event;
    return TRUE;
}

static int PV_UnrollEventCompare(const void *a, const void *b)
{
    const PV_UnrollParsedEvent *ea;
    const PV_UnrollParsedEvent *eb;
    ea = (const PV_UnrollParsedEvent *)a;
    eb = (const PV_UnrollParsedEvent *)b;
    if (ea->tick < eb->tick)
    {
        return -1;
    }
    if (ea->tick > eb->tick)
    {
        return 1;
    }
    if (ea->trackIndex < eb->trackIndex)
    {
        return -1;
    }
    if (ea->trackIndex > eb->trackIndex)
    {
        return 1;
    }
    if (ea->seq < eb->seq)
    {
        return -1;
    }
    if (ea->seq > eb->seq)
    {
        return 1;
    }
    return 0;
}

static BAE_BOOL PV_UnrollIsMetaOnce(uint8_t metaType)
{
    return (metaType == 0x00) || (metaType == 0x02) || (metaType == 0x03) || (metaType == 0x04);
}

static BAE_BOOL PV_UnrollIsNoteEvent(const PV_UnrollParsedEvent *event)
{
    uint8_t type;
    if (event->kind != PV_UNROLL_EVENT_MIDI)
    {
        return FALSE;
    }
    type = (uint8_t)(event->status & 0xF0);
    return (type == 0x80) || (type == 0x90);
}

static BAE_BOOL PV_UnrollParseSMF(const unsigned char *midiData,
                                  uint32_t midiSize,
                                  PV_UnrollEventArray *events,
                                  uint16_t *outDivision,
                                  uint16_t *outTrackCount,
                                  uint32_t *outSongLength,
                                  BAE_BOOL *outHasRoll,
                                  uint8_t *outMaxLoopCount,
                                  uint8_t *outMaxRollIndex)
{
    const unsigned char *data;
    const unsigned char *end;
    uint32_t headerLength;
    uint16_t trackCount;
    uint32_t maxTick;
    BAE_BOOL hasRoll;
    uint8_t maxLoopCount;
    uint8_t maxRollIndex;
    uint16_t trackIndex;

    if ((midiData == NULL) || (midiSize < 14) || (events == NULL) || (outDivision == NULL) || (outTrackCount == NULL) ||
        (outSongLength == NULL) || (outHasRoll == NULL) || (outMaxLoopCount == NULL) || (outMaxRollIndex == NULL))
    {
        return FALSE;
    }

    data = midiData;
    end = midiData + midiSize;
    if ((size_t)(end - data) < 14)
    {
        return FALSE;
    }
    if (memcmp(data, "MThd", 4) != 0)
    {
        return FALSE;
    }
    headerLength = PV_UnrollReadBE32(data + 4);
    if ((headerLength < 6) || ((size_t)(end - data) < (size_t)(8 + headerLength)))
    {
        return FALSE;
    }

    if ((PV_UnrollReadBE16(data + 12) & 0x8000) != 0)
    {
        return FALSE;
    }

    *outDivision = PV_UnrollReadBE16(data + 12);
    trackCount = PV_UnrollReadBE16(data + 10);
    *outTrackCount = trackCount;

    data += 8 + headerLength;
    maxTick = 0;
    hasRoll = FALSE;
    maxLoopCount = 0;
    maxRollIndex = 0;

    for (trackIndex = 0; trackIndex < trackCount; trackIndex++)
    {
        const unsigned char *trackData;
        const unsigned char *trackEnd;
        uint32_t trackLen;
        uint32_t absTick;
        uint32_t seq;
        unsigned char runningStatus;

        if ((size_t)(end - data) < 8)
        {
            return FALSE;
        }
        if (memcmp(data, "MTrk", 4) != 0)
        {
            return FALSE;
        }
        trackLen = PV_UnrollReadBE32(data + 4);
        trackData = data + 8;
        trackEnd = trackData + trackLen;
        if ((trackEnd < trackData) || (trackEnd > end))
        {
            return FALSE;
        }

        absTick = 0;
        seq = 0;
        runningStatus = 0;

        while (trackData < trackEnd)
        {
            uint32_t delta;
            PV_UnrollParsedEvent ev;
            unsigned char status;

            if (PV_UnrollReadVLQ(&trackData, trackEnd, &delta) == FALSE)
            {
                return FALSE;
            }
            absTick += delta;
            if (absTick > maxTick)
            {
                maxTick = absTick;
            }
            if (trackData >= trackEnd)
            {
                return FALSE;
            }

            status = *trackData++;
            if ((status & 0x80) == 0)
            {
                if (runningStatus == 0)
                {
                    return FALSE;
                }
                trackData--;
                status = runningStatus;
            }
            else if ((status < 0xF0) || (status == 0xF0) || (status == 0xF7))
            {
                runningStatus = status;
            }

            ev.tick = absTick;
            ev.seq = seq++;
            ev.trackIndex = trackIndex;
            ev.status = status;
            ev.metaType = 0;
            ev.midiDataLen = 0;
            ev.midiData[0] = 0;
            ev.midiData[1] = 0;
            ev.payload = NULL;
            ev.payloadLen = 0;

            if (status == 0xFF)
            {
                uint32_t metaLen;
                if (trackData >= trackEnd)
                {
                    return FALSE;
                }
                ev.kind = PV_UNROLL_EVENT_META;
                ev.metaType = *trackData++;
                if (PV_UnrollReadVLQ(&trackData, trackEnd, &metaLen) == FALSE)
                {
                    return FALSE;
                }
                if ((size_t)(trackEnd - trackData) < metaLen)
                {
                    return FALSE;
                }
                ev.payload = trackData;
                ev.payloadLen = metaLen;
                trackData += metaLen;
                runningStatus = 0;

                if (ev.metaType == 0x2F)
                {
                    continue;
                }
                if (PV_UnrollAppendEvent(events, &ev) == FALSE)
                {
                    return FALSE;
                }
                continue;
            }

            if ((status == 0xF0) || (status == 0xF7))
            {
                uint32_t sysexLen;
                if (PV_UnrollReadVLQ(&trackData, trackEnd, &sysexLen) == FALSE)
                {
                    return FALSE;
                }
                if ((size_t)(trackEnd - trackData) < sysexLen)
                {
                    return FALSE;
                }
                ev.kind = PV_UNROLL_EVENT_SYSEX;
                ev.payload = trackData;
                ev.payloadLen = sysexLen;
                trackData += sysexLen;
                runningStatus = 0;
                if (PV_UnrollAppendEvent(events, &ev) == FALSE)
                {
                    return FALSE;
                }
                continue;
            }

            ev.kind = PV_UNROLL_EVENT_MIDI;
            switch (status & 0xF0)
            {
            case 0x80:
            case 0x90:
            case 0xA0:
            case 0xB0:
            case 0xE0:
                if ((size_t)(trackEnd - trackData) < 2)
                {
                    return FALSE;
                }
                ev.midiDataLen = 2;
                ev.midiData[0] = trackData[0];
                ev.midiData[1] = trackData[1];
                trackData += 2;
                break;
            case 0xC0:
            case 0xD0:
                if ((size_t)(trackEnd - trackData) < 1)
                {
                    return FALSE;
                }
                ev.midiDataLen = 1;
                ev.midiData[0] = trackData[0];
                trackData += 1;
                break;
            default:
                return FALSE;
            }

            if (((ev.status & 0xF0) == 0xB0) && (ev.midiDataLen == 2))
            {
                if (ev.midiData[0] == 85)
                {
                    hasRoll = TRUE;
                    if (ev.midiData[1] > maxLoopCount)
                    {
                        maxLoopCount = ev.midiData[1];
                    }
                }
                else if ((ev.midiData[0] == 86) || (ev.midiData[0] == 87))
                {
                    hasRoll = TRUE;
                    if (ev.midiData[1] > maxRollIndex)
                    {
                        maxRollIndex = ev.midiData[1];
                    }
                }
            }

            if (PV_UnrollAppendEvent(events, &ev) == FALSE)
            {
                return FALSE;
            }
        }
        data = trackEnd;
    }

    *outSongLength = maxTick;
    *outHasRoll = hasRoll;
    *outMaxLoopCount = maxLoopCount;
    *outMaxRollIndex = maxRollIndex;
    return TRUE;
}

static BAE_BOOL PV_UnrollEnsureTrackEvents(PV_UnrollOutputTrack *track, uint32_t needed)
{
    if (track->eventCapacity < needed)
    {
        uint32_t newCap;
        PV_UnrollParsedEvent *newEvents;

        newCap = track->eventCapacity ? track->eventCapacity * 2 : 512;
        if (newCap < needed)
        {
            newCap = needed;
        }
        newEvents = (PV_UnrollParsedEvent *)realloc(track->events, (size_t)newCap * sizeof(PV_UnrollParsedEvent));
        if (newEvents == NULL)
        {
            return FALSE;
        }
        track->events = newEvents;
        track->eventCapacity = newCap;
    }
    return TRUE;
}

static BAE_BOOL PV_UnrollAppendTrackEvent(PV_UnrollOutputTrack *track, const PV_UnrollParsedEvent *event)
{
    if (PV_UnrollEnsureTrackEvents(track, track->eventCount + 1) == FALSE)
    {
        return FALSE;
    }
    track->events[track->eventCount++] = *event;
    return TRUE;
}

static int PV_UnrollFindOrAddKeyTrack(PV_UnrollKeyTrackArray *groups, const PV_UnrollKey *key)
{
    uint32_t i;
    for (i = 0; i < groups->count; i++)
    {
        if ((groups->items[i].key.channel == key->channel) &&
            (groups->items[i].key.bankMSB == key->bankMSB) &&
            (groups->items[i].key.bankLSB == key->bankLSB) &&
            (groups->items[i].key.program == key->program) &&
            (groups->items[i].key.drumNote == key->drumNote))
        {
            return (int)i;
        }
    }

    if (groups->count >= groups->capacity)
    {
        uint32_t newCap;
        PV_UnrollKeyTrack *newItems;
        newCap = groups->capacity ? groups->capacity * 2 : 32;
        newItems = (PV_UnrollKeyTrack *)realloc(groups->items, (size_t)newCap * sizeof(PV_UnrollKeyTrack));
        if (newItems == NULL)
        {
            return -1;
        }
        groups->items = newItems;
        groups->capacity = newCap;
    }

    groups->items[groups->count].key = *key;
    groups->items[groups->count].track.events = NULL;
    groups->items[groups->count].track.eventCount = 0;
    groups->items[groups->count].track.eventCapacity = 0;
    groups->count++;
    return (int)(groups->count - 1);
}

static BAE_BOOL PV_UnrollSplitByInstrument(const PV_UnrollOutputTrack *singleTrack,
                                           PV_UnrollOutputTrack *conductorTrack,
                                           PV_UnrollKeyTrackArray *groups)
{
    uint32_t i;
    uint8_t bankMSB[16];
    uint8_t bankLSB[16];
    uint8_t program[16];
    int16_t noteGroupStack[16][128][32];
    uint8_t noteGroupDepth[16][128];

    for (i = 0; i < 16; i++)
    {
        uint32_t n;
        bankMSB[i] = 0;
        bankLSB[i] = 0;
        program[i] = 0;
        for (n = 0; n < 128; n++)
        {
            noteGroupDepth[i][n] = 0;
        }
    }

    for (i = 0; i < singleTrack->eventCount; i++)
    {
        const PV_UnrollParsedEvent *ev;
        ev = &singleTrack->events[i];
        if (ev->kind != PV_UNROLL_EVENT_MIDI)
        {
            if (PV_UnrollAppendTrackEvent(conductorTrack, ev) == FALSE)
            {
                return FALSE;
            }
            continue;
        }

        {
            uint8_t channel;
            uint8_t type;
            PV_UnrollKey key;
            int groupIndex;
            BAE_BOOL isNoteOn;
            BAE_BOOL isNoteOff;
            uint8_t noteValue;

            channel = (uint8_t)(ev->status & 0x0F);
            type = (uint8_t)(ev->status & 0xF0);
            isNoteOn = FALSE;
            isNoteOff = FALSE;
            noteValue = 0;

            if ((type == 0xB0) && (ev->midiDataLen == 2))
            {
                if (ev->midiData[0] == 0)
                {
                    bankMSB[channel] = ev->midiData[1];
                }
                else if (ev->midiData[0] == 32)
                {
                    bankLSB[channel] = ev->midiData[1];
                }
            }
            else if ((type == 0xC0) && (ev->midiDataLen == 1))
            {
                program[channel] = ev->midiData[0];
            }

            if (((type == 0x90) || (type == 0x80)) && (ev->midiDataLen >= 2))
            {
                noteValue = ev->midiData[0];
                isNoteOn = (type == 0x90) && (ev->midiData[1] != 0);
                isNoteOff = (type == 0x80) || ((type == 0x90) && (ev->midiData[1] == 0));
            }

            key.channel = channel;
            key.bankMSB = bankMSB[channel];
            key.bankLSB = bankLSB[channel];
            key.program = program[channel];
            key.drumNote = -1;
            if ((channel == 9) && ((type == 0x80) || (type == 0x90)) && (ev->midiDataLen >= 1))
            {
                key.drumNote = (int16_t)ev->midiData[0];
            }

            if (isNoteOn)
            {
                groupIndex = PV_UnrollFindOrAddKeyTrack(groups, &key);
                if (groupIndex < 0)
                {
                    return FALSE;
                }
                if (PV_UnrollAppendTrackEvent(&groups->items[groupIndex].track, ev) == FALSE)
                {
                    return FALSE;
                }

                if (noteGroupDepth[channel][noteValue] < 32)
                {
                    noteGroupStack[channel][noteValue][noteGroupDepth[channel][noteValue]] = (int16_t)groupIndex;
                    noteGroupDepth[channel][noteValue]++;
                }
                else
                {
                    noteGroupStack[channel][noteValue][31] = (int16_t)groupIndex;
                }
                continue;
            }

            if (isNoteOff)
            {
                if (noteGroupDepth[channel][noteValue] > 0)
                {
                    noteGroupDepth[channel][noteValue]--;
                    groupIndex = noteGroupStack[channel][noteValue][noteGroupDepth[channel][noteValue]];
                }
                else
                {
                    groupIndex = -1;
                }

                if (groupIndex < 0)
                {
                    groupIndex = PV_UnrollFindOrAddKeyTrack(groups, &key);
                    if (groupIndex < 0)
                    {
                        return FALSE;
                    }
                }

                if (PV_UnrollAppendTrackEvent(&groups->items[groupIndex].track, ev) == FALSE)
                {
                    return FALSE;
                }
                continue;
            }

            {
                int activeGroupList[256];
                int activeCount;
                uint32_t n;
                int k;
                int candidate;
                BAE_BOOL already;

                activeCount = 0;
                for (n = 0; n < 128; n++)
                {
                    if (noteGroupDepth[channel][n] > 0)
                    {
                        candidate = noteGroupStack[channel][n][noteGroupDepth[channel][n] - 1];
                        already = FALSE;
                        for (k = 0; k < activeCount; k++)
                        {
                            if (activeGroupList[k] == candidate)
                            {
                                already = TRUE;
                                break;
                            }
                        }
                        if (!already && (activeCount < 256))
                        {
                            activeGroupList[activeCount++] = candidate;
                        }
                    }
                }

                if (activeCount == 0)
                {
                    groupIndex = PV_UnrollFindOrAddKeyTrack(groups, &key);
                    if (groupIndex < 0)
                    {
                        return FALSE;
                    }
                    if (PV_UnrollAppendTrackEvent(&groups->items[groupIndex].track, ev) == FALSE)
                    {
                        return FALSE;
                    }
                }
                else
                {
                    for (k = 0; k < activeCount; k++)
                    {
                        if (PV_UnrollAppendTrackEvent(&groups->items[activeGroupList[k]].track, ev) == FALSE)
                        {
                            return FALSE;
                        }
                    }
                }
            }
        }
    }
    return TRUE;
}

static BAE_BOOL PV_UnrollByteBufferEnsure(PV_UnrollByteBuffer *out, uint32_t needed)
{
    if (out->capacity < needed)
    {
        uint32_t newCap;
        unsigned char *newData;
        newCap = out->capacity ? out->capacity * 2 : 4096;
        if (newCap < needed)
        {
            newCap = needed;
        }
        newData = (unsigned char *)realloc(out->data, newCap);
        if (newData == NULL)
        {
            return FALSE;
        }
        out->data = newData;
        out->capacity = newCap;
    }
    return TRUE;
}

static BAE_BOOL PV_UnrollByteBufferWrite(PV_UnrollByteBuffer *out, const void *data, uint32_t size)
{
    if (PV_UnrollByteBufferEnsure(out, out->size + size) == FALSE)
    {
        return FALSE;
    }
    XBlockMove(data, out->data + out->size, size);
    out->size += size;
    return TRUE;
}

static BAE_BOOL PV_UnrollWriteTrackBytes(const PV_UnrollOutputTrack *track, PV_UnrollByteBuffer *out)
{
    PV_UnrollByteBuffer trackData;
    uint32_t i;
    uint64_t lastTick;
    unsigned char trackHeader[8];
    unsigned char eot[4];

    trackData.data = NULL;
    trackData.size = 0;
    trackData.capacity = 0;
    lastTick = 0;

    for (i = 0; i < track->eventCount; i++)
    {
        const PV_UnrollParsedEvent *ev;
        unsigned char vlq[4];
        uint64_t delta64;
        uint32_t delta;
        uint32_t vlqLen;

        ev = &track->events[i];
        if (ev->tick < lastTick)
        {
            free(trackData.data);
            return FALSE;
        }
        delta64 = ev->tick - lastTick;
        if (delta64 > 0x0FFFFFFFUL)
        {
            free(trackData.data);
            return FALSE;
        }
        delta = (uint32_t)delta64;
        lastTick = ev->tick;

        vlqLen = PV_UnrollWriteVLQ(vlq, delta);
        if (PV_UnrollByteBufferWrite(&trackData, vlq, vlqLen) == FALSE)
        {
            free(trackData.data);
            return FALSE;
        }

        if (ev->kind == PV_UNROLL_EVENT_META)
        {
            unsigned char marker[2];
            unsigned char lenVLQ[4];
            uint32_t lenLen;
            marker[0] = 0xFF;
            marker[1] = ev->metaType;
            lenLen = PV_UnrollWriteVLQ(lenVLQ, ev->payloadLen);
            if ((PV_UnrollByteBufferWrite(&trackData, marker, 2) == FALSE) ||
                (PV_UnrollByteBufferWrite(&trackData, lenVLQ, lenLen) == FALSE) ||
                (PV_UnrollByteBufferWrite(&trackData, ev->payload, ev->payloadLen) == FALSE))
            {
                free(trackData.data);
                return FALSE;
            }
        }
        else if (ev->kind == PV_UNROLL_EVENT_SYSEX)
        {
            unsigned char status;
            unsigned char lenVLQ[4];
            uint32_t lenLen;
            status = ev->status;
            lenLen = PV_UnrollWriteVLQ(lenVLQ, ev->payloadLen);
            if ((PV_UnrollByteBufferWrite(&trackData, &status, 1) == FALSE) ||
                (PV_UnrollByteBufferWrite(&trackData, lenVLQ, lenLen) == FALSE) ||
                (PV_UnrollByteBufferWrite(&trackData, ev->payload, ev->payloadLen) == FALSE))
            {
                free(trackData.data);
                return FALSE;
            }
        }
        else
        {
            if ((PV_UnrollByteBufferWrite(&trackData, &ev->status, 1) == FALSE) ||
                (PV_UnrollByteBufferWrite(&trackData, ev->midiData, ev->midiDataLen) == FALSE))
            {
                free(trackData.data);
                return FALSE;
            }
        }
    }

    eot[0] = 0x00;
    eot[1] = 0xFF;
    eot[2] = 0x2F;
    eot[3] = 0x00;
    if (PV_UnrollByteBufferWrite(&trackData, eot, 4) == FALSE)
    {
        free(trackData.data);
        return FALSE;
    }

    XBlockMove("MTrk", trackHeader, 4);
    PV_UnrollWriteBE32(trackHeader + 4, trackData.size);
    if ((PV_UnrollByteBufferWrite(out, trackHeader, 8) == FALSE) ||
        (PV_UnrollByteBufferWrite(out, trackData.data, trackData.size) == FALSE))
    {
        free(trackData.data);
        return FALSE;
    }

    free(trackData.data);
    return TRUE;
}

static BAE_BOOL PV_UnrollBuildSMF(uint16_t division,
                                  const PV_UnrollOutputTrack *tracks,
                                  uint16_t trackCount,
                                  unsigned char **ppOut,
                                  uint32_t *pOutSize)
{
    PV_UnrollByteBuffer out;
    unsigned char header[14];
    uint16_t i;
    unsigned char *finalCopy;

    out.data = NULL;
    out.size = 0;
    out.capacity = 0;

    XBlockMove("MThd", header, 4);
    PV_UnrollWriteBE32(header + 4, 6);
    PV_UnrollWriteBE16(header + 8, (trackCount == 1) ? 0 : 1);
    PV_UnrollWriteBE16(header + 10, trackCount);
    PV_UnrollWriteBE16(header + 12, division);
    if (PV_UnrollByteBufferWrite(&out, header, 14) == FALSE)
    {
        free(out.data);
        return FALSE;
    }

    for (i = 0; i < trackCount; i++)
    {
        if (PV_UnrollWriteTrackBytes(&tracks[i], &out) == FALSE)
        {
            free(out.data);
            return FALSE;
        }
    }

    finalCopy = (unsigned char *)XNewPtr(out.size);
    if (finalCopy == NULL)
    {
        free(out.data);
        return FALSE;
    }
    XBlockMove(out.data, finalCopy, out.size);
    *ppOut = finalCopy;
    *pOutSize = out.size;
    free(out.data);
    return TRUE;
}

static void PV_UnrollFreeTracks(PV_UnrollOutputTrack *tracks, uint16_t trackCount)
{
    uint16_t i;
    for (i = 0; i < trackCount; i++)
    {
        free(tracks[i].events);
        tracks[i].events = NULL;
        tracks[i].eventCount = 0;
        tracks[i].eventCapacity = 0;
    }
}

static BAEResult PV_UnrollRolledMidi(const unsigned char *midiData,
                                     uint32_t midiSize,
                                     uint32_t options,
                                     unsigned char **ppMidiOut,
                                     uint32_t *pMidiSizeOut,
                                     BAE_BOOL *outWasRolled)
{
    const unsigned char *smfData;
    uint32_t smfSize;
    PV_UnrollEventArray parsed;
    uint16_t division;
    uint16_t trackCount;
    uint32_t songLength;
    BAE_BOOL hasRoll;
    uint8_t maxLoopCount;
    uint8_t maxRollIndex;
    uint32_t totalPasses;
    uint32_t passIndex;
    BAE_BOOL *mutedTracks;
    PV_UnrollOutputTrack singleTrack;
    BAEResult result;
    uint32_t i;

    if ((midiData == NULL) || (ppMidiOut == NULL) || (pMidiSizeOut == NULL))
    {
        return BAE_PARAM_ERR;
    }

    smfData = PV_FindMThdStart(midiData, midiSize, &smfSize);
    if (smfData == NULL)
    {
        return BAE_BAD_FILE;
    }

    *ppMidiOut = NULL;
    *pMidiSizeOut = 0;
    if (outWasRolled)
    {
        *outWasRolled = FALSE;
    }

    parsed.items = NULL;
    parsed.count = 0;
    parsed.capacity = 0;
    if (PV_UnrollParseSMF(smfData, smfSize, &parsed, &division, &trackCount, &songLength, &hasRoll, &maxLoopCount, &maxRollIndex) == FALSE)
    {
        free(parsed.items);
        return BAE_BAD_FILE;
    }
    if (parsed.count)
    {
        qsort(parsed.items, parsed.count, sizeof(PV_UnrollParsedEvent), PV_UnrollEventCompare);
    }

    if (maxLoopCount > 0)
    {
        totalPasses = (uint32_t)maxLoopCount + 1;
    }
    else if (hasRoll)
    {
        totalPasses = (uint32_t)maxRollIndex + 1;
    }
    else
    {
        totalPasses = 1;
    }
    if (totalPasses == 0)
    {
        totalPasses = 1;
    }
    if (totalPasses > PV_UNROLL_MAX_PASSES)
    {
        free(parsed.items);
        return BAE_BAD_FILE;
    }
    if (outWasRolled)
    {
        *outWasRolled = hasRoll;
    }

    mutedTracks = (BAE_BOOL *)calloc(trackCount ? trackCount : 1, sizeof(BAE_BOOL));
    if (mutedTracks == NULL)
    {
        free(parsed.items);
        return BAE_MEMORY_ERR;
    }

    singleTrack.events = NULL;
    singleTrack.eventCount = 0;
    singleTrack.eventCapacity = 0;
    result = BAE_NO_ERROR;

    for (passIndex = 0; passIndex < totalPasses; passIndex++)
    {
        uint64_t passOffset;
        passOffset = (uint64_t)passIndex * (uint64_t)songLength;
        for (i = 0; i < parsed.count; i++)
        {
            PV_UnrollParsedEvent outEvent;
            const PV_UnrollParsedEvent *ev;
            ev = &parsed.items[i];

            if (ev->kind == PV_UNROLL_EVENT_META)
            {
                if ((passIndex > 0) && PV_UnrollIsMetaOnce(ev->metaType))
                {
                    continue;
                }
            }
            else if ((ev->kind == PV_UNROLL_EVENT_MIDI) && ((ev->status & 0xF0) == 0xB0) && (ev->midiDataLen == 2))
            {
                if (ev->midiData[0] == 86)
                {
                    if (passIndex == (uint32_t)ev->midiData[1])
                    {
                        mutedTracks[ev->trackIndex] = TRUE;
                    }
                    continue;
                }
                if (ev->midiData[0] == 87)
                {
                    if (passIndex == (uint32_t)ev->midiData[1])
                    {
                        mutedTracks[ev->trackIndex] = FALSE;
                    }
                    continue;
                }
                if (ev->midiData[0] == 85)
                {
                    continue;
                }
            }

            if (PV_UnrollIsNoteEvent(ev) && mutedTracks[ev->trackIndex])
            {
                continue;
            }

            outEvent = *ev;
            outEvent.tick = passOffset + ev->tick;
            if (PV_UnrollAppendTrackEvent(&singleTrack, &outEvent) == FALSE)
            {
                result = BAE_MEMORY_ERR;
                goto unroll_cleanup;
            }
        }
    }

    if ((options & BAE_UNROLL_MIDI_OPTION_SPLIT_INSTRUMENTS) != 0)
    {
        PV_UnrollOutputTrack conductor;
        PV_UnrollKeyTrackArray groups;
        PV_UnrollOutputTrack *outTracks;
        uint16_t outTrackCount;

        conductor.events = NULL;
        conductor.eventCount = 0;
        conductor.eventCapacity = 0;
        groups.items = NULL;
        groups.count = 0;
        groups.capacity = 0;

        if (PV_UnrollSplitByInstrument(&singleTrack, &conductor, &groups) == FALSE)
        {
            result = BAE_MEMORY_ERR;
            free(groups.items);
            free(conductor.events);
            goto unroll_cleanup;
        }

        outTrackCount = (uint16_t)(1 + groups.count);
        outTracks = (PV_UnrollOutputTrack *)calloc(outTrackCount, sizeof(PV_UnrollOutputTrack));
        if (outTracks == NULL)
        {
            result = BAE_MEMORY_ERR;
            free(conductor.events);
            for (i = 0; i < groups.count; i++)
            {
                free(groups.items[i].track.events);
            }
            free(groups.items);
            goto unroll_cleanup;
        }

        outTracks[0] = conductor;
        for (i = 0; i < groups.count; i++)
        {
            outTracks[i + 1] = groups.items[i].track;
        }

        if (PV_UnrollBuildSMF(division, outTracks, outTrackCount, ppMidiOut, pMidiSizeOut) == FALSE)
        {
            result = BAE_MEMORY_ERR;
        }

        PV_UnrollFreeTracks(outTracks, outTrackCount);
        free(outTracks);
        free(groups.items);
        if (result != BAE_NO_ERROR)
        {
            goto unroll_cleanup;
        }
    }
    else
    {
        if (PV_UnrollBuildSMF(division, &singleTrack, 1, ppMidiOut, pMidiSizeOut) == FALSE)
        {
            result = BAE_MEMORY_ERR;
            goto unroll_cleanup;
        }
    }

unroll_cleanup:
    free(parsed.items);
    free(mutedTracks);
    free(singleTrack.events);
    return result;
}

#endif // BAE_ENABLE_ROLLED_MIDI_UNROLL

#if USE_OPUS_DECODER == TRUE || USE_OPUS_ENCODER == TRUE
#include <opus.h>
#include <ogg/ogg.h>
#endif

#if USE_OPUS_DECODER == TRUE
#include <opusfile.h>
#endif


#if _BUILT_IN_PATCHES == TRUE
#include "BAEPatches.h"
#endif

#if USE_FLAC_ENCODER != TRUE && USE_FLAC_DECODER == TRUE
#include "FLAC/stream_decoder.h"
#endif

#if USE_FLAC_ENCODER == TRUE
#include "FLAC/stream_encoder.h"
// Forward declaration of FLAC encoding function from GenSoundFiles.c
OPErr PV_WriteFromMemoryFLACFile(XFILENAME *file, GM_Waveform const *pAudioData, uint16_t formatTag);
// Wave format constant
#define X_WAVE_FORMAT_PCM 0x0001
#endif
#include "sha1mini.h" // hashing for friendly name resolution

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

void process_and_send_audio(int16_t *rawAudio, int length)
{
    // Call the JavaScript function "postAudioData" with the pointer
    EM_ASM({ window.NeoBAEInstance.postAudioData($0, $1); }, rawAudio, length);
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

#if defined(__clang_version__)
    int major, minor, patch;
    sscanf(__clang_version__, "%d.%d.%d", &major, &minor, &patch);
#endif

#if defined(__EMSCRIPTEN__) && defined(__clang_version__)
#ifdef __cplusplus
    snprintf(versionString, maxStrSize, "clang++ v%d.%d.%d, emscripten v%d.%d", major, minor, patch, __EMSCRIPTEN_MAJOR__, __EMSCRIPTEN_MINOR__);
#else
    snprintf(versionString, maxStrSize, "clang v%d.%d.%d, emscripten v%d.%d", major, minor, patch, __EMSCRIPTEN_MAJOR__, __EMSCRIPTEN_MINOR__);
#endif
#elif defined(__ANDROID__) && defined(__clang_version__)
#ifdef __cplusplus
    snprintf(versionString, maxStrSize, "clang++ v%d.%d.%d, (Android NDK %d.%d, API %d)", major, minor, patch, __NDK_MAJOR__, __NDK_MINOR__, __ANDROID_API__);
#else
    snprintf(versionString, maxStrSize, "clang v%d.%d.%d (Android NDK %d.%d, API %d)", major, minor, patch, __NDK_MAJOR__, __NDK_MINOR__, __ANDROID_API__);
#endif
#elif defined(__clang_version__)
#ifdef __cplusplus
    snprintf(versionString, maxStrSize, "clang++ v%d.%d.%d", major, minor, patch);
#else
    snprintf(versionString, maxStrSize, "clang v%d.%d.%d", major, minor, patch);
#endif
#elif defined(__MINGW32__)
#ifdef __cplusplus
    snprintf(versionString, maxStrSize, "mingw32 v%d.%d (g++ v%d.%d)", __MINGW32_MAJOR_VERSION, __MINGW32_MINOR_VERSION, __GNUC__, __GNUC_MINOR__);
#else
    snprintf(versionString, maxStrSize, "mingw32 v%d.%d (gcc v%d.%d)", __MINGW32_MAJOR_VERSION, __MINGW32_MINOR_VERSION, __GNUC__, __GNUC_MINOR__);
#endif
#elif defined(__GNUC__)
#ifdef __cplusplus
    snprintf(versionString, maxStrSize, "g++ v%d.%d", __GNUC__, __GNUC_MINOR__);
#else
    snprintf(versionString, maxStrSize, "gcc v%d.%d", __GNUC__, __GNUC_MINOR__);
#endif
#elif defined(_MSC_VER)
    // _MSC_VER is e.g. 1944 for 19.44 (VS 2022 / 2026 toolsets).
#ifdef __cplusplus
    snprintf(versionString, maxStrSize, "MSVC C++ v%u.%u", (unsigned)(_MSC_VER / 100), (unsigned)(_MSC_VER % 100));
#else
    snprintf(versionString, maxStrSize, "MSVC v%u.%u", (unsigned)(_MSC_VER / 100), (unsigned)(_MSC_VER % 100));
#endif
#else
    snprintf(versionString, maxStrSize, "UNKNOWN");
#endif
    return versionString;
}

const char *BAE_GetFeatureString()
{
    static char featBuf[1024];
    featBuf[0] = '\0';
    bool first = TRUE;

    // Debugging
#if _DEBUG == TRUE
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
    char audio[64];
    snprintf(audio, sizeof(audio), "SDL (v%d.%d.%d)", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
#elif (X_PLATFORM == X_SDL3)
    char audio[64];
    snprintf(audio, sizeof(audio), "SDL (v%d.%d.%d)", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
#elif (X_PLATFORM == X_RAYLIB)
    const char *audio = "raylib";
#elif (X_PLATFORM == X_WIN95)
    const char *audio = "DirectSound";
#elif (X_PLATFORM == X_DUMMY)
    const char *audio = "None (Dummy)";
#elif (X_PLATFORM == X_ANDROID)
    const char *audio = "Android Subsystem";    
#elif (X_PLATFORM == X_FOOBAR2000_PLUGIN)
    const char *audio = "Foobar2000";
#else
    const char *audio = NULL;
#endif
    if (audio[0] != '\0')
    {
        snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", audio);
        first = FALSE;
    }

    // Built-in patches
#if _BUILT_IN_PATCHES == TRUE
    const char *patches = "Built-in Patches";
    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", patches);
    first = FALSE;
#endif

#if SUPPORT_KARAOKE == TRUE
    const char *karaoke = "Karaoke Support";
    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", karaoke);
    first = FALSE;
#endif

#if USE_RMI_SUPPORT == TRUE
    const char *rmi = "RMI Support";
    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", rmi);
    first = FALSE;
#endif
        
#if USE_XMF_SUPPORT == TRUE
    const char *xmf = "XMF Support";
    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", xmf);
    first = FALSE;
#endif

#if USE_ZMF_SUPPORT == TRUE
    const char *zmf = "ZMF Support";
    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", zmf);
    first = FALSE;
#endif

#if USE_MTHC_SUPPORT == TRUE
    const char *mthc = "Compressed MIDI (MThc)";
#else
    const char *mthc = NULL;
#endif

#if USE_ADP_SUPPORT == TRUE
    const char *adp = "ADP";
#else
    const char *adp = NULL;
#endif

#if USE_RETRO_RINGTONE_SUPPORT == TRUE
    const char *rtx = "Monophonic Ringtones";
#else
    const char *rtx = NULL;
#endif

#if DISABLE_NOKIA_PATCH == FALSE
    const char *nokia_patch = "Mute Ring Instrument";
#else
    const char *nokia_patch = NULL;
#endif

if ((rtx && rtx[0]) || (adp && adp[0]) || (mthc && mthc[0]) || (nokia_patch && nokia_patch[0])) {
    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", "Nokia Features (");

    if (rtx && rtx[0])
    {
        snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : "", rtx);
        first = FALSE;
    }

    if (adp && adp[0])
    {
        snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : (rtx && rtx[0]) ? ", " : "", adp);
        first = FALSE;
    }

    if (mthc && mthc[0])
    {
        snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : (rtx && rtx[0]) || (adp && adp[0]) ? ", " : "", mthc);
        first = FALSE;
    }

    if (nokia_patch && nokia_patch[0])
    {
        snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : (rtx && rtx[0]) || (adp && adp[0]) || (mthc && mthc[0]) ? ", " : "", nokia_patch);
        first = FALSE;
    }

    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), ")");
}

#if USE_QOA_SUPPORT == TRUE
    const char *qoa = "QOA (Quite OK Audio) Support";
    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", qoa);
    first = FALSE;
#endif

#if USE_ADX_SUPPORT == TRUE
    const char *adx = "CRI ADX Support";
    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", adx);
    first = FALSE;
#endif

#if USE_WMA_SUPPORT == TRUE
    const char *wma = "Windows Media Audio (WMA v1/v2/v7/v8/v9) Support";
    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", wma);
    first = FALSE;
#endif

#if USE_NATIVE_DLS == TRUE
    const char *native_dls = "DLS Support";
    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", native_dls);
    first = FALSE;
#endif


    // SF2 support
#if USE_SF2_SUPPORT == TRUE
    #if USE_VORBIS_DECODER == TRUE
      char *sf2supp = "SF2/SF3/SFO Support";
    #else
      char *sf2supp = "SF2 Support";
    #endif
    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", sf2supp);
    first = FALSE;
#endif

    // Playlist support
#if SUPPORT_PLAYLIST == TRUE
    const char *playlist = "Playlist Support";
    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", playlist);
    first = FALSE;
#endif

    // MIDI hardware
#if SUPPORT_MIDI_HW == TRUE
    const char *midi = "MIDI Hardware Support";
    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", midi);
    first = FALSE;
#endif

    // MP3 support

#if USE_MPEG_DECODER == TRUE && USE_MPEG_ENCODER == TRUE
    const char *lame_version = get_lame_version();
    static char mp3[64];
    snprintf(mp3, sizeof(mp3), "Full MP3 Support (LAME v%s)", lame_version);
#elif USE_MPEG_DECODER == TRUE && USE_MPEG_ENCODER != TRUE
    const char *mp3 = "MP3 Decoder Support";
#elif USE_MPEG_DECODER != TRUE && USE_MPEG_ENCODER == TRUE
    const char *lame_version = get_lame_version();
    static char mp3[64];
    snprintf(mp3, sizeof(mp3), "MP3 Encoder Support (LAME v%s)", lame_version);
#endif

#if USE_MPEG_DECODER == TRUE || USE_MPEG_ENCODER == TRUE
    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", mp3);
    first = FALSE;
#endif

    // FLAC support
#if USE_FLAC_DECODER == TRUE && USE_FLAC_ENCODER == TRUE
    static char flac[64];
    snprintf(flac, sizeof(flac), "Full FLAC Support (v%s)", FLAC__VERSION_STRING );
#elif USE_FLAC_DECODER == TRUE && USE_FLAC_ENCODER != TRUE
    static char flac[64];
    snprintf(flac, sizeof(flac), "FLAC Decoder Support (v%s)", FLAC__VERSION_STRING);
#elif USE_FLAC_DECODER != TRUE && USE_FLAC_ENCODER == TRUE
    static char flac[64];
    snprintf(flac, sizeof(flac), "FLAC Encoder Support (v%s)", FLAC__VERSION_STRING);
#endif

#if USE_FLAC_DECODER == TRUE || USE_FLAC_ENCODER == TRUE
    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", flac);
    first = FALSE;
#endif

    // Vorbis support
#if USE_VORBIS_DECODER == TRUE && USE_VORBIS_ENCODER == TRUE
    const char *vorbis_version_full = vorbis_version_string();
    const char *vorbis_p = strrchr(vorbis_version_full, ' ');
    const char *vorbis_version = vorbis_p ? vorbis_p + 1 : vorbis_version_full;
    static char vorbis[64];
    snprintf(vorbis, sizeof(vorbis), "Full Vorbis Support (v%s)", vorbis_version);
#elif USE_VORBIS_DECODER == TRUE && USE_VORBIS_ENCODER != TRUE
    const char *vorbis_version_full = vorbis_version_string();
    const char *vorbis_p = strrchr(vorbis_version_full, ' ');
    const char *vorbis_version = vorbis_p ? vorbis_p + 1 : vorbis_version_full;
    static char vorbis[64];
    snprintf(vorbis, sizeof(vorbis), "Vorbis Decoder Support (v%s)", vorbis_version);
#elif USE_VORBIS_DECODER != TRUE && USE_VORBIS_ENCODER == TRUE
    const char *vorbis_version_full = vorbis_version_string();
    const char *vorbis_p = strrchr(vorbis_version_full, ' ');
    const char *vorbis_version = vorbis_p ? vorbis_p + 1 : vorbis_version_full;
    static char vorbis[64];
    snprintf(vorbis, sizeof(vorbis), "Vorbis Encoder Support (v%s)", vorbis_version);
#endif

#if USE_VORBIS_DECODER == TRUE || USE_VORBIS_ENCODER == TRUE
    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", vorbis);
    first = FALSE;
#endif

    // If nothing was added, return an empty string
    if (featBuf[0] == '\0')
        featBuf[0] = '\0';

    // Opus support
#if USE_OPUS_DECODER == TRUE && USE_OPUS_ENCODER == TRUE
    const char *opus_version_full = opus_get_version_string();
    const char *opus_p = strrchr(opus_version_full, ' ');
    const char *opus_version = opus_p ? opus_p + 1 : opus_version_full;
    static char opus[64];
    snprintf(opus, sizeof(opus), "Full Opus Support (v%s)", opus_version);
#elif USE_OPUS_DECODER == TRUE && USE_OPUS_ENCODER != TRUE
    const char *opus_version_full = opus_get_version_string();
    const char *opus_p = strrchr(opus_version_full, ' ');
    const char *opus_version = opus_p ? opus_p + 1 : opus_version_full;
    static char opus[64];
    snprintf(opus, sizeof(opus), "Opus Decoder Support (v%s)", opus_version);
#elif USE_OPUS_DECODER != TRUE && USE_OPUS_ENCODER == TRUE
    const char *opus_version_full = opus_get_version_string();
    const char *opus_p = strrchr(opus_version_full, ' ');
    const char *opus_version = opus_p ? opus_p + 1 : opus_version_full;
    static char opus[64];
    snprintf(opus, sizeof(opus), "Opus Encoder Support (v%s)", opus_version);
#endif

#if USE_OPUS_DECODER == TRUE || USE_OPUS_ENCODER == TRUE
    snprintf(featBuf + strlen(featBuf), sizeof(featBuf) - strlen(featBuf), "%s%s", first ? "" : ", ", opus);
    first = FALSE;
#endif

    // If nothing was added, return an empty string
    if (featBuf[0] == '\0')
        featBuf[0] = '\0';

    return featBuf;
}

const char *BAE_GetCurrentCPUArchitecture()
{ // Get current architecture, detects many architectures. Coded by Freak.
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
                      // the dual-CPU version of NeoBAE, this will be only a reference.
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

/* Bind TLS current mixer from a BAEMixer; returns previous for restore. */
static GM_Mixer *PV_BindBAEMixer(BAEMixer mixer)
{
    if (mixer && mixer->pMixer)
        return GM_SetCurrentMixer(mixer->pMixer);
    return MusicGlobals;
}

struct sBAESong
{
    int32_t mID;
    BAEMixer mixer;
    GM_Song *pSong; // Don't dereference pSong, since if you are running
                    // the dual-CPU version of NeoBAE, this will be only a reference.

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

    int16_t mVolume;
    int mRouteBus;
    BAE_BOOL mAutoBuzz;
    BAE_BOOL mAutoFlash;
    BAE_BOOL mHasEmbeddedBank;
    char *mTitle;

    // Saved mixer state for per-song engine config override/restore
#if BAE_CLASSIC_CHORUS
    bool mSavedClassicChorus;
    bool mHasSavedClassicChorus;
#endif
#if BAE_FIX_SPAN_DC
    bool mSavedFixSpanDC;
    bool mHasSavedFixSpanDC;
#endif
    // Per-song velocity curve override
    VelocityCurveType mSavedVelocityCurve;
    bool mHasSavedVelocityCurve;
};

struct sBAESound
{
    int32_t mID;
    BAEMixer mixer;
    GM_Waveform *pWave; // Don't dereference pWave, since if you are running
                        // the dual-CPU version of NeoBAE, this will be only a reference.

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

    // Optional owned memory backing for BAEStream_SetupMemory
    XPTR mMemoryData;
    uint32_t mMemoryDataSize;
#if !TRACKING
    BAE_BOOL mValid;
#endif
};
#endif

// NeoBAE.c globals
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

#if USE_NATIVE_DLS == TRUE
/* DLS banks are not HSB tokens; keep a separate SHA1→friendly lookup. */
static char g_dlsBankSha1[41];
static const char *g_dlsBankFriendly;

static void PV_ClearDlsBankFriendly(void)
{
    g_dlsBankSha1[0] = '\0';
    g_dlsBankFriendly = NULL;
}

static const BankInfo *PV_LookupBankInfoByMemory(const void *pMemory, uint32_t memorySize, char outHex[41])
{
    unsigned char digest[20];
    char hex[41];
    static const char *hexmap = "0123456789abcdef";
    int i;

    if (outHex)
    {
        outHex[0] = '\0';
    }
    /* Same soft cap as HSB file hashing; skip huge blobs. */
    if (!pMemory || memorySize == 0 || memorySize >= (32u * 1024u * 1024u))
    {
        return NULL;
    }

    sha1mini((const unsigned char *)pMemory, memorySize, digest);
    for (i = 0; i < 20; i++)
    {
        hex[i * 2] = hexmap[digest[i] >> 4];
        hex[i * 2 + 1] = hexmap[digest[i] & 15];
    }
    hex[40] = '\0';
    if (outHex)
    {
        memcpy(outHex, hex, 41);
    }
    for (i = 0; i < kBankCount; i++)
    {
        if (strcmp(hex, kBanks[i].sha1) == 0)
        {
            return &kBanks[i];
        }
    }
    return NULL;
}

static void PV_RegisterDlsBankFriendly(const void *pMemory, uint32_t memorySize)
{
    char hex[41];
    const BankInfo *info;

    PV_ClearDlsBankFriendly();
    info = PV_LookupBankInfoByMemory(pMemory, memorySize, hex);
    if (hex[0])
    {
        strncpy(g_dlsBankSha1, hex, 40);
        g_dlsBankSha1[40] = '\0';
    }
    if (info)
    {
        g_dlsBankFriendly = info->name;
    }
}
#endif /* USE_NATIVE_DLS */

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

BAEResult BAEMixer_GetDLSBankFriendlyName(BAEMixer mixer, char *outName, uint32_t outNameSize)
{
    if (!outName || outNameSize == 0)
        return BAE_PARAM_ERR;
    outName[0] = '\0';
    if (!mixer)
        return BAE_NULL_OBJECT;
#if USE_NATIVE_DLS == TRUE
    if (!g_dlsBankFriendly)
        return BAE_RESOURCE_NOT_FOUND;
    strncpy(outName, g_dlsBankFriendly, outNameSize - 1);
    outName[outNameSize - 1] = '\0';
    return BAE_NO_ERROR;
#else
    return BAE_UNSUPPORTED_FORMAT;
#endif
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
/* Mixer that owns the active file export — needed so Stop can rebind TLS /
 * reconnect hardware on a worker thread where MusicGlobals is otherwise NULL. */
static BAEMixer mWritingMixer = NULL;

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
static bool PV_XFileHasModernCodecSamples(XFILE fileRef);
static void PV_EnableZmfCompatIfMixerHasZsb(BAESong song);
static GM_Waveform *PV_ReadADPIntoMemoryFromMemory(void *pMemoryFile, uint32_t memoryFileSize, OPErr *pErr);

static BAE_FIXED PV_CalculateTimeDeltaForFade(
    BAE_FIXED sourceVolume,
    BAE_FIXED destVolume,
    BAE_FIXED timeInMiliseconds);

// song related
static BAEResult PV_BAESong_InitLiveSong(BAESong song, BAE_BOOL addToMixer);

static void PV_BAESong_Stop(BAESong song, BAE_BOOL startFade);
static void PV_BAESong_Unload(BAESong song);
static void PV_ApplySongEngineConfig(BAESong song);
static void PV_RestoreSongEngineConfig(BAESong song);
static void PV_BAESong_SetCallback(BAESong song, BAE_SongCallbackPtr pCallback,
                                   void *callbackReference);

static BAETerpMode PV_TranslateTerpModeToBAETerpMode(TerpMode tm_in);

// sound related
static void PV_BAESound_SetCallback(BAESound sound, BAE_SoundCallbackPtr pCallback,
                                    void *callbackReference);
static void PV_BAESound_Unload(BAESound sound);
static void PV_BAESound_Stop(BAESound sound, BAE_BOOL startFade);

/* Map BAESound user volume (1.0 = digital full scale) to GM sample NoteVolume.
 *
 * The 16-bit serve path applies extra >>4 scaling plus final OUTPUT_SCALAR (>>9).
 * With default effectsVolume (8×), the historical MAX_NOTE_VOLUME mapping for
 * user volume 1.0 lands ~6 dB below full scale. Scale by 2 so 1.0 ≈ FS. */
#ifndef BAE_SOUND_UNITY_TO_NOTE_SCALE
#define BAE_SOUND_UNITY_TO_NOTE_SCALE 2
#endif

static int32_t PV_BAESound_UserVolumeToNoteVolume(BAE_UNSIGNED_FIXED userVol)
{
    int32_t v = (int32_t)UNSIGNED_FIXED_TO_LONG_ROUNDED(
        userVol * (MAX_NOTE_VOLUME * BAE_SOUND_UNITY_TO_NOTE_SCALE));
    if (v < 0)
        v = 0;
    if (v > 32767)
        v = 32767;
    return v;
}

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

#if USE_ADP_SUPPORT == TRUE
static GM_Waveform *PV_ReadADPIntoMemoryFromMemory(void *pMemoryFile, uint32_t memoryFileSize, OPErr *pErr)
{
    BAEAdpDecodedAudio decodedAudio;
    GM_Waveform *wave;

    memset(&decodedAudio, 0, sizeof(decodedAudio));
    wave = NULL;
    if (BAEAdp_DecodeMemoryToPCM16Mono(pMemoryFile, memoryFileSize, NULL, &decodedAudio) != 0)
    {
        if (pErr)
        {
            *pErr = BAD_FILE;
        }
        return NULL;
    }    
    if (decodedAudio.frameCount > 0xFFFFFFFFu)
    {
        BAEAdp_FreeDecodedAudio(&decodedAudio);
        if (pErr)
        {
            *pErr = BAD_FILE;
        }
        return NULL;
    }

    wave = GM_ReadRawAudioIntoMemoryFromMemory(decodedAudio.samples,
                                               (uint32_t)decodedAudio.frameCount,
                                               16,
                                               1,
                                               LONG_TO_UNSIGNED_FIXED(decodedAudio.sampleRate),
                                               0,
                                               0,
                                               pErr);
    BAEAdp_FreeDecodedAudio(&decodedAudio);
    return wave;
}
#endif

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
    REVERB_TYPE_11,
    REVERB_TYPE_12,
    REVERB_TYPE_13,
    REVERB_TYPE_14,
    REVERB_TYPE_15,
    REVERB_TYPE_16,
    REVERB_TYPE_17,
    REVERB_TYPE_18,
    REVERB_TYPE_19};
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
    BAE_REVERB_TYPE_11,
    BAE_REVERB_TYPE_12,
    BAE_REVERB_TYPE_13,
    BAE_REVERB_TYPE_14,
    BAE_REVERB_TYPE_15,
    BAE_REVERB_TYPE_16,
    BAE_REVERB_TYPE_17,
    BAE_REVERB_TYPE_18,
    BAE_REVERB_TYPE_19};
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
    if (igorVerb >= MAX_REVERB_TYPES) {
        r = REVERB_TYPE_18;
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
bool BAESong_IsSF2Song(BAESong song)
{
    if (!song || !song->pSong)
        return FALSE;
    return GM_IsSF2Song(song->pSong);
}
#endif

#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDLITE == TRUE
BAEResult BAESong_EnableSF2(BAESong song, BAE_BOOL enable)
{
    if (!song || !song->pSong)
    {
        return BAE_NULL_OBJECT;
    }

    return BAE_TranslateOPErr(GM_EnableSF2ForSong(song->pSong, enable ? TRUE : FALSE));
}
#endif

#if USE_NATIVE_DLS == TRUE
bool BAESong_IsDLSSong(BAESong song)
{
    if (!song || !song->pSong)
        return FALSE;
    return GM_IsDLSSong(song->pSong);
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
#if USE_OPUS_DECODER == TRUE || USE_OPUS_ENCODER == TRUE
    case BAE_OPUS_TYPE:
        haeFileType = FILE_OPUS_TYPE;
        break;
#endif
#if USE_QOA_SUPPORT == TRUE
    case BAE_QOA_TYPE:
        haeFileType = FILE_QOA_TYPE;
        break;
#endif
#if USE_WMA_SUPPORT == TRUE
    case BAE_WMA_TYPE:
        haeFileType = FILE_WMA_TYPE;
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
// Global default velocity curve (0..5). Applied to songs when they are created or loaded.
static int g_defaultVelocityCurve = 1;

BAEResult BAE_SetDefaultVelocityCurve(int curveType)
{
    if (curveType < 0)
        curveType = 0;
    if (curveType > 5)
        curveType = 5;
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

BAEResult BAE_SetSpanDCFix(BAE_BOOL enable)
{
#if BAE_FIX_SPAN_DC
    GM_Mixer *pMixer = MusicGlobals;
    if (!pMixer)
        return BAE_NOT_SETUP;
    pMixer->fixSpanDC = (bool)enable;
    return BAE_NO_ERROR;
#else
    (void)enable;
    return BAE_NOT_SETUP;
#endif
}

BAEResult BAE_GetSpanDCFix(BAE_BOOL *outEnable)
{
    if (!outEnable)
        return BAE_PARAM_ERR;
#if BAE_FIX_SPAN_DC
    GM_Mixer *pMixer = MusicGlobals;
    if (!pMixer)
        return BAE_NOT_SETUP;
    *outEnable = (BAE_BOOL)pMixer->fixSpanDC;
    return BAE_NO_ERROR;
#else
    *outEnable = FALSE;
    return BAE_NOT_SETUP;
#endif
}

BAEResult BAE_SetClassicChorus(BAE_BOOL enable)
{
#if BAE_CLASSIC_CHORUS
    GM_Mixer *pMixer = MusicGlobals;
    if (!pMixer)
        return BAE_NOT_SETUP;
    pMixer->classicChorus = (bool)enable;
    return BAE_NO_ERROR;
#else
    (void)enable;
    return BAE_NOT_SETUP;
#endif
}

BAEResult BAE_GetClassicChorus(BAE_BOOL *outEnable)
{
    if (!outEnable)
        return BAE_PARAM_ERR;
#if BAE_CLASSIC_CHORUS
    GM_Mixer *pMixer = MusicGlobals;
    if (!pMixer)
        return BAE_NOT_SETUP;
    *outEnable = (BAE_BOOL)pMixer->classicChorus;
    return BAE_NO_ERROR;
#else
    *outEnable = FALSE;
    return BAE_NOT_SETUP;
#endif
}

BAEResult BAE_SetClassicLFO(BAE_BOOL enable)
{
    GM_Mixer *pMixer = MusicGlobals;
    if (!pMixer)
        return BAE_NOT_SETUP;
    pMixer->classicLFO = (bool)enable;
    return BAE_NO_ERROR;
}

BAEResult BAE_GetClassicLFO(BAE_BOOL *outEnable)
{
    if (!outEnable)
        return BAE_PARAM_ERR;
    GM_Mixer *pMixer = MusicGlobals;
    if (!pMixer)
        return BAE_NOT_SETUP;
    *outEnable = (BAE_BOOL)pMixer->classicLFO;
    return BAE_NO_ERROR;
}

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)

BAEResult BAE_EnableChannelCapture(BAEMixer mixer, const char *outputDir)
{
    GM_Mixer *pMixer = (mixer && mixer->pMixer) ? mixer->pMixer : MusicGlobals;
    int ch;

    if (!pMixer)
        return BAE_NOT_SETUP;
    if (!outputDir || !outputDir[0])
        return BAE_PARAM_ERR;
    if (pMixer->channelCaptureEnabled)
        return BAE_NO_ERROR;

    size_t dirLen = strlen(outputDir);
    if (dirLen >= sizeof(pMixer->channelCaptureDir))
        return BAE_PARAM_ERR;

    strncpy(pMixer->channelCaptureDir, outputDir, sizeof(pMixer->channelCaptureDir) - 1);
    pMixer->channelCaptureDir[sizeof(pMixer->channelCaptureDir) - 1] = '\0';

#if defined(_MSC_VER)
    _mkdir(outputDir);
#elif defined(__MINGW32__)
    mkdir(outputDir);
#else
    mkdir(outputDir, 0755);
#endif

    pMixer->channelCaptureBufSamples = (pMixer->maxChunkSize + 64) * 2;
    size_t bufBytes = pMixer->channelCaptureBufSamples * sizeof(int32_t);

    pMixer->channelCaptureSnapshot = (int32_t *)malloc(bufBytes);
    if (!pMixer->channelCaptureSnapshot)
        return BAE_MEMORY_ERR;

    for (ch = 0; ch < 16; ch++)
    {
        pMixer->channelCaptureBuf[ch] = (int32_t *)calloc(1, bufBytes);
        if (!pMixer->channelCaptureBuf[ch])
        {
            for (int j = 0; j < ch; j++)
            {
                free(pMixer->channelCaptureBuf[j]);
                pMixer->channelCaptureBuf[j] = NULL;
            }
            free(pMixer->channelCaptureSnapshot);
            pMixer->channelCaptureSnapshot = NULL;
            return BAE_MEMORY_ERR;
        }
        pMixer->channelCaptureFiles[ch] = NULL;
    }

    pMixer->channelCaptureEnabled = true;
    return BAE_NO_ERROR;
}

BAEResult BAE_DisableChannelCapture(BAEMixer mixer)
{
    GM_Mixer *pMixer = MusicGlobals;
    int ch;

    (void)mixer;
    if (!pMixer)
        return BAE_NOT_SETUP;
    if (!pMixer->channelCaptureEnabled)
        return BAE_NO_ERROR;

    for (ch = 0; ch < 16; ch++)
    {
        if (pMixer->channelCaptureFiles[ch] != NULL)
        {
            PV_FinalizeChannelCaptureFile(ch);
            pMixer->channelCaptureFiles[ch] = NULL;
        }
        if (pMixer->channelCaptureBuf[ch] != NULL)
        {
            free(pMixer->channelCaptureBuf[ch]);
            pMixer->channelCaptureBuf[ch] = NULL;
        }
    }

    if (pMixer->channelCaptureSnapshot != NULL)
    {
        free(pMixer->channelCaptureSnapshot);
        pMixer->channelCaptureSnapshot = NULL;
    }

    pMixer->channelCaptureEnabled = false;
    return BAE_NO_ERROR;
}

BAEResult BAE_GetChannelCaptureActive(BAEMixer mixer, bool *outActive)
{
    GM_Mixer *pMixer = MusicGlobals;
    (void)mixer;
    if (!pMixer || !outActive)
        return BAE_NOT_SETUP;
    memcpy(outActive, pMixer->channelCaptureActive, sizeof(pMixer->channelCaptureActive));
    return BAE_NO_ERROR;
}

#endif

BAEResult BAESong_GetEngineConfig(BAESong song, uint32_t *outFlags)
{
    uint32_t flags;
    
    if (!outFlags)
        return BAE_PARAM_ERR;
    if (!song || song->mID != OBJECT_ID || !song->pSong)
        return BAE_NULL_OBJECT;
    
    flags = song->pSong->engineConfigFlags;
    // Only trust flags if container is ZMF; old RMF files may have garbage
    if (!(flags & SONG_CONFIG_CONTAINER_IS_ZMF))
    {
        *outFlags = 0;  // not a ZMF file, discard flags
    }
    else
    {
        // ZMF file, clamp to valid bits to filter out any remaining garbage
        *outFlags = flags & SONG_CONFIG_VALID_BITS_MASK;
    }
    return BAE_NO_ERROR;
}

BAEResult BAESong_SetZmfCompatibilityMode(BAESong song, BAE_BOOL enable)
{
    if (!song || song->mID != OBJECT_ID || !song->pSong)
    {
        return BAE_NULL_OBJECT;
    }
    if (enable)
    {
        song->pSong->engineConfigFlags |= SONG_CONFIG_CONTAINER_IS_ZMF;
    }
    else
    {
        song->pSong->engineConfigFlags &= ~((uint32_t)SONG_CONFIG_CONTAINER_IS_ZMF);
    }
    return BAE_NO_ERROR;
}

static bool PV_XFileIsZsbContainer(XFILE fileRef)
{
#if USE_ZMF_SUPPORT == TRUE
    XFILERESOURCEMAP map;
    int32_t savedPos;

    if (!fileRef)
    {
        return FALSE;
    }
    if (XFileGetForceZsbFeatures(fileRef))
    {
        return TRUE;
    }
    savedPos = XFileGetPosition(fileRef);
    if (savedPos < 0)
    {
        return FALSE;
    }
    if (XFileSetPosition(fileRef, 0L) != 0)
    {
        return FALSE;
    }
    if (XFileRead(fileRef, (XPTR)&map, (int32_t)sizeof(XFILERESOURCEMAP)) != 0)
    {
        (void)XFileSetPosition(fileRef, savedPos);
        return FALSE;
    }
    (void)XFileSetPosition(fileRef, savedPos);
    return (XGetLong(&map.mapID) == XFILERESOURCE_ZMF_ID) ? TRUE : FALSE;
#else
    (void)fileRef;
    return FALSE;
#endif
}

/* MIDI + ZSB: SONG_TYPE_SMS merge zeros engineConfigFlags, so short loops and
 * other ZMF gates would stay off unless we re-tag from the open bank. */
static void PV_EnableZmfCompatIfMixerHasZsb(BAESong song)
{
#if USE_ZMF_SUPPORT == TRUE
    int16_t i;

    if (!song || song->mID != OBJECT_ID || !song->pSong || !song->mixer)
    {
        return;
    }
    for (i = 0; i < song->mixer->numPatchFiles; i++)
    {
        if (PV_XFileIsZsbContainer(song->mixer->pPatchFiles[i]))
        {
            song->pSong->engineConfigFlags |= SONG_CONFIG_CONTAINER_IS_ZMF;
            return;
        }
    }
#else
    (void)song;
#endif
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

BAEResult BAEMixer_MakeCurrent(BAEMixer mixer)
{
    if (!mixer)
        return BAE_NULL_OBJECT;
    if (!mixer->pMixer)
        return BAE_NOT_SETUP;
    GM_SetCurrentMixer(mixer->pMixer);
    return BAE_NO_ERROR;
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
BAEResult BAEMixer_SetDefaultReverb(BAEMixer mixer, BAEReverbType verb)
{
#if REVERB_USED != REVERB_DISABLED
    if (!mixer || !mixer->pMixer)
        return BAE_PARAM_ERR;
    {
        GM_Mixer *saved = GM_SetCurrentMixer(mixer->pMixer);
        GM_SetReverbType(BAE_TranslateFromBAEReverb(verb));
        GM_SetCurrentMixer(saved);
    }
    return BAE_NO_ERROR;
#else
    return BAE_NOT_SETUP;
#endif
}

BAEResult BAEMixer_GetDefaultReverb(BAEMixer mixer, BAEReverbType *pOutResult)
{
#if REVERB_USED != REVERB_DISABLED
    if (!mixer || !mixer->pMixer || !pOutResult)
        return BAE_PARAM_ERR;
    {
        GM_Mixer *saved = GM_SetCurrentMixer(mixer->pMixer);
        ReverbMode r = GM_GetReverbType();
        *pOutResult = BAE_TranslateToBAEReverb(r);
        GM_SetCurrentMixer(saved);
    }
    return BAE_NO_ERROR;
#else
    return BAE_NOT_SETUP;
#endif
}

BAEResult BAEMixer_SetEQEnabled(BAEMixer mixer, BAE_BOOL enabled)
{
    if (mixer && mixer->pMixer)
    {
        BAE_AcquireMutex(mixer->mLock);
        if (mixer->pMixer->eq.enabled != (enabled ? true : false))
        {
            mixer->pMixer->eq.enabled = (enabled ? true : false);
            if (mixer->pMixer->eq.enabled)
            {
                PV_ClearEQState(mixer->pMixer);
                PV_UpdateEQCoefficients(mixer->pMixer);
            }
        }
        BAE_ReleaseMutex(mixer->mLock);
        return BAE_NO_ERROR;
    }
    return BAE_PARAM_ERR;
}

BAEResult BAEMixer_GetEQEnabled(BAEMixer mixer, BAE_BOOL *pOutEnabled)
{
    if (mixer && mixer->pMixer && pOutEnabled)
    {
        BAE_AcquireMutex(mixer->mLock);
        *pOutEnabled = mixer->pMixer->eq.enabled ? TRUE : FALSE;
        BAE_ReleaseMutex(mixer->mLock);
        return BAE_NO_ERROR;
    }
    return BAE_PARAM_ERR;
}

BAEResult BAEMixer_SetEQGain(BAEMixer mixer, uint32_t bandIndex, float gaindB)
{
    if (mixer && mixer->pMixer && bandIndex < BAE_EQ_BANDS)
    {
        if (gaindB < -12.0f) gaindB = -12.0f;
        if (gaindB > 12.0f) gaindB = 12.0f;
        
        BAE_AcquireMutex(mixer->mLock);
        if (mixer->pMixer->eq.gains[bandIndex] != gaindB)
        {
            mixer->pMixer->eq.gains[bandIndex] = gaindB;
            PV_UpdateEQCoefficients(mixer->pMixer);
        }
        BAE_ReleaseMutex(mixer->mLock);
        return BAE_NO_ERROR;
    }
    return BAE_PARAM_ERR;
}

BAEResult BAEMixer_GetEQGain(BAEMixer mixer, uint32_t bandIndex, float *pOutGaindB)
{
    if (mixer && mixer->pMixer && bandIndex < BAE_EQ_BANDS && pOutGaindB)
    {
        BAE_AcquireMutex(mixer->mLock);
        *pOutGaindB = mixer->pMixer->eq.gains[bandIndex];
        BAE_ReleaseMutex(mixer->mLock);
        return BAE_NO_ERROR;
    }
    return BAE_PARAM_ERR;
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
    case BAE_SINC_INTERPOLATION:
#if LOOPS_USED == U3232_LOOPS
        theTerp = E_SINC_INTERPOLATION_U3232;
        break;
#endif
        /* Fall back to linear on mixers that never got U3232. */
        /* FALLTHROUGH */
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
            case BAE_SINC_INTERPOLATION:
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
                am &= ~BAE_USE_16; // 8 bit (do not drop BAE_USE_STEREO)
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
                        /* Pass GM_Mixer* so platform audio callbacks bind TLS via
                         * BAE_BuildMixerSlice(threadContext=userdata). */
                        theErr = GM_ResumeGeneralSound(mixer->pMixer);
                        if (theErr == NO_ERR)
                        {
                            mixer->audioEngaged = TRUE;
                        }
                    }
                    else if (mixer->pMixer)
                    {
                        /* Pull/render mixers: unpause engine without acquiring hardware.
                         * systemPaused blocks PV_ProcessSampleFrame, so Open(FALSE)
                         * must still be able to BAE_BuildMixerSlice. */
                        mixer->pMixer->systemPaused = FALSE;
                        mixer->pMixer->sequencerPaused = FALSE;
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
            GM_Mixer *closingMixer = mixer->pMixer;
            GM_Mixer *savedMixer = GM_SetCurrentMixer(closingMixer);
#if USE_CALLBACKS
            GM_SetAudioTask(NULL, NULL);
#endif
            if (mixer->audioEngaged)
            {
                // Close up sound manager BEFORE releasing memory!
                GM_StopHardwareSoundManager(NULL);
                mixer->audioEngaged = FALSE;
            }
            GM_FinisGeneralSound(NULL, closingMixer);
            mixer->pMixer = NULL;
            /* Finis clears TLS if it pointed at closingMixer; restore prior if still valid. */
            if (MusicGlobals == NULL && savedMixer != closingMixer)
                GM_SetCurrentMixer(savedMixer);
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
            GM_BankBalance_OnHsbBankAdded(newPatchFile);
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
            /* Reject IREZ (classic HSB) banks that contain modern-codec samples.
            ** Only ZREZ (ZSB) banks are permitted to embed FLAC, Vorbis, Opus, or QOA. */
            {
                XFILERESOURCEMAP mapHdr;
                XFileSetPosition(newPatchFile, 0L);
                if (XFileRead(newPatchFile, &mapHdr, (int32_t)sizeof(XFILERESOURCEMAP)) == 0)
                {
                    if (XGetLong(&mapHdr.mapID) == XFILERESOURCE_ID) /* IREZ, not ZREZ */
                    {
                        if (PV_XFileHasModernCodecSamples(newPatchFile))
                        {
                            debug_message("[Bank] IREZ bank rejected: contains FLAC/Vorbis/Opus/QOA sample(s) - upgrade to ZSB (.zsb)\n");
                            XFileClose(newPatchFile);
                            return BAE_UNSUPPORTED_FORMAT;
                        }
                    }
                }
            }
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
            /* Reject IREZ (classic HSB) banks that contain modern-codec samples.
            ** Only ZREZ (ZSB) banks are permitted to embed FLAC, Vorbis, Opus, or QOA. */
            {
                XFILERESOURCEMAP mapHdr;
                XFileSetPosition(newPatchFile, 0L);
                if (XFileRead(newPatchFile, &mapHdr, (int32_t)sizeof(XFILERESOURCEMAP)) == 0)
                {
                    if (XGetLong(&mapHdr.mapID) == XFILERESOURCE_ID) /* IREZ, not ZREZ */
                    {
                        if (PV_XFileHasModernCodecSamples(newPatchFile))
                        {
                            debug_message("[Bank] IREZ bank rejected: contains FLAC/Vorbis/Opus/QOA sample(s) - upgrade to ZSB (.zsb)\n");
                            XFileClose(newPatchFile);
                            return BAE_UNSUPPORTED_FORMAT;
                        }
                    }
                }
            }
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
                GM_BankBalance_OnHsbBankRemoved(patchFile);
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

#if USE_NATIVE_DLS == TRUE
BAEResult BAEMixer_LoadDLSBankFromMemory(BAEMixer mixer, void* pMemory, uint32_t memorySize)
{
    BAEResult err;
    if (mixer && mixer->pMixer)
    {
        char hex[41];
        const BankInfo *info = PV_LookupBankInfoByMemory(pMemory, memorySize, hex);
        bool knownMobileBAE = (info && (info->flags & BANKINFO_FLAG_MOBILEBAE) != 0);
        OPErr opErr;

        /* Bake articulations with quirks when bankinfo flags this as mobileBAE. */
        if (knownMobileBAE)
        {
            GM_DLS_BeginForcedQuirksLoad();
        }
        opErr = GM_LoadDLSFromMemory(mixer->pMixer, pMemory, memorySize);
        if (knownMobileBAE)
        {
            GM_DLS_EndForcedQuirksLoad();
        }
        err = BAE_TranslateOPErr(opErr);
        if (err == BAE_NO_ERROR)
        {
            PV_RegisterDlsBankFriendly(pMemory, memorySize);
            if (knownMobileBAE)
            {
                GM_DLS_MarkMainBankMobileBAE(mixer->pMixer);
            }
        }
        else
        {
            PV_ClearDlsBankFriendly();
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

BAEResult BAEMixer_LoadDLSBankAsXMFOverlayFromMemory(BAEMixer mixer, void* pMemory, uint32_t memorySize)
{
    BAEResult err;
    if (mixer && mixer->pMixer)
    {
        OPErr opErr = GM_LoadDLSAsXMFOverlayFromMemory(mixer->pMixer, pMemory, memorySize);
        err = BAE_TranslateOPErr(opErr);
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

BAEResult BAEMixer_UnloadXMFDLSOverlayBank(BAEMixer mixer)
{
    if (!(mixer && mixer->pMixer))
    {
        return BAE_NULL_OBJECT;
    }

    GM_UnloadXMFDLSOverlay(mixer->pMixer);
    return BAE_NO_ERROR;
}

int BAEMixer_HasXMFDLSOverlayBank(BAEMixer mixer)
{
    if (!(mixer && mixer->pMixer))
    {
        return 0;
    }

    return (int)GM_DLS_HasXmfEmbeddedBank(mixer->pMixer);
}

int BAEMixer_HasEggsDLSBank(BAEMixer mixer)
{
    if (!(mixer && mixer->pMixer))
    {
        return 0;
    }
#if USE_NATIVE_DLS == TRUE
    return (int)GM_DLS_HasEggsBank(mixer->pMixer);
#else
    return 0;
#endif
}

int BAEMixer_HasMobileBAEDLSBank(BAEMixer mixer)
{
    if (!(mixer && mixer->pMixer))
    {
        return 0;
    }
#if USE_NATIVE_DLS == TRUE
    return (int)GM_DLS_HasMobileBAEBank(mixer->pMixer);
#else
    return 0;
#endif
}

int BAEMixer_HasMobileBAEMainBank(BAEMixer mixer)
{
    if (!(mixer && mixer->pMixer))
    {
        return 0;
    }
#if USE_NATIVE_DLS == TRUE
    return (int)GM_DLS_HasMobileBAEMainBank(mixer->pMixer);
#else
    return 0;
#endif
}

int BAEMixer_GetDLSBankLevel(BAEMixer mixer)
{
    if (!(mixer && mixer->pMixer))
    {
        return 0;
    }
#if USE_NATIVE_DLS == TRUE
    return GM_DLS_GetBankLevel(mixer->pMixer);
#else
    return 0;
#endif
}

BAEResult BAEMixer_LoadDLSBank(BAEMixer mixer, const char* filePath)
{
    BAEResult err;
    if (mixer && mixer->pMixer)
    {
        void *pAudioFile = NULL;
        uint32_t fileSize = 0;

        XFILENAME xfile;
        XConvertNativeFileToXFILENAME((void*)filePath, &xfile);
        err = BAE_TranslateOPErr(XGetFileAsData(&xfile, &pAudioFile, (int32_t*)&fileSize));
        if (err == BAE_NO_ERROR)
        {
            err = BAEMixer_LoadDLSBankFromMemory(mixer, pAudioFile, fileSize);
            XDisposePtr(pAudioFile);
        }
    }
    else
    {
        err = BAE_NULL_OBJECT;
    }
    return err;
}

BAEResult BAEMixer_UnloadDLSBank(BAEMixer mixer)
{
    if (mixer && mixer->pMixer)
    {
        GM_Mixer *pMixer = mixer->pMixer;
        PV_ClearDlsBankFriendly();

        // DLS and built-in banks are mutually exclusive render paths.
        // When unloading DLS, force channels back to GM routing so
        // subsequent built-in bank playback does not stay latched to DLS.
        for (int songIndex = 0; songIndex < MAX_SONGS; ++songIndex)
        {
            GM_Song *song = pMixer->pSongsToPlay[songIndex];
            if (!song)
            {
                continue;
            }

            for (int ch = 0; ch < MAX_CHANNELS; ++ch)
            {
                if (song->channelType[ch] == CHANNEL_TYPE_DLS)
                {
                    song->channelType[ch] = CHANNEL_TYPE_GM;
                }
            }
        }
        pMixer->isDLS = false;
#if USE_SF2_SUPPORT == TRUE
        pMixer->isSF2 = false;
#endif
        if (pMixer->pDLSSynth) {
            if (pMixer->pDLSSynth->banks[0]) {
                GM_UnloadDLSBank(pMixer->pDLSSynth->banks[0]);
                pMixer->pDLSSynth->banks[0] = NULL;
            }
            if (!pMixer->pDLSSynth->banks[1]) {
                GM_FinisDLSSynth(pMixer->pDLSSynth);
                pMixer->pDLSSynth = NULL;
            }
        }
        GM_BankBalance_OnDlsBanksChanged(pMixer);
    }
    return BAE_NO_ERROR;
}
#endif

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
        case BAE_SINC_INTERPOLATION:
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
    GM_Mixer *saved;
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        saved = PV_BindBAEMixer(mixer);
        GM_SetMasterVolume((int32_t)(UNSIGNED_FIXED_TO_LONG_ROUNDED(theVolume * MAX_MASTER_VOLUME)));
        GM_SetCurrentMixer(saved);
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
    GM_Mixer *saved;
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (outVolume)
        {
            saved = PV_BindBAEMixer(mixer);
            *outVolume = UNSIGNED_RATIO_TO_FIXED(GM_GetMasterVolume(), MAX_MASTER_VOLUME);
            GM_SetCurrentMixer(saved);
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

// BAEMixer_SetGlobalVolume()
// ------------------------------------
//
//
BAEResult BAEMixer_SetGlobalVolume(BAEMixer mixer, BAE_UNSIGNED_FIXED theVolume)
{
    GM_Mixer *saved;
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        saved = PV_BindBAEMixer(mixer);
        GM_SetGlobalVolume((int32_t)(UNSIGNED_FIXED_TO_LONG_ROUNDED(theVolume * MAX_MASTER_VOLUME)));
        GM_SetCurrentMixer(saved);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAEMixer_GetGlobalVolume()
// ------------------------------------
//
//
BAEResult BAEMixer_GetGlobalVolume(BAEMixer mixer, BAE_UNSIGNED_FIXED *outVolume)
{
    GM_Mixer *saved;
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (outVolume)
        {
            saved = PV_BindBAEMixer(mixer);
            *outVolume = UNSIGNED_RATIO_TO_FIXED(GM_GetGlobalVolume(), MAX_MASTER_VOLUME);
            GM_SetCurrentMixer(saved);
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

// BAEMixer_SetOutputGain()
// ------------------------------------
// Sets output gain as a percent (100 = normal, >100 = overdrive). Applied on the
// full mix bus (HSB+SF2+DLS) before the peak limiter.
//
BAEResult BAEMixer_SetOutputGain(BAEMixer mixer, int32_t gainPct)
{
    GM_Mixer *saved;
    if (!mixer) return BAE_TranslateOPErr(NULL_OBJECT);
    saved = PV_BindBAEMixer(mixer);
    GM_SetOutputGain(gainPct);
    GM_SetCurrentMixer(saved);
    return BAE_NO_ERROR;
}

BAEResult BAEMixer_GetOutputGain(BAEMixer mixer, int32_t *outGainPct)
{
    GM_Mixer *saved;
    if (!mixer) return BAE_TranslateOPErr(NULL_OBJECT);
    if (!outGainPct) return BAE_TranslateOPErr(PARAM_ERR);
    saved = PV_BindBAEMixer(mixer);
    *outGainPct = GM_GetOutputGain();
    GM_SetCurrentMixer(saved);
    return BAE_NO_ERROR;
}

BAEResult BAEMixer_SetSongNormalizeGain(BAEMixer mixer, int32_t gainPct)
{
    GM_Mixer *saved;
    if (!mixer) return BAE_TranslateOPErr(NULL_OBJECT);
    saved = PV_BindBAEMixer(mixer);
    GM_SetSongNormalizeGain(gainPct);
    GM_SetCurrentMixer(saved);
    return BAE_NO_ERROR;
}

BAEResult BAEMixer_GetSongNormalizeGain(BAEMixer mixer, int32_t *outGainPct)
{
    GM_Mixer *saved;
    if (!mixer) return BAE_TranslateOPErr(NULL_OBJECT);
    if (!outGainPct) return BAE_TranslateOPErr(PARAM_ERR);
    saved = PV_BindBAEMixer(mixer);
    *outGainPct = GM_GetSongNormalizeGain();
    GM_SetCurrentMixer(saved);
    return BAE_NO_ERROR;
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
    GM_Mixer *saved;

    err = NO_ERR;
    if (mixer)
    {
        if (outFrame)
        {
            saved = PV_BindBAEMixer(mixer);
            *outFrame = GM_GetAudioSampleFrame(pLeft, pRight);
            GM_SetCurrentMixer(saved);
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
#if USE_NATIVE_DLS == TRUE
            pStatus->voicesActive += GM_DLS_GetActiveVoiceCount(mixer->pMixer);
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
    bool isPaused;

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
            err = GM_ResumeGeneralSound(mixer->pMixer);
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
    GM_TestTone((bool)status);
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

    err = NO_ERR;
    if (mixer)
    {
        if (outMods)
        {
            if (mixer->pMixer)
            {
                /* Read mixer fields directly — do not use GM_Generate* helpers here.
                 * Those depend on TLS MusicGlobals, which is unbound on zefidi's
                 * export thread and left channel/bit-depth flags uninitialized. */
                theMods = 0;
                if (mixer->pMixer->generate16output)
                    theMods |= BAE_USE_16;
                if (mixer->pMixer->generateStereoOutput)
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

// BAEMixer_SetTerpModeLive()
// --------------------------------------
// Switch interpolation on a running mixer without touching the audio device,
// reverb or any playing songs. Only supported when all interpolation modes
// share one voice position representation (U3232 loops); otherwise returns
// BAE_NOT_SETUP and the caller should reopen the mixer instead.
//
BAEResult BAEMixer_SetTerpModeLive(BAEMixer mixer, BAETerpMode t)
{
    if (!mixer)
        return BAE_NULL_OBJECT;
    if (!mixer->pMixer)
        return BAE_NOT_SETUP;
    switch (t)
    {
    case BAE_DROP_SAMPLE:
    case BAE_2_POINT_INTERPOLATION:
    case BAE_LINEAR_INTERPOLATION:
    case BAE_SINC_INTERPOLATION:
        break;
    default:
        return BAE_PARAM_ERR;
    }
#if LOOPS_USED == U3232_LOOPS
    {
        GM_Mixer *pm = mixer->pMixer;
        TerpMode newTerp = PV_GetDefaultTerp(t);
        int32_t count;

        if (pm->interpolationMode != newTerp)
        {
            // force every voice to recompute its slice step for the new mode
            for (count = 0; count < (pm->MaxNotes + pm->MaxEffects); count++)
                pm->NoteEntry[count].NoteNextSize = 0;
            pm->interpolationMode = newTerp; // mixer picks its procs from this each frame
        }
        return BAE_NO_ERROR;
    }
#else
    return BAE_NOT_SETUP;
#endif
}

// BAEMixer_GetRate()
// --------------------------------------
//
//
BAEResult BAEMixer_GetRate(BAEMixer mixer, BAERate *outRate)
{
    OPErr err;

    err = NO_ERR;
    if (mixer)
    {
        if (outRate)
        {
            if (mixer->pMixer)
            {
                /* Direct field read — GM_GetRate() needs TLS MusicGlobals. */
                *outRate = (BAERate)mixer->pMixer->outputRate;
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
    int16_t song, mix, sound;

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
    int16_t song, mix, sound;

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
    int16_t song, mix, sound;

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
// ********************** parameter 1 added for NeoBAE *********************
// ********************** method name changed for NeoBAE conformance *******
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
    GM_Mixer *savedMixer = NULL;
    // begin block added for NeoBAE 11/29/00 tom
    BAEAudioModifiers theModifiers;
    BAERate theRate;
    // end block added for NeoBAE

#if DUMP_OUTPUTFILE
    fp = fopen("C:\\temp\\test.txt", "w");
    if (fp)
    {
        fprintf(fp, "BAEOutputMixer::StartOutputToFile dump\n");
    }
#endif

    if (!theMixer || !theMixer->pMixer)
        return BAE_NULL_OBJECT;

    /* Export often runs on a worker thread (zefidi / Android). Bind TLS so any
     * remaining MusicGlobals-dependent helpers see this mixer. */
    savedMixer = PV_BindBAEMixer(theMixer);

    // begin block added for NeoBAE  11/28/00  tom
    err = BAEMixer_GetModifiers(theMixer, &theModifiers);
    if (err != BAE_NO_ERROR)
    {
        GM_SetCurrentMixer(savedMixer);
        return err;
    }
    err = BAEMixer_GetRate(theMixer, &theRate);
    if (err != BAE_NO_ERROR)
    {
        GM_SetCurrentMixer(savedMixer);
        return err;
    }
    // end block added for NeoBAE

    theErr = (OPErr)BAE_NO_ERROR;

    // close old one first
    if (mWritingToFile)
    {
        // StopOutputToFile();
        BAEMixer_StopOutputToFile();
        /* Stop may leave TLS on a different mixer; re-bind without losing
         * the pre-Start TLS saved above. */
        (void)PV_BindBAEMixer(theMixer);
    }

    mWritingMixer = theMixer;
    mWriteToFileType = outputType;
    XConvertPathToXFILENAME(pAudioOutputFile, &theFile);

    /* Offline export builds slices via Service, not OpenSL. Clear systemPaused so
     * PV_ProcessSampleFrame actually advances (a prior DisengageAudio would leave
     * the mixer paused and export would hang / never move position). */
    if (MusicGlobals)
    {
        MusicGlobals->systemPaused = FALSE;
        MusicGlobals->sequencerPaused = FALSE;
    }

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
                uint32_t channels = (theModifiers & BAE_USE_STEREO) ? 2 : 1;
                // Preserve original slice size (typically ~11ms worth) to maintain correct sequencing tempo.
                // The MP3 encoder will internally accumulate multiple slices to reach a full 1152-frame MP3 frame.
                // helper translates compression enum to per-channel bitrate in bits/sec
                extern uint32_t BAE_TranslateMPEGTypeToBitrate(BAECompressionType ct);
                extern bool PV_RefillMPEGEncodeBuffer(void *buffer, void *userRef);
                mWritingEncoder = MPG_EncodeNewStream(BAE_TranslateMPEGTypeToBitrate(compressionType),
                                                      GM_ConvertFromOutputRateToRate((Rate)theRate),
                                                      channels,
                                                      mWritingDataBlock,
                                                      (uint32_t)(mWritingDataBlockSize / (sizeof(short) * channels)));
                if (mWritingEncoder)
                {
                    debug_message("audio: MPG_EncodeNewStream ok ch=");
                    char tmp[16];
                    XLongToStr(tmp, (int32_t)channels);
                    debug_message(tmp);
                    debug_message(" framesPerCall=");
                    XLongToStr(tmp, (int32_t)(mWritingDataBlockSize / (sizeof(short) * channels)));
                    debug_message(tmp);
                    debug_message(" rate=");
                    XLongToStr(tmp, (int32_t)GM_ConvertFromOutputRateToRate((Rate)theRate));
                    debug_message(tmp);
                    debug_message("\n");
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
                uint32_t channels = (theModifiers & BAE_USE_STEREO) ? 2 : 1;

                /* Initialize Vorbis encoder and write headers. */
                extern void *XInitVorbisEncoder(uint32_t sample_rate, uint32_t channels, float quality);
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

#if USE_OPUS_ENCODER == TRUE
    case BAE_OPUS_TYPE:
    {
        if (theModifiers & BAE_USE_16)
        {
            mWritingToFileReference = (void *)XFileOpenForWrite(&theFile, TRUE);
            if (mWritingToFileReference)
            {
                uint32_t channels = (theModifiers & BAE_USE_STEREO) ? 2 : 1;

                extern uint32_t BAE_TranslateOpusTypeToBitrate(BAECompressionType ct);
                extern void *XInitOpusEncoder(uint32_t sample_rate, uint32_t channels, uint32_t bitrate, uint32_t application);
                extern long XWriteOpusHeader(void *encoder_handle, XFILE output_file);

                uint32_t bitrate = BAE_TranslateOpusTypeToBitrate(compressionType);
                mWritingEncoder = XInitOpusEncoder(GM_ConvertFromOutputRateToRate((Rate)theRate), channels, bitrate, OPUS_APPLICATION_AUDIO);
                if (mWritingEncoder)
                {
                    (void)XWriteOpusHeader(mWritingEncoder, (XFILE)mWritingToFileReference);

                    GM_StopHardwareSoundManager(NULL);
                    mWritingToFile = TRUE;
                }
                else
                {
                    BAE_STDERR("audio: XInitOpusEncoder FAILED\n");
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

            // initialize GM_Waveform with one frame of data, so that GM_WriteFileFromMemory()
            // doesn't complain.

            if (!w)
            {
                theErr = MEMORY_ERR;
            }
            else
            {
                w->bitSize = (theModifiers /*iModifiers*/ & BAE_USE_16) ? 16 : 8;
                w->channels = (theModifiers /*iModifiers*/ & BAE_USE_STEREO) ? 2 : 1;
                w->sampledRate = LONG_TO_UNSIGNED_FIXED(GM_ConvertFromOutputRateToRate((Rate)theRate /*iRate*/));
                w->compressionType = C_NONE;
                w->waveFrames = 1;
                w->waveSize = (w->bitSize / 8) * (w->channels);
                w->theWaveform = XNewPtr(w->waveSize);
                if (!w->theWaveform)
                {
                    theErr = MEMORY_ERR;
                }
                else
                {
                    XSetMemory(w->theWaveform, w->waveSize, 0);
                    // Write out the header now, we'll add data to it in ServiceAudioOutputToFile()
                    theErr = GM_WriteFileFromMemory(&theFile, w, BAE_TranslateBAEFileType(outputType));
                }
            }

            GM_FreeWaveform(w);
            w = NULL;

            // Reopen the file and jump to the end, so we can add data to it later...
            mWritingToFileReference = theErr == NO_ERR ? (void *)XFileOpenForWrite(&theFile, FALSE) : NULL;
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
        mWritingMixer = NULL;
    }
    GM_SetCurrentMixer(savedMixer);
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
// ********************** method name changed for NeoBAE conformance ******

// Stop saving audio output to a file
void BAEMixer_StopOutputToFile(void)
{
#if USE_CREATION_API == TRUE
    GM_Mixer *savedMixer = NULL;
    GM_Mixer *hardwareMixer = NULL;
    int didBindWritingMixer = 0;

    if (mWritingMixer && mWritingMixer->pMixer)
    {
        savedMixer = PV_BindBAEMixer(mWritingMixer);
        hardwareMixer = mWritingMixer->pMixer;
        didBindWritingMixer = 1;
    }
    else
    {
        hardwareMixer = MusicGlobals;
    }

    if (mWritingToFile && mWritingToFileReference)
    {
        switch (mWriteToFileType)
        {
#if USE_MPEG_ENCODER == TRUE
        case BAE_MPEG_TYPE:
            debug_message("audio: BAEMixer_StopOutputToFile freeing mWritingEncoder=%p\n", mWritingEncoder);
            MPG_EncodeFreeStream(mWritingEncoder);
            mWritingEncoder = NULL;
            debug_message("audio: BAEMixer_StopOutputToFile mWritingEncoder now NULL\n");
            break;
#endif
#if USE_VORBIS_ENCODER == TRUE
        case BAE_VORBIS_TYPE:
            debug_message("audio: BAEMixer_StopOutputToFile freeing vorbis encoder=%p\n", mWritingEncoder);
            if (mWritingEncoder)
            {
                extern long XEncodeVorbisData(void *encoder_handle, float **pcm_data, long samples, XFILE output_file);
                extern void XCloseVorbisEncoder(void *encoder_handle);
                /* Signal EOS and flush remaining Ogg pages before teardown. */
                (void)XEncodeVorbisData(mWritingEncoder, NULL, 0, (XFILE)mWritingToFileReference);
                XCloseVorbisEncoder(mWritingEncoder);
                mWritingEncoder = NULL;
            }
            break;
#else
            break;
#endif
#if USE_OPUS_ENCODER == TRUE
        case BAE_OPUS_TYPE:
            debug_message("audio: BAEMixer_StopOutputToFile freeing opus encoder=%p\n", mWritingEncoder);
            if (mWritingEncoder)
            {
                extern long XFlushOpusEncoder(void *encoder_handle, XFILE output_file);
                extern void XCloseOpusEncoder(void *encoder_handle);
                (void)XFlushOpusEncoder(mWritingEncoder, (XFILE)mWritingToFileReference);
                XCloseOpusEncoder(mWritingEncoder);
                mWritingEncoder = NULL;
            }
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

        /* Pass GM_Mixer* so Acquire stores it for the OpenSL/audio callback TLS bind. */
        GM_StartHardwareSoundManager(hardwareMixer); // reconnect to hardware
    }
    mWritingToFile = FALSE;
    mWritingMixer = NULL;
    if (didBindWritingMixer)
        GM_SetCurrentMixer(savedMixer);
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

    // begin block added for NeoBAE
    BAEAudioModifiers theModifiers;
    BAEMixer_GetModifiers(theMixer, &theModifiers);
    // end block added for NeoBAE

    theErr = NO_ERR;

    channels = (theModifiers & BAE_USE_STEREO) ? 2 : 1;
    sampleSize = (theModifiers /*iModifiers*/ & BAE_USE_16) ? 2 : 1;
    uint32_t numSamples = (uint32_t)(mWritingDataBlockSize / sampleSize / channels);

    BAE_BuildMixerSlice(theMixer ? theMixer->pMixer : NULL, mWritingDataBlock, mWritingDataBlockSize, numSamples);
    process_and_send_audio(mWritingDataBlock, numSamples);
    return BAE_TranslateOPErr(theErr);
}
#endif

// ********************** BAEMixer_ServiceAudioOutputToFile   ************
//
// ********************** added from BAE 11/29/00 tom ********************
// ********************** method name changed for NeoBAE conformance ****
// ********************** parameter 1 added for NeoBAE ******************
// ***** 'iModifiers'  replaced by call to accessor method  **************

BAEResult BAEMixer_ServiceAudioOutputToFile(BAEMixer theMixer)
{
#if USE_CREATION_API == TRUE
    int32_t sampleSize, channels;
    OPErr theErr;
    GM_Mixer *savedMixer = NULL;

#ifndef HMP3_ENC_LOG
#define HMP3_ENC_LOG 0
#endif

    // Bind TLS mixer for this thread (zefidi export runs off the GUI thread).
    savedMixer = PV_BindBAEMixer(theMixer);

    // begin block added for NeoBAE
    BAEAudioModifiers theModifiers;
    BAEMixer_GetModifiers(theMixer, &theModifiers);
    // end block added for NeoBAE

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
                    uint32_t compressedLength = 0;
                    bool isDone = FALSE;
                    if (!mWritingEncoder)
                    {
                        debug_message("audio: MPEG encode service called with NULL encoder (encoder not built?) aborting export.\n");
                        // Gracefully abort: close file and reset state
                        mWriteToFileType = 0; // invalid
                        XFileClose((XFILE)mWritingToFileReference);
                        mWritingToFileReference = NULL;
                        mWritingToFile = FALSE;
                        GM_SetCurrentMixer(savedMixer);
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
                            debug_message("audio: MPG_EncodeProcess signaled done, freeing encoder %p\n", mWritingEncoder);
                            if (mWritingEncoder)
                            {
                                MPG_EncodeFreeStream(mWritingEncoder);
                                mWritingEncoder = NULL;
                                debug_message("audio: encoder freed, mWritingEncoder=NULL\n");
                            }
                            else
                            {
                                debug_message("audio: encoder already NULL when done signaled\n");
                            }
                        }
                    }
                }
                break;
#endif

                case BAE_RAW_PCM:
                {
                    BAE_BuildMixerSlice(theMixer ? theMixer->pMixer : NULL, mWritingDataBlock, mWritingDataBlockSize,
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
                    BAE_BuildMixerSlice(theMixer ? theMixer->pMixer : NULL, mWritingDataBlock, mWritingDataBlockSize,
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

                    BAE_BuildMixerSlice(theMixer ? theMixer->pMixer : NULL, mWritingDataBlock, mWritingDataBlockSize, framesToProcess);

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
                            debug_message("FLAC accumulation buffer full (>10 minutes), export will be truncated\n");
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
                    BAE_BuildMixerSlice(theMixer ? theMixer->pMixer : NULL, mWritingDataBlock, mWritingDataBlockSize, framesToProcess);

                    // Convert interleaved 16-bit PCM to planar float arrays expected by encoder
                    extern long XEncodeVorbisData(void *encoder_handle, float **pcm_data, long samples, XFILE output_file);
                    int ch = channels;
                    float *chanBufs[2] = {0};
                    bool allocOk = (mWritingEncoder != NULL);

                    for (int c = 0; c < ch && allocOk; c++)
                    {
                        chanBufs[c] = (float *)XNewPtr(sizeof(float) * framesToProcess);
                        if (!chanBufs[c])
                            allocOk = false;
                    }

                    long written = -1;
                    if (allocOk)
                    {
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

                        written = XEncodeVorbisData(mWritingEncoder, chanBufs, (long)framesToProcess,
                                                    (XFILE)mWritingToFileReference);
                    }

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

#if USE_OPUS_ENCODER == TRUE
                case BAE_OPUS_TYPE:
                {
                    uint32_t framesToProcess = (uint32_t)(mWritingDataBlockSize / sampleSize / channels);
                    extern long XEncodeOpusData(void *encoder_handle, const int16_t *pcm_interleaved, long frames, XFILE output_file);

                    BAE_BuildMixerSlice(theMixer ? theMixer->pMixer : NULL, mWritingDataBlock, mWritingDataBlockSize, framesToProcess);

                    if (XEncodeOpusData(mWritingEncoder,
                                        (const int16_t *)mWritingDataBlock,
                                        (long)framesToProcess,
                                        (XFILE)mWritingToFileReference) < 0)
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
    GM_SetCurrentMixer(savedMixer);
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
        debug_message("audio: BAESound_Delete invalid object\n");
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
        //      debug_message("BAE:deleting sound...\n");
        XWaitMicroseconds(BAE_GetSliceTimeInMicroseconds());
    }

    if (sound->pWave)
    {
        GM_FreeWaveform(sound->pWave);
        sound->pWave = NULL;
    }
    //  debug_message("BAE:deleting sound done\n");
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
                    XPhase8BitWaveform((unsigned char *)pWave->theWaveform, pWave->waveSize);
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
        BAE_AcquireMutex(sound->mLock);

        // if sound already loaded, then free it...
        BAESound_Unload(sound);
#if USE_ADP_SUPPORT == TRUE
        if (fileType == BAE_ADP_TYPE)
        {
            sound->pWave = PV_ReadADPIntoMemoryFromMemory(pMemoryFile, memoryFileSize, &theErr);
        }
        else
#endif
#if USE_ADX_SUPPORT == TRUE
        if (fileType == BAE_ADX_TYPE)
        {
            sound->pWave = PV_ReadADXIntoMemoryFromMemory(pMemoryFile, memoryFileSize, &theErr);
        }
        else
#endif
        {
            type = BAE_TranslateBAEFileType(fileType);
            if (type != FILE_INVALID_TYPE)
            {
                sound->pWave = GM_ReadFileIntoMemoryFromMemory(pMemoryFile, memoryFileSize,
                                                               type, TRUE, &theErr);
            }
            else
            {
                theErr = BAD_FILE_TYPE;
            }
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
    XPTR fileData;
#if USE_ADP_SUPPORT == TRUE
    int32_t fileSize;
#endif

    theErr = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        fileData = NULL;
#if USE_ADP_SUPPORT == TRUE        
        fileSize = 0;
#endif

        BAE_AcquireMutex(sound->mLock);

        // if sound already loaded, then free it...
        BAESound_Unload(sound);

        XConvertPathToXFILENAME(filePath, &theFile);
#if USE_ADP_SUPPORT == TRUE        
        if (fileType == BAE_ADP_TYPE)
        {
            fileData = PV_GetFileAsData(&theFile, &fileSize);
            if (fileData && fileSize > 0)
            {
                sound->pWave = PV_ReadADPIntoMemoryFromMemory(fileData, (uint32_t)fileSize, &theErr);
            }
        }
#endif
#if USE_ADX_SUPPORT == TRUE
        if (fileType == BAE_ADX_TYPE)
        {
            int32_t xFileSize = 0;
            fileData = PV_GetFileAsData(&theFile, &xFileSize);
            if (fileData && xFileSize > 0)
            {
                sound->pWave = PV_ReadADXIntoMemoryFromMemory(fileData, (uint32_t)xFileSize, &theErr);
            }
        }
#endif
        if (sound->pWave == NULL && theErr == NO_ERR)
        {
            type = BAE_TranslateBAEFileType(fileType);
            if (type != FILE_INVALID_TYPE)
            {
                sound->pWave = GM_ReadFileIntoMemory(&theFile, type, TRUE, &theErr);
            }
            else
            {
                theErr = BAD_FILE_TYPE;
            }
        }

        if (fileData)
        {
            XDisposePtr(fileData);
        }
        if ((sound->pWave == NULL) && (theErr == NO_ERR))
        {
            theErr = BAD_FILE;
        }
        BAE_ReleaseMutex(sound->mLock);

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

// BAESound_NormalizeFromPeak()
// --------------------------------------
// Peak-normalize already-decoded PCM in RAM (scan + in-place scale).
//
BAEResult BAESound_NormalizeFromPeak(BAESound sound,
                                     int32_t targetPeakPct,
                                     int32_t *outAppliedGainPct)
{
    GM_Waveform *pWave;
    uint32_t sampleCount;
    uint32_t i;
    int32_t peakAbs;
    int32_t gainPct = 100;
    int64_t scaled;

    if (outAppliedGainPct)
        *outAppliedGainPct = 100;

    if (!sound || sound->mID != OBJECT_ID)
        return BAE_TranslateOPErr(NULL_OBJECT);

    BAE_AcquireMutex(sound->mLock);
    pWave = sound->pWave;
    if (!pWave || !pWave->theWaveform || pWave->waveFrames == 0 || pWave->channels == 0)
    {
        BAE_ReleaseMutex(sound->mLock);
        return BAE_TranslateOPErr(NOT_SETUP);
    }

    if (pWave->compressionType != C_NONE)
    {
        BAE_ReleaseMutex(sound->mLock);
        return BAE_TranslateOPErr(BAD_FILE_TYPE);
    }

    if (targetPeakPct <= 0)
        targetPeakPct = 89; /* ~-1 dBFS */
    if (targetPeakPct > 99)
        targetPeakPct = 99;

    sampleCount = pWave->waveFrames * (uint32_t)pWave->channels;
    peakAbs = 0;

    if (pWave->bitSize == 16)
    {
        int16_t *pcm = (int16_t *)pWave->theWaveform;
        for (i = 0; i < sampleCount; i++)
        {
            int32_t s = (int32_t)pcm[i];
            if (s < 0)
                s = -s;
            if (s > peakAbs)
                peakAbs = s;
        }
        if (peakAbs < 1)
        {
            BAE_ReleaseMutex(sound->mLock);
            return BAE_NO_ERROR;
        }

        gainPct = (int32_t)(((int64_t)targetPeakPct * 32767LL) / (int64_t)peakAbs);
        if (gainPct < 5)
            gainPct = 5;
        if (gainPct > 800)
            gainPct = 800;

        if (gainPct != 100)
        {
            for (i = 0; i < sampleCount; i++)
            {
                scaled = ((int64_t)pcm[i] * (int64_t)gainPct) / 100LL;
                if (scaled > 32767)
                    scaled = 32767;
                if (scaled < -32768)
                    scaled = -32768;
                pcm[i] = (int16_t)scaled;
            }
        }
    }
    else if (pWave->bitSize == 8)
    {
        unsigned char *pcm = (unsigned char *)pWave->theWaveform;
        for (i = 0; i < sampleCount; i++)
        {
            int32_t s = (int32_t)pcm[i] - 128;
            if (s < 0)
                s = -s;
            if (s > peakAbs)
                peakAbs = s;
        }
        if (peakAbs < 1)
        {
            BAE_ReleaseMutex(sound->mLock);
            return BAE_NO_ERROR;
        }

        gainPct = (int32_t)(((int64_t)targetPeakPct * 127LL) / (int64_t)peakAbs);
        if (gainPct < 5)
            gainPct = 5;
        if (gainPct > 800)
            gainPct = 800;

        if (gainPct != 100)
        {
            for (i = 0; i < sampleCount; i++)
            {
                scaled = ((((int64_t)pcm[i] - 128) * (int64_t)gainPct) / 100LL) + 128;
                if (scaled > 255)
                    scaled = 255;
                if (scaled < 0)
                    scaled = 0;
                pcm[i] = (unsigned char)scaled;
            }
        }
    }
    else
    {
        BAE_ReleaseMutex(sound->mLock);
        return BAE_TranslateOPErr(BAD_FILE_TYPE);
    }

    if (outAppliedGainPct)
        *outAppliedGainPct = gainPct;

    BAE_ReleaseMutex(sound->mLock);
    return BAE_NO_ERROR;
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
    int16_t minVolume;
    int16_t maxVolume;
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
            delta = XFixedMultiply(delta, -LONG_TO_FIXED(MAX_NOTE_VOLUME * BAE_SOUND_UNITY_TO_NOTE_SCALE));
#else
            double delta;
            delta = PV_CalculateTimeDeltaForFade(sourceVolume, destVolume, timeInMiliseconds);
            delta = delta * -(MAX_NOTE_VOLUME * BAE_SOUND_UNITY_TO_NOTE_SCALE);
#endif
            source = (int16_t)PV_BAESound_UserVolumeToNoteVolume((BAE_UNSIGNED_FIXED)sourceVolume);
            dest = (int16_t)PV_BAESound_UserVolumeToNoteVolume((BAE_UNSIGNED_FIXED)destVolume);
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
    bool shouldRestart = FALSE;

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
                    int32_t volume = PV_BAESound_UserVolumeToNoteVolume(sound->mVolume);
                    sound->voiceRef = GM_SetupSampleFromInfo(sound->pWave, (void *)sound,
                                                             volume,
                                                             0,
                                                             NULL,
                                                             PV_LoopingSoundDoneCallback,
                                                             0); // start from beginning
                    if (sound->voiceRef != DEAD_VOICE)
                    {
                        GM_SetSampleRouteBus(sound->voiceRef, sound->mRouteBus);
                        GM_ChangeSampleVolume(sound->voiceRef, (int16_t)volume);
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
                    debug_message("audio:sound not in mixer list, no callback\n");
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
        debug_message("audio:sound no longer valid, no callback\n");
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
            volume = PV_BAESound_UserVolumeToNoteVolume(sampleVolume);

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
                GM_ChangeSampleVolume(sound->voiceRef, (int16_t)volume);
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

// BAESound_StopAndWait()
// --------------------------------------
// Stop immediately, then wait until the mixer is no longer processing the
// former voice. Required before Load/Unload from the UI thread: a plain
// BAESound_Stop() clears voiceRef first, so a following Unload cannot wait
// and may free the waveform mid-slice (intermittent one-channel click).
//
BAEResult BAESound_StopAndWait(BAESound sound)
{
    OPErr err;
    VOICE_REFERENCE voice;

    err = NO_ERR;
    if ((sound) && (sound->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(sound->mLock);
        voice = sound->voiceRef;
        PV_BAESound_Stop(sound, FALSE);
        while (GM_IsSampleProcessing(voice))
        {
            XWaitMicroseconds(BAE_GetSliceTimeInMicroseconds());
        }
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
            GM_ChangeSampleVolume(sound->voiceRef,
                                 (int16_t)PV_BAESound_UserVolumeToNoteVolume(newVolume));
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

        // Looping for BAESound is implemented via the sample "done" callback.
        // BAESound_Start selects the looping vs non-looping callback at start time.
        // If the loop count is changed while the sound is already playing, we must
        // update the active voice's done callback so the mixer respects the new
        // loop setting without requiring a restart/reload.
        if (sound->voiceRef != DEAD_VOICE)
        {
            GM_SoundDoneCallbackPtr doneCallback = (loops > 0) ? PV_LoopingSoundDoneCallback : PV_DefaultSoundDoneCallback;
            GM_SetSampleDoneCallback(sound->voiceRef, doneCallback, (void *)sound);
        }
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
            if (BAE_NewMutex(&stream->mLock, "bae", "str", __LINE__))
            {
                if (BAEStream_SetMixer(stream, mixer) != BAE_NO_ERROR)
                {
                    BAE_DestroyMutex(stream->mLock);
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
                    stream->mMemoryData = NULL;
                    stream->mMemoryDataSize = 0;
                    stream->mID = OBJECT_ID;
                }
            }
            else
            {
                XDisposePtr(stream);
                stream = NULL;
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
    BAE_AcquireMutex(stream->mLock);
    BAEStream_Unload(stream);
    BAE_ReleaseMutex(stream->mLock);
    BAE_DestroyMutex(stream->mLock);
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

        if (stream->mMemoryData)
        {
            XDisposePtr(stream->mMemoryData);
            stream->mMemoryData = NULL;
            stream->mMemoryDataSize = 0;
        }
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// Forward declare (definition is later in this file)
static void PV_DefaultStreamFileDoneCallback(void *reference);


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
                        for (int _c = 0; _c < 16; _c++)
                            for (int _n = 0; _n < 128; _n++)
                                song->pSong->channelActiveNotes[_c][_n] = 0;
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

    BAE_STDERR("NeoBAE::Display Song info\n");

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
            PV_BindBAEMixer(song->mixer);

            BAEMixer_GetMidiVoices(song->mixer, &maxSongVoices);
            BAEMixer_GetSoundVoices(song->mixer, &maxEffectVoices);
            BAEMixer_GetMixLevel(song->mixer, &mixLevel);
            GM_ChangeSongVoices(song->pSong, maxSongVoices, mixLevel, maxEffectVoices);
            GM_SetVelocityCurveType(song->pSong, (VelocityCurveType)g_defaultVelocityCurve);
#if USE_SF2_SUPPORT == TRUE
            if (GM_SF2_IsActive()) { GM_EnableSF2ForSong(song->pSong, TRUE); }
#endif
            PV_EnableZmfCompatIfMixerHasZsb(song);
            /* Keep the mixer's current reverb type (per-mixer, not process-global). */
            GM_SetReverbType(GM_GetReverbType());

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

    if (!cName || maxSize <= 0)
    {
        return BAE_TranslateOPErr(PARAM_ERR);
    }

    if ((song) && (song->mID == OBJECT_ID))
    {
        uint32_t copyLen;

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
            copyLen = XStrLen(song->mTitle);
            if (copyLen >= (uint32_t)maxSize)
            {
                copyLen = (uint32_t)maxSize - 1;
            }
            XBlockMove(song->mTitle, cName, (int32_t)copyLen);
            cName[copyLen] = 0;
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
        if (song->mHasEmbeddedBank)
            return TRUE;
#if USE_NATIVE_DLS == TRUE
        if (song->mixer && song->mixer->pMixer &&
            GM_DLS_HasXmfEmbeddedBank(song->mixer->pMixer))
            return TRUE;
#endif
        return FALSE;
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
#if USE_MTHC_SUPPORT == TRUE    
    unsigned char *decodedMthcMidi = NULL;
    uint32_t decodedMthcMidiLen = 0;
#endif
    BAE_BOOL ownsMidiData = FALSE;
#if USE_RMI_SUPPORT == TRUE
    uint32_t extractedMidiLen = 0;
#endif

    theErr = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
#if USE_MTHC_SUPPORT == TRUE
        /* Transparent MThc support: decode container to raw MIDI bytes first. */
        if (midiSize >= 4 && memcmp(pMidiData, "MThc", 4) == 0)
        {
            if (mthc_decompress_memory(pMidiData, midiSize, &decodedMthcMidi, &decodedMthcMidiLen) == 0)
            {
                pMidiData = decodedMthcMidi;
                midiSize = decodedMthcMidiLen;
            }
            else
            {
                BAE_ReleaseMutex(song->mLock);
                return BAE_TranslateOPErr(BAD_FILE);
            }
        }
#endif        
#if USE_RMI_SUPPORT == TRUE
        // Check if this is an RMI file and extract MIDI + DLS
        if (GM_IsRMIFile((const unsigned char *)pMidiData, midiSize))
        {
            debug_message("[BAE] Detected RMI file format\n");
            theErr = GM_LoadRMIFromMemory((const unsigned char *)pMidiData, midiSize,
                                          &extractedMidi, &extractedMidiLen, TRUE);
            if (theErr == NO_ERR && extractedMidi)
            {
                pMidiData = extractedMidi;
                midiSize = extractedMidiLen;
                ownsMidiData = TRUE;
            }
            else
            {
                debug_message("[BAE] Failed to parse RMI file (error %d)\n", theErr);
                BAE_ReleaseMutex(song->mLock);
                return BAE_TranslateOPErr(theErr);
            }
        }
        else
#endif
        {
            pMidiData = XDuplicateMemory((XPTRC)pMidiData, (uint32_t)midiSize);
            if (pMidiData)
            {
                ownsMidiData = TRUE;
            }
        }

#if BAE_ENABLE_ROLLED_MIDI_UNROLL == TRUE
        if (pMidiData && midiSize)
        {
            BAE_BOOL preLoadRolled;
            preLoadRolled = PV_IsRolledMIDIMemory(pMidiData, midiSize);
            debug_message("[BAE] Rolled MIDI pre-load detect: %s (size=%u)\n",
                          preLoadRolled ? "yes" : "no",
                          midiSize);
            if (preLoadRolled)
            {
                unsigned char *unrolledMidi = NULL;
                uint32_t unrolledMidiSize = 0;
                BAE_BOOL wasRolledNow = FALSE;
                BAEResult unrollResult;

                unrollResult = BAEUtil_UnrollRolledMidiFromMemory(pMidiData,
                                                                  midiSize,
                                                                  BAE_UNROLL_MIDI_OPTION_SPLIT_INSTRUMENTS,
                                                                  &unrolledMidi,
                                                                  &unrolledMidiSize,
                                                                  &wasRolledNow);
                if ((unrollResult == BAE_NO_ERROR) && wasRolledNow && unrolledMidi && unrolledMidiSize)
                {
                    debug_message("[BAE] Unrolled Beatnik rolled MIDI at load-time (%u -> %u bytes)\n",
                                  midiSize,
                                  unrolledMidiSize);
                    if (ownsMidiData)
                    {
                        XDisposePtr((XPTR)pMidiData);
                    }
                    pMidiData = unrolledMidi;
                    midiSize = unrolledMidiSize;
                    ownsMidiData = TRUE;
                }
                else
                {
                    debug_message("[BAE] Rolled MIDI pre-load unroll skipped/failed (res=%d wasRolled=%d out=%p outSize=%u)\n",
                                  (int)unrollResult,
                                  (int)wasRolledNow,
                                  (void *)unrolledMidi,
                                  unrolledMidiSize);
                    if (unrolledMidi)
                    {
                        XDisposePtr(unrolledMidi);
                    }
                }
            }
        }
#endif

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
                    if (GM_SF2_IsActive()) {
                        GM_ResetSF2();
                    }
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
#if BAE_ENABLE_ROLLED_MIDI_UNROLL == TRUE
                        {
                            BAE_BOOL postLoadRolled;

                            postLoadRolled = PV_IsRolledMIDISequence(pSong);
                            debug_message("[BAE] Rolled MIDI post-load detect: %s (seqSize=%u)\n",
                                          postLoadRolled ? "yes" : "no",
                                          (uint32_t)pSong->sequenceDataSize);
                            if (postLoadRolled)
                            {
                                unsigned char *unrolledMidi = NULL;
                                uint32_t unrolledMidiSize = 0;
                                BAE_BOOL wasRolledNow = FALSE;
                                BAEResult unrollResult;

                                unrollResult = BAEUtil_UnrollRolledMidiFromMemory(pSong->sequenceData,
                                                                                  (uint32_t)pSong->sequenceDataSize,
                                                                                  BAE_UNROLL_MIDI_OPTION_SPLIT_INSTRUMENTS,
                                                                                  &unrolledMidi,
                                                                                  &unrolledMidiSize,
                                                                                  &wasRolledNow);
                                if ((unrollResult == BAE_NO_ERROR) && wasRolledNow && unrolledMidi && unrolledMidiSize)
                                {
                                    GM_Song *pReloadedSong;

                                    pReloadedSong = GM_LoadSong(song->mixer->pMixer,
                                                                NULL,
                                                                song,
                                                                theID,
                                                                (void *)pXSong,
                                                                (void *)unrolledMidi,
                                                                (int32_t)unrolledMidiSize,
                                                                NULL,
                                                                TRUE,
                                                                ignoreBadInstruments,
                                                                CreateBankToken(),
                                                                &theErr);
                                    if (pReloadedSong)
                                    {
                                        debug_message("[BAE] Unrolled Beatnik rolled MIDI after load (%u -> %u bytes)\n",
                                                      (uint32_t)pSong->sequenceDataSize,
                                                      unrolledMidiSize);
                                        GM_SetDisposeSongDataWhenDoneFlag(pReloadedSong, TRUE);
                                        GM_SetSongLoopFlag(pReloadedSong, FALSE);
                                        GM_SetVelocityCurveType(pReloadedSong, (VelocityCurveType)g_defaultVelocityCurve);
                                        GM_FreeSong(song, pSong);
                                        pSong = pReloadedSong;
                                    }
                                    else
                                    {
                                        debug_message("[BAE] Rolled MIDI post-load reload failed (res=%d); keeping original load\n", (int)theErr);
                                        XDisposePtr(unrolledMidi);
                                    }
                                }
                                else
                                {
                                    debug_message("[BAE] Rolled MIDI post-load unroll skipped/failed (res=%d wasRolled=%d out=%p outSize=%u)\n",
                                                  (int)unrollResult,
                                                  (int)wasRolledNow,
                                                  (void *)unrolledMidi,
                                                  unrolledMidiSize);
                                    if (unrolledMidi)
                                    {
                                        XDisposePtr(unrolledMidi);
                                    }
                                }
                            }
                        }
#endif

                        // things are cool
                        GM_SetDisposeSongDataWhenDoneFlag(pSong, TRUE); // dispose of midi data
                        GM_SetSongLoopFlag(pSong, FALSE);               // don't loop song
                        GM_SetVelocityCurveType(pSong, (VelocityCurveType)g_defaultVelocityCurve);
                        song->pSong = pSong;                            // preserve for use later
                        PV_EnableZmfCompatIfMixerHasZsb(song);
#if USE_SF2_SUPPORT == TRUE
                        if (GM_SF2_IsActive()) { GM_EnableSF2ForSong(song->pSong, TRUE); }
#endif

                        if (pSong->titleOffset)
                        {
                            title = XNewPtr(pSong->titleLength + 1);
                            if (title)
                            {
                                XBlockMove(((unsigned char *)pSong->sequenceData) + pSong->titleOffset,
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
        if (ownsMidiData && pMidiData && theErr != NO_ERR)
        {
            XDisposePtr((XPTR)pMidiData);
        }
#if USE_MTHC_SUPPORT == TRUE        
        if (decodedMthcMidi)
        {
            free(decodedMthcMidi);
        }
#endif        
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        theErr = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(theErr);
}


#if USE_RMI_SUPPORT == TRUE
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
                debug_message("[BAE] Detected RMI file format, useEmbeddedBank=%d\n", useEmbeddedBank);
                
                // Load RMI and extract MIDI, optionally loading embedded DLS
                theErr = GM_LoadRMIFromMemory((const unsigned char *)pRmiData, rmiSize,
                                              &extractedMidi, &extractedMidiLen, useEmbeddedBank);
                
                if (theErr == NO_ERR && extractedMidi)
                {
#if USE_RMI_SUPPORT == TRUE
                    if (useEmbeddedBank && GM_LastRMIHadEmbeddedSoundbank())
                    {
                        song->mHasEmbeddedBank = TRUE;
                    }
#endif
                    debug_message("[BAE] RMI processing complete, extracted %u bytes of MIDI\n", extractedMidiLen);
                    
                    // Now load the extracted MIDI data using LoadMidiFromMemory
                    BAE_ReleaseMutex(song->mLock);
                    BAEResult result = BAESong_LoadMidiFromMemory(song, extractedMidi, extractedMidiLen, ignoreBadInstruments);
                    if (result == BAE_NO_ERROR && song->pSong)
                    {
                        BAE_OverrideSongFromData(song->pSong, pRmiData, rmiSize);
                    }
                    XDisposePtr(extractedMidi); // Clean up extracted MIDI
                    return result;
                }
                else
                {
                    debug_message("[BAE] Failed to parse RMI file (error %d)\n", theErr);
                }
            }            
            else         
            {
                debug_message("[BAE] Data is not RMI format\n");
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

#if USE_MTHC_SUPPORT == TRUE
        /* If file content is MThc, decode to raw MIDI before loading. */
        if (pMidiData && midiSize >= 4 && memcmp(pMidiData, "MThc", 4) == 0)
        {
            unsigned char *decodedMthcMidi = NULL;
            uint32_t decodedMthcMidiLen = 0;
            if (mthc_decompress_memory((unsigned char const *)pMidiData,
                                       (uint32_t)midiSize,
                                       &decodedMthcMidi,
                                       &decodedMthcMidiLen) == 0)
            {
                XPTR midiCopy = XDuplicateMemory((XPTRC)decodedMthcMidi, decodedMthcMidiLen);
                free(decodedMthcMidi);
                decodedMthcMidi = NULL;
                XDisposePtr(pMidiData);
                pMidiData = midiCopy;
                midiSize = (int32_t)decodedMthcMidiLen;
                if (!pMidiData || midiSize < 14 || memcmp(pMidiData, "MThd", 4) != 0)
                {
                    if (pMidiData)
                    {
                        XDisposePtr(pMidiData);
                        pMidiData = NULL;
                    }
                    theErr = BAD_FILE;
                }
            }
            else
            {
                XDisposePtr(pMidiData);
                pMidiData = NULL;
                theErr = BAD_FILE;
            }
        }
#endif
        
        if (pMidiData)
        {
            // Check if this is an RMI file and extract MIDI + optional DLS
#if USE_RMI_SUPPORT == TRUE
            if (GM_IsRMIFile((const unsigned char *)pMidiData, midiSize))
            {
                debug_message("[BAE] Detected RMI file format, useEmbeddedBank=%d\n", useEmbeddedBank);
                
                // Load RMI and extract MIDI, optionally loading embedded DLS
                theErr = GM_LoadRMIFromMemory((const unsigned char *)pMidiData, midiSize,
                                              &extractedMidi, &extractedMidiLen, useEmbeddedBank);
                XDisposePtr(pMidiData); // Free original file data
                
                if (theErr == NO_ERR && extractedMidi)
                {
#if USE_RMI_SUPPORT == TRUE
                    if (useEmbeddedBank && GM_LastRMIHadEmbeddedSoundbank())
                    {
                        song->mHasEmbeddedBank = TRUE;
                    }
#endif
                    debug_message("[BAE] RMI processing complete, extracted %u bytes of MIDI\n", extractedMidiLen);
                    
                    // Now load the extracted MIDI data using LoadMidiFromMemory
                    BAE_ReleaseMutex(song->mLock);
                    BAEResult result = BAESong_LoadMidiFromMemory(song, extractedMidi, extractedMidiLen, ignoreBadInstruments);
                    XDisposePtr(extractedMidi); // Clean up extracted MIDI
                    return result;
                }
                else
                {
                    debug_message("[BAE] Failed to parse RMI file (error %d)\n", theErr);
                }
            }
            else
#endif            
            {
                XDisposePtr(pMidiData);
                debug_message("[BAE] File is not RMI format\n");
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
    int32_t midiSize;

    if ((song == NULL) || (song->mID != OBJECT_ID) || (filePath == NULL))
    {
        return BAE_TranslateOPErr(NULL_OBJECT);
    }

    XConvertPathToXFILENAME(filePath, &name);
    pMidiData = PV_GetFileAsData(&name, &midiSize);
    if ((pMidiData == NULL) || (midiSize <= 0))
    {
        if (pMidiData)
        {
            XDisposePtr(pMidiData);
        }
        return BAE_TranslateOPErr(BAD_FILE);
    }

#if USE_MTHC_SUPPORT == TRUE
    if (midiSize >= 4 && memcmp(pMidiData, "MThc", 4) == 0)
    {
        unsigned char *decodedMthcMidi = NULL;
        uint32_t decodedMthcMidiLen = 0;

        if (mthc_decompress_memory((unsigned char const *)pMidiData,
                                   (uint32_t)midiSize,
                                   &decodedMthcMidi,
                                   &decodedMthcMidiLen) == 0)
        {
            XPTR midiCopy = XDuplicateMemory((XPTRC)decodedMthcMidi, decodedMthcMidiLen);
            free(decodedMthcMidi);
            XDisposePtr(pMidiData);
            pMidiData = midiCopy;
            midiSize = (int32_t)decodedMthcMidiLen;
        }
        else
        {
            XDisposePtr(pMidiData);
            return BAE_TranslateOPErr(BAD_FILE);
        }
    }
#endif

    {
        BAEResult result;

        result = BAESong_LoadMidiFromMemory(song,
                                            (void const *)pMidiData,
                                            (uint32_t)midiSize,
                                            ignoreBadInstruments);
        XDisposePtr(pMidiData);
        if (result == BAE_NO_ERROR)
        {
            BAE_OverrideBAESongFromFile(song, filePath);
        }
        return result;
    }
}

// PV_XFileHasModernCodecSamples()
// --------------------------------------
// Scan an open XFILE resource map for any snd /csnd/esnd resource that uses
// a "modern" codec (FLAC, Ogg Vorbis, Ogg Opus, or QOA).  Only Type 3 snd headers
// carry these codecs, so we only look there.
//
// These codec FourCC values are checked unconditionally regardless of which
// decoder features are compiled in, so the guard is always effective.
//
// Returns TRUE if at least one such resource is found, FALSE otherwise.
//
static bool PV_XFileHasModernCodecSamples(XFILE fileRef)
{
    static const XResourceType sndTypes[] = { ID_SND, ID_CSND, ID_ESND };
    // FourCC values for the blocked codecs – hard-coded so the check works
    // even when a particular decoder is not compiled in.
    static const uint32_t modernCodecs[] = {
        0x664C6143UL,   // 'fLaC' = C_FLAC
        0x4F676756UL,   // 'OggV' = C_VORBIS
        0x4F67674FUL,   // 'OggO' = C_OPUS
        0x716F6166UL,   // 'qoaf' = C_QOA
        0x696D6132UL    // 'ima2' = C_IMA2
    };

    for (int t = 0; t < 3; t++)
    {
        int32_t count = XCountFileResourcesOfType(fileRef, sndTypes[t]);
        for (int32_t i = 0; i < count; i++)
        {
            XLongResourceID resID;
            int32_t resSize = 0;
            XPTR resData = XGetIndexedFileResource(fileRef, sndTypes[t], &resID, i, NULL, &resSize);
            if (resData && resSize >= 6)
            {
                const uint8_t *rb = (const uint8_t *)resData;
                // First 2 bytes: snd resource format type (big-endian int16)
                uint16_t soundFormat = ((uint16_t)rb[0] << 8) | rb[1];
                if (soundFormat == (uint16_t)XThirdSoundFormat)
                {
                    // Bytes 2-5: XSoundHeader3.subType (big-endian FourCC)
                    uint32_t subType = ((uint32_t)rb[2] << 24) | ((uint32_t)rb[3] << 16)
                                     | ((uint32_t)rb[4] <<  8) |  (uint32_t)rb[5];
                    for (int c = 0; c < (int)(sizeof(modernCodecs) / sizeof(modernCodecs[0])); c++)
                    {
                        if (subType == modernCodecs[c])
                        {
                            XDisposePtr(resData);
                            return TRUE;
                        }
                    }
                }
            }
            XDisposePtr(resData);
        }
    }
    return FALSE;
}

static void PV_TagSongResourceContainerType(SongResource *songResource, bool isZmfContainer)
{
    SongResource_RMF *songRMF;
    uint32_t flags;

    if (!songResource)
    {
        return;
    }
    if (((SongResource_SMS *)songResource)->songType != SONG_TYPE_RMF)
    {
        return;
    }

    songRMF = (SongResource_RMF *)songResource;
    flags = (uint32_t)XGetLong(&songRMF->unused[SONG_CONFIG_UNUSED_INDEX]);
    if (isZmfContainer)
    {
        flags |= SONG_CONFIG_CONTAINER_IS_ZMF;
    }
    else
    {
        flags &= ~((uint32_t)SONG_CONFIG_CONTAINER_IS_ZMF);
    }
    XPutLong(&songRMF->unused[SONG_CONFIG_UNUSED_INDEX], flags);
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
    bool isZmfContainer;

    theErr = NO_ERR;
    isZmfContainer = FALSE;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (pRMFData && rmfSize)
        {
            fileRef = XFileOpenResourceFromMemory((XPTR)pRMFData, rmfSize, TRUE);
            if (fileRef)
            {
                // Reject IREZ (classic RMF) files that contain modern-codec samples.
                // Only ZREZ (ZMF) files are permitted to embed FLAC, Vorbis, Opus, or QOA.
                {
                    XFILERESOURCEMAP mapHdr;
                    XFileSetPosition(fileRef, 0L);
                    if (XFileRead(fileRef, &mapHdr, (int32_t)sizeof(XFILERESOURCEMAP)) == 0)
                    {
                        if (XGetLong(&mapHdr.mapID) == XFILERESOURCE_ID) // IREZ, not ZREZ
                        {
                            if (PV_XFileHasModernCodecSamples(fileRef))
                            {
                                debug_message("[RMF] IREZ file rejected: contains FLAC/Vorbis/Opus/QOA sample(s) - upgrade to ZMF (.zmf)\n");
                                XFileClose(fileRef);
                                BAE_ReleaseMutex(song->mLock);
                                return BAE_UNSUPPORTED_FORMAT;
                            }
                        }
                        else if (XGetLong(&mapHdr.mapID) == XFILERESOURCE_ZMF_ID)
                        {
#if USE_ZMF_SUPPORT == TRUE
                            isZmfContainer = TRUE;
#else
                            debug_message("[RMF] ZREZ (ZMF) file rejected: ZMF support is not compiled in\n");
                            XFileClose(fileRef);
                            BAE_ReleaseMutex(song->mLock);
                            return BAE_UNSUPPORTED_FORMAT;
#endif
                        }
                    }
                }
                {
#if _DEBUG
                    int32_t songCount = XCountFileResourcesOfType(fileRef, ID_SONG);
                    debug_message("[RMF] RMF contains %ld SONG resource(s); requested songIndex=%d\n", (long)songCount, (int)songIndex);
#endif
                }
                pXSong = (SongResource *)XGetIndexedFileResource(fileRef, ID_SONG, &theID, songIndex, NULL, &size);
                if (pXSong)
                {
                    PV_TagSongResourceContainerType(pXSong, isZmfContainer);
                    debug_message("[RMF] Primary path: XGetIndexedFileResource succeeded, pXSong=%p\n", pXSong);
                    if (song->pSong)
                    {
                        PV_BAESong_Unload(song);
#if USE_SF2_SUPPORT == TRUE
                        if (GM_SF2_IsActive()) {
                            GM_ResetSF2();
                        }
#endif                         
                        XBankToken songBankToken = CreateBankToken();
                        pSong = GM_LoadSong(song->mixer->pMixer,
                                            NULL,
                                            song,
                                            (XShortResourceID)theID,
                                            (void *)pXSong,
                                            NULL,
                                            0L,
                                            NULL, // no callback
                                            FALSE, // load after RMF metadata is attached
                                            ignoreBadInstruments,
                                            songBankToken,
                                            &theErr);
                        if (pSong)
                        {
                            // things are cool
#if USE_SF2_SUPPORT == TRUE
                            uint32_t instBuf[MAX_INSTRUMENTS];
                            uint32_t totalInst = 0;
                            memset(instBuf, 0, sizeof(instBuf));
                            BAEUtil_GetRmfInstrumentListFromMemory(pRMFData, rmfSize, songIndex, instBuf, MAX_INSTRUMENTS, &totalInst);
                            debug_message("pSong = %p\n", pSong);
                            debug_message("instBuf[0] = %d\n", instBuf[0]);
                            pSong->RMFInstrumentIDs[0] = totalInst; // Store count first
                            for (uint32_t i = 1; i <= totalInst; i++)
                            {
                                pSong->RMFInstrumentIDs[i] = instBuf[i-1];
                            }
                            debug_message("Found %u Instruments in RMF (stored=%u)\n", totalInst, (unsigned)XMIN(totalInst, MAX_INSTRUMENTS));
                            for (uint32_t i = 1; i <= totalInst; i++) {
                                debug_message("    %u - INST: %u\n", i, pSong->RMFInstrumentIDs[i]);
                            }
#else
                            pSong->RMFInstrumentIDs[0] = 0;
#endif
                            pSong->songFlags = SONG_FLAG_IS_RMF;
                            theErr = GM_LoadSongInstruments(pSong, NULL, songBankToken, TRUE);
                            if (theErr != NO_ERR)
                            {
                                GM_FreeSong(NULL, pSong);
                                pSong = NULL;
                            }
                        }
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
                    debug_message("[RMF] Primary path failed: XGetIndexedFileResource returned NULL for songIndex=%d\n", songIndex);
                    // Fallback attempt: direct memory scan if primary lookup failed.
                    SongResource *fallbackSong = PV_FallbackFindSongInRMFMemory(pRMFData, rmfSize, songIndex, &theID, &size);
                    if (fallbackSong)
                    {
                        pXSong = fallbackSong; // treat as found
                        PV_TagSongResourceContainerType(pXSong, isZmfContainer);
                        theErr = NO_ERR;
                        if (song->pSong)
                        {
                            PV_BAESong_Unload(song);
                            XBankToken songBankToken = CreateBankToken();
                            pSong = GM_LoadSong(song->mixer->pMixer,
                                                NULL,
                                                song,
                                                (XShortResourceID)theID,
                                                (void *)pXSong,
                                                NULL,
                                                0L,
                                                NULL, // no callback
                                                FALSE, // load after RMF metadata is attached
                                                ignoreBadInstruments,
                                                songBankToken,
                                                &theErr);
                            if (pSong)
                            {
#if USE_SF2_SUPPORT == TRUE
                                uint32_t instBuf[MAX_INSTRUMENTS];
                                uint32_t totalInst = 0;
                                BAEUtil_GetRmfInstrumentListFromMemory(pRMFData, rmfSize, songIndex, instBuf, MAX_INSTRUMENTS, &totalInst);
                                debug_message("[FALLBACK] pSong = %p\n", pSong);
                                pSong->RMFInstrumentIDs[0] = totalInst;
                                for (uint32_t i = 1; i <= totalInst; i++)
                                {
                                    pSong->RMFInstrumentIDs[i] = instBuf[i-1];
                                }
                                debug_message("[FALLBACK] Found %u Instruments in RMF (stored=%u)\n", totalInst, (unsigned)XMIN(totalInst, MAX_INSTRUMENTS));
                                for (uint32_t i = 1; i <= totalInst; i++) {
                                    debug_message("[FALLBACK]     %u - INST: %u\n", i, pSong->RMFInstrumentIDs[i]);
                                }
                                debug_message("[FALLBACK] Set songFlags=0x%X, RMFInstrumentIDs[0]=%u\n", pSong->songFlags, pSong->RMFInstrumentIDs[0]);
#else
                                pSong->RMFInstrumentIDs[0] = 0;
#endif
                                pSong->songFlags = SONG_FLAG_IS_RMF;
                                theErr = GM_LoadSongInstruments(pSong, NULL, songBankToken, TRUE);
                                if (theErr != NO_ERR)
                                {
                                    GM_FreeSong(NULL, pSong);
                                    pSong = NULL;
                                }
                            }
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

// Forward declaration
// Enumerate INST resource IDs in an RMF/ZMF resource image. If pOutInstruments is non-NULL, up to maxInstruments
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

// BAESong_LoadRmfFromFile()
// --------------------------------------
//
//
/* Collect DATe-stamped SONG / Midi object ids (same rules as nbeditor). */
static void PV_CollectDateStampedSongKeys(XFILE fileRef,
                                          uint32_t *songIds, int32_t *songCount, int32_t songMax,
                                          uint32_t *midiIds, int32_t *midiCount, int32_t midiMax)
{
    XResourceType dateType = FOUR_CHAR('D', 'A', 'T', 'e');
    int32_t dateCount;
    int32_t di;

    *songCount = 0;
    *midiCount = 0;
    if (!fileRef || !songIds || !midiIds || songMax <= 0 || midiMax <= 0)
    {
        return;
    }

    dateCount = XCountFileResourcesOfType(fileRef, dateType);
    for (di = 0; di < dateCount; di++)
    {
        XLongResourceID dateId = 0;
        int32_t dateSize = 0;
        XPTR dateData = XGetIndexedFileResource(fileRef, dateType, &dateId, di, NULL, &dateSize);
        const unsigned char *body;
        int32_t off;

        if (!dateData || dateSize <= 0)
        {
            if (dateData)
            {
                XDisposePtr(dateData);
            }
            continue;
        }
        body = (const unsigned char *)dateData;
        for (off = 0; off + 12 <= dateSize; off += 12)
        {
            uint32_t type = XGetLong(body + off);
            uint32_t id = XGetLong(body + off + 4);
            uint32_t ts = XGetLong(body + off + 8);
            int32_t i;
            bool found;

            if (type == 0 && id == 0 && ts == 0)
            {
                break;
            }
            if (type == (uint32_t)ID_SONG)
            {
                found = FALSE;
                for (i = 0; i < *songCount; i++)
                {
                    if (songIds[i] == id)
                    {
                        found = TRUE;
                        break;
                    }
                }
                if (!found && *songCount < songMax)
                {
                    songIds[(*songCount)++] = id;
                }
            }
            else if (type == (uint32_t)ID_MIDI || type == (uint32_t)ID_EMID ||
                     type == (uint32_t)ID_ECMI || type == (uint32_t)ID_CMID)
            {
                found = FALSE;
                for (i = 0; i < *midiCount; i++)
                {
                    if (midiIds[i] == id)
                    {
                        found = TRUE;
                        break;
                    }
                }
                if (!found && *midiCount < midiMax)
                {
                    midiIds[(*midiCount)++] = id;
                }
            }
        }
        XDisposePtr(dateData);
    }
}

static bool PV_IdInList(uint32_t id, const uint32_t *ids, int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i++)
    {
        if (ids[i] == id)
        {
            return TRUE;
        }
    }
    return FALSE;
}

/* Preferred play order: custom/session songs, then groovoids. outIndices are
 * XGetIndexedFileResource(ID_SONG) indices. Returns how many were collected. */
static int16_t PV_CollectMixerBankPreferredSongIndices(XFILE fileRef,
                                                       int16_t *outIndices,
                                                       int16_t maxOut)
{
    enum { kMaxDateKeys = 512, kMaxSongs = 512 };
    uint32_t datedSongIds[kMaxDateKeys];
    uint32_t datedMidiIds[kMaxDateKeys];
    int32_t datedSongCount = 0;
    int32_t datedMidiCount = 0;
    bool useDateFilter;
    int16_t customIdx[kMaxSongs];
    int16_t groovoidIdx[kMaxSongs];
    int16_t customCount = 0;
    int16_t groovoidCount = 0;
    int32_t songCount;
    int32_t i;
    bool prevForce;
    int16_t outCount = 0;

    if (!fileRef || !outIndices || maxOut <= 0)
    {
        return 0;
    }

    prevForce = XFileGetForceZsbFeatures(fileRef);
    XFileSetForceZsbFeatures(fileRef, TRUE);

    PV_CollectDateStampedSongKeys(fileRef,
                                  datedSongIds, &datedSongCount, kMaxDateKeys,
                                  datedMidiIds, &datedMidiCount, kMaxDateKeys);
    useDateFilter = (datedSongCount > 0 || datedMidiCount > 0);

    songCount = XCountFileResourcesOfType(fileRef, ID_SONG);
    for (i = 0; i < songCount && (customCount + groovoidCount) < kMaxSongs; i++)
    {
        XLongResourceID songId = 0;
        int32_t songSize = 0;
        XPTR songData;
        const unsigned char *body;
        uint16_t objectId;
        unsigned char songType;
        bool hasMidi;
        bool hasEmid;
        bool dated;
        bool isCustom;
        bool isGroovoid;

        songData = XGetIndexedFileResource(fileRef, ID_SONG, &songId, i, NULL, &songSize);
        if (!songData || songSize < 8)
        {
            if (songData)
            {
                XDisposePtr(songData);
            }
            continue;
        }
        body = (const unsigned char *)songData;
        objectId = XGetShort(body);
        songType = body[6];
        XDisposePtr(songData);
        /* SMS (0) and structured RMF (1) both reference Midi. */
        if (songType != SONG_TYPE_SMS && songType != SONG_TYPE_RMF)
        {
            continue;
        }

        hasMidi =
            XExistsFileResource(fileRef, ID_MIDI, (XLongResourceID)objectId) ||
            XExistsFileResource(fileRef, ID_MIDI_OLD, (XLongResourceID)objectId);
        hasEmid =
            XExistsFileResource(fileRef, ID_EMID, (XLongResourceID)objectId) ||
            XExistsFileResource(fileRef, ID_ECMI, (XLongResourceID)objectId) ||
            XExistsFileResource(fileRef, ID_CMID, (XLongResourceID)objectId);
        if (!hasMidi && !hasEmid)
        {
            continue;
        }

        dated = PV_IdInList((uint32_t)songId, datedSongIds, datedSongCount) ||
                PV_IdInList((uint32_t)objectId, datedMidiIds, datedMidiCount);

        /* Match nbeditor: DATe stamps mark Custom Session songs; undated Midi
         * with a date table present are converted bank groovoids; without any
         * DATe song stamps, every Midi-backed SONG is treated as custom. */
        isCustom = FALSE;
        if (useDateFilter)
        {
            isCustom = dated;
        }
        else if (hasMidi)
        {
            isCustom = TRUE;
        }

        isGroovoid = FALSE;
        if (!isCustom)
        {
            if (hasEmid)
            {
                isGroovoid = TRUE;
            }
            else if (hasMidi && useDateFilter)
            {
                isGroovoid = TRUE;
            }
        }

        if (isCustom && customCount < kMaxSongs)
        {
            customIdx[customCount++] = (int16_t)i;
        }
        else if (isGroovoid && groovoidCount < kMaxSongs)
        {
            groovoidIdx[groovoidCount++] = (int16_t)i;
        }
    }

    for (i = 0; i < customCount && outCount < maxOut; i++)
    {
        outIndices[outCount++] = customIdx[i];
    }
    for (i = 0; i < groovoidCount && outCount < maxOut; i++)
    {
        outIndices[outCount++] = groovoidIdx[i];
    }

    XFileSetForceZsbFeatures(fileRef, prevForce);
    return outCount;
}

BAEResult BAESong_LoadSongFromMixerBank(BAESong song, int16_t songIndex, BAE_BOOL ignoreBadInstruments)
{
#if USE_FULL_RMF_SUPPORT == TRUE
    SongResource *pXSong;
    GM_Song *pSong;
    OPErr theErr;
    XLongResourceID theID;
    int32_t size;
    XFILE fileRef;
    bool isZmfContainer;
    XFILERESOURCEMAP mapHdr;
    int16_t preferred[512];
    int16_t preferredCount;
    int16_t fileSongIndex;

    theErr = NO_ERR;
    isZmfContainer = FALSE;
    if (!song || song->mID != OBJECT_ID)
    {
        return BAE_NULL_OBJECT;
    }
    if (songIndex < 0)
    {
        return BAE_PARAM_ERR;
    }
    if (!song->mixer || !song->mixer->pPatchFiles || song->mixer->numPatchFiles <= 0)
    {
        return BAE_NOT_SETUP;
    }

    BAE_AcquireMutex(song->mLock);
    fileRef = song->mixer->pPatchFiles[song->mixer->numPatchFiles - 1];
    if (!fileRef)
    {
        BAE_ReleaseMutex(song->mLock);
        return BAE_BAD_FILE;
    }
    XFileUseThisResourceFile(fileRef);

    XFileSetPosition(fileRef, 0L);
    if (XFileRead(fileRef, &mapHdr, (int32_t)sizeof(XFILERESOURCEMAP)) == 0)
    {
        if (XGetLong(&mapHdr.mapID) == XFILERESOURCE_ZMF_ID)
        {
#if USE_ZMF_SUPPORT == TRUE
            isZmfContainer = TRUE;
#else
            BAE_ReleaseMutex(song->mLock);
            return BAE_UNSUPPORTED_FORMAT;
#endif
        }
    }

    preferredCount = PV_CollectMixerBankPreferredSongIndices(fileRef, preferred, 512);
    if (preferredCount <= 0 || songIndex >= preferredCount)
    {
        BAE_ReleaseMutex(song->mLock);
        return BAE_RESOURCE_NOT_FOUND;
    }
    fileSongIndex = preferred[songIndex];

    pXSong = (SongResource *)XGetIndexedFileResource(fileRef, ID_SONG, &theID, fileSongIndex, NULL, &size);
    if (!pXSong)
    {
        BAE_ReleaseMutex(song->mLock);
        return BAE_RESOURCE_NOT_FOUND;
    }

    PV_TagSongResourceContainerType(pXSong, isZmfContainer);
    if (!song->pSong)
    {
        XDisposePtr(pXSong);
        BAE_ReleaseMutex(song->mLock);
        return BAE_GENERAL_BAD;
    }

    PV_BAESong_Unload(song);
#if USE_SF2_SUPPORT == TRUE
    if (GM_SF2_IsActive())
    {
        GM_ResetSF2();
    }
#endif
    {
        /* Bank stays open — token is the mixer patch file, not a temp RMF XFILE. */
        XBankToken songBankToken = CreateBankToken();
        pSong = GM_LoadSong(song->mixer->pMixer,
                            NULL,
                            song,
                            (XShortResourceID)theID,
                            (void *)pXSong,
                            NULL,
                            0L,
                            NULL,
                            FALSE, /* instruments after song shell exists */
                            ignoreBadInstruments,
                            songBankToken,
                            &theErr);
        if (pSong)
        {
            pSong->songFlags = SONG_FLAG_IS_RMF;
            theErr = GM_LoadSongInstruments(pSong, NULL, songBankToken, TRUE);
            if (theErr != NO_ERR)
            {
                GM_FreeSong(NULL, pSong);
                pSong = NULL;
            }
        }
        if (pSong)
        {
            GM_SetDisposeSongDataWhenDoneFlag(pSong, TRUE);
            GM_SetSongLoopFlag(pSong, FALSE);
            GM_SetVelocityCurveType(pSong, (VelocityCurveType)g_defaultVelocityCurve);
            song->pSong = pSong;
#if USE_SF2_SUPPORT == TRUE
            if (GM_SF2_IsActive())
            {
                GM_EnableSF2ForSong(song->pSong, TRUE);
            }
#endif
            theErr = NO_ERR;
        }
        else
        {
            PV_BAESong_InitLiveSong(song, FALSE);
            theErr = BAD_FILE;
        }
    }
    XDisposePtr(pXSong);
    BAE_ReleaseMutex(song->mLock);
    return BAE_TranslateOPErr(theErr);
#else
    (void)song;
    (void)songIndex;
    (void)ignoreBadInstruments;
    return BAE_NOT_SETUP;
#endif
}

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
    bool isZmfContainer;

    theErr = NO_ERR;
    isZmfContainer = FALSE;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        XConvertPathToXFILENAME(filePath, &name);
        fileRef = XFileOpenResource(&name, TRUE);
        if (fileRef)
        {
            // Reject IREZ (classic RMF) files that contain modern-codec samples.
            // Only ZREZ (ZMF) files are permitted to embed FLAC, Vorbis, Opus, or QOA.
            {
                XFILERESOURCEMAP mapHdr;
                XFileSetPosition(fileRef, 0L);
                if (XFileRead(fileRef, &mapHdr, (int32_t)sizeof(XFILERESOURCEMAP)) == 0)
                {
                    if (XGetLong(&mapHdr.mapID) == XFILERESOURCE_ID) // IREZ, not ZREZ
                    {
                        if (PV_XFileHasModernCodecSamples(fileRef))
                        {
                            debug_message("[RMF] IREZ file rejected: contains FLAC/Vorbis/Opus/QOA sample(s) - upgrade to ZMF (.zmf)\n");
                            XFileClose(fileRef);
                            BAE_ReleaseMutex(song->mLock);
                            return BAE_UNSUPPORTED_FORMAT;
                        }
                    }
                    else if (XGetLong(&mapHdr.mapID) == XFILERESOURCE_ZMF_ID)
                    {
#if USE_ZMF_SUPPORT == TRUE
                        isZmfContainer = TRUE;
#else
                        debug_message("[RMF] ZREZ (ZMF) file rejected: ZMF support is not compiled in\n");
                        XFileClose(fileRef);
                        BAE_ReleaseMutex(song->mLock);
                        return BAE_UNSUPPORTED_FORMAT;
#endif
                    }
                }
            }
            pXSong = (SongResource *)XGetIndexedFileResource(fileRef, ID_SONG, &theID, songIndex, NULL, &size);
            if (pXSong)
            {
                PV_TagSongResourceContainerType(pXSong, isZmfContainer);
                {
#if _DEBUG
                    int32_t songCount = XCountFileResourcesOfType(fileRef, ID_SONG);
                    debug_message("[RMF] RMF contains %ld SONG resource(s); requested songIndex=%d\n", (long)songCount, (int)songIndex);
#endif
                }
                if (song->pSong)
                {
                    PV_BAESong_Unload(song);
#if USE_SF2_SUPPORT == TRUE
                    if (GM_SF2_IsActive()) {
                        GM_ResetSF2();
                    }
#endif                    
                    XBankToken songBankToken = CreateBankToken();
                    pSong = GM_LoadSong(song->mixer->pMixer,
                                        NULL,
                                        song,
                                        (XShortResourceID)theID,
                                        (void *)pXSong,
                                        NULL,
                                        0L,
                                        NULL, // no callback
                                        FALSE, // load after RMF metadata is attached
                                        ignoreBadInstruments,
                                        songBankToken,
                                        &theErr);
                    if (pSong)
                    {
#if USE_SF2_SUPPORT == TRUE
                        uint32_t instBuf[MAX_INSTRUMENTS];
                        uint32_t totalInst = 0;
                        uint32_t fileSize = (uint32_t)XFileGetLength(fileRef);
                        BAEUtil_GetRmfInstrumentList(fileRef, fileSize, songIndex, instBuf, MAX_INSTRUMENTS, &totalInst);
                        debug_message("pSong = %p\n", pSong);
                        debug_message("instBuf[0] = %d\n", instBuf[0]);
                        pSong->RMFInstrumentIDs[0] = totalInst; // Store count first
                        for (uint32_t i = 1; i <= totalInst; i++)
                        {
                            pSong->RMFInstrumentIDs[i] = instBuf[i-1];
                        }
                        debug_message("Found %u Instruments in RMF (stored=%u)\n", totalInst, (unsigned)XMIN(totalInst, MAX_INSTRUMENTS));
                        for (uint32_t i = 1; i <= totalInst; i++) {
                            debug_message("    %u - INST: %u\n", i, pSong->RMFInstrumentIDs[i]);
                        }
#else
                        pSong->RMFInstrumentIDs[0] = 0;
#endif
                        pSong->songFlags = SONG_FLAG_IS_RMF;
                        theErr = GM_LoadSongInstruments(pSong, NULL, songBankToken, TRUE);
                        if (theErr != NO_ERR)
                        {
                            GM_FreeSong(NULL, pSong);
                            pSong = NULL;
                        }
                    }
                    if (pSong)
                    {
                        // things are cool
                        GM_SetDisposeSongDataWhenDoneFlag(pSong, TRUE); // dispose of midi data
                        GM_SetSongLoopFlag(pSong, FALSE);               // don't loop song
                        GM_SetVelocityCurveType(pSong, (VelocityCurveType)g_defaultVelocityCurve);
                        song->pSong = pSong;                            // preserve for use later
                        BAE_OverrideSongFromFile(pSong, filePath);
                        
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
        if (curveType > 5)
            curveType = 5; // engine currently supports 0..5
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
        for (int _n = 0; _n < 128; _n++)
            song->pSong->channelActiveNotes[channel][_n] = 0;
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
                for (int _n = 0; _n < 128; _n++)
                    outNotes[_n] = song->pSong->channelActiveNotes[channel][_n];
                // Compiler barrier: force fresh reads of the volatile
                // channelActiveNotes array on every invocation.  Clang may
                // otherwise hoist or coalesce reads across mutex boundaries.
                BAE_COMPILER_BARRIER();
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

// BAESong_GetAllActiveNotes()
// --------------------------------------
// Thread-safe copy of current active note velocities for all 16 channels
// into a single 16×128 buffer with one mutex acquisition.
BAEResult BAESong_GetAllActiveNotes(BAESong song, unsigned char outNotes[BAE_MAX_MIDI_CHANNELS][BAE_MAX_NOTES])
{
    OPErr err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        if (outNotes)
        {
            memset(outNotes, 0, BAE_MAX_MIDI_CHANNELS * BAE_MAX_NOTES);
            BAE_AcquireMutex(song->mLock);
            if (song->pSong)
            {
                for (int _c = 0; _c < BAE_MAX_MIDI_CHANNELS; _c++)
                    for (int _n = 0; _n < BAE_MAX_NOTES; _n++)
                        outNotes[_c][_n] = song->pSong->channelActiveNotes[_c][_n];
                // Compiler barrier: force fresh reads of the volatile
                // channelActiveNotes array on every invocation.
                BAE_COMPILER_BARRIER();
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
            /* mInstrumentsLoadedCount only tracks BAESong_LoadInstrument calls.
             * Songs loaded via GM_LoadSongInstruments leave it at 0; treating
             * that as "last instrument" and Stop()'ing here killed RMF preview
             * whenever the editor remounted a single INST (keysplit sample
             * change, etc.). Only adjust the counter on successful unload. */
            if (err == NO_ERR && song->mInstrumentsLoadedCount)
            {
                song->mInstrumentsLoadedCount--;
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

// BAESong_PatchLoadedInstrumentExtInfo()
// --------------------------------------
// Modify the ADSR/LFO/LPF parameters of an already-loaded instrument
// in place so that subsequent notes hear the modified params.
//
static void PV_PatchInstrumentEnvelopes(GM_Instrument *theI,
                                         BAERmfEditorInstrumentExtInfo const *info,
                                         BAE_BOOL applyHeaderMiscRoot)
{
    int32_t i, j;

    /* ADSR envelope */
    for (i = 0; i < (int32_t)info->volumeADSR.stageCount && i < ADSR_STAGES; i++)
    {
        theI->volumeADSRRecord.ADSRLevel[i] = info->volumeADSR.stages[i].level;
        theI->volumeADSRRecord.ADSRTime[i] = info->volumeADSR.stages[i].time;
        theI->volumeADSRRecord.ADSRFlags[i] =
            PV_TranslateFromFileToMemoryID((uint32_t)info->volumeADSR.stages[i].flags);
    }
    for (; i < ADSR_STAGES; i++)
    {
        theI->volumeADSRRecord.ADSRLevel[i] = 0;
        theI->volumeADSRRecord.ADSRTime[i] = 0;
        theI->volumeADSRRecord.ADSRFlags[i] = ADSR_OFF;
    }
    /* Stage remove / shrink must reset playback so the next NoteOn does not
     * continue from a removed stage index. */
    theI->volumeADSRRecord.currentTime = 0;
    theI->volumeADSRRecord.currentPosition = 0;
    theI->volumeADSRRecord.currentLevel = 0;
    theI->volumeADSRRecord.previousTarget = 0;
    theI->volumeADSRRecord.mode = 0;
    theI->volumeADSRRecord.sustainingDecayLevel = XFIXED_1;

    /* INST header pitch / root flags — IE "MIDI root key" and flag toggles.
     * When useSoundModifierAsRootKey, miscParameter1 is the pitch root (do not
     * overwrite it from SND baseMidiPitch — that is often middle-C / 60).
     * Never stamp parent-header misc onto keymap split children. */
    theI->masterRootKey = info->midiRootKey;
    theI->useSoundModifierAsRootKey =
        TEST_FLAG_VALUE(info->flags2, ZBF_useSoundModifierAsRootKey) ? TRUE : FALSE;
    if (applyHeaderMiscRoot &&
        theI->useSoundModifierAsRootKey &&
        !theI->doKeymapSplit &&
        !theI->sampleOffsetStartEnabled)
    {
        theI->miscParameter1 = info->miscParameter1;
    }

    /* Live IE preview: INST flags2 is authoritative for the toggle (including
     * keymap splits patched below). Disk/SND bit is applied on full GenPatch load. */
    theI->advancedInterpolation =
        TEST_FLAG_VALUE(info->flags2, ZBF_advancedInterpolation) ? TRUE : FALSE;

    /* LPF */
    theI->LPF_frequency = info->LPF_frequency;
    theI->LPF_resonance = info->LPF_resonance;
    theI->LPF_lowpassAmount = info->LPF_lowpassAmount;

#if USE_ZMF_SUPPORT == TRUE
    theI->useOscillator = info->useOscillator ? TRUE : FALSE;
    if (theI->useOscillator)
    {
        theI->oscWaveShape = PV_TranslateFromFileToMemoryID((uint32_t)info->oscWaveShape);
        if (theI->oscWaveShape == 0 || theI->oscWaveShape == SINE_WAVE)
            theI->oscWaveShape = SINE_WAVE_REAL;
        theI->oscPulseWidth = info->oscPulseWidth > 0 ? info->oscPulseWidth : 32768;
        theI->oscVolume = info->oscVolume;
        if (theI->oscVolume < 0)
            theI->oscVolume = 0;
        if (theI->oscVolume > 65536)
            theI->oscVolume = 65536;
    }
#endif

    /* LFOs — replace the editor list. Optionally keep one trailing engine
     * default mod-wheel pitch LFO (PV_GetEnvelopeData when !INST_DEFAULT_MOD).
     * The old "restore originalLFOCount" path left removed IE LFOs audible. */
    {
        int32_t originalLFOCount = theI->LFORecordCount;
        int32_t patchedCount;
        GM_LFO savedDefault;
        bool haveDefault = FALSE;

        XSetMemory(&savedDefault, (int32_t)sizeof(savedDefault), 0);
        if (!info->hasDefaultMod)
        {
            for (i = 0; i < originalLFOCount && i < MAX_LFOS; i++)
            {
                GM_LFO *p = &theI->LFORecords[i];
                /* Match GenPatch.c default mod-wheel pitch LFO signature. */
                if (p->where_to_feed == PITCH_LFO && p->period == 180000 &&
                    p->DC_feed == 0 && p->level == 64)
                {
                    savedDefault = *p;
                    haveDefault = TRUE;
                }
            }
        }

        theI->LFORecordCount = 0;
        for (i = 0; i < (int32_t)info->lfoCount && i < MAX_LFOS; i++)
        {
            GM_LFO *pLFO = &theI->LFORecords[i];
            pLFO->where_to_feed = PV_TranslateFromFileToMemoryID(
                (uint32_t)info->lfos[i].destination);
#if DEBUG
            debug_message("  lfo[%d] dest=0x%x->%d period=%d shape=0x%x dc=%d level=%d adsr.stages=%u\n",
                       (int)i, (unsigned)info->lfos[i].destination, (int)pLFO->where_to_feed,
                       (int)info->lfos[i].period, (unsigned)info->lfos[i].waveShape,
                       (int)info->lfos[i].DC_feed, (int)info->lfos[i].level,
                       (unsigned)info->lfos[i].adsr.stageCount);
#endif
            pLFO->period = info->lfos[i].period;
            if (pLFO->period != 0 && pLFO->period <= 512)
                pLFO->period = 0; // disable invalid LFO period
            pLFO->waveShape = PV_TranslateFromFileToMemoryID(
                (uint32_t)info->lfos[i].waveShape);
            pLFO->DC_feed = info->lfos[i].DC_feed;
            pLFO->level = info->lfos[i].level;

            for (j = 0; j < (int32_t)info->lfos[i].adsr.stageCount && j < ADSR_STAGES; j++)
            {
                pLFO->a.ADSRLevel[j] = info->lfos[i].adsr.stages[j].level;
                pLFO->a.ADSRTime[j] = info->lfos[i].adsr.stages[j].time;
                pLFO->a.ADSRFlags[j] = PV_TranslateFromFileToMemoryID(
                    (uint32_t)info->lfos[i].adsr.stages[j].flags);
            }
            for (; j < ADSR_STAGES; j++)
            {
                pLFO->a.ADSRLevel[j] = 0;
                pLFO->a.ADSRTime[j] = 0;
                pLFO->a.ADSRFlags[j] = ADSR_OFF;
            }
            pLFO->a.currentTime = 0;
            pLFO->a.currentPosition = 0;
            pLFO->a.currentLevel = 0;
            pLFO->a.previousTarget = 0;
            pLFO->a.mode = 0;
            pLFO->a.sustainingDecayLevel = XFIXED_1;
            pLFO->currentWaveValue = 0;
            pLFO->currentTime = 0;
            pLFO->LFOcurrentTime = 0;
            theI->LFORecordCount++;
        }
        patchedCount = theI->LFORecordCount;
        for (i = patchedCount; i < MAX_LFOS; i++)
        {
            XSetMemory(&theI->LFORecords[i], (int32_t)sizeof(GM_LFO), 0);
        }
        if (haveDefault && patchedCount < MAX_LFOS)
        {
            theI->LFORecords[patchedCount] = savedDefault;
            theI->LFORecordCount = patchedCount + 1;
        }
    }
}

BAEResult BAESong_PatchLoadedInstrumentExtInfo(BAESong song,
                                               BAE_INSTRUMENT instrument,
                                               BAERmfEditorInstrumentExtInfo const *info)
{
    OPErr err;
    GM_Song *pSong;
    GM_Instrument *theI;
    XLongResourceID realInstrument;

    err = NO_ERR;
    if (!info)
    {
        return BAE_PARAM_ERR;
    }
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        pSong = song->pSong;
        if (!pSong)
        {
            BAE_ReleaseMutex(song->mLock);
            return BAE_NOT_SETUP;
        }

        if (GM_GetSongInstrumentRemap(pSong, (XLongResourceID)instrument,
                                       &realInstrument) != NO_ERR)
        {
            realInstrument = (XLongResourceID)instrument;
        }
        if (realInstrument < 0 ||
            realInstrument >= (XLongResourceID)(MAX_INSTRUMENTS * MAX_BANKS))
        {
            BAE_ReleaseMutex(song->mLock);
            return BAE_PARAM_ERR;
        }

        theI = pSong->instrumentData[realInstrument];
        if (!theI)
        {
            BAE_ReleaseMutex(song->mLock);
            return BAE_NOT_SETUP;
        }

        /* Patch the main instrument. Keysplit parents never own a sample root in
         * header miscParameter1 — only apply that word for flat INST bodies. */
        PV_PatchInstrumentEnvelopes(theI, info,
                                    theI->doKeymapSplit ? FALSE : TRUE);

        /* Propagate envelopes/LFOs/flags to keymap splits. Leave each split's
         * miscParameter1 (useSoundModifierAsRootKey pitch root) alone unless a
         * sampleOverride supplies a new root — do not stomp with SND baseKey. */
        if (theI->doKeymapSplit)
        {
            uint16_t splitCount = theI->u.k.KeymapSplitCount;
            uint16_t s;
            for (s = 0; s < splitCount; s++)
            {
                GM_KeymapSplit *ks = &theI->u.k.keySplits[s];
                GM_Instrument *theS = ks->pSplitInstrument;
                if (theS)
                {
                    PV_PatchInstrumentEnvelopes(theS, info, FALSE);
                }
            }
        }

        /* Sample-level overrides (root key, sample rate, loop, key range).
         * sampleRootKey is SND baseMidiPitch when !useSoundModifierAsRootKey,
         * else the per-split / INST misc pitch root. */
        if (info->hasSampleOverride)
        {
            uint32_t idx = info->sampleOverrideIndex;
            if (theI->doKeymapSplit)
            {
                uint16_t splitCount = theI->u.k.KeymapSplitCount;
                if (idx < (uint32_t)splitCount)
                {
                    GM_KeymapSplit *ks = &theI->u.k.keySplits[idx];
                    GM_Instrument *theS = ks->pSplitInstrument;
                    ks->lowMidi = info->sampleLowKey;
                    ks->highMidi = info->sampleHighKey;
                    ks->miscParameter2 = (int16_t)info->sampleSplitVolume;
                    if (theS && !theS->doKeymapSplit)
                    {
                        /* sampleRate==0 means keymap-only edit — do not silence. */
                        if (info->sampleRate != 0)
                        {
                            theS->u.w.sampledRate = (XFIXED)((uint32_t)info->sampleRate << 16);
                            theS->u.w.startLoop = (uint32_t)info->sampleLoopStart;
                            theS->u.w.endLoop = (uint32_t)info->sampleLoopEnd;
                        }
                        if (theS->useSoundModifierAsRootKey &&
                            !theS->sampleOffsetStartEnabled)
                        {
                            ks->miscParameter1 = (int16_t)info->sampleRootKey;
                            theS->miscParameter1 = (int16_t)info->sampleRootKey;
                        }
                        else
                        {
                            theS->u.w.baseMidiPitch = (uint16_t)info->sampleRootKey;
                        }
                        theS->miscParameter2 = (int16_t)info->sampleSplitVolume;
                    }
                }
            }
            else
            {
                /* Non-split instrument: patch the base waveform directly */
                if (idx == 0)
                {
                    if (info->sampleRate != 0)
                    {
                        theI->u.w.sampledRate = (XFIXED)((uint32_t)info->sampleRate << 16);
                        theI->u.w.startLoop = (uint32_t)info->sampleLoopStart;
                        theI->u.w.endLoop = (uint32_t)info->sampleLoopEnd;
                    }
                    if (theI->useSoundModifierAsRootKey &&
                        !theI->sampleOffsetStartEnabled)
                    {
                        theI->miscParameter1 = (int16_t)info->sampleRootKey;
                    }
                    else
                    {
                        theI->u.w.baseMidiPitch = (uint16_t)info->sampleRootKey;
                    }
                    theI->miscParameter2 = (int16_t)info->sampleSplitVolume;
                }
            }
            debug_message("PatchExtInfo: sampleOverride idx=%u rootKey=%u rate=%u loop=%u-%u lowKey=%u highKey=%u splitVol=%d smod=%d\n",
                       idx, (unsigned)info->sampleRootKey, info->sampleRate,
                       info->sampleLoopStart, info->sampleLoopEnd,
                       (unsigned)info->sampleLowKey, (unsigned)info->sampleHighKey,
                       (int)info->sampleSplitVolume,
                       theI->doKeymapSplit ? (theI->u.k.keySplits[idx].pSplitInstrument ?
                           theI->u.k.keySplits[idx].pSplitInstrument->useSoundModifierAsRootKey : -1) : theI->useSoundModifierAsRootKey);
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
                                 unsigned char *outBank,
                                 bool useRawBank)
{
    OPErr err;
    int16_t bank, program;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outBank && outProgram)
        {
            if (song->pSong)
            {
                GM_GetProgramBank(song->pSong, channel, &program, &bank, useRawBank);
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
        BAESong_GetProgramBank(song, channel, &program, &bank, FALSE);
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

// BAESong_KillActiveNotes()
// --------------------------------------
// Hard-kill all active voices for this song, bypassing ADSR release,
// without stopping the song (safe for paused preview/audition songs).
// --------------------------------------
BAEResult BAESong_KillActiveNotes(BAESong song)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        GM_KillSongNotes(song->pSong);
        GM_KillSongEventsFromQueue(song->pSong);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_Panic()
// --------------------------------------
// Hard-kill all active voices for this song, bypassing ADSR release,
// then stop the song.
// --------------------------------------
BAEResult BAESong_Panic(BAESong song)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        GM_KillSongNotes(song->pSong);
        GM_KillSongEventsFromQueue(song->pSong);
        PV_BAESong_Stop(song, FALSE);
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
#if _DEBUG == TRUE
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
        debug_message("BAE-INJECT: time=%u %s (len=%d) hex=%s\n", time, desc, (int)length, hexdump);
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
    GM_Mixer *savedMixer = NULL;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        /* GM_PrerollSong still touches MusicGlobals (reverb / voice rebalance). */
        if (song->mixer)
            savedMixer = PV_BindBAEMixer(song->mixer);
        // auto level engaged
        err = GM_PrerollSong(song->pSong, NULL, FALSE, TRUE);
        if (song->mixer)
            GM_SetCurrentMixer(savedMixer);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// Fast MIDI+patch peak estimate → song normalize gain (no audio render).
BAEResult BAESong_NormalizeFromMidiEstimate(BAESong song,
                                            int32_t targetPeakPct,
                                            int32_t *outAppliedGainPct)
{
    OPErr err;
    int32_t gainPct = 100;

    if (outAppliedGainPct)
        *outAppliedGainPct = 100;

    if (!song || song->mID != OBJECT_ID)
        return BAE_TranslateOPErr(NULL_OBJECT);
    if (!song->mixer || song->mixer->mID != OBJECT_ID || !song->pSong)
        return BAE_TranslateOPErr(NOT_SETUP);

    err = GM_Song_EstimateNormalizePeak(song->pSong, targetPeakPct, &gainPct);
    if (outAppliedGainPct)
        *outAppliedGainPct = gainPct;
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
                    PV_RestoreSongEngineConfig(song);
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
#if SUPPORT_KARAOKE == TRUE
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
#if SUPPORT_KARAOKE == TRUE
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

static void PV_BAESong_SetProgramBankCallback(BAESong song, GM_ProgramBankCallbackPtr pCallback, void *callbackReference)
{
    if (song && song->pSong)
    {
        song->pSong->programBankCallbackPtr = pCallback;
        song->pSong->programBankCallbackReference = callbackReference;
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

BAEResult BAESong_SetProgramBankCallback(BAESong song, GM_ProgramBankCallbackPtr pCallback, void *callbackReference)
{
    OPErr err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        PV_BAESong_SetProgramBankCallback(song, pCallback, callbackReference);
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
// Apply per-song engine config overrides from the song resource (SONG_CONFIG_* flags
// stored in SongResource_RMF.unused[0]).  Saves the current mixer values so they
// can be restored when the song ends.
static void PV_ApplySongEngineConfig(BAESong song)
{
    GM_Mixer *pMixer;
    uint32_t flags;

    if (!song || !song->pSong)
        return;

    pMixer = MusicGlobals;
    if (!pMixer)
        return;

    flags = song->pSong->engineConfigFlags;
    // Only use flags if container is ZMF; old RMF files may have garbage
    if (!(flags & SONG_CONFIG_CONTAINER_IS_ZMF))
    {
        return;  // not a ZMF file, discard flags
    }
    // ZMF file, clamp to valid bits to filter out any remaining garbage
    flags &= SONG_CONFIG_VALID_BITS_MASK;
    if (flags == 0)
        return;  // no per-song overrides

#if BAE_CLASSIC_CHORUS == TRUE
    if (flags & SONG_CONFIG_HAS_CLASSIC_CHORUS)
    {
        song->mSavedClassicChorus = pMixer->classicChorus;
        song->mHasSavedClassicChorus = TRUE;
        pMixer->classicChorus = (flags & SONG_CONFIG_CLASSIC_CHORUS_ON) ? TRUE : FALSE;
    }
#endif
#if BAE_FIX_SPAN_DC == TRUE
    if (flags & SONG_CONFIG_HAS_PANFIX)
    {
        song->mSavedFixSpanDC = pMixer->fixSpanDC;
        song->mHasSavedFixSpanDC = TRUE;
        pMixer->fixSpanDC = (flags & SONG_CONFIG_PANFIX_ON) ? TRUE : FALSE;
    }
#endif

    // Per-song volume curve override
    if (flags & SONG_CONFIG_OVERRIDE_VOLUME_CURVE)
    {
        VelocityCurveType curveType = (VelocityCurveType)((flags & SONG_CONFIG_VOLUME_CURVE_TYPE_MASK) >> SONG_CONFIG_VOLUME_CURVE_TYPE_SHIFT);
        if (curveType <= 5)
        {
            song->mSavedVelocityCurve = song->pSong->velocityCurveType;
            song->mHasSavedVelocityCurve = TRUE;
            GM_SetVelocityCurveType(song->pSong, curveType);
        }
    }
}

// Restore mixer values that were overridden by PV_ApplySongEngineConfig.
static void PV_RestoreSongEngineConfig(BAESong song)
{
    GM_Mixer *pMixer;

    if (!song)
        return;

    pMixer = MusicGlobals;
    if (!pMixer)
        return;

#if BAE_CLASSIC_CHORUS
    if (song->mHasSavedClassicChorus)
    {
        pMixer->classicChorus = song->mSavedClassicChorus;
        song->mHasSavedClassicChorus = FALSE;
    }
#endif
#if BAE_FIX_SPAN_DC
    if (song->mHasSavedFixSpanDC)
    {
        pMixer->fixSpanDC = song->mSavedFixSpanDC;
        song->mHasSavedFixSpanDC = FALSE;
    }
#endif

    if (song->mHasSavedVelocityCurve)
    {
        GM_SetVelocityCurveType(song->pSong, song->mSavedVelocityCurve);
        song->mHasSavedVelocityCurve = FALSE;
    }
}

void BAE_OverrideBAESongFromFile(void *songObject, const void *filePath)
{
    BAESong song = (BAESong)songObject;

    if (!song || song->mID != OBJECT_ID || !song->pSong || !filePath)
        return;

    BAE_OverrideSongFromFile(song->pSong, filePath);
}

//
//
BAEResult BAESong_Start(BAESong song, int16_t priority)
{
    OPErr err;
    GM_Mixer *savedMixer = NULL;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (song->mixer)
        {
            savedMixer = PV_BindBAEMixer(song->mixer);
            GM_SetSongPriority(song->pSong, priority);
            GM_SetSongVolume(song->pSong, song->mVolume);
            GM_SetSongRouteBus(song->pSong, song->mRouteBus);

            // auto level engaged
            err = GM_BeginSong(song->pSong, NULL, FALSE, TRUE);
            if (err == NO_ERR)
            {
                song->mInMixer = TRUE;
                GM_SetSongCallback(song->pSong, PV_DefaultSongDoneCallback, (void *)song);
                PV_ApplySongEngineConfig(song);
            }
            GM_SetCurrentMixer(savedMixer);
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
    GM_Mixer *savedMixer = NULL;

    if (song->mixer)
        savedMixer = PV_BindBAEMixer(song->mixer);

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
        PV_RestoreSongEngineConfig(song);
    }

    if (song->mixer)
        GM_SetCurrentMixer(savedMixer);
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
    int16_t minVolume;
    int16_t maxVolume;
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
            debug_message("[SetLoops] Setting song loop count to %d\n", numLoops);
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

BAEResult BAESong_GetMicrosecondLength64(BAESong song, uint64_t *outLength)
{
    OPErr err = NO_ERR;

    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outLength)
            *outLength = GM_GetSongMicrosecondLength64(song->pSong, &err);
        else
            err = PARAM_ERR;
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_GetTickLength()
// --------------------------------------
//
//
BAEResult BAESong_GetTickLength(BAESong song, uint32_t *outLength)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outLength)
        {
            *outLength = GM_GetSongTickLength(song->pSong, &err);
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

BAEResult BAESong_GetTickLength64(BAESong song, uint64_t *outLength)
{
    OPErr err = NO_ERR;

    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outLength)
            *outLength = GM_GetSongTickLength64(song->pSong, &err);
        else
            err = PARAM_ERR;
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

BAEResult BAESong_SetMicrosecondPosition64(BAESong song, uint64_t ticks)
{
    OPErr err = NO_ERR;

    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (song->pSong)
            err = GM_SetSongMicrosecondPosition64(song->pSong, ticks);
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

BAEResult BAESong_GetMicrosecondPosition64(BAESong song, uint64_t *outTicks)
{
    OPErr err = NO_ERR;

    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outTicks)
            *outTicks = GM_SongMicroseconds64(song->pSong);
        else
            err = PARAM_ERR;
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_SetTickPosition()
// --------------------------------------
//
//
BAEResult BAESong_SetTickPosition(BAESong song, uint32_t ticks)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (song->pSong)
        {
            err = GM_SetSongTickPosition(song->pSong, ticks);
        }
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

BAEResult BAESong_SetTickPosition64(BAESong song, uint64_t ticks)
{
    OPErr err = NO_ERR;

    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (song->pSong)
            err = GM_SetSongTickPosition64(song->pSong, ticks);
        BAE_ReleaseMutex(song->mLock);
    }
    else
    {
        err = NULL_OBJECT;
    }
    return BAE_TranslateOPErr(err);
}

// BAESong_GetTickPosition()
// --------------------------------------
//
//
BAEResult BAESong_GetTickPosition(BAESong song, uint32_t *outTicks)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outTicks)
        {
            *outTicks = GM_SongTicks(song->pSong);
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

BAEResult BAESong_GetTickPosition64(BAESong song, uint64_t *outTicks)
{
    OPErr err = NO_ERR;

    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outTicks)
            *outTicks = GM_SongTicks64(song->pSong);
        else
            err = PARAM_ERR;
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

// BAESong_IsRolledMIDI()
// --------------------------------------
// returns TRUE if the loaded MIDI uses Beatnik/WebTV loop-driven track rolling
//
BAEResult BAESong_IsRolledMIDI(BAESong song, BAE_BOOL *outIsRolled)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        BAE_AcquireMutex(song->mLock);
        if (outIsRolled)
        {
            *outIsRolled = FALSE;
            if (song->pSong)
            {
                *outIsRolled = PV_IsRolledMIDISequence(song->pSong);
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

BAEResult BAEUtil_UnrollRolledMidiFromMemory(void const *pMidiData,
                                             uint32_t midiSize,
                                             uint32_t options,
                                             unsigned char **ppMidiOut,
                                             uint32_t *pMidiSizeOut,
                                             BAE_BOOL *outWasRolled)
{
#if BAE_ENABLE_ROLLED_MIDI_UNROLL == TRUE
    return PV_UnrollRolledMidi((const unsigned char *)pMidiData,
                               midiSize,
                               options,
                               ppMidiOut,
                               pMidiSizeOut,
                               outWasRolled);
#else
    (void)pMidiData;
    (void)midiSize;
    (void)options;
    if (ppMidiOut)
    {
        *ppMidiOut = NULL;
    }
    if (pMidiSizeOut)
    {
        *pMidiSizeOut = 0;
    }
    if (outWasRolled)
    {
        *outWasRolled = FALSE;
    }
    return BAE_UNSUPPORTED;
#endif
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

// BAESong_SetTempoBPM()
// --------------------------------------
//
//
BAEResult BAESong_SetTempoBPM(BAESong song, uint32_t bpm)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        if ((bpm > 0) && (bpm < 500))
        {
            BAE_AcquireMutex(song->mLock);
            GM_SetSongTempInBeatsPerMinute(song->pSong, bpm);
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

// BAESong_GetTempoBPM()
// --------------------------------------
//
//
BAEResult BAESong_GetTempoBPM(BAESong song, uint32_t *outBPM)
{
    OPErr err;

    err = NO_ERR;
    if ((song) && (song->mID == OBJECT_ID))
    {
        if (outBPM)
        {
            BAE_AcquireMutex(song->mLock);
            *outBPM = GM_GetSongTempoInBeatsPerMinute(song->pSong);
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
static uint16_t PV_RmfListReadBE16(unsigned char const *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t PV_RmfListReadBE32(unsigned char const *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

#if USE_ZMF_SUPPORT == TRUE
static void PV_RmfListAppendInstrument(uint32_t instId,
                                       uint32_t *pOutInstruments,
                                       uint32_t maxInstruments,
                                       uint32_t *pInstrumentCount)
{
    if (pOutInstruments && *pInstrumentCount < maxInstruments)
    {
        pOutInstruments[*pInstrumentCount] = instId;
    }
    (*pInstrumentCount)++;
}

static void PV_RmfListAppendZinsInstEntries(XPTR decodedBlock,
                                            int32_t decodedSize,
                                            uint32_t *pOutInstruments,
                                            uint32_t maxInstruments,
                                            uint32_t *pInstrumentCount)
{
    unsigned char *p;
    unsigned char *end;
    uint32_t version;
    uint32_t count;

    if (!decodedBlock || decodedSize < 12 || !pInstrumentCount)
    {
        return;
    }

    p = (unsigned char *)decodedBlock;
    end = p + decodedSize;

    if (PV_RmfListReadBE32(p) != (uint32_t)FOUR_CHAR('Z', 'I', 'N', 'S'))
    {
        return;
    }

    version = PV_RmfListReadBE32(p + 4);
    if (version != 1u && version != 2u)
    {
        return;
    }

    count = PV_RmfListReadBE32(p + 8);
    p += 12;

    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t type = (uint32_t)ID_INST;
        uint32_t id;
        uint16_t nameLen;
        uint32_t dataLen;

        if (version >= 2u)
        {
            if (p + 4 > end)
            {
                return;
            }
            type = PV_RmfListReadBE32(p);
            p += 4;
        }

        if (p + 10 > end)
        {
            return;
        }

        id = PV_RmfListReadBE32(p);
        p += 4;
        nameLen = PV_RmfListReadBE16(p);
        p += 2;
        dataLen = PV_RmfListReadBE32(p);
        p += 4;

        if (p + nameLen > end)
        {
            return;
        }
        p += nameLen;

        if (p + dataLen > end)
        {
            return;
        }

        if ((XResourceType)type == ID_INST)
        {
            PV_RmfListAppendInstrument(id, pOutInstruments, maxInstruments, pInstrumentCount);
        }

        p += dataLen;
    }
}
#endif

// BAEUtil_GetRmfInstrumentListFromMemory()
// --------------------------------------
//
// Gets the list of instruments used by the RMF/ZMF from memory buffer
BAEResult BAEUtil_GetRmfInstrumentListFromMemory(void const *pRMFData, uint32_t rmfSize, int16_t songIndex,
                                       uint32_t *pOutInstruments, uint32_t maxInstruments,
                                       uint32_t *pOutNumInstruments)
{
    (void)songIndex; // current implementation ignores song filtering; could refine later
    if (!pRMFData || rmfSize < 12 || !pOutNumInstruments) return BAE_PARAM_ERR;

    const char *rmfData = (const char *)pRMFData;

    if (memcmp(rmfData, "IREZ", 4) != 0 
#if USE_ZMF_SUPPORT == TRUE
    && memcmp(rmfData, "ZREZ", 4) != 0
#endif
    ) {
        return BAE_PARAM_ERR;
    }

    const uint8_t headerMaxLen = 24;
    uint32_t numResources = (rmfData[8] << 24) | (rmfData[9] << 16) | (rmfData[10] << 8) | rmfData[11];
    uint32_t instrumentCount = 0;
    size_t offset = 12;

    for (uint32_t i = 0; i < numResources && offset + 13 <= rmfSize; i++) {
        if (offset + 13 > rmfSize) break;
        uint32_t nextOffset = PV_RmfListReadBE32((unsigned char const *)&rmfData[offset]);
        char type[5]; memcpy(type, &rmfData[offset + 4], 4); type[4] = '\0';
        uint32_t id = PV_RmfListReadBE32((unsigned char const *)&rmfData[offset + 8]);
        uint8_t nameLen = (uint8_t)rmfData[offset + 12];
        size_t bodyLenOffset = offset + 13 + nameLen;
        if (nameLen >= headerMaxLen || bodyLenOffset + 4 > rmfSize) break;
        uint32_t bodyLen = PV_RmfListReadBE32((unsigned char const *)&rmfData[bodyLenOffset]);
        unsigned char *bodyPtr = (unsigned char *)&rmfData[bodyLenOffset + 4];
        if (type[0]=='I' && type[1]=='N' && type[2]=='S' && type[3]=='T') {
            if (pOutInstruments && instrumentCount < maxInstruments) pOutInstruments[instrumentCount] = id;
            instrumentCount++;
        }
#if USE_ZMF_SUPPORT == TRUE
        if (type[0]=='Z' && type[1]=='I' && type[2]=='N' && type[3]=='S') {
            XPTR decodedBlock = NULL;

            if (bodyLenOffset + 4 + bodyLen > rmfSize)
            {
                break;
            }

            decodedBlock = XDecompressPtr(bodyPtr, bodyLen, TRUE);
            if (decodedBlock)
            {
                int32_t decodedSize = XGetPtrSize(decodedBlock);
                PV_RmfListAppendZinsInstEntries(decodedBlock,
                                                decodedSize,
                                                pOutInstruments,
                                                maxInstruments,
                                                &instrumentCount);
                XDisposePtr(decodedBlock);
            }
        }
#endif
        if (nextOffset == 0xFFFFFFFF) break;
        if (nextOffset <= offset || nextOffset > rmfSize) break;
        offset = nextOffset;
    }

    *pOutNumInstruments = instrumentCount; // total discovered (may exceed maxInstruments)
    return BAE_NO_ERROR;
}

// BAEUtil_GetRmfInstrumentList()
// --------------------------------------
//
// Gets the list of instruments used by the RMF/ZMF
BAEResult BAEUtil_GetRmfInstrumentList(void *pRMFData, uint32_t rmfSize, int16_t songIndex,
                                       uint32_t *pOutInstruments, uint32_t maxInstruments,
                                       uint32_t *pOutNumInstruments)
{
    if (!pRMFData || rmfSize < 12 || !pOutNumInstruments) return BAE_PARAM_ERR;

    XFILE fileRef = (XFILE)pRMFData; // caller must pass actual XFILE (not &fileRef)
    char *rmfData = (char *)XNewPtr(rmfSize);
    if (!rmfData) return BAE_MEMORY_ERR;
    XFileSetPosition(fileRef, 0);

    if (XFileRead(fileRef, rmfData, (int32_t)rmfSize) != 0) {
        XDisposePtr(rmfData);
        return BAE_FILE_IO_ERROR;
    }

    BAEResult result = BAEUtil_GetRmfInstrumentListFromMemory(rmfData, rmfSize, songIndex, pOutInstruments, maxInstruments, pOutNumInstruments);

    XDisposePtr(rmfData);
    return result;
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
    case E_SINC_INTERPOLATION_U3232:
        mode_out = BAE_SINC_INTERPOLATION;
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

// EOF NeoBAE.c

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

#if USE_OPUS_ENCODER == TRUE
// Translate OPUS compression enum to total stream bitrate in bits/sec.
uint32_t BAE_TranslateOpusTypeToBitrate(BAECompressionType ct)
{
    switch (ct)
    {
    case BAE_COMPRESSION_OPUS_16:
        return 16000;
    case BAE_COMPRESSION_OPUS_32:
        return 32000;
    case BAE_COMPRESSION_OPUS_64:
        return 64000;
    case BAE_COMPRESSION_OPUS_96:
        return 96000;
    case BAE_COMPRESSION_OPUS_128:
        return 128000;
    case BAE_COMPRESSION_OPUS_256:
        return 256000;
    default:
        return 128000;
    }
}
#endif

#if USE_MPEG_ENCODER == TRUE
// Refill callback: build next mixer slice of PCM into provided buffer.
bool PV_RefillMPEGEncodeBuffer(void *buffer, void *userRef)
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
    BAE_BuildMixerSlice(mixer->pMixer, buffer, mWritingDataBlockSize, frames);
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
#if USE_OPUS_DECODER == TRUE
        || ftype == BAE_OPUS_TYPE
#endif
#if USE_ADP_SUPPORT == TRUE
        || ftype == BAE_ADP_TYPE
#endif
#if USE_ADX_SUPPORT == TRUE
        || ftype == BAE_ADX_TYPE
#endif
#if USE_QOA_SUPPORT == TRUE
        || ftype == BAE_QOA_TYPE
#endif
#if USE_WMA_SUPPORT == TRUE
        || ftype == BAE_WMA_TYPE
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
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDLITE == TRUE && USE_RMI_SUPPORT == TRUE
        else if (ftype == BAE_RMI)
        {
            sr = BAESong_LoadRmiFromFile(result->data.song, filePath, 0, TRUE);
        }
#endif
#if USE_MTHC_SUPPORT == TRUE
        else if (ftype == BAE_MTHC)
        {
            XPTR pMidiData;
            int32_t midiSize;
            unsigned char *decodedMthcMidi = NULL;
            uint32_t decodedMthcMidiLen = 0;
            XFILENAME midiPath;
            XConvertPathToXFILENAME((void *)filePath, &midiPath);
            pMidiData = PV_GetFileAsData(&midiPath, &midiSize);
            if (!pMidiData || midiSize <= 0)
            {
                BAESong_Delete(result->data.song);
                result->data.song = NULL;
                result->result = BAE_FILE_IO_ERROR;
                return BAE_FILE_IO_ERROR;
            }

            if (mthc_decompress_memory((unsigned char const *)pMidiData,
                                        (uint32_t)midiSize,
                                        &decodedMthcMidi,
                                        &decodedMthcMidiLen) == 0)
            {
                sr = BAESong_LoadMidiFromMemory(result->data.song,
                                                (void const *)decodedMthcMidi,
                                                (uint32_t)decodedMthcMidiLen,
                                                TRUE);
                free(decodedMthcMidi);
                decodedMthcMidi = NULL;
                XDisposePtr(pMidiData);
                pMidiData = NULL;
            }
            else
            {
                XDisposePtr(pMidiData);
                pMidiData = NULL;
                BAESong_Delete(result->data.song);
                result->data.song = NULL;
                result->result = BAE_BAD_FILE;
                return BAE_BAD_FILE;
            }
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
#if USE_RETRO_RINGTONE_SUPPORT == TRUE           
        else if (ftype == BAE_RINGTONE_IMY || ftype == BAE_RINGTONE_RNG || ftype == BAE_RINGTONE_RTX)
        {
            unsigned char *midiOut = NULL;
            uint32_t midiOutSize = 0;

            sr = BAERingtone_ConvertToMidiFromFile(filePath, ftype, &midiOut, &midiOutSize);
            if (sr == BAE_NO_ERROR)
            {
                sr = BAESong_LoadMidiFromMemory(result->data.song, midiOut, midiOutSize, TRUE);
            }
            BAERingtone_FreeMidiBuffer(midiOut);
            midiOut = NULL;
        }
#endif        
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

    switch (result->type)
    {
        case BAE_LOAD_TYPE_SONG:
            if (result->data.song)
            {
                BAESong_Delete(result->data.song);
                result->data.song = NULL;
            }
            break;
        case BAE_LOAD_TYPE_SOUND:
            if (result->data.sound)
            {
                BAESound_Delete(result->data.sound);
                result->data.sound = NULL;
            }
            break;
        case BAE_LOAD_TYPE_STREAM:
            if (result->data.stream)
            {
                BAEStream_Delete(result->data.stream);
                result->data.stream = NULL;
            }
            break;
        case BAE_LOAD_TYPE_NONE:
        default:
            break;
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
    result->data.stream = NULL;

    // Detect file type from content.
    // Prefer explicit RMF/ZMF signatures so embedded-instrument playback
    // cannot be misrouted into the MIDI loader.
    int32_t probeSize = (int32_t)dataSize;
    BAEFileType ftype;

    if (probeSize > 64) probeSize = 64; // Generic probe only needs first 64 bytes

    ftype = BAE_INVALID_TYPE;
    if (dataSize >= 4)
    {
        const unsigned char *bytes = (const unsigned char *)pData;

        if ((bytes[0] == 'I' && bytes[1] == 'R' && bytes[2] == 'E' && bytes[3] == 'Z') ||
            (bytes[0] == 'Z' && bytes[1] == 'R' && bytes[2] == 'E' && bytes[3] == 'Z'))
        {
            ftype = BAE_RMF;
        }
    }
    if (ftype == BAE_INVALID_TYPE)
    {
        ftype = X_DetermineFileTypeByData((const unsigned char *)pData, probeSize);
    }
    result->fileType = ftype;
    
    debug_message("[BAEMixer_LoadFromMemory] Detected file type: %s\n", X_GetFileTypeString(ftype));
    
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
#if USE_OPUS_DECODER == TRUE
        || ftype == BAE_OPUS_TYPE
#endif
#if USE_QOA_SUPPORT == TRUE
        || ftype == BAE_QOA_TYPE
#endif
#if USE_ADP_SUPPORT == TRUE
        || ftype == BAE_ADP_TYPE
#endif
#if USE_ADX_SUPPORT == TRUE
        || ftype == BAE_ADX_TYPE
#endif
#if USE_WMA_SUPPORT == TRUE
        || ftype == BAE_WMA_TYPE
#endif
    )
    {
        isAudio = TRUE;
    }

    if (isAudio)
    {
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
#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDLITE == TRUE && USE_RMI_SUPPORT == TRUE
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
#if USE_MTHC_SUPPORT == TRUE
        else if (ftype == BAE_MTHC)
        {
            unsigned char *decodedMthcMidi = NULL;
            uint32_t decodedMthcMidiLen = 0;
            if (mthc_decompress_memory((unsigned char const *)pData,
                                        (uint32_t)dataSize,
                                        &decodedMthcMidi,
                                        &decodedMthcMidiLen) != 0)
            {
                BAESong_Delete(result->data.song);
                sr = BAE_BAD_FILE;
                result->data.song = NULL;
                result->result = sr;
                return sr;
            }
            
            sr = BAESong_LoadMidiFromMemory(result->data.song, (void *)decodedMthcMidi, (uint32_t)decodedMthcMidiLen, TRUE);
            free(decodedMthcMidi);
            decodedMthcMidi = NULL;
        }
#endif
        else if (ftype == BAE_MIDI_TYPE || ftype == BAE_RMI)
        {
            sr = BAESong_LoadMidiFromMemory(result->data.song, pData, dataSize, TRUE);
        }
#if USE_RETRO_RINGTONE_SUPPORT == TRUE        
        else if (ftype == BAE_RINGTONE_IMY || ftype == BAE_RINGTONE_RNG || ftype == BAE_RINGTONE_RTX)
        {
            unsigned char *midiOut = NULL;
            uint32_t midiOutSize = 0;

            sr = BAERingtone_ConvertToMidiFromMemory(pData, dataSize, ftype, &midiOut, &midiOutSize);
            if (sr == BAE_NO_ERROR)
            {
                sr = BAESong_LoadMidiFromMemory(result->data.song, midiOut, midiOutSize, TRUE);
            }
            BAERingtone_FreeMidiBuffer(midiOut);
            midiOut = NULL;
        }
#endif        
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

        // Apply hash-based runtime overrides (same as file-based load paths).
        // Android always loads from memory, so this is the only place to hook in.
        BAE_OverrideSongFromData(result->data.song->pSong, pData, dataSize);

        result->type = BAE_LOAD_TYPE_SONG;
        result->result = BAE_NO_ERROR;
        return BAE_NO_ERROR;
    }
}

// BAEMixer_ProbeSongLengthFromMemory()
// ------------------------------------
// Playlist-friendly duration probe: sequencer walk only, no instrument decode.
BAEResult BAEMixer_ProbeSongLengthFromMemory(BAEMixer mixer,
                                             void const *pData,
                                             uint32_t dataSize,
                                             uint32_t *outLengthMicros,
                                             BAEFileType *outFileType)
{
    BAEFileType ftype;
    BAESong song;
    BAEResult sr;
    uint32_t len;

    if (!mixer || !mixer->pMixer || !pData || dataSize == 0 || !outLengthMicros)
        return BAE_PARAM_ERR;

    *outLengthMicros = 0;
    if (outFileType)
        *outFileType = BAE_INVALID_TYPE;

    ftype = BAE_INVALID_TYPE;
    if (dataSize >= 4)
    {
        const unsigned char *bytes = (const unsigned char *)pData;
        if ((bytes[0] == 'I' && bytes[1] == 'R' && bytes[2] == 'E' && bytes[3] == 'Z') ||
            (bytes[0] == 'Z' && bytes[1] == 'R' && bytes[2] == 'E' && bytes[3] == 'Z'))
        {
            ftype = BAE_RMF;
        }
    }
    if (ftype == BAE_INVALID_TYPE)
    {
        int32_t probeSize = (int32_t)dataSize;
        if (probeSize > 64)
            probeSize = 64;
        ftype = X_DetermineFileTypeByData((const unsigned char *)pData, probeSize);
    }
    if (outFileType)
        *outFileType = ftype;
    if (ftype == BAE_INVALID_TYPE)
        return BAE_BAD_FILE;

    // Audio files are not songs — caller should not use this for sample media.
    if (ftype == BAE_WAVE_TYPE || ftype == BAE_AIFF_TYPE || ftype == BAE_AU_TYPE)
        return BAE_BAD_FILE_TYPE;

    song = BAESong_New(mixer);
    if (!song)
        return BAE_MEMORY_ERR;

    sr = BAE_GENERAL_ERR;

    if (ftype == BAE_RMF)
    {
        XFILE fileRef;
        SongResource *pXSong;
        GM_Song *pSong;
        OPErr theErr;
        XLongResourceID theID;
        int32_t size;
        bool isZmfContainer = FALSE;

        theErr = NO_ERR;
        fileRef = XFileOpenResourceFromMemory((XPTR)pData, dataSize, TRUE);
        if (!fileRef)
        {
            BAESong_Delete(song);
            return BAE_BAD_FILE;
        }

        {
            XFILERESOURCEMAP mapHdr;
            XFileSetPosition(fileRef, 0L);
            if (XFileRead(fileRef, &mapHdr, (int32_t)sizeof(XFILERESOURCEMAP)) == 0)
            {
                if (XGetLong(&mapHdr.mapID) == XFILERESOURCE_ZMF_ID)
                {
#if USE_ZMF_SUPPORT == TRUE
                    isZmfContainer = TRUE;
#else
                    XFileClose(fileRef);
                    BAESong_Delete(song);
                    return BAE_UNSUPPORTED_FORMAT;
#endif
                }
            }
        }

        pXSong = (SongResource *)XGetIndexedFileResource(fileRef, ID_SONG, &theID, 0, NULL, &size);
        if (pXSong && song->pSong)
        {
            PV_TagSongResourceContainerType(pXSong, isZmfContainer);
            PV_BAESong_Unload(song);
            pSong = GM_LoadSong(mixer->pMixer,
                                NULL,
                                song,
                                (XShortResourceID)theID,
                                (void *)pXSong,
                                NULL,
                                0L,
                                NULL,
                                FALSE, // do not decode INST/samples
                                TRUE,
                                CreateBankToken(),
                                &theErr);
            if (pSong && theErr == NO_ERR)
            {
                pSong->songFlags |= SONG_FLAG_IS_RMF;
                GM_SetDisposeSongDataWhenDoneFlag(pSong, TRUE);
                GM_SetSongLoopFlag(pSong, FALSE);
                song->pSong = pSong;
                sr = BAE_NO_ERROR;
            }
            else
            {
                sr = BAE_TranslateOPErr(theErr != NO_ERR ? theErr : BAD_FILE);
            }
            XDisposePtr(pXSong);
        }
        else
        {
            sr = BAE_BAD_FILE;
        }
        XFileClose(fileRef);
    }
    else if (ftype == BAE_MIDI_TYPE || ftype == BAE_RMI
#if USE_MTHC_SUPPORT == TRUE
             || ftype == BAE_MTHC
#endif
    )
    {
        // Full MIDI loader already walks the sequence; instrument load is the
        // expensive part for banks, but ignoreBad + builtin bank may still touch
        // patches. Prefer a direct loadInstruments=FALSE path.
        SongResource *pXSong;
        GM_Song *pSong;
        OPErr theErr = NO_ERR;
        XShortResourceID theID;
        short soundVoices, midiVoices, mixLevel;
        void *midiCopy;
        uint32_t midiSize = dataSize;
        const void *midiData = pData;

#if USE_MTHC_SUPPORT == TRUE
        unsigned char *decodedMthcMidi = NULL;
        uint32_t decodedMthcMidiLen = 0;
        if (ftype == BAE_MTHC || (dataSize >= 4 && memcmp(pData, "MThc", 4) == 0))
        {
            if (mthc_decompress_memory((unsigned char const *)pData, dataSize, &decodedMthcMidi, &decodedMthcMidiLen) != 0)
            {
                BAESong_Delete(song);
                return BAE_BAD_FILE;
            }
            midiData = decodedMthcMidi;
            midiSize = decodedMthcMidiLen;
        }
#endif

        midiCopy = XDuplicateMemory((XPTRC)midiData, (uint32_t)midiSize);
#if USE_MTHC_SUPPORT == TRUE
        if (decodedMthcMidi)
        {
            free(decodedMthcMidi);
            decodedMthcMidi = NULL;
        }
#endif
        if (!midiCopy)
        {
            BAESong_Delete(song);
            return BAE_MEMORY_ERR;
        }

        theID = midiSongCount++;
        BAEMixer_GetMidiVoices(mixer, &midiVoices);
        BAEMixer_GetMixLevel(mixer, &mixLevel);
        BAEMixer_GetSoundVoices(mixer, &soundVoices);
        pXSong = XNewSongPtr(SONG_TYPE_SMS, theID, midiVoices, mixLevel, soundVoices, REVERB_TYPE_1);
        if (pXSong && song->pSong)
        {
            PV_BAESong_Unload(song);
            pSong = GM_LoadSong(mixer->pMixer,
                                NULL,
                                song,
                                theID,
                                (void *)pXSong,
                                midiCopy,
                                (int32_t)midiSize,
                                NULL,
                                FALSE,
                                TRUE,
                                CreateBankToken(),
                                &theErr);
            if (pSong && theErr == NO_ERR)
            {
                GM_SetDisposeSongDataWhenDoneFlag(pSong, TRUE);
                GM_SetSongLoopFlag(pSong, FALSE);
                song->pSong = pSong;
                sr = BAE_NO_ERROR;
            }
            else
            {
                XDisposePtr(midiCopy);
                sr = BAE_TranslateOPErr(theErr != NO_ERR ? theErr : BAD_FILE);
            }
            XDisposePtr(pXSong);
        }
        else
        {
            XDisposePtr(midiCopy);
            sr = BAE_MEMORY_ERR;
        }
    }
#if USE_RETRO_RINGTONE_SUPPORT == TRUE
    else if (ftype == BAE_RINGTONE_IMY || ftype == BAE_RINGTONE_RNG || ftype == BAE_RINGTONE_RTX)
    {
        unsigned char *midiOut = NULL;
        uint32_t midiOutSize = 0;
        sr = BAERingtone_ConvertToMidiFromMemory(pData, dataSize, ftype, &midiOut, &midiOutSize);
        if (sr == BAE_NO_ERROR && midiOut && midiOutSize)
        {
            BAESong_Delete(song);
            song = NULL;
            {
                BAEResult nested = BAEMixer_ProbeSongLengthFromMemory(mixer, midiOut, midiOutSize, outLengthMicros, NULL);
                if (outFileType)
                    *outFileType = ftype;
                BAERingtone_FreeMidiBuffer(midiOut);
                return nested;
            }
        }
        BAERingtone_FreeMidiBuffer(midiOut);
    }
#endif
    else
    {
        // XMF and other containers can pull embedded banks; skip heavy probe.
        sr = BAE_NOT_SETUP;
    }

    if (sr == BAE_NO_ERROR && song && song->pSong)
    {
        len = 0;
        sr = BAESong_GetMicrosecondLength(song, &len);
        if (sr == BAE_NO_ERROR)
            *outLengthMicros = (len == 0) ? 1000u : len;
    }

    if (song)
        BAESong_Delete(song);
    return sr;
}

bool GM_IsAudioTailActive(GM_Mixer *mixer)
{
    if (!mixer)
        return FALSE;
      
    NewReverbParams *nr = GetNewReverbParams();

    bool needCheck = FALSE;
    if (mixer && mixer->reverbBuffer && mixer->reverbBufferSize > 0)
        needCheck = TRUE;
    if (nr && nr->mIsInitialized)
        needCheck = TRUE;
    if (!needCheck && !BAENeoReverb_IsActive())
        return FALSE; // no reverb buffers allocated
                    
    bool foundNonZero = FALSE;

    // 1) Legacy fixed reverb buffer
    if (!foundNonZero && mixer && mixer->reverbBuffer && mixer->reverbBufferSize > 0)
    {
        uint32_t wb = mixer->reverbBufferSize;
        uint32_t wp = (uint32_t)mixer->reverbPtr;
        uint32_t window = (wb < 1024) ? wb : 1024;
        uint32_t start = (wp >= window) ? (wp - window) : 0;
        for (uint32_t j = 0; j < window; ++j)
        {
            uint32_t idx = (start + j) % wb;
            if (mixer->reverbBuffer[idx] != 0)
            {
                foundNonZero = TRUE;
                break;
            }
        }
    }

    // 2) New reverb buffers (comb filters, early reflections, diffusion, stereoizer)
    if (!foundNonZero && nr && nr->mIsInitialized)
    {
        // sample a small window around each write pointer for signs of activity
        const uint32_t sampleWindow = 256;

        // comb filters
        for (int ci = 0; ci < kNumberOfCombFilters && !foundNonZero; ++ci)
        {
            if (nr->mReverbBuffer[ci])
            {
                uint32_t wb = (uint32_t)kCombBufferFrameSize;
                uint32_t wp = (uint32_t)nr->mWriteIndex[ci];
                uint32_t window = (wb < sampleWindow) ? wb : sampleWindow;
                uint32_t start = (wp >= window) ? (wp - window) : 0;
                for (uint32_t j = 0; j < window; ++j)
                {
                    uint32_t idx = (start + j) % wb;
                    if (nr->mReverbBuffer[ci][idx] != 0)
                    {
                        foundNonZero = TRUE;
                        break;
                    }
                }
            }
        }

        // early reflections
        if (!foundNonZero && nr->mEarlyReflectionBuffer)
        {
            uint32_t wb = (uint32_t)kEarlyReflectionBufferFrameSize;
            uint32_t wp = (uint32_t)nr->mReflectionWriteIndex;
            uint32_t window = (wb < sampleWindow) ? wb : sampleWindow;
            uint32_t start = (wp >= window) ? (wp - window) : 0;
            for (uint32_t j = 0; j < window; ++j)
            {
                uint32_t idx = (start + j) % wb;
                if (nr->mEarlyReflectionBuffer[idx] != 0)
                {
                    foundNonZero = TRUE;
                    break;
                }
            }
        }

        // diffusion buffers
        for (int di = 0; di < kNumberOfDiffusionStages && !foundNonZero; ++di)
        {
            if (nr->mDiffusionBuffer[di])
            {
                uint32_t wb = (uint32_t)kDiffusionBufferFrameSize;
                uint32_t wp = (uint32_t)nr->mDiffWriteIndex[di];
                uint32_t window = (wb < sampleWindow) ? wb : sampleWindow;
                uint32_t start = (wp >= window) ? (wp - window) : 0;
                for (uint32_t j = 0; j < window; ++j)
                {
                    uint32_t idx = (start + j) % wb;
                    if (nr->mDiffusionBuffer[di][idx] != 0)
                    {
                        foundNonZero = TRUE;
                        break;
                    }
                }
            }
        }

        // stereoizer buffers
        if (!foundNonZero && (nr->mStereoizerBufferL || nr->mStereoizerBufferR))
        {
            uint32_t wb = (uint32_t)kStereoizerBufferFrameSize;
            uint32_t wp = (uint32_t)nr->mStereoWriteIndex;
            uint32_t window = (wb < sampleWindow) ? wb : sampleWindow;
            uint32_t start = (wp >= window) ? (wp - window) : 0;
            for (uint32_t j = 0; j < window && !foundNonZero; ++j)
            {
                uint32_t idx = (start + j) % wb;
                if ((nr->mStereoizerBufferL && nr->mStereoizerBufferL[idx] != 0) ||
                    (nr->mStereoizerBufferR && nr->mStereoizerBufferR[idx] != 0))
                {
                    foundNonZero = TRUE;
                    break;
                }
            }
        }
    }

#if USE_NEO_EFFECTS == TRUE
    // 3) Neo reverb: use public helper to determine if Neo is still active.
    if (!foundNonZero && BAENeoReverb_IsActive())
    {
        foundNonZero = TRUE;
    }
#endif

    return foundNonZero;
}

/* Public wrapper that accepts a BAEMixer handle and checks the underlying
 * GM_Mixer for an active audio tail. This avoids exposing sBAEMixer internals
 * to callers that only have a BAEMixer opaque pointer. */
bool BAEMixer_IsAudioTailActive(BAEMixer mixer)
{
    if (!mixer) return FALSE;
    if (!mixer->pMixer) return FALSE;
    return GM_IsAudioTailActive(mixer->pMixer) ? TRUE : FALSE;
}

bool PV_ExtractRMIDToSMF(const unsigned char *buf, uint32_t len,
                          const unsigned char **outSmf, uint32_t *outSmfLen)
{
    if (!buf || len < 12) return FALSE;

    if (!(buf[0] == 'R' && buf[1] == 'I' && buf[2] == 'F' && buf[3] == 'F')) return FALSE;

    uint32_t riffSize = PV_ReadLE32(&buf[4]);
    if (riffSize + 8 > len) return FALSE;

    if (!(buf[8] == 'R' && buf[9] == 'M' && buf[10] == 'I' && buf[11] == 'D')) return FALSE;

    uint32_t i = 12;
    while (i + 8 <= len)
    {
        const unsigned char *chunk = buf + i;
        uint32_t chunkSize = PV_ReadLE32(&chunk[4]);

        if (i + 8 + chunkSize > len) break;

        if (chunk[0] == 'd' && chunk[1] == 'a' && chunk[2] == 't' && chunk[3] == 'a')
        {
            if (outSmf) *outSmf = chunk + 8;
            if (outSmfLen) *outSmfLen = chunkSize;
            return TRUE;
        }

        i += 8 + chunkSize;
        if (i & 1) i++;
    }

    return FALSE;
}
