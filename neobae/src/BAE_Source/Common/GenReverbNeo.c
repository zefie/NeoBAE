/*
    Copyright (c) 2025 NeoBAE Contributors
    
    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:
    
    Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
    
    Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    
    Neither the name of NeoBAE nor the names of its contributors may be
    used to endorse or promote products derived from this software without
    specific prior written permission.
    
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
** "GenReverbNeo.c"
**
**  Roland MT-32 style reverb implementation for miniBAE
**
**  Written by: NeoBAE Contributors
**  Created: 2025
**
**  This file implements a Roland MT-32 inspired reverb system with three modes:
**  - Room: Short early reflections with moderate decay
**  - Hall: Longer reverb with smoother decay
**  - Tap Delay: Multiple discrete echoes (characteristic MT-32 effect)
**
**  The MT-32 used a relatively simple but distinctive reverb algorithm based
**  on delay lines and feedback. This implementation captures that character
**  while adapting to miniBAE's architecture.
**
** Modification History:
**  12/24/25    Initial implementation of MT-32 style reverb
*/
/*****************************************************************************/

#include "GenSnd.h"
#include "GenPriv.h"
#include "BAE_API.h"
#include "X_API.h"
#include <stdint.h>
#include "X_Assert.h"

#if USE_NEO_EFFECTS     // Conditionally compile this file

// Fixed-point shift amounts for precision
#define NEO_COEFF_SHIFT         16
#define NEO_COEFF_MULTIPLY      (1L << NEO_COEFF_SHIFT)

// Match the internal scaling used by GenReverbNew.c so the mono send buffer
// (songBufferReverb) is interpreted in the same domain and the wet output is
// added back to the dry mix consistently.
#define NEO_INPUTSHIFT          10

// Neo reverb tends to be perceptually quieter than the legacy/"new" reverb path
// at the same controller sends. Boost the wet addback slightly so Room/Hall/Tap
// are clearly audible vs. None.
#define NEO_WETSHIFT            (NEO_INPUTSHIFT + 1)

// Feedback coefficient limits (Q16.16).
// Keeping these conservative avoids "build-up" behavior where long feedback
// approaches unity and the tail becomes effectively non-decaying.
#define NEO_FEEDBACK_MIN_Q16    19661   // ~0.30
#define NEO_ROOM_FEEDBACK_MAX_Q16 45875 // ~0.70
#define NEO_HALL_FEEDBACK_MAX_Q16 51118 // ~0.78

// Fixed-point limit-cycle killer: once the feedback loop falls below this
// magnitude, snap to zero so the tail actually dies out.
// (This avoids the classic "infinite sustain / buzzing" artifact in IIR delay
// networks implemented with truncating fixed-point math.)
#define NEO_SILENCE_THRESHOLD   8

// Default reverb time used when the host doesn't provide one.
// This must be < 1.0 feedback (via SetNeoReverbTime) to avoid non-decaying tails.
#define NEO_DEFAULT_REVERB_TIME 100

// NOTE:
// In miniBAE, MusicGlobals->songBufferReverb is a MONO send buffer with
// length == One_Loop (frames). The destination dry buffer is interleaved
// stereo (L,R,L,R...).
// This implementation keeps internal delay lines interleaved stereo for
// a wider image, but consumes mono input.

// Buffer sizes for MT-32 style delays (must be power of 2)
// Tap delay needs to hold up to ~400ms @ 44.1kHz in *stereo-interleaved* samples:
// 400ms is 17640 frames => 35280 interleaved samples, so 32768 would wrap.
#define NEO_TAP_BUFFER_SIZE     65536
// Custom reverb needs to hold up to 500ms at common output rates.
// Buffer size is in *stereo-interleaved* samples, so max frames is (size/2)-2.
// At 48kHz, 500ms is 24000 frames -> 48000 interleaved samples, so use 65536.
#define NEO_CUSTOM_BUFFER_SIZE  65536

#define NEO_TAP_BUFFER_MASK     (NEO_TAP_BUFFER_SIZE - 1)
#define NEO_CUSTOM_BUFFER_MASK  (NEO_CUSTOM_BUFFER_SIZE - 1)

// MT-32 Tap Delay mode: Multiple discrete echoes
// Delays for rhythmic echoes at ~100ms, 200ms, 300ms, 400ms at 44.1kHz
#define NEO_TAP_COUNT           4
static const int32_t neo_tap_delays[] = {4410, 8820, 13230, 17640};
static const INT32 neo_tap_gains[] = {XFIXED_1, 52428, 39321, 26214};  // Descending gains

// Custom reverb mode: User-configurable comb filters
#define NEO_CUSTOM_MAX_COMBS    4

