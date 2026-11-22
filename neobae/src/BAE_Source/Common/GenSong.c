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
** "GenSong.c"
**
**  Generalized Music Synthesis package. Part of SoundMusicSys.
**
**  © Copyright 1993-2001 Beatnik, Inc, All Rights Reserved.
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
** Overview
**  This contains code to load an maintain songs and midi data. This code library is specific
**  to the platform, but the API is independent in nature.
**
**  NOTE: relies on structures from SoundMusicSys API
**
**
** Modification History:
**
**  1/24/96     Created
**  1/25/96     Moved GM_GetUsedPatchlist from GenSeq.c
**  1/28/96     Moved GM_FreeSong from GenSeq.c
**  2/3/96      Removed extra includes
**  2/5/96      Removed unused variables. Working towards multiple songs
**              Added GM_GetSongTickLength
**  2/12/96     Added GM_SetSongTickPosition
**  3/1/96      Fixed bug with GM_SetSongTickPosition that would blow the songLoop flag away
**  3/5/96      Eliminated the global songVolume
**  4/15/96     Added support to interpret SONG resource via GM_MergeExternalSong
**  4/21/96     Removed CPU edian issues by use XGetShort & XGetLong
**              Removed register usage in parameters
**  5/18/96     More error correction in GM_GetSongTickLength & GM_SetSongTickPosition
**  5/30/96     Fixed possible bad code in PV_CreateSongFromMidi
**              Added ignore Bad patches code
**  6/7/96      Added some error correction changes to GM_LoadSong
**  7/4/96      Changed font and re tabbed
**  9/17/96     Added GM_LoadSongInstrument & GM_UnloadSongInstrument
**  9/23/96     Added channel muting feature
**  9/25/96     Changed GM_SetSongTickPosition to end just the song notes its working on
**  11/9/96     Fixed a bug with GM_FreeSong that would free instruments before
**              instruments were done playing
**  12/30/96    Changed copyright
**  1/12/97     Added support for more song types
**              Changed maxNormalizedVoices to mixLevel
**  1/28/97     Eliminated terminateDecay flag. Not used anymore
**  1/29/97     Reworked GM_LoadSong to support encrypted midi files
**  2/1/97      Added support for pitch offset control on a per channel basis
**  3/20/97     Added GM_SetSongMicrosecondPosition & GM_GetSongMicrosecondLength
**  4/20/97     Changed PV_MusicIRQ to PV_ProcessMidiSequencerSlice
**  5/3/97      Fixed a few potential problems that the MOT compiler found. Specificly
**              changed a theSong->songMidiTickLength = -1 to theSong->songMidiTickLength = 0
**              to signal calculate length.
**  7/29/97     (ddz) Added call to PV_FreePgmEntries in GM_FreeSong, and GM_GetSongProgramChanges
**  8/7/97      Changed GM_SongTicks & GM_SetSongTickPosition & GM_GetSongTickLength
**              to support UFLOAT
**  8/13/97     Renamed GM_GetSongProgramChanges to GM_GetSongInstrumentChanges and changed
**              Byte reference to unsigned char
**  8/15/97     Fixed a bug in which a data block was being freed in GM_FreeSong after
**              the main pointer was trashed.
**              Added disposal in GM_FreeSong of the new controller callbacks
**  9/5/97      (ddz) Made all pgm change stuff in one structure PatchInfo, changed PV_FreePgmEntries
**              to PV_FreePatchInfo
**  9/19/97     Fixed bug with GM_GetSongTickLength that allowed callbacks to be
**              called and freed during a length calculation
**  9/25/97     Added ddz changes, and wrapped a couple of function around USE_CREATION_API == TRUE
**  10/15/97    Modified GM_UnloadSongInstrument to handle the case in which a instrument
**              is still busy
**              Modified GM_FreeSong to handle the case in which instruments, samples, or
**              midi data is still busy.
**  10/16/97    Changed GM_LoadSong parmeters to include an option to ignore bad instruments
**              when loading.
**              Renamed ignoreBadPatches to ignoreBadInstruments
**  10/18/97    Fixed some compiler warnings and modified GM_FreeSong to kill midi data
**              pointer now rather than let the decoder thread get it later.
**  10/27/97    Removed reference to MusicGlobals->theSongPlaying
**  2/2/98      Added GM_SetVelocityCurveType
**  2/8/98      Changed BOOL_FLAG to bool
** JAVASOFT
**  02.10.98:   $$kk: GM_SetSongMicrosecondPosition: added check for whether
**              song was paused so that we don't resume if this method was called on a paused song.
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
**  6/30/98     Removed int16_t casting in GM_MergeExternalSong
**              Changed GM_LoadSong/GM_CreateLiveSong to accept a int32_t rather than a short for
**              the songID
**  7/6/98      Added GM_IsSongInstrumentLoaded
**              Fixed type problems with GM_LoadSong & PV_CreateSongFromMidi
** 2/18/99      Changed GM_LoadSong & GM_CreateLiveSong to pass in a context
**              Added GM_GetSongContext & GM_SetSongContext
**  3/5/99      Added threadContext to GM_LoadSong & GM_FreeSong
**  5/12/99     Fixed a problem with RMF files and GM_LoadSong in which it would not
**              decode and use the passed in Midi data.
**  5/16/99     Moved GM_BeginSong & GM_PrerollSong from GenSeq.c
**  6/15/99     Moved GM_EndSong from GenSeq.c. Split the control of GM_EndSong into
**              two functions.
**              Added GM_EndSongButKeepActive
**  6/17/99     Removed access to global MusicGlobals from PV_EndSongWithControl. Changed
**              GM_EndSongButKeepActive to pause the song prior to ending.
**  8/4/99      MOE: Changed PV_CreateSongFromMidi() to return error code
**              so that proper error was returned when no MIDI resource found
**  10/30/99    Removed GM_GetSongInstrumentChanges, which is not used.
**  11/3/99     MSD: Fixed logic error in finding empty slot in GM_PrerollSong
**  11/4/99     Added GM_KillSongEventsFromQueue
**              Fixed bug in GM_FreeSong by calling GM_KillSongEventsFromQueue
**              that clear events in the future when a song is deleted.
**  11/9/99     Added GM_SetSongPriority & GM_GetSongPriority
**  11/10/99    Added ifdefs to handle the case REVERB_USED == REVERB_DISABLED
**  1/31/00     Added GM_SetDisposeSongDataWhenDoneFlag(), GM_GetDisposeSongDataWhenDoneFlag()
**              Added GM_GetSongVoices(), GM_ChangeSongVoices()
**  2/4/2000    Changed copyright. We're Y2K compliant!
**  2/5/2000    Removed unused variable in GM_ChangeSongVoices
**  2/26/2000   Added GM_SetSongMixer & GM_GetSongMixer
**              Added GM_Mixer parameter to GM_LoadSong
**  3/3/2000    Added GM_SetupSongRemaps. Moved GM_RemapInstrument from GenPatch.c
**              Modifed PV_CreateSongFromMidi & GM_CreateLiveSong & GM_LoadSongInstrument
**              to support the new function GM_SetupSongRemaps.
**              Changed GM_IsSongInstrumentLoaded & GM_UnloadSongInstrument & GM_LoadSongInstrument
**              to deal with aliases of instruments when working with passed in instruments.
**  3/7/2000    Added GM_IsSongInstrumentRemapped
**  3/10/2000   Fixed memory leak in GM_FreeSong. Thanks Andrew. In the rare case of an instrument
**              failing to unload, the midi data pointer is lost.
**  3/14/2000   Fixed GM_RemapInstrument to actaully map the correct direction! Fixed
**              GM_LoadSongInstrument to map the correct direction!
**  4/3/2000    Changed GM_GetSongInstrumentReamp to GM_GetSongInstrumentRemap
**  4/19/2000   Changed GM_LoadSongInstrument to always force a recheck of the
**              alias link for the current patch bank.
**  4/25/2000   msd: added GM_GetProgramBank() to support MiniBAE API lego.
**  2000.05.16 AER  Completed modifications for new sample cache
**  2000.05.25 sh   Use GM_SetSongLoopFlag/GM_GetSongLoopFlag instead of touching
**                  the structure GM_Song directly
**  6/19/2000   jsc: in GM_SetSongTickPosition, if song was paused, then don't resume song
**  2000.07.14 AER  Modified access to XBankTokens for new system
**  8/27/2000   sh  Changed GM_SetSongTickPosition & GM_SetSongMicrosecondPosition to return
**                  a error code correctly.
**  12/7/2000   sh  Changed GM_GetSongTickLength & GM_GetSongMicrosecondLength to return
**                  an overflow error.
**  1/25/2001   sh  Added GM_IsSongPrerolled.
**  2/14/2001   se  Found pause/resume bug in GM_SetSongTickPosition/GM_SetSongMicrosecondPosition
**  2/16/2001   se  Modified GM_SetSongMicrosecondPosition to kill current notes rather than
**                  allow them to release.
**  3/8/2001    se  Modified GM_GetSongTickLength to stop calculation
**                  when the song being operated on has a duration of over one hour.
**  3/29/2001   sh  Removed extra line in GM_GetSongTickLength that copied over the
**                  temporary position of the song which resulted in a bogus position
**                  being returned for the song in question.
**  7/24/2001       Added PV_FindEmptySongSlot, and now only place preloaded song
**                  into mixer when GM_BeginSong is called.
*/
/*****************************************************************************/

#include "X_API.h"
#include "X_Formats.h"
#include "GenSnd.h"
#include "GenPriv.h"
#include "X_Assert.h"
#include "GenBankBalance.h"
#include <math.h>
#if USE_NATIVE_DLS == TRUE
#include "GenDLS_MobileBAE.h"
#endif
#include <stdint.h>
#if USE_SF2_SUPPORT == TRUE
#if _USING_FLUIDLITE == TRUE
#include "GenSF2_FluidLite.h" // FluidSynth integration
#endif
#endif

