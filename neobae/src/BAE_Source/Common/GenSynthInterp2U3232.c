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
** "GenSynthInterp2U3232.c"
**
**  Generalized Music Synthesis package. Part of SoundMusicSys.
**  Confidential-- Internal use only
**
**  � Copyright 1993-2000 Beatnik, Inc, All Rights Reserved.
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
** Modification History:
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
**  3/11/99     MOE: created file from GenSynthInterp2Float.c
**  10/19/99    MSD: switched to REVERB_USED and LOOPS_USED
**  2/4/2000    Changed copyright. We're Y2K compliant!
**  1/27/2002   sh Fixed warnings.
*/
/*****************************************************************************/

#include "GenSnd.h"
#include "GenPriv.h"
#include "X_Assert.h"
#include <stdint.h>

#if LOOPS_USED == U3232_LOOPS

// Cubic Hermite (Catmull-Rom) interpolation for advanced interpolation mode.
// frac is U3232 fractional part (full 32-bit), representing position between s1 and s2.
// Returns the interpolated sample value with smooth C1-continuous curve.
static inline int32_t PV_CubicHermiteInterpU3232(int32_t s0, int32_t s1, int32_t s2, int32_t s3, U32 frac)
{
    // Use top 15 bits of fraction for interpolation (matching U3232 precision)
    int32_t t = (int32_t)(frac >> 17);  // 0..32767
    // Catmull-Rom via Horner's method, scaled by 2 to avoid fractions:
    //   A = -s0 + 3*s1 - 3*s2 + s3
    //   B = 2*s0 - 5*s1 + 4*s2 - s3
    //   C = s2 - s0
    //   result = ((A*t + B)*t + C)*t / 2 + s1   (t in 0..1 mapped to 0..32767)
    int32_t A = -s0 + 3*s1 - 3*s2 + s3;
    int32_t B = 2*s0 - 5*s1 + 4*s2 - s3;
    int32_t C = s2 - s0;
    int32_t r;
    r = (int32_t)(((int64_t)A * t) >> 15) + B;
    r = (int32_t)(((int64_t)r * t) >> 15) + C;
    r = (int32_t)(((int64_t)r * t) >> 16);  // >> 15 for t scale, +1 for the /2
    return s1 + r;
}

#include "GenInterpSincU3232.inc"

#define PV_HQ16_AUTO(s0, s1, s2, s3, frac) \
    (PV_U3232UseSincInterp() \
         ? PV_SincInterp16Auto(this_voice, source, (int32_t)cur_wave_i, (frac), \
                               (this_voice->NoteLoopEnd > this_voice->NoteLoopPtr)) \
         : PV_CubicHermiteInterpU3232((s0), (s1), (s2), (s3), (frac)))
#define PV_HQ16_CH(s0, s1, s2, s3, frac, off) \
    (PV_U3232UseSincInterp() \
         ? PV_SincInterp16Stride(this_voice, source, (int32_t)cur_wave_i, (frac), \
                                 (this_voice->NoteLoopEnd > this_voice->NoteLoopPtr), 2, (off)) \
         : PV_CubicHermiteInterpU3232((s0), (s1), (s2), (s3), (frac)))

// Fetch a 16-bit sample with loop-wrapping for cubic Hermite boundary cases.
static inline int32_t PV_LoopWrapSample16U3232(int16_t *source, int32_t idx, int32_t loopStart, int32_t loopEnd)
{
    int32_t loopLen = loopEnd - loopStart;
    if (loopLen <= 0)
        return (int32_t)source[loopStart];
    if (idx < loopStart)
    {
        int32_t d = (loopStart - idx) % loopLen;
        idx = (d == 0) ? loopStart : (loopEnd - d);
    }
    else if (idx >= loopEnd)
    {
        idx = loopStart + ((idx - loopStart) % loopLen);
    }
    return (int32_t)source[idx];
}