// Global reverb parameters
typedef struct NeoReverbParams
{
    XBOOL       mIsInitialized;
    Rate        mSampleRate;
    XSDWORD     mReverbMode;  // Which MT-32 mode (Room/Hall/Tap)
    
    // Tap delay buffer
    INT32       *mTapBuffer;
    int         mTapWriteIdx;
    int         mTapReadIdx[NEO_TAP_COUNT];
    int         mTapDelayFrames[NEO_TAP_COUNT];
    
    // Low-pass filter for smoothing
    INT32       mFilterMemoryL;
    INT32       mFilterMemoryR;
    INT32       mLopassK;
    
    // Wet/dry mix
    INT32       mWetGain;
    INT32       mDryGain;
    
    // Custom mode buffers and parameters
    INT32       *mCustomBuffer[NEO_CUSTOM_MAX_COMBS];
    int         mCustomWriteIdx[NEO_CUSTOM_MAX_COMBS];
    int         mCustomReadIdx[NEO_CUSTOM_MAX_COMBS];
    int         mCustomDelayFrames[NEO_CUSTOM_MAX_COMBS];
    INT32       mCustomFeedback[NEO_CUSTOM_MAX_COMBS];
    INT32       mCustomGain[NEO_CUSTOM_MAX_COMBS];
    int         mCustomCombCount;
    XBOOL       mCustomParamsDirty;  // Need to rebuild delays/indices
    
} NeoReverbParams;

static NeoReverbParams gNeoReverbParams;

static INLINE INT32 PV_Clamp32From64(int64_t v)
{
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (INT32)v;
}

static INLINE INT32 PV_ScaleReverbSend(INT32 sendSample)
{
    // Match RunNewReverb(): convert engine mix domain to a smaller internal domain.
    // The +1 keeps headroom (mirrors the historical implementation).
    return sendSample >> (NEO_INPUTSHIFT + 1);
}

static INLINE INT32 PV_ZapSmall(INT32 v)
{
    if (v > -NEO_SILENCE_THRESHOLD && v < NEO_SILENCE_THRESHOLD)
        return 0;
    return v;
}

static INLINE int PV_ClampDelayFramesForBuffer(int frames, int interleavedBufferSize)
{
    // Interleaved stereo buffer holds (size/2) frames.
    const int maxFrames = (interleavedBufferSize / 2) - 2;
    if (frames < 1) return 1;
    if (frames > maxFrames) return maxFrames;
    return frames;
}

static void PV_UpdateNeoDelayTables(NeoReverbParams *params)
{
    // The constants are expressed in frames @ 44.1kHz. Scale to the actual output rate.
    // This keeps the perceived time constants consistent across sample rates.
    const int32_t refRate = 44100;
    int i;

    if (!params || params->mSampleRate <= 0)
        return;

    for (i = 0; i < NEO_TAP_COUNT; i++)
    {
        int64_t scaled = ((int64_t)neo_tap_delays[i] * (int64_t)params->mSampleRate + (refRate / 2)) / refRate;
        params->mTapDelayFrames[i] = PV_ClampDelayFramesForBuffer((int)scaled, NEO_TAP_BUFFER_SIZE);
    }
}

