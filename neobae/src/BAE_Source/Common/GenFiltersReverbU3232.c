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
**
** "GenFiltersReverbU3232.c"
**
**  Generalized Music Synthesis package. Part of SoundMusicSys.
**
**  � Copyright 1995-2000 Beatnik, Inc, All Rights Reserved.
**  Written by Jim Nitchals
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
**  3/11/99     MOE: created file from GenFiltersReverbFloat.c
**  10/19/99    MSD: switched to REVERB_USED and LOOPS_USED
**  2/4/2000    Changed copyright. We're Y2K compliant!
**  6/11/2001   sh  Removed katmai code
*/
/*****************************************************************************/

#include "GenSnd.h"
#include "GenPriv.h"
#include <stdint.h>

#if ((REVERB_USED == VARIABLE_REVERB) && (LOOPS_USED == U3232_LOOPS))

#include "GenInterpCubicU3232.inc"

#define CLIP(LIMIT_VAR, LIMIT_LOWER, LIMIT_UPPER) if (LIMIT_VAR < LIMIT_LOWER) LIMIT_VAR = LIMIT_LOWER; if (LIMIT_VAR > LIMIT_UPPER) LIMIT_VAR = LIMIT_UPPER;
#define GET_FILTER_PARAMS \
    CLIP (this_voice->LPF_frequency, 0x200, MAXRESONANCE*256);  \
    if (this_voice->previous_zFrequency == 0)\
        this_voice->previous_zFrequency = this_voice->LPF_frequency;\
    CLIP (this_voice->LPF_resonance, 0, 0x100);\
    CLIP (this_voice->LPF_lowpassAmount, -0xFF, 0xFF);\
    Z1 = this_voice->LPF_lowpassAmount << 8;\
    if (Z1 < 0)\
        Xn = 65536 + Z1;\
    else\
        Xn = 65536 - Z1;\
    if (Z1 >= 0)\
    {\
        Zn = ((0x10000 - Z1) * this_voice->LPF_resonance) >> 8;\
        Zn = -Zn;\
    }\
    else\
        Zn = 0;