// Functions

static GM_Song *PV_CreateSongFromMidi(XLongResourceID theID,
                                      XPTR useThisMidiData, int32_t midiSize,
                                      OPErr *pErr)
{
    XPTR theMidiData;
    GM_Song *theSong;
    OPErr err;

    err = NO_ERR;
    theSong = NULL;
    if (useThisMidiData)
    {
        theMidiData = useThisMidiData;
    }
    else
    {
        midiSize = 0;
        theMidiData = XGetMidiData(theID, &midiSize, NULL);
        if (theMidiData == NULL)
        {
            err = BAD_FILE;
            BAE_STDERR("DEBUG: PV_CreateSongFromMidi: XGetMidiData failed id=%d\n", (int)theID);
        }
    }
    if (theMidiData)
    {
        theSong = (GM_Song *)XNewPtr((int32_t)sizeof(GM_Song));
        if (theSong)
        {
            // Initialize the structure to zero to avoid garbage values
            XSetMemory(theSong, sizeof(GM_Song), 0);

            theSong->sequenceData = theMidiData;
            theSong->sequenceDataSize = midiSize;
            theSong->seqType = SEQ_MIDI;
            theSong->disposeSongDataWhenDone = (useThisMidiData == NULL) ? TRUE : FALSE;
            // Fill in remap first
            GM_SetupSongRemaps(theSong, FALSE);
        }
        else
        {
            err = MEMORY_ERR;
        }
    }
    if (pErr)
    {
        *pErr = err;
    }
    return theSong;
}

static void PV_SetTempo(GM_Song *pSong, int32_t masterTempo)
{
    if (pSong)
    {
        if (masterTempo == 0L)
        {
            masterTempo = 16667;
        }
        masterTempo = (100L * masterTempo) / 16667;
        if (masterTempo < 25)
            masterTempo = 25;
        if (masterTempo > 300)
            masterTempo = 300;
        GM_SetMasterSongTempo(pSong, (masterTempo << 16L) / 100L);
    }
}

#if USE_FULL_RMF_SUPPORT == TRUE
static uint32_t PV_RmfSkipStringResource(const char *pUnit, bool encrypted)
{
    if (encrypted)
    {
        return (uint32_t)XEncryptedStrLen(pUnit) + 1u;
    }
    return (uint32_t)XStrLen(pUnit) + 1u;
}

static void PV_ApplyRmfInstrumentRemaps(SongResource_RMF *songRMF, GM_Song *theSong)
{
    char *pUnit;
    int16_t count;
    int16_t resourceCount;
    int16_t subCount;
    int16_t wordIndex;
    int16_t number;
    SongResourceType type;
    uint32_t length;

    if (!songRMF || !theSong)
    {
        return;
    }
    resourceCount = (int16_t)XGetShort(&songRMF->resourceCount);
    pUnit = (char *)&songRMF->resourceData;
    for (count = 0; count < resourceCount; count++)
    {
        type = (SongResourceType)XGetLong(pUnit);
        pUnit += sizeof(SongResourceType);
        switch (type)
        {
        default:
            return;
        case R_TITLE:
        case R_PERFORMED_BY:
        case R_COMPOSER:
        case R_COPYRIGHT_DATE:
        case R_COPYRIGHT_LINE:
        case R_PUBLISHER_CONTACT:
        case R_LICENSED_TO_URL:
        case R_USE_OF_LICENSE:
        case R_LICENSE_TERM:
        case R_EXPIRATION_DATE:
        case R_COMPOSER_NOTES:
        case R_INDEX_NUMBER:
        case R_GENRE:
        case R_SUB_GENRE:
        case R_TEMPO:
        case R_ORIGINAL_SOURCE:
        case R_MANUFACTURER:
        case R_MISC1:
        case R_MISC2:
        case R_MISC3:
        case R_MISC4:
        case R_MISC5:
        case R_MISC6:
        case R_MISC7:
        case R_MISC8:
            pUnit += (int32_t)PV_RmfSkipStringResource(pUnit, songRMF->locked ? TRUE : FALSE);
            break;
        case R_VELOCITY_CURVE:
            pUnit += 256;
            break;
        case R_INSTRUMENT_REMAP:
            subCount = (int16_t)XGetShort(pUnit);
            pUnit += 2;
            /* First word is remaining word count, then (midi program, INST ID) pairs. */
            for (wordIndex = 0; wordIndex + 1 < subCount; wordIndex += 2)
            {
                number = XGetShort(pUnit + (wordIndex * (int16_t)sizeof(int16_t)));
                number = (int16_t)(number & ((MAX_INSTRUMENTS * MAX_BANKS) - 1));
                theSong->remapArray[number] =
                    XGetShort(pUnit + ((wordIndex + 1) * (int16_t)sizeof(int16_t)));
            }
            length = sizeof(int16_t) * (uint32_t)subCount;
            pUnit += (int32_t)length;
            break;
        }
    }
}
#endif

void GM_MergeExternalSong(void *theExternalSong, XShortResourceID theSongID, GM_Song *theSong)
{
    int16_t maps;
    int16_t count;
    int16_t number;
    SongResource_SMS *songSMS;
    SongResource_RMF *songRMF;
    Remap *pMap;

    if (theExternalSong && theSong)
    {
        switch (((SongResource_SMS *)theExternalSong)->songType)
        {
        case SONG_TYPE_SMS:
            songSMS = (SongResource_SMS *)theExternalSong;
            theSong->songID = theSongID;
            theSong->songPitchShift = songSMS->songPitchShift;
            theSong->allowProgramChanges = (songSMS->flags1 & XBF_enableMIDIProgram) ? TRUE : FALSE;
            theSong->defaultPercusionProgram = songSMS->defaultPercusionProgram;
#if REVERB_USED != REVERB_DISABLED
            theSong->defaultReverbType = songSMS->reverbType;
#endif
            theSong->maxSongVoices = songSMS->maxNotes;
            theSong->mixLevel = XGetShort(&songSMS->mixLevel);
            theSong->maxEffectVoices = songSMS->maxEffects;
            theSong->ignoreBadInstruments = (songSMS->flags2 & XBF_ignoreBadPatches) ? TRUE : FALSE;
            maps = XGetShort(&songSMS->remapCount);
            PV_SetTempo(theSong, XGetShort(&songSMS->songTempo));
            theSong->songVolume = XGetSongVolume((SongResource *)theExternalSong);
#if USE_SF2_SUPPORT == TRUE
            theSong->songFlags = 0;
            theSong->RMFInstrumentIDs[0] = 0;
#endif
            theSong->engineConfigFlags = 0;

            // Load instruments
            if (maps)
            {
                pMap = (Remap *)&songSMS->remaps;
                for (count = 0; count < maps; count++)
                {
                    number = XGetShort(&pMap[count].instrumentNumber) & ((MAX_INSTRUMENTS * MAX_BANKS) - 1);
                    theSong->remapArray[number] = XGetShort(&pMap[count].ResourceINSTID);
                }
            }
            break;

        case SONG_TYPE_RMF:
            songRMF = (SongResource_RMF *)theExternalSong;
            theSong->songID = theSongID;
            theSong->songPitchShift = songRMF->songPitchShift;
            theSong->allowProgramChanges = TRUE;   // aloways allow program changes
            theSong->defaultPercusionProgram = -1; // GM percussion only
#if REVERB_USED != REVERB_DISABLED
            theSong->defaultReverbType = songRMF->reverbType;
#endif
            theSong->maxSongVoices = XGetShort(&songRMF->maxNotes);
            theSong->mixLevel = XGetShort(&songRMF->mixLevel);
            theSong->maxEffectVoices = XGetShort(&songRMF->maxEffects);
            theSong->ignoreBadInstruments = TRUE;
            PV_SetTempo(theSong, XGetShort(&songRMF->songTempo));
            theSong->songVolume = XGetSongVolume((SongResource *)theExternalSong);
#if USE_SF2_SUPPORT == TRUE
            theSong->songFlags = 0;
            theSong->RMFInstrumentIDs[0] = 0;
#endif
            // Read per-song engine config flags from unused[0]
            // Only trust flags if this is a ZMF container; old RMF files may have garbage
            theSong->engineConfigFlags = (uint32_t)XGetLong(&songRMF->unused[SONG_CONFIG_UNUSED_INDEX]);
            if (!(theSong->engineConfigFlags & SONG_CONFIG_CONTAINER_IS_ZMF))
            {
                // Not a ZMF container, discard all flags (they're likely garbage)
                theSong->engineConfigFlags = 0;
            }
            else
            {
                // ZMF file, clamp to valid bits to filter out any remaining garbage
                theSong->engineConfigFlags &= SONG_CONFIG_VALID_BITS_MASK;
            }
#if USE_FULL_RMF_SUPPORT == TRUE
            PV_ApplyRmfInstrumentRemaps(songRMF, theSong);
#endif
            break;
        }
    }
}

static void PV_ClearSongInstruments(GM_Song *pSong)
{
    int32_t count;

    if (pSong)
    {
        for (count = 0; count < (MAX_INSTRUMENTS * MAX_BANKS); count++)
        {
            pSong->instrumentData[count] = NULL;
        }
    }
}

// return valid context for song that was passed in when calling GM_LoadSong or GM_CreateLiveSong
void *GM_GetSongContext(GM_Song *pSong)
{
    void *context;

    context = NULL;
    if (pSong)
    {
        context = pSong->context;
    }
    return context;
}