static void PV_ApplyNeoMt32Defaults(NeoReverbParams *params)
{
    // Keep this conservative: MT-32 reverb is audible but not a huge wash.
    // Values are MIDI-ish (0..127) for the public setters.
    if (!params)
        return;

    switch (params->mReverbMode)
    {
        case REVERB_TYPE_12: // Room - Use Custom reverb preset
            SetNeoCustomReverbCombCount(3);
            // Short delays for room: ~35ms, 43ms, 52ms
            SetNeoCustomReverbCombDelay(0, 35);
            SetNeoCustomReverbCombDelay(1, 43);
            SetNeoCustomReverbCombDelay(2, 52);
            // Moderate feedback for all combs
            SetNeoCustomReverbCombFeedback(0, 70);
            SetNeoCustomReverbCombFeedback(1, 70);
            SetNeoCustomReverbCombFeedback(2, 70);
            // Equal gain for all combs
            SetNeoCustomReverbCombGain(0, 127);
            SetNeoCustomReverbCombGain(1, 127);
            SetNeoCustomReverbCombGain(2, 127);
            SetNeoCustomReverbLowpass(50);
            SetNeoReverbMix(96);
            break;
        case REVERB_TYPE_13: // Hall - Use Custom reverb preset
            SetNeoCustomReverbCombCount(4);
            // Longer delays for hall: ~52ms, 65ms, 79ms, 93ms
            SetNeoCustomReverbCombDelay(0, 52);
            SetNeoCustomReverbCombDelay(1, 65);
            SetNeoCustomReverbCombDelay(2, 79);
            SetNeoCustomReverbCombDelay(3, 93);
            // Longer feedback for hall
            SetNeoCustomReverbCombFeedback(0, 85);
            SetNeoCustomReverbCombFeedback(1, 85);
            SetNeoCustomReverbCombFeedback(2, 85);
            SetNeoCustomReverbCombFeedback(3, 85);
            // Equal gain for all combs
            SetNeoCustomReverbCombGain(0, 127);
            SetNeoCustomReverbCombGain(1, 127);
            SetNeoCustomReverbCombGain(2, 127);
            SetNeoCustomReverbCombGain(3, 127);
            SetNeoCustomReverbLowpass(40);
            SetNeoReverbMix(88);
            break;
        case REVERB_TYPE_14: // Cavern - Use Custom reverb preset
            SetNeoCustomReverbCombCount(4);
            SetNeoCustomReverbCombDelay(0, 75);
            SetNeoCustomReverbCombDelay(1, 125);
            SetNeoCustomReverbCombDelay(2, 175);
            SetNeoCustomReverbCombDelay(3, 200);
            SetNeoCustomReverbCombFeedback(0, 107);
            SetNeoCustomReverbCombFeedback(1, 107);
            SetNeoCustomReverbCombFeedback(2, 107);
            SetNeoCustomReverbCombFeedback(3, 107);
            SetNeoCustomReverbCombGain(0, 127);
            SetNeoCustomReverbCombGain(1, 127);
            SetNeoCustomReverbCombGain(2, 127);
            SetNeoCustomReverbCombGain(3, 127);
            SetNeoCustomReverbLowpass(64);
            SetNeoReverbMix(110);
            break;
        case REVERB_TYPE_15: // Dungeon - Use Custom reverb preset
            SetNeoCustomReverbCombCount(4);
            SetNeoCustomReverbCombDelay(0, 175);
            SetNeoCustomReverbCombDelay(1, 250);
            SetNeoCustomReverbCombDelay(2, 325);
            SetNeoCustomReverbCombDelay(3, 450);
            SetNeoCustomReverbCombFeedback(0, 107);
            SetNeoCustomReverbCombFeedback(1, 107);
            SetNeoCustomReverbCombFeedback(2, 107);
            SetNeoCustomReverbCombFeedback(3, 107);
            SetNeoCustomReverbCombGain(0, 127);
            SetNeoCustomReverbCombGain(1, 127);
            SetNeoCustomReverbCombGain(2, 127);
            SetNeoCustomReverbCombGain(3, 127);
            SetNeoCustomReverbLowpass(64);
            SetNeoReverbMix(110);
            break;
        case REVERB_TYPE_16: // Reserved
            SetNeoCustomReverbCombCount(4);
            SetNeoCustomReverbCombDelay(0, 22);
            SetNeoCustomReverbCombDelay(1, 29);
            SetNeoCustomReverbCombDelay(2, 36);
            SetNeoCustomReverbCombDelay(3, 43);
            SetNeoCustomReverbCombFeedback(0, 112);
            SetNeoCustomReverbCombFeedback(1, 112);
            SetNeoCustomReverbCombFeedback(2, 112);
            SetNeoCustomReverbCombFeedback(3, 112);
            SetNeoCustomReverbCombGain(0, 127);
            SetNeoCustomReverbCombGain(1, 127);
            SetNeoCustomReverbCombGain(2, 127);
            SetNeoCustomReverbCombGain(3, 127);
            SetNeoCustomReverbLowpass(64);
            SetNeoReverbMix(110);
            break;
        case REVERB_TYPE_17: // Tap delay
            params->mLopassK = 13107; // ~0.20
            SetNeoReverbMix(104);
            // Tap mode doesn't use feedback; leave time as-is.
            break;
        default:
            if (params->mReverbMode >= REVERB_TYPE_18)
            {
                // Custom mode: user controls all parameters via API
                // Apply reasonable defaults that user can override
                SetNeoCustomReverbLowpass(50);
                SetNeoReverbMix(110);  // More aggressive wet mix
            }
            break;
    }
}

//++------------------------------------------------------------------------------
//  GetNeoReverbParams()
//
//  Returns pointer to global Neo reverb parameters
//++------------------------------------------------------------------------------
NeoReverbParams* GetNeoReverbParams(void)
{
    return &gNeoReverbParams;
}