void PV_ServeU3232FilterPartialBufferNewReverb (GM_Voice *this_voice, bool looping)
{
    register int32_t          *destL;
    register int32_t          *destReverb, *destChorus;
    register unsigned char          *source;
#if 1   // MOE'S OBSESSIVE FOLLY
    register int32_t          b, c;
#else
    register unsigned char          b, c;
#endif
    register U32            cur_wave_i, cur_wave_f;
    register U32            end_wave, wave_adjust = 0;
    U3232                   wave_increment;
    register int32_t          amplitudeL, amplitudeReverb, amplitudeChorus;
    register int32_t          inner;
    int32_t                   amplitudeLincrement;
    int32_t                   ampValueL;
    int32_t                   a;
    register int16_t          *z;
    register int32_t          Z1value, zIndex1, zIndex2, Xn, Z1, Zn, sample;

    z = this_voice->z;
    Z1value = this_voice->Z1value;
    zIndex2 = this_voice->zIndex;

    GET_FILTER_PARAMS

    amplitudeL = this_voice->lastAmplitudeL;
    ampValueL = (this_voice->NoteVolume * this_voice->NoteVolumeEnvelope) >> VOLUME_PRECISION_SCALAR;
    /* Ramp in full precision (lastAmplitude is the accumulator) and shift only for the
       multiply. Shifting the step itself truncated it to 0 below Four_Loop<<2, so quiet
       voices stuck at a fixed level or dropped out. */
    amplitudeLincrement = (ampValueL - amplitudeL) / MusicGlobals->Four_Loop;
    
    amplitudeL = amplitudeL >> 2;

    destL = &MusicGlobals->songBufferDry[0];
    destReverb = &MusicGlobals->songBufferReverb[0];
    destChorus = &MusicGlobals->songBufferChorus[0];
    source = this_voice->NotePtr;
    cur_wave_i = this_voice->samplePosition.i;
    cur_wave_f = this_voice->samplePosition.f;

    wave_increment = PV_GetWavePitchU3232(this_voice->NotePitch);

    if (looping)
    {
        end_wave = (this_voice->NoteLoopEnd - this_voice->NotePtr);
        wave_adjust = (this_voice->NoteLoopEnd - this_voice->NoteLoopPtr);
    }
    else

    {
        end_wave = (this_voice->NotePtrEnd - this_voice->NotePtr - 1);
    }
    {
        if (this_voice->LPF_resonance == 0)
        {
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                amplitudeReverb = (amplitudeL >> 7) * this_voice->reverbLevel;
                amplitudeChorus = (amplitudeL >> 7) * this_voice->chorusLevel;

                for (inner = 0; inner < 4; inner++)
                {
                    THE_CHECK_U3232(unsigned char *);
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = ((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80) << 2;
                    sample = (sample * Xn + Z1value * Z1) >> 16;
                    Z1value = sample - (sample >> 9);   // remove DC bias
                    *destL += sample * amplitudeL;
                    destL++;
                    *destReverb += sample * amplitudeReverb;
                    destReverb++;
                    *destChorus++ += sample * amplitudeChorus;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                }
                this_voice->lastAmplitudeL += amplitudeLincrement; amplitudeL = this_voice->lastAmplitudeL >> 2;
            }
        }
        else
        {
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                this_voice->previous_zFrequency += (this_voice->LPF_frequency - this_voice->previous_zFrequency) >> BAE_FILTER_GLIDE_BLOCK(5);
                zIndex1 = zIndex2 - (this_voice->previous_zFrequency >> 8);
                amplitudeReverb = (amplitudeL >> 7) * this_voice->reverbLevel;
                amplitudeChorus = (amplitudeL >> 7) * this_voice->chorusLevel;

                for (inner = 0; inner < 4; inner++)
                {
                    THE_CHECK_U3232(unsigned char *);
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = ((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80) << 2;
                    sample = (sample * Xn + Z1value * Z1 + z[zIndex1 & MAXRESONANCE] * Zn) >> 16;
                    zIndex1++;
                    z[zIndex2 & MAXRESONANCE] = (int16_t)sample;
                    zIndex2++;
                    Z1value = sample - (sample >> 9);
                    *destL += sample * amplitudeL;
                    destL++;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    *destReverb += sample * amplitudeReverb;
                    destReverb++;
                    *destChorus++ += sample * amplitudeChorus;
                }
                this_voice->lastAmplitudeL += amplitudeLincrement; amplitudeL = this_voice->lastAmplitudeL >> 2;
            }
        }
    }
    this_voice->Z1value = Z1value;
    this_voice->zIndex = zIndex2;
    this_voice->samplePosition.i = cur_wave_i;
    this_voice->samplePosition.f = cur_wave_f;
    /* lastAmplitudeL holds the full-precision ramp end. */
FINISH:
    return;
}

