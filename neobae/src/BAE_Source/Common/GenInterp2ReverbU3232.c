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
** "GenInterp2ReverbU3232.c"
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
**  3/11/99     MOE: created file from GenInterp2ReverbFloat.c
**  10/19/99    MSD: switched to REVERB_USED and LOOPS_USED
**  2/4/2000    Changed copyright. We're Y2K compliant!
**  6/11/2001   sh  Removed katmai code
*/
/*****************************************************************************/

#include "GenSnd.h"
#include "GenPriv.h"
#include <stdint.h>

#if ((REVERB_USED == VARIABLE_REVERB) && (LOOPS_USED == U3232_LOOPS))

// Cubic Hermite (Catmull-Rom) interpolation for NewReverb advanced interpolation mode.
static inline int32_t PV_CubicHermiteInterpNR(int32_t s0, int32_t s1, int32_t s2, int32_t s3, U32 frac)
{
    int32_t t = (int32_t)(frac >> 17);
    int32_t A = -s0 + 3*s1 - 3*s2 + s3;
    int32_t B = 2*s0 - 5*s1 + 4*s2 - s3;
    int32_t C = s2 - s0;
    int32_t r;
    r = (int32_t)(((int64_t)A * t) >> 15) + B;
    r = (int32_t)(((int64_t)r * t) >> 15) + C;
    r = (int32_t)(((int64_t)r * t) >> 16);
    return s1 + r;
}

#include "GenInterpSincU3232.inc"

#define PV_HQ16_AUTO(s0, s1, s2, s3, frac) \
    (PV_U3232UseSincInterp() \
         ? PV_SincInterp16Auto(this_voice, source, (int32_t)cur_wave_i, (frac), \
                               (this_voice->NoteLoopEnd > this_voice->NoteLoopPtr)) \
         : PV_CubicHermiteInterpNR((s0), (s1), (s2), (s3), (frac)))
#define PV_HQ16_CH(s0, s1, s2, s3, frac, off) \
    (PV_U3232UseSincInterp() \
         ? PV_SincInterp16Stride(this_voice, source, (int32_t)cur_wave_i, (frac), \
                                 (this_voice->NoteLoopEnd > this_voice->NoteLoopPtr), 2, (off)) \
         : PV_CubicHermiteInterpNR((s0), (s1), (s2), (s3), (frac)))

static inline int32_t PV_LoopWrapSample16(int16_t *source, int32_t idx, int32_t loopStart, int32_t loopEnd)
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

static inline int32_t PV_GetSample16WithBounds(const int16_t *source, int32_t idx, int32_t totalFrames)
{
    if (idx < 0) return source[0];
    if (idx >= totalFrames) return source[totalFrames - 1];
    return source[idx];
}

static inline int32_t PV_GetSample8WithBounds(const unsigned char *source, int32_t idx, int32_t totalFrames)
{
    if (idx < 0) return source[0];
    if (idx >= totalFrames) return source[totalFrames - 1];
    return source[idx];
}