//++------------------------------------------------------------------------------
//  InitNeoReverb()
//
//  Initialize the MT-32 style reverb system
//++------------------------------------------------------------------------------
XBOOL InitNeoReverb(void)
{
    int i;
    NeoReverbParams* params = GetNeoReverbParams();
    
    params->mIsInitialized = FALSE;
    
    // Allocate tap delay buffer
    params->mTapBuffer = (INT32*)XNewPtr(sizeof(INT32) * NEO_TAP_BUFFER_SIZE);
    if (params->mTapBuffer == NULL)
    {
        ShutdownNeoReverb();
        return FALSE;
    }
    XSetMemory(params->mTapBuffer, sizeof(INT32) * NEO_TAP_BUFFER_SIZE, 0);
    params->mTapWriteIdx = 0;
    
    // Allocate custom mode buffers
    for (i = 0; i < NEO_CUSTOM_MAX_COMBS; i++)
    {
        params->mCustomBuffer[i] = (INT32*)XNewPtr(sizeof(INT32) * NEO_CUSTOM_BUFFER_SIZE);
        if (params->mCustomBuffer[i] == NULL)
        {
            ShutdownNeoReverb();
            return FALSE;
        }
        XSetMemory(params->mCustomBuffer[i], sizeof(INT32) * NEO_CUSTOM_BUFFER_SIZE, 0);
        params->mCustomWriteIdx[i] = 0;
        params->mCustomFeedback[i] = (INT32)(NEO_COEFF_MULTIPLY * 0.75);  // Default 0.75 feedback
        params->mCustomGain[i] = NEO_COEFF_MULTIPLY;  // Default full gain
        // Varied delay times for richer texture
        params->mCustomDelayFrames[i] = 1000 + (i * 300);  // 22ms, 29ms, 36ms, etc. @ 44.1kHz
    }
    params->mCustomCombCount = 4;  // Default to 4 combs
    params->mCustomParamsDirty = FALSE;
    
    // Setup delay tables and read indices based on delay times
    // Multiply delays by 2 for stereo interleaving (L,R,L,R...)
    params->mSampleRate = MusicGlobals->outputRate;
    PV_UpdateNeoDelayTables(params);
    for (i = 0; i < NEO_TAP_COUNT; i++)
    {
        params->mTapReadIdx[i] = (NEO_TAP_BUFFER_SIZE - (params->mTapDelayFrames[i] * 2)) & NEO_TAP_BUFFER_MASK;
    }
    
    for (i = 0; i < NEO_CUSTOM_MAX_COMBS; i++)
    {
        params->mCustomReadIdx[i] = (NEO_CUSTOM_BUFFER_SIZE - (params->mCustomDelayFrames[i] * 2)) & NEO_CUSTOM_BUFFER_MASK;
    }
    
    // Initialize filter state
    params->mFilterMemoryL = 0;
    params->mFilterMemoryR = 0;
    params->mLopassK = 13107;  // ~0.2 filter coefficient (gentle smoothing)
    
    // Default wet/dry mix (MT-32 style: strong wet signal for obvious effect)
    params->mWetGain = 98304;   // ~1.5 (very strong for obvious reverb)
    params->mDryGain = 52428;   // ~0.8
    
    params->mReverbMode = -1;  // Will be set by CheckNeoReverbType
    params->mIsInitialized = TRUE;
    
    return TRUE;
}

//++------------------------------------------------------------------------------
//  ShutdownNeoReverb()
//
//  Clean up and deallocate Neo reverb resources
//++------------------------------------------------------------------------------
void ShutdownNeoReverb(void)
{
    int i;
    NeoReverbParams* params = GetNeoReverbParams();
    
    params->mIsInitialized = FALSE;
    
    // Deallocate tap buffer
    if (params->mTapBuffer)
    {
        XDisposePtr(params->mTapBuffer);
        params->mTapBuffer = NULL;
    }
    
    // Deallocate custom buffers
    for (i = 0; i < NEO_CUSTOM_MAX_COMBS; i++)
    {
        if (params->mCustomBuffer[i])
        {
            XDisposePtr(params->mCustomBuffer[i]);
            params->mCustomBuffer[i] = NULL;
        }
    }
}

//++------------------------------------------------------------------------------
//  CheckNeoReverbType()
//
//  Check if reverb type has changed and clear buffers if needed
//++------------------------------------------------------------------------------
XBOOL CheckNeoReverbType(void)
{
    NeoReverbParams* params = GetNeoReverbParams();
    XBOOL changed = FALSE;
    int i;
    
    if (!params->mIsInitialized)
        return FALSE;
    
    if (params->mReverbMode != MusicGlobals->reverbUnitType)
    {
        changed = TRUE;
        params->mReverbMode = MusicGlobals->reverbUnitType;

        // If the output rate changes, keep the time constants stable.
        if (params->mSampleRate != MusicGlobals->outputRate)
        {
            params->mSampleRate = MusicGlobals->outputRate;
            PV_UpdateNeoDelayTables(params);
        }
        
        // Clear all buffers when changing modes
        if (params->mTapBuffer)
            XSetMemory(params->mTapBuffer, sizeof(INT32) * NEO_TAP_BUFFER_SIZE, 0);
        params->mTapWriteIdx = 0;
        
        for (i = 0; i < NEO_CUSTOM_MAX_COMBS; i++)
        {
            if (params->mCustomBuffer[i])
                XSetMemory(params->mCustomBuffer[i], sizeof(INT32) * NEO_CUSTOM_BUFFER_SIZE, 0);
            params->mCustomWriteIdx[i] = 0;
        }
        params->mCustomParamsDirty = FALSE;
        
        // Reset filter memory
        params->mFilterMemoryL = 0;
        params->mFilterMemoryR = 0;

        // Apply MT-32-ish defaults per mode.
        PV_ApplyNeoMt32Defaults(params);
    }
    
    return changed;
}