// set valid context for song
void GM_SetSongContext(GM_Song *pSong, void *context)
{
    if (pSong)
    {
        pSong->context = context;
    }
}

// return associated Mixer from Song
GM_Mixer *GM_GetSongMixer(GM_Song *pSong)
{
    if (pSong)
    {
        return pSong->pMixer;
    }
    return NULL;
}

// Associate a Mixer to a Song
OPErr GM_SetSongMixer(GM_Song *pSong, GM_Mixer *pMixer)
{
    OPErr err;

    err = NO_ERR;
    if (pSong && pMixer)
    {
        pSong->pMixer = pMixer;
    }
    else
    {
        err = NOT_SETUP;
    }
    return err;
}

#if 0
// This will walk through all active songs and return a max number of voices used for midi
static int16_t PV_GetMaxVoicesPlayingFromAllSongs(void)
{
    register GM_Mixer       *pMixer;
    register GM_Song        *pSong;
    register int16_t      count, max;

    max = 0;
    pMixer = GM_GetCurrentMixer();
    if (pMixer)
    {
        for (count = 0; count < MAX_SONGS; count++)
        {
            pSong = pMixer->pSongsToPlay[count];
            if (pSong)
            {
                max += pSong->maxSongVoices;
            }
        }
    }
    return max;
}
#endif

#if 1
// This will walk through all active songs and return a max mix level used for midi
static int16_t PV_GetMixLevelPlayingFromAllSongs(void)
{
    register GM_Mixer *pMixer;
    register GM_Song *pSong;
    register int16_t count, mix;

    mix = 0;
    pMixer = MusicGlobals;
    if (pMixer)
    {
        for (count = 0; count < MAX_SONGS; count++)
        {
            pSong = pMixer->pSongsToPlay[count];
            if (pSong)
            {
                if (pSong->mixLevel > mix)
                {
                    mix = pSong->mixLevel;
                }
            }
        }
    }
    return mix;
}
#endif

// scan through mixer and find an empty song slot.
// Returns -1, if failure.
static int16_t PV_FindEmptySongSlot(GM_Mixer *pMixer, GM_Song *pSong)
{
    int16_t songSlot = -1;
    int16_t count;

    if (pSong)
    {
        // first find a slot in the song queue
        for (count = 0; count < MAX_SONGS; count++)
        {
            // Reuse slot, if song already playing
            if (pMixer->pSongsToPlay[count] == pSong)
            {
                pMixer->pSongsToPlay[count] = NULL;
                GM_KillSongNotes(pSong);
                songSlot = count;
                break;
            }
        }
        if (songSlot == -1)
        {
            // Song wasn't already playing, so try to find empty slot.
            for (count = 0; count < MAX_SONGS; count++)
            {
                if (pMixer->pSongsToPlay[count] == NULL)
                {
                    songSlot = count;
                    break;
                }
            }
        }
    }
    return songSlot;
}

// preroll song but don't start
OPErr GM_PrerollSong(GM_Song *pSong, GM_SongCallbackProcPtr theCallbackProc,
                     bool useEmbeddedMixerSettings, bool autoLevel)
{
    OPErr theErr;
    int16_t count;
    int16_t sMaxSong, sMixLevel, sMaxEffect;

    theErr = NO_ERR;
    if (pSong)
    {
#if USE_NATIVE_DLS == TRUE
        if (pSong->pMixer && pSong->pMixer->isDLS)
        {
            /* External DLS bank available: mark MIDI/XMF as DLS songs, but keep
               RMF/ZMF on INST/ADSR. Hybrid note-on still falls back to DLS for
               patches that are not embedded in the RMF. */
            if ((pSong->songFlags & SONG_FLAG_IS_RMF) == 0)
            {
                pSong->isDLSSong = TRUE;
            }
            // Prevent stale channel/program bindings from prior songs causing silent note-ons.
            GM_DLS_ResetForSong(pSong);
        }
#endif
        // preroll song prior to start
        pSong->AnalyzeMode = SCAN_NORMAL;
        pSong->songEndCallbackPtr = theCallbackProc;

        if (pSong->seqType == SEQ_MIDI)
        {
            theErr = PV_ConfigureMusic(pSong);
        }
        else
        {
            theErr = BAD_FILE;
        }
        if (theErr == NO_ERR)
        {
            pSong->SomeTrackIsAlive = TRUE;
            pSong->songFinished = FALSE;

            // first time looping, and set mute tracks to off
            pSong->songLoopCount = 0;
            pSong->songMaxLoopCount = 0;
            for (count = 0; count < MAX_TRACKS; count++)
            {
                XClearBit(&pSong->trackMuted, count);
                XClearBit(&pSong->soloTrackMuted, count);
                pSong->pTrackPositionSave[count] = NULL;
                pSong->trackTicksSave[count] = 0;
                pSong->trackStatusSave[count] = TRACK_OFF;
            }
            pSong->loopbackSaved = FALSE;
            pSong->loopbackCount = -1;
            for (count = 0; count < MAX_CHANNELS; count++)
            {
                XClearBit(&pSong->channelMuted, count);
                XClearBit(&pSong->soloChannelMuted, count);
                XSetBit(&pSong->allowPitchShift, count);
            }
            if (pSong->channelBankMode[PERCUSSION_CHANNEL] != USE_NORM_BANK)
            {
                XClearBit(&pSong->allowPitchShift, PERCUSSION_CHANNEL); // don't allow pitch changes on percussion
            }

            // Keep the velocity curve chosen before start (BAESong_SetVelocityCurve,
            // BAE_SetDefaultVelocityCurve at load). Resetting it here made every
            // song start on curve 0 regardless of the API, playbae -vc or the apps.

#if REVERB_USED != REVERB_DISABLED
            // reconfigure reverb settings if desired
            // Set reverb type now.
            GM_SetReverbType(pSong->defaultReverbType);
#endif
#if 0
            autoLevel = TRUE;
#endif
            // rebalance them mixer based upon load
            if (autoLevel)
            {

                pSong->mixLevel = (pSong->averageVoiceUsage * 90) / 100; // reduce by 10%
                if (pSong->mixLevel < 1)
                {
                    pSong->mixLevel = 1;
                }
                pSong->maxSongVoices = MAX_VOICES - pSong->maxEffectVoices;

                // debug_message("audio::max voices from all songs %d\n", PV_GetMaxVoicesPlayingFromAllSongs());
                // debug_message("audio::mix level from all songs %d\n", PV_GetMixLevelPlayingFromAllSongs());

                GM_GetSystemVoices(&sMaxSong, &sMixLevel, &sMaxEffect);
                // if the mix level for all currently playing songs is not 0, then reconfigure mix level only up
                // other go up and down
                count = PV_GetMixLevelPlayingFromAllSongs();
                if (count)
                {
                    // if there's a song playing don't lower the mix level
                    if (pSong->mixLevel < count)
                    {
                        pSong->mixLevel = count;
                    }
                }

                // debug_message("audio::Song mix %d midi %d pcm %d\n", pSong->mixLevel, pSong->maxSongVoices, pSong->maxEffectVoices);
                // debug_message("audio::Mixer mix %d midi %d pcm %d\n", sMixLevel, sMaxSong, sMaxEffect);
                useEmbeddedMixerSettings = TRUE; // force a reconfigure
            }
            // reconfigure global mixer settings if desired
            if (useEmbeddedMixerSettings)
            {
                theErr = GM_ChangeSystemVoices(pSong->maxSongVoices,
                                               pSong->mixLevel,
                                               pSong->maxEffectVoices);
                GM_GetSystemVoices(&sMaxSong, &sMixLevel, &sMaxEffect);
                // debug_message("audio::mixer settings: mix %d midi %d pcm %d\n", sMixLevel, sMaxSong, sMaxEffect);
            }
            // allocate song song, but don't play sequencer
            pSong->songPaused = TRUE;
            pSong->songPrerolled = TRUE;
            
#if USE_SF2_SUPPORT == TRUE
            // Check if all instruments are RMF-embedded and disable SF2 if so
            // This prevents double-playback issue on initial load
            GM_SF2_CheckAndDisableSF2ForRMFEmbedded(pSong);
#endif
        }
    }
    return theErr;
}

// return preroll status
bool GM_IsSongPrerolled(GM_Song *pSong)
{
    bool roll;

    roll = FALSE;
    if (pSong)
    {
        roll = pSong->songPrerolled;
    }
    return roll;
}

// Set up the system to start playing a song
OPErr GM_BeginSong(GM_Song *pSong, GM_SongCallbackProcPtr theCallbackProc,
                   bool useEmbeddedMixerSettings, bool autoLevel)
{
    OPErr theErr;
    GM_Mixer *pMixer;
    GM_Mixer *savedMixer = NULL;
    int16_t songSlot;

    theErr = NO_ERR;
    if (pSong)
    {
        pMixer = pSong->pMixer ? pSong->pMixer : GM_GetCurrentMixer();
        if (!pMixer)
            return NOT_SETUP;
        savedMixer = GM_SetCurrentMixer(pMixer);

        if (GM_IsSongPrerolled(pSong) == FALSE)
        {
            theErr = GM_PrerollSong(pSong, theCallbackProc, useEmbeddedMixerSettings, autoLevel);
        }
        if (theErr == NO_ERR)
        {
            // we clear the preroll flag here, because if GM_BeginSong is called again while the song
            // is playing, we don't want to song in a unsafe state, this forces a reset.

            // also find an empty slot in the mixer for playback.
            songSlot = PV_FindEmptySongSlot(pMixer, pSong);
            if (songSlot != -1)
            {
                pMixer->pSongsToPlay[songSlot] = pSong;
                pSong->songPrerolled = FALSE;
                pSong->songPaused = FALSE; // start sequencer
            }
            else
            {
                theErr = TOO_MANY_SONGS_PLAYING;
            }
        }
        GM_SetCurrentMixer(savedMixer);
    }
    else
    {
        theErr = PARAM_ERR;
    }
    return theErr;
}