void PV_ServeU3232FullBufferNewReverb(GM_Voice *this_voice)
{
    register int32_t          *dest;
    register int32_t          *destReverb, *destChorus;
    register int32_t      a, inner;
    register unsigned char          *source;
    register int32_t          b, c, sample;
    register U32            cur_wave_i, cur_wave_f;
    U3232                   wave_increment;
    register int32_t          amplitude, amplitudeAdjust;
    register int32_t          amplitudeReverb, amplitudeChorus;

    amplitude = this_voice->lastAmplitudeL;
    amplitudeAdjust = (this_voice->NoteVolume * this_voice->NoteVolumeEnvelope) >> VOLUME_PRECISION_SCALAR;
    amplitudeAdjust = (amplitudeAdjust - amplitude) / MusicGlobals->Four_Loop;
    dest = &MusicGlobals->songBufferDry[0];
    destReverb = &MusicGlobals->songBufferReverb[0];
    destChorus = &MusicGlobals->songBufferChorus[0];
    source = this_voice->NotePtr;
    cur_wave_i = this_voice->samplePosition.i;
    cur_wave_f = this_voice->samplePosition.f;

    wave_increment = PV_GetWavePitchU3232(this_voice->NotePitch);

    {
        if (this_voice->channels == 1)
        {
            int32_t totalFrames = (int32_t)(this_voice->NotePtrEnd - this_voice->NotePtr);
            amplitudeReverb = (amplitude * this_voice->reverbLevel) >> 7;
            amplitudeChorus = (amplitude * this_voice->chorusLevel) >> 7;
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                for (inner = 0; inner < 4; inner++)
                {
                    int32_t pos = (int32_t)cur_wave_i;
                    int32_t s0, s1, s2, s3;
                    if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
                    {
                        s0 = source[(pos > 0) ? pos - 1 : 0] - 0x80;
                        s1 = source[pos] - 0x80;
                        s2 = PV_GetSample8WithBounds(source, pos + 1, totalFrames) - 0x80;
                        s3 = PV_GetSample8WithBounds(source, pos + 2, totalFrames) - 0x80;
                        sample = PV_CubicHermiteInterpNR(s0, s1, s2, s3, cur_wave_f >> 1);
                    }
                    else
                    {
                        b = source[cur_wave_i];
                        c = source[cur_wave_i + 1];
                        sample = ((((int32_t)(cur_wave_f >> 16) * (int32_t)(c - b)) >> 16) + b - 0x80);
                    }
                    dest[inner] += sample * amplitude;
                    destReverb[inner] += sample * amplitudeReverb;
                    destChorus[inner] += sample * amplitudeChorus;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                }
                dest += 4;
                destReverb += 4;
                destChorus += 4;
                amplitude += amplitudeAdjust;
            }
        }
        else
        {   // stereo 8 bit instrument
            int32_t totalFrames = (int32_t)(this_voice->NotePtrEnd - this_voice->NotePtr);
            for (a = MusicGlobals->Sixteen_Loop; a > 0; --a)
            {
                amplitudeReverb = (amplitude >> 7) * this_voice->reverbLevel;
                amplitudeChorus = (amplitude >> 7) * this_voice->chorusLevel;

                for (inner = 0; inner < 16; inner++)
                {
                    int32_t pos = (int32_t)cur_wave_i;
                    int32_t s0L, s1L, s2L, s3L;
                    int32_t s0R, s1R, s2R, s3R;
                    if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
                    {
                        int32_t prev_pos = (pos > 0) ? pos - 1 : 0;
                        int32_t next_pos1 = pos + 1;
                        int32_t next_pos2 = pos + 2;
                        if (next_pos1 >= totalFrames) next_pos1 = totalFrames - 1;
                        if (next_pos2 >= totalFrames) next_pos2 = totalFrames - 1;
                        s0L = source[prev_pos * 2] - 0x80;
                        s1L = source[pos * 2] - 0x80;
                        s2L = source[next_pos1 * 2] - 0x80;
                        s3L = source[next_pos2 * 2] - 0x80;
                        s0R = source[prev_pos * 2 + 1] - 0x80;
                        s1R = source[pos * 2 + 1] - 0x80;
                        s2R = source[next_pos1 * 2 + 1] - 0x80;
                        s3R = source[next_pos2 * 2 + 1] - 0x80;
                        sample = (PV_CubicHermiteInterpNR(s0L, s1L, s2L, s3L, cur_wave_f >> 1) +
                                  PV_CubicHermiteInterpNR(s0R, s1R, s2R, s3R, cur_wave_f >> 1)) >> 1;
                    }
                    else
                    {
                        b = source[cur_wave_i * 2] + source[cur_wave_i * 2 + 1];
                        c = source[cur_wave_i * 2 + 2] + source[cur_wave_i * 2 + 3];
                        sample = ((((int32_t)(cur_wave_f >> 16) * (int32_t)(c - b)) >> 16) + b - 0x100) >> 1;
                    }
                    *dest += sample * amplitude;
                    *destReverb += sample * amplitudeReverb;
                    dest++;
                    destReverb++;
                    *destChorus++ += sample * amplitudeChorus;
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

void PV_ServeU3232PartialBufferNewReverb (GM_Voice *this_voice, bool looping)
{
    register int32_t          *dest;
    register int32_t          *destReverb, *destChorus;
    register int32_t      a, inner;
    register unsigned char          *source;
    register int32_t          b, c, sample;
    register U32            cur_wave_i, cur_wave_f;
    register U32            end_wave, wave_adjust = 0;
    U3232                   wave_increment;
    register int32_t          amplitude, amplitudeAdjust;
    register int32_t          amplitudeReverb, amplitudeChorus;

    amplitude = this_voice->lastAmplitudeL;
    amplitudeAdjust = (this_voice->NoteVolume * this_voice->NoteVolumeEnvelope) >> VOLUME_PRECISION_SCALAR;
    amplitudeAdjust = (amplitudeAdjust - amplitude) / MusicGlobals->Four_Loop;
    dest = &MusicGlobals->songBufferDry[0];
    destReverb = &MusicGlobals->songBufferReverb[0];
    destChorus = &MusicGlobals->songBufferChorus[0];
    source = this_voice->NotePtr;
    cur_wave_i = this_voice->samplePosition.i;
    cur_wave_f = this_voice->samplePosition.f;

    wave_increment = PV_GetWavePitchU3232(this_voice->NotePitch);

    if (looping)
    {
        wave_adjust = this_voice->NoteLoopEnd - this_voice->NoteLoopPtr;
        end_wave = this_voice->NoteLoopEnd - this_voice->NotePtr;
        if (this_voice->channels == 2)
        {
            wave_adjust >>= 1;
            end_wave >>= 1;
        }
    }
    else
    {
        if (this_voice->channels == 2)
        {
            end_wave = ((this_voice->NotePtrEnd - this_voice->NotePtr) >> 1) - 1;
        }
        else
        {
            end_wave = this_voice->NotePtrEnd - this_voice->NotePtr - 1;
        }
    }

    {
        if (this_voice->channels == 1)
        {
            int32_t loopStartIdx = (int32_t)(this_voice->NoteLoopPtr - this_voice->NotePtr);
            int32_t loopEndIdx = (int32_t)(this_voice->NoteLoopEnd - this_voice->NotePtr);
            int32_t totalFrames = (int32_t)(this_voice->NotePtrEnd - this_voice->NotePtr);

            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                amplitudeReverb = (amplitude >> 7) * this_voice->reverbLevel;
                amplitudeChorus = (amplitude >> 7) * this_voice->chorusLevel;

                for (inner = 0; inner < 4; inner++)
                {
                    THE_CHECK_U3232(unsigned char *);
                    int32_t pos = (int32_t)cur_wave_i;
                    int32_t s0, s1, s2, s3;
                    if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
                    {
                        if (looping)
                        {
                            int32_t loopLen = loopEndIdx - loopStartIdx;
                            s1 = source[pos] - 0x80;
                            s0 = source[(pos - 1 < loopStartIdx) ? pos - 1 + loopLen : pos - 1] - 0x80;
                            s2 = source[(pos + 1 >= loopEndIdx) ? pos + 1 - loopLen : pos + 1] - 0x80;
                            s3 = source[(pos + 2 >= loopEndIdx) ? pos + 2 - loopLen : pos + 2] - 0x80;
                        }
                        else
                        {
                            s0 = source[(pos > 0) ? pos - 1 : 0] - 0x80;
                            s1 = source[pos] - 0x80;
                            s2 = PV_GetSample8WithBounds(source, pos + 1, totalFrames) - 0x80;
                            s3 = PV_GetSample8WithBounds(source, pos + 2, totalFrames) - 0x80;
                        }
                        sample = PV_CubicHermiteInterpNR(s0, s1, s2, s3, cur_wave_f >> 1);
                    }
                    else
                    {
                        b = source[cur_wave_i];
                        c = source[cur_wave_i + 1];
                        sample = (((int32_t)(cur_wave_f >> 16) * (int32_t)(c - b)) >> 16) + b - 0x80;
                    }
                    *dest += sample * amplitude;
                    *destReverb += sample * amplitudeReverb;
                    dest++;
                    destReverb++;
                    *destChorus++ += sample * amplitudeChorus;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                }
                amplitude += amplitudeAdjust;
            }
        }
        else
        {   // stereo 8 bit instrument
            int32_t loopStartIdx = (int32_t)(this_voice->NoteLoopPtr - this_voice->NotePtr);
            int32_t loopEndIdx = (int32_t)(this_voice->NoteLoopEnd - this_voice->NotePtr);
            int32_t totalFrames = (int32_t)(this_voice->NotePtrEnd - this_voice->NotePtr);

            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                amplitudeReverb = (amplitude >> 7) * this_voice->reverbLevel;
                amplitudeChorus = (amplitude >> 7) * this_voice->chorusLevel;

                for (inner = 0; inner < 4; inner++)
                {
                    THE_CHECK_U3232(unsigned char *);
                    int32_t pos = (int32_t)cur_wave_i;
                    int32_t s0L, s1L, s2L, s3L;
                    int32_t s0R, s1R, s2R, s3R;
                    if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
                    {
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
                            idx2 = pos + 1;
                            if (idx2 >= totalFrames) idx2 = totalFrames - 1;
                            idx3 = pos + 2;
                            if (idx3 >= totalFrames) idx3 = totalFrames - 1;
                        }
                        s0L = source[idx0 * 2] - 0x80;
                        s1L = source[pos * 2] - 0x80;
                        s2L = source[idx2 * 2] - 0x80;
                        s3L = source[idx3 * 2] - 0x80;
                        s0R = source[idx0 * 2 + 1] - 0x80;
                        s1R = source[pos * 2 + 1] - 0x80;
                        s2R = source[idx2 * 2 + 1] - 0x80;
                        s3R = source[idx3 * 2 + 1] - 0x80;
                        sample = (PV_CubicHermiteInterpNR(s0L, s1L, s2L, s3L, cur_wave_f >> 1) +
                                  PV_CubicHermiteInterpNR(s0R, s1R, s2R, s3R, cur_wave_f >> 1)) >> 1;
                    }
                    else
                    {
                        U32 next_wave_i = cur_wave_i + 1;
                        if (looping && (next_wave_i >= end_wave))
                        {
                            next_wave_i -= wave_adjust;
                        }
                        b = source[cur_wave_i * 2] + source[cur_wave_i * 2 + 1];
                        c = source[next_wave_i * 2] + source[next_wave_i * 2 + 1];
                        sample = ((((int32_t)(cur_wave_f >> 16) * (int32_t)(c - b)) >> 16) + b - 0x100) >> 1;
                    }
                    *dest += sample * amplitude;
                    *destReverb += sample * amplitudeReverb;
                    dest++;
                    destReverb++;
                    *destChorus++ += sample * amplitudeChorus;
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

void PV_ServeU3232StereoFullBufferNewReverb(GM_Voice *this_voice)
{
    register int32_t          *destL;
    register int32_t          *destReverb, *destChorus;
    register int32_t      a, inner;
    register unsigned char          *source;
    register int32_t          b, c;
    register U32            cur_wave_i, cur_wave_f;
    U3232                   wave_increment;
    register int32_t          sample;
    int32_t                   ampValueL, ampValueR;
    register int32_t          amplitudeL;
    register int32_t          amplitudeR;
    register int32_t          amplitudeLincrement;
    register int32_t          amplitudeRincrement;
    register int32_t          amplitudeReverb, amplitudeChorus;

    PV_CalculateStereoVolume(this_voice, &ampValueL, &ampValueR);
    amplitudeL = this_voice->lastAmplitudeL;
    amplitudeR = this_voice->lastAmplitudeR;
    amplitudeLincrement = (ampValueL - amplitudeL) / (MusicGlobals->Four_Loop);
    amplitudeRincrement = (ampValueR - amplitudeR) / (MusicGlobals->Four_Loop);

    destL = &MusicGlobals->songBufferDry[0];
    destReverb = &MusicGlobals->songBufferReverb[0];
    destChorus = &MusicGlobals->songBufferChorus[0];
    source = this_voice->NotePtr;
    cur_wave_i = this_voice->samplePosition.i;
    cur_wave_f = this_voice->samplePosition.f;

    wave_increment = PV_GetWavePitchU3232(this_voice->NotePitch);

    {
        if (this_voice->channels == 1)
        {   // mono instrument
            int32_t totalFrames = (int32_t)(this_voice->NotePtrEnd - this_voice->NotePtr);
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                amplitudeReverb = ((amplitudeL + amplitudeR) >> 8) * this_voice->reverbLevel;
                amplitudeChorus = ((amplitudeL + amplitudeR) >> 8) * this_voice->chorusLevel;
                for (inner = 0; inner < 4; inner++)
                {
                    int32_t pos = (int32_t)cur_wave_i;
                    int32_t s0, s1, s2, s3;
                    if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
                    {
                        s0 = source[(pos > 0) ? pos - 1 : 0] - 0x80;
                        s1 = source[pos] - 0x80;
                        s2 = PV_GetSample8WithBounds(source, pos + 1, totalFrames) - 0x80;
                        s3 = PV_GetSample8WithBounds(source, pos + 2, totalFrames) - 0x80;
                        sample = PV_CubicHermiteInterpNR(s0, s1, s2, s3, cur_wave_f >> 1);
                    }
                    else
                    {
                        b = source[cur_wave_i];
                        c = source[cur_wave_i + 1];
                        sample = (((int32_t)(cur_wave_f >> 16) * (int32_t)(c - b)) >> 16) + b - 0x80;
                    }
                    destL[inner * 2] += sample * amplitudeL;
                    destL[inner * 2 + 1] += sample * amplitudeR;
                    destReverb[inner] += sample * amplitudeReverb;
                    destChorus[inner] += sample * amplitudeChorus;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                }
                destL += 8;
                destReverb += 4;
                destChorus += 4;
                amplitudeL += amplitudeLincrement;
                amplitudeR += amplitudeRincrement;
            }
        }
        else
        {   // stereo 8 bit instrument
            int32_t totalFrames = (int32_t)(this_voice->NotePtrEnd - this_voice->NotePtr);
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                amplitudeReverb = ((amplitudeL + amplitudeR) >> 9) * this_voice->reverbLevel;
                amplitudeChorus = ((amplitudeL + amplitudeR) >> 9) * this_voice->chorusLevel;

                for (inner = 0; inner < 4; inner++)
                {
                    int32_t pos = (int32_t)cur_wave_i;
                    int32_t s0L, s1L, s2L, s3L;
                    int32_t s0R, s1R, s2R, s3R;
                    if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
                    {
                        int32_t prev_pos = (pos > 0) ? pos - 1 : 0;
                        int32_t next_pos1 = pos + 1;
                        int32_t next_pos2 = pos + 2;
                        if (next_pos1 >= totalFrames) next_pos1 = totalFrames - 1;
                        if (next_pos2 >= totalFrames) next_pos2 = totalFrames - 1;
                        s0L = source[prev_pos * 2] - 0x80;
                        s1L = source[pos * 2] - 0x80;
                        s2L = source[next_pos1 * 2] - 0x80;
                        s3L = source[next_pos2 * 2] - 0x80;
                        s0R = source[prev_pos * 2 + 1] - 0x80;
                        s1R = source[pos * 2 + 1] - 0x80;
                        s2R = source[next_pos1 * 2 + 1] - 0x80;
                        s3R = source[next_pos2 * 2 + 1] - 0x80;
                        int32_t sampleL = PV_CubicHermiteInterpNR(s0L, s1L, s2L, s3L, cur_wave_f >> 1);
                        int32_t sampleR = PV_CubicHermiteInterpNR(s0R, s1R, s2R, s3R, cur_wave_f >> 1);
                        destL[0] += sampleL * amplitudeL;
                        *destReverb += (sampleL + sampleR) * amplitudeReverb >> 1;
                        *destChorus += (sampleL + sampleR) * amplitudeChorus >> 1;
                        destL[1] += sampleR * amplitudeR;
                        *destReverb += (sampleL + sampleR) * amplitudeReverb >> 1;
                        *destChorus += (sampleL + sampleR) * amplitudeChorus >> 1;
                    }
                    else
                    {
                        b = source[cur_wave_i * 2];
                        c = source[cur_wave_i * 2 + 2];
                        sample = (((int32_t)(cur_wave_f >> 16) * (int32_t)(c - b)) >> 16) + b - 0x80;
                        destL[0] += sample * amplitudeL;
                        *destReverb += sample * amplitudeReverb;
                        *destChorus += sample * amplitudeChorus;
                        b = source[cur_wave_i * 2 + 1];
                        c = source[cur_wave_i * 2 + 3];
                        sample = (((int32_t)(cur_wave_f >> 16) * (int32_t)(c - b)) >> 16) + b - 0x80;
                        destL[1] += sample * amplitudeR;
                        *destReverb += sample * amplitudeReverb;
                        *destChorus += sample * amplitudeChorus;
                    }
                    destL += 2;
                    destReverb++;
                    destChorus++;
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

void PV_ServeU3232StereoPartialBufferNewReverb (GM_Voice *this_voice, bool looping)
{
    register int32_t          *destL;
    register int32_t          *destReverb, *destChorus;
    register int32_t      a, inner;
    register unsigned char          *source;
    register int32_t          b, c, sample;
    register U32            cur_wave_i, cur_wave_f;
    register U32            end_wave, wave_adjust = 0;
    U3232                   wave_increment;
    int32_t                   ampValueL, ampValueR;
    register int32_t          amplitudeL;
    register int32_t          amplitudeR;
    register int32_t          amplitudeLincrement, amplitudeRincrement;
    register int32_t          amplitudeReverb, amplitudeChorus;
    U32                      totalFrames;

    if ((this_voice == NULL) || (this_voice->NotePtr == NULL) || (this_voice->NotePtrEnd == NULL) ||
        (this_voice->NotePtrEnd <= this_voice->NotePtr))
    {
        if (this_voice)
        {
            this_voice->voiceMode = VOICE_UNUSED;
        }
        return;
    }

    if (this_voice->channels == 2)
    {
        totalFrames = (U32)((this_voice->NotePtrEnd - this_voice->NotePtr) >> 1);
    }
    else
    {
        totalFrames = (U32)(this_voice->NotePtrEnd - this_voice->NotePtr);
    }

    if (totalFrames < 2)
    {
        this_voice->voiceMode = VOICE_UNUSED;
        return;
    }

    if (looping)
    {
        if ((this_voice->NoteLoopPtr == NULL) || (this_voice->NoteLoopEnd == NULL) ||
            (this_voice->NoteLoopPtr < this_voice->NotePtr) ||
            (this_voice->NoteLoopEnd > this_voice->NotePtrEnd) ||
            (this_voice->NoteLoopEnd <= this_voice->NoteLoopPtr))
        {
            looping = FALSE;
        }
    }

    PV_CalculateStereoVolume(this_voice, &ampValueL, &ampValueR);
    amplitudeL = this_voice->lastAmplitudeL;
    amplitudeR = this_voice->lastAmplitudeR;
    amplitudeLincrement = (ampValueL - amplitudeL) / (MusicGlobals->Four_Loop);
    amplitudeRincrement = (ampValueR - amplitudeR) / (MusicGlobals->Four_Loop);

    destL = &MusicGlobals->songBufferDry[0];
    destReverb = &MusicGlobals->songBufferReverb[0];
    destChorus = &MusicGlobals->songBufferChorus[0];
    source = this_voice->NotePtr;
    cur_wave_i = this_voice->samplePosition.i;
    cur_wave_f = this_voice->samplePosition.f;

    if (cur_wave_i >= (totalFrames - 1))
    {
        if (looping)
        {
            cur_wave_i %= (totalFrames - 1);
        }
        else
        {
            this_voice->voiceMode = VOICE_UNUSED;
            return;
        }
    }

    wave_increment = PV_GetWavePitchU3232(this_voice->NotePitch);

    if (looping)
    {
        wave_adjust = this_voice->NoteLoopEnd - this_voice->NoteLoopPtr;
        end_wave = this_voice->NoteLoopEnd - this_voice->NotePtr;
        if (this_voice->channels == 2)
        {
            wave_adjust >>= 1;
            end_wave >>= 1;
        }
    }
    else
    {
        if (this_voice->channels == 2)
        {
            end_wave = ((this_voice->NotePtrEnd - this_voice->NotePtr) >> 1) - 1;
        }
        else
        {
            end_wave = this_voice->NotePtrEnd - this_voice->NotePtr - 1;
        }
    }

    {
        if (this_voice->channels == 1)
        {   // mono instrument
            int32_t loopStartIdx = (int32_t)(this_voice->NoteLoopPtr - this_voice->NotePtr);
            int32_t loopEndIdx = (int32_t)(this_voice->NoteLoopEnd - this_voice->NotePtr);
            int32_t totalFrames = (int32_t)(this_voice->NotePtrEnd - this_voice->NotePtr);

            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                amplitudeReverb = ((amplitudeL + amplitudeR) >> 8) * this_voice->reverbLevel;
                amplitudeChorus = ((amplitudeL + amplitudeR) >> 8) * this_voice->chorusLevel;

                for (inner = 0; inner < 4; inner++)
                {
                    THE_CHECK_U3232(unsigned char *);
                    int32_t pos = (int32_t)cur_wave_i;
                    int32_t s0, s1, s2, s3;
                    if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
                    {
                        if (looping)
                        {
                            int32_t loopLen = loopEndIdx - loopStartIdx;
                            s1 = source[pos] - 0x80;
                            s0 = source[(pos - 1 < loopStartIdx) ? pos - 1 + loopLen : pos - 1] - 0x80;
                            s2 = source[(pos + 1 >= loopEndIdx) ? pos + 1 - loopLen : pos + 1] - 0x80;
                            s3 = source[(pos + 2 >= loopEndIdx) ? pos + 2 - loopLen : pos + 2] - 0x80;
                        }
                        else
                        {
                            s0 = source[(pos > 0) ? pos - 1 : 0] - 0x80;
                            s1 = source[pos] - 0x80;
                            s2 = PV_GetSample8WithBounds(source, pos + 1, totalFrames) - 0x80;
                            s3 = PV_GetSample8WithBounds(source, pos + 2, totalFrames) - 0x80;
                        }
                        sample = PV_CubicHermiteInterpNR(s0, s1, s2, s3, cur_wave_f >> 1);
                    }
                    else
                    {
                        b = source[cur_wave_i];
                        c = source[cur_wave_i + 1];
                        sample = (((int32_t)(cur_wave_f >> 16) * (int32_t)(c - b)) >> 16) + b - 0x80;
                    }
                    destL[0] += sample * amplitudeL;
                    destL[1] += sample * amplitudeR;
                    destReverb[0] += sample * amplitudeReverb;
                    destChorus[0] += sample * amplitudeChorus;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    destL += 2;
                    destReverb++;
                    destChorus++;
                }
                amplitudeL += amplitudeLincrement;
                amplitudeR += amplitudeRincrement;
            }
        }
        else
        {   // Stereo 8 bit instrument
            int32_t loopStartIdx = (int32_t)((this_voice->NoteLoopPtr - this_voice->NotePtr) >> 1);
            int32_t loopEndIdx = (int32_t)((this_voice->NoteLoopEnd - this_voice->NotePtr) >> 1);
            int32_t totalFrames = (int32_t)((this_voice->NotePtrEnd - this_voice->NotePtr) >> 1);

            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                amplitudeReverb = ((amplitudeL + amplitudeR) >> 9) * this_voice->reverbLevel;
                amplitudeChorus = ((amplitudeL + amplitudeR) >> 9) * this_voice->chorusLevel;

                for (inner = 0; inner < 4; inner++)
                {
                    THE_CHECK_U3232(unsigned char *);
                    int32_t pos = (int32_t)cur_wave_i;
                    int32_t s0L, s1L, s2L, s3L;
                    int32_t s0R, s1R, s2R, s3R;
                    if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
                    {
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
                            idx2 = pos + 1;
                            if (idx2 >= totalFrames) idx2 = totalFrames - 1;
                            idx3 = pos + 2;
                            if (idx3 >= totalFrames) idx3 = totalFrames - 1;
                        }
                        s0L = source[idx0 * 2] - 0x80;
                        s1L = source[pos * 2] - 0x80;
                        s2L = source[idx2 * 2] - 0x80;
                        s3L = source[idx3 * 2] - 0x80;
                        s0R = source[idx0 * 2 + 1] - 0x80;
                        s1R = source[pos * 2 + 1] - 0x80;
                        s2R = source[idx2 * 2 + 1] - 0x80;
                        s3R = source[idx3 * 2 + 1] - 0x80;
                        int32_t sampleL = PV_CubicHermiteInterpNR(s0L, s1L, s2L, s3L, cur_wave_f >> 1);
                        int32_t sampleR = PV_CubicHermiteInterpNR(s0R, s1R, s2R, s3R, cur_wave_f >> 1);
                        destL[0] += sampleL * amplitudeL;
                        *destReverb += (sampleL + sampleR) * amplitudeReverb >> 1;
                        *destChorus += (sampleL + sampleR) * amplitudeChorus >> 1;
                        destL[1] += sampleR * amplitudeR;
                        *destReverb += (sampleL + sampleR) * amplitudeReverb >> 1;
                        *destChorus += (sampleL + sampleR) * amplitudeChorus >> 1;
                    }
                    else
                    {
                        U32 next_wave_i = cur_wave_i + 1;
                        if (looping && (next_wave_i >= end_wave))
                        {
                            next_wave_i -= wave_adjust;
                        }
                        b = source[cur_wave_i * 2];
                        c = source[next_wave_i * 2];
                        sample = (((int32_t)(cur_wave_f >> 16) * (int32_t)(c - b)) >> 16) + b - 0x80;
                        destL[0] += sample * amplitudeL;
                        *destReverb += sample * amplitudeReverb;
                        *destChorus += sample * amplitudeChorus;
                        b = source[cur_wave_i * 2 + 1];
                        c = source[next_wave_i * 2 + 1];
                        sample = (((int32_t)(cur_wave_f >> 16) * (int32_t)(c - b)) >> 16) + b - 0x80;
                        destL[1] += sample * amplitudeR;
                        *destReverb += sample * amplitudeReverb;
                        *destChorus += sample * amplitudeChorus;
                    }
                    destL += 2;
                    destReverb++;
                    destChorus++;
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

void PV_ServeU3232FullBuffer16NewReverb (GM_Voice *this_voice)
{
    register int32_t          *dest;
    register int32_t          *destReverb, *destChorus;
    register int32_t      a, inner;
    register int16_t          *source, *calculated_source;
    register int32_t          b, c, sample;
    register U32            cur_wave_i, cur_wave_f;
    U3232                   wave_increment;
    register int32_t          amplitude, amplitudeAdjust;
    register int32_t          amplitudeReverb, amplitudeChorus;

    amplitude = this_voice->lastAmplitudeL;
    amplitudeAdjust = (this_voice->NoteVolume * this_voice->NoteVolumeEnvelope) >> VOLUME_PRECISION_SCALAR;
    /* Ramp in full precision (lastAmplitude is the accumulator) and shift only for the
       multiply. Shifting the step itself truncated it to 0 below Four_Loop<<4, so quiet
       voices stuck at a fixed level or dropped out. */
    amplitudeAdjust = (amplitudeAdjust - amplitude) / MusicGlobals->Four_Loop;
    amplitude = amplitude >> 4;

    dest = &MusicGlobals->songBufferDry[0];
    destReverb = &MusicGlobals->songBufferReverb[0];
    destChorus = &MusicGlobals->songBufferChorus[0];
    source = (int16_t *) this_voice->NotePtr;
    cur_wave_i = this_voice->samplePosition.i;
    cur_wave_f = this_voice->samplePosition.f;

    wave_increment = PV_GetWavePitchU3232(this_voice->NotePitch);

    {
        if (this_voice->channels == 1)
        {
            if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
            {
                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    amplitudeReverb = (amplitude >> 7) * this_voice->reverbLevel;
                    amplitudeChorus = (amplitude >> 7) * this_voice->chorusLevel;
                    for (inner = 0; inner < 4; inner++)
                    {
                        int32_t pos = (int32_t)cur_wave_i;
                        int32_t s0 = source[(pos > 0) ? pos - 1 : 0];
                        int32_t s1 = source[pos];
                        int32_t s2 = source[pos + 1];
                        int32_t s3 = source[pos + 2];
                        sample = PV_HQ16_AUTO(s0, s1, s2, s3, cur_wave_f);
                        dest[inner] += (sample * amplitude) >> 4;
                        destReverb[inner] += (sample * amplitudeReverb) >> 4;
                        destChorus[inner] += (sample * amplitudeChorus) >> 4;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    }
                    dest += 4;
                    destReverb += 4;
                    destChorus += 4;
                    this_voice->lastAmplitudeL += amplitudeAdjust; amplitude = this_voice->lastAmplitudeL >> 4;
                }
            }
            else
            {
                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    amplitudeReverb = (amplitude >> 7) * this_voice->reverbLevel;
                    amplitudeChorus = (amplitude >> 7) * this_voice->chorusLevel;

                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    *dest += (sample * amplitude) >> 4;
                    *destReverb += (sample * amplitudeReverb) >> 4;
                    *destChorus += (sample * amplitudeChorus) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    dest[1] += (sample * amplitude) >> 4;
                    destReverb[1] += (sample * amplitudeReverb) >> 4;
                    destChorus[1] += (sample * amplitudeChorus) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    dest[2] += (sample * amplitude) >> 4;
                    destReverb[2] += (sample * amplitudeReverb) >> 4;
                    destChorus[2] += (sample * amplitudeChorus) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    dest[3] += (sample * amplitude) >> 4;
                    destReverb[3] += (sample * amplitudeReverb) >> 4;
                    destChorus[3] += (sample * amplitudeChorus) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    dest += 4;
                    destReverb += 4;
                    destChorus += 4;
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
                    amplitudeReverb = (amplitude >> 7) * this_voice->reverbLevel;
                    amplitudeChorus = (amplitude >> 7) * this_voice->chorusLevel;
                    for (inner = 0; inner < 4; inner++)
                    {
                        int32_t pos = (int32_t)cur_wave_i;
                        int32_t prev_pos = (pos > 0) ? pos - 1 : 0;
                        int32_t s0 = source[prev_pos*2] + source[prev_pos*2 + 1];
                        int32_t s1 = source[pos*2] + source[pos*2 + 1];
                        int32_t s2 = source[(pos+1)*2] + source[(pos+1)*2 + 1];
                        int32_t s3 = source[(pos+2)*2] + source[(pos+2)*2 + 1];
                        sample = PV_HQ16_AUTO(s0, s1, s2, s3, cur_wave_f);
                        *dest += (sample * amplitude) >> 5;
                        *destReverb += (sample * amplitudeReverb) >> 5;
                        *destChorus += (sample * amplitudeChorus) >> 5;
                        dest++;
                        destReverb++;
                        destChorus++;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    }
                    this_voice->lastAmplitudeL += amplitudeAdjust; amplitude = this_voice->lastAmplitudeL >> 4;
                }
            }
            else
            {
                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    amplitudeReverb = (amplitude >> 7) * this_voice->reverbLevel;
                    amplitudeChorus = (amplitude >> 7) * this_voice->chorusLevel;

                    for (inner = 0; inner < 4; inner++)
                    {
                        calculated_source = source + cur_wave_i * 2;
                        b = calculated_source[0] + calculated_source[1];
                        c = calculated_source[2] + calculated_source[3];
                        sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                        *dest += (sample  * amplitude) >> 5;
                        *destReverb += (sample  * amplitudeReverb) >> 5;
                        *destChorus += (sample  * amplitudeChorus) >> 5;
                        dest++;
                        destReverb++;
                        destChorus++;

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

void PV_ServeU3232PartialBuffer16NewReverb (GM_Voice *this_voice, bool looping)
{
    register int32_t          *dest;
    register int32_t          *destReverb, *destChorus;
    register int32_t      a, inner;
    register int16_t          *source;
    register int32_t          b, c, sample;
    register U32            cur_wave_i, cur_wave_f;
    register U32            end_wave, wave_adjust = 0;
    U3232                   wave_increment;
    register int32_t          amplitude, amplitudeAdjust;
    register int32_t          amplitudeReverb, amplitudeChorus;

    amplitude = this_voice->lastAmplitudeL;
    amplitudeAdjust = (this_voice->NoteVolume * this_voice->NoteVolumeEnvelope) >> VOLUME_PRECISION_SCALAR;
    /* Ramp in full precision (lastAmplitude is the accumulator) and shift only for the
       multiply. Shifting the step itself truncated it to 0 below Four_Loop<<4, so quiet
       voices stuck at a fixed level or dropped out. */
    amplitudeAdjust = (amplitudeAdjust - amplitude) / MusicGlobals->Four_Loop;
    amplitude = amplitude >> 4;

    dest = &MusicGlobals->songBufferDry[0];
    destReverb = &MusicGlobals->songBufferReverb[0];
    destChorus = &MusicGlobals->songBufferChorus[0];
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
                int32_t loopStartIdx = (int32_t)(this_voice->NoteLoopPtr - this_voice->NotePtr);
                int32_t loopEndIdx = (int32_t)(this_voice->NoteLoopEnd - this_voice->NotePtr);
                int32_t totalFrames = (int32_t)(this_voice->NotePtrEnd - this_voice->NotePtr);

                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    amplitudeReverb = (amplitude >> 7) * this_voice->reverbLevel;
                    amplitudeChorus = (amplitude >> 7) * this_voice->chorusLevel;
                    for (inner = 0; inner < 4; inner++)
                    {
                        THE_CHECK_U3232(int16_t *);
                        int32_t pos = (int32_t)cur_wave_i;
                        int32_t s1 = source[pos];
                        int32_t s0, s2, s3;
                        if (looping)
                        {
                            s0 = PV_LoopWrapSample16(source, pos - 1, loopStartIdx, loopEndIdx);
                            s2 = PV_LoopWrapSample16(source, pos + 1, loopStartIdx, loopEndIdx);
                            s3 = PV_LoopWrapSample16(source, pos + 2, loopStartIdx, loopEndIdx);
                        }
                        else
                        {
                            s0 = PV_GetSample16WithBounds(source, pos - 1, totalFrames);
                            s1 = PV_GetSample16WithBounds(source, pos, totalFrames);
                            s2 = PV_GetSample16WithBounds(source, pos + 1, totalFrames);
                            s3 = PV_GetSample16WithBounds(source, pos + 2, totalFrames);
                        }
                        sample = PV_HQ16_AUTO(s0, s1, s2, s3, cur_wave_f);
                        dest[0] += (sample * amplitude) >> 4;
                        destReverb[0] += (sample * amplitudeReverb) >> 4;
                        destChorus[0] += (sample * amplitudeChorus) >> 4;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                        dest++;
                        destReverb++;
                        destChorus++;
                    }
                    this_voice->lastAmplitudeL += amplitudeAdjust; amplitude = this_voice->lastAmplitudeL >> 4;
                }
            }
            else
            {
                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    amplitudeReverb = (amplitude >> 7) * this_voice->reverbLevel;
                    amplitudeChorus = (amplitude >> 7) * this_voice->chorusLevel;

#if 1   //MOE'S OBSESSIVE FOLLY
                    for (inner = 0; inner < 4; inner++)
                    {
                        U32 next_wave_i;
                        THE_CHECK_U3232(int16_t *);
                        next_wave_i = cur_wave_i + 1;
                        if (looping && (next_wave_i >= end_wave))
                        {
                            next_wave_i -= wave_adjust;
                        }
                        b = source[cur_wave_i];
                        c = source[next_wave_i];
                        sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                        dest[0] += (sample * amplitude) >> 4;
                        destReverb[0] += (sample * amplitudeReverb) >> 4;
                        destChorus[0] += (sample * amplitudeChorus) >> 4;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                        dest++;
                        destReverb++;
                        destChorus++;
                    }
#else
                    THE_CHECK_U3232(int16_t *);
            int32_t loopStartIdx = (int32_t)((this_voice->NoteLoopPtr - this_voice->NotePtr) >> 1);
            int32_t loopEndIdx = (int32_t)((this_voice->NoteLoopEnd - this_voice->NotePtr) >> 1);
            int32_t totalFrames = (int32_t)((this_voice->NotePtrEnd - this_voice->NotePtr) >> 1);
                    dest[0] += (sample * amplitude) >> 4;
                    destReverb[0] += (sample * amplitudeReverb) >> 4;
                    destChorus[0] += (sample * amplitudeChorus) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                            
                    THE_CHECK_U3232(int16_t *);
                                U32 next_wave_i = cur_wave_i + 1;
                                if (looping && (next_wave_i >= end_wave))
                                {
                                    next_wave_i -= wave_adjust;
                                }
                                b = source[cur_wave_i * 2];
                                c = source[next_wave_i * 2];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    dest[1] += (sample * amplitude) >> 4;
                    destReverb[1] += (sample * amplitudeReverb) >> 4;
                    destChorus[1] += (sample * amplitudeChorus) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                                c = source[next_wave_i * 2 + 1];
                    THE_CHECK_U3232(int16_t *);
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    dest[2] += (sample * amplitude) >> 4;
                    destReverb[2] += (sample * amplitudeReverb) >> 4;
                    destChorus[2] += (sample * amplitudeChorus) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    THE_CHECK_U3232(int16_t *);
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    dest[3] += (sample * amplitude) >> 4;
                    destReverb[3] += (sample * amplitudeReverb) >> 4;
                    destChorus[3] += (sample * amplitudeChorus) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    dest += 4;
                    destReverb += 4;
                    destChorus += 4;
#endif
                    this_voice->lastAmplitudeL += amplitudeAdjust; amplitude = this_voice->lastAmplitudeL >> 4;
                }
            }
        }
        else
        {
            int32_t totalFrames = (int32_t)(this_voice->NotePtrEnd - this_voice->NotePtr);
            if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
            {
                int32_t loopStartIdx = (int32_t)(this_voice->NoteLoopPtr - this_voice->NotePtr);
                int32_t loopEndIdx = (int32_t)(this_voice->NoteLoopEnd - this_voice->NotePtr);

                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    amplitudeReverb = (amplitude >> 7) * this_voice->reverbLevel;
                    amplitudeChorus = (amplitude >> 7) * this_voice->chorusLevel;
                    for (inner = 0; inner < 4; inner++)
                    {
                        THE_CHECK_U3232(int16_t *);
                        int32_t pos = (int32_t)cur_wave_i;
                        int32_t idx0, idx1, idx2, idx3;
                        if (looping)
                        {
                            int32_t loopLen = loopEndIdx - loopStartIdx;
                            idx0 = pos - 1;
                            if (idx0 < loopStartIdx) idx0 += loopLen;
                            idx1 = pos;
                            idx2 = pos + 1;
                            if (idx2 >= loopEndIdx) idx2 -= loopLen;
                            idx3 = pos + 2;
                            if (idx3 >= loopEndIdx) idx3 -= loopLen;
                        }
                        else
                        {
                            idx0 = (pos > 0) ? pos - 1 : 0;
                            idx1 = pos;
                            idx2 = pos + 1;
                            idx3 = pos + 2;
                            if (idx2 >= totalFrames) idx2 = totalFrames - 1;
                            if (idx3 >= totalFrames) idx3 = totalFrames - 1;
                        }
                        int32_t s0 = source[idx0*2] + source[idx0*2 + 1];
                        int32_t s1 = source[idx1*2] + source[idx1*2 + 1];
                        int32_t s2 = source[idx2*2] + source[idx2*2 + 1];
                        int32_t s3 = source[idx3*2] + source[idx3*2 + 1];
                        sample = PV_HQ16_AUTO(s0, s1, s2, s3, cur_wave_f);
                        *dest += ((sample >> 1) * amplitude) >> 5;
                        *destReverb += ((sample >> 1) * amplitudeReverb) >> 5;
                        *destChorus++ += ((sample >> 1) * amplitudeChorus) >> 5;
                        dest++; destReverb++;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    }
                    this_voice->lastAmplitudeL += amplitudeAdjust; amplitude = this_voice->lastAmplitudeL >> 4;
                }
            }
            else
            {
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                amplitudeReverb = (amplitude >> 7) * this_voice->reverbLevel;
                amplitudeChorus = (amplitude >> 7) * this_voice->chorusLevel;

                for (inner = 0; inner < 4; inner++)
                {
                    U32 next_wave_i;
                    THE_CHECK_U3232(int16_t *);
                    next_wave_i = cur_wave_i + 1;
                        if (looping && (next_wave_i >= end_wave))
                        {
                            next_wave_i -= wave_adjust;
                        }
                        b = source[cur_wave_i * 2] + source[cur_wave_i * 2 + 1];
                        c = source[next_wave_i * 2] + source[next_wave_i * 2 + 1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    *dest += ((sample >> 1) * amplitude) >> 5;
                    *destReverb += ((sample >> 1) * amplitudeReverb) >> 5;
                    *destChorus++ += ((sample >> 1) * amplitudeChorus) >> 5;
                    dest++; destReverb++;
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

void PV_ServeU3232StereoFullBuffer16NewReverb (GM_Voice *this_voice)
{
    register int32_t          *destL;
    register int32_t          *destReverb, *destChorus;
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
    register int32_t          amplitudeReverb, amplitudeChorus;

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
    destReverb = &MusicGlobals->songBufferReverb[0];
    destChorus = &MusicGlobals->songBufferChorus[0];
    cur_wave_i = this_voice->samplePosition.i;
    cur_wave_f = this_voice->samplePosition.f;

    source = (int16_t *) this_voice->NotePtr;

    wave_increment = PV_GetWavePitchU3232(this_voice->NotePitch);

    {
        if (this_voice->channels == 1)
        {   // mono instrument
            if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
            {
                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    amplitudeReverb = ((amplitudeL + amplitudeR) >> 8) * this_voice->reverbLevel;
                    amplitudeChorus = ((amplitudeL + amplitudeR) >> 8) * this_voice->chorusLevel;
                    for (inner = 0; inner < 4; inner++)
                    {
                        int32_t pos = (int32_t)cur_wave_i;
                        int32_t s0 = source[(pos > 0) ? pos - 1 : 0];
                        int32_t s1 = source[pos];
                        int32_t s2 = source[pos + 1];
                        int32_t s3 = source[pos + 2];
                        sample = PV_HQ16_AUTO(s0, s1, s2, s3, cur_wave_f);
                        destL[0] += (sample * amplitudeL) >> 4;
                        destL[1] += (sample * amplitudeR) >> 4;
                        *destReverb += (sample * amplitudeReverb) >> 4;
                        *destChorus += (sample * amplitudeChorus) >> 4;
                        destL += 2;
                        destReverb++;
                        destChorus++;
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
                    amplitudeReverb = ((amplitudeL + amplitudeR) >> 8) * this_voice->reverbLevel;
                    amplitudeChorus = ((amplitudeL + amplitudeR) >> 8) * this_voice->chorusLevel;

                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    destL[0] += (sample * amplitudeL) >> 4;
                    destL[1] += (sample * amplitudeR) >> 4;
                    destReverb[0] += (sample * amplitudeReverb) >> 4;
                    destChorus[0] += (sample * amplitudeChorus) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    destL[2] += (sample * amplitudeL) >> 4;
                    destL[3] += (sample * amplitudeR) >> 4;
                    destReverb[1] += (sample * amplitudeReverb) >> 4;
                    destChorus[1] += (sample * amplitudeChorus) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    destL[4] += (sample * amplitudeL) >> 4;
                    destL[5] += (sample * amplitudeR) >> 4;
                    destReverb[2] += (sample * amplitudeReverb) >> 4;
                    destChorus[2] += (sample * amplitudeChorus) >> 4;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);

                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                    destL[6] += (sample * amplitudeL) >> 4;
                    destL[7] += (sample * amplitudeR) >> 4;
                    destReverb[3] += (sample * amplitudeReverb) >> 4;
                    destChorus[3] += (sample * amplitudeChorus) >> 4;
                    destL += 8;
                    destReverb += 4;
                    destChorus += 4;

                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
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
                    amplitudeReverb = ((amplitudeL + amplitudeR) >> 8) * this_voice->reverbLevel;
                    amplitudeChorus = ((amplitudeL + amplitudeR) >> 8) * this_voice->chorusLevel;
                    for (inner = 0; inner < 4; inner++)
                    {
                        int32_t pos = (int32_t)cur_wave_i;
                        int32_t prev_pos = (pos > 0) ? pos - 1 : 0;
                        int32_t sL0 = source[prev_pos*2];
                        int32_t sL1 = source[pos*2];
                        int32_t sL2 = source[(pos+1)*2];
                        int32_t sL3 = source[(pos+2)*2];
                        int32_t sampleL = PV_HQ16_CH(sL0, sL1, sL2, sL3, cur_wave_f, 0);
                        destL[0] += (sampleL * amplitudeL) >> 4;
                        *destReverb += (sampleL * amplitudeReverb) >> 5;
                        *destChorus += (sampleL * amplitudeChorus) >> 5;
                        int32_t sR0 = source[prev_pos*2 + 1];
                        int32_t sR1 = source[pos*2 + 1];
                        int32_t sR2 = source[(pos+1)*2 + 1];
                        int32_t sR3 = source[(pos+2)*2 + 1];
                        int32_t sampleR = PV_HQ16_CH(sR0, sR1, sR2, sR3, cur_wave_f, 1);
                        destL[1] += (sampleR * amplitudeR) >> 4;
                        *destReverb += (sampleR * amplitudeReverb) >> 5;
                        *destChorus += (sampleR * amplitudeChorus) >> 5;
                        destL += 2;
                        destReverb++;
                        destChorus++;
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
                    amplitudeReverb = ((amplitudeL + amplitudeR) >> 8) * this_voice->reverbLevel;
                    amplitudeChorus = ((amplitudeL + amplitudeR) >> 8) * this_voice->chorusLevel;

                    for (inner = 0; inner < 4; inner++)
                    {
                        calculated_source = source + cur_wave_i * 2;
                        b = calculated_source[0];
                        c = calculated_source[2];
                        sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                        destL[0] += (sample * amplitudeL) >> 4;
                        *destReverb += (sample * amplitudeReverb) >> 5;
                        *destChorus += (sample * amplitudeChorus) >> 5;
                        b = calculated_source[1];
                        c = calculated_source[3];
                        sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                        destL[1] += (sample * amplitudeR) >> 4;
                        *destReverb += (sample * amplitudeReverb) >> 5;
                        *destChorus += (sample * amplitudeChorus) >> 5;
                        destL += 2;
                        destReverb++;
                        destChorus++;

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

void PV_ServeU3232StereoPartialBuffer16NewReverb (GM_Voice *this_voice, bool looping)
{
    register int32_t          *destL;
    register int32_t          *destReverb, *destChorus;
    register int32_t      a, inner;
    register int16_t          *source;
    register int32_t          b, c, sample;
    register U32            cur_wave_i, cur_wave_f;
    register U32            end_wave, wave_adjust = 0;
    U3232                   wave_increment;
    int32_t                   ampValueL, ampValueR;
    register int32_t          amplitudeL;
    register int32_t          amplitudeR;
    register int32_t          amplitudeLincrement, amplitudeRincrement;
    register int32_t          amplitudeReverb, amplitudeChorus;

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
    destReverb = &MusicGlobals->songBufferReverb[0];
    destChorus = &MusicGlobals->songBufferChorus[0];
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
                int32_t loopStartIdx = (int32_t)(this_voice->NoteLoopPtr - this_voice->NotePtr);
                int32_t loopEndIdx = (int32_t)(this_voice->NoteLoopEnd - this_voice->NotePtr);
                int32_t totalFrames = (int32_t)(this_voice->NotePtrEnd - this_voice->NotePtr);

                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    amplitudeReverb = ((amplitudeL + amplitudeR) >> 8) * this_voice->reverbLevel;
                    amplitudeChorus = ((amplitudeL + amplitudeR) >> 8) * this_voice->chorusLevel;
                    for (inner = 0; inner < 4; inner++)
                    {
                        THE_CHECK_U3232(int16_t *);
                        int32_t pos = (int32_t)cur_wave_i;
                        int32_t s1 = source[pos];
                        int32_t s0, s2, s3;
                        if (looping)
                        {
                            s0 = PV_LoopWrapSample16(source, pos - 1, loopStartIdx, loopEndIdx);
                            s2 = PV_LoopWrapSample16(source, pos + 1, loopStartIdx, loopEndIdx);
                            s3 = PV_LoopWrapSample16(source, pos + 2, loopStartIdx, loopEndIdx);
                        }
                        else
                        {
                            s0 = PV_GetSample16WithBounds(source, pos - 1, totalFrames);
                            s1 = PV_GetSample16WithBounds(source, pos, totalFrames);
                            s2 = PV_GetSample16WithBounds(source, pos + 1, totalFrames);
                            s3 = PV_GetSample16WithBounds(source, pos + 2, totalFrames);
                        }
                        sample = PV_HQ16_AUTO(s0, s1, s2, s3, cur_wave_f);
                        destL[0] += (sample * amplitudeL) >> 4;
                        destL[1] += (sample * amplitudeR) >> 4;
                        *destReverb += (sample * amplitudeReverb) >> 4;
                        *destChorus += (sample * amplitudeChorus) >> 4;
                        destL += 2;
                        destReverb++;
                        destChorus++;
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
                    amplitudeReverb = ((amplitudeL + amplitudeR) >> 8) * this_voice->reverbLevel;
                    amplitudeChorus = ((amplitudeL + amplitudeR) >> 8) * this_voice->chorusLevel;

                    for (inner = 0; inner < 4; inner++)
                    {
                        U32 next_wave_i;
                        THE_CHECK_U3232(int16_t *);
                        next_wave_i = cur_wave_i + 1;
                        if (looping && (next_wave_i >= end_wave))
                        {
                            next_wave_i -= wave_adjust;
                        }
                        b = source[cur_wave_i];
                        c = source[next_wave_i];
                        sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                        destL[0] += (sample * amplitudeL) >> 4;
                        destL[1] += (sample * amplitudeR) >> 4;
                        *destReverb += (sample * amplitudeReverb) >> 4;
                        *destChorus += (sample * amplitudeChorus) >> 4;
                        destL += 2;
                        destReverb++;
                        destChorus++;
                        ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    }
                    this_voice->lastAmplitudeL += amplitudeLincrement; amplitudeL = this_voice->lastAmplitudeL >> 4;
                    this_voice->lastAmplitudeR += amplitudeRincrement; amplitudeR = this_voice->lastAmplitudeR >> 4;
                }
            }
        }
        else
        {   // Stereo 16 bit instrument
            int32_t totalFrames = (int32_t)(this_voice->NotePtrEnd - this_voice->NotePtr);
            if (PV_U3232UseSincInterp() || this_voice->advancedInterpolation)
            {
                int32_t loopStartIdx = (int32_t)(this_voice->NoteLoopPtr - this_voice->NotePtr);
                int32_t loopEndIdx = (int32_t)(this_voice->NoteLoopEnd - this_voice->NotePtr);

                for (a = MusicGlobals->Four_Loop; a > 0; --a)
                {
                    amplitudeReverb = ((amplitudeL + amplitudeR) >> 8) * this_voice->reverbLevel;
                    amplitudeChorus = ((amplitudeL + amplitudeR) >> 8) * this_voice->chorusLevel;
                    for (inner = 0; inner < 4; inner++)
                    {
                        THE_CHECK_U3232(int16_t *);
                        int32_t pos = (int32_t)cur_wave_i;
                        int32_t idx0, idx1, idx2, idx3;
                        if (looping)
                        {
                            int32_t loopLen = loopEndIdx - loopStartIdx;
                            idx0 = pos - 1;
                            if (idx0 < loopStartIdx) idx0 += loopLen;
                            idx1 = pos;
                            idx2 = pos + 1;
                            if (idx2 >= loopEndIdx) idx2 -= loopLen;
                            idx3 = pos + 2;
                            if (idx3 >= loopEndIdx) idx3 -= loopLen;
                        }
                        else
                        {
                            idx0 = (pos > 0) ? pos - 1 : 0;
                            idx1 = pos;
                            idx2 = pos + 1;
                            idx3 = pos + 2;
                            if (idx2 >= totalFrames) idx2 = totalFrames - 1;
                            if (idx3 >= totalFrames) idx3 = totalFrames - 1;
                        }
                        int32_t sL0 = source[idx0*2];
                        int32_t sL1 = source[idx1*2];
                        int32_t sL2 = source[idx2*2];
                        int32_t sL3 = source[idx3*2];
                        int32_t sampleL = PV_HQ16_CH(sL0, sL1, sL2, sL3, cur_wave_f, 0);
                        destL[0] += (sampleL * amplitudeL) >> 4;
                        *destReverb += (sampleL * amplitudeReverb) >> 5;
                        *destChorus += (sampleL * amplitudeChorus) >> 5;
                        int32_t sR0 = source[idx0*2 + 1];
                        int32_t sR1 = source[idx1*2 + 1];
                        int32_t sR2 = source[idx2*2 + 1];
                        int32_t sR3 = source[idx3*2 + 1];
                        int32_t sampleR = PV_HQ16_CH(sR0, sR1, sR2, sR3, cur_wave_f, 1);
                        destL[1] += (sampleR * amplitudeR) >> 4;
                        *destReverb += (sampleR * amplitudeReverb) >> 5;
                        *destChorus += (sampleR * amplitudeChorus) >> 5;
                        destL += 2;
                        destReverb++;
                        destChorus++;
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
                    amplitudeReverb = ((amplitudeL + amplitudeR) >> 8) * this_voice->reverbLevel;
                    amplitudeChorus = ((amplitudeL + amplitudeR) >> 8) * this_voice->chorusLevel;

                    for (inner = 0; inner < 4; inner++)
                    {
                        U32 next_wave_i;
                        THE_CHECK_U3232(int16_t *);
                        next_wave_i = cur_wave_i + 1;
                        if (looping && (next_wave_i >= end_wave))
                        {
                            next_wave_i -= wave_adjust;
                        }
                        b = source[cur_wave_i * 2];
                        c = source[next_wave_i * 2];
                        sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                        destL[0] += (sample * amplitudeL) >> 4;
                        *destReverb += (sample * amplitudeReverb) >> 5;
                        *destChorus += (sample * amplitudeChorus) >> 5;
                        b = source[cur_wave_i * 2 + 1];
                        c = source[next_wave_i * 2 + 1];
                        sample = (((int32_t)(cur_wave_f >> 17) * (int32_t)(c-b)) >> 15) + b;
                        destL[1] += (sample * amplitudeR) >> 4;
                        *destReverb += (sample * amplitudeReverb) >> 5;
                        *destChorus += (sample * amplitudeChorus) >> 5;
                        destL += 2;
                        destReverb++;
                        destChorus++;
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

#endif  // REVERB_USED and LOOPS_USED