//++------------------------------------------------------------------------------
//  PV_ProcessNeoTapReverb()
//
//  MT-32 Tap Delay mode: Multiple discrete echoes
//  Characteristic MT-32 rhythmic echo effect
//++------------------------------------------------------------------------------
static void PV_ProcessNeoTapReverb(INT32 *sourceP, INT32 *destP, int numFrames)
{
    NeoReverbParams* params = GetNeoReverbParams();
    INT32 inputL, inputR, outputL, outputR;
    INT32 tapL, tapR;
    int i, frame, readPos;
    
    for (frame = 0; frame < numFrames; frame++)
    {
        // Get mono input from reverb send buffer
        INT32 input = PV_ScaleReverbSend(sourceP[frame]);
        inputL = input;
        inputR = input;
        
        // Write input to delay buffer first
        params->mTapBuffer[params->mTapWriteIdx] = inputL;
        params->mTapBuffer[(params->mTapWriteIdx + 1) & NEO_TAP_BUFFER_MASK] = inputR;
        
        outputL = 0;
        outputR = 0;
        
        // Sum all tap delays with decreasing gains
        for (i = 0; i < NEO_TAP_COUNT; i++)
        {
            // Calculate read position: delay samples back from write position
            readPos = (params->mTapWriteIdx - (params->mTapDelayFrames[i] * 2)) & NEO_TAP_BUFFER_MASK;
            
            tapL = params->mTapBuffer[readPos];
            tapR = params->mTapBuffer[(readPos + 1) & NEO_TAP_BUFFER_MASK];
            
            outputL = PV_Clamp32From64((int64_t)outputL + (((int64_t)tapL * (int64_t)neo_tap_gains[i]) >> NEO_COEFF_SHIFT));
            outputR = PV_Clamp32From64((int64_t)outputR + (((int64_t)tapR * (int64_t)neo_tap_gains[i]) >> NEO_COEFF_SHIFT));
        }
        
        // Advance write index
        params->mTapWriteIdx = (params->mTapWriteIdx + 2) & NEO_TAP_BUFFER_MASK;
        
        // Apply light filtering to taps
        params->mFilterMemoryL = PV_Clamp32From64((int64_t)params->mFilterMemoryL + ((((int64_t)(outputL - params->mFilterMemoryL)) * (int64_t)params->mLopassK) >> NEO_COEFF_SHIFT));
        params->mFilterMemoryR = PV_Clamp32From64((int64_t)params->mFilterMemoryR + ((((int64_t)(outputR - params->mFilterMemoryR)) * (int64_t)params->mLopassK) >> NEO_COEFF_SHIFT));
        
        // Mix wet reverb signal into destination (dry) buffer
        {
            INT32 wetL = PV_Clamp32From64(((int64_t)params->mFilterMemoryL * (int64_t)params->mWetGain) >> NEO_COEFF_SHIFT);
            INT32 wetR = PV_Clamp32From64(((int64_t)params->mFilterMemoryR * (int64_t)params->mWetGain) >> NEO_COEFF_SHIFT);
            destP[frame * 2] += (XSDWORD)(((int64_t)wetL) << NEO_WETSHIFT);
            destP[frame * 2 + 1] += (XSDWORD)(((int64_t)wetR) << NEO_WETSHIFT);
        }
    }
}

//++------------------------------------------------------------------------------
//  PV_RebuildCustomDelayIndices()
//
//  Rebuild read indices for custom reverb when parameters change
//++------------------------------------------------------------------------------
static void PV_RebuildCustomDelayIndices(NeoReverbParams *params)
{
    int i;
    for (i = 0; i < params->mCustomCombCount; i++)
    {
        int clampedDelay = PV_ClampDelayFramesForBuffer(params->mCustomDelayFrames[i], NEO_CUSTOM_BUFFER_SIZE);
        params->mCustomDelayFrames[i] = clampedDelay;
        params->mCustomReadIdx[i] = (NEO_CUSTOM_BUFFER_SIZE - (clampedDelay * 2)) & NEO_CUSTOM_BUFFER_MASK;
    }
    params->mCustomParamsDirty = FALSE;
}