int16_t GM_GetSongRouteBus(GM_Song *pSong)
{
    int16_t routeBus;

    routeBus = 0;
    if (pSong)
    {
        routeBus = pSong->routeBus;
    }
    return routeBus;
}

OPErr GM_SetSongRouteBus(GM_Song *pSong, int16_t routeBus)
{
    OPErr theErr = NO_ERR;

    if (pSong)
    {
        pSong->routeBus = routeBus;
    }
    else
    {
        theErr = PARAM_ERR;
    }
    return theErr;
}

// Get song priority level. Higher values will allow for better note stealing.
int16_t GM_GetSongPriority(GM_Song *pSong)
{
    int16_t priority;

    priority = 0;
    if (pSong)
    {
        priority = pSong->songPriority;
    }
    return priority;
}

// Set song priority level. Higher values will allow for better note stealing.
OPErr GM_SetSongPriority(GM_Song *pSong, int16_t songPriority)
{
    OPErr theErr = NO_ERR;

    if (pSong)
    {
        pSong->songPriority = songPriority;
    }
    else
    {
        theErr = PARAM_ERR;
    }
    return theErr;
}

// REMAP

OPErr GM_SetupSongRemaps(GM_Song *pSong, bool checkForAliases)
{
    int16_t count;
    XAliasLinkResource *pAlias;
    XLongResourceID newInstrumentID;
    XLongResourceID savedRemaps[MAX_INSTRUMENTS * MAX_BANKS];
    OPErr err;

    err = NO_ERR;
    if (pSong)
    {
        pAlias = NULL;
        pSong->checkedForAliases = checkForAliases;
        if (checkForAliases)
        {
            /* Keep SONG remaps (SMS header / RMF RMAP) across alias refresh. */
            XBlockMove(pSong->remapArray, savedRemaps, sizeof(savedRemaps));
            pAlias = XGetAliasLink(); // get current aliases
        }
        for (count = 0; count < MAX_INSTRUMENTS * MAX_BANKS; count++)
        {
            pSong->remapArray[count] = (XLongResourceID)count; // no remap
            if (checkForAliases)
            {
                if (XLookupAlias(pAlias, (XLongResourceID)count, &newInstrumentID) == 0)
                {
                    pSong->remapArray[count] = newInstrumentID;
                }
                if (savedRemaps[count] != (XLongResourceID)count)
                {
                    pSong->remapArray[count] = savedRemaps[count];
                }
            }
        }
        if (checkForAliases)
        {
            XDisposePtr((XPTR)pAlias);
        }
    }
    else
    {
        err = NOT_SETUP;
    }
    return err;
}

OPErr GM_GetSongInstrumentRemap(GM_Song *pSong, XLongResourceID fromInstrument, XLongResourceID *pToInstrument)
{
    OPErr err;

    err = NO_ERR;
    if (pSong && pToInstrument)
    {
        *pToInstrument = pSong->remapArray[fromInstrument];
    }
    else
    {
        err = NOT_SETUP;
    }
    return err;
}

// This will remap the 'from' instrument into the 'to' instrument.
OPErr GM_RemapInstrument(GM_Song *pSong, XLongResourceID from, XLongResourceID to)
{
    OPErr theErr;

    theErr = BAD_INSTRUMENT;
    if (pSong && (from >= 0) && (from < MAX_INSTRUMENTS * MAX_BANKS))
    {
        if ((to >= 0) && (to < MAX_INSTRUMENTS * MAX_BANKS))
        {
            if (to != from)
            {
                if (pSong->instrumentData[from])
                {
                    pSong->remapArray[from] = to;
                    theErr = NO_ERR;
                }
            }
            else
            {
                theErr = NO_ERR;
            }
        }
    }
    return theErr;
}

// GM_CreateLiveSong is used to create an active midi object that can be
// controled directly. ie via midi commands without loading midi data
//
//  context             context of song creation. C++ 'this' pointer, thread, etc.
//                      Its just stored in the GM_Song->context variable
//  songID              unique ID for song structure
GM_Song *GM_CreateLiveSong(void *context, XShortResourceID songID)
{
    GM_Song *pSong;
    int16_t count;

    pSong = NULL;

    pSong = (GM_Song *)XNewPtr((int32_t)sizeof(GM_Song));
    if (pSong)
    {
        // Initialize the structure to zero to avoid garbage values
        XSetMemory(pSong, sizeof(GM_Song), 0);

        pSong->context = context;
        // Fill in remap first
        GM_SetupSongRemaps(pSong, FALSE);

        for (count = 0; count < MAX_CHANNELS; count++)
        {
            pSong->firstChannelBank[count] = 0;
            pSong->firstChannelProgram[count] = -1;
        }
        PV_ConfigureInstruments(pSong);

#if REVERB_USED != REVERB_DISABLED
        pSong->defaultReverbType = GM_GetReverbType();
#endif
        pSong->songID = songID;
        pSong->songPitchShift = 0;
        pSong->allowProgramChanges = TRUE;
        pSong->defaultPercusionProgram = -1;

        if (MusicGlobals)
        {
            pSong->pMixer = MusicGlobals;
            pSong->maxSongVoices = MusicGlobals->MaxNotes;
            pSong->mixLevel = MusicGlobals->mixLevel;
            pSong->maxEffectVoices = MusicGlobals->MaxEffects;
        }

        PV_SetTempo(pSong, 0L);
        pSong->songVolume = MAX_SONG_VOLUME;
    }
    return pSong;
}

// Reset lyric processing state for the song (clears seen flags)
void GM_ResetSongLyricState(GM_Song *pSong)
{
    if (pSong)
    {
        pSong->seenTrueLyric = FALSE;
        pSong->seenGenericTextLyric = FALSE;
        pSong->seenLyricMeta = FALSE;
        pSong->lastLyric[0] = '\0';
        pSong->lastLyricTimestamp = 0;
    }
}

OPErr GM_StartLiveSong(GM_Song *pSong, bool loadPatches, XBankToken bankToken)
{
    OPErr theErr;
    int16_t songSlot, count;
    GM_Mixer *pMixer;
    GM_Mixer *savedMixer;

    theErr = NO_ERR;
    if (pSong)
    {
        pMixer = pSong->pMixer ? pSong->pMixer : MusicGlobals;
        if (!pMixer)
            return NOT_SETUP;
        pSong->pMixer = pMixer;
        savedMixer = GM_SetCurrentMixer(pMixer);

        // first find a slot in the song queue
        songSlot = -1;
        for (count = 0; count < MAX_SONGS; count++)
        {
            if (pMixer->pSongsToPlay[count] == NULL)
            {
                songSlot = count;
                break;
            }
        }
        if (songSlot != -1)
        {
            if (loadPatches)
            {
                for (count = 0; count < (MAX_INSTRUMENTS * MAX_BANKS); count++)
                {
                    GM_LoadSongInstrument(pSong, (XLongResourceID)count, bankToken);
                }
            }

            pSong->SomeTrackIsAlive = FALSE;
            pSong->songFinished = FALSE;
            pSong->AnalyzeMode = SCAN_NORMAL;

            theErr = GM_ChangeSystemVoices(pSong->maxSongVoices,
                                           pSong->mixLevel,
                                           pSong->maxEffectVoices);

#if REVERB_USED != REVERB_DISABLED
            // Set reverb type now.
            GM_SetReverbType(pSong->defaultReverbType);
#endif

            // first time looping, and set mute tracks to off
            pSong->songLoopCount = 0;
            pSong->songMaxLoopCount = 0;
            for (count = 0; count < MAX_TRACKS; count++)
            {
                XClearBit(&pSong->trackMuted, count);
                XSetBit(&pSong->soloTrackMuted, count);
                pSong->pTrackPositionSave[count] = NULL;
                pSong->trackTicksSave[count] = 0;
            }
            pSong->loopbackSaved = FALSE;
            pSong->loopbackCount = -1;
            for (count = 0; count < MAX_CHANNELS; count++)
            {
                XClearBit(&pSong->channelMuted, count);
                XClearBit(&pSong->soloChannelMuted, count);
                XSetBit(&pSong->allowPitchShift, count);
            }
            if (pSong->channelBankMode[PERCUSSION_CHANNEL] != USE_NORM_BANK) {
                XClearBit(&pSong->allowPitchShift, PERCUSSION_CHANNEL); // don't allow pitch changes on percussion
            }

            // Keep the velocity curve chosen before start (BAESong_SetVelocityCurve,
            // BAE_SetDefaultVelocityCurve at load). Resetting it here made every
            // song start on curve 0 regardless of the API, playbae -vc or the apps.

            // Start song playing now.
            pMixer->pSongsToPlay[songSlot] = pSong;
        }
        GM_SetCurrentMixer(savedMixer);
    }
    return theErr;
}

// This follows reamped or aliased instruments.
OPErr GM_LoadSongInstrument(GM_Song *pSong,
                            XLongResourceID instrument,
                            XBankToken bankToken)
{
    XLongResourceID realInstrument;
    OPErr theErr;

    theErr = BAD_INSTRUMENT;
    if (pSong && (instrument >= 0) && (instrument < (MAX_INSTRUMENTS * MAX_BANKS)))
    {
        GM_SetupSongRemaps(pSong, TRUE); // always reset alias links

        if (GM_GetSongInstrumentRemap(pSong, instrument, &realInstrument) != NO_ERR)
        {
            realInstrument = instrument; // failed, no alias, so try to load instrument passed
        }
        theErr = GM_LoadInstrument(pSong, realInstrument, bankToken);
        GM_RemapInstrument(pSong, instrument, realInstrument);
    }
    return theErr;
}