void PV_ServeU3232StereoFilterPartialBufferNewReverb (GM_Voice *this_voice, bool looping)
{
    register int32_t          *destL;
    register int32_t          *destReverb, *destChorus;
    register unsigned char          *source;
#if 1   // MOE'S OBSESSIVE FOLLY
    register int32_t          b, c;
#else
    register unsigned char          b, c;
#endif
    register U32            cur_wave_i, cur_wave_f;
    register U32            end_wave, wave_adjust = 0;
    U3232                   wave_increment;
    register int32_t          amplitudeL;
    register int32_t          amplitudeR;
    register int32_t          amplitudeReverb, amplitudeChorus;
    register int32_t          inner;
    int32_t                   amplitudeLincrement, amplitudeRincrement;
    int32_t                   ampValueL, ampValueR;
    int32_t                   a;
    register int16_t          *z;
    register int32_t          Z1value, zIndex1, zIndex2, Xn, Z1, Zn, sample;

    z = this_voice->z;
    Z1value = this_voice->Z1value;
    zIndex2 = this_voice->zIndex;

    GET_FILTER_PARAMS

    PV_CalculateStereoVolume(this_voice, &ampValueL, &ampValueR);

    amplitudeL = this_voice->lastAmplitudeL;
    amplitudeR = this_voice->lastAmplitudeR;
    /* Ramp in full precision (lastAmplitude is the accumulator) and shift only for the
       multiply. Shifting the step itself truncated it to 0 below Four_Loop<<2, so quiet
       voices stuck at a fixed level or dropped out. */
    amplitudeLincrement = (ampValueL - amplitudeL) / MusicGlobals->Four_Loop;
    amplitudeRincrement = (ampValueR - amplitudeR) / MusicGlobals->Four_Loop;

    amplitudeL = amplitudeL >> 2;
    amplitudeR = amplitudeR >> 2;

    destL = &MusicGlobals->songBufferDry[0];
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
    }
    else
    {
        end_wave = this_voice->NotePtrEnd - this_voice->NotePtr - 1;
    }

    {
        if (this_voice->LPF_resonance == 0)
        {
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                amplitudeReverb = ((amplitudeL + amplitudeR) >> 8) * this_voice->reverbLevel;
                amplitudeChorus = ((amplitudeL + amplitudeR) >> 8) * this_voice->chorusLevel;

                for (inner = 0; inner < 4; inner++)
                {
                    THE_CHECK_U3232(unsigned char *);
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (int32_t)(((unsigned int)((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16)) + b - 0x80) << 2);
                    sample = (sample * Xn + Z1value * Z1) >> 16;
                    Z1value = sample - (sample >> 9);
                    destL[0] += sample * amplitudeL;
                    destL[1] += sample * amplitudeR;
                    destL += 2;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    *destReverb += sample * amplitudeReverb;
                    destReverb++;
                    *destChorus++ += sample * amplitudeChorus;
                }
                this_voice->lastAmplitudeL += amplitudeLincrement; amplitudeL = this_voice->lastAmplitudeL >> 2;
                this_voice->lastAmplitudeR += amplitudeRincrement; amplitudeR = this_voice->lastAmplitudeR >> 2;
            }
        }
        else
        {
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                zIndex1 = zIndex2 - (this_voice->previous_zFrequency >> 8);
                this_voice->previous_zFrequency += (this_voice->LPF_frequency - this_voice->previous_zFrequency) >> BAE_FILTER_GLIDE_BLOCK(3);
                amplitudeReverb = ((amplitudeL + amplitudeR) >> 8) * this_voice->reverbLevel;
                amplitudeChorus = ((amplitudeL + amplitudeR) >> 8) * this_voice->chorusLevel;

                for (inner = 0; inner < 4; inner++)
                {
                    THE_CHECK_U3232(unsigned char *);
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (int32_t)(((unsigned int)((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80)) << 2);
                    sample = (sample * Xn + Z1value * Z1 + z[zIndex1 & MAXRESONANCE] * Zn) >> 16;
                    zIndex1++;
                    z[zIndex2 & MAXRESONANCE] = (int16_t)sample;
                    zIndex2++;
                    Z1value = sample - (sample >> 9);
                    destL[0] += sample * amplitudeL;
                    destL[1] += sample * amplitudeR;
                    destL += 2;
                    *destReverb += sample * amplitudeReverb;
                    destReverb++;
                    *destChorus++ += sample * amplitudeChorus;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                }
                this_voice->lastAmplitudeL += amplitudeLincrement; amplitudeL = this_voice->lastAmplitudeL >> 2;
                this_voice->lastAmplitudeR += amplitudeRincrement; amplitudeR = this_voice->lastAmplitudeR >> 2;
            }
        }
    }
    this_voice->Z1value = Z1value;
    this_voice->zIndex = zIndex2;
    this_voice->samplePosition.i = cur_wave_i;
    this_voice->samplePosition.f = cur_wave_f;
    /* lastAmplitudeL holds the full-precision ramp end. */
    /* lastAmplitudeR holds the full-precision ramp end. */
