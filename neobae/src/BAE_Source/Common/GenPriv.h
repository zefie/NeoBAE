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
** "GenPriv.h"
**
**  Generalized Music Synthesis package. Part of SoundMusicSys.
**
**  © Copyright 1993-2001 Beatnik, Inc, All Rights Reserved.
**  Written by Jim Nitchals and Steve Hales
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
**
** Overview
**  Private structures
**
** Modification History
**
**  11/7/95     Major changes, revised just about everything.
**  11/11/95    Added microSyncCount for live link
**  11/16/95    Removed microSyncCount
**              Moved static variables into MusicVar structure
**              Created an external function 'PV_GetExternalTimeSync()' for the external midi source
**   12/95      upgraded mixing bus to 32 bit; improved scaleback resolution; added reverb unit; first pass at volume ADSR
**  12/6/95     removed reference to USE_AMP_LOOKUP
**              moved REVERB_TYPE to GENSND.H
**  12/7/95     Added channelReverb to GM_Mixer structure
**              Added REVERB_CONTROLER_THRESHOLD
**  1/18/96     Spruced up for C++ extra error checking
**              Changed InstUsedList to pUsedList and allocate it when needed
**  2/5/96      Removed unused variables. Working towards multiple songs
**              Moved lots of variables from the GM_Mixer structure into
**              Moved the MAX_TRACKS define to GenSnd.h
**  2/12/96     Added PV_CleanExternalQueue
**              Moved SongMicroseconds to GenSnd.h
**  2/13/96     Added multi song support
**  3/5/96      Eliminated the global songVolume
**  3/28/96     Added PV_SetSampleIntoCache & PV_GetInstrument
**  4/10/96     Reworked the sample cache system to not clone the sample data
**  5/2/96      Changed int to BOOL_FLAG
**  5/18/96     Added error condition to PV_MusicIRQ
**  6/30/96     Changed font and re tabbed
**  7/3/96      Added packing pragmas
**              Removed usage of Machine.h. Now merged into X_API.h
**  7/14/96     Fixed structure alignment issue for PowerPC
**  7/23/96     Changed PV_GetExternalTimeSync to uint32_t
**  7/24/96     Changed Midi Queue system to use a head/tail
**  7/25/96     Moved Mac audio variables to GenMacTools.c
**              Changed PV_GetExternalTimeSync to GM_GetSyncTimeStampQuantizedAhead
**  8/12/96     Changed PV_ResetControlers to support semi-complete reset
**  9/25/96     Added GM_Song pointer in NoteRecord structure
**  9/27/96     Added more parameters to ServeMIDINote & PV_StopMIDINote
**  10/18/96    Made CacheSampleInfo smaller
**  10/23/96    Removed reference to BYTE and changed them all to unsigned char or signed char
**  12/19/96    Added Sparc pragmas
**  12/30/96    Changed copyrights
**  1/23/97     Added support for stereoFilter
**              In NoteRecord changed PitchBend to NotePitchBend
**              Added in NoteRecord NoteFadeRate
**  1/30/97     Changed SYMPHONY_SIZE to MAX_VOICES
**  3/17/97     Changed the API to PV_GetInstrument. Enlarged CacheSampleInfo member
**              theID to a int32_t
**  4/9/97      Added sampleExpansion factor
**  4/20/97     Changed PV_MusicIRQ to PV_ProcessMidiSequencerSlice
**  6/4/97      Renamed InitSoundManager to GM_StartHardwareSoundManager, and
**              renamed FinsSoundManager to GM_StopHardwareSoundManager, and
**              now pass in a thread context
**  712/97      Added a drift fixer flag to GM_Mixer that tries
**              to compensate for real time midi time stamping
**  7/16/97     Moved GM_Mixer *MusicGlobals to be protected againsts C++
**              name mangling
**  7/17/97     Aligned GM_Mixer structure to 8 bytes
**  7/18/97     Moved GM_AudioTaskCallbackPtr pTaskProc &
**              GM_AudioOutputCallbackPtr pOutputProc into here from GenXXXTools.c
**  7/22/97     Changed SYNC_BUFFER_TIME to BUFFER_SLICE_TIME
**  7/28/97     Changed pack structure alignment for SPARC from 8 to 4. Compiler bug.
**  8/8/97      Added PV_FreePgmEntries
**  8/27/97     Moved GM_StartHardwareSoundManager & GM_StopHardwareSoundManager to
**              GenSnd.h
**  9/2/97      Fixed bug with THE_CHECK that forgot to look at zero length buffers
**  9/19/97     Changed name of PV_FreePatchInfo.
**              Added PV_InsertBankSelect
**  10/15/97    Added processingSlice to NoteRecord to handle threading issues
**  10/27/97    Removed reference to MusicGlobals->theSongPlaying
**  10/28/97    Eliminated reference to FAR
**  10/29/97    Promoted PV_AnyStereoInstrumentsLoaded to GM_AnyStereoInstrumentsLoaded and
**              moved it to GenSnd.h
**  12/4/97     Renamed GM_Mixer to GM_Mixer. Renamed NoteRecord to GM_Voice
**  1/14/98     kk: added NoteLoopTarget to GM_Voice (number of loops between loop points desired
**              before sample continues to end.
**              changed NoteLoopCount from unsigned char to uint32_t because we are actually counting loops now and  
**              may want quite a few.
**  1/27/98     Renamed MACINTOSH to H_MACINTOSH
**  2/3/98      Renamed songBufferLeftMono to songBufferDry
**  2/5/98      Added a GM_Song pointer to PV_SetSampleIntoCache
**  2/8/98      Changed BOOL_FLAG to bool
**  2/10/98     added a bunch of structures for storing new effect parameters
**  2/20/98     kcr converted floating-point to fixed for new effects -- added chorus buffer
**  2/23/98     Removed last of old variable reverb code
**  2/24/98     kcr deal with sample-rate changes for chorus and reverb...
**  3/16/98     Removed PV_ProcessReverbMono & PV_ProcessReverbStereo & PV_PostFilterStereo
**              from public view
**              Changed InitNewReverb to return a bool for success or failure
**  4/1/98      MOE: took out references to FilterEnvelope{} so that all compiles
**  4/14/98     Added some comments and removed extra structures that are not being used
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
**  7/1/98      Changed various API to use the new XResourceType and XLongResourceID
**  7/7/98      Removed reverbIsVariable from GM_Mixer structure. Using function 
**              GM_IsReverbFixed instead.
**  7/28/98     Renamed inst_struct to pInstrument
**              Changed meaning of processExternalMidiQueue in GM_Song. Now its a counter
**              instead of just a boolean.
**  7/30/98     Added constant value to MAX_CHUNK_SIZE for 48k output in GM_Mixer structure
**  8/12/98     Added PV_ModifyVelocityFromCurve
**  10/27/98    Moved MIN_LOOP_SIZE to GenSnd.h
**  11/9/98     Renamed NoteDur to voiceMode
**  12/22/98    Removed old USE_SEQUENCER flag
**  1/12/99     Added a useKatmaiCPU flag that is dynamic if the USE_KAT flag
**              is set to build.
**  3/1/99      Changed NoteRefNum to NoteContext
**  3/3/99      Added PV_GetVoiceFromSoundReference
**              Added voiceStartTimeStamp to GM_Voice
**              Removed USE_DIRECT_MIXDOWN
**  3/5/99      Added VOICE_ALLOCATED_READY_TO_SYNC_START
**              Added threadContext to PV_ServeEffectCallbacks & PV_ProcessSampleFrame &
**              PV_ProcessSequencerEvents & PV_ProcessMidiSequencerSlice
**  3/11/99     Renamed ADSRRecord to GM_ADSR. Renamed LFORecord to GM_LFO. Renamed CurveRecord to GM_TieTo.
**  3/31/99     Added pMixer pointer to the GM_Voice structure.
**              Renamed ServeMIDINote to PV_StartMIDINote, renamed StopMIDINote to PV_StopMIDINote
**  5/10/99     Removed CODE_TYPE
**  5/28/99     MOE:  Moved DEFAULT_ constants to GenSnd.h
**  6/8/99      Modified Q_MIDIEvent and added an status byte for type of event to work with the new
**              wire event model.
**  6/15/99     Changed PV_CleanExternalQueue parmeters
**  7/9/99      Added taskReference for the Taskcallback
**  7/19/99     Renamed unsigned char to unsigned char. Renamed int16_t to int16_t. Renamed int32_t to int32_t.
**              Renamed uint32_t to uint32_t. Renamed signed char to signed char. Renamed uint16_t to uint16_t
**  8/3/99      Changed pragma settings for X_BE
**  10/19/99    MSD: switched to REVERB_USED and LOOPS_USED
**  10/30/99    Removed cacheBlockID field from CacheSampleInfo
**              Removed extra LFORecords in GM_Voice structure
**              Added the ability to change the heartbeat from 11610 ms to 10000 ms with
**              a compile time swtich. See BUFFER_SLICE_TIME
**  11/10/99    Set default to 10 ms slice time
**  2/4/2000    Changed copyright. We're Y2K compliant!
**  3/21/2000   Set default to 11 ms slice time. See BUFFER_SLICE_TIME.
**  5/9/2000    sh  Added FIXED_BUFFER_SLICE_TIME & FIXED_MAX_CHUNK_SIZE. Need
**                  this is allow for different audio render rates, because our
**                  midi decoder and lfo's run at a fixed 11.61 ms rate.
**  2000.05.15 AER  Renamed CacheSampleInfo to GM_SampleCacheEntry
**  2000.05.16 AER  Completed modifications for new sample cache
**  2000.05.28 sh   Added PV_UnloadInstrumentData, and documented PV_GetInstrument
**  2000.06.01 sh   Changed element voiceStartTimeStamp in GM_Voice structure
**                  to a uint32_t.
**  7/07/2000  DS:  Increased MIDI queue size to 1024 for Windows platform.  Added
**                  mutex struct to GM_Mixer as future placeholder, but #ifdef'd out.
**  7/11/2000  DS:  Added NoteStartFrame member to GM_Voice.
**  9/7/2000    sh  Comments added to NoteProgram for GM_Voice. Increased size to a
**                  XLongResourceID to better reflect what how the value is defined.
**  2/18/2001   sh  Added GM_Mixer::outputReference for callbacks.
**              sh  Enabled bitfields for GM_Voice & GM_Mixer
**  3/28/2001   sh  grrr. Corrected (!) bitfield size of GM_Voice::sustainMode.
**                  Suppose to be 2 bits, not 1.
**  4/18/2001   sh  Added PV_GetVoiceNumberFromVoice
**  4/19/2001   sh  Removed songBufferDry from GM_Mixer for split build
**                  Add U3232_TO_XFIXED & ADD_32_16
**  4/23/2001   sh  Added PV_CalculateMonoVolume
**  5/23/2001   sh  Removed bitfields. Failed on gcc.
**  7/5/2001        Modified parmeters of PV_GetInstrument
*/
/*****************************************************************************/

#ifndef G_PRIVATE
#define G_PRIVATE

#include "BAE_API.h"
#include "X_API.h"
#include "GenSnd.h"

#define VOLUME_PRECISION_SCALAR     6L      // used to be 8, so we must scale down output by 2
#define OUTPUT_SCALAR               9L      // 9 for volume minus 4 for increased volume_range resolution, plus 2 for increased volume precision scalar
#define VOLUME_RANGE                4096    // original range was 256, therefore:
#define UPSCALAR                    16L     // multiplier (NOT a shift count!) for increasing amplitude resolution
#define MAXRESONANCE                127     // mask and buffer size for resonant filter.  Higher means wider frequency range.

// BUFFER_SLICE_TIME is calculated by the formula:
//
// 1 second / sample rate * samples
// 1 000 000 / 22050 * 256
//
// the amount of time in microseconds that
// passes when calling ProcessSampleFrame
#if 1
    #define BUFFER_SLICE_TIME           11610
#else
    #define BUFFER_SLICE_TIME           10000
#endif

// These times are fixed because our LFO's, and midi decode code
// relies on the constant 11.6 ms decode rate. This allows for content to
// sound the same. 
#define FIXED_BUFFER_SLICE_TIME         11610
#define FIXED_MAX_CHUNK_SIZE            512

#if BUFFER_SLICE_TIME == 5000
    #define MAX_CHUNK_SIZE          224     // max samples to build per slice at 44k
#endif

#if BUFFER_SLICE_TIME == 10000
    #define MAX_CHUNK_SIZE          448     // max samples to build per slice at 44k
#endif

#if BUFFER_SLICE_TIME == 11610
    #define MAX_CHUNK_SIZE          512     // max samples to build per slice at 44k
#endif

#ifndef MAX_CHUNK_SIZE
    #error "MAX_CHUNK_SIZE not defined!" 
#endif

#if (MAX_CHUNK_SIZE%16) != 0
    #error "Bad MAX_CHUNK_SIZE, Divisible by 16 only!" 
#endif


#define SOUND_EFFECT_CHANNEL        16      // channel used for sound effects. One beyond the normal

// LIMITED_LOOPS sample position uses 20.12 XFIXED (STEP_BIT_RANGE=12).
// Whole-sample offset fits in 20 bits (~1,048,575 frames). Longer PCM can
// wrap/overflow THE_CHECK end_wave math — prefer U3232_LOOPS (or FLOAT) for
// large samples / streamed buffers. Cast pointer deltas to XFIXED before <<.
#define STEP_BIT_RANGE              12L
#define STEP_OVERFLOW_FLAG          (1<<(STEP_BIT_RANGE-1))     
#define STEP_FULL_RANGE             ((1<<STEP_BIT_RANGE)-1)

#define ALLOW_16_BIT            1           // 1 - allow 16 bit if available, 0 - force 8 bit
#define ALLOW_STEREO            1           // 1 - allow stereo if available, 0 - force mono
#define ALLOW_DEBUG_STEREO      0           // 1 - allow keyboard debugging of stereo code
#define USE_DLS                 0           // 1 - allow DLS changes, 0 - IGOR // Old DLS code not Native DLS

/* Extreme pitch can step past more than one loop length per output sample.
 * One subtract leaves the index past end_wave and the next source[] read OOBs.
 * Wrap until inside the loop; if wave_adjust is 0/invalid, kill the voice. */
#if USE_CALLBACKS
// a macro to handle broken loops and partial buffers in the inner loop code
#define THE_CHECK(TYPE) \
    if (cur_wave >= end_wave)\
    {\
        if (looping && wave_adjust > 0)\
        {\
            do {\
                cur_wave -= wave_adjust;\
            } while (cur_wave >= end_wave);\
            if (this_voice->doubleBufferProc)\
            {\
                /* we hit the end of the loop call double buffer to notify swap*/ \
                if (PV_DoubleBufferCallbackAndSwap(this_voice->doubleBufferProc, this_voice)) \
                {\
                    /* recalculate our internal pointers */\
                    end_wave = (XFIXED)(this_voice->NoteLoopEnd - this_voice->NotePtr) << STEP_BIT_RANGE;\
                    wave_adjust =  (XFIXED)(this_voice->NoteLoopEnd - this_voice->NoteLoopPtr) << STEP_BIT_RANGE;\
                    source = (TYPE) this_voice->NotePtr;\
                    if (wave_adjust > 0)\
                    {\
                        while (cur_wave >= end_wave)\
                            cur_wave -= wave_adjust;\
                    }\
                    if (cur_wave >= end_wave)\
                    {\
                        this_voice->voiceMode = VOICE_UNUSED;\
                        PV_DoCallBack(this_voice);\
                        goto FINISH;\
                    }\
                }\
                else\
                {\
                    goto FINISH;\
                }\
            }\
        }\
        else\
        {\
            this_voice->voiceMode = VOICE_UNUSED;\
            PV_DoCallBack(this_voice);\
            goto FINISH;\
        }\
    }
#else
// a macro to handle broken loops and partial buffers in the inner loop code
#define THE_CHECK(TYPE) \
    if (cur_wave >= end_wave)\
    {\
        if (looping && wave_adjust > 0)\
        {\
            do {\
                cur_wave -= wave_adjust;\
            } while (cur_wave >= end_wave);\
        }\
        else\
        {\
            this_voice->voiceMode = VOICE_UNUSED;\
            goto FINISH;\
        }\
    }
#endif


#if LOOPS_USED == U3232_LOOPS

typedef uint32_t   U32;
typedef struct U3232
{
    U32     i;
    U32     f;
} U3232;


#define ADD_32_32(target_int, target_frac, addend_int, addend_frac)\
    {\
        target_frac += addend_frac;\
        if ((U32)target_frac < (U32)addend_frac) target_int++;\
        target_int += addend_int;\
    }
#define ADD_U3232(target_int, target_frac, u3232)\
    ADD_32_32(target_int, target_frac, u3232.i, u3232.f)

#define U3232_TO_XFIXED(u3232)  \
    (((u3232.i&0xFFFFL)<<16L) | (u3232.f>>16L))

#define ADD_32_16(target_int, target_frac, u1616) \
    ADD_32_32(target_int, target_frac, ((u1616&0xFFFF0000L)>>16L), ((u1616&0xFFFFL) << 16L))


#if USE_CALLBACKS
#define THE_CHECK_U3232(TYPE) \
    if (cur_wave_i >= end_wave)\
    {\
        if (looping && wave_adjust > 0)\
        {\
            /* Extreme pitch: step may exceed one loop length — wrap with modulo. */\
            {\
                U32 loop_start = end_wave - wave_adjust;\
                cur_wave_i = loop_start + ((cur_wave_i - loop_start) % wave_adjust);\
            }\
/*          cur_wave_f = 0; TRY PUTTING THIS IN SOME DAY, MIGHT SOUND BETTER */\
            if (this_voice->doubleBufferProc)\
            {\
                /* we hit the end of the loop call double buffer to notify swap*/ \
                if (PV_DoubleBufferCallbackAndSwap(this_voice->doubleBufferProc, this_voice)) \
                {\
                    /* recalculate our internal pointers */\
                    end_wave = this_voice->NoteLoopEnd - this_voice->NotePtr;\
                    wave_adjust = this_voice->NoteLoopEnd - this_voice->NoteLoopPtr;\
                    source = (TYPE)this_voice->NotePtr;\
                    if (wave_adjust > 0 && cur_wave_i >= end_wave)\
                    {\
                        U32 loop_start = end_wave - wave_adjust;\
                        cur_wave_i = loop_start + ((cur_wave_i - loop_start) % wave_adjust);\
                    }\
                    if (cur_wave_i >= end_wave)\
                    {\
                        this_voice->voiceMode = VOICE_UNUSED;\
                        PV_DoCallBack(this_voice);\
                        goto FINISH;\
                    }\
                }\
                else\
                {\
                    goto FINISH;\
                }\
            }\
        }\
        else\
        {\
            this_voice->voiceMode = VOICE_UNUSED;\
            PV_DoCallBack(this_voice);\
            goto FINISH;\
        }\
    }
#else
#define THE_CHECK_U3232(TYPE) \
    if (cur_wave_i >= end_wave)\
    {\
        if (looping && wave_adjust > 0)\
        {\
            {\
                U32 loop_start = end_wave - wave_adjust;\
                cur_wave_i = loop_start + ((cur_wave_i - loop_start) % wave_adjust);\
            }\
/*          cur_wave_f = 0; TRY PUTTING THIS IN SOME DAY, MIGHT SOUND BETTER */\
        }\
        else\
        {\
            this_voice->voiceMode = VOICE_UNUSED;\
            goto FINISH;\
        }\
    }
#endif
#endif


#if LOOPS_USED == FLOAT_LOOPS
#if USE_CALLBACKS
#define THE_CHECK_FLOAT(TYPE) \
    if (cur_wave >= end_wave)\
    {\
        if (looping && wave_adjust > 0)\
        {\
            do {\
                cur_wave -= wave_adjust;\
            } while (cur_wave >= end_wave);\
            if (this_voice->doubleBufferProc)\
            {\
                /* we hit the end of the loop call double buffer to notify swap*/ \
                if (PV_DoubleBufferCallbackAndSwap(this_voice->doubleBufferProc, this_voice)) \
                {\
                    /* recalculate our internal pointers */\
                    end_wave = (this_voice->NoteLoopEnd - this_voice->NotePtr);\
                    wave_adjust = (this_voice->NoteLoopEnd - this_voice->NoteLoopPtr);\
                    source = (TYPE) this_voice->NotePtr;\
                    if (wave_adjust > 0)\
                    {\
                        while (cur_wave >= end_wave)\
                            cur_wave -= wave_adjust;\
                    }\
                    if (cur_wave >= end_wave)\
                    {\
                        this_voice->voiceMode = VOICE_UNUSED;\
                        PV_DoCallBack(this_voice);\
                        goto FINISH;\
                    }\
                }\
                else\
                {\
                    goto FINISH;\
                }\
            }\
        }\
        else\
        {\
            this_voice->voiceMode = VOICE_UNUSED;\
            PV_DoCallBack(this_voice);\
            goto FINISH;\
        }\
    }
#else
#define THE_CHECK_FLOAT(TYPE) \
    if (cur_wave >= end_wave)\
    {\
        if (looping && wave_adjust > 0)\
        {\
            do {\
                cur_wave -= wave_adjust;\
            } while (cur_wave >= end_wave);\
        }\
        else\
        {\
            this_voice->voiceMode = VOICE_UNUSED;\
            goto FINISH;\
        }\
    }
#endif
#endif

typedef unsigned char           OUTSAMPLE8;
typedef int16_t               OUTSAMPLE16;        // 16 bit output sample

enum
{
    SUS_NORMAL          =   0,      // normal release at note off
    SUS_ON_NOTE_ON      =   1,      // note on, with pedal
    SUS_ON_NOTE_OFF     =   2       // note off, with pedal
};


#define X_PACK_FAST
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wpragma-pack"
#endif
#include "X_PackStructures.h"
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

// Mode in which a GM_Voice is currently being used
typedef enum 
{
    // These are left as reference. They refer to the old code base of what the numbers
    // ment.
//  VOICE_UNUSED        =   -1,             // voice is free
//  VOICE_RELEASING     =   0,              // voice is releasing
//  VOICE_SUSTAINING    =   32767,          // voice is sustaining
//  VOICE_ALLOCATED     =   1               // voice is allocated, but not active

    VOICE_UNUSED        =   0,              // voice is free
    VOICE_ALLOCATED,                        // voice is allocated, but not active
    VOICE_ALLOCATED_READY_TO_SYNC_START,    // voice is allocated, ready to start and on the next slice it will
                                            // set them to VOICE_SUSTAINING. This will look at the syncVoiceReference
                                            // variable and start all voices with the same reference
    VOICE_RELEASING,                        // voice is releasing
    VOICE_SUSTAINING                        // voice is sustaining
} VoiceMode;

// Need a forward reference to the GM_Mixer struct to keep
// our compiler from complaining.
struct GM_Mixer;

// This structure is created and maintained for each sample that is to mixed into the final output
struct GM_Voice
{
    VoiceMode               voiceMode;              // duration of note to play. VOICE_UNUSED is dead
                                                    // This field must be first!
    void                    *syncVoiceReference;    // this field is used when voiceMode has been set to VOICE_ALLOCATED_READY_TO_SYNC_START
                                                    // A single pass search will happen and it will look for matching syncVoiceReference
                                                    // values. Once the voice is started it will be set to NULL.
    int16_t                  NoteDecay;              // after voiceMode == VOICE_RELEASING then this is ticks of decay
    uint32_t                  voiceStartTimeStamp;    // this is a time stamp of when this voice is started, used to
                                                    // track unique voices
    GM_Instrument           *pInstrument;           // read-only pointer to instrument information
    GM_Song                 *pSong;                 // read-only pointer to song information
    struct GM_Mixer         *pMixer;                // read-only pointer to mixer information
                                                    // used to backtrace where note came from
    unsigned char                   *NotePtr;               // pointer to start of sample
    unsigned char                   *NotePtrEnd;            // pointer to end of sample
    uint32_t                  NoteStartFrame;         // offset to start of sample in frames
#if LOOPS_USED == U3232_LOOPS
    U3232                   samplePosition;         // new index from NotePtr
#endif
#if LOOPS_USED == FLOAT_LOOPS
    UFLOAT                  samplePosition_f;       // new index from NotePtr
#endif
    XFIXED                  NoteWave;               // current fractional position within sample (NotePtr:NotePtrEnd)
    XFIXED                  NotePitch;              // playback pitch in 16.16 fixed. 1.0 will play recorded speed
    XFIXED                  noteSamplePitchAdjust;  // adjustment to pitch based on difference from 22KHz in recorded rate
    unsigned char                   *NoteLoopPtr;           // pointer to start of loop point within NotePtr & NotePtrEnd
    unsigned char                   *NoteLoopEnd;           // pointer to end of loop point within NotePtr & NotePtrEnd
    
    // $$kk: 01.14.98: added NoteLoopTarget
    uint32_t                  NoteLoopTarget;         // target number of loops before continuing to end of sample

#if USE_CALLBACKS
    void                    *NoteContext;           // user context for callbacks
// Double buffer variables. If using double buffering, then doubleBufferPtr1 will be non-zero. These variables
// will be swapped with NotePtr, NotePtrEnd, NoteLoopPtr, NoteLoopPtrEnd
    unsigned char                       *doubleBufferPtr1;
    unsigned char                       *doubleBufferPtr2;
    GM_DoubleBufferCallbackPtr  doubleBufferProc;

// Call back procs
    GM_LoopDoneCallbackPtr      NoteLoopProc;       // normal loop continue proc
    GM_SoundDoneCallbackPtr     NoteEndCallback;    // sample done callback proc
#endif

    int32_t                  NoteNextSize;           // frames advanced per slice. Use 0 to recalculate (int32: high pitch overflows int16)
    signed char                  NoteMIDIPitch;          // midi note pitch to start note
    signed char                  noteOffsetStart;        // at the start of the midi note, what was the offset
    int16_t                  ProcessedPitch;         // actual pitch to play (proccessed)
    XLongResourceID         NoteProgram;            // note program number. This is a combined value
                                                    // of program and bank.
    signed char                  NoteChannel;            // channel note is playing on
    signed char                  NoteTrack;              // track note is playing on
    int32_t                 NoteVolume;             // note volume (scaled)
    int16_t                  NoteVolumeEnvelope;     // scalar from volume ADSR and LFO's.  0 min, VOLUME_RANGE max.
    int16_t                  NoteVolumeEnvelopeBeforeLFO;    // as described.
    int16_t                  NoteMIDIVolume;         // note volume (unscaled)
    int16_t                  NotePitchBend;          // 8.8 Fixed amount of bend
    int16_t                  ModWheelValue;          // 0-127
    int16_t                  LastModWheelValue;      // has it changed?  This is how we know.
    int16_t                  LastPitchBend;          // last bend
    int16_t                  stereoPosition;         // -63 (left) 0 (Middle) 63 (Right)
    int16_t                  routeBus;

    // $$kk: 01.14.98: changed NoteLoopCount from unsigned char to int32_t because we are actually counting loops now and may want quite a few
    uint32_t                  NoteLoopCount;

    unsigned char                   bitSize;                // 8 or 16 bit data
    unsigned char                   channels;               // mono or stereo data
    unsigned char                   sustainMode;            // sustain mode, for pedal controls
    unsigned char                   sampleAndHold;          // flag whether to sample & hold, or sample & release
    unsigned char                   advancedInterpolation;  // use higher-resolution interpolation for this voice
    unsigned char                   processingSlice;        // if TRUE, then thread is processing slice of this instrument
    unsigned char                   avoidReverb;            // don't mix into reverb unit
    uint32_t                  largestPeak;
#if REVERB_USED != REVERB_DISABLED
    unsigned char                   reverbLevel;            // 0-127 when reverb is enabled
#endif

// sound effects variables. Not used for normal envelope or instruments
    unsigned char                   soundEndAtFade;
    XFIXED                  soundFadeRate;          // when non-zero fading is enabled
    XFIXED                  soundFixedVolume;       // inital volume level that will be changed by soundFadeRate
    int16_t                  soundFadeMaxVolume;     // max volume
    int16_t                  soundFadeMinVolume;     // min volume
#if USE_CALLBACKS
    GM_SampleCallbackEntry  *pSampleMarkList;       // linked list of callbacks on a per sample frame basis
#endif

    int32_t                 stereoPanBend;

    GM_ADSR                 volumeADSRRecord;
    int32_t                 volumeLFOValue;
    int16_t                  LFORecordCount;
    GM_LFO                  LFORecords[MAX_LFOS];   // allocate for maximum allowed
    int32_t                 lastAmplitudeL;
    int32_t                 lastAmplitudeR;         // used to interpolate between points in volume ADSR
#if REVERB_USED != REVERB_DISABLED
    int16_t                  chorusLevel;            // 0-127 when chorus is enabled
#endif
    int16_t                  z[MAXRESONANCE+1];
    int32_t                 zIndex, Z1value, previous_zFrequency;
    int16_t                  zRight[MAXRESONANCE+1];
    int32_t                 zIndexRight, Z1valueRight, previous_zFrequencyRight;
    int32_t                 LPF_lowpassAmount, LPF_frequency, LPF_resonance;
    int32_t                 LPF_base_lowpassAmount, LPF_base_frequency, LPF_base_resonance;
//  int32_t                 s1Left, s2Left, s3Left, s4Left, s5Left, s6Left; // for INTERP3 mode only
#if USE_ZMF_SUPPORT == TRUE
    unsigned char           useOscillator;      // nonzero: audio-rate generator voice
    int32_t                 oscWaveShape;       // base shape from OSCL (may be overridden by WIDX)
    int32_t                 oscWaveIndexBase;   // index * step into oscillator wave table
    int32_t                 oscWaveIndexLive;   // base + WIDX modulation (~51 = one step; 255 ≈ full 0..5)
    int32_t                 oscPulseWidth;      // base PW from INST
    int32_t                 oscPulseWidthLive;  // after PWID LFO/ADSR modulation
    int32_t                 oscVolume;          // 0..65536 unity gain from OSCL
    uint32_t                oscPhase;           // 0..0xFFFFFFFF phase accumulator
    uint32_t                oscPhaseInc;        // phase increment per sample
    uint32_t                oscNoiseState;      // LFSR state for NOIS
    int32_t                 oscNoiseHeld;       // sample-and-hold output (note-rate noise)
#endif
};
typedef struct GM_Voice GM_Voice;

// support for historical reasons
#define NoteRecord  GM_Voice

// Structure used for caching samples for instruments
struct GM_SampleCacheEntry
{
    XSampleID       theID;          // sample ID
    XBankToken      bankToken;      // The unique bank token to supplement theID
    XFIXED          rate;           // sample rate
    uint32_t   waveSize;       // size in bytes
    uint32_t   waveFrames;     // number of frames
    uint32_t   loopStart;      // loop start frame
    uint32_t   loopEnd;        // loop end frame
    char            bitSize;        // sample bit size; 8 or 16
    char            channels;       // mono or stereo; 1 or 2
    unsigned char           sndFlags;       // XSoundHeader3 reserved2[0] flags
    int16_t       baseKey;        // base sample key
    int32_t            referenceCount; // how many references to this sample block
    void            *pSampleData;   // pointer to sample data. This may be an offset into the pMasterPtr
    void            *pMasterPtr;    // master pointer that contains the snd format information
};
typedef struct GM_SampleCacheEntry GM_SampleCacheEntry;

#define MAX_QUEUE_EVENTS                1024

#define REVERB_BUFFER_SIZE_SMALL        4096        // * sizeof(int32_t)
#define REVERB_BUFFER_MASK_SMALL        4095

#if REVERB_USED == SMALL_MEMORY_REVERB
    #define REVERB_BUFFER_SIZE          REVERB_BUFFER_SIZE_SMALL
    #define REVERB_BUFFER_MASK          REVERB_BUFFER_MASK_SMALL
#elif REVERB_USED == VARIABLE_REVERB
    #define REVERB_BUFFER_SIZE          16384
    #define REVERB_BUFFER_MASK_SHORT    16383
    #define REVERB_BUFFER_MASK          32767
#endif


enum
{
    Q_MIDI_DEAD = 0,
    Q_MIDI_ALLOCATING,
    Q_MIDI_READY
};

// This structure is to allow for queuing midi events into the playback other than those that are
// pulled from the midi file stream
struct Q_MIDIEvent
{
    GM_Song         *pSong;         // pSong the event was placed from
    uint32_t          timeStamp;      // timestamp of event
    unsigned char           status;         // status of event: 0 - dead, 1 - allocating, 2 - ready
    unsigned char           midiChannel;    // which channel
    unsigned char           command;        // which command
    unsigned char           byte1;          // note, controller
    unsigned char           byte2;          // velocity, lsb/msb
};
typedef struct Q_MIDIEvent Q_MIDIEvent;

typedef void            (*InnerLoop)(GM_Voice *pVoice);
typedef void            (*InnerLoop2)(GM_Voice *pVoice, bool looping);

#ifndef BAE_EQ_BANDS
#define BAE_EQ_BANDS 5
#endif

typedef struct {
    double b0, b1, b2, a1, a2;
    double x1, x2, y1, y2;
} BAEBiquad;

typedef struct {
    BAEBiquad filters[BAE_EQ_BANDS];
} BAEChannelEQ;

typedef struct {
    BAEChannelEQ channels[2];
    float gains[BAE_EQ_BANDS];
    bool enabled;
    uint32_t sampleRate;
} BAEEQState;

// tried to 8 byte align structure (7/17/97)
struct GM_Mixer
{
    TerpMode            interpolationMode;              // output interpolation mode
    Rate                outputRate;                 // output sample rate

    ReverbMode          reverbUnitType;                 // verb mode
    ReverbMode          reverbTypeAllocated;            // verb mode allocated

    unsigned char               sampleFrameSize;                // size in bytes of each sample frame
    unsigned char               sampleExpansion;                // output expansion factor 1, 2, or 4
    int16_t              MasterVolume;
    int16_t              globalVolume;                   // global volume for final mixdown

    int16_t              effectsVolume;                  // volume multiplier of all effects
    int32_t             scaleBackAmount;
    int32_t             outputGainPct;          // user-set output gain percent (100 = normal, >100 = overdrive)
    XFIXED              songNormalizeGain;      // final-mix normalize scale (XFIXED_1 = unity, may boost)
    int16_t              routeBus;

    int16_t              MaxNotes;
    int16_t              mixLevel;
    int16_t              MaxEffects;
    int16_t              maxChunkSize;
    uint32_t              bufferTime;
    uint32_t              lfoBufferTime;

    uint16_t               One_Slice, One_Loop, Two_Loop, Four_Loop;
    uint16_t               Sixteen_Loop;

    bool       /*0*/   generate16output;               // if TRUE, then build 16 bit output
    bool       /*1*/   generateStereoOutput;           // if TRUE, then output stereo data
    bool       /*2*/   insideAudioInterrupt;
    bool       /*3*/   systemPaused;                   // all sound paused and disengaged from hardware

    bool       /*4*/   enableDriftFixer;               // if enabled, this will fix the drift of real time with our synth time.
    bool       /*5*/   sequencerPaused;                // MIDI sequencer paused
    bool       /*6*/   cacheInstruments;               // current not used

    bool       /*7*/   stereoFilter;                   // if TRUE, then filter stereo output
#if BAE_FIX_SPAN_DC
    bool       /*8*/   fixSpanDC;                      // if TRUE, skip DC_feed for STEREO_PAN LFOs
#endif
#if BAE_CLASSIC_CHORUS
    bool       /*9*/   classicChorus;                  // if TRUE, use pre-DLS chorus ordering (reverb before chorus, no chorus in fixed reverb)
#endif
    bool               classicLFO;                     // if TRUE, use BAE 1.x fixed LFO slice clock
#if USE_WEBTV_MODE
    bool               webtvMode;                      // if TRUE, match WebTV (LC2) Beatnik: velocity curve, LFO shapes, default pans, filter glide
#endif
    unsigned char       /*0*/   processExternalMidiQueue;       // counter flag to lock processing of queue. 0 means process
    GM_SampleCacheEntry *sampleCaches[MAX_SAMPLES];     // cache of samples loaded

    // voice allocation, and dry and wet mix buffers
    GM_Voice            NoteEntry[MAX_VOICES];
#if BAE_COMPLETE == TRUE
    int32_t             songBufferDry[(MAX_CHUNK_SIZE+64)*2];   // interleaved samples: left-right
#if REVERB_USED != REVERB_DISABLED
    int32_t             songBufferReverb[MAX_CHUNK_SIZE+64];    // the +64 is for 48k output
    int32_t             songBufferChorus[MAX_CHUNK_SIZE+64];
#endif
#endif
#if USE_SF2_SUPPORT == TRUE
    bool               isSF2;
#endif
    bool               isDLS;
    struct DLS_Synth*  pDLSSynth;

// MIDI Interpreter variables
    GM_Song             *pSongsToPlay[MAX_SONGS];       // number of songs to play at once

// normal inner loop procs
    InnerLoop2          partialBufferProc;
    InnerLoop           fullBufferProc;
    InnerLoop2          partialBufferProc16;
    InnerLoop           fullBufferProc16;

// procs for resonant low-pass filtering
    InnerLoop2          filterPartialBufferProc;
    InnerLoop           filterFullBufferProc;
    InnerLoop2          filterPartialBufferProc16;
    InnerLoop           filterFullBufferProc16;

// external midi control variables
    Q_MIDIEvent         theExternalMidiQueue[MAX_QUEUE_EVENTS];

// lock for queue access
    BAE_Mutex           queueLock;

// pointers for circular event buffer
    Q_MIDIEvent         *pHead;                         // pointer to events to read from queue
    Q_MIDIEvent         *pTail;                         // pointer to events to write to queue
                                                        // always points to the next one to use
    uint32_t              syncCount;                      // in microseconds. Current tick of audio output
    int32_t             syncBufferCount;

    uint32_t              samplesPlayed;                  // number of samples played by device
    uint32_t              samplesWritten;                 // number of samples written to device
    uint32_t              lastSamplePosition;             // last time GM_UpdateSamplesPlayed was called

    uint32_t              timeSliceDifference;            // value in microseconds between calls to
                                                        // HAE_BuildMixerSlice
#if USE_CALLBACKS
    GM_AudioTaskCallbackPtr     pTaskProc;              // callback for audio tasks
    void                        *taskReference;
    GM_AudioOutputCallbackPtr   pOutputProc;            // callback for audio output
#endif
#if REVERB_USED != REVERB_DISABLED
// variables used for "classic" fixed verb
    int32_t             *reverbBuffer;          // this is the master pointer used
                                                // for verb. It is shared between
                                                // different types of verbs, although
                                                // the data maybe different
    uint32_t              reverbBufferSize;       // Set the size of memory allocated here.
                                                // Make sure you set this because it is
                                                // compared and tested against
    int32_t             reverbPtr;              // delay line index into verb buffer
    int32_t             LPfilterL, LPfilterR;   // used for fixed verb
    int32_t             LPfilterLz, LPfilterRz;
#endif
    BAEEQState          eq;

    bool                channelCaptureEnabled;
    char                channelCaptureDir[1024];
    void               *channelCaptureFiles[16];
    int32_t            *channelCaptureBuf[16];
    int32_t             channelCaptureBufSamples;
    int32_t            *channelCaptureSnapshot;
    bool                channelCaptureActive[16];
    int32_t             channelCaptureSliceCount;

#if USE_NEW_EFFECTS
    /* Per-mixer effect state (was process-global). Allocated during reverb setup. */
    struct NewReverbParams  *pNewReverb;
    struct ChorusParams     *pChorus;
#if USE_NEO_EFFECTS == TRUE
    struct NeoReverbParams  *pNeoReverb;
#endif
#endif
#if USE_SF2_SUPPORT == TRUE
    /* Per-mixer FluidLite state (was process-global). */
    void                    *pSF2State;
#endif
};
typedef struct GM_Mixer GM_Mixer;

// support for historical reasons
#define MusicVars   GM_Mixer

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wpragma-pack"
#endif
#include "X_UnpackStructures.h"
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#ifdef __cplusplus
    extern "C" {
#endif

/* Thread-local active mixer. Concurrent mixers each bind this on their thread. */
extern BAE_THREAD_LOCAL GM_Mixer *MusicGlobals;

/* Set the TLS current mixer; returns the previous value (for save/restore). */
struct GM_Mixer *GM_SetCurrentMixer(struct GM_Mixer *mixer);

void PV_FlushChannelCaptureBuffers(GM_Mixer *pMixer);
void PV_FinalizeChannelCaptureFile(int ch);

#if USE_NEW_EFFECTS
/******************************* new reverb stuff *****************************/

#define kCombBufferFrameSize            4096    /* 5000 */
#define kDiffusionBufferFrameSize       4096    /* 4410 */
#define kStereoizerBufferFrameSize      1024    /* 1000 */
#define kEarlyReflectionBufferFrameSize 0x2000  /* 0x1500 */

#define kCombBufferMask                 (kCombBufferFrameSize - 1)
#define kDiffusionBufferMask            (kDiffusionBufferFrameSize - 1)
#define kStereoizerBufferMask           (kStereoizerBufferFrameSize - 1)
#define kEarlyReflectionBufferMask      (kEarlyReflectionBufferFrameSize - 1)


#define kNumberOfCombFilters        6
#define kNumberOfEarlyReflections   7


#define kNumberOfDiffusionStages    3


struct NewReverbParams
{
    bool               mIsInitialized;
    Rate                mSampleRate;
    int32_t             mReverbType;    
    
    /* early reflection params */
    int32_t             *mEarlyReflectionBuffer;
    int32_t             mEarlyReflectionGain[kNumberOfEarlyReflections];
    int                 mReflectionWriteIndex;
    int                 mReflectionReadIndex[kNumberOfEarlyReflections];
    
    
    /* comb filter params */    
    int32_t             *mReverbBuffer[kNumberOfCombFilters];
    
    int                 mReadIndex[kNumberOfCombFilters];
    int                 mWriteIndex[kNumberOfCombFilters];
    
    int32_t                mUnscaledDelayFrames[kNumberOfCombFilters];
    int32_t                mDelayFrames[kNumberOfCombFilters];
    
    int32_t                 mFeedbackList[kNumberOfCombFilters];
    
    int32_t             mRoomSize;
    int32_t             mRoomChoice;
    int32_t             mMaxRegen;      // 0-127
    int32_t             mDiffusedBalance;
    
    /* diffusion params */
    int32_t             *mDiffusionBuffer[kNumberOfDiffusionStages];
    int                 mDiffReadIndex[kNumberOfDiffusionStages];
    int                 mDiffWriteIndex[kNumberOfDiffusionStages];
    
    /* output filter */
    int32_t             mLopassK;
    int32_t             mFilterMemory;
    
    /* stereoizer params */
    int32_t             *mStereoizerBufferL;
    int32_t             *mStereoizerBufferR;
    int                 mStereoReadIndex;
    int                 mStereoWriteIndex;
};

typedef struct NewReverbParams NewReverbParams;

typedef struct NeoReverbParams NeoReverbParams;

/* prototypes */
NewReverbParams*    GetNewReverbParams();
NeoReverbParams*    GetNeoReverbParams();

/* Query whether Neo reverb is currently active or still decaying.
   Returns TRUE if Neo reverb reports activity or non-zero internal state. */
bool   BAENeoReverb_IsActive(void);
bool InitNewReverb();  // returns TRUE if success
void ShutdownNewReverb();
bool CheckReverbType();
void ScaleDelayTimes();
void GenerateDelayTimes();
void GenerateFeedbackValues();
void SetupDiffusion();
void SetupStereoizer();
void SetupEarlyReflections();
void RunNewReverb(int32_t *sourceP, int32_t *destP, int nSampleFrames);
void BAE_ClearNewReverbBuffers(void);
uint32_t GetSamplingRate();
uint32_t GetSR_44100Ratio();
uint32_t Get44100_SRRatio();

#if USE_NEO_EFFECTS == TRUE
// Neo reverb (MT-32 style)
bool       InitNeoReverb(void);
void        ShutdownNeoReverb(void);
bool       CheckNeoReverbType(void);
void        BAE_ClearNeoReverbBuffers(void);
void        RunNeoReverb(int32_t *sourceP, int32_t *destP, int numFrames);
void        CheckMobileReverbType(void);
void        RunMobileReverb(int32_t *sourceP, int32_t *destP, int numFrames);
void        SetNeoReverbMix(int wetLevel);
int         GetNeoReverbMix(void);
void        SetNeoReverbTime(int reverbTime);

// Custom reverb control functions
void        SetNeoCustomReverbCombCount(int combCount);
void        SetNeoCustomReverbCombDelay(int combIndex, int delayMs);
void        SetNeoCustomReverbCombFeedback(int combIndex, int feedback);
void        SetNeoCustomReverbCombGain(int combIndex, int gain);
void        SetNeoCustomReverbLowpass(int lowpass);
int         GetNeoCustomReverbCombCount(void);
int         GetNeoCustomReverbCombDelay(int combIndex);
int         GetNeoCustomReverbCombFeedback(int combIndex);
int         GetNeoCustomReverbCombGain(int combIndex);
// Get Neo reverb preset parameters (for UI customization)
void        GetNeoReverbPresetParams(int reverbType, int *combCount, int *delaysMs, int *feedback, int *gain, int *lowpass, int *mix);
// Custom reverb mode: User-configurable comb filters
#define NEO_CUSTOM_MAX_COMBS    4
#define NEO_CUSTOM_MAX_FEEDBACK 127   // Max feedback value for combs, > 127 causes feedback loop
#define NEO_CUSTOM_MAX_GAIN     255   // Max gain value for combs
#define NEO_CUSTOM_MAX_LOWPASS  127   // values over 127 appear to have no effect
#define NEO_CUSTOM_MAX_DELAY_MS 500
#endif

/******************************* new chorus stuff *****************************/
#define kChorusBufferFrameSize      4410L

struct ChorusParams
{
    bool               mIsInitialized;
    Rate                mSampleRate;
    
    int32_t*                mChorusBufferL;
    int32_t*                mChorusBufferR;

    int                 mWriteIndex;
    int32_t             mReadIndexL;
    int32_t             mReadIndexR;
    
    int                 mSampleFramesDelay;

    int32_t             mRate;
    //float             mDepth;
    int32_t             mPhi;
    
    int32_t             mFeedbackGain;  // between 0-127
};

typedef struct ChorusParams ChorusParams;

/* prototypes */
ChorusParams* GetChorusParams();
void InitChorus();
void ShutdownChorus();
int32_t GetChorusReadIncrement(int32_t readIndex, int32_t writeIndex, int32_t nSampleFrames, int32_t phase);
void SetupChorusDelay();
void RunChorus(int32_t *sourceP, int32_t *destP, int nSampleFrames);


#if 0   // only reverb and chorus are currently activated...

/******************************* delay stuff *****************************/
#define kDelayBufferFrameSize       44100

struct DelayEffect
{
    int32_t*                mDelayBuffer;

    int                 mWriteIndex;
    int                 mReadIndex;
    
    float               mSecondsDelay;
    
    float               mFeedbackValue;
    float               mFeedbackGain;
    
    int32_t             mFilterMemoryL;
    int32_t             mFilterMemoryR;
    int32_t             mLopassK;
};

typedef struct DelayEffect DelayEffect;

/* prototypes */
void Delay_Initialize(DelayEffect *This);
void Delay_Shutdown(DelayEffect *This);
void Delay_Run(DelayEffect *This, int32_t *sourceP);

extern DelayEffect      gDelay;


/******************************* graphic eq stuff *****************************/
#define kNumberOfBands      7

struct GraphicEqParams
{
    /* right and left filter memory */
    int32_t     mHistory1L[kNumberOfBands];
    int32_t     mHistory2L[kNumberOfBands];
    int32_t     mHistory1R[kNumberOfBands];
    int32_t     mHistory2R[kNumberOfBands];
    
    float       mControlList[kNumberOfBands];       /* values between 0.0 and 1.0 */
    float       mGain[kNumberOfBands];
};

typedef struct GraphicEqParams GraphicEqParams;

/* prototypes */
GraphicEqParams* GetGraphicEqParams();
void InitGraphicEq();
void CalculateGraphicEqGains();
void RunGraphicEq(int32_t *sourceP, int nSampleFrames);


/******************************* parametric eq stuff *****************************/

struct ParametricEq
{
    float   mFreqValue;
    float   mQValue;
    float   mGainValue;

    float   mControlList[3];
    
    double  pi;

    float   sweep;
    
    /* filter memory */
    int32_t x1;
    int32_t x2;
    int32_t y1;
    int32_t y2;
    
    /* filter coefficients */
    float   b0;
    float   b1;
    float   b2;
    float   a1;
    float   a2;
};

typedef struct ParametricEq ParametricEq;

/* prototypes */
void    ParametricEq_Initialize(ParametricEq *This);
void    ParametricEq_CalculateParams(ParametricEq *This);
void    ParametricEq_Run(ParametricEq *This, int32_t *buffer);

extern ParametricEq     gParametricEq;

/******************************* resonant filter stuff *****************************/

struct ResonantFilterParams
{
    float   mFrequency;
    float   mResonance;

    float   mControlList[2];
    
    double  pi;

    float   sweep;
    
    /* filter memory */
    int32_t y1;
    int32_t y2;
    
    /* filter coefficients */
    float   c0;
    float   c1;
    float   c2;
};

typedef struct ResonantFilterParams ResonantFilterParams;

/* prototypes */
ResonantFilterParams* GetResonantFilterParams();
void InitResonantFilter();
void CalculateResonantParams(float inFrequency, float inResonance);
void RunResonantFilter(int32_t *buffer, int nSampleFrames);

#endif // 0

#endif // USE_NEW_EFFECTS
/******************************************************************************/





// internal function declarations

void PV_Generate8outputStereo(OUTSAMPLE8 * dest8);
void PV_Generate8outputMono(OUTSAMPLE8 * dest8);
void PV_Generate16outputStereo(OUTSAMPLE16 * dest16);
void PV_Generate16outputMono(OUTSAMPLE16 * dest16);

int32_t PV_DoubleBufferCallbackAndSwap(GM_DoubleBufferCallbackPtr doubleBufferCallback, 
                                        GM_Voice *this_voice);
void PV_CalculateStereoVolume(GM_Voice *this_voice, int32_t *pLeft, int32_t *pRight);
void PV_CalculateMonoVolume(GM_Voice *pVoice, int32_t *pVolume);

void PV_ProcessSampleEvents(void *threadContext);           // process all sample events

#if LOOPS_USED == U3232_LOOPS
void PV_ServeU3232FilterFullBufferNewReverb (GM_Voice *this_voice);
void PV_ServeU3232StereoFilterFullBufferNewReverb (GM_Voice *this_voice);
void PV_ServeU3232FilterFullBufferNewReverb16 (GM_Voice *this_voice);
void PV_ServeU3232StereoFilterFullBufferNewReverb16 (GM_Voice *this_voice);

void PV_ServeU3232FilterFullBuffer (GM_Voice *this_voice);
void PV_ServeU3232StereoFilterFullBuffer (GM_Voice *this_voice);
void PV_ServeU3232FilterFullBuffer16 (GM_Voice *this_voice);
void PV_ServeU3232StereoFilterFullBuffer16 (GM_Voice *this_voice);

void PV_ServeU3232FilterPartialBuffer (GM_Voice *this_voice, bool looping);
void PV_ServeU3232StereoFilterPartialBuffer (GM_Voice *this_voice, bool looping);
void PV_ServeU3232FilterPartialBuffer16 (GM_Voice *this_voice, bool looping);
void PV_ServeU3232StereoFilterPartialBuffer16 (GM_Voice *this_voice, bool looping);

void PV_ServeU3232FilterPartialBufferNewReverb (GM_Voice *this_voice, bool looping);
void PV_ServeU3232FilterPartialBufferNewReverb16 (GM_Voice *this_voice, bool looping);
void PV_ServeU3232StereoFilterPartialBufferNewReverb (GM_Voice *this_voice, bool looping);
void PV_ServeU3232StereoFilterPartialBufferNewReverb16 (GM_Voice *this_voice, bool looping);

void PV_ServeU3232FullBuffer (GM_Voice *this_voice);
void PV_ServeU3232StereoFullBuffer (GM_Voice *this_voice);
void PV_ServeU3232FullBuffer16 (GM_Voice *this_voice);
void PV_ServeU3232StereoFullBuffer16 (GM_Voice *this_voice);

void PV_ServeU3232PartialBuffer (GM_Voice *this_voice, bool looping);
void PV_ServeU3232StereoPartialBuffer (GM_Voice *this_voice, bool looping);
void PV_ServeU3232PartialBuffer16 (GM_Voice *this_voice, bool looping);
void PV_ServeU3232StereoPartialBuffer16 (GM_Voice *this_voice, bool looping);

void PV_ServeU3232FullBufferNewReverb (GM_Voice *this_voice);
void PV_ServeU3232StereoFullBufferNewReverb (GM_Voice *this_voice);
void PV_ServeU3232FullBuffer16NewReverb (GM_Voice *this_voice);
void PV_ServeU3232StereoFullBuffer16NewReverb (GM_Voice *this_voice);

void PV_ServeU3232PartialBufferNewReverb (GM_Voice *this_voice, bool looping);
void PV_ServeU3232StereoPartialBufferNewReverb (GM_Voice *this_voice, bool looping);
void PV_ServeU3232PartialBuffer16NewReverb (GM_Voice *this_voice, bool looping);
void PV_ServeU3232StereoPartialBuffer16NewReverb (GM_Voice *this_voice, bool looping);
#endif

#if LOOPS_USED == FLOAT_LOOPS
void PV_ServeFloatFilterFullBufferNewReverb (GM_Voice *this_voice);
void PV_ServeStereoFloatFilterFullBufferNewReverb (GM_Voice *this_voice);
void PV_ServeFloatFilterFullBufferNewReverb16 (GM_Voice *this_voice);
void PV_ServeStereoFloatFilterFullBufferNewReverb16 (GM_Voice *this_voice);

void PV_ServeFloatFilterFullBuffer (GM_Voice *this_voice);
void PV_ServeStereoFloatFilterFullBuffer (GM_Voice *this_voice);
void PV_ServeFloatFilterFullBuffer16 (GM_Voice *this_voice);
void PV_ServeStereoFloatFilterFullBuffer16 (GM_Voice *this_voice);

void PV_ServeFloatFilterPartialBuffer (GM_Voice *this_voice, bool looping);
void PV_ServeStereoFloatFilterPartialBuffer (GM_Voice *this_voice, bool looping);
void PV_ServeFloatFilterPartialBuffer16 (GM_Voice *this_voice, bool looping);
void PV_ServeStereoFloatFilterPartialBuffer16 (GM_Voice *this_voice, bool looping);

void PV_ServeFloatFilterPartialBufferNewReverb (GM_Voice *this_voice, bool looping);
void PV_ServeFloatFilterPartialBufferNewReverb16 (GM_Voice *this_voice, bool looping);
void PV_ServeStereoFloatFilterPartialBufferNewReverb (GM_Voice *this_voice, bool looping);
void PV_ServeStereoFloatFilterPartialBufferNewReverb16 (GM_Voice *this_voice, bool looping);

void PV_ServeFloatFullBuffer (GM_Voice *this_voice);
void PV_ServeStereoFloatFullBuffer (GM_Voice *this_voice);
void PV_ServeFloatFullBuffer16 (GM_Voice *this_voice);
void PV_ServeStereoFloatFullBuffer16 (GM_Voice *this_voice);

void PV_ServeFloatPartialBuffer (GM_Voice *this_voice, bool looping);
void PV_ServeStereoFloatPartialBuffer (GM_Voice *this_voice, bool looping);
void PV_ServeFloatPartialBuffer16 (GM_Voice *this_voice, bool looping);
void PV_ServeStereoFloatPartialBuffer16 (GM_Voice *this_voice, bool looping);

void PV_ServeFloatFullBufferNewReverb (GM_Voice *this_voice);
void PV_ServeStereoFloatFullBufferNewReverb (GM_Voice *this_voice);
void PV_ServeFloatFullBuffer16NewReverb (GM_Voice *this_voice);
void PV_ServeStereoFloatFullBuffer16NewReverb (GM_Voice *this_voice);

void PV_ServeFloatPartialBufferNewReverb (GM_Voice *this_voice, bool looping);
void PV_ServeStereoFloatPartialBufferNewReverb (GM_Voice *this_voice, bool looping);
void PV_ServeFloatPartialBuffer16NewReverb (GM_Voice *this_voice, bool looping);
void PV_ServeStereoFloatPartialBuffer16NewReverb (GM_Voice *this_voice, bool looping);
#endif

void PV_ServeInterp2FilterFullBufferNewReverb (GM_Voice *this_voice);
void PV_ServeStereoInterp2FilterFullBufferNewReverb (GM_Voice *this_voice);
void PV_ServeInterp2FilterFullBufferNewReverb16 (GM_Voice *this_voice);
void PV_ServeStereoInterp2FilterFullBufferNewReverb16 (GM_Voice *this_voice);

void PV_ServeInterp2FilterFullBuffer (GM_Voice *this_voice);
void PV_ServeStereoInterp2FilterFullBuffer (GM_Voice *this_voice);
void PV_ServeInterp2FilterFullBuffer16 (GM_Voice *this_voice);
void PV_ServeStereoInterp2FilterFullBuffer16 (GM_Voice *this_voice);

void PV_ServeInterp2FilterPartialBuffer (GM_Voice *this_voice, bool looping);
void PV_ServeStereoInterp2FilterPartialBuffer (GM_Voice *this_voice, bool looping);
void PV_ServeInterp2FilterPartialBuffer16 (GM_Voice *this_voice, bool looping);
void PV_ServeStereoInterp2FilterPartialBuffer16 (GM_Voice *this_voice, bool looping);

void PV_ServeInterp2FilterPartialBufferNewReverb (GM_Voice *this_voice, bool looping);
void PV_ServeInterp2FilterPartialBufferNewReverb16 (GM_Voice *this_voice, bool looping);
void PV_ServeStereoInterp2FilterPartialBufferNewReverb (GM_Voice *this_voice, bool looping);
void PV_ServeStereoInterp2FilterPartialBufferNewReverb16 (GM_Voice *this_voice, bool looping);

void PV_ServeInterp2FullBuffer (GM_Voice *this_voice);
void PV_ServeStereoInterp2FullBuffer (GM_Voice *this_voice);
void PV_ServeInterp2FullBuffer16 (GM_Voice *this_voice);
void PV_ServeStereoInterp2FullBuffer16 (GM_Voice *this_voice);

void PV_ServeInterp2PartialBuffer (GM_Voice *this_voice, bool looping);
void PV_ServeStereoInterp2PartialBuffer (GM_Voice *this_voice, bool looping);
void PV_ServeInterp2PartialBuffer16 (GM_Voice *this_voice, bool looping);
void PV_ServeStereoInterp2PartialBuffer16 (GM_Voice *this_voice, bool looping);

void PV_ServeInterp2FullBufferNewReverb (GM_Voice *this_voice);
void PV_ServeStereoInterp2FullBufferNewReverb (GM_Voice *this_voice);
void PV_ServeInterp2FullBuffer16NewReverb (GM_Voice *this_voice);
void PV_ServeStereoInterp2FullBuffer16NewReverb (GM_Voice *this_voice);

void PV_ServeInterp2PartialBufferNewReverb (GM_Voice *this_voice, bool looping);
void PV_ServeStereoInterp2PartialBufferNewReverb (GM_Voice *this_voice, bool looping);
void PV_ServeInterp2PartialBuffer16NewReverb (GM_Voice *this_voice, bool looping);
void PV_ServeStereoInterp2PartialBuffer16NewReverb (GM_Voice *this_voice, bool looping);

void PV_ServeInterp1FullBuffer (GM_Voice *this_voice);
void PV_ServeInterp1PartialBuffer (GM_Voice *this_voice, bool looping);
void PV_ServeStereoInterp1FullBuffer (GM_Voice *this_voice);
void PV_ServeStereoInterp1PartialBuffer (GM_Voice *this_voice, bool looping);

void PV_ServeDropSampleFullBuffer (GM_Voice *this_voice);
void PV_ServeDropSamplePartialBuffer (GM_Voice *this_voice, bool looping);
void PV_ServeDropSampleFullBuffer16 (GM_Voice *this_voice);
void PV_ServeDropSamplePartialBuffer16 (GM_Voice *this_voice, bool looping);
void PV_ServeStereoAmpFullBuffer (GM_Voice *this_voice);
void PV_ServeStereoAmpPartialBuffer (GM_Voice *this_voice, bool looping);


void PV_StartMIDINote(GM_Song *pSong, int16_t the_instrument, 
                        int16_t the_channel, int16_t the_track, int16_t notePitch, int32_t Volume);
void PV_StopMIDINote(GM_Song *pSong, int16_t the_instrument, 
                        int16_t the_channel, int16_t the_track, int16_t notePitch);

/* Patch/bank mapping shared by GenSeq note-on and GenBankBalance MIDI estimate. */
int16_t PV_ConvertPatchBank(GM_Song *pSong, int16_t thePatch, int16_t theChannel);
int16_t PV_DetermineInstrumentToUse(GM_Song *pSong, int16_t midiNote, int16_t MIDIChannel);

// voices modifiers
int16_t SetChannelPitchBend(GM_Song *pSong, int16_t the_channel, unsigned char bendRange, unsigned char bendMSB, unsigned char bendLSB);
void SetChannelVolume(GM_Song *pSong, int16_t the_channel, int16_t newVolume);
int16_t SetChannelStereoPosition(GM_Song *pSong, int16_t the_channel, uint16_t newPosition);
void SetChannelModWheel(GM_Song *pSong, int16_t the_channel, uint16_t value);
void PV_ChangeSustainedNotes(GM_Song *pSong, int16_t the_channel, int16_t data);

void PV_CleanExternalQueue(GM_Mixer *pMixer);

// process 11 ms worth of sample data
void PV_ProcessSampleFrame(void *threadContext, void *destSampleData);
void PV_ProcessSequencerEvents(void *threadContext);

// EQ functions
void PV_UpdateEQCoefficients(GM_Mixer *pMixer);
void PV_ClearEQState(GM_Mixer *pMixer);
void PV_ApplyEQ(GM_Mixer *pMixer);

OPErr PV_ProcessMidiSequencerSlice(void *threadContext, GM_Song *pSong);

// MIDI
void PV_ConfigureInstruments(GM_Song *theSong);
OPErr PV_ConfigureMusic(GM_Song *theSong);
void PV_ResetControlers(GM_Song *pSong, int16_t channel2Reset, bool completeReset);

// Resonant filter cutoff glide. WebTV Mode uses LC2's rate: >>3 per 4-sample
// block (16406 0x804aa854), which is about >>5 when applied every sample.
#if USE_WEBTV_MODE
#define BAE_FILTER_GLIDE_BLOCK(n)   ((MusicGlobals && MusicGlobals->webtvMode) ? 3 : (n))
#define BAE_FILTER_GLIDE_SAMPLE(n)  ((MusicGlobals && MusicGlobals->webtvMode) ? 5 : (n))
#else
#define BAE_FILTER_GLIDE_BLOCK(n)   (n)
#define BAE_FILTER_GLIDE_SAMPLE(n)  (n)
#endif

// channelStereoPosition holding a default pan (low byte = signed log pan).
enum { kChannelPanDefault = 0x100 };
int16_t PV_DefaultChannelStereoPosition(int32_t channel);

// GenPatch.c

// given an instrument ID and a bank token or a block of data, load an instrument
// and create a GM_Instrument structure. This will also load any samples required to play
GM_Instrument * PV_GetInstrument(GM_Mixer *pMixer, GM_Song *pSong, 
                                    XLongResourceID theID,
                                     XBankToken bankToken,
                                     void *theExternalX,
                                     int32_t patchSize,
                                     OPErr *pErr);

// unload an instrument and remove all of its memory and optionally the samples
OPErr PV_UnloadInstrumentData(GM_Instrument *theI, GM_Mixer *pMixer, bool freeSamples);

uint32_t PV_ScaleVolumeFromChannelAndSong(GM_Song *pSong, int16_t channel, uint32_t volume);
#if USE_CALLBACKS
void PV_DoCallBack(GM_Voice *this_one);
#endif
void PV_CleanNoteEntry(GM_Voice * the_entry);
void PV_CalcScaleBack(void);


// given a voice structure, calculate what voice this is
uint16_t PV_GetVoiceNumberFromVoice(GM_Voice *pVoice);

XFIXED PV_GetWavePitch(XFIXED notePitch);
#if LOOPS_USED == FLOAT_LOOPS
UFLOAT PV_GetWavePitchFloat(XFIXED notePitch);
#define FRAC(num, num_whole)        ((num) - (num_whole))
#endif
#if LOOPS_USED == U3232_LOOPS
UFLOAT PV_GetWavePitchFloat(XFIXED notePitch);
U3232 PV_GetWavePitchU3232(XFIXED notePitch);
#endif

// get voice sample position from active voice
uint32_t PV_GetPositionFromVoice(GM_Voice *pVoice);
void PV_SetPositionFromVoice(GM_Voice *pVoice, uint32_t pos);

// GenModFiles.c
void PV_WriteModOutput(Rate q, bool stereo);

// GenAudioStreams.c
void PV_ServeStreamFades(void);

// GenSeq.c
void PV_FreePatchInfo(GM_Song *pSong);
void PV_InsertBankSelect(GM_Song *pSong, int16_t channel, int16_t currentTrack);
// process end song callback
void PV_CallSongCallback(void *threadContext, GM_Song *theSong, bool clearCallback);

// GenSynth.c
int32_t PV_ModifyVelocityFromCurve(GM_Song *pSong, int32_t volume);

// GenSample.c
GM_Voice * PV_GetVoiceFromSoundReference(VOICE_REFERENCE reference);

int GetNeoCustomReverbLowpass();

// GenSetup.c
#if (X_PLATFORM == X_WIN95) && (USE_KAT)
bool PV_IntelKatActive(void);
#endif

#ifdef __cplusplus
    }
#endif

#endif  /* G_PRIVATE    */ 