// This follows reamped or aliased instruments.
OPErr GM_UnloadSongInstrument(GM_Song *pSong, XLongResourceID instrument)
{
    XLongResourceID realInstrument;
    OPErr theErr;

    theErr = BAD_INSTRUMENT;
    if (pSong && (instrument >= 0) && (instrument < (MAX_INSTRUMENTS * MAX_BANKS)))
    {
        if (GM_GetSongInstrumentRemap(pSong, instrument, &realInstrument) != NO_ERR)
        {
            realInstrument = instrument; // failed, no alias, so try to load instrument passed
        }
        theErr = GM_UnloadInstrument(pSong, realInstrument);
    }
    return theErr;
}

// Will check to see if an instrument has got a remap.
// Returns TRUE if instrument is remapped, otherwise FALSE
bool GM_IsSongInstrumentRemapped(GM_Song *pSong, XLongResourceID instrument)
{
    XLongResourceID realInstrument;
    bool aliased;

    aliased = FALSE;
    if (pSong && (instrument >= 0) && (instrument < (MAX_INSTRUMENTS * MAX_BANKS)))
    {
        if (GM_GetSongInstrumentRemap(pSong, instrument, &realInstrument) != NO_ERR)
        {
            realInstrument = instrument; // failed, no alias, so try to load instrument passed
        }
        if (realInstrument != instrument)
        {
            aliased = TRUE;
        }
    }
    return aliased;
}

// Returns TRUE if instrument is loaded, otherwise FALSE
// This follows reamped or aliased instruments.
bool GM_IsSongInstrumentLoaded(GM_Song *pSong, XLongResourceID instrument)
{
    XLongResourceID realInstrument;
    bool loaded;

    loaded = FALSE;
    if (pSong && (instrument >= 0) && (instrument < (MAX_INSTRUMENTS * MAX_BANKS)))
    {
        if (GM_GetSongInstrumentRemap(pSong, instrument, &realInstrument) != NO_ERR)
        {
            realInstrument = instrument; // failed, no alias, so try to load instrument passed
        }
        if (pSong->instrumentData[realInstrument])
        {
            loaded = TRUE;
        }
    }
    return loaded;
}

// Load the SongID from an external SONG resource and or a extneral midi resource.
//
//  pMixer              When the song is created this is the mixer it will be associated
//                      with during playback. Can be NULL, if you're not loading
//                      instrments ie. loadInstruments = FALSE
//
//  threadContext       context of thread. passed all the way down to callbacks
//  context             context of song creation. C++ 'this' pointer, etc. this is a user variable
//                      Its just stored in the GM_Song->context variable
//  songID              will be the ID used during playback
//  theExternalSong     standard SONG resource structure

//  theExternalMidiData if not NULL, then will use this midi data rather than what is found in external SONG resource
//  midiSize            size of midi data if theExternalMidiData is not NULL
//                      theExternalMidiData and midiSize is not used if the songType is RMF

//  pInstrumentArray    array, if not NULL will be filled with the instruments that need to be loaded.
//  loadInstruments     if not zero, then instruments and samples will be loaded
//  pErr                pointer to an OPErr

GM_Song *GM_LoadSong(struct GM_Mixer *pMixer,
                     void *threadContext,
                     void *context,
                     XShortResourceID songID,
                     void *theExternalSong,
                     void *theExternalMidiData,
                     int32_t midiSize,
                     XShortResourceID *pInstrumentArray,
                     bool loadInstruments,
                     bool ignoreBadInstruments,
                     XBankToken bankToken,
                     OPErr *pErr)
{
    GM_Song *pSong;
    XLongResourceID songObjectID;
    OPErr err;

    err = NO_ERR;
    pSong = NULL;
    if (theExternalSong)
    {
        songObjectID = (XLongResourceID)XGetSongResourceObjectID(theExternalSong);
        switch (XGetSongResourceObjectType(theExternalSong))
        {
        case SONG_TYPE_SMS:
            pSong = PV_CreateSongFromMidi(songObjectID, theExternalMidiData, midiSize, &err);
            break;
        case SONG_TYPE_RMF:
            if (theExternalMidiData == NULL)
            {
                pSong = PV_CreateSongFromMidi(songObjectID, NULL, 0, &err);
            }
            else
            {
                pSong = PV_CreateSongFromMidi(songObjectID, theExternalMidiData, midiSize, &err);
            }
            break;
        default:
            err = PARAM_ERR;
            break;
        }
    }
    else
    {
        err = PARAM_ERR;
    }

    // load instruments
    if (pSong)
    {
        pSong->context = context;
        GM_SetSongMixer(pSong, pMixer);

        GM_MergeExternalSong(theExternalSong, songID, pSong);
        pSong->ignoreBadInstruments = ignoreBadInstruments;
        if (XGetSongResourceObjectType(theExternalSong) == SONG_TYPE_RMF)
        {
            pSong->songFlags |= SONG_FLAG_IS_RMF;
        }
        err = GM_LoadSongInstruments(pSong,
                                     pInstrumentArray,
                                     bankToken,
                                     loadInstruments);
        if (err)
        {
            GM_FreeSong(threadContext, pSong); // we ignore the error codes, because it should be ok to dispose
                                               // since this song was never engaged
            pSong = NULL;
        }
        else
        {
            // song length not calculated
            pSong->songMidiTickLength = (UFLOAT)0;
            pSong->songMicrosecondLength = (UFLOAT)0;
            err = NO_ERR;
        }
    }
    if (pErr)
    {
        *pErr = err;
    }
    return pSong;
}

// Stop this song playing, or pass NULL with pSong to stop all songs. "removeFromMixer" determines
// if the song is removed from the mixer or not. If TRUE then you can still send midi events to the song
// because the song is still in the mixer for event processing. FALSE removes the song from the mixer.
static void PV_EndSongWithControl(void *threadContext, GM_Song *pSong, bool removeFromMixer)
{
    int32_t count;
    GM_Mixer *pMixer;
    GM_Mixer *savedMixer = NULL;

    pMixer = (pSong && pSong->pMixer) ? pSong->pMixer : MusicGlobals;
    if (pMixer)
    {
        savedMixer = GM_SetCurrentMixer(pMixer);
        if (pSong)
        {
            // For SF2-backed songs ensure the SF2 engine is fully silenced (sustain off, all sound off, all notes off)
            // which also clears engine bookkeeping, otherwise lingering SF2 voices can reappear after stop.
#if USE_SF2_SUPPORT == TRUE
            if (GM_IsSF2Song(pSong))
            {
                GM_SF2_SilenceSong(pSong); // also calls GM_EndSongNotes internally
            }
            else
#endif
            {
                GM_EndSongNotes(pSong); // end just notes associated with this song
            }
            if (removeFromMixer)
            {
                for (count = 0; count < MAX_SONGS; count++)
                {
                    if (pMixer->pSongsToPlay[count] == pSong)
                    {
                        pMixer->pSongsToPlay[count] = NULL;
                        break;
                    }
                }
            }
            for (count = 0; count < MAX_TRACKS; count++)
            {
                pSong->ptrack[count] = NULL;
                pSong->trackon[count] = TRACK_OFF;
            }
            PV_CallSongCallback(threadContext, pSong, TRUE);
        }
        else
        {
            for (count = 0; count < MAX_SONGS; count++)
            {
                if (pMixer->pSongsToPlay[count])
                {
                    PV_EndSongWithControl(threadContext, pMixer->pSongsToPlay[count], removeFromMixer);
                }
            }
        }
        GM_SetCurrentMixer(savedMixer);
    }
}

// Stop this song playing, or if NULL stop all songs playing.
// This removes the Song from the mixer, so you can no longer send
// midi events to the song for processing.
void GM_EndSong(void *threadContext, GM_Song *pSong)
{
    PV_EndSongWithControl(threadContext, pSong, TRUE);
}

// Stop this song playing, or if NULL stop all songs playing.
// This does NOT remove the Song from the mixer, so you can send
// midi events to the song for processing.
void GM_EndSongButKeepActive(void *threadContext, GM_Song *pSong)
{
    GM_PauseSong(pSong, TRUE);
    PV_EndSongWithControl(threadContext, pSong, FALSE);
}

// Walk through the current queue and invalidate all events that are associated to a specific
// GM_Song.
void GM_KillSongEventsFromQueue(GM_Song *pSong)
{
    int16_t count;
    Q_MIDIEvent *pEvent;
    GM_Mixer *pMixer;

    if (pSong == NULL)
    {
        return;
    }

    pMixer = pSong->pMixer ? pSong->pMixer : MusicGlobals;
    if (pMixer == NULL)
    {
        return;
    }

    for (count = 0; count < MAX_QUEUE_EVENTS; count++)
    {
        pEvent = &pMixer->theExternalMidiQueue[count];
        if (pEvent)
        {
            if (pEvent->pSong == pSong)
            {
                pEvent->pSong = NULL;
            }
        }
    }
}