FINISH:
    return;
}


void PV_ServeU3232FilterFullBufferNewReverb (GM_Voice *this_voice)
{
    register int32_t          *destL;
    register int32_t          *destReverb, *destChorus;
    register unsigned char          *source;
#if 1   // MOE'S OBSESSIVE FOLLY
    register int32_t          b, c;
#else
    register unsigned char          b, c;
#endif
    register U32            cur_wave_i, cur_wave_f;
    U3232                   wave_increment;
    register int32_t          amplitudeL;
    register int32_t          amplitudeReverb, amplitudeChorus;
    register int32_t          inner;
    int32_t                   amplitudeLincrement;
    int32_t                   ampValueL;
    int32_t                   a;
    register int16_t          *z;
    register int32_t          Z1value, zIndex1, zIndex2, Xn, Z1, Zn, sample;

    z = this_voice->z;
    Z1value = this_voice->Z1value;
    zIndex2 = this_voice->zIndex;

    GET_FILTER_PARAMS

    amplitudeL = this_voice->lastAmplitudeL;
    ampValueL = (this_voice->NoteVolume * this_voice->NoteVolumeEnvelope) >> VOLUME_PRECISION_SCALAR;
    /* Ramp in full precision (lastAmplitude is the accumulator) and shift only for the
       multiply. Shifting the step itself truncated it to 0 below Four_Loop<<2, so quiet
       voices stuck at a fixed level or dropped out. */
    amplitudeLincrement = (ampValueL - amplitudeL) / MusicGlobals->Four_Loop;
    
    amplitudeL = amplitudeL >> 2;

    destL = &MusicGlobals->songBufferDry[0];
    destReverb = &MusicGlobals->songBufferReverb[0];
    destChorus = &MusicGlobals->songBufferChorus[0];
    source = this_voice->NotePtr;
    cur_wave_i = this_voice->samplePosition.i;
    cur_wave_f = this_voice->samplePosition.f;

    wave_increment = PV_GetWavePitchU3232(this_voice->NotePitch);

    {
        if (this_voice->LPF_resonance == 0)
        {
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                amplitudeReverb = (amplitudeL * this_voice->reverbLevel) >> 7;
                amplitudeChorus = (amplitudeL * this_voice->chorusLevel) >> 7;

                for (inner = 0; inner < 4; inner++)
                {
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = ((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80) << 2;
                    sample = (sample * Xn + Z1value * Z1) >> 16;
                    Z1value = sample - (sample >> 9);   // remove DC bias
                    *destL += sample * amplitudeL;
                    destL++;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    *destReverb += sample * amplitudeReverb;
                    destReverb++;
                    *destChorus++ += sample * amplitudeChorus;
                }
                this_voice->lastAmplitudeL += amplitudeLincrement; amplitudeL = this_voice->lastAmplitudeL >> 2;
            }
        }
        else
        {
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                this_voice->previous_zFrequency += (this_voice->LPF_frequency - this_voice->previous_zFrequency) >> BAE_FILTER_GLIDE_BLOCK(5);
                zIndex1 = zIndex2 - (this_voice->previous_zFrequency >> 8);
                amplitudeReverb = (amplitudeL * this_voice->reverbLevel) >> 7;
                amplitudeChorus = (amplitudeL * this_voice->chorusLevel) >> 7;

                for (inner = 0; inner < 4; inner++)
                {
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = ((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80) << 2;
                    sample = (sample * Xn + Z1value * Z1 + z[zIndex1 & MAXRESONANCE] * Zn) >> 16;
                    zIndex1++;
                    z[zIndex2 & MAXRESONANCE] = (int16_t)sample;
                    zIndex2++;
                    Z1value = sample - (sample >> 9);
                    *destL += sample * amplitudeL;
                    destL++;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                    *destReverb += sample * amplitudeReverb;
                    destReverb++;
                    *destChorus++ += sample * amplitudeChorus;
                }
                this_voice->lastAmplitudeL += amplitudeLincrement; amplitudeL = this_voice->lastAmplitudeL >> 2;
            }
        }
    }
    this_voice->Z1value = Z1value;
    this_voice->zIndex = zIndex2;
    this_voice->samplePosition.i = cur_wave_i;
    this_voice->samplePosition.f = cur_wave_f;
    /* lastAmplitudeL holds the full-precision ramp end. */
    return;
}