void PV_ServeU3232FullBuffer (GM_Voice *this_voice)
{
    register int32_t          *dest;
    register int32_t      a, inner;
    register unsigned char          *source, *calculated_source;
    register int32_t          b, c;
    register U32            cur_wave_i, cur_wave_f;
    U3232                   wave_increment;
    register int32_t          amplitude, amplitudeAdjust;

#if REVERB_USED == VARIABLE_REVERB
    if (this_voice->reverbLevel || this_voice->chorusLevel)
    {
        PV_ServeU3232FullBufferNewReverb (this_voice); 
        return;
    }
#endif
    amplitude = this_voice->lastAmplitudeL;
    amplitudeAdjust = (this_voice->NoteVolume * this_voice->NoteVolumeEnvelope) >> VOLUME_PRECISION_SCALAR;
    amplitudeAdjust = (amplitudeAdjust - amplitude) / MusicGlobals->Four_Loop;
    dest = &MusicGlobals->songBufferDry[0];
    source = this_voice->NotePtr;
    cur_wave_i = this_voice->samplePosition.i;
    cur_wave_f = this_voice->samplePosition.f;

    wave_increment = PV_GetWavePitchU3232(this_voice->NotePitch);

    {
        if (this_voice->channels == 1)
        {
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                b = source[cur_wave_i];
                c = source[cur_wave_i+1];
                dest[0] += ((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80) * amplitude;
                ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                b = source[cur_wave_i];
                c = source[cur_wave_i+1];
                dest[1] += ((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80) * amplitude;
                ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                b = source[cur_wave_i];
                c = source[cur_wave_i+1];
                dest[2] += ((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80) * amplitude;
                ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                b = source[cur_wave_i];
                c = source[cur_wave_i+1];
                dest[3] += ((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80) * amplitude;
                ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                dest += 4;
                amplitude += amplitudeAdjust;
            }
        }
        else
        {   // stereo 8 bit instrument
            for (a = MusicGlobals->Sixteen_Loop; a > 0; --a)
            {
                for (inner = 0; inner < 16; inner++)
                {
                    calculated_source = source + cur_wave_i * 2;
                    b = calculated_source[0] + calculated_source[1];    // average left & right channels
                    c = calculated_source[2] + calculated_source[3];
                    *dest += (((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x100) * amplitude) >> 1;
                    dest++;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                }
                amplitude += amplitudeAdjust;
            }
        }
    }
    this_voice->samplePosition.i = cur_wave_i;
    this_voice->samplePosition.f = cur_wave_f;
    this_voice->lastAmplitudeL = amplitude;
}

void PV_ServeU3232PartialBuffer (GM_Voice *this_voice, bool looping)
{
    register int32_t          *dest;
    register int32_t      a, inner;
    register unsigned char          *source, *calculated_source;
    register int32_t          b, c;
    register U32            cur_wave_i, cur_wave_f;
    register U32            end_wave, wave_adjust = 0;
    U3232                   wave_increment;
    register int32_t          amplitude, amplitudeAdjust;

#if REVERB_USED == VARIABLE_REVERB
    if (this_voice->reverbLevel || this_voice->chorusLevel)
    {
        PV_ServeU3232PartialBufferNewReverb (this_voice, looping);
        return;
    }
#endif
    amplitude = this_voice->lastAmplitudeL;
    amplitudeAdjust = (this_voice->NoteVolume * this_voice->NoteVolumeEnvelope) >> VOLUME_PRECISION_SCALAR;
    amplitudeAdjust = (amplitudeAdjust - amplitude) / MusicGlobals->Four_Loop;
    dest = &MusicGlobals->songBufferDry[0];
    source = this_voice->NotePtr;
    cur_wave_i = this_voice->samplePosition.i;
    cur_wave_f = this_voice->samplePosition.f;

    wave_increment = PV_GetWavePitchU3232(this_voice->NotePitch);

    if (looping)
    {
        wave_adjust = this_voice->NoteLoopEnd - this_voice->NoteLoopPtr;
        end_wave = this_voice->NoteLoopEnd - this_voice->NotePtr;
    }
    else
    {
        end_wave = this_voice->NotePtrEnd - this_voice->NotePtr - 1;
    }

    {
        if (this_voice->channels == 1)
        {
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                for (inner = 0; inner < 4; inner++)
                {
                    THE_CHECK_U3232(unsigned char *);
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    *dest += ((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80) * amplitude;
                    dest++;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                }
                amplitude += amplitudeAdjust;
            }
        }
        else
        {   // stereo 8 bit instrument
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                for (inner = 0; inner < 4; inner++)
                {
                    THE_CHECK_U3232(unsigned char *);
                    calculated_source = source + cur_wave_i * 2;
                    b = calculated_source[0] + calculated_source[1];
                    c = calculated_source[2] + calculated_source[3];
                    *dest += (((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x100) * amplitude) >> 1;
                    dest++;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                }
                amplitude += amplitudeAdjust;
            }
        }
    }

    this_voice->samplePosition.i = cur_wave_i;
    this_voice->samplePosition.f = cur_wave_f;
    this_voice->lastAmplitudeL = amplitude;
FINISH:
    return;
}

void PV_ServeU3232StereoFullBuffer(GM_Voice *this_voice)
{
    register int32_t          *destL;
    register int32_t      a, inner;
    register unsigned char          *source, *calculated_source;
    register int32_t          b, c;
    register U32            cur_wave_i, cur_wave_f;
    U3232                   wave_increment;
    register int32_t          sample;
    int32_t                   ampValueL, ampValueR;
    register int32_t          amplitudeL;
    register int32_t          amplitudeR;
    register int32_t          amplitudeLincrement;
    register int32_t          amplitudeRincrement;

#if REVERB_USED == VARIABLE_REVERB
    if (this_voice->reverbLevel || this_voice->chorusLevel)
    {
        PV_ServeU3232StereoFullBufferNewReverb (this_voice);
        return;
    }
#endif
    PV_CalculateStereoVolume(this_voice, &ampValueL, &ampValueR);
    amplitudeL = this_voice->lastAmplitudeL;
    amplitudeR = this_voice->lastAmplitudeR;
    amplitudeLincrement = (ampValueL - amplitudeL) / (MusicGlobals->Four_Loop);
    amplitudeRincrement = (ampValueR - amplitudeR) / (MusicGlobals->Four_Loop);

    destL = &MusicGlobals->songBufferDry[0];
    source = this_voice->NotePtr;
    cur_wave_i = this_voice->samplePosition.i;
    cur_wave_f = this_voice->samplePosition.f;

    wave_increment = PV_GetWavePitchU3232(this_voice->NotePitch);

    {
        if (this_voice->channels == 1)
        {   // mono instrument
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                b = source[cur_wave_i];
                c = source[cur_wave_i+1];
                sample = (((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80;
                destL[0] += sample * amplitudeL;
                destL[1] += sample * amplitudeR;
                ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                b = source[cur_wave_i];
                c = source[cur_wave_i+1];
                sample = (((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80;
                destL[2] += sample * amplitudeL;
                destL[3] += sample * amplitudeR;
                ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                b = source[cur_wave_i];
                c = source[cur_wave_i+1];
                sample = (((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80;
                destL[4] += sample * amplitudeL;
                destL[5] += sample * amplitudeR;
                ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                b = source[cur_wave_i];
                c = source[cur_wave_i+1];
                sample = (((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80;
                destL[6] += sample * amplitudeL;
                destL[7] += sample * amplitudeR;
                ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                destL += 8;
                amplitudeL += amplitudeLincrement;
                amplitudeR += amplitudeRincrement;
            }
        }
        else
        {   // stereo 8 bit instrument
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                for (inner = 0; inner < 4; inner++)
                {
                    calculated_source = source + cur_wave_i * 2;
                    b = calculated_source[0];
                    c = calculated_source[2];
                    destL[0] += ((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80) * amplitudeL;
                    b = calculated_source[1];
                    c = calculated_source[3];
                    destL[1] += ((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80) * amplitudeR;
                    destL += 2;
            
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                }
                amplitudeL += amplitudeLincrement;
                amplitudeR += amplitudeRincrement;
            }
        }
    }
    this_voice->lastAmplitudeL = amplitudeL;
    this_voice->lastAmplitudeR = amplitudeR;
    this_voice->samplePosition.i = cur_wave_i;
    this_voice->samplePosition.f = cur_wave_f;
}

void PV_ServeU3232StereoPartialBuffer (GM_Voice *this_voice, bool looping)
{
    register int32_t          *destL;
    register int32_t      a, inner;
    register unsigned char          *source, *calculated_source;
    register int32_t          b, c, sample;
    register U32            cur_wave_i, cur_wave_f;
    register U32            end_wave, wave_adjust = 0;
    U3232                   wave_increment;
    int32_t                   ampValueL, ampValueR;
    register int32_t          amplitudeL;
    register int32_t          amplitudeR;
    register int32_t          amplitudeLincrement, amplitudeRincrement;

#if REVERB_USED == VARIABLE_REVERB
    if (this_voice->reverbLevel || this_voice->chorusLevel)
    {
        PV_ServeU3232StereoPartialBufferNewReverb (this_voice, looping); 
        return;
    }
#endif
    PV_CalculateStereoVolume(this_voice, &ampValueL, &ampValueR);
    amplitudeL = this_voice->lastAmplitudeL;
    amplitudeR = this_voice->lastAmplitudeR;
    amplitudeLincrement = (ampValueL - amplitudeL) / (MusicGlobals->Four_Loop);
    amplitudeRincrement = (ampValueR - amplitudeR) / (MusicGlobals->Four_Loop);

    destL = &MusicGlobals->songBufferDry[0];
    source = this_voice->NotePtr;
    cur_wave_i = this_voice->samplePosition.i;
    cur_wave_f = this_voice->samplePosition.f;

    wave_increment = PV_GetWavePitchU3232(this_voice->NotePitch);

    if (looping)
    {
        wave_adjust = this_voice->NoteLoopEnd - this_voice->NoteLoopPtr;
        end_wave = this_voice->NoteLoopEnd - this_voice->NotePtr;
    }
    else
    {
        end_wave = this_voice->NotePtrEnd - this_voice->NotePtr - 1;
    }

    {
        if (this_voice->channels == 1)
        {   // mono instrument
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
#if 1   //MOE'S OBSESSIVE FOLLY
                for (inner = 0; inner < 4; inner++)
                {
                    THE_CHECK_U3232(unsigned char *);
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80;
                    destL[0] += sample * amplitudeL;
                    destL[1] += sample * amplitudeR;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    destL += 2;
                }
#else
                THE_CHECK_U3232(unsigned char *);
                b = source[cur_wave_i];
                c = source[cur_wave_i+1];
                sample = (((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80;
                destL[0] += sample * amplitudeL;
                destL[1] += sample * amplitudeR;
                ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                
                THE_CHECK_U3232(unsigned char *);
                b = source[cur_wave_i];
                c = source[cur_wave_i+1];
                sample = (((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80;
                destL[2] += sample * amplitudeL;
                destL[3] += sample * amplitudeR;
                ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                THE_CHECK_U3232(unsigned char *);
                b = source[cur_wave_i];
                c = source[cur_wave_i+1];
                sample = (((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80;
                destL[4] += sample * amplitudeL;
                destL[5] += sample * amplitudeR;
                ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                THE_CHECK_U3232(unsigned char *);
                b = source[cur_wave_i];
                c = source[cur_wave_i+1];
                sample = (((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80;
                destL[6] += sample * amplitudeL;
                destL[7] += sample * amplitudeR;
                ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                destL += 8;
#endif
                amplitudeL += amplitudeLincrement;
                amplitudeR += amplitudeRincrement;
            }
        }
        else
        {   // Stereo 8 bit instrument
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                for (inner = 0; inner < 4; inner++)
                {
                    THE_CHECK_U3232(unsigned char *);
                    calculated_source = source + cur_wave_i * 2;
                    b = calculated_source[0];
                    c = calculated_source[2];
                    destL[0] += ((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80) * amplitudeL;
                    b = calculated_source[1];
                    c = calculated_source[3];
                    destL[1] += ((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80) * amplitudeR;
                    destL += 2;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                }
                amplitudeL += amplitudeLincrement;
                amplitudeR += amplitudeRincrement;
            }
        }
    }
    this_voice->samplePosition.i = cur_wave_i;
    this_voice->samplePosition.f = cur_wave_f;
    this_voice->lastAmplitudeL = amplitudeL;
    this_voice->lastAmplitudeR = amplitudeR;
FINISH:
    return;
}

// 16 bit cases
void PV_ServeU3232FullBuffer16 (GM_Voice *this_voice)
{
    register int32_t          *dest;
    register int32_t      a, inner;
    register int16_t          *source, *calculated_source;
    register int32_t          b, c, sample;
    register U32            cur_wave_i, cur_wave_f;
    U3232                   wave_increment;
    register int32_t          amplitude, amplitudeAdjust;
    int32_t                   totalFrames;

#if REVERB_USED == VARIABLE_REVERB
    if (this_voice->reverbLevel || this_voice->chorusLevel)
    {
        PV_ServeU3232FullBuffer16NewReverb (this_voice); 
        return;
    }
#endif
    amplitude = this_voice->lastAmplitudeL;
    //debug_message("f0, amp = %ld n = %ld nve = %ld\n", (int32_t)amplitude, (int32_t)this_voice->NoteVolume,
    //                                              (int32_t)this_voice->NoteVolumeEnvelope);
    amplitudeAdjust = (this_voice->NoteVolume * this_voice->NoteVolumeEnvelope) >> VOLUME_PRECISION_SCALAR;
    /* Ramp in full precision (lastAmplitude is the accumulator) and shift only for the
       multiply. Shifting the step itself truncated it to 0 below Four_Loop<<4, so quiet
       voices stuck at a fixed level or dropped out. */
    amplitudeAdjust = (amplitudeAdjust - amplitude) / MusicGlobals->Four_Loop;
    amplitude = amplitude >> 4;
    //debug_message("f1, amp = %ld aa = %ld\n", (int32_t)amplitude, (int32_t)amplitudeAdjust);

    dest = &MusicGlobals->songBufferDry[0];
    source = (int16_t *) this_voice->NotePtr;
    cur_wave_i = this_voice->samplePosition.i;
    cur_wave_f = this_voice->samplePosition.f;
    totalFrames = (int32_t)(this_voice->NotePtrEnd - this_voice->NotePtr);

    wave_increment = PV_GetWavePitchU3232(this_voice->NotePitch);

    {
        if (this_voice->channels == 1)
        {
            if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
            {
                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    int32_t ii;
                    for (ii = 0; ii < 4; ii++)
                    {
                        int32_t pos = (int32_t)cur_wave_i;
                        int32_t pos1 = (pos + 1 < totalFrames) ? (pos + 1) : pos;
                        int32_t pos2 = (pos + 2 < totalFrames) ? (pos + 2) : pos1;
                        int32_t s0 = source[(pos > 0) ? pos - 1 : 0];
                        int32_t s1 = source[pos];
                        int32_t s2 = source[pos1];
                        int32_t s3 = source[pos2];
                        dest[ii] += (PV_HQ16_AUTO(s0, s1, s2, s3, cur_wave_f) * amplitude) >> 4;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    }
                    dest += 4;
                    this_voice->lastAmplitudeL += amplitudeAdjust; amplitude = this_voice->lastAmplitudeL >> 4;
                }
            }
            else
            {
                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    dest[0] += (((((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b) * amplitude) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    dest[1] += (((((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b) * amplitude) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    dest[2] += (((((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b) * amplitude) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    dest[3] += (((((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b) * amplitude) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    dest += 4;
                    this_voice->lastAmplitudeL += amplitudeAdjust; amplitude = this_voice->lastAmplitudeL >> 4;
                }
            }
        }
        else
        {   // stereo 16 bit instrument
            if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
            {
                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    for (inner = 0; inner < 4; inner++)
                    {
                        int32_t pos = (int32_t)cur_wave_i;
                        int32_t prev_pos = (pos > 0) ? pos - 1 : 0;
                        int32_t pos1 = (pos + 1 < totalFrames) ? (pos + 1) : pos;
                        int32_t pos2 = (pos + 2 < totalFrames) ? (pos + 2) : pos1;
                        int32_t s0 = source[prev_pos*2] + source[prev_pos*2 + 1];
                        int32_t s1 = source[pos*2] + source[pos*2 + 1];
                        int32_t s2 = source[pos1*2] + source[pos1*2 + 1];
                        int32_t s3 = source[pos2*2] + source[pos2*2 + 1];
                        sample = PV_HQ16_AUTO(s0, s1, s2, s3, cur_wave_f);
                        *dest += (sample * amplitude) >> 5;
                        dest++;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    }
                    this_voice->lastAmplitudeL += amplitudeAdjust; amplitude = this_voice->lastAmplitudeL >> 4;
                }
            }
            else
            {
                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    for (inner = 0; inner < 4; inner++)
                    {
                        calculated_source = source + cur_wave_i * 2;
                        b = calculated_source[0] + calculated_source[1];
                        c = calculated_source[2] + calculated_source[3];
                        sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                        *dest += (sample  * amplitude) >> 5;    // divide extra for summed stereo channels
                        dest++;

                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    }
                    this_voice->lastAmplitudeL += amplitudeAdjust; amplitude = this_voice->lastAmplitudeL >> 4;
                }
            }
        }
    }
    this_voice->samplePosition.i = cur_wave_i;
    this_voice->samplePosition.f = cur_wave_f;
    /* lastAmplitudeL holds the full-precision ramp end. */
}

void PV_ServeU3232PartialBuffer16 (GM_Voice *this_voice, bool looping)
{
    register int32_t          *dest;
    register int32_t      a, inner;
    register int16_t          *source, *calculated_source;
    register int32_t          b, c, sample;
    register U32            cur_wave_i, cur_wave_f;
    register U32            end_wave, wave_adjust = 0;
    U3232                   wave_increment;
    register int32_t          amplitude, amplitudeAdjust;

#if REVERB_USED == VARIABLE_REVERB
    if (this_voice->reverbLevel || this_voice->chorusLevel)
    {
        PV_ServeU3232PartialBuffer16NewReverb (this_voice, looping); 
        return;
    }
#endif
    amplitude = this_voice->lastAmplitudeL;
    amplitudeAdjust = (this_voice->NoteVolume * this_voice->NoteVolumeEnvelope) >> VOLUME_PRECISION_SCALAR;
    /* Ramp in full precision (lastAmplitude is the accumulator) and shift only for the
       multiply. Shifting the step itself truncated it to 0 below Four_Loop<<4, so quiet
       voices stuck at a fixed level or dropped out. */
    amplitudeAdjust = (amplitudeAdjust - amplitude) / MusicGlobals->Four_Loop;
    amplitude = amplitude >> 4;
    //debug_message("p,amp = %ld\n", (int32_t)amplitude);
    dest = &MusicGlobals->songBufferDry[0];
    cur_wave_i = this_voice->samplePosition.i;
    cur_wave_f = this_voice->samplePosition.f;
    source = (int16_t *) this_voice->NotePtr;

    wave_increment = PV_GetWavePitchU3232(this_voice->NotePitch);

    if (looping)
    {
        wave_adjust = this_voice->NoteLoopEnd - this_voice->NoteLoopPtr;
        end_wave = this_voice->NoteLoopEnd - this_voice->NotePtr;
    }
    else
    {
        end_wave = this_voice->NotePtrEnd - this_voice->NotePtr - 1;
    }

    {
        if (this_voice->channels == 1)
        {
            if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
            {
                // Cubic Hermite with loop-aware boundary handling
                int32_t loopStartIdx = (int32_t)(this_voice->NoteLoopPtr - this_voice->NotePtr);
                int32_t loopEndIdx = (int32_t)(this_voice->NoteLoopEnd - this_voice->NotePtr);
                int32_t totalFrames = (int32_t)(this_voice->NotePtrEnd - this_voice->NotePtr);

                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    for (inner = 0; inner < 4; inner++)
                    {
                        THE_CHECK_U3232(int16_t *);
                        int32_t pos = (int32_t)cur_wave_i;
                        int32_t s1 = source[pos];
                        int32_t s0, s2, s3;
                        if (looping)
                        {
                            s0 = PV_LoopWrapSample16U3232(source, pos - 1, loopStartIdx, loopEndIdx);
                            s2 = PV_LoopWrapSample16U3232(source, pos + 1, loopStartIdx, loopEndIdx);
                            s3 = PV_LoopWrapSample16U3232(source, pos + 2, loopStartIdx, loopEndIdx);
                        }
                        else
                        {
                            s0 = source[(pos > 0) ? pos - 1 : 0];
                            s2 = source[(pos + 1 < totalFrames) ? pos + 1 : pos];
                            s3 = source[(pos + 2 < totalFrames) ? pos + 2 : (pos + 1 < totalFrames) ? pos + 1 : pos];
                        }
                        dest[0] += (PV_HQ16_AUTO(s0, s1, s2, s3, cur_wave_f) * amplitude) >> 4;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                        dest++;
                    }
                    this_voice->lastAmplitudeL += amplitudeAdjust; amplitude = this_voice->lastAmplitudeL >> 4;
                }
            }
            else
            {
                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
#if 1   //MOE'S OBSESSIVE FOLLY
                    for (inner = 0; inner < 4; inner++)
                    {
                        THE_CHECK_U3232(int16_t *);
                        b = source[cur_wave_i];
                        c = source[cur_wave_i+1];
                        dest[0] += (((((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b) * amplitude) >> 4;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                        dest++;
                    }
#else
                    THE_CHECK_U3232(int16_t *);
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    dest[0] += (((((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b) * amplitude) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    THE_CHECK_U3232(int16_t *);
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    dest[1] += (((((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b) * amplitude) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    THE_CHECK_U3232(int16_t *);
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    dest[2] += (((((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b) * amplitude) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    THE_CHECK_U3232(int16_t *);
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    dest[3] += (((((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b) * amplitude) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    dest += 4;
#endif
                    this_voice->lastAmplitudeL += amplitudeAdjust; amplitude = this_voice->lastAmplitudeL >> 4;
                }
            }
        }
        else
        {
            if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
            {
                // Cubic Hermite for stereo with loop-aware boundary handling
                int32_t loopStartIdx = (int32_t)(this_voice->NoteLoopPtr - this_voice->NotePtr);
                int32_t loopEndIdx = (int32_t)(this_voice->NoteLoopEnd - this_voice->NotePtr);
                int32_t totalFrames = (int32_t)(this_voice->NotePtrEnd - this_voice->NotePtr);

                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    for (inner = 0; inner < 4; inner++)
                    {
                        THE_CHECK_U3232(int16_t *);
                        int32_t pos = (int32_t)cur_wave_i;
                        int32_t idx0, idx2, idx3;
                        if (looping)
                        {
                            int32_t loopLen = loopEndIdx - loopStartIdx;
                            idx0 = pos - 1;
                            if (idx0 < loopStartIdx) idx0 += loopLen;
                            idx2 = pos + 1;
                            if (idx2 >= loopEndIdx) idx2 -= loopLen;
                            idx3 = pos + 2;
                            if (idx3 >= loopEndIdx) idx3 -= loopLen;
                        }
                        else
                        {
                            idx0 = (pos > 0) ? pos - 1 : 0;
                            idx2 = (pos + 1 < totalFrames) ? (pos + 1) : pos;
                            idx3 = (pos + 2 < totalFrames) ? (pos + 2) : idx2;
                        }
                        int32_t s0 = source[idx0*2] + source[idx0*2 + 1];
                        int32_t s1 = source[pos*2] + source[pos*2 + 1];
                        int32_t s2 = source[idx2*2] + source[idx2*2 + 1];
                        int32_t s3 = source[idx3*2] + source[idx3*2 + 1];
                        sample = PV_HQ16_AUTO(s0, s1, s2, s3, cur_wave_f);
                        *dest += ((sample >> 1) * amplitude) >> 5;
                        dest++;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    }
                    this_voice->lastAmplitudeL += amplitudeAdjust; amplitude = this_voice->lastAmplitudeL >> 4;
                }
            }
            else
            {
                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    for (inner = 0; inner < 4; inner++)
                    {
                        THE_CHECK_U3232(int16_t *);
                        calculated_source = source + cur_wave_i * 2;
                        b = calculated_source[0] + calculated_source[1];
                        c = calculated_source[2] + calculated_source[3];
                        sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                        *dest += ((sample >> 1) * amplitude) >> 5;
                        dest++;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    }
                    this_voice->lastAmplitudeL += amplitudeAdjust; amplitude = this_voice->lastAmplitudeL >> 4;
                }
            }
        }
    }
    this_voice->samplePosition.i = cur_wave_i;
    this_voice->samplePosition.f = cur_wave_f;
    /* lastAmplitudeL holds the full-precision ramp end. */
FINISH:
    return;
}

void PV_ServeU3232StereoFullBuffer16 (GM_Voice *this_voice)
{
    register int32_t          *destL;
    register int32_t      a, inner;
    register int16_t          *source, *calculated_source;
    register int32_t          b, c;
    register U32            cur_wave_i, cur_wave_f;
    U3232                   wave_increment;
    register int32_t          sample;
    int32_t                   ampValueL, ampValueR;
    register int32_t          amplitudeL;
    register int32_t          amplitudeR;
    register int32_t          amplitudeLincrement;
    register int32_t          amplitudeRincrement;
    int32_t                   totalFrames;

#if REVERB_USED == VARIABLE_REVERB
    if (this_voice->reverbLevel || this_voice->chorusLevel)
    { 
        PV_ServeU3232StereoFullBuffer16NewReverb (this_voice);
        return;
    }
#endif
    PV_CalculateStereoVolume(this_voice, &ampValueL, &ampValueR);
    amplitudeL = this_voice->lastAmplitudeL;
    amplitudeR = this_voice->lastAmplitudeR;
    /* Ramp in full precision (lastAmplitude is the accumulator) and shift only for the
       multiply. Shifting the step itself truncated it to 0 below Four_Loop<<4, so quiet
       voices stuck at a fixed level or dropped out. */
    amplitudeLincrement = (ampValueL - amplitudeL) / (MusicGlobals->Four_Loop);
    amplitudeRincrement = (ampValueR - amplitudeR) / (MusicGlobals->Four_Loop);

    amplitudeL = amplitudeL >> 4;
    amplitudeR = amplitudeR >> 4;

    destL = &MusicGlobals->songBufferDry[0];
    cur_wave_i = this_voice->samplePosition.i;
    cur_wave_f = this_voice->samplePosition.f;

    source = (int16_t *) this_voice->NotePtr;
    totalFrames = (int32_t)(this_voice->NotePtrEnd - this_voice->NotePtr);

    wave_increment = PV_GetWavePitchU3232(this_voice->NotePitch);
    {
        if (this_voice->channels == 1)
        {   // mono instrument
            if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
            {
                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    for (inner = 0; inner < 4; inner++)
                    {
                        int32_t pos = (int32_t)cur_wave_i;
                        int32_t pos1 = (pos + 1 < totalFrames) ? (pos + 1) : pos;
                        int32_t pos2 = (pos + 2 < totalFrames) ? (pos + 2) : pos1;
                        int32_t s0 = source[(pos > 0) ? pos - 1 : 0];
                        int32_t s1 = source[pos];
                        int32_t s2 = source[pos1];
                        int32_t s3 = source[pos2];
                        sample = PV_HQ16_AUTO(s0, s1, s2, s3, cur_wave_f);
                        destL[0] += (sample * amplitudeL) >> 4;
                        destL[1] += (sample * amplitudeR) >> 4;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                        destL += 2;
                    }
                    this_voice->lastAmplitudeL += amplitudeLincrement; amplitudeL = this_voice->lastAmplitudeL >> 4;
                    this_voice->lastAmplitudeR += amplitudeRincrement; amplitudeR = this_voice->lastAmplitudeR >> 4;
                }
            }
            else
            {
                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    destL[0] += (sample * amplitudeL) >> 4;
                    destL[1] += (sample * amplitudeR) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    destL[2] += (sample * amplitudeL) >> 4;
                    destL[3] += (sample * amplitudeR) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    destL[4] += (sample * amplitudeL) >> 4;
                    destL[5] += (sample * amplitudeR) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    destL[6] += (sample * amplitudeL) >> 4;
                    destL[7] += (sample * amplitudeR) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    destL += 8;
                    this_voice->lastAmplitudeL += amplitudeLincrement; amplitudeL = this_voice->lastAmplitudeL >> 4;
                    this_voice->lastAmplitudeR += amplitudeRincrement; amplitudeR = this_voice->lastAmplitudeR >> 4;
                }
            }
        }
        else
        {   // stereo 16 bit instrument
            if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
            {
                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    for (inner = 0; inner < 4; inner++)
                    {
                        int32_t pos = (int32_t)cur_wave_i;
                        int32_t prev_pos = (pos > 0) ? pos - 1 : 0;
                        int32_t pos1 = (pos + 1 < totalFrames) ? (pos + 1) : pos;
                        int32_t pos2 = (pos + 2 < totalFrames) ? (pos + 2) : pos1;
                        // Left channel
                        int32_t sL0 = source[prev_pos*2];
                        int32_t sL1 = source[pos*2];
                        int32_t sL2 = source[pos1*2];
                        int32_t sL3 = source[pos2*2];
                        destL[0] += (PV_HQ16_CH(sL0, sL1, sL2, sL3, cur_wave_f, 0) * amplitudeL) >> 4;
                        // Right channel
                        int32_t sR0 = source[prev_pos*2 + 1];
                        int32_t sR1 = source[pos*2 + 1];
                        int32_t sR2 = source[pos1*2 + 1];
                        int32_t sR3 = source[pos2*2 + 1];
                        destL[1] += (PV_HQ16_CH(sR0, sR1, sR2, sR3, cur_wave_f, 1) * amplitudeR) >> 4;
                        destL += 2;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    }
                    this_voice->lastAmplitudeL += amplitudeLincrement; amplitudeL = this_voice->lastAmplitudeL >> 4;
                    this_voice->lastAmplitudeR += amplitudeRincrement; amplitudeR = this_voice->lastAmplitudeR >> 4;
                }
            }
            else
            {
                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    for (inner = 0; inner < 4; inner++)
                    {
                        calculated_source = source + cur_wave_i * 2;
                        b = calculated_source[0];
                        c = calculated_source[2];
                        sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                        destL[0] += (sample * amplitudeL) >> 4;
                        b = calculated_source[1];
                        c = calculated_source[3];
                        sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                        destL[1] += (sample * amplitudeR) >> 4;
                        destL += 2;
                
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    }
                    this_voice->lastAmplitudeL += amplitudeLincrement; amplitudeL = this_voice->lastAmplitudeL >> 4;
                    this_voice->lastAmplitudeR += amplitudeRincrement; amplitudeR = this_voice->lastAmplitudeR >> 4;
                }
            }
        }
    }
    /* lastAmplitudeL holds the full-precision ramp end. */
    /* lastAmplitudeR holds the full-precision ramp end. */
    this_voice->samplePosition.i = cur_wave_i;
    this_voice->samplePosition.f = cur_wave_f;
}

void PV_ServeU3232StereoPartialBuffer16 (GM_Voice *this_voice, bool looping)
{
    register int32_t          *destL;
    register int32_t      a, inner;
    register int16_t          *source, *calculated_source;
    register int32_t          b, c, sample;
    register U32            cur_wave_i, cur_wave_f;
    register U32            end_wave, wave_adjust = 0;
    U3232                   wave_increment;
    int32_t                   ampValueL, ampValueR;
    register int32_t          amplitudeL;
    register int32_t          amplitudeR;
    register int32_t          amplitudeLincrement, amplitudeRincrement;

#if REVERB_USED == VARIABLE_REVERB
    if (this_voice->reverbLevel || this_voice->chorusLevel)
    {
        PV_ServeU3232StereoPartialBuffer16NewReverb (this_voice, looping);
        return;
    }
#endif
    PV_CalculateStereoVolume(this_voice, &ampValueL, &ampValueR);
    amplitudeL = this_voice->lastAmplitudeL;
    amplitudeR = this_voice->lastAmplitudeR;
    /* Ramp in full precision (lastAmplitude is the accumulator) and shift only for the
       multiply. Shifting the step itself truncated it to 0 below Four_Loop<<4, so quiet
       voices stuck at a fixed level or dropped out. */
    amplitudeLincrement = (ampValueL - amplitudeL) / (MusicGlobals->Four_Loop);
    amplitudeRincrement = (ampValueR - amplitudeR) / (MusicGlobals->Four_Loop);

    amplitudeL = amplitudeL >> 4;
    amplitudeR = amplitudeR >> 4;

    destL = &MusicGlobals->songBufferDry[0];
    cur_wave_i = this_voice->samplePosition.i;
    cur_wave_f = this_voice->samplePosition.f;
    source = (int16_t *) this_voice->NotePtr;

    wave_increment = PV_GetWavePitchU3232(this_voice->NotePitch);

    if (looping)
    {
        wave_adjust = this_voice->NoteLoopEnd - this_voice->NoteLoopPtr;
        end_wave = this_voice->NoteLoopEnd - this_voice->NotePtr;
    }
    else
    {
        end_wave = this_voice->NotePtrEnd - this_voice->NotePtr - 1;
    }

    {
        if (this_voice->channels == 1)
        {   // mono instrument
            if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
            {
                // Cubic Hermite with loop-aware boundary handling
                int32_t loopStartIdx = (int32_t)(this_voice->NoteLoopPtr - this_voice->NotePtr);
                int32_t loopEndIdx = (int32_t)(this_voice->NoteLoopEnd - this_voice->NotePtr);
                int32_t totalFrames = (int32_t)(this_voice->NotePtrEnd - this_voice->NotePtr);

                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    for (inner = 0; inner < 4; inner++)
                    {
                        THE_CHECK_U3232(int16_t *);
                        int32_t pos = (int32_t)cur_wave_i;
                        int32_t s1 = source[pos];
                        int32_t s0, s2, s3;
                        if (looping)
                        {
                            s0 = PV_LoopWrapSample16U3232(source, pos - 1, loopStartIdx, loopEndIdx);
                            s2 = PV_LoopWrapSample16U3232(source, pos + 1, loopStartIdx, loopEndIdx);
                            s3 = PV_LoopWrapSample16U3232(source, pos + 2, loopStartIdx, loopEndIdx);
                        }
                        else
                        {
                            s0 = source[(pos > 0) ? pos - 1 : 0];
                            s2 = source[(pos + 1 < totalFrames) ? pos + 1 : pos];
                            s3 = source[(pos + 2 < totalFrames) ? pos + 2 : (pos + 1 < totalFrames) ? pos + 1 : pos];
                        }
                        sample = PV_HQ16_AUTO(s0, s1, s2, s3, cur_wave_f);
                        destL[0] += (sample * amplitudeL) >> 4;
                        destL[1] += (sample * amplitudeR) >> 4;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                        destL += 2;
                    }
                    this_voice->lastAmplitudeL += amplitudeLincrement; amplitudeL = this_voice->lastAmplitudeL >> 4;
                    this_voice->lastAmplitudeR += amplitudeRincrement; amplitudeR = this_voice->lastAmplitudeR >> 4;
                }
            }
            else
            {
                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
#if 1   //MOE'S OBSESSIVE FOLLY
                    for (inner = 0; inner < 4; inner++)
                    {
                        THE_CHECK_U3232(int16_t *);
                        b = source[cur_wave_i];
                        c = source[cur_wave_i+1];
                        sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                        destL[0] += (sample * amplitudeL) >> 4;
                        destL[1] += (sample * amplitudeR) >> 4;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                        destL += 2;
                    }
#else
                    THE_CHECK_U3232(int16_t *);
                    b = source[cur_wave_i];
                        int32_t pos1 = (pos + 1 < totalFrames) ? (pos + 1) : pos;
                        int32_t pos2 = (pos + 2 < totalFrames) ? (pos + 2) : pos1;
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    destL[0] += (sample * amplitudeL) >> 4;
                        int32_t sL2 = source[pos1*2];
                        int32_t sL3 = source[pos2*2];

                    THE_CHECK_U3232(int16_t *);
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                        int32_t sR2 = source[pos1*2 + 1];
                        int32_t sR3 = source[pos2*2 + 1];
                    destL[3] += (sample * amplitudeR) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    THE_CHECK_U3232(int16_t *);
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    destL[4] += (sample * amplitudeL) >> 4;
                    destL[5] += (sample * amplitudeR) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    THE_CHECK_U3232(int16_t *);
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    destL[6] += (sample * amplitudeL) >> 4;
                    destL[7] += (sample * amplitudeR) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    destL += 8;
#endif
                    this_voice->lastAmplitudeL += amplitudeLincrement; amplitudeL = this_voice->lastAmplitudeL >> 4;
                    this_voice->lastAmplitudeR += amplitudeRincrement; amplitudeR = this_voice->lastAmplitudeR >> 4;
                }
            }
        }
        else
        {   // Stereo 16 bit instrument
            if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
            {
                int32_t loopStartIdx = (int32_t)(this_voice->NoteLoopPtr - this_voice->NotePtr);
                int32_t loopEndIdx = (int32_t)(this_voice->NoteLoopEnd - this_voice->NotePtr);
                int32_t totalFrames = (int32_t)(this_voice->NotePtrEnd - this_voice->NotePtr);

                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    for (inner = 0; inner < 4; inner++)
                    {
                        THE_CHECK_U3232(int16_t *);
                        int32_t pos = (int32_t)cur_wave_i;
                        int32_t idx0, idx2, idx3;
                        if (looping)
                        {
                            int32_t loopLen = loopEndIdx - loopStartIdx;
                            idx0 = pos - 1;
                            if (idx0 < loopStartIdx) idx0 += loopLen;
                            idx2 = pos + 1;
                            if (idx2 >= loopEndIdx) idx2 -= loopLen;
                            idx3 = pos + 2;
                            if (idx3 >= loopEndIdx) idx3 -= loopLen;
                        }
                        else
                        {
                            idx0 = (pos > 0) ? pos - 1 : 0;
                            idx2 = (pos + 1 < totalFrames) ? (pos + 1) : pos;
                            idx3 = (pos + 2 < totalFrames) ? (pos + 2) : idx2;
                        }
                        // Left channel
                        int32_t sL0 = source[idx0*2];
                        int32_t sL1 = source[pos*2];
                        int32_t sL2 = source[idx2*2];
                        int32_t sL3 = source[idx3*2];
                        destL[0] += (PV_HQ16_CH(sL0, sL1, sL2, sL3, cur_wave_f, 0) * amplitudeL) >> 4;
                        // Right channel
                        int32_t sR0 = source[idx0*2 + 1];
                        int32_t sR1 = source[pos*2 + 1];
                        int32_t sR2 = source[idx2*2 + 1];
                        int32_t sR3 = source[idx3*2 + 1];
                        destL[1] += (PV_HQ16_CH(sR0, sR1, sR2, sR3, cur_wave_f, 1) * amplitudeR) >> 4;
                        destL += 2;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    }
                    this_voice->lastAmplitudeL += amplitudeLincrement; amplitudeL = this_voice->lastAmplitudeL >> 4;
                    this_voice->lastAmplitudeR += amplitudeRincrement; amplitudeR = this_voice->lastAmplitudeR >> 4;
                }
            }
            else
            {
                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    for (inner = 0; inner < 4; inner++)
                    {
                        THE_CHECK_U3232(int16_t *);
                        calculated_source = source + cur_wave_i * 2;
                        b = calculated_source[0];
                        c = calculated_source[2];
                        sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                        destL[0] += (sample * amplitudeL) >> 4;
                        b = calculated_source[1];
                        c = calculated_source[3];
                        sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                        destL[1] += (sample * amplitudeR) >> 4;
                        destL += 2;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    }
                    this_voice->lastAmplitudeL += amplitudeLincrement; amplitudeL = this_voice->lastAmplitudeL >> 4;
                    this_voice->lastAmplitudeR += amplitudeRincrement; amplitudeR = this_voice->lastAmplitudeR >> 4;
                }
            }
        }
    }
    this_voice->samplePosition.i = cur_wave_i;
    this_voice->samplePosition.f = cur_wave_f;
    /* lastAmplitudeL holds the full-precision ramp end. */
    /* lastAmplitudeR holds the full-precision ramp end. */
FINISH:
    return;
}

#endif  // LOOPS_USED == U3232_LOOPS