// Given a song pointer, this will attempt to free all memory related to the song: midi
// data, instruments, samples, etc. It can fail and will return STILL_PLAYING if
// midi data is still being accessed, or samples, or instruments.
//
// If you pass NULL, then this function will be called recursively will all songs
// currently playing.
OPErr GM_FreeSong(void *threadContext, GM_Song *pSong)
{
    OPErr err;
    XPTR midiData;

    err = NO_ERR;
    GM_EndSong(threadContext, pSong);
    if (pSong)
    {
        GM_KillSongNotes(pSong); // we must kill the notes because we are about to free
                                 // instrument memory
        if (pSong->processingSlice == FALSE)
        {
            GM_PauseSong(pSong, TRUE);
            // remove any events associated with this song
            GM_KillSongEventsFromQueue(pSong);

            midiData = (XPTR)pSong->sequenceData; // save midi pointer now
            pSong->sequenceData = NULL;           // and disable midi decoder now, just
            pSong->sequenceDataSize = 0;
            // in case the decoder thread comes to life
            // GM_SetCacheSamples(pSong, FALSE);
            err = GM_UnloadSongInstruments(pSong);
            if (err == NO_ERR)
            {
                if (pSong->disposeSongDataWhenDone)
                {
                    XDisposePtr(midiData);
                }
                XDisposePtr((XPTR)pSong->controllerCallback);

                pSong->pMixer = NULL;

#if 0 && USE_CREATION_API == TRUE
                if (pSong->pPatchInfo)
                {
                    PV_FreePatchInfo(pSong);    // must free before killing pSong pointer
                }
#endif
                XDisposePtr((XPTR)pSong);
            }
            else
            {
                // DebugStr("\pGM_FreeSong::GM_UnloadSongInstruments::STILL_PLAYING");

                // we've failed to unload all the instruments. So we need to restore
                // our midi pointer, so this function can be called again without a leak.
                pSong->sequenceData = midiData;
            }
        }
        else
        {
            // DebugStr("\pGM_FreeSong::STILL_PLAYING");
            err = STILL_PLAYING;
        }
    }
    return err;
}

/* Convert sequencer UFLOAT time/tick to uint64 without changing voice math. */
static uint64_t PV_UFloatToUInt64(UFLOAT v)
{
#if USE_FLOAT == FALSE
    return (uint64_t)(uint32_t)v;
#else
    if (v <= 0.0)
        return 0;
    /* Exact integer double covers >100 years of microseconds; clamp extremes. */
    if (v >= 18446744073709549568.0) /* largest uint64 representable as double */
        return UINT64_MAX;
    return (uint64_t)v;
#endif
}

/* Length-scan safety. USE_FLOAT builds track time in double — allow long songs.
 * Non-float UFLOAT is uint32_t, so keep the historic ~1 hour ceiling. */
#if USE_FLOAT == FALSE
#define kSongLengthScanMaxUs ((UFLOAT)3600000000u)
#else
#define kSongLengthScanMaxUs ((UFLOAT)604800000000.0) /* 7 days */
#endif

// Return the length in MIDI ticks of the song passed

//  pSong   GM_Song structure. Data will be cloned for this function.
//  pErr    OPErr error type
uint64_t GM_GetSongTickLength64(GM_Song *pSong, OPErr *pErr)
{
    GM_Song *theSong;
    uint64_t tickLength;
    OPErr err;

    err = NO_ERR;
    tickLength = 0;
    if (!pSong)
    {
        if (pErr)
            *pErr = NULL_OBJECT;
        return 0;
    }
    if (pSong->seqType != SEQ_MIDI)
    {
        if (pErr)
        {
            *pErr = NOT_SETUP;
        }
        return 0;
    }
    if (pSong->songMidiTickLength == (UFLOAT)0)
    {
        theSong = (GM_Song *)XNewPtr(sizeof(GM_Song));
        if (theSong)
        {
            *theSong = *pSong;
            theSong->controllerCallback = NULL; // ignore callbacks
            theSong->songEndCallbackPtr = NULL;
            theSong->songTimeCallbackPtr = NULL;
            theSong->metaEventCallbackPtr = NULL;
            theSong->disposeSongDataWhenDone = FALSE;
            PV_ClearSongInstruments(theSong); // don't free the instruments

            err = PV_ConfigureMusic(theSong);
            if (err == NO_ERR)
            {
                theSong->AnalyzeMode = SCAN_DETERMINE_LENGTH;
                theSong->SomeTrackIsAlive = TRUE;

                GM_SetSongLoopFlag(theSong, FALSE);
                GM_SetSongMetaLoopFlag(theSong, FALSE);
                theSong->songLoopCount = 0;
                theSong->songMaxLoopCount = 0;
                while (theSong->SomeTrackIsAlive)
                {
                    // don't need a thread context here because we don't callback
                    err = PV_ProcessMidiSequencerSlice(NULL, theSong);
                    if (err)
                    {
                        break;
                    }

                    if (theSong->songMicroseconds >= kSongLengthScanMaxUs)
                    {
                        err = OUT_OF_RANGE;
                        break;
                    }
                }
                theSong->AnalyzeMode = SCAN_NORMAL;
                pSong->songMidiTickLength = theSong->CurrentMidiClock;
                pSong->songMicrosecondLength = theSong->songMicroseconds;
                theSong->sequenceData = NULL;
                theSong->songEndCallbackPtr = NULL;
                theSong->disposeSongDataWhenDone = FALSE;

                tickLength = PV_UFloatToUInt64(theSong->CurrentMidiClock);
                if (err)
                {
                    tickLength = 0;
                }
            }
            // don't need a thread context here because we don't callback
            GM_FreeSong(NULL, theSong); // we ignore the error codes, because it should be ok to dispose
                                        // since this song was never engaged
        }
        else
        {
            err = MEMORY_ERR;
        }
    }
    else
    {
        tickLength = PV_UFloatToUInt64(pSong->songMidiTickLength);
    }
    if (pErr)
    {
        *pErr = err;
    }
    return tickLength;
}

uint32_t GM_GetSongTickLength(GM_Song *pSong, OPErr *pErr)
{
    OPErr err = NO_ERR;
    uint64_t tickLength = GM_GetSongTickLength64(pSong, &err);

    if (err == NO_ERR && tickLength > UINT32_MAX)
    {
        tickLength = 0;
        err = OUT_OF_RANGE;
    }
    if (pErr)
        *pErr = err;
    return (uint32_t)tickLength;
}

// meta event callback that collects track names
#if USE_CREATION_API == TRUE
void PV_TrackNameCallback(void *threadContext, GM_Song *pSong, char markerType, void *pMetaText, int32_t metaTextLength, int16_t currentTrack)
{
    unsigned char **tnArray, *str;

    if (markerType == 0x03)
    { // track name
        if (currentTrack != -1)
        {
            str = (unsigned char *)XNewPtr(metaTextLength + 1);
            if (str)
            {
                XBlockMove(pMetaText, str + 1, metaTextLength);
                str[0] = (unsigned char)metaTextLength;
                tnArray = (unsigned char **)pSong->metaEventCallbackReference;
                tnArray[currentTrack] = str;
            }
        }
    }
}
#endif

#if 0
// unsued
// Go through the song and find all the program changes on each track
// theSongResource -> Standard Song Resource
//  pErr        OPErr error type
//  outSong      modified, loaded song
//  trackNames   array of track names extracted
#if USE_CREATION_API == TRUE
OPErr GM_GetSongInstrumentChanges(void *theSongResource, GM_Song **outSong, unsigned char **outTrackNames)
{
    OPErr       err;
    ScanMode    saveScan;
    bool       saveLoop,saveAlive;
    GM_Song     *theSong;
    void        *newMidiData;
    
    err = NO_ERR;
    // don't need a thread context here because we don't callback
    theSong = GM_LoadSong(NULL, NULL, NULL, 0, theSongResource, NULL, 0L, NULL, FALSE, TRUE, &err);
    if (!theSong)
    {
        return err;
    }   
    err = PV_ConfigureMusic(theSong);
    // first pass: count the program changes, so we know how big to make our
    if (err == NO_ERR)
    {
        theSong->pPatchInfo = (PatchInfo *)XNewPtr(sizeof(PatchInfo));
        if (theSong->pPatchInfo)
        {
            saveScan = theSong->AnalyzeMode;
            theSong->AnalyzeMode = SCAN_COUNT_PATCHES;
            saveLoop = GM_GetSongLoopFlag(theSong);
            GM_SetSongLoopFlag(theSong, FALSE);
            GM_SetSongMetaLoopFlag(theSong, FALSE);
            saveAlive = theSong->SomeTrackIsAlive;
            theSong->SomeTrackIsAlive = TRUE;
            while (theSong->SomeTrackIsAlive)
            {
            // don't need a thread context here because we don't callback
                err = PV_ProcessMidiSequencerSlice(NULL, theSong);
                if (err)
                {
                    break;
                }
            }
        }
        else
        {
            err = MEMORY_ERR;
        }
    }
    // now theSong->pPatchInfo->pgmCount points to the number of program
    // changes without immediately preceding bank changes, and rsCount
    // is the number of program changes that were discovered to have been
    // written into the file in running status mode (implement this later)
    // done in two passes because we can't keep changing the midi data
    // pointer, so we have figure out how big it needs to be first and
    // move it in place

    if (err == NO_ERR)
    {
        // expand the ptr enough to accomodate bank messages (4 bytes each)
        newMidiData = XNewPtr(XGetPtrSize(theSong->midiData) + theSong->pPatchInfo->instrCount * 4);
        XBlockMove(theSong->midiData,newMidiData,XGetPtrSize(theSong->midiData));
        XDisposePtr(theSong->midiData);
        theSong->midiData = newMidiData;

        // start over
        err = PV_ConfigureMusic(theSong);

        // second pass: get the program changes, add bank events before each one
        if (err == NO_ERR)
        {
            GM_SetSongMetaEventCallback(theSong, PV_TrackNameCallback, outTrackNames);
            saveScan = theSong->AnalyzeMode;
            theSong->AnalyzeMode = SCAN_FIND_PATCHES;
            saveLoop = GM_GetSongLoopFlag(theSong);
            GM_SetSongLoopFlag(theSong, FALSE);
            GM_SetSongMetaLoopFlag(theSong, FALSE);
            saveAlive = theSong->SomeTrackIsAlive;
            theSong->SomeTrackIsAlive = TRUE;
            while (theSong->SomeTrackIsAlive)
            {
                // don't need a thread context here because we don't callback
                err = PV_ProcessMidiSequencerSlice(NULL, theSong);
                if (err)
                {
                    break;
                }
            }
        }
    }
    // since there was an error, caller won't be using the song
    if (err)
    {
        if (theSong->pPatchInfo)
        {
            XDisposePtr(theSong->pPatchInfo);
        }
        // don't need a thread context here because we don't callback
        GM_FreeSong(NULL, theSong); // we ignore the error codes, because it should be ok to dispose
                                    // since this song was never engaged
    }
    else
    {
        *outSong = theSong;
    }
    return err;
}
#endif // USE_CREATION_API == TRUE
#endif