void PV_ServeU3232StereoFilterFullBufferNewReverb (GM_Voice *this_voice)
{
    register int32_t          *destL;
    register int32_t          *destReverb, *destChorus;
    register unsigned char          *source;
#if 1   // MOE'S OBSESSIVE FOLLY
    register int32_t          b, c;
#else
    register unsigned char          b, c;
#endif
    register U32            cur_wave_i, cur_wave_f;
    U3232                   wave_increment;
    register int32_t          amplitudeL;
    register int32_t          amplitudeR;
    register int32_t          amplitudeReverb, amplitudeChorus;
    register int32_t          inner;
    int32_t                   amplitudeLincrement, amplitudeRincrement;
    int32_t                   ampValueL, ampValueR;
    int32_t                   a;
    register                int16_t *z;
    register                int32_t Z1value, zIndex1, zIndex2, Xn, Z1, Zn, sample;

    z = this_voice->z;
    Z1value = this_voice->Z1value;
    zIndex2 = this_voice->zIndex;

    GET_FILTER_PARAMS

    PV_CalculateStereoVolume(this_voice, &ampValueL, &ampValueR);

    amplitudeL = this_voice->lastAmplitudeL;
    amplitudeR = this_voice->lastAmplitudeR;
    /* Ramp in full precision (lastAmplitude is the accumulator) and shift only for the
       multiply. Shifting the step itself truncated it to 0 below Four_Loop<<2, so quiet
       voices stuck at a fixed level or dropped out. */
    amplitudeLincrement = (ampValueL - amplitudeL) / MusicGlobals->Four_Loop;
    amplitudeRincrement = (ampValueR - amplitudeR) / MusicGlobals->Four_Loop;

    amplitudeL = amplitudeL >> 2;
    amplitudeR = amplitudeR >> 2;

    destL = &MusicGlobals->songBufferDry[0];
    destReverb = &MusicGlobals->songBufferReverb[0];
    destChorus = &MusicGlobals->songBufferChorus[0];
    source = this_voice->NotePtr;
    cur_wave_i = this_voice->samplePosition.i;
    cur_wave_f = this_voice->samplePosition.f;

    wave_increment = PV_GetWavePitchU3232(this_voice->NotePitch);

    {
        if (this_voice->LPF_resonance == 0)
        {
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                amplitudeReverb = ((amplitudeL + amplitudeR) * this_voice->reverbLevel) >> 8;
                amplitudeChorus = ((amplitudeL + amplitudeR) * this_voice->chorusLevel) >> 8;

                for (inner = 0; inner < 4; inner++)
                {
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (int32_t)(((unsigned int)(((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80)) << 2));
                    sample = (sample * Xn + Z1value * Z1) >> 16;
                    Z1value = sample - (sample >> 9);
                    destL[0] += sample * amplitudeL;
                    destL[1] += sample * amplitudeR;
                    destL += 2;
                    *destReverb += sample * amplitudeReverb;
                    destReverb++;
                    *destChorus++ += sample * amplitudeChorus;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                }
                this_voice->lastAmplitudeL += amplitudeLincrement; amplitudeL = this_voice->lastAmplitudeL >> 2;
                this_voice->lastAmplitudeR += amplitudeRincrement; amplitudeR = this_voice->lastAmplitudeR >> 2;
            }
        }
        else
        {
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                zIndex1 = zIndex2 - (this_voice->previous_zFrequency >> 8);
                this_voice->previous_zFrequency += (this_voice->LPF_frequency - this_voice->previous_zFrequency) >> BAE_FILTER_GLIDE_BLOCK(3);
                amplitudeReverb = ((amplitudeL + amplitudeR) * this_voice->reverbLevel) >> 8;
                amplitudeChorus = ((amplitudeL + amplitudeR) * this_voice->chorusLevel) >> 8;

                for (inner = 0; inner < 4; inner++)
                {
                    b = source[cur_wave_i];
                    c = source[cur_wave_i+1];
                    sample = (int32_t)(((unsigned int)((((int32_t)(cur_wave_f >> 16) * (int32_t)(c-b)) >> 16) + b - 0x80)) << 2);
                    sample = (sample * Xn + Z1value * Z1 + z[zIndex1 & MAXRESONANCE] * Zn) >> 16;
                    zIndex1++;
                    z[zIndex2 & MAXRESONANCE] = (int16_t)sample;
                    zIndex2++;
                    Z1value = sample - (sample >> 9);
                    destL[0] += sample * amplitudeL;
                    destL[1] += sample * amplitudeR;
                    destL += 2;
                    *destReverb += sample * amplitudeReverb;
                    destReverb++;
                    *destChorus++ += sample * amplitudeChorus;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                }
                this_voice->lastAmplitudeL += amplitudeLincrement; amplitudeL = this_voice->lastAmplitudeL >> 2;
                this_voice->lastAmplitudeR += amplitudeRincrement; amplitudeR = this_voice->lastAmplitudeR >> 2;
            }
        }
    }
    this_voice->Z1value = Z1value;
    this_voice->zIndex = zIndex2;
    this_voice->samplePosition.i = cur_wave_i;
    this_voice->samplePosition.f = cur_wave_f;
    /* lastAmplitudeL holds the full-precision ramp end. */
    /* lastAmplitudeR holds the full-precision ramp end. */
}