//++------------------------------------------------------------------------------
//  PV_ProcessNeoCustomReverb()
//
//  Custom reverb mode: User-configurable parallel comb filters
//  Allows full control over delay times, feedback, and gain per comb
//++------------------------------------------------------------------------------
static void PV_ProcessNeoCustomReverb(INT32 *sourceP, INT32 *destP, int numFrames)
{
    NeoReverbParams* params = GetNeoReverbParams();
    INT32 inputL, inputR, combOutL, combOutR;
    int64_t outputL, outputR;
    INT32 delayedL, delayedR, feedback, gain;
    int i, frame, readPos;
    
#if _DEBUG == TRUE
    static int debugCounter = 0;
    if (debugCounter++ % 100 == 0)
    {
        BAE_PRINTF("Custom reverb processing: combs=%d",params->mCustomCombCount);
        BAE_PRINTF(" fb0=%d",params->mCustomFeedback[0]);
        BAE_PRINTF(" gain0=%d",params->mCustomGain[0]);
        BAE_PRINTF(" delay0=%d",params->mCustomDelayFrames[0]);
        BAE_PRINTF("\n");
    }
#endif
    
    // Rebuild delay indices if parameters have changed
    if (params->mCustomParamsDirty)
    {
        PV_RebuildCustomDelayIndices(params);
    }
    
    for (frame = 0; frame < numFrames; frame++)
    {
        // Get mono input from reverb send buffer
        INT32 input = PV_ScaleReverbSend(sourceP[frame]);
        inputL = input;
        inputR = input;
        
        outputL = 0;
        outputR = 0;
        
        // Process parallel comb filters (up to user-defined count)
        for (i = 0; i < params->mCustomCombCount; i++)
        {
            // Calculate read position: delay samples back from write position
            readPos = (params->mCustomWriteIdx[i] - (params->mCustomDelayFrames[i] * 2)) & NEO_CUSTOM_BUFFER_MASK;
            
            // Read delayed samples
            delayedL = params->mCustomBuffer[i][readPos];
            delayedR = params->mCustomBuffer[i][(readPos + 1) & NEO_CUSTOM_BUFFER_MASK];
            
            // Compute comb filter output: input + delayed * feedback
            feedback = params->mCustomFeedback[i];
            combOutL = PV_Clamp32From64((int64_t)inputL + (((int64_t)delayedL * (int64_t)feedback) >> NEO_COEFF_SHIFT));
            combOutR = PV_Clamp32From64((int64_t)inputR + (((int64_t)delayedR * (int64_t)feedback) >> NEO_COEFF_SHIFT));

            combOutL = PV_ZapSmall(combOutL);
            combOutR = PV_ZapSmall(combOutR);
            
            // Write to current position
            params->mCustomBuffer[i][params->mCustomWriteIdx[i]] = combOutL;
            params->mCustomBuffer[i][(params->mCustomWriteIdx[i] + 1) & NEO_CUSTOM_BUFFER_MASK] = combOutR;
            
            // Accumulate output with per-comb gain (use delayed values for output)
            gain = params->mCustomGain[i];
            outputL += (((int64_t)delayedL * (int64_t)gain) >> NEO_COEFF_SHIFT);
            outputR += (((int64_t)delayedR * (int64_t)gain) >> NEO_COEFF_SHIFT);
            
            // Advance write index
            params->mCustomWriteIdx[i] = (params->mCustomWriteIdx[i] + 2) & NEO_CUSTOM_BUFFER_MASK;
        }
        
        // Average the output from all combs to prevent clipping
        if (params->mCustomCombCount > 0)
        {
            outputL = outputL / params->mCustomCombCount;
            outputR = outputR / params->mCustomCombCount;
        }
        
        // Apply low-pass filtering for smoothing
        params->mFilterMemoryL = PV_Clamp32From64((int64_t)params->mFilterMemoryL + ((((int64_t)(outputL - params->mFilterMemoryL)) * (int64_t)params->mLopassK) >> NEO_COEFF_SHIFT));
        params->mFilterMemoryR = PV_Clamp32From64((int64_t)params->mFilterMemoryR + ((((int64_t)(outputR - params->mFilterMemoryR)) * (int64_t)params->mLopassK) >> NEO_COEFF_SHIFT));
        
        // Mix wet reverb signal into destination (dry) buffer
        {
            INT32 wetL = PV_Clamp32From64(((int64_t)params->mFilterMemoryL * (int64_t)params->mWetGain) >> NEO_COEFF_SHIFT);
            INT32 wetR = PV_Clamp32From64(((int64_t)params->mFilterMemoryR * (int64_t)params->mWetGain) >> NEO_COEFF_SHIFT);
            destP[frame * 2] += (XSDWORD)(((int64_t)wetL) << NEO_WETSHIFT);
            destP[frame * 2 + 1] += (XSDWORD)(((int64_t)wetR) << NEO_WETSHIFT);
        }
    }
}