// Set the song position in midi ticks
OPErr GM_SetSongTickPosition64(GM_Song *pSong, uint64_t songTickPosition)
{
    GM_Song *theSong;
    OPErr theErr;
    bool foundPosition;
    int32_t count;
    bool songPaused = FALSE;

    if (!pSong)
        return PARAM_ERR;
    if (pSong->seqType != SEQ_MIDI)
    {
        return NOT_SETUP;
    }
    theErr = NO_ERR;
    theSong = (GM_Song *)XNewPtr(sizeof(GM_Song));
    if (theSong)
    {
        *theSong = *pSong;
        PV_ClearSongInstruments(theSong); // don't free the instruments

        theErr = PV_ConfigureMusic(theSong);
        if (theErr == NO_ERR)
        {
            theSong->AnalyzeMode = SCAN_DETERMINE_LENGTH;
            theSong->SomeTrackIsAlive = TRUE;

            GM_SetSongLoopFlag(theSong, FALSE);
            GM_SetSongMetaLoopFlag(theSong, FALSE);
            foundPosition = FALSE;
            if (pSong->songPaused)
            {
                songPaused = TRUE;
            }
            GM_PauseSong(pSong, TRUE);
            while (theSong->SomeTrackIsAlive)
            {
                // don't need a thread context here because we don't callback
                theErr = PV_ProcessMidiSequencerSlice(NULL, theSong);
                if (theErr)
                {
                    break;
                }
                if (PV_UFloatToUInt64(theSong->CurrentMidiClock) > songTickPosition)
                {
                    foundPosition = TRUE;
                    break;
                }
            }
            theSong->AnalyzeMode = SCAN_NORMAL;
            GM_SetSongLoopFlag(theSong, GM_GetSongLoopFlag(pSong));
            GM_SetSongMetaLoopFlag(theSong, GM_GetSongMetaLoopFlag(pSong));
            if (foundPosition)
            {
                for (count = 0; count < (MAX_INSTRUMENTS * MAX_BANKS); count++)
                {
                    theSong->instrumentData[count] = pSong->instrumentData[count];
                }

                GM_EndSongNotes(pSong);
                *pSong = *theSong;                // copy over all song information at the new position
                PV_ClearSongInstruments(theSong); // don't free the instruments

#if USE_SF2_SUPPORT == TRUE
#if _USING_FLUIDLITE == TRUE
                if (GM_IsSF2Song(pSong) && GM_SF2_IsActive())
                {
                    (void)GM_EnableSF2ForSong(pSong, TRUE);
                }
#endif
#endif
            }

            if (!songPaused)
            {
                GM_ResumeSong(pSong);
            }
            // free duplicate song
            theSong->sequenceData = NULL;
            theSong->disposeSongDataWhenDone = FALSE;
            theSong->songEndCallbackPtr = NULL;
            theSong->songTimeCallbackPtr = NULL;
            theSong->metaEventCallbackPtr = NULL;
            theSong->controllerCallback = NULL;
        }
        // don't need a thread context here because we don't callback
        GM_FreeSong(NULL, theSong); // we ignore the error codes, because it should be ok to dispose
                                    // since this song was never engaged
    }
    return theErr;
}

OPErr GM_SetSongTickPosition(GM_Song *pSong, uint32_t songTickPosition)
{
    return GM_SetSongTickPosition64(pSong, (uint64_t)songTickPosition);
}

uint64_t GM_SongTicks64(GM_Song *pSong)
{
    if (pSong)
    {
        if (GM_IsSongDone(pSong) == FALSE)
        {
            return PV_UFloatToUInt64(pSong->CurrentMidiClock);
        }
    }
    return 0;
}

uint32_t GM_SongTicks(GM_Song *pSong)
{
    uint64_t ticks = GM_SongTicks64(pSong);
    return (ticks > UINT32_MAX) ? UINT32_MAX : (uint32_t)ticks;
}

uint64_t GM_SongMicroseconds64(GM_Song *pSong)
{
    if (pSong)
    {
        if (GM_IsSongDone(pSong) == FALSE)
        {
            return PV_UFloatToUInt64(pSong->songMicroseconds);
        }
    }
    return 0;
}

uint32_t GM_SongMicroseconds(GM_Song *pSong)
{
    uint64_t us = GM_SongMicroseconds64(pSong);
    return (us > UINT32_MAX) ? UINT32_MAX : (uint32_t)us;
}

uint64_t GM_GetSongMicrosecondLength64(GM_Song *pSong, OPErr *pErr)
{
    OPErr err = NO_ERR;
    uint64_t ms = 0;

    if (!pSong)
    {
        if (pErr)
            *pErr = PARAM_ERR;
        return 0;
    }

    (void)GM_GetSongTickLength64(pSong, &err);
    if (err == NO_ERR)
        ms = PV_UFloatToUInt64(pSong->songMicrosecondLength);

    if (pErr)
        *pErr = err;
    return ms;
}

uint32_t GM_GetSongMicrosecondLength(GM_Song *pSong, OPErr *pErr)
{
    uint32_t ms = 0;

    if (pErr && pSong)
    {
        uint64_t ms64 = GM_GetSongMicrosecondLength64(pSong, pErr);
        if (*pErr == NO_ERR)
        {
            if (ms64 > UINT32_MAX)
            {
                ms = 0;
                *pErr = OUT_OF_RANGE;
            }
            else
            {
                ms = (uint32_t)ms64;
            }
        }
    }
    return ms;
}

// Set the song position in microseconds
// $$kk: 08.12.98 merge: changed this method
OPErr GM_SetSongMicrosecondPosition64(GM_Song *pSong, uint64_t songMicrosecondPosition)
{
    GM_Song *theSong;
    OPErr theErr;
    bool foundPosition;
    int32_t count;
    bool songPaused = FALSE;

    // $$kk: 02.10.98
    // the way this was, it paused the song, changed the position, and resumed.
    // if this is applied to a paused song, it suddenly starts playing again!
    // i am adding a mechanism to record whether the song was paused and only
    // resume it in that case.

    if (!pSong)
        return PARAM_ERR;
    if (pSong->seqType != SEQ_MIDI)
    {
        return NOT_SETUP;
    }
    theErr = NO_ERR;
    theSong = (GM_Song *)XNewPtr(sizeof(GM_Song));
    if (theSong)
    {
        *theSong = *pSong;
        PV_ClearSongInstruments(theSong); // don't free the instruments

        theErr = PV_ConfigureMusic(theSong);
        if (theErr == NO_ERR)
        {
            theSong->AnalyzeMode = SCAN_DETERMINE_LENGTH;
            theSong->SomeTrackIsAlive = TRUE;

            GM_SetSongLoopFlag(theSong, FALSE);
            GM_SetSongMetaLoopFlag(theSong, FALSE);
            foundPosition = FALSE;

            // $$kk: 02.10.98: keep track of whether song is paused
            if (pSong->songPaused)
            {
                songPaused = TRUE;
            }
            GM_PauseSong(pSong, TRUE);
            while (theSong->SomeTrackIsAlive)
            {
                // don't need a thread context here because we don't callback
                theErr = PV_ProcessMidiSequencerSlice(NULL, theSong);
                if (theErr)
                {
                    break;
                }
                if (PV_UFloatToUInt64(theSong->songMicroseconds) > songMicrosecondPosition)
                {
                    foundPosition = TRUE;
                    break;
                }
            }
            theSong->AnalyzeMode = SCAN_NORMAL;
            GM_SetSongLoopFlag(theSong, GM_GetSongLoopFlag(pSong));
            GM_SetSongMetaLoopFlag(theSong, GM_GetSongMetaLoopFlag(pSong));
            if (foundPosition)
            {
                for (count = 0; count < (MAX_INSTRUMENTS * MAX_BANKS); count++)
                {
                    theSong->instrumentData[count] = pSong->instrumentData[count];
                }

                GM_KillSongNotes(pSong);
                *pSong = *theSong;                // copy over all song information at the new position
                PV_ClearSongInstruments(theSong); // don't free the instruments

#if USE_SF2_SUPPORT == TRUE
#if _USING_FLUIDLITE == TRUE
                if (GM_IsSF2Song(pSong) && GM_SF2_IsActive())
                {
                    (void)GM_EnableSF2ForSong(pSong, TRUE);
                }
#endif
#endif
            }

            // $$kk: 02.10.98: do not resume if song was paused before
            if (!songPaused)
            {
                GM_ResumeSong(pSong);
            }
            // free duplicate song
            theSong->sequenceData = NULL;
            theSong->songEndCallbackPtr = NULL;
            theSong->disposeSongDataWhenDone = FALSE;
        }
        // don't need a thread context here because we don't callback
        GM_FreeSong(NULL, theSong); // we ignore the error codes, because it should be ok to dispose
                                    // since this song was never engaged
    }
    return theErr;
}

OPErr GM_SetSongMicrosecondPosition(GM_Song *pSong, uint32_t songMicrosecondPosition)
{
    return GM_SetSongMicrosecondPosition64(pSong, (uint64_t)songMicrosecondPosition);
}