// ������������� ������������� ������������� ������������� ������������� ������������� ����������
// ������������� ������������� ������������� ������������� ������������� ������������� ����������
// ������������� ������������� ������������� ������������� ������������� ������������� ����������
// ������������� ������������� ������������� ������������� ������������� ������������� ����������
// ������������� ������������� ������������� ������������� ������������� ������������� ����������
// 16 bit cases

void PV_ServeU3232FilterFullBufferNewReverb16 (GM_Voice *this_voice)
{
    PV_ServeU3232FilterPartialBufferNewReverb16 (this_voice, FALSE);
}

void PV_ServeU3232FilterPartialBufferNewReverb16 (GM_Voice *this_voice, bool looping)
{
    register int32_t          *destL;
    register int32_t          *destReverb, *destChorus;
    register int16_t          *source;
    register U32            cur_wave_i, cur_wave_f;
    register U32            end_wave, wave_adjust = 0;
    U3232                   wave_increment;
    register int32_t          amplitudeL;
    register int32_t          amplitudeReverb, amplitudeChorus;
    register int32_t          inner;
    int32_t                   amplitudeLincrement;
    int32_t                   ampValueL;
    int32_t                   a;
    register int16_t          *z;
    register int32_t          Z1value, zIndex1, zIndex2, Xn, Z1, Zn, sample;

    z = this_voice->z;
    Z1value = this_voice->Z1value;
    zIndex2 = this_voice->zIndex;

    GET_FILTER_PARAMS

    amplitudeL = this_voice->lastAmplitudeL;
    ampValueL = (this_voice->NoteVolume * this_voice->NoteVolumeEnvelope) >> VOLUME_PRECISION_SCALAR;
    amplitudeLincrement = (ampValueL - amplitudeL) / MusicGlobals->Four_Loop;

    destL = &MusicGlobals->songBufferDry[0];
    destReverb = &MusicGlobals->songBufferReverb[0];
    destChorus = &MusicGlobals->songBufferChorus[0];
    source = (int16_t *) this_voice->NotePtr;
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
        if (this_voice->LPF_resonance == 0)
        {
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                amplitudeReverb = (amplitudeL * this_voice->reverbLevel) >> 9;
                amplitudeChorus = (amplitudeL * this_voice->chorusLevel) >> 9;

                for (inner = 0; inner < 4; inner++)
                {
                    THE_CHECK_U3232(int16_t *);       // is in the mail
                    sample = PV_FilterFetchInterp16(this_voice, source, cur_wave_i, cur_wave_f, looping);
                    sample = (sample * Xn + Z1value * Z1) >> 16;
                    Z1value = sample - (sample >> 9);   // remove DC bias
                    *destL += (sample * amplitudeL) >> 2;
                    destL++;
                    *destReverb += sample * amplitudeReverb;
                    destReverb++;
                    *destChorus++ += sample * amplitudeChorus;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                }
                amplitudeL += amplitudeLincrement;
            }
        }
        else
        {
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                this_voice->previous_zFrequency += (this_voice->LPF_frequency - this_voice->previous_zFrequency) >> BAE_FILTER_GLIDE_BLOCK(5);
                zIndex1 = zIndex2 - (this_voice->previous_zFrequency >> 8);
                amplitudeReverb = (amplitudeL * this_voice->reverbLevel) >> 9;
                amplitudeChorus = (amplitudeL * this_voice->chorusLevel) >> 9;

                for (inner = 0; inner < 4; inner++)
                {
                    THE_CHECK_U3232(int16_t *);       // is in the mail
                    sample = PV_FilterFetchInterp16(this_voice, source, cur_wave_i, cur_wave_f, looping);
                    sample = (sample * Xn + Z1value * Z1 + z[zIndex1 & MAXRESONANCE] * Zn) >> 16;
                    zIndex1++;
                    z[zIndex2 & MAXRESONANCE] = (int16_t)sample;
                    zIndex2++;
                    Z1value = sample - (sample >> 9);
                    *destL += (sample * amplitudeL) >> 2;
                    destL++;
                    *destReverb += sample * amplitudeReverb;
                    destReverb++;
                    *destChorus++ += sample * amplitudeChorus;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                }
                amplitudeL += amplitudeLincrement;
            }
        }
    }
    this_voice->Z1value = Z1value;
    this_voice->zIndex = zIndex2;
    this_voice->samplePosition.i = cur_wave_i;
    this_voice->samplePosition.f = cur_wave_f;
    this_voice->lastAmplitudeL = amplitudeL;