//++------------------------------------------------------------------------------
//  RunNeoReverb()
//
//  Main entry point for Neo reverb processing
//  Dispatches to appropriate MT-32 mode
//++------------------------------------------------------------------------------
void RunNeoReverb(INT32 *sourceP, INT32 *destP, int numFrames)
{
    NeoReverbParams* params = GetNeoReverbParams();
    
    
    if (!params->mIsInitialized)
    {
        return;
    }
    
    CheckNeoReverbType();
    
    // Dispatch to appropriate reverb mode
    switch (params->mReverbMode)
    {
        case REVERB_TYPE_12:  // Neo Room (uses Custom preset)
        case REVERB_TYPE_13:  // Neo Hall (uses Custom preset)
        case REVERB_TYPE_14:  // Neo Cavern (uses Custom preset)
        case REVERB_TYPE_15:  // Neo Dungeon (uses Custom preset)
        case REVERB_TYPE_16:  // Neo Reserved (uses Custom preset)
            PV_ProcessNeoCustomReverb(sourceP, destP, numFrames);
            break;
            
        case REVERB_TYPE_17:  // Neo Tap Delay
            PV_ProcessNeoTapReverb(sourceP, destP, numFrames);
            break;
            
        default:
            if (params->mReverbMode >= REVERB_TYPE_18)
            {
                // Treat unknown custom modes as Custom reverb
                PV_ProcessNeoCustomReverb(sourceP, destP, numFrames);
            }
            // No reverb or unsupported type
            break;
    }
}

//++------------------------------------------------------------------------------
//  SetNeoReverbMix()
//
//  Set the wet/dry mix for the reverb
//  wetLevel: 0-127 (MIDI style)
//++------------------------------------------------------------------------------
void SetNeoReverbMix(int wetLevel)
{
    NeoReverbParams* params = GetNeoReverbParams();
    
    if (wetLevel < 0) wetLevel = 0;
    if (wetLevel > 127) wetLevel = 127;
    
    // Convert MIDI level (0-127) to fixed-point gain
    params->mWetGain = (wetLevel * NEO_COEFF_MULTIPLY) / 127;
    params->mDryGain = ((127 - (wetLevel / 2)) * NEO_COEFF_MULTIPLY) / 127;  // Reduce dry less aggressively
}

//++------------------------------------------------------------------------------
//  SetNeoCustomReverbCombCount()
//
//  Set the number of active comb filters for custom reverb
//  combCount: 1-8 (number of parallel comb filters)
//++------------------------------------------------------------------------------
void SetNeoCustomReverbCombCount(int combCount)
{
    NeoReverbParams* params = GetNeoReverbParams();
    
    if (combCount < 1) combCount = 1;
    if (combCount > NEO_CUSTOM_MAX_COMBS) combCount = NEO_CUSTOM_MAX_COMBS;
    
    if (params->mCustomCombCount != combCount)
    {
        params->mCustomCombCount = combCount;
        params->mCustomParamsDirty = TRUE;
    }
}

//++------------------------------------------------------------------------------
//  SetNeoCustomReverbCombDelay()
//
//  Set the delay time in milliseconds for a specific comb filter
//  combIndex: 0-3 (which comb filter to configure) (max of 4 or value of NEO_CUSTOM_MAX_COMBS)
//  delayMs: delay time in milliseconds (1-500ms) (or value of NEO_CUSTOM_MAX_DELAY_MS)
//++------------------------------------------------------------------------------
void SetNeoCustomReverbCombDelay(int combIndex, int delayMs)
{
    NeoReverbParams* params = GetNeoReverbParams();
    
    if (combIndex < 0 || combIndex >= NEO_CUSTOM_MAX_COMBS)
        return;
    
    if (delayMs < 1) delayMs = 1;
    if (delayMs > NEO_CUSTOM_MAX_DELAY_MS) delayMs = NEO_CUSTOM_MAX_DELAY_MS;
    
    // Convert milliseconds to frames at current sample rate
    // delayMs * sampleRate / 1000
    int delayFrames = (delayMs * params->mSampleRate) / 1000;
    
    if (params->mCustomDelayFrames[combIndex] != delayFrames)
    {
        params->mCustomDelayFrames[combIndex] = delayFrames;
        params->mCustomParamsDirty = TRUE;
    }
}