// Return the used patch array of instruments used in the song passed.

//  theExternalSong     standard SONG resource structure
//  theExternalMidiData if not NULL, then will use this midi data rather than what is found in external SONG resource
//  midiSize            size of midi data if theExternalMidiData is not NULL
//  pInstrumentArray    array, if not NULL will be filled with the instruments that need to be loaded.
//  pErr                pointer to an OPErr
#if USE_CREATION_API == TRUE
int32_t GM_GetUsedPatchlist(void *theExternalSong,
                          void *theExternalMidiData,
                          int32_t midiSize,
                          XShortResourceID *pInstrumentArray,
                          OPErr *pErr)
{
    GM_Song *theSong;
    int32_t count;
    XBankToken bankToken;

    *pErr = NO_ERR;

    bankToken = CreateBankToken();
    // don't need a thread context here because we don't callback
    theSong = GM_LoadSong(NULL,
                          NULL,
                          NULL,
                          0,
                          theExternalSong,
                          theExternalMidiData,
                          midiSize,
                          pInstrumentArray,
                          FALSE,
                          TRUE,
                          bankToken,
                          pErr);
    if (theSong)
    {
        GM_FreeSong(NULL, theSong); // we ignore the error codes, because it should be ok to dispose
                                    // since this song was never engaged
    }

    count = 0;
    if (*pErr == NO_ERR)
    {
        for (; count < MAX_INSTRUMENTS * MAX_BANKS; count++)
        {
            if (pInstrumentArray[count] == (XShortResourceID)-1)
            {
                break;
            }
        }
    }
    return count;
}
#endif // USE_CREATION_API

void GM_SetVelocityCurveType(GM_Song *pSong, VelocityCurveType velocityCurveType)
{
    if (pSong)
    {
        pSong->velocityCurveType = velocityCurveType;
    }
}

OPErr GM_SetDisposeSongDataWhenDoneFlag(GM_Song *pSong, bool disposeData)
{
    OPErr theErr;

    theErr = NO_ERR;
    if (pSong)
    {
        pSong->disposeSongDataWhenDone = disposeData;
    }
    else
    {
        theErr = NOT_SETUP;
    }
    return theErr;
}

OPErr GM_GetDisposeSongDataWhenDoneFlag(GM_Song *pSong, bool *outDisposeData)
{
    OPErr theErr;

    theErr = NO_ERR;
    if (pSong)
    {
        if (outDisposeData)
        {
            *outDisposeData = pSong->disposeSongDataWhenDone;
        }
        else
        {
            theErr = PARAM_ERR;
        }
    }
    else
    {
        theErr = NOT_SETUP;
    }
    return theErr;
}

OPErr GM_GetSongVoices(GM_Song *pSong, int16_t *pMaxSongVoices, int16_t *pMixLevel, int16_t *pMaxEffectVoices)
{
    OPErr theErr;
    GM_Mixer *pMixer;

    theErr = NO_ERR;
    if (pSong)
    {
        if (pMaxSongVoices && pMixLevel && pMaxEffectVoices)
        {
            pMixer = pSong->pMixer ? pSong->pMixer : MusicGlobals;
            if (pMixer)
            {
                *pMaxSongVoices = pMixer->MaxNotes;
                *pMixLevel = pMixer->mixLevel;
                *pMaxEffectVoices = pMixer->MaxEffects;
            }
            else
            {
                *pMaxSongVoices = pSong->maxSongVoices;
                *pMixLevel = pSong->mixLevel;
                *pMaxEffectVoices = pSong->maxEffectVoices;
            }
        }
        else
        {
            theErr = PARAM_ERR;
        }
    }
    else
    {
        theErr = NOT_SETUP;
    }
    return theErr;
}

OPErr GM_ChangeSongVoices(GM_Song *pSong, int16_t maxSongVoices, int16_t mixLevel, int16_t maxEffectVoices)
{
    OPErr theErr;

    theErr = NO_ERR;
    if (pSong)
    {
        if ((maxSongVoices >= 0) &&
            (mixLevel > 0) &&
            (maxEffectVoices >= 0) &&
            ((maxEffectVoices + maxSongVoices) > 0) &&
            ((maxEffectVoices + maxSongVoices) <= MAX_VOICES))
        {
            pSong->maxSongVoices = maxSongVoices;
            pSong->mixLevel = mixLevel;
            pSong->maxEffectVoices = maxEffectVoices;
        }
        else
        {
            theErr = PARAM_ERR;
        }
    }
    else
    {
        theErr = NOT_SETUP;
    }
    return theErr;
}

OPErr GM_GetProgramBank(GM_Song *pSong, int16_t channel, int16_t *outProgram, int16_t *outBank, bool useRawBank)
{
    OPErr err;

    err = NO_ERR;
    if (pSong)
    {
        if (outProgram && outBank)
        {
            *outProgram = pSong->channelProgram[channel];
            *outBank = useRawBank ? pSong->channelRawBank[channel] : pSong->channelBank[channel];
        }
        else
        {
            err = PARAM_ERR;
        }
    }
    else
    {
        err = NOT_SETUP;
    }
    return err;
}

// Fast MIDI+patch loudness estimate → song normalize gain (no audio render).
OPErr GM_Song_EstimateNormalizePeak(GM_Song *pSong,
                                    int32_t targetPeakPct,
                                    int32_t *outAppliedGainPct)
{
    OPErr theErr;
    bool loopSongSave;
    bool metaLoopSave;
    ScanMode savedMode;
    float peakLin;
    int32_t peakAbs;
    int32_t gainPct;
    ReverbMode reverb;

    if (outAppliedGainPct)
        *outAppliedGainPct = 100;

    if (!pSong || pSong->seqType != SEQ_MIDI)
        return NOT_SETUP;

    if (targetPeakPct <= 0)
        targetPeakPct = 89; /* ~-1 dBFS */
    if (targetPeakPct > 99)
        targetPeakPct = 99;

    /* Bind the song's mixer so normalize gain writes do not hit another session. */
    {
        GM_Mixer *songMixer = pSong->pMixer ? pSong->pMixer : MusicGlobals;
        if (songMixer)
            GM_SetCurrentMixer(songMixer);
    }

    GM_SetSongNormalizeGain(100);
    GM_EstimatePeak_Reset();

    savedMode = pSong->AnalyzeMode;
    loopSongSave = GM_GetSongLoopFlag(pSong);
    metaLoopSave = GM_GetSongMetaLoopFlag(pSong);

    theErr = PV_ConfigureMusic(pSong);
    if (theErr != NO_ERR)
    {
        pSong->AnalyzeMode = savedMode;
        return theErr;
    }

    pSong->AnalyzeMode = SCAN_ESTIMATE_PEAK;
    pSong->SomeTrackIsAlive = TRUE;
    pSong->voiceCount = 0;
    pSong->voiceSustain = 0;
    GM_SetSongLoopFlag(pSong, FALSE);
    GM_SetSongMetaLoopFlag(pSong, FALSE);

    while (pSong->SomeTrackIsAlive)
    {
        theErr = PV_ProcessMidiSequencerSlice(NULL, pSong);
        if (theErr)
            break;
    }

    pSong->AnalyzeMode = SCAN_NORMAL;
    GM_SetSongLoopFlag(pSong, loopSongSave);
    GM_SetSongMetaLoopFlag(pSong, metaLoopSave);
    GM_ResetTempoToDefault(pSong);
    PV_ConfigureMusic(pSong); /* rewind tracks for subsequent Preroll/Start */
    pSong->AnalyzeMode = savedMode;
    pSong->songPaused = FALSE;
    pSong->songPrerolled = FALSE;
    pSong->songFinished = FALSE;

    if (theErr != NO_ERR && theErr != STILL_PLAYING)
    {
        GM_SetSongNormalizeGain(100);
        return theErr;
    }

    peakLin = GM_EstimatePeak_GetMax();
    reverb = GM_GetReverbType();
    /* Estimate under-reads real peaks (ADSR hits, modulators, dense stacking).
     * Inflate peak so applied gain sits a little under the clip point. */
    peakLin *= 1.12f;
    /* Wet path is invisible to the MIDI estimate; pad further with reverb. */
    if (reverb > REVERB_TYPE_1)
        peakLin *= 1.12f;

    /* Native HSB/ZSB (and RMF on that path) peaks hotter than the MIDI+RMS
     * estimate — ADSR hits, scaleBack stacking, etc. Extra pad avoids overgain.
     * Shared for all platforms (zefidi/playbae/Android); do not fork per-OS. */
    {
        bool nativeHsb = TRUE;
#if USE_SF2_SUPPORT == TRUE
        if (GM_IsSF2Song(pSong))
            nativeHsb = FALSE;
#endif
#if USE_NATIVE_DLS == TRUE
        if (GM_IsDLSSong(pSong))
            nativeHsb = FALSE;
#endif
        if (nativeHsb)
            peakLin *= 1.333521432163324f; /* ~+2.5 dB — some HSB/ZSB banks still overgain at +1.5 */
    }

    if (peakLin <= 1.0e-6f)
    {
        gainPct = 100;
    }
    else
    {
        peakAbs = (int32_t)(peakLin * 32767.0f + 0.5f);
        if (peakAbs < 1)
            peakAbs = 1;
        gainPct = (int32_t)(((int64_t)targetPeakPct * 32767LL) / (int64_t)peakAbs);
        if (gainPct < 5)
            gainPct = 5;
        if (gainPct > 800)
            gainPct = 800;
    }

    GM_SetSongNormalizeGain(gainPct);
    if (outAppliedGainPct)
        *outAppliedGainPct = gainPct;
    return NO_ERR;
}

// EOF of GenSong.c