FINISH:
    return;
}


void PV_ServeU3232StereoFilterFullBufferNewReverb16 (GM_Voice *this_voice)
{
    PV_ServeU3232StereoFilterPartialBufferNewReverb16 (this_voice, FALSE);
}

void PV_ServeU3232StereoFilterPartialBufferNewReverb16 (GM_Voice *this_voice, bool looping)
{
    register int32_t          *destL;
    register int32_t          *destReverb, *destChorus;
    register int16_t          *source;
    register U32            cur_wave_i, cur_wave_f;
    register U32            end_wave, wave_adjust = 0;
    U3232                   wave_increment;
    register int32_t          amplitudeL;
    register int32_t          amplitudeR;
    register int32_t          inner;
    int32_t                   amplitudeLincrement, amplitudeRincrement;
    int32_t                   ampValueL, ampValueR;
    register int32_t          amplitudeReverb, amplitudeChorus;
    int32_t                   a;
    register int16_t          *z;
    register int32_t          Z1value, zIndex1, zIndex2, Xn, Z1, Zn, sample;

    if (this_voice->channels > 1)
    {
        PV_ServeU3232StereoPartialBuffer16 (this_voice, looping); 
        return; 
    }

    z = this_voice->z;
    Z1value = this_voice->Z1value;
    zIndex2 = this_voice->zIndex;

    GET_FILTER_PARAMS

    PV_CalculateStereoVolume(this_voice, &ampValueL, &ampValueR);

    amplitudeL = this_voice->lastAmplitudeL;
    amplitudeR = this_voice->lastAmplitudeR;
    amplitudeLincrement = (ampValueL - amplitudeL) / MusicGlobals->Four_Loop;
    amplitudeRincrement = (ampValueR - amplitudeR) / MusicGlobals->Four_Loop;

    destL = &MusicGlobals->songBufferDry[0];
    destReverb = &MusicGlobals->songBufferReverb[0];
    destChorus = &MusicGlobals->songBufferChorus[0];
    source = (int16_t *) this_voice->NotePtr;
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
        if (this_voice->LPF_resonance == 0)
        {
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                amplitudeReverb = ((amplitudeL + amplitudeR) * this_voice->reverbLevel) >> 9;
                amplitudeChorus = ((amplitudeL + amplitudeR) * this_voice->chorusLevel) >> 9;

                for (inner = 0; inner < 4; inner++)
                {
                    THE_CHECK_U3232(int16_t *);       // is in the mail
                    sample = PV_FilterFetchInterp16(this_voice, source, cur_wave_i, cur_wave_f, looping);
                    sample = (sample * Xn + Z1value * Z1) >> 16;
                    Z1value = sample - (sample >> 9);
                    destL[0] += (sample * amplitudeL) >> 2;
                    destL[1] += (sample * amplitudeR) >> 2;
                    destL += 2;
                    *destReverb += sample * amplitudeReverb;
                    destReverb++;
                    *destChorus++ += sample * amplitudeChorus;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                }
                amplitudeL += amplitudeLincrement;
                amplitudeR += amplitudeRincrement;
            }
        }
        else
        {
            for (a = MusicGlobals->Four_Loop; a > 0; --a)
            {
                zIndex1 = zIndex2 - (this_voice->previous_zFrequency >> 8);
                this_voice->previous_zFrequency += (this_voice->LPF_frequency - this_voice->previous_zFrequency) >> BAE_FILTER_GLIDE_BLOCK(3);
                amplitudeReverb = ((amplitudeL + amplitudeR) * this_voice->reverbLevel) >> 9;
                amplitudeChorus = ((amplitudeL + amplitudeR) * this_voice->chorusLevel) >> 9;

                for (inner = 0; inner < 4; inner++)
                {
                    THE_CHECK_U3232(int16_t *);
                    sample = PV_FilterFetchInterp16(this_voice, source, cur_wave_i, cur_wave_f, looping);
                    sample = (sample * Xn + Z1value * Z1 + z[zIndex1 & MAXRESONANCE] * Zn) >> 16;
                    zIndex1++;
                    z[zIndex2 & MAXRESONANCE] = (int16_t)sample;
                    zIndex2++;
                    Z1value = sample - (sample >> 9);
                    destL[0] += (sample * amplitudeL) >> 2;
                    destL[1] += (sample * amplitudeR) >> 2;
                    destL += 2;
                    *destReverb += sample * amplitudeReverb;
                    destReverb++;
                    *destChorus++ += sample * amplitudeChorus;
                    ADD_U3232(cur_wave_i, cur_wave_f, wave_increment);
                }
                amplitudeL += amplitudeLincrement;
                amplitudeR += amplitudeRincrement;
            }
        }
    }
    this_voice->Z1value = Z1value;
    this_voice->zIndex = zIndex2;
    this_voice->samplePosition.i = cur_wave_i;
    this_voice->samplePosition.f = cur_wave_f;
    this_voice->lastAmplitudeL = amplitudeL;
    this_voice->lastAmplitudeR = amplitudeR;
FINISH:
    return;
}

#endif  // REVERB_USED and LOOPS_USED