//++------------------------------------------------------------------------------
//  SetNeoCustomReverbCombFeedback()
//
//  Set the feedback coefficient for a specific comb filter
//  combIndex: 0-7 (which comb filter to configure)
//  feedback: 0-127 (MIDI style, maps to ~0.0-0.85 feedback)
//++------------------------------------------------------------------------------
void SetNeoCustomReverbCombFeedback(int combIndex, int feedback)
{
    NeoReverbParams* params = GetNeoReverbParams();
    
    if (combIndex < 0 || combIndex >= NEO_CUSTOM_MAX_COMBS)
        return;
    
    if (feedback < 0) feedback = 0;
    if (feedback > 127) feedback = 127;
    
    // Map 0-127 to feedback range (0.0 to ~0.85)
    // Use a safe max to avoid runaway feedback
    const INT32 maxFeedback = (INT32)(NEO_COEFF_MULTIPLY * 0.85);
    params->mCustomFeedback[combIndex] = (feedback * maxFeedback) / 127;
}

//++------------------------------------------------------------------------------
//  SetNeoCustomReverbCombGain()
//
//  Set the output gain for a specific comb filter
//  combIndex: 0-7 (which comb filter to configure)
//  gain: 0-127 (MIDI style, maps to 0.0-1.0 gain)
//++------------------------------------------------------------------------------
void SetNeoCustomReverbCombGain(int combIndex, int gain)
{
    NeoReverbParams* params = GetNeoReverbParams();
    
    if (combIndex < 0 || combIndex >= NEO_CUSTOM_MAX_COMBS)
        return;
    
    if (gain < 0) gain = 0;
    if (gain > 127) gain = 127;
    
    // Map 0-127 to gain range (0.0 to 1.0)
    params->mCustomGain[combIndex] = (gain * NEO_COEFF_MULTIPLY) / 127;
}

//++------------------------------------------------------------------------------
//  SetNeoCustomReverbLowpass()
//
//  Set the low-pass filter coefficient for custom reverb
//  lowpass: 0-127 (MIDI style, maps to filter coefficient 0.0-0.5)
//          Lower values = more filtering (darker sound)
//          Higher values = less filtering (brighter sound)
//++------------------------------------------------------------------------------
void SetNeoCustomReverbLowpass(int lowpass)
{
    NeoReverbParams* params = GetNeoReverbParams();
    
    if (lowpass < 0) lowpass = 0;
    if (lowpass > 127) lowpass = 127;
    
    // Map 0-127 to lowpass coefficient range (0.0 to 0.5)
    // This controls how much of the new signal blends with the filtered memory
    params->mLopassK = (lowpass * NEO_COEFF_MULTIPLY / 2) / 127;
}

//++------------------------------------------------------------------------------
//  GetNeoCustomReverbCombCount()
//
//  Get the current number of active comb filters
//++------------------------------------------------------------------------------
int GetNeoCustomReverbCombCount(void)
{
    NeoReverbParams* params = GetNeoReverbParams();
    return params->mCustomCombCount;
}

//++------------------------------------------------------------------------------
//  GetNeoCustomReverbCombDelay()
//
//  Get the delay time in milliseconds for a specific comb filter
//++------------------------------------------------------------------------------
int GetNeoCustomReverbCombDelay(int combIndex)
{
    NeoReverbParams* params = GetNeoReverbParams();
    
    if (combIndex < 0 || combIndex >= NEO_CUSTOM_MAX_COMBS)
        return 0;
    
    // Convert frames back to milliseconds
    return (params->mCustomDelayFrames[combIndex] * 1000) / params->mSampleRate;
}

//++------------------------------------------------------------------------------
//  GetNeoCustomReverbCombFeedback()
//
//  Get the feedback coefficient for a specific comb filter (0-127)
//++------------------------------------------------------------------------------
int GetNeoCustomReverbCombFeedback(int combIndex)
{
    NeoReverbParams* params = GetNeoReverbParams();
    
    if (combIndex < 0 || combIndex >= NEO_CUSTOM_MAX_COMBS)
        return 0;
    
    // Map feedback back to 0-127 range
    const INT32 maxFeedback = (INT32)(NEO_COEFF_MULTIPLY * 0.85);
    return (int)((params->mCustomFeedback[combIndex] * 127) / maxFeedback);
}

//++------------------------------------------------------------------------------
//  GetNeoCustomReverbCombGain()
//
//  Get the output gain for a specific comb filter (0-127)
//++------------------------------------------------------------------------------
int GetNeoCustomReverbCombGain(int combIndex)
{
    NeoReverbParams* params = GetNeoReverbParams();
    
    if (combIndex < 0 || combIndex >= NEO_CUSTOM_MAX_COMBS)
        return 0;
    
    // Map gain back to 0-127 range
    return (int)((params->mCustomGain[combIndex] * 127) / NEO_COEFF_MULTIPLY);
}

#endif  // USE_NEO_EFFECTS
