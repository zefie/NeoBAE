/*
    Copyright (c) 2025 zefie. All rights reserved.

    SF2 (SoundFont 2) support for miniBAE

    Implementation of SF    pBank = (SF2_Bank*)XNewPtr(sizeof(SF2_Bank));
    if (pBank == NULL)
    {
        BAE_PRINTF("SF2 Debug: Failed to allocate memory for SF2 bank\n");
        XFileClose(fileRef);
        return MEMORY_ERR;
    }

    BAE_PRINTF("SF2 Debug: Allocated SF2 bank structure\n"); loading and instrument creation
*/

#include "GenSF2.h"
#include "GenSnd.h"
#include "MiniBAE.h"
#include "GenPriv.h" // for VOLUME_RANGE and ADSR constants
#include "X_Assert.h"
#include "X_Formats.h"
#include "X_API.h"

#if USE_SF2_SUPPORT == TRUE

// FOURCC macros for SF2 chunks
#define SF2_RIFF FOUR_CHAR('R', 'I', 'F', 'F')
#define SF2_SFBK FOUR_CHAR('s', 'f', 'b', 'k')
#define SF2_LIST FOUR_CHAR('L', 'I', 'S', 'T')
#define SF2_INFO FOUR_CHAR('I', 'N', 'F', 'O')
#define SF2_SDTA FOUR_CHAR('s', 'd', 't', 'a')
#define SF2_SMPL FOUR_CHAR('s', 'm', 'p', 'l')
#define SF2_PDTA FOUR_CHAR('p', 'd', 't', 'a')
#define SF2_PHDR FOUR_CHAR('p', 'h', 'd', 'r')
#define SF2_PBAG FOUR_CHAR('p', 'b', 'a', 'g')
#define SF2_PMOD FOUR_CHAR('p', 'm', 'o', 'd')
#define SF2_PGEN FOUR_CHAR('p', 'g', 'e', 'n')
#define SF2_INST FOUR_CHAR('i', 'n', 's', 't')
#define SF2_IBAG FOUR_CHAR('i', 'b', 'a', 'g')
#define SF2_IMOD FOUR_CHAR('i', 'm', 'o', 'd')
#define SF2_IGEN FOUR_CHAR('i', 'g', 'e', 'n')
#define SF2_SHDR FOUR_CHAR('s', 'h', 'd', 'r')

// Internal helper functions
static GM_Instrument *PV_SF2_CreateSimpleInstrument(SF2_Bank *pBank, int32_t *instrumentIDs, uint32_t instrumentCount, OPErr *pErr);
static GM_Instrument *PV_SF2_CreateKeymapSplitInstrument(SF2_Bank *pBank, int32_t *instrumentIDs, uint32_t instrumentCount, int32_t presetID, OPErr *pErr);
// instrumentID is needed to merge instrument-global generators with local zone values
static OPErr PV_SF2_CreateWaveformFromSample(SF2_Bank *pBank, int32_t instrumentID, int16_t sampleID, uint32_t genStart, uint32_t genEnd, GM_Waveform *pWaveform);
// Forward declaration used by helpers below
static int16_t PV_FindGeneratorValue(SF2_Generator *generators, uint32_t genCount,
                                     uint32_t startIndex, uint32_t endIndex,
                                     SF2_GeneratorType genType, int16_t defaultValue);

// Helpers to resolve instrument-global + local generator values
static void PV_GetInstGlobalGenRange(SF2_Bank *pBank, int32_t instrumentID,
                                     uint32_t *pGStart, uint32_t *pGEnd, XBOOL *pHasGlobal);
// Similar helper for preset-level global generators (first preset bag without an INSTRUMENT generator)
static void PV_GetPresetGlobalGenRange(SF2_Bank *pBank, int32_t presetIndex,
                                      uint32_t *pGStart, uint32_t *pGEnd, XBOOL *pHasGlobal);
static int16_t PV_FindInstGenMerged(SF2_Bank *pBank, int32_t instrumentID,
                                    uint32_t localStart, uint32_t localEnd,
                                    SF2_GeneratorType genType, int16_t defaultValue);

// Basic case-insensitive substring check (ASCII)
static XBOOL PV_StrContainsIgnoreCase(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !*needle)
        return FALSE;
    // Simple scan; convert both chars to lower on the fly
    for (const char *h = haystack; *h; ++h)
    {
        const char *h2 = h;
        const char *n2 = needle;
        while (*h2 && *n2)
        {
            char ch = *h2;
            char cn = *n2;
            if (ch >= 'A' && ch <= 'Z')
                ch = (char)(ch - 'A' + 'a');
            if (cn >= 'A' && cn <= 'Z')
                cn = (char)(cn - 'A' + 'a');
            if (ch != cn)
                break;
            ++h2;
            ++n2;
        }
        if (!*n2)
            return TRUE; // matched entire needle
    }
    return FALSE;
}

static int16_t PV_FindEffectiveGenValue(SF2_Bank *pBank, int32_t presetID, int32_t instrumentID,
                                        uint32_t presetGenStart, uint32_t presetGenEnd,
                                        uint32_t instGenStart, uint32_t instGenEnd,
                                        SF2_GeneratorType genType, int16_t defaultValue)
{
    // Get instrument-level value (local overrides global)
    int16_t instValue = PV_FindInstGenMerged(pBank, instrumentID, instGenStart, instGenEnd, genType, defaultValue);

    // Get preset-level value (global + local additive)
    int16_t presetGlobal = 0; // Additive defaults to 0
    int16_t presetLocal = 0;
    XBOOL hasPresetGlobal = FALSE;
    uint32_t presetGlobalStart, presetGlobalEnd;
    // Use preset-level helper to find global generators for the preset
    PV_GetPresetGlobalGenRange(pBank, presetID, &presetGlobalStart, &presetGlobalEnd, &hasPresetGlobal);

    if (hasPresetGlobal)
    {
        presetGlobal = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, presetGlobalStart, presetGlobalEnd, genType, 0);
    }
    presetLocal = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, presetGenStart, presetGenEnd, genType, presetGlobal);

    // For most generators, add preset to instrument; ranges/instrument override when present
    if (genType == SF2_GEN_KEY_RANGE || genType == SF2_GEN_VEL_RANGE || genType == SF2_GEN_INSTRUMENT)
    {
        // Determine if preset-local generator is actually present (don't treat sentinel defaults like 0x7F00 as "present")
        XBOOL presetHasLocal = FALSE;
        if (presetGenStart < presetGenEnd && pBank && pBank->presetGens)
        {
            for (uint32_t i = presetGenStart; i < presetGenEnd && i < pBank->numPresetGens; ++i)
            {
                if (pBank->presetGens[i].generator == genType)
                {
                    presetHasLocal = TRUE;
                    break;
                }
            }
        }

        return presetHasLocal ? presetLocal : instValue; // Preset overrides only when explicitly present
    }
    return instValue + presetLocal; // Additive for tuning, vol, etc.
}

// Determine the preset-level global generator range, if present.
// The first preset bag is considered a global zone when it has no INSTRUMENT generator.
static void PV_GetPresetGlobalGenRange(SF2_Bank *pBank, int32_t presetIndex,
                                      uint32_t *pGStart, uint32_t *pGEnd, XBOOL *pHasGlobal)
{
    if (pGStart)
        *pGStart = 0;
    if (pGEnd)
        *pGEnd = 0;
    if (pHasGlobal)
        *pHasGlobal = FALSE;
    if (!pBank || presetIndex < 0 || presetIndex >= (int32_t)pBank->numPresets)
        return;

    SF2_Preset *preset = &pBank->presets[presetIndex];
    uint32_t bagStart = preset->bagIndex;
    uint32_t bagEnd = (presetIndex + 1 < pBank->numPresets) ? pBank->presets[presetIndex + 1].bagIndex : pBank->numPresetBags;
    if (bagStart >= bagEnd || bagStart >= pBank->numPresetBags)
        return;

    SF2_Bag *firstBag = &pBank->presetBags[bagStart];
    uint32_t gStart = firstBag->genIndex;
    uint32_t gEnd = (bagStart + 1 < pBank->numPresetBags) ? pBank->presetBags[bagStart + 1].genIndex : pBank->numPresetGens;

    int16_t instInFirst = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens,
                                                 gStart, gEnd, SF2_GEN_INSTRUMENT, -1);
    if (instInFirst < 0)
    {
        if (pGStart)
            *pGStart = gStart;
        if (pGEnd)
            *pGEnd = gEnd;
        if (pHasGlobal)
            *pHasGlobal = TRUE;
    }
}

// Convert SF2 timecents to microseconds (engine ADSR time unit)
// timecents tc -> seconds = 2^(tc/1200); microseconds = seconds * 1e6
static inline uint32_t PV_SF2_TimecentsToUSec(int16_t timecents)
{
    // Per spec, -12000 means 0 seconds exactly
    if (timecents <= -12000)
        return 0;
    // Clamp extreme values to safe bounds
    if (timecents < -12000)
        timecents = -12000;

    // Correct SF2 formula: seconds = 2^(timecents/1200)
    float seconds = powf(2.0f, (float)timecents / 1200.0f);
    if (seconds < 0.0f)
        seconds = 0.0f;

    double usec = (double)seconds * 1000000.0;
    if (usec < 0.0)
        usec = 0.0;
    // Don't arbitrarily clamp to small human-friendly values here; return the
    // correctly computed microseconds per spec. Clamp only to the return type
    // limits to avoid overflow in callers.
    const double kMaxUsec = (double)UINT32_MAX;
    if (usec > kMaxUsec)
        usec = kMaxUsec;
    if (usec < 1.0)
        usec = 1.0;

    return (uint32_t)usec;
}

// Convert attenuation in centibels to a linear level scaled against fullLevel
// level = fullLevel * 10^(-cB/2000)
XSDWORD PV_SF2_LevelFromCentibels(int16_t centibels, XSDWORD fullLevel)
{
// If fullLevel is 0, return 0 (no volume to adjust)
    if (fullLevel == 0)
        return 0;

    // Handle centibels = 0 explicitly to avoid floating-point errors
    if (centibels == 0)
        return fullLevel;

    // Calculate gain based on centibels
    float gain = powf(10.0f, -(float)centibels / 2000.0f);
    double lvl = (double)fullLevel * (double)gain;

    // Clamp to fullLevel if lvl exceeds it
    if (lvl > (double)fullLevel)
        lvl = (double)fullLevel;

    // Prevent lvl from dropping to 0 or below; use a small positive value
    if (lvl <= 0.0)
        lvl = 1.0; // Small positive value to avoid muting

    XSDWORD result = (XSDWORD)lvl;
    return result;
}

// Convert SF2 Hz frequency to LFO period in microseconds
// SF2 frequency is in absolute Hz (0.001 to 100 Hz)
// LFO period is in microseconds (1/freq * 1,000,000)
static inline uint32_t PV_SF2_FreqToLFOPeriod(int16_t frequency_centiHz)
{
    if (frequency_centiHz <= 0)
    {
        return 0; // Default to 8 second period (~0.125 Hz)
    }

    // Convert from centi-Hz to Hz: frequency_centiHz is in hundredths of Hz
    // Per SF2 spec: 0 = 8.176 Hz, positive values higher, negative values lower
    // 8.176 * 2^(val/1200) Hz
    float freq_hz = 8.176f * powf(2.0f, (float)frequency_centiHz / 1200.0f);

    // Clamp to reasonable range
    if (freq_hz < 0.001f)
        freq_hz = 0.001f; // 1000 second period max
    if (freq_hz > 100.0f)
        freq_hz = 100.0f; // 10ms period min

    uint32_t period_us = (uint32_t)(1000000.0f / freq_hz);

    BAE_PRINTF("SF2 Debug: LFO freq %d centiHz -> %.3f Hz -> %u µs period\n",
               (int)frequency_centiHz, freq_hz, period_us);

    return period_us;
}

// Fill SF2 LFO records for modulation and vibrato LFOs
static void PV_SF2_InitLFO(GM_LFO *l, uint32_t period_us, int16_t delay_tc)
{
    if (!l)
        return;
    XSetMemory(l, sizeof(GM_LFO), 0);
    l->period = period_us;
    l->waveShape = SINE_WAVE_REAL; // SF2 LFOs are sine waves
    l->DC_feed = 0;
    l->currentWaveValue = 0;
    l->currentTime = 0;
    l->LFOcurrentTime = 0;
    if (delay_tc > -12000)
    {
        uint32_t delayTime = PV_SF2_TimecentsToUSec(delay_tc);
        l->a.ADSRLevel[0] = 0;
        l->a.ADSRTime[0] = delayTime;
        l->a.ADSRFlags[0] = ADSR_EXPONENTIAL_RAMP;
        l->a.ADSRLevel[1] = 65536;
        l->a.ADSRTime[1] = 0;
        l->a.ADSRFlags[1] = ADSR_TERMINATE;
        l->a.currentLevel = 0;
    }
    else
    {
        l->a.ADSRLevel[0] = 65536;
        l->a.ADSRTime[0] = 0;
        l->a.ADSRFlags[0] = ADSR_TERMINATE;
        l->a.currentLevel = 65536;
    }
    l->a.currentTime = 0;
    l->a.currentPosition = 0;
    l->a.previousTarget = 0;
    l->a.mode = 0;
    l->a.sustainingDecayLevel = XFIXED_1;
}

// Implement SF2 Default Modulators (DMOD) according to SF2 specification
// These are always active unless overridden by PMOD/IMOD
static void PV_SF2_ApplyDefaultModulators(GM_Instrument *pInstrument)
{
    if (!pInstrument)
        return;

    uint32_t curveCount = pInstrument->curveRecordCount;
    
    // DMOD 1: Note-On Velocity -> Initial Attenuation (Volume)
    // SF2 Default: velocity 0 = quiet, velocity 127 = full volume
    if (curveCount < MAX_CURVES)
    {
        GM_TieTo *curve = &pInstrument->curve[curveCount];
        curve->tieFrom = SAMPLE_NUMBER;  // Use available constant (velocity will be handled elsewhere)
        curve->tieTo = VOLUME_ATTACK_TIME;
        curve->curveCount = 3;
        curve->from_Value[0] = 0;    // velocity 0
        curve->from_Value[1] = 64;   // velocity 64 (mid)
        curve->from_Value[2] = 127;  // velocity 127 (max)
        curve->to_Scalar[0] = 50;    // Very quiet for low velocity
        curve->to_Scalar[1] = 180;   // Normal for mid velocity  
        curve->to_Scalar[2] = 256;   // Full volume for high velocity
        curveCount++;
        BAE_PRINTF("SF2 Debug: DMOD - Added Velocity -> Volume curve\n");
    }

    // DMOD 2: Note-On Velocity -> Filter Cutoff (brightness control) 
    // Higher velocity = brighter sound (higher cutoff frequency)
    if (curveCount < MAX_CURVES)
    {
        GM_TieTo *curve = &pInstrument->curve[curveCount];
        curve->tieFrom = SAMPLE_NUMBER;  // Use available constant
        curve->tieTo = LPF_FREQUENCY;
        curve->curveCount = 3;
        curve->from_Value[0] = 0;    // velocity 0
        curve->from_Value[1] = 64;   // velocity 64 (mid)
        curve->from_Value[2] = 127;  // velocity 127 (max)
        curve->to_Scalar[0] = 180;   // Darker for low velocity
        curve->to_Scalar[1] = 256;   // Normal for mid velocity
        curve->to_Scalar[2] = 320;   // Brighter for high velocity
        curveCount++;
        BAE_PRINTF("SF2 Debug: DMOD - Added Velocity -> Filter Cutoff curve\n");
    }

    // DMOD 3: MOD Wheel (CC1) -> Vibrato LFO Pitch Depth
    // Standard SF2 behavior: MOD wheel controls vibrato depth
    if (curveCount < MAX_CURVES)
    {
        GM_TieTo *curve = &pInstrument->curve[curveCount];
        curve->tieFrom = MOD_WHEEL_CONTROL;
        curve->tieTo = PITCH_LFO;
        curve->curveCount = 3;
        curve->from_Value[0] = 0;    // MOD wheel at 0
        curve->from_Value[1] = 64;   // MOD wheel at mid
        curve->from_Value[2] = 127;  // MOD wheel at max
        curve->to_Scalar[0] = 0;     // No vibrato
        curve->to_Scalar[1] = 128;   // Half vibrato
        curve->to_Scalar[2] = 256;   // Full vibrato (about 50 cents)
        curveCount++;
        BAE_PRINTF("SF2 Debug: DMOD - Added MOD Wheel -> Vibrato curve\n");
    }

    // DMOD 4: Volume LFO for tremolo control (if present)
    if (curveCount < MAX_CURVES)
    {
        GM_TieTo *curve = &pInstrument->curve[curveCount];
        curve->tieFrom = MOD_WHEEL_CONTROL;
        curve->tieTo = VOLUME_LFO;
        curve->curveCount = 2;
        curve->from_Value[0] = 0;    // MOD wheel at 0
        curve->from_Value[1] = 127;  // MOD wheel at max
        curve->to_Scalar[0] = 0;     // No tremolo
        curve->to_Scalar[1] = 128;   // Light tremolo
        curveCount++;
        BAE_PRINTF("SF2 Debug: DMOD - Added MOD Wheel -> Tremolo curve\n");
    }

    pInstrument->curveRecordCount = curveCount;
    BAE_PRINTF("SF2 Debug: DMOD - Applied %u default modulators, total curves: %u\n", 
               curveCount - pInstrument->curveRecordCount, curveCount);
}

// SF2 Modulator source/destination constants (from SF2 spec)
// Source Operator bits
#define SF2_MOD_CC              0x0000  // MIDI Controller
#define SF2_MOD_GENERAL         0x0000  // General controller
#define SF2_MOD_VELOCITY        0x0002  // Note-on velocity
#define SF2_MOD_KEY             0x0003  // Key number
#define SF2_MOD_POLY_PRESSURE   0x000A  // Poly pressure
#define SF2_MOD_CHANNEL_PRESSURE 0x000D // Channel pressure

// Transform operators
#define SF2_TRANSFORM_LINEAR    0x0000

// Helper function to decode SF2 modulator source
static int PV_SF2_DecodeModulatorSource(uint16_t srcOper, uint16_t *outControllerNum, XBOOL *outIsCC)
{
    // Extract the controller type from the source operator
    uint16_t controllerType = srcOper & 0x7F; // Lower 7 bits
    uint16_t controllerPalette = (srcOper >> 7) & 0x01; // Bit 7: 0=general, 1=MIDI CC
    
    *outIsCC = (controllerPalette == 1);
    *outControllerNum = controllerType;
    
    // Map SF2 controller types to BAE curve constants
    if (!*outIsCC) {
        // General controllers
        switch (controllerType) {
            case 2: return SAMPLE_NUMBER;     // Note-on velocity (use as sample number for curves)
            case 3: return PITCH_LFO;         // Key number (use pitch LFO as proxy)
            case 10: return SAMPLE_NUMBER;    // Poly pressure (limited support)
            case 13: return SAMPLE_NUMBER;    // Channel pressure (limited support)
            default: return -1;               // Unsupported
        }
    } else {
        // MIDI CC controllers
        switch (controllerType) {
            case 1: return MOD_WHEEL_CONTROL; // Mod wheel
            case 7: return VOLUME_LFO;        // Volume (use volume LFO as proxy)
            case 10: return VOLUME_LFO;       // Pan (limited support)
            case 11: return VOLUME_LFO;       // Expression (limited support)
            default: return -1;               // Unsupported CC
        }
    }
}

// Helper function to map SF2 generator destination to BAE curve destination
static int PV_SF2_MapGeneratorToCurveDestination(uint16_t genType)
{
    switch (genType) {
        case SF2_GEN_MOD_LFO_TO_PITCH:      return PITCH_LFO;
        case SF2_GEN_VIB_LFO_TO_PITCH:      return PITCH_LFO;
        case SF2_GEN_MOD_LFO_TO_FILTER_FC:  return LPF_FREQUENCY;
        case SF2_GEN_MOD_LFO_TO_VOLUME:     return VOLUME_LFO;
        case SF2_GEN_INITIAL_ATTENUATION:   return VOLUME_ATTACK_TIME;
        case SF2_GEN_PAN:                   return STEREO_PAN_LFO;
        case SF2_GEN_INITIAL_FILTER_FC:     return LPF_FREQUENCY;
        default:                            return -1; // Unsupported destination
    }
}

// Process SF2 modulators (PMOD/IMOD) and convert them to BAE curves
static void PV_SF2_ProcessModulators(SF2_Bank *pBank, GM_Instrument *pInstrument,
                                    uint32_t presetGenStart, uint32_t presetGenEnd,
                                    int32_t instrumentID, uint32_t instGenStart, uint32_t instGenEnd)
{
    if (!pBank || !pInstrument)
        return;

    uint32_t curveCount = pInstrument->curveRecordCount;
    
    // Process Preset Modulators (PMOD) first
    if (pBank->presetBags && pBank->presetMods) {
        // Find preset bags that contain this instrument
        for (uint32_t bagIdx = 0; bagIdx < pBank->numPresetBags; bagIdx++) {
            SF2_Bag *bag = &pBank->presetBags[bagIdx];
            uint32_t genStart = bag->genIndex;
            uint32_t genEnd = (bagIdx + 1 < pBank->numPresetBags) ? 
                             pBank->presetBags[bagIdx + 1].genIndex : pBank->numPresetGens;
                             
            // Check if this bag references our instrument
            int32_t bagInstID = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, 
                                                     genStart, genEnd, SF2_GEN_INSTRUMENT, -1);
            if (bagInstID == instrumentID) {
                // Process modulators for this bag
                uint32_t modStart = bag->modIndex;
                uint32_t modEnd = (bagIdx + 1 < pBank->numPresetBags) ?
                                 pBank->presetBags[bagIdx + 1].modIndex : pBank->numPresetMods;
                
                for (uint32_t modIdx = modStart; modIdx < modEnd && curveCount < MAX_CURVES; modIdx++) {
                    SF2_Modulator *mod = &pBank->presetMods[modIdx];
                    
                    // Decode source and destination
                    uint16_t controllerNum;
                    XBOOL isCC;
                    int tieFromValue = PV_SF2_DecodeModulatorSource(mod->srcOper, &controllerNum, &isCC);
                    int tieToValue = PV_SF2_MapGeneratorToCurveDestination(mod->destOper);
                    
                    if (tieFromValue >= 0 && tieToValue >= 0 && mod->amount != 0) {
                        // Create a BAE curve for this modulator
                        GM_TieTo *curve = &pInstrument->curve[curveCount];
                        curve->tieFrom = tieFromValue;
                        curve->tieTo = tieToValue;
                        curve->curveCount = 2;
                        curve->from_Value[0] = 0;
                        curve->from_Value[1] = 127;
                        curve->to_Scalar[0] = 0;
                        // Scale the SF2 amount to BAE curve scalar (SF2 amounts are in various units)
                        curve->to_Scalar[1] = (mod->amount > 0) ? 
                            XMIN(512, (mod->amount * 256) / 100) : 
                            XMAX(0, 256 + ((mod->amount * 256) / 100));
                        
                        curveCount++;
                        BAE_PRINTF("SF2 Debug: PMOD - Added modulator: src=0x%04X -> dest=%u, amount=%d\n",
                                   mod->srcOper, mod->destOper, mod->amount);
                    }
                }
            }
        }
    }
    
    // Process Instrument Modulators (IMOD)
    if (pBank->instBags && pBank->instMods && instrumentID >= 0 && instrumentID < (int32_t)pBank->numInstruments) {
        SF2_Instrument *instrument = &pBank->instruments[instrumentID];
        uint32_t bagStart = instrument->bagIndex;
        uint32_t bagEnd = (instrumentID + 1 < (int32_t)pBank->numInstruments) ? 
                         pBank->instruments[instrumentID + 1].bagIndex : pBank->numInstBags;
        
        for (uint32_t bagIdx = bagStart; bagIdx < bagEnd; bagIdx++) {
            SF2_Bag *bag = &pBank->instBags[bagIdx];
            uint32_t genStart = bag->genIndex;
            uint32_t genEnd = (bagIdx + 1 < pBank->numInstBags) ?
                             pBank->instBags[bagIdx + 1].genIndex : pBank->numInstGens;
            
            // Check if this bag overlaps with our generator range
            if (genStart <= instGenEnd && genEnd >= instGenStart) {
                uint32_t modStart = bag->modIndex;
                uint32_t modEnd = (bagIdx + 1 < pBank->numInstBags) ?
                                 pBank->instBags[bagIdx + 1].modIndex : pBank->numInstMods;
                
                for (uint32_t modIdx = modStart; modIdx < modEnd && curveCount < MAX_CURVES; modIdx++) {
                    SF2_Modulator *mod = &pBank->instMods[modIdx];
                    
                    // Decode source and destination
                    uint16_t controllerNum;
                    XBOOL isCC;
                    int tieFromValue = PV_SF2_DecodeModulatorSource(mod->srcOper, &controllerNum, &isCC);
                    int tieToValue = PV_SF2_MapGeneratorToCurveDestination(mod->destOper);
                    
                    if (tieFromValue >= 0 && tieToValue >= 0 && mod->amount != 0) {
                        // Create a BAE curve for this modulator
                        GM_TieTo *curve = &pInstrument->curve[curveCount];
                        curve->tieFrom = tieFromValue;
                        curve->tieTo = tieToValue;
                        curve->curveCount = 2;
                        curve->from_Value[0] = 0;
                        curve->from_Value[1] = 127;
                        curve->to_Scalar[0] = 0;
                        // Scale the SF2 amount to BAE curve scalar
                        curve->to_Scalar[1] = (mod->amount > 0) ? 
                            XMIN(512, (mod->amount * 256) / 100) : 
                            XMAX(0, 256 + ((mod->amount * 256) / 100));
                        
                        curveCount++;
                        BAE_PRINTF("SF2 Debug: IMOD - Added modulator: src=0x%04X -> dest=%u, amount=%d\n",
                                   mod->srcOper, mod->destOper, mod->amount);
                    }
                }
            }
        }
    }
    
    pInstrument->curveRecordCount = curveCount;
    BAE_PRINTF("SF2 Debug: PMOD/IMOD - Total curves after modulators: %u\n", curveCount);
}

static void PV_SF2_FillLFORecords(SF2_Bank *pBank, int32_t instrumentID, uint32_t genStart, uint32_t genEnd,
                                  GM_Instrument *pInstrument)
{
    if (!pBank || !pInstrument)
        return;

    int lfoCount = 0;
    GM_LFO *pLFO;

    // Get modulation LFO generators (merged local + instrument-global)
    int16_t modLfoDelay = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_DELAY_MOD_LFO, -12000);
    int16_t modLfoFreq = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_FREQ_MOD_LFO, 0);
    int16_t modLfoToPitch = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_MOD_LFO_TO_PITCH, 0);
    int16_t modLfoToVolume = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_MOD_LFO_TO_VOLUME, 0);
    int16_t modLfoToFilterFc = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_MOD_LFO_TO_FILTER_FC, 0);

    // Get vibrato LFO generators (merged)
    int16_t vibLfoDelay = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_DELAY_VIB_LFO, -12000);
    int16_t vibLfoFreq = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_FREQ_VIB_LFO, 0);
    int16_t vibLfoToPitch = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_VIB_LFO_TO_PITCH, 0);

    BAE_PRINTF("SF2 Debug: LFO generators (merged) - ModLFO: delay=%d, freq=%d, toPitch=%d, toVol=%d, toFilter=%d\n",
               modLfoDelay, modLfoFreq, modLfoToPitch, modLfoToVolume, modLfoToFilterFc);
    BAE_PRINTF("SF2 Debug: LFO generators (merged) - VibLFO: delay=%d, freq=%d, toPitch=%d\n",
               vibLfoDelay, vibLfoFreq, vibLfoToPitch);

    // Create modulation LFOs for each destination specified (pitch, volume, filter)
    if (modLfoToPitch != 0 && lfoCount < MAX_LFOS)
    {
        pLFO = &pInstrument->LFORecords[lfoCount];
        PV_SF2_InitLFO(pLFO, PV_SF2_FreqToLFOPeriod(modLfoFreq), modLfoDelay);
        pLFO->where_to_feed = PITCH_LFO;
        pLFO->level = modLfoToPitch * 4; // cents -> engine units
        BAE_PRINTF("SF2 Debug: Created mod LFO %d -> PITCH: level=%d, period=%u µs, delay=%d tc\n",
                   lfoCount, (int)pLFO->level, pLFO->period, modLfoDelay);
        lfoCount++;
    }
    if (modLfoToVolume != 0 && lfoCount < MAX_LFOS)
    {
        pLFO = &pInstrument->LFORecords[lfoCount];
        PV_SF2_InitLFO(pLFO, PV_SF2_FreqToLFOPeriod(modLfoFreq), modLfoDelay);
        pLFO->where_to_feed = VOLUME_LFO;
        pLFO->level = modLfoToVolume * 16; // cB -> engine units
        BAE_PRINTF("SF2 Debug: Created mod LFO %d -> VOLUME: level=%d, period=%u µs, delay=%d tc\n",
                   lfoCount, (int)pLFO->level, pLFO->period, modLfoDelay);
        lfoCount++;
    }
    if (modLfoToFilterFc != 0 && lfoCount < MAX_LFOS)
    {
        pLFO = &pInstrument->LFORecords[lfoCount];
        PV_SF2_InitLFO(pLFO, PV_SF2_FreqToLFOPeriod(modLfoFreq), modLfoDelay);
        pLFO->where_to_feed = LPF_FREQUENCY;
        pLFO->level = modLfoToFilterFc * 4; // cents -> engine units
        BAE_PRINTF("SF2 Debug: Created mod LFO %d -> LPF_FREQUENCY: level=%d, period=%u µs, delay=%d tc\n",
                   lfoCount, (int)pLFO->level, pLFO->period, modLfoDelay);
        lfoCount++;
    }

    // Create vibrato LFO for pitch if specified and different from mod LFO
    if (vibLfoToPitch != 0 && lfoCount < MAX_LFOS)
    {
        pLFO = &pInstrument->LFORecords[lfoCount];
        PV_SF2_InitLFO(pLFO, PV_SF2_FreqToLFOPeriod(vibLfoFreq), vibLfoDelay);
        pLFO->where_to_feed = PITCH_LFO;
        pLFO->level = vibLfoToPitch * 4; // cents -> engine units

        BAE_PRINTF("SF2 Debug: Created vibrato LFO %d: level=%d, period=%u µs, delay=%d tc\n",
                   lfoCount, (int)pLFO->level, pLFO->period, vibLfoDelay);

        lfoCount++;
    }

    // Create basic vibrato LFO if there's frequency but no explicit pitch depth
    // The DMOD system will handle MOD wheel control separately
    if (vibLfoToPitch == 0 && vibLfoFreq != 0 && lfoCount < MAX_LFOS)
    {
        // Create vibrato LFO with default maximum depth - DMOD will scale it
        const int defaultVibDepthCents = 50; // typical default per SF2 default modulators
        pLFO = &pInstrument->LFORecords[lfoCount];
        PV_SF2_InitLFO(pLFO, PV_SF2_FreqToLFOPeriod(vibLfoFreq), vibLfoDelay);
        pLFO->where_to_feed = PITCH_LFO;
        pLFO->level = defaultVibDepthCents * 4; // cents -> engine units

        BAE_PRINTF("SF2 Debug: Created default vibrato LFO %d (depth %d cents), period=%u µs, delay=%d tc\n",
                   lfoCount, defaultVibDepthCents, pLFO->period, vibLfoDelay);

        lfoCount++;
    }

    // Create basic ModLFO if there's frequency but no explicit destinations
    // Modulators (PMOD/IMOD) might control its routing and depth
    if (modLfoToPitch == 0 && modLfoToVolume == 0 && modLfoToFilterFc == 0 && 
        modLfoFreq != 0 && lfoCount < MAX_LFOS)
    {
        // Create ModLFO with default routing to pitch - modulators can override this
        const int defaultModDepthCents = 25; // typical default for ModLFO
        pLFO = &pInstrument->LFORecords[lfoCount];
        PV_SF2_InitLFO(pLFO, PV_SF2_FreqToLFOPeriod(modLfoFreq), modLfoDelay);
        pLFO->where_to_feed = PITCH_LFO;  // Default destination, can be overridden by modulators
        pLFO->level = defaultModDepthCents * 4; // cents -> engine units

        BAE_PRINTF("SF2 Debug: Created default ModLFO %d (depth %d cents), period=%u µs, delay=%d tc\n",
                   lfoCount, defaultModDepthCents, pLFO->period, modLfoDelay);

        lfoCount++;
    }

    pInstrument->LFORecordCount = lfoCount;

    if (lfoCount == 0)
    {
        BAE_PRINTF("SF2 Debug: Created 0 LFO records for instrument (no non-zero SF2 LFO depths)\n");
    }
    else
    {
        BAE_PRINTF("SF2 Debug: Created %d LFO records for instrument\n", lfoCount);
    }
}

// Build a GM_ADSR volume envelope from SF2 volume envelope generators
static void PV_SF2_FillVolumeADSR(SF2_Bank *pBank, int32_t instrumentID, uint32_t genStart, uint32_t genEnd, GM_ADSR *pADSR)
{
    if (!pBank || !pADSR)
        return;

    // Defaults per SF2 spec - now using merged (local overrides instrument-global)
    int16_t tcDelay = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_DELAY_VOL_ENV, -12000);
    int16_t tcAttack = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_ATTACK_VOL_ENV, -12000);
    int16_t tcHold = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_HOLD_VOL_ENV, -12000);
    int16_t tcDecay = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_DECAY_VOL_ENV, -12000);
    int16_t cBSus = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_SUSTAIN_VOL_ENV, 0);
    int16_t tcRel = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_RELEASE_VOL_ENV, -12000);
    int16_t cBInitAtt = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_INITIAL_ATTENUATION, 0);

    // Clamp timecents to prevent extreme values that could cause issues
    // Only enforce the SF2 lower sentinel meaning of -12000 (instant/zero).
    if (tcDelay < -12000)
        tcDelay = -12000;
    if (tcAttack < -12000)
        tcAttack = -12000;
    if (tcHold < -12000)
        tcHold = -12000;
    if (tcDecay < -12000)
        tcDecay = -12000;
    if (tcRel < -12000)
        tcRel = -12000;

    // Convert to engine units
    uint32_t tDelay = PV_SF2_TimecentsToUSec(tcDelay);
    uint32_t tAttack = PV_SF2_TimecentsToUSec(tcAttack);
    uint32_t tHold = PV_SF2_TimecentsToUSec(tcHold);
    uint32_t tDecay = PV_SF2_TimecentsToUSec(tcDecay);
    uint32_t tRel = PV_SF2_TimecentsToUSec(tcRel);
    uint32_t cBInitAttScaled = XFIXED_1;
    if (cBInitAtt != 0) {
        cBInitAttScaled = PV_SF2_LevelFromCentibels(cBInitAtt, XFIXED_1);        
    }
    

    BAE_PRINTF("SF2 Debug: Raw generators - Delay:%d, Attack:%d, Hold:%d, Decay:%d, Sustain:%d, Release:%d, InitAtt:%d\n",
               (int)tcDelay, (int)tcAttack, (int)tcHold, (int)tcDecay, (int)cBSus, (int)tcRel, (int)cBInitAtt);

    BAE_PRINTF("SF2 Debug: Converted times - tDelay:%uus, tAttack:%uus, tHold:%uus, tDecay:%uus, tRel:%uus\n",
               tDelay, tAttack, tHold, tDecay, tRel);

    // Ensure minimum timing to prevent zero-time stages and single-slice jumps.
    // Use the engine's slice time so ramps span at least one slice.
    const uint32_t kMinStageUS = (uint32_t)BUFFER_SLICE_TIME;
    if (tAttack > 0 && tAttack < kMinStageUS)
        tAttack = kMinStageUS;
    if (tDecay > 0 && tDecay < kMinStageUS)
        tDecay = kMinStageUS;
    if (tRel > 0 && tRel < kMinStageUS)
        tRel = kMinStageUS;

    BAE_PRINTF("SF2 Debug: Final times - tAttack:%uus, tDecay:%uus, tRel:%uus\n",
               tAttack, tDecay, tRel);

    BAE_PRINTF("SF2 Debug: cbInitAtt: %d, cbInitAttScaled: %d\n",
               cBInitAtt, cBInitAttScaled);

    pADSR->sustainingDecayLevel = cBInitAttScaled; 
    // Initialize ADSR - start from silence and ramp up
    pADSR->currentTime = 0;
    pADSR->currentPosition = 0;
    pADSR->currentLevel = 0; // Start from silence
    pADSR->previousTarget = 0;
    pADSR->mode = 0;
    // SF2-specific: Set flag and cB levels
    pADSR->isSF2Envelope = TRUE;
    pADSR->currentLevelCB = 14400;


    // Build proper SF2 ADSR: Delay -> Attack -> Hold -> Decay -> Sustain -> Release
    // Skip very short delay/hold stages to avoid engine timing issues
    int stageIndex = 0;

    // Stage: Delay (optional - stay at silence) - skip if default
    if (stageIndex < ADSR_STAGES && tcDelay > -12000)
    { // Only if non-default
        pADSR->ADSRLevelCB[stageIndex] = 14400; // Start from silence (14400 cB)
        pADSR->ADSRTime[stageIndex] = tDelay;
        pADSR->ADSRFlags[stageIndex] = ADSR_EXPONENTIAL_RAMP;
        stageIndex++;
        BAE_PRINTF("SF2 Debug: Added delay stage %d: %uus\n", stageIndex - 1, tDelay);
    }

    // Stage: Attack (silence -> peak level) - always present
    if (stageIndex < ADSR_STAGES)
    {
        // Attack: Ramp att from 14400 to 0 cB (linear dB decrease)
        pADSR->ADSRLevelCB[stageIndex] = 0;
        pADSR->ADSRTime[stageIndex] = tAttack;
        pADSR->ADSRFlags[stageIndex] = ADSR_EXPONENTIAL_RAMP;
        stageIndex++;
    }

    // Stage: Hold (optional - stay at peak) - skip if default
    if (stageIndex < ADSR_STAGES && tcHold > -12000)
    { // Only if non-default
        pADSR->ADSRLevelCB[stageIndex] = 0;
        pADSR->ADSRTime[stageIndex] = tHold;
        pADSR->ADSRFlags[stageIndex] = ADSR_EXPONENTIAL_RAMP;
        stageIndex++;
        BAE_PRINTF("SF2 Debug: Added hold stage %d: %uus\n", stageIndex - 1, tHold);
    }

    // Stage: Decay (peak -> sustain level, if needed)
    if (stageIndex < ADSR_STAGES)
    {
        pADSR->ADSRLevelCB[stageIndex] = cBSus - cBInitAtt;
        pADSR->ADSRTime[stageIndex] = tDecay;
        pADSR->ADSRFlags[stageIndex] = ADSR_EXPONENTIAL_RAMP;
        stageIndex++;
    }

    if (stageIndex < ADSR_STAGES)
    {
        
        pADSR->ADSRLevelCB[stageIndex] = cBSus - cBInitAtt;
        pADSR->ADSRTime[stageIndex] = 0; // Indefinite time
        pADSR->ADSRFlags[stageIndex] = ADSR_SUSTAIN;
        stageIndex++;
    }

    // Stage: Release (peak -> silence on note-off)
    if (stageIndex < ADSR_STAGES)
    {
        pADSR->ADSRLevelCB[stageIndex] = 14400;
        pADSR->ADSRTime[stageIndex] = tRel;
        pADSR->ADSRFlags[stageIndex] = ADSR_RELEASE;
        stageIndex++;
        BAE_PRINTF("SF2 Debug: Added release stage %d: %uus -> 0\n", stageIndex - 1, tRel);
    }

    // Terminate remaining stages
    for (int i = stageIndex; i < ADSR_STAGES; i++)
    {
        pADSR->ADSRLevelCB[i] = 14400;
        pADSR->ADSRTime[i] = 1;
        pADSR->ADSRFlags[i] = ADSR_TERMINATE;
    }
}
\
// Build a GM_ADSR modulation envelope from SF2 modulation envelope generators
static void PV_SF2_FillModulationADSR(SF2_Bank *pBank, int32_t instrumentID, uint32_t genStart, uint32_t genEnd, GM_ADSR *pADSR)
{
    if (!pBank || !pADSR)
        return;

    // Get SF2 modulation envelope generators (merged local + instrument-global)
    int16_t tcDelay = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_DELAY_MOD_ENV, -12000);
    int16_t tcAttack = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_ATTACK_MOD_ENV, -12000);
    int16_t tcHold = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_HOLD_MOD_ENV, -12000);
    int16_t tcDecay = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_DECAY_MOD_ENV, -12000);
    int16_t cBSus = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_SUSTAIN_MOD_ENV, 0);
    int16_t tcRel = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_RELEASE_MOD_ENV, -12000);

    // Clamp timecents to prevent extreme values
    if (tcDelay < -12000) tcDelay = -12000;
    if (tcAttack < -12000) tcAttack = -12000;
    if (tcHold < -12000) tcHold = -12000;
    if (tcDecay < -12000) tcDecay = -12000;
    if (tcRel < -12000) tcRel = -12000;

    // Convert to engine units
    uint32_t tDelay = PV_SF2_TimecentsToUSec(tcDelay);
    uint32_t tAttack = PV_SF2_TimecentsToUSec(tcAttack);
    uint32_t tHold = PV_SF2_TimecentsToUSec(tcHold);
    uint32_t tDecay = PV_SF2_TimecentsToUSec(tcDecay);
    uint32_t tRel = PV_SF2_TimecentsToUSec(tcRel);

    BAE_PRINTF("SF2 Debug: ModEnv generators - Delay:%d, Attack:%d, Hold:%d, Decay:%d, Sustain:%d, Release:%d\n",
               (int)tcDelay, (int)tcAttack, (int)tcHold, (int)tcDecay, (int)cBSus, (int)tcRel);

    // Ensure minimum timing to prevent zero-time stages
    const uint32_t kMinStageUS = (uint32_t)BUFFER_SLICE_TIME;
    if (tAttack > 0 && tAttack < kMinStageUS) tAttack = kMinStageUS;
    if (tDecay > 0 && tDecay < kMinStageUS) tDecay = kMinStageUS;
    if (tRel > 0 && tRel < kMinStageUS) tRel = kMinStageUS;

    // Initialize modulation envelope - start from zero modulation
    pADSR->sustainingDecayLevel = XFIXED_1;
    pADSR->currentTime = 0;
    pADSR->currentPosition = 0;
    pADSR->currentLevel = 0; // Start from zero modulation
    pADSR->previousTarget = 0;
    pADSR->mode = 0;
    // Mark as SF2 envelope
    pADSR->isSF2Envelope = TRUE;
    pADSR->currentLevelCB = 0; // Modulation envelope uses 1/10ths of a percent, not centibels

    // Build SF2 modulation ADSR: Delay -> Attack -> Hold -> Decay -> Sustain -> Release
    int stageIndex = 0;

    // Stage: Delay (optional - stay at zero modulation)
    if (stageIndex < ADSR_STAGES && tcDelay > -12000)
    {
        pADSR->ADSRLevel[stageIndex] = 0; // Start from zero
        pADSR->ADSRTime[stageIndex] = tDelay;
        pADSR->ADSRFlags[stageIndex] = ADSR_EXPONENTIAL_RAMP;
        stageIndex++;
        BAE_PRINTF("SF2 Debug: ModEnv added delay stage %d: %uus\n", stageIndex - 1, tDelay);
    }

    // Stage: Attack (zero -> peak modulation) - always present
    if (stageIndex < ADSR_STAGES)
    {
        // Attack: Ramp from 0 to 1000 (100.0% modulation)
        pADSR->ADSRLevel[stageIndex] = 1000; // 100.0% in 1/10ths of percent
        pADSR->ADSRTime[stageIndex] = tAttack;
        pADSR->ADSRFlags[stageIndex] = ADSR_EXPONENTIAL_RAMP;
        stageIndex++;
    }

    // Stage: Hold (optional - stay at peak)
    if (stageIndex < ADSR_STAGES && tcHold > -12000)
    {
        pADSR->ADSRLevel[stageIndex] = 1000;
        pADSR->ADSRTime[stageIndex] = tHold;
        pADSR->ADSRFlags[stageIndex] = ADSR_EXPONENTIAL_RAMP;
        stageIndex++;
        BAE_PRINTF("SF2 Debug: ModEnv added hold stage %d: %uus\n", stageIndex - 1, tHold);
    }

    // Stage: Decay (peak -> sustain level)
    if (stageIndex < ADSR_STAGES)
    {
        pADSR->ADSRLevel[stageIndex] = cBSus; // Sustain level in 1/10ths of percent
        pADSR->ADSRTime[stageIndex] = tDecay;
        pADSR->ADSRFlags[stageIndex] = ADSR_EXPONENTIAL_RAMP;
        stageIndex++;
    }

    // Stage: Sustain (stay at sustain level)
    if (stageIndex < ADSR_STAGES)
    {
        pADSR->ADSRLevel[stageIndex] = cBSus;
        pADSR->ADSRTime[stageIndex] = 0; // Indefinite time
        pADSR->ADSRFlags[stageIndex] = ADSR_SUSTAIN;
        stageIndex++;
    }

    // Stage: Release (sustain -> zero on note-off)
    if (stageIndex < ADSR_STAGES)
    {
        pADSR->ADSRLevel[stageIndex] = 0;
        pADSR->ADSRTime[stageIndex] = tRel;
        pADSR->ADSRFlags[stageIndex] = ADSR_RELEASE;
        stageIndex++;
        BAE_PRINTF("SF2 Debug: ModEnv added release stage %d: %uus -> 0\n", stageIndex - 1, tRel);
    }

    // Terminate remaining stages
    for (int i = stageIndex; i < ADSR_STAGES; i++)
    {
        pADSR->ADSRLevel[i] = 0;
        pADSR->ADSRTime[i] = 1;
        pADSR->ADSRFlags[i] = ADSR_TERMINATE;
    }
}

// Heuristic: decide if a preset likely represents a drum kit
static XBOOL PV_PresetLooksLikeDrumKit(SF2_Bank *pBank, uint32_t presetIndex)
{
    if (!pBank || presetIndex >= pBank->numPresets)
        return FALSE;
    SF2_Preset *preset = &pBank->presets[presetIndex];
    // Strong signal: SF2 bank 128 is percussion by convention
    if (preset->bank == 128)
        return TRUE;
    // Name hints
    if (PV_StrContainsIgnoreCase(preset->name, "drum"))
        return TRUE;
    if (PV_StrContainsIgnoreCase(preset->name, "kit"))
        return TRUE;
    if (PV_StrContainsIgnoreCase(preset->name, "perc"))
        return TRUE; // percussion, percussive

    // Structural hint: many zones spanning wide key range
    uint32_t bagStart = preset->bagIndex;
    uint32_t bagEnd = (presetIndex + 1 < pBank->numPresets) ? pBank->presets[presetIndex + 1].bagIndex : pBank->numPresetBags;
    uint32_t instCount = 0;
    uint8_t minKey = 127, maxKey = 0;
    // Deeper hints: instrument-level zones with fixed key or narrow ranges are typical of kits
    uint32_t totalInstZones = 0;
    uint32_t fixedKeyOrNarrowZones = 0; // GEN_KEYNUM or width <= 1
    uint32_t exclusiveZones = 0;        // non-zero SF2_GEN_EXCLUSIVE_CLASS
    for (uint32_t bagIdx = bagStart; bagIdx < bagEnd; ++bagIdx)
    {
        if (bagIdx >= pBank->numPresetBags)
            break;
        SF2_Bag *bag = &pBank->presetBags[bagIdx];
        uint32_t genStart = bag->genIndex;
        uint32_t genEnd = (bagIdx + 1 < pBank->numPresetBags) ? pBank->presetBags[bagIdx + 1].genIndex : pBank->numPresetGens;
        int32_t instrumentID = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, genStart, genEnd, SF2_GEN_INSTRUMENT, -1);
        if (instrumentID >= 0 && instrumentID < (int32_t)pBank->numInstruments)
        {
            instCount++;
            int16_t keyRange = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, genStart, genEnd, SF2_GEN_KEY_RANGE, 0x7F00);
            uint8_t keyLo = keyRange & 0xFF;
            uint8_t keyHi = (keyRange >> 8) & 0xFF;
            if (keyRange == 0x7F00 || keyHi == 0)
            {
                keyLo = 0;
                keyHi = 127;
            }
            if (keyLo < minKey)
                minKey = keyLo;
            if (keyHi > maxKey)
                maxKey = keyHi;

            // Walk instrument zones to gather kit-like traits
            SF2_Instrument *inst = &pBank->instruments[instrumentID];
            uint32_t iBagStart = inst->bagIndex;
            uint32_t iBagEnd = (instrumentID + 1 < (int32_t)pBank->numInstruments) ? pBank->instruments[instrumentID + 1].bagIndex : pBank->numInstBags;
            for (uint32_t ib = iBagStart; ib < iBagEnd; ++ib)
            {
                if (ib >= pBank->numInstBags)
                    break;
                SF2_Bag *ibag = &pBank->instBags[ib];
                uint32_t igStart = ibag->genIndex;
                uint32_t igEnd = (ib + 1 < pBank->numInstBags) ? pBank->instBags[ib + 1].genIndex : pBank->numInstGens;
                // Only count zones with a sample
                int16_t sID = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, igStart, igEnd, SF2_GEN_SAMPLE_ID, -1);
                if (sID < 0 || sID >= (int16_t)pBank->numSamples)
                    continue;
                totalInstZones++;
                int16_t zKeyRange = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, igStart, igEnd, SF2_GEN_KEY_RANGE, 0x7F00);
                uint8_t zLo = zKeyRange & 0xFF;
                uint8_t zHi = (zKeyRange >> 8) & 0xFF;
                if (zKeyRange == 0x7F00 || zHi == 0)
                {
                    zLo = 0;
                    zHi = 127;
                }
                int16_t zKeyNum = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, igStart, igEnd, SF2_GEN_KEYNUM, -1);
                if ((zKeyNum >= 0 && zKeyNum <= 127) || (zLo <= zHi && (uint8_t)(zHi - zLo) <= 1))
                {
                    fixedKeyOrNarrowZones++;
                }
                int16_t excl = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, igStart, igEnd, SF2_GEN_EXCLUSIVE_CLASS, 0);
                if (excl != 0)
                    exclusiveZones++;
            }
        }
    }
    // Many instrument zones spanning a wide key range is typical of a drum kit preset
    if (instCount >= 8 && (maxKey > minKey) && ((int)maxKey - (int)minKey) >= 24)
        return TRUE;
    // Stronger kit signal: lots of fixed-key/narrow zones and/or exclusive classes
    if (totalInstZones >= 6)
    {
        float fixedRatio = (float)fixedKeyOrNarrowZones / (float)totalInstZones;
        if (fixedRatio >= 0.5f)
            return TRUE;
        if (exclusiveZones >= 2)
            return TRUE;
    }
    return FALSE;
}
static OPErr PV_ReadSF2Chunk(XFILE file, SF2_ChunkHeader *header)
{
    OPErr err = NO_ERR;

    if (XFileRead(file, header, sizeof(SF2_ChunkHeader)) != 0)
    {
        err = BAD_FILE;
    }

#if X_WORD_ORDER == TRUE // little endian - need to swap ID for FOURCC comparison, but not size
    header->id = XSwapLong(header->id);
    // Don't swap size
#else // big endian
    header->size = XSwapLong(header->size);
#endif

    return err;
}

static OPErr PV_ReadSF2Samples(XFILE file, uint32_t size, SF2_Bank *pBank)
{
    OPErr err = NO_ERR;

    pBank->samplesSize = size;
    pBank->samples = (char *)XNewPtr(size);
    if (pBank->samples == NULL)
    {
        return MEMORY_ERR;
    }

    if (XFileRead(file, pBank->samples, size) != 0)
    {
        XDisposePtr(pBank->samples);
        pBank->samples = NULL;
        err = BAD_FILE;
    }

    return err;
}

// Choose an effective root key for a zone/sample:
// Priority: zone overriding root key (as-is) > sample originalPitch > center of key range > 60
static int16_t PV_EffectiveRootKey(SF2_Bank *pBank, int32_t sampleID, int16_t zoneRootKey,
                                   uint8_t keyLo, uint8_t keyHi)
{
    if (zoneRootKey >= 0 && zoneRootKey <= 127)
    {
        // Per SF2 spec, the overriding root key defines the unity note for the zone
        // even if it lies outside the key range. Do not "correct" it.
        BAE_PRINTF("SF2 Debug EffectiveRootKey: Using zone override rootKey=%d (range %d-%d)\n",
                   zoneRootKey, keyLo, keyHi);
        return zoneRootKey;
    }

    if (pBank && sampleID >= 0 && sampleID < (int32_t)pBank->numSamples)
    {
        int16_t orig = (int16_t)pBank->sampleHeaders[sampleID].originalPitch;

        // Use originalPitch if present. It may be outside the key range; that's fine.
        if (orig >= 0 && orig <= 127)
        {
            BAE_PRINTF("SF2 Debug EffectiveRootKey: Using sample originalPitch=%d (zone %d-%d)\n", orig, keyLo, keyHi);
            return orig;
        }
        else
        {
            BAE_PRINTF("SF2 Debug EffectiveRootKey: Sample originalPitch=%d invalid, using fallback\n", orig);
        }
    }

    BAE_PRINTF("SF2 Debug EffectiveRootKey: Using default rootKey=60\n");
    return 60;
}

static OPErr PV_ReadSF2Array(XFILE file, uint32_t size, void **ppData, uint32_t elementSize, uint32_t *pCount)
{
    OPErr err = NO_ERR;
    uint32_t count = size / elementSize;

    *pCount = count;
    *ppData = XNewPtr(size);
    if (*ppData == NULL)
    {
        return MEMORY_ERR;
    }

    if (XFileRead(file, *ppData, size) != 0)
    {
        XDisposePtr(*ppData);
        *ppData = NULL;
        err = BAD_FILE;
    }

#if X_WORD_ORDER == FALSE // big endian - need to swap multi-byte fields
    // TODO: Add byte swapping for SF2 structures if needed on big-endian systems
#endif

    return err;
}

OPErr SF2_LoadBank(XFILENAME *file, SF2_Bank **ppBank)
{
    OPErr err = NO_ERR;
    XFILE fileRef = NULL;
    SF2_Bank *pBank = NULL;
    SF2_ChunkHeader chunk;
    uint32_t fourcc;

    if (file == NULL || ppBank == NULL)
    {
        return PARAM_ERR;
    }

    *ppBank = NULL;

    // Open file
    fileRef = XFileOpenForRead(file);
    if (fileRef == NULL)
    {
        return BAD_FILE;
    }

    // Allocate bank structure
    pBank = (SF2_Bank *)XNewPtr(sizeof(SF2_Bank));
    if (pBank == NULL)
    {
        XFileClose(fileRef);
        return MEMORY_ERR;
    }
    XSetMemory(pBank, sizeof(SF2_Bank), 0);

    // Read RIFF header
    err = PV_ReadSF2Chunk(fileRef, &chunk);
    if (err != NO_ERR || chunk.id != SF2_RIFF)
    {
        err = BAD_FILE_TYPE;
        goto cleanup;
    }

    // Read sfbk signature
    if (XFileRead(fileRef, &fourcc, sizeof(fourcc)) != 0)
    {
        err = BAD_FILE;
        goto cleanup;
    }
#if X_WORD_ORDER == TRUE // little endian - need to swap for FOURCC comparison
    fourcc = XSwapLong(fourcc);
#endif

    if (fourcc != SF2_SFBK)
    {
        err = BAD_FILE_TYPE;
        goto cleanup;
    } // Parse chunks
    while (XFileRead(fileRef, &chunk, sizeof(chunk)) == 0)
    {
        // SF2 files store FOURCC in little-endian, but our FOURCC constants are big-endian
        // So we need to swap the ID to match our constants, but not the size
#if X_WORD_ORDER == TRUE // little endian - need to swap ID for FOURCC comparison, but not size
        chunk.id = XSwapLong(chunk.id);
#else // big endian
        chunk.size = XSwapLong(chunk.size);
#endif

        switch (chunk.id)
        {
        case SF2_LIST:
            // Read list type
            if (XFileRead(fileRef, &fourcc, sizeof(fourcc)) != 0)
            {
                err = BAD_FILE;
                goto cleanup;
            }

#if X_WORD_ORDER == TRUE // little endian - need to swap for FOURCC comparison
            fourcc = XSwapLong(fourcc);
#else // big endian
            fourcc = XSwapLong(fourcc);
#endif

            BAE_PRINTF("SF2 Debug: LIST type: 0x%08X (SDTA=0x%08X, PDTA=0x%08X)\n",
                       fourcc, SF2_SDTA, SF2_PDTA);

            if (fourcc == SF2_SDTA)
            {
                // Sample data list - look for smpl chunk
                BAE_PRINTF("SF2 Debug: Found SDTA (sample data) section, size: %d\n", chunk.size - 4);
                SF2_ChunkHeader subchunk;
                long listEnd = XFileGetPosition(fileRef) + chunk.size - 4;

                while (XFileGetPosition(fileRef) < listEnd)
                {
                    if (XFileRead(fileRef, &subchunk, sizeof(subchunk)) != 0)
                        break;

                    BAE_PRINTF("SF2 Debug: SDTA subchunk raw ID: 0x%08X, size: %d\n",
                               subchunk.id, subchunk.size);

#if X_WORD_ORDER == TRUE // little endian - need to swap ID for FOURCC comparison, but not size
                    subchunk.id = XSwapLong(subchunk.id);
                    // Don't swap size
#else // big endian
                    subchunk.size = XSwapLong(subchunk.size);
#endif

                    BAE_PRINTF("SF2 Debug: SDTA subchunk after swap - ID: 0x%08X, size: %d\n",
                               subchunk.id, subchunk.size);

                    if (subchunk.id == SF2_SMPL)
                    {
                        BAE_PRINTF("SF2 Debug: Found SMPL chunk, reading samples\n");
                        err = PV_ReadSF2Samples(fileRef, subchunk.size, pBank);
                        if (err != NO_ERR)
                            goto cleanup;
                    }
                    else
                    {
                        BAE_PRINTF("SF2 Debug: Skipping unknown SDTA subchunk 0x%08X\n", subchunk.id);
                        XFileSetPositionRelative(fileRef, subchunk.size);
                    }
                }
                BAE_PRINTF("SF2 Debug: Finished parsing SDTA section\n");
            }
            else if (fourcc == SF2_PDTA)
            {
                // Preset data list
                long listEnd = XFileGetPosition(fileRef) + chunk.size - 4;
                SF2_ChunkHeader subchunk;

                while (XFileGetPosition(fileRef) < listEnd)
                {
                    if (XFileRead(fileRef, &subchunk, sizeof(subchunk)) != 0)
                        break;

#if X_WORD_ORDER == TRUE // little endian - need to swap ID for FOURCC comparison, but not size
                    subchunk.id = XSwapLong(subchunk.id);
                    // Don't swap size
#else // big endian
                    subchunk.size = XSwapLong(subchunk.size);
#endif

                    switch (subchunk.id)
                    {
                    case SF2_PHDR:
                        err = PV_ReadSF2Array(fileRef, subchunk.size, (void **)&pBank->presets,
                                              sizeof(SF2_Preset), &pBank->numPresets);
                        break;
                    case SF2_PBAG:
                        err = PV_ReadSF2Array(fileRef, subchunk.size, (void **)&pBank->presetBags,
                                              sizeof(SF2_Bag), &pBank->numPresetBags);
                        break;
                    case SF2_PMOD:
                        err = PV_ReadSF2Array(fileRef, subchunk.size, (void **)&pBank->presetMods,
                                              sizeof(SF2_Modulator), &pBank->numPresetMods);
                        break;
                    case SF2_PGEN:
                        err = PV_ReadSF2Array(fileRef, subchunk.size, (void **)&pBank->presetGens,
                                              sizeof(SF2_Generator), &pBank->numPresetGens);
                        break;
                    case SF2_INST:
                        err = PV_ReadSF2Array(fileRef, subchunk.size, (void **)&pBank->instruments,
                                              sizeof(SF2_Instrument), &pBank->numInstruments);
                        break;
                    case SF2_IBAG:
                        err = PV_ReadSF2Array(fileRef, subchunk.size, (void **)&pBank->instBags,
                                              sizeof(SF2_Bag), &pBank->numInstBags);
                        break;
                    case SF2_IMOD:
                        err = PV_ReadSF2Array(fileRef, subchunk.size, (void **)&pBank->instMods,
                                              sizeof(SF2_Modulator), &pBank->numInstMods);
                        break;
                    case SF2_IGEN:
                        err = PV_ReadSF2Array(fileRef, subchunk.size, (void **)&pBank->instGens,
                                              sizeof(SF2_Generator), &pBank->numInstGens);
                        break;
                    case SF2_SHDR:
                        err = PV_ReadSF2Array(fileRef, subchunk.size, (void **)&pBank->sampleHeaders,
                                              sizeof(SF2_Sample), &pBank->numSamples);
                        break;
                    default:
                        XFileSetPositionRelative(fileRef, subchunk.size);
                        break;
                    }

                    if (err != NO_ERR)
                        goto cleanup;
                }
            }
            else
            {
                // Skip unknown list
                XFileSetPositionRelative(fileRef, chunk.size - 4);
            }
            break;

        default:
            // Skip unknown chunk
            XFileSetPositionRelative(fileRef, chunk.size);
            break;
        }

        BAE_PRINTF("SF2 Debug: Current file position: %ld\n", XFileGetPosition(fileRef));
    }

    BAE_PRINTF("SF2 Debug: Finished parsing all chunks\n");

    BAE_PRINTF("SF2 Debug: Bank loaded successfully - %d presets, %d instruments, %d samples\n",
               pBank->numPresets, pBank->numInstruments, pBank->numSamples);

    *ppBank = pBank;
    XFileClose(fileRef);
    return NO_ERR;

cleanup:
    SF2_UnloadBank(pBank);
    if (fileRef)
        XFileClose(fileRef);
    return err;
}

void SF2_UnloadBank(SF2_Bank *pBank)
{
    if (pBank)
    {
        if (pBank->samples)
            XDisposePtr(pBank->samples);
        if (pBank->sampleHeaders)
            XDisposePtr(pBank->sampleHeaders);
        if (pBank->presets)
            XDisposePtr(pBank->presets);
        if (pBank->instruments)
            XDisposePtr(pBank->instruments);
        if (pBank->presetBags)
            XDisposePtr(pBank->presetBags);
        if (pBank->presetMods)
            XDisposePtr(pBank->presetMods);
        if (pBank->presetGens)
            XDisposePtr(pBank->presetGens);
        if (pBank->instBags)
            XDisposePtr(pBank->instBags);
        if (pBank->instMods)
            XDisposePtr(pBank->instMods);
        if (pBank->instGens)
            XDisposePtr(pBank->instGens);
        XDisposePtr(pBank);
    }
}

// Helper function to find generator value in a list
static int16_t PV_FindGeneratorValue(SF2_Generator *generators, uint32_t genCount,
                                     uint32_t startIndex, uint32_t endIndex,
                                     SF2_GeneratorType genType, int16_t defaultValue)
{
    // Debug: trace key/velocity range lookups to diagnose missing generators
    if (genType == SF2_GEN_KEY_RANGE || genType == SF2_GEN_VEL_RANGE)
    {
        BAE_PRINTF("SF2 Debug: Searching for generator type %d in range %u-%u (genCount=%u) with default 0x%04X\n",
                   genType, (unsigned)startIndex, (unsigned)endIndex, (unsigned)genCount, (unsigned)(uint16_t)defaultValue);
    }
    for (uint32_t i = startIndex; i < endIndex && i < genCount; i++)
    {
        if (generators[i].generator == genType)
        {
            if (genType == SF2_GEN_KEY_RANGE || genType == SF2_GEN_VEL_RANGE)
            {
                BAE_PRINTF("SF2 Debug: Found generator %d at index %u -> amount=0x%04X\n",
                           genType, (unsigned)i, (unsigned)generators[i].amount);
            }
            return (int16_t)generators[i].amount;
        }
    }
    if (genType == SF2_GEN_KEY_RANGE || genType == SF2_GEN_VEL_RANGE)
    {
        BAE_PRINTF("SF2 Debug: Generator %d not found in range %u-%u, returning default 0x%04X\n",
                   genType, (unsigned)startIndex, (unsigned)endIndex, (unsigned)(uint16_t)defaultValue);
    }
    return defaultValue;
}

// Check whether a specific generator type exists in the given generator index range
static XBOOL PV_HasGeneratorInRange(SF2_Generator *generators, uint32_t genCount,
                                    uint32_t startIndex, uint32_t endIndex,
                                    SF2_GeneratorType genType)
{
    if (!generators || startIndex >= endIndex || startIndex >= genCount)
        return FALSE;
    for (uint32_t i = startIndex; i < endIndex && i < genCount; ++i)
    {
        if (generators[i].generator == genType)
            return TRUE;
    }
    return FALSE;
}

// Determine the instrument-level global generator range, if present.
// The first instrument bag is considered a global zone when it has no SAMPLE_ID generator.
static void PV_GetInstGlobalGenRange(SF2_Bank *pBank, int32_t instrumentID,
                                     uint32_t *pGStart, uint32_t *pGEnd, XBOOL *pHasGlobal)
{
    if (pGStart)
        *pGStart = 0;
    if (pGEnd)
        *pGEnd = 0;
    if (pHasGlobal)
        *pHasGlobal = FALSE;
    if (!pBank || instrumentID < 0 || instrumentID >= (int32_t)pBank->numInstruments)
        return;

    SF2_Instrument *inst = &pBank->instruments[instrumentID];
    uint32_t bagStart = inst->bagIndex;
    uint32_t bagEnd = (instrumentID + 1 < (int32_t)pBank->numInstruments) ? pBank->instruments[instrumentID + 1].bagIndex : pBank->numInstBags;
    if (bagStart >= bagEnd || bagStart >= pBank->numInstBags)
        return;

    SF2_Bag *firstBag = &pBank->instBags[bagStart];
    uint32_t gStart = firstBag->genIndex;
    uint32_t gEnd = (bagStart + 1 < pBank->numInstBags) ? pBank->instBags[bagStart + 1].genIndex : pBank->numInstGens;

    int16_t sampleInFirst = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens,
                                                  gStart, gEnd, SF2_GEN_SAMPLE_ID, -1);
    if (sampleInFirst < 0)
    {
        if (pGStart)
            *pGStart = gStart;
        if (pGEnd)
            *pGEnd = gEnd;
        if (pHasGlobal)
            *pHasGlobal = TRUE;
    }
}

// Find generator in local zone, else fall back to instrument-global zone if present.
static int16_t PV_FindInstGenMerged(SF2_Bank *pBank, int32_t instrumentID, uint32_t localStart, uint32_t localEnd,
                                    SF2_GeneratorType genType, int16_t defaultValue)
{
    // Check local zone first
    int16_t localValue = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, localStart, localEnd, genType, defaultValue);

    // For override types, detect presence by scanning the generator entries rather than
    // relying on numeric equality with a default sentinel. This avoids endianness or
    // encoding differences causing false positives/negatives.
    if (genType == SF2_GEN_KEY_RANGE || genType == SF2_GEN_VEL_RANGE ||
        genType == SF2_GEN_SAMPLE_ID || genType == SF2_GEN_OVERRIDING_ROOT_KEY ||
        genType == SF2_GEN_KEYNUM || genType == SF2_GEN_SAMPLE_MODES)
    {
        if (PV_HasGeneratorInRange(pBank->instGens, pBank->numInstGens, localStart, localEnd, genType))
            return localValue;
    }

    // Get global zone
    XBOOL hasGlobal = FALSE;
    uint32_t globalStart, globalEnd;
    PV_GetInstGlobalGenRange(pBank, instrumentID, &globalStart, &globalEnd, &hasGlobal);
    int16_t globalValue = defaultValue;
    if (hasGlobal)
    {
        globalValue = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, globalStart, globalEnd, genType, defaultValue);
    }

    // Additive for tuning, volume, etc.; otherwise use local or global
    if (genType == SF2_GEN_COARSE_TUNE || genType == SF2_GEN_FINE_TUNE || 
        genType == SF2_GEN_INITIAL_ATTENUATION || 
        genType == SF2_GEN_MOD_LFO_TO_PITCH || genType == SF2_GEN_MOD_LFO_TO_VOLUME || 
        genType == SF2_GEN_MOD_LFO_TO_FILTER_FC || 
        genType == SF2_GEN_VIB_LFO_TO_PITCH || 
        genType == SF2_GEN_MOD_ENV_TO_PITCH || genType == SF2_GEN_MOD_ENV_TO_FILTER_FC)
    {
        return localValue + globalValue; // Additive
    }

    return (localValue != defaultValue) ? localValue : globalValue; // Override
}

// Simple linear interpolation resampler to convert samples to target rate
// Resampling support removed:
// Previously this file implemented an internal linear resampler. To simplify the codebase
// and avoid duplicate resampling logic, the resampler has been removed. Keep a small
// compatibility stub so callers that expect the function signature continue to build.
static SBYTE *PV_ResampleSample(SBYTE *inputData, uint32_t inputFrames, uint32_t inputRate,
                                uint32_t targetRate, SBYTE bitsPerSample, SBYTE channels, uint32_t *outputFrames)
{
    // No resampling performed. Return the original buffer and report the same frame count.
    if (outputFrames)
        *outputFrames = inputFrames;
    return inputData;
}

// Convert SF2 sample to miniBAE format
// Apply optional instrument fineTune (in cents) in addition to sample->pitchCorrection
static SBYTE *PV_ConvertSF2Sample(SF2_Bank *pBank, SF2_Sample *sample, int16_t instFineTune,
                                  uint32_t effectiveStart, uint32_t effectiveEnd,
                                  uint32_t *outSize, uint32_t *outTargetRate, OPErr *pErr)
{
    SBYTE *convertedSample = NULL;
    uint32_t sampleSize;
    uint32_t originalFrames;

    if (!pBank || !sample || !outSize || !outTargetRate || !pErr)
    {
        if (pErr)
            *pErr = PARAM_ERR;
        return NULL;
    }

    // Calculate original sample size in frames and bytes (SF2 samples are 16-bit mono)
    // Honor effectiveStart/effectiveEnd if provided; otherwise use sample header bounds
    uint32_t srcStart = (effectiveStart != 0 || effectiveEnd != 0) ? effectiveStart : sample->start;
    uint32_t srcEnd = (effectiveStart != 0 || effectiveEnd != 0) ? effectiveEnd : sample->end;
    if (srcEnd <= srcStart)
    {
        BAE_PRINTF("SF2 Debug: PV_ConvertSF2Sample invalid range: start=%u end=%u; forcing minimal frame\n", srcStart, srcEnd);
        if (srcEnd <= srcStart)
            srcEnd = srcStart + 1;
    }
    originalFrames = srcEnd - srcStart;
    sampleSize = originalFrames * 2; // 2 bytes per sample

    BAE_PRINTF("SF2 Debug: Converting sample - start=%u, end=%u, frames=%u, original rate=%d\n",
               (unsigned)srcStart, (unsigned)srcEnd, (unsigned)originalFrames, sample->sampleRate);

    /* Extra diagnostics: report sample link/type which may indicate stereo linked samples */
    BAE_PRINTF("SF2 Debug: sample header: originalPitch=%u pitchCorrection=%d sampleLink=%u sampleType=0x%04X\n",
               (unsigned)sample->originalPitch, (int)sample->pitchCorrection,
               (unsigned)sample->sampleLink, (unsigned)sample->sampleType);

    if (sampleSize == 0 || srcStart >= pBank->samplesSize / 2)
    {
        BAE_PRINTF("SF2 Debug: Sample conversion failed - invalid size or start position\n");
        *pErr = BAD_SAMPLE;
        return NULL;
    }

    // Allocate memory for original sample data
    convertedSample = (SBYTE *)XNewPtr(sampleSize);
    if (!convertedSample)
    {
        *pErr = MEMORY_ERR;
        return NULL;
    }

    // Copy sample data from SF2 bank (samples are stored as 16-bit signed)
    int16_t *sf2Samples = (int16_t *)pBank->samples;
    int16_t *outputSamples = (int16_t *)convertedSample;

    for (uint32_t i = 0; i < originalFrames; i++)
    {
        uint32_t sf2Index = srcStart + i;
        if (sf2Index < pBank->samplesSize / 2)
        {
            outputSamples[i] = sf2Samples[sf2Index];
        }
        else
        {
            outputSamples[i] = 0; // Silence for out-of-bounds
        }
    }

    // Determine initial target rate from sample header
    uint32_t targetRate = sample->sampleRate;
    if (sample->sampleRate > 48000)
    {
        targetRate = 48000; // Downsample very high rates
    }
    else if (sample->sampleRate < 8000)
    {
        targetRate = 8000; // Upsample very low rates
    }
    sampleSize = originalFrames * 2;

    // Quick check if sample has any non-zero data
    int nonZeroCount = 0;
    int16_t *finalSamples = (int16_t *)convertedSample;
    uint32_t finalFrames = sampleSize / 2;
    for (uint32_t i = 0; i < finalFrames && i < 100; i++) // Check first 100 samples
    {
        if (finalSamples[i] != 0)
            nonZeroCount++;
    }
    // NOTE: pitchCorrection and instrument fine-tune are already applied before resampling
    // (if provided via the instFineTune parameter). Do not re-apply sample->pitchCorrection here,
    // it would double-count the cents and cause octave-scale shifts.

    *outSize = sampleSize;
    *outTargetRate = targetRate;
    *pErr = NO_ERR;
    return convertedSample;
}

GM_Instrument *SF2_CreateInstrumentFromPreset(SF2_Bank *pBank, uint16_t bankNum, uint16_t presetNum, OPErr *pErr)
{
    GM_Instrument *pInstrument = NULL;
    OPErr err = BAD_INSTRUMENT;
    uint32_t presetIndex;
    SF2_Preset *preset = NULL;

    if (!pBank || !pErr)
    {
        if (pErr)
            *pErr = PARAM_ERR;
        return NULL;
    }

    // Find the preset
    for (presetIndex = 0; presetIndex < pBank->numPresets; presetIndex++)
    {
        if (pBank->presets[presetIndex].bank == bankNum &&
            pBank->presets[presetIndex].preset == presetNum)
        {
            preset = &pBank->presets[presetIndex];
            break;
        }
    }

    if (!preset)
    {
        *pErr = BAD_INSTRUMENT;
        return NULL;
    }

    BAE_PRINTF("SF2 Debug: Creating instrument from preset '%s' (bank=%u, program=%u)\n",
               preset->name, (unsigned)bankNum, (unsigned)presetNum);

    // Process preset bags to find all instrument zones
    uint32_t bagStart = preset->bagIndex;
    uint32_t bagEnd = (presetIndex + 1 < pBank->numPresets) ? pBank->presets[presetIndex + 1].bagIndex : pBank->numPresetBags;

    // Collect all valid instrument IDs from this preset
    int32_t instrumentIDs[16]; // Maximum 16 instruments per preset (should be plenty)
    uint32_t instrumentCount = 0;

    for (uint32_t bagIdx = bagStart; bagIdx < bagEnd && instrumentCount < 16; bagIdx++)
    {
        if (bagIdx >= pBank->numPresetBags)
            break;

        SF2_Bag *bag = &pBank->presetBags[bagIdx];
        uint32_t genStart = bag->genIndex;
        uint32_t genEnd = (bagIdx + 1 < pBank->numPresetBags) ? pBank->presetBags[bagIdx + 1].genIndex : pBank->numPresetGens;

        // Look for instrument generator
        int32_t instrumentID = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens,
                                                     genStart, genEnd, SF2_GEN_INSTRUMENT, -1);

        if (instrumentID != -1 && instrumentID < (int32_t)pBank->numInstruments)
        {
            instrumentIDs[instrumentCount++] = instrumentID;
            BAE_PRINTF("SF2 Debug: Found instrument %d in preset bag %u\n", (int)instrumentID, (unsigned)bagIdx);
        }
    }

    if (instrumentCount == 0)
    {
        BAE_PRINTF("SF2 Debug: No valid instruments found in preset '%s'\n", preset->name);
        *pErr = BAD_INSTRUMENT;
        return NULL;
    }

    BAE_PRINTF("SF2 Debug: Found %u instruments in preset, analyzing zones...\n", (unsigned)instrumentCount);

    // Count total zones across all instruments
    uint32_t totalZones = 0;
    for (uint32_t i = 0; i < instrumentCount; i++)
    {
        SF2_Instrument *instrument = &pBank->instruments[instrumentIDs[i]];
        uint32_t instBagStart = instrument->bagIndex;
        uint32_t instBagEnd = (instrumentIDs[i] + 1 < pBank->numInstruments) ? pBank->instruments[instrumentIDs[i] + 1].bagIndex : pBank->numInstBags;

        for (uint32_t bagIdx = instBagStart; bagIdx < instBagEnd; bagIdx++)
        {
            if (bagIdx >= pBank->numInstBags)
                break;

            SF2_Bag *bag = &pBank->instBags[bagIdx];
            uint32_t genStart = bag->genIndex;
            uint32_t genEnd = (bagIdx + 1 < pBank->numInstBags) ? pBank->instBags[bagIdx + 1].genIndex : pBank->numInstGens;

            // Check if this zone has a sample
            int16_t sampleID = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens,
                                                     genStart, genEnd, SF2_GEN_SAMPLE_ID, -1);
            if (sampleID != -1 && sampleID < pBank->numSamples)
            {
                totalZones++;
            }
        }
    }

    BAE_PRINTF("SF2 Debug: Total zones with samples: %u\n", (unsigned)totalZones);

    if (totalZones == 0)
    {
        BAE_PRINTF("SF2 Debug: No zones with valid samples found\n");
        *pErr = BAD_INSTRUMENT;
        return NULL;
    }

    // Decide between single instrument and multi-zone keymap split
    if (totalZones <= 1)
    {
        BAE_PRINTF("SF2 Debug: Single zone detected, creating simple instrument\n");
        {
            GM_Instrument *inst = PV_SF2_CreateSimpleInstrument(pBank, instrumentIDs, instrumentCount, pErr);
            if (inst)
                inst->isSF2Instrument = TRUE;
            return inst;
        }
    }
    else
    {
        BAE_PRINTF("SF2 Debug: Multiple zones detected (%u), creating keymap split instrument\n", (unsigned)totalZones);
        {
            GM_Instrument *inst = PV_SF2_CreateKeymapSplitInstrument(pBank, instrumentIDs, instrumentCount, presetNum, pErr);
            if (inst)
                inst->isSF2Instrument = TRUE;
            return inst;
        }
    }
}

OPErr SF2_GetPresetInfo(SF2_Bank *pBank, uint16_t index, char *name, uint16_t *bank, uint16_t *preset)
{
    if (!pBank || index >= pBank->numPresets)
    {
        return PARAM_ERR;
    }

    SF2_Preset *p = &pBank->presets[index];

    if (name)
    {
        XStrCpy(name, p->name);
    }
    if (bank)
    {
        *bank = p->bank;
    }
    if (preset)
    {
        *preset = p->preset;
    }

    return NO_ERR;
}

// Implementation of GM API functions for SF2 support
OPErr GM_LoadSF2Bank(XFILENAME *file, SF2_Bank **ppBank)
{
    return SF2_LoadBank(file, ppBank);
}

void GM_UnloadSF2Bank(SF2_Bank *pBank)
{
    SF2_UnloadBank(pBank);
}

OPErr GM_LoadSF2Instrument(GM_Song *pSong, SF2_Bank *pBank,
                           XLongResourceID instrument,
                           uint16_t sf2Bank, uint16_t sf2Preset)
{
    OPErr err = BAD_INSTRUMENT;
    GM_Instrument *pInstrument = NULL;

    if (!pSong || !pBank || instrument < 0 || instrument >= (MAX_INSTRUMENTS * MAX_BANKS))
    {
        return PARAM_ERR;
    }

    // Create instrument from SF2 preset
    pInstrument = SF2_CreateInstrumentFromPreset(pBank, sf2Bank, sf2Preset, &err);
    if (err != NO_ERR || !pInstrument)
    {
        return err;
    }

    // Install instrument into song
    if (pSong->instrumentData[instrument])
    {
        // Unload existing instrument
        GM_UnloadInstrument(pSong, instrument);
    }

    pInstrument->usageReferenceCount = 1;
    pSong->instrumentData[instrument] = pInstrument;

    return NO_ERR;
}

OPErr GM_GetSF2PresetInfo(SF2_Bank *pBank, uint16_t index,
                          char *name, uint16_t *bank, uint16_t *preset)
{
    return SF2_GetPresetInfo(pBank, index, name, bank, preset);
}

GM_Instrument *SF2_CreateInstrumentFromPresetWithNote(SF2_Bank *pBank, uint16_t bankNum, uint16_t presetNum, uint16_t note, OPErr *pErr)
{
    GM_Instrument *pInstrument = NULL;
    OPErr err = BAD_INSTRUMENT;
    uint32_t presetIndex;
    SF2_Preset *preset = NULL;

    if (!pBank || !pErr)
    {
        if (pErr)
            *pErr = PARAM_ERR;
        return NULL;
    }

    // Find the preset
    for (presetIndex = 0; presetIndex < pBank->numPresets; presetIndex++)
    {
        if (pBank->presets[presetIndex].bank == bankNum &&
            pBank->presets[presetIndex].preset == presetNum)
        {
            preset = &pBank->presets[presetIndex];
            break;
        }
    }

    if (!preset)
    {
        *pErr = BAD_INSTRUMENT;
        return NULL;
    }

    // Allocate GM_Instrument
    pInstrument = (GM_Instrument *)XNewPtr(sizeof(GM_Instrument));
    if (!pInstrument)
    {
        *pErr = MEMORY_ERR;
        return NULL;
    }

    // Initialize entire structure to zero first
    XSetMemory(pInstrument, sizeof(GM_Instrument), 0);

    BAE_PRINTF("SF2 Debug: Creating instrument for note %d from preset bank=%d, preset=%d\n",
               note, bankNum, presetNum);

    // Initialize basic instrument parameters (copy from working SF2 function)
    pInstrument->doKeymapSplit = FALSE;
    pInstrument->extendedFormat = FALSE; // Use standard format
    pInstrument->notPolyphonic = FALSE;
    // ENABLE sample rate factoring for proper pitch - SF2 samples can be any rate
    pInstrument->useSampleRate = TRUE;
    // For percussion one-shots, play at recorded rate (no musical transposition)
    pInstrument->playAtSampledFreq = (bankNum == 128) ? TRUE : FALSE;
    pInstrument->sampleAndHold = FALSE;
    pInstrument->usageReferenceCount = 0;

    // Set default pan to center
    pInstrument->panPlacement = 0; // Center pan

#if REVERB_USED != REVERB_DISABLED
    pInstrument->avoidReverb = FALSE;
#endif

    // Set ADSR from SF2 volume envelope generators for the selected zone later (after we know genStart/genEnd)

    // Process preset bags to find instrument zones that match the note
    uint32_t bagStart = preset->bagIndex;
    uint32_t bagEnd = (presetIndex + 1 < pBank->numPresets) ? pBank->presets[presetIndex + 1].bagIndex : pBank->numPresetBags;

    // Collect all instrument IDs from preset zones that include this note (don’t stop at first)
    int32_t candidateInstIDs[32];
    uint32_t candidateCount = 0;
    for (uint32_t bagIdx = bagStart; bagIdx < bagEnd && candidateCount < 32; bagIdx++)
    {
        if (bagIdx >= pBank->numPresetBags)
            break;
        SF2_Bag *bag = &pBank->presetBags[bagIdx];
        uint32_t genStart = bag->genIndex;
        uint32_t genEnd = (bagIdx + 1 < pBank->numPresetBags) ? pBank->presetBags[bagIdx + 1].genIndex : pBank->numPresetGens;
        int16_t keyRange = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, genStart, genEnd, SF2_GEN_KEY_RANGE, 0x7F00);
        uint8_t keyLo = keyRange & 0xFF;
        uint8_t keyHi = (keyRange >> 8) & 0xFF;
        if (keyRange == 0x7F00 || keyHi == 0)
        {
            keyLo = 0;
            keyHi = 127;
        }
        BAE_PRINTF("SF2 Debug: Zone %d key range: %d-%d (raw=0x%04X), looking for note %d\n", bagIdx, keyLo, keyHi, keyRange, note);
        if (note < keyLo || note > keyHi)
            continue;
        int32_t instID = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, genStart, genEnd, SF2_GEN_INSTRUMENT, -1);
        if (instID >= 0 && instID < (int32_t)pBank->numInstruments)
        {
            // Avoid duplicates
            XBOOL seen = FALSE;
            for (uint32_t t = 0; t < candidateCount; ++t)
                if (candidateInstIDs[t] == instID)
                {
                    seen = TRUE;
                    break;
                }
            if (!seen)
                candidateInstIDs[candidateCount++] = instID;
        }
    }
    if (candidateCount == 0)
    {
        BAE_PRINTF("SF2 Debug: No instrument zones cover note %d in preset bank=%d, preset=%d\n", note, bankNum, presetNum);
        XDisposePtr(pInstrument);
        *pErr = BAD_INSTRUMENT;
        return NULL;
    }
    // Search all candidate instruments’ zones for the best matching sample
    uint16_t bestSampleID = 0xFFFF;
    XBOOL bestFound = FALSE;
    uint32_t bestGenStart = 0, bestGenEnd = 0;
    int32_t bestInstID = -1;
    int bestScore = 0x7FFFFFFF; // lower is better
    int16_t bestRootKey = -1, bestFine = 0, bestCoarse = 0;
    for (uint32_t c = 0; c < candidateCount; ++c)
    {
        int32_t instID = candidateInstIDs[c];
        SF2_Instrument *instrument = &pBank->instruments[instID];
        uint32_t iBagStart = instrument->bagIndex;
        uint32_t iBagEnd = (instID + 1 < (int32_t)pBank->numInstruments) ? pBank->instruments[instID + 1].bagIndex : pBank->numInstBags;
        for (uint32_t bagIdx = iBagStart; bagIdx < iBagEnd; ++bagIdx)
        {
            if (bagIdx >= pBank->numInstBags)
                break;
            SF2_Bag *bag = &pBank->instBags[bagIdx];
            uint32_t genStart = bag->genIndex;
            uint32_t genEnd = (bagIdx + 1 < pBank->numInstBags) ? pBank->instBags[bagIdx + 1].genIndex : pBank->numInstGens;
            int16_t sID = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_SAMPLE_ID, -1);
            if (sID < 0 || sID >= (int16_t)pBank->numSamples)
                continue;
            int16_t zKeyNum = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_KEYNUM, -1);
            int16_t keyRange = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_KEY_RANGE, 0x7F00);
            uint8_t kLo = keyRange & 0xFF;
            uint8_t kHi = (keyRange >> 8) & 0xFF;
            if (keyRange == 0x7F00 || kHi == 0)
            {
                kLo = 0;
                kHi = 127;
            }
            // Compute score: exact GEN_KEYNUM match wins; otherwise note within range with narrower width preferred;
            // tie-breaker by distance to mid.
            int score = 0x7FFFFFFF;
            if (zKeyNum >= 0 && zKeyNum <= 127)
            {
                if ((uint16_t)zKeyNum == note)
                    score = 0;
                else
                    score = 1000 + ABS((int)note - (int)zKeyNum);
            }
            else if (note >= kLo && note <= kHi)
            {
                int width = (int)kHi - (int)kLo;
                int mid = (kLo + kHi) / 2;
                score = 100000 + (width << 8) + ABS((int)note - mid);
            }
            if (score < bestScore)
            {
                bestScore = score;
                bestFound = TRUE;
                bestSampleID = (uint16_t)sID;
                bestGenStart = genStart;
                bestGenEnd = genEnd;
                bestInstID = instID;
                int16_t zoneRootKey = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_OVERRIDING_ROOT_KEY, -1);
                bestRootKey = PV_EffectiveRootKey(pBank, sID, zoneRootKey, kLo, kHi);
                bestFine = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_FINE_TUNE, 0);
                bestCoarse = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_COARSE_TUNE, 0);
            }
            if (bestScore == 0)
                break; // exact hit
        }
        if (bestScore == 0)
            break;
    }
    if (!bestFound)
    {
        BAE_PRINTF("SF2 Debug: No sample zone found for note %d in any candidate instrument (count=%u)\n", note, (unsigned)candidateCount);
        XDisposePtr(pInstrument);
        *pErr = BAD_INSTRUMENT;
        return NULL;
    }
    // Adopt selection
    uint16_t sampleID = bestSampleID;
    uint32_t selectedGenStart = bestGenStart, selectedGenEnd = bestGenEnd;
    int16_t rootKey = bestRootKey;
    int16_t fineTune = bestFine;
    int16_t coarseTune = bestCoarse;
    XBOOL sampleFound = TRUE;

    // (Variables moved above to reflect best-of-all-instruments selection)

    if (!sampleFound)
    {
        // Look for sample zones that match this note
        for (uint32_t bagIdx = bagStart; bagIdx < bagEnd; bagIdx++)
        {
            if (bagIdx >= pBank->numInstBags)
                break;

            SF2_Bag *bag = &pBank->instBags[bagIdx];
            uint32_t genStart = bag->genIndex;
            uint32_t genEnd = (bagIdx + 1 < pBank->numInstBags) ? pBank->instBags[bagIdx + 1].genIndex : pBank->numInstGens;

            // Check key range for this sample zone
            int16_t keyRange = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens,
                                                     genStart, genEnd, SF2_GEN_KEY_RANGE, 0x7F00); // Default 0-127

            uint8_t keyLo = keyRange & 0xFF;
            uint8_t keyHi = (keyRange >> 8) & 0xFF;
            if (keyRange == 0x7F00 || keyHi == 0)
            {
                keyLo = 0;
                keyHi = 127;
            }

            BAE_PRINTF("SF2 Debug: Sample zone %d key range: %d-%d (raw=0x%04X)\n",
                       bagIdx, keyLo, keyHi, keyRange);

            // Prefer explicit fixed key (SF2_GEN_KEYNUM) when present
            int16_t zoneKeyNum = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens,
                                                       genStart, genEnd, SF2_GEN_KEYNUM, -1);
            if (zoneKeyNum >= 0 && zoneKeyNum <= 127)
            {
                if ((uint16_t)zoneKeyNum == note)
                {
                    sampleID = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens,
                                                     genStart, genEnd, SF2_GEN_SAMPLE_ID, 0);
                    if (sampleID < pBank->numSamples)
                    {
                        sampleFound = TRUE;
                        selectedGenStart = genStart;
                        selectedGenEnd = genEnd;
                        int16_t zoneRootKey = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens,
                                                                    genStart, genEnd, SF2_GEN_OVERRIDING_ROOT_KEY, -1);
                        rootKey = PV_EffectiveRootKey(pBank, sampleID, zoneRootKey, keyLo, keyHi);
                        fineTune = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_FINE_TUNE, 0);
                        coarseTune = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_COARSE_TUNE, 0);
                        BAE_PRINTF("SF2 Debug: Matched by GEN_KEYNUM=%d for note %d -> sample %d\n", zoneKeyNum, note, sampleID);
                        break;
                    }
                }
            }
            // Else fall back to key range check
            else if (note >= keyLo && note <= keyHi)
            {
                sampleID = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens,
                                                 genStart, genEnd, SF2_GEN_SAMPLE_ID, 0);
                if (sampleID < pBank->numSamples)
                {
                    sampleFound = TRUE;
                    selectedGenStart = genStart;
                    selectedGenEnd = genEnd;
                    // Prefer zones with explicit fixed GEN_KEYNUM matching the note
                    int16_t zKeyNum = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_KEYNUM, -1);
                    if (!(zKeyNum >= 0 && zKeyNum <= 127) || (uint16_t)zKeyNum == note)
                    {
                        // Get additional parameters and compute effective root key
                        int16_t zoneRootKey = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens,
                                                                    genStart, genEnd, SF2_GEN_OVERRIDING_ROOT_KEY, -1);
                        int16_t keyRange2 = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens,
                                                                  genStart, genEnd, SF2_GEN_KEY_RANGE, 0x7F00);
                        uint8_t kLo = keyRange2 & 0xFF;
                        uint8_t kHi = (keyRange2 >> 8) & 0xFF;
                        if (keyRange2 == 0x7F00 || kHi == 0)
                        {
                            kLo = 0;
                            kHi = 127;
                        }
                        rootKey = PV_EffectiveRootKey(pBank, sampleID, zoneRootKey, kLo, kHi);
                    }
                    fineTune = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens,
                                                     genStart, genEnd, SF2_GEN_FINE_TUNE, 0);
                    coarseTune = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens,
                                                       genStart, genEnd, SF2_GEN_COARSE_TUNE, 0);

                    BAE_PRINTF("SF2 Debug: Found matching sample %d for note %d (range %d-%d, rootKey=%d)\n",
                               sampleID, note, keyLo, keyHi, rootKey);
                    break;
                }
            }
        }

        if (!sampleFound || sampleID >= pBank->numSamples)
        {
            // Fallback A: pick the nearest zone by key range distance (common for drum kits with sparse mapping)
            uint32_t bestGenStart = 0, bestGenEnd = 0;
            int16_t bestSample = -1;
            uint32_t bestDistance = 0xFFFFFFFFU;
            int16_t bestRoot = -1;
            int16_t bestFine = 0, bestCoarse = 0;
            for (uint32_t bagIdx = bagStart; bagIdx < bagEnd; bagIdx++)
            {
                if (bagIdx >= pBank->numInstBags)
                    break;
                SF2_Bag *bag = &pBank->instBags[bagIdx];
                uint32_t genStart = bag->genIndex;
                uint32_t genEnd = (bagIdx + 1 < pBank->numInstBags) ? pBank->instBags[bagIdx + 1].genIndex : pBank->numInstGens;
                int16_t sID = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_SAMPLE_ID, -1);
                if (sID < 0 || sID >= (int16_t)pBank->numSamples)
                    continue;
                int16_t keyRange = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_KEY_RANGE, 0x7F00);
                uint8_t kLo = keyRange & 0xFF;
                uint8_t kHi = (keyRange >> 8) & 0xFF;
                if (keyRange == 0x7F00 || kHi == 0)
                {
                    kLo = 0;
                    kHi = 127;
                }
                // Use GEN_KEYNUM when present to measure distance; else distance to key range
                int16_t zKeyNum = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_KEYNUM, -1);
                uint32_t dist = 0;
                if (zKeyNum >= 0 && zKeyNum <= 127)
                {
                    dist = (note > (uint16_t)zKeyNum) ? (note - (uint16_t)zKeyNum) : ((uint16_t)zKeyNum - note);
                }
                else
                {
                    if (note < kLo)
                        dist = (uint32_t)(kLo - note);
                    else if (note > kHi)
                        dist = (uint32_t)(note - kHi);
                    else
                        dist = 0;
                }
                if (dist < bestDistance)
                {
                    bestDistance = dist;
                    bestSample = sID;
                    bestGenStart = genStart;
                    bestGenEnd = genEnd;
                    int16_t zoneRootKey = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_OVERRIDING_ROOT_KEY, -1);
                    bestRoot = PV_EffectiveRootKey(pBank, sID, zoneRootKey, kLo, kHi);
                    bestFine = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_FINE_TUNE, 0);
                    bestCoarse = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_COARSE_TUNE, 0);
                    if (bestDistance == 0)
                        break; // nothing better than exact
                }
            }
            if (bestSample >= 0)
            {
                sampleFound = TRUE;
                sampleID = (uint16_t)bestSample;
                selectedGenStart = bestGenStart;
                selectedGenEnd = bestGenEnd;
                rootKey = bestRoot;
                fineTune = bestFine;
                coarseTune = bestCoarse;
                BAE_PRINTF("SF2 Debug: Using nearest zone sample %d (distance %u) for note %d\n", sampleID, (unsigned)bestDistance, note);
            }
        }

        if (!sampleFound || sampleID >= pBank->numSamples)
        {
            // Fallback B: Try to find any sample in this instrument (global zone)
            BAE_PRINTF("SF2 Debug: No zone near note %d, trying global zone\n", note);
            for (uint32_t bagIdx = bagStart; bagIdx < bagEnd; bagIdx++)
            {
                if (bagIdx >= pBank->numInstBags)
                    break;
                SF2_Bag *bag = &pBank->instBags[bagIdx];
                uint32_t genStart = bag->genIndex;
                uint32_t genEnd = (bagIdx + 1 < pBank->numInstBags) ? pBank->instBags[bagIdx + 1].genIndex : pBank->numInstGens;
                sampleID = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_SAMPLE_ID, 0);
                if (sampleID < pBank->numSamples)
                {
                    sampleFound = TRUE;
                    selectedGenStart = genStart;
                    selectedGenEnd = genEnd;
                    int16_t zoneRootKey = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_OVERRIDING_ROOT_KEY, -1);
                    int16_t keyRange2 = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_KEY_RANGE, 0x7F00);
                    uint8_t kLo = keyRange2 & 0xFF;
                    uint8_t kHi = (keyRange2 >> 8) & 0xFF;
                    if (keyRange2 == 0x7F00 || kHi == 0)
                    {
                        kLo = 0;
                        kHi = 127;
                    }
                    rootKey = PV_EffectiveRootKey(pBank, sampleID, zoneRootKey, kLo, kHi);
                    fineTune = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_FINE_TUNE, 0);
                    coarseTune = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_COARSE_TUNE, 0);
                    BAE_PRINTF("SF2 Debug: Using global sample %d for note %d\n", sampleID, note);
                    break;
                }
            }
        }
    }

    if (!sampleFound || sampleID >= pBank->numSamples)
    {
        // No valid sample found for this note
        BAE_PRINTF("SF2 Debug: No sample found for note %d (bestInstID=%d)\n", note, bestInstID);
        XDisposePtr(pInstrument);
        *pErr = BAD_INSTRUMENT;
        return NULL;
    }

    // Build waveform using the same helper used by split instruments (handles offsets/loops)
    OPErr werr = PV_SF2_CreateWaveformFromSample(pBank, bestInstID, sampleID, selectedGenStart, selectedGenEnd, &pInstrument->u.w);
    if (werr != NO_ERR)
    {
        XDisposePtr(pInstrument);
        *pErr = werr;
        return NULL;
    }
    // Now that we know the exact zone (genStart/genEnd), fill the volume ADSR from SF2 generators
    PV_SF2_FillVolumeADSR(pBank, bestInstID, selectedGenStart, selectedGenEnd, &pInstrument->volumeADSRRecord);

    // Fill modulation envelope from SF2 generators
    PV_SF2_FillModulationADSR(pBank, bestInstID, selectedGenStart, selectedGenEnd, &pInstrument->modEnvelopeRecord);
    
    // Get modulation envelope to pitch/filter amounts
    pInstrument->modEnvelopeToPitch = PV_FindInstGenMerged(pBank, bestInstID, selectedGenStart, selectedGenEnd, SF2_GEN_MOD_ENV_TO_PITCH, 0) * 4; // cents -> engine units
    pInstrument->modEnvelopeToFilter = PV_FindInstGenMerged(pBank, bestInstID, selectedGenStart, selectedGenEnd, SF2_GEN_MOD_ENV_TO_FILTER_FC, 0) * 4; // cents -> engine units
    
    BAE_PRINTF("SF2 Debug: Note-specific ModEnv amounts - toPitch: %d, toFilter: %d\n", 
               (int)pInstrument->modEnvelopeToPitch, (int)pInstrument->modEnvelopeToFilter);

    // Fill LFO records from SF2 generators for this zone
    PV_SF2_FillLFORecords(pBank, bestInstID, selectedGenStart, selectedGenEnd, pInstrument);

    // Check SF2 sample modes to determine looping behavior
    // SF2 Spec: 0 = No looping, 1 = Continuous loop, 2 = Reserved, 3 = Loop + release
    int16_t sampleModes = PV_FindInstGenMerged(pBank, bestInstID, selectedGenStart, selectedGenEnd, SF2_GEN_SAMPLE_MODES, 0);
    if (sampleModes == 0 || sampleModes == 2) {
        // No looping or reserved mode - disable looping
        pInstrument->disableSndLooping = TRUE;
        BAE_PRINTF("SF2 Debug: Sample modes = %d, disabling looping\n", sampleModes);
    } else if (sampleModes == 1 || sampleModes == 3) {
        // Continuous loop or loop+release - enable looping
        pInstrument->disableSndLooping = FALSE;
        BAE_PRINTF("SF2 Debug: Sample modes = %d, enabling looping\n", sampleModes);
    } else {
        // Unknown mode - keep default behavior
        BAE_PRINTF("SF2 Debug: Unknown sample modes = %d, keeping default looping setting\n", sampleModes);
    }

    // Process SF2 preset and instrument modulators (PMOD/IMOD)
    PV_SF2_ProcessModulators(pBank, pInstrument, 0, 0, bestInstID, selectedGenStart, selectedGenEnd);
    
    // Apply SF2 default modulators (DMOD)
    PV_SF2_ApplyDefaultModulators(pInstrument);

    // Percussion: force base pitch to the triggering note so different keys select different samples,
    // not different transpositions of one sample.
    pInstrument->u.w.baseMidiPitch = note;

    BAE_PRINTF("SF2 Debug: Created note-specific instrument - note=%d, rootKey=%d, frames=%u\n",
               note, rootKey, (unsigned)pInstrument->u.w.waveFrames);
    pInstrument->isSF2Instrument = TRUE;

    *pErr = NO_ERR;
    return pInstrument;
}

// Global SF2 bank manager
static SF2_BankManager g_sf2Manager = {NULL, 0};

// SF2 Bank manager implementation
OPErr SF2_InitBankManager(void)
{
    g_sf2Manager.bankList = NULL;
    g_sf2Manager.numBanks = 0;
    return NO_ERR;
}

void SF2_ShutdownBankManager(void)
{
    SF2_BankNode *current = g_sf2Manager.bankList;
    while (current)
    {
        SF2_BankNode *next = current->next;
        SF2_UnloadBank(current->bank);
        if (current->filePath)
            XDisposePtr(current->filePath);
        XDisposePtr(current);
        current = next;
    }
    g_sf2Manager.bankList = NULL;
    g_sf2Manager.numBanks = 0;
}

OPErr SF2_AddBankToManager(SF2_Bank *bank, const char *filePath)
{
    SF2_BankNode *node;

    if (!bank)
        return PARAM_ERR;

    node = (SF2_BankNode *)XNewPtr(sizeof(SF2_BankNode));
    if (!node)
        return MEMORY_ERR;

    node->bank = bank;
    node->filePath = NULL;
    node->next = g_sf2Manager.bankList;

    if (filePath)
    {
        uint32_t pathLen = XStrLen(filePath) + 1;
        node->filePath = (char *)XNewPtr(pathLen);
        if (node->filePath)
        {
            XStrCpy(node->filePath, filePath);
        }
    }

    g_sf2Manager.bankList = node;
    g_sf2Manager.numBanks++;

    return NO_ERR;
}

void SF2_RemoveBankFromManager(SF2_Bank *bank)
{
    SF2_BankNode **current = &g_sf2Manager.bankList;

    while (*current)
    {
        if ((*current)->bank == bank)
        {
            SF2_BankNode *toDelete = *current;
            *current = (*current)->next;

            if (toDelete->filePath)
                XDisposePtr(toDelete->filePath);
            XDisposePtr(toDelete);
            g_sf2Manager.numBanks--;
            break;
        }
        current = &((*current)->next);
    }
}

SF2_Bank *SF2_FindBankByPath(const char *filePath)
{
    SF2_BankNode *current = g_sf2Manager.bankList;

    if (!filePath)
        return NULL;

    while (current)
    {
        if (current->filePath && XStrCmp(current->filePath, filePath) == 0)
        {
            return current->bank;
        }
        current = current->next;
    }

    return NULL;
}

OPErr SF2_LoadInstrumentFromAnyBank(uint16_t bankNum, uint16_t presetNum, GM_Instrument **ppInstrument)
{
    SF2_BankNode *current = g_sf2Manager.bankList;
    OPErr err = BAD_INSTRUMENT;

    if (!ppInstrument)
        return PARAM_ERR;
    *ppInstrument = NULL;

    // Search all loaded SF2 banks for the requested preset
    while (current)
    {
        SF2_Bank *bank = current->bank;

        // Check if this bank contains the requested preset
        for (uint32_t i = 0; i < bank->numPresets; i++)
        {
            if (bank->presets[i].bank == bankNum && bank->presets[i].preset == presetNum)
            {
                *ppInstrument = SF2_CreateInstrumentFromPreset(bank, bankNum, presetNum, &err);
                return err;
            }
        }

        current = current->next;
    }

    return BAD_INSTRUMENT;
}

// Private function to get SF2 instrument for MIDI playback
GM_Instrument *PV_GetSF2Instrument(GM_Song *pSong, XLongResourceID instrument, OPErr *pErr)
{
    SF2_BankNode *bankNode;
    GM_Instrument *pInstrument = NULL;
    uint16_t midiBank, midiProgram;

    if (!pSong || !pErr)
    {
        if (pErr)
            *pErr = PARAM_ERR;
        return NULL;
    }

    *pErr = BAD_INSTRUMENT;

    // Convert instrument ID to MIDI bank/program
    // miniBAE uses: instrument = (bank * 128) + program + note
    // For percussion: bank = (bank * 2) + 1, note is included
    // For melodic: bank = bank * 2, note = 0
    midiBank = (uint16_t)(instrument / 128);    // Bank number (internal mapping)
    midiProgram = (uint16_t)(instrument % 128); // Program number or note depending on mapping

    // Determine percussion intent from two signals:
    // 1) Internal odd-bank mapping (legacy miniBAE percussion mapping)
    // 2) Direct MIDI bank MSB 128 (SF2 percussion bank convention)
    XBOOL isOddBankPerc = ((midiBank % 2) == 1);
    XBOOL isMSB128Perc = FALSE;

    if (!isOddBankPerc)
    {
        // If not odd mapping, treat direct bank 128 as percussion
        // Convert back to MIDI bank first to test the external value
        uint16_t extBank = midiBank / 2; // internal even bank encodes extBank*2
        if (extBank == 128)
            isMSB128Perc = TRUE;
    }

    if (isOddBankPerc)
    {
        // Odd banks are percussion in miniBAE mapping
        uint16_t noteNumber = midiProgram; // In percussion mapping, program field carries the note
        midiBank = (midiBank - 1) / 2;     // Convert back to external MIDI bank
        // Route to SF2 percussion bank
        midiProgram = 0; // Standard drum kit preset
        midiBank = 128;  // SF2 percussion bank
    }
    else if (isMSB128Perc)
    {
        // Treat explicit MIDI bank 128 as percussion
        // Keep requested kit program if provided; use note from low 7 bits if present
        uint16_t extProgram = midiProgram; // may indicate kit variant
        uint16_t noteGuess = midiProgram;  // best-effort note guess from instrument encoding
        midiBank = 128;                    // enforce SF2 percussion bank
        midiProgram = extProgram;          // try requested kit first, fall back later if needed
    }
    else
    {
        // Melodic mapping
        midiBank = midiBank / 2; // Convert back to external MIDI bank
        // midiProgram stays as-is for melodic instruments
    }

    BAE_PRINTF("SF2 Debug: Looking for instrument %d -> bank=%d, program=%d\n",
               instrument, midiBank, midiProgram);

    // Search through loaded SF2 banks for a matching preset
    bankNode = g_sf2Manager.bankList;
    int bankCount = 0;
    while (bankNode)
    {
        SF2_Bank *sf2Bank = bankNode->bank;
        bankCount++;
        BAE_PRINTF("SF2 Debug: Checking SF2 bank %d with %u presets\n", bankCount, sf2Bank ? sf2Bank->numPresets : 0);

        if (sf2Bank && sf2Bank->presets)
        {
            // Look for preset matching this bank/program
            for (uint32_t i = 0; i < sf2Bank->numPresets; i++)
            {
                SF2_Preset *preset = &sf2Bank->presets[i];

                if (preset->bank == midiBank && preset->preset == midiProgram)
                {
                    BAE_PRINTF("SF2 Debug: Found matching preset! Creating instrument...\n");
                    // Found matching preset, create instrument
                    // Case A: odd internal mapping -> per-note drum (single sample instrument)
                    if (((instrument / 128) % 2) == 1)
                    {
                        uint16_t noteNumber = (uint16_t)(instrument % 128);
                        pInstrument = SF2_CreateInstrumentFromPresetWithNote(sf2Bank, midiBank, midiProgram, noteNumber, pErr);
                    }
                    // Case B: direct SF2 drum bank requested (bank 128) but not odd mapping -> build full kit (keymap split)
                    else if (preset->bank == 128)
                    {
                        pInstrument = SF2_CreateInstrumentFromPreset(sf2Bank, midiBank, midiProgram, pErr);
                    }
                    else
                    {
                        // Regular melodic instrument
                        pInstrument = SF2_CreateInstrumentFromPreset(sf2Bank, midiBank, midiProgram, pErr);
                    }

                    if (pInstrument && *pErr == NO_ERR)
                    {
                        BAE_PRINTF("SF2: Loaded instrument %d (bank=%d, program=%d) from SF2\n",
                                   instrument, midiBank, midiProgram);
                        return pInstrument;
                    }
                    else
                    {
                        BAE_PRINTF("SF2 Debug: Failed to create instrument, err=%d\n", *pErr);
                    }
                }
            }
        }
        bankNode = bankNode->next;
    }

    // If exact bank/program not found, try fallback strategies
    BAE_PRINTF("SF2 Debug: Exact match not found, trying fallbacks...\n");

    // If original intent was percussion, try percussion-specific fallbacks FIRST and bail out if found.
    if (isOddBankPerc || isMSB128Perc)
    {
        uint16_t noteNumber = (uint16_t)(instrument % 128);
        bankNode = g_sf2Manager.bankList;
        while (bankNode)
        {
            SF2_Bank *sf2Bank = bankNode->bank;
            if (sf2Bank && sf2Bank->presets)
            {
                // Pass 1: explicit bank 128
                for (uint32_t i = 0; i < sf2Bank->numPresets; i++)
                {
                    SF2_Preset *preset = &sf2Bank->presets[i];
                    if (preset->bank == 128)
                    {
                        pInstrument = SF2_CreateInstrumentFromPresetWithNote(sf2Bank, preset->bank, preset->preset, noteNumber, pErr);
                        if (pInstrument && *pErr == NO_ERR)
                            return pInstrument;
                    }
                }
                // Pass 2: heuristics on non-128 banks
                for (uint32_t i = 0; i < sf2Bank->numPresets; i++)
                {
                    if (sf2Bank->presets[i].bank == 128)
                        continue;
                    if (PV_PresetLooksLikeDrumKit(sf2Bank, i))
                    {
                        SF2_Preset *preset = &sf2Bank->presets[i];
                        pInstrument = SF2_CreateInstrumentFromPresetWithNote(sf2Bank, preset->bank, preset->preset, noteNumber, pErr);
                        if (pInstrument && *pErr == NO_ERR)
                            return pInstrument;
                    }
                }
            }
            bankNode = bankNode->next;
        }
        // If we intended percussion and couldn't find any, don't fall back to melodic instruments
        BAE_PRINTF("SF2 Debug: Percussion request but no kit found; not falling back to melodic.\n");
        *pErr = BAD_INSTRUMENT;
        return NULL;
    }

    // Fallback 1: Try program in bank 0 (General MIDI)
    if (midiBank != 0)
    {
        bankNode = g_sf2Manager.bankList;
        while (bankNode)
        {
            SF2_Bank *sf2Bank = bankNode->bank;
            if (sf2Bank && sf2Bank->presets)
            {
                for (uint32_t i = 0; i < sf2Bank->numPresets; i++)
                {
                    SF2_Preset *preset = &sf2Bank->presets[i];
                    if (preset->bank == 0 && preset->preset == midiProgram)
                    {
                        BAE_PRINTF("SF2 Debug: Found fallback in GM bank (bank=0, program=%d)\n", midiProgram);
                        pInstrument = SF2_CreateInstrumentFromPreset(sf2Bank, 0, midiProgram, pErr);
                        if (pInstrument && *pErr == NO_ERR)
                        {
                            return pInstrument;
                        }
                    }
                }
            }
            bankNode = bankNode->next;
        }
    }

    // Fallback X: Try matching by program number only (ignore bank).
    // Some SoundFonts don't populate the bank field consistently; try a looser match
    // before falling back to piano.
    bankNode = g_sf2Manager.bankList;
    while (bankNode)
    {
        SF2_Bank *sf2Bank = bankNode->bank;
        if (sf2Bank && sf2Bank->presets)
        {
            for (uint32_t i = 0; i < sf2Bank->numPresets; i++)
            {
                SF2_Preset *preset = &sf2Bank->presets[i];
                if (preset->preset == midiProgram)
                {
                    BAE_PRINTF("SF2 Debug: Found program-only fallback (program=%d) in bank=%d\n", midiProgram, preset->bank);
                    // For percussion we still need note-specific loading
                    if ((instrument / 128) % 2 == 1)
                    {
                        uint16_t noteNumber = (uint16_t)(instrument % 128);
                        pInstrument = SF2_CreateInstrumentFromPresetWithNote(sf2Bank, preset->bank, preset->preset, noteNumber, pErr);
                    }
                    else
                    {
                        pInstrument = SF2_CreateInstrumentFromPreset(sf2Bank, preset->bank, preset->preset, pErr);
                    }

                    if (pInstrument && *pErr == NO_ERR)
                    {
                        BAE_PRINTF("SF2: Loaded instrument via program-only fallback (bank=%d, program=%d)\n", preset->bank, preset->preset);
                        return pInstrument;
                    }
                }
            }
        }
        bankNode = bankNode->next;
    }

    // Fallback 2: Use piano (program 0) from any bank
    bankNode = g_sf2Manager.bankList;
    while (bankNode)
    {
        SF2_Bank *sf2Bank = bankNode->bank;
        if (sf2Bank && sf2Bank->presets)
        {
            for (uint32_t i = 0; i < sf2Bank->numPresets; i++)
            {
                SF2_Preset *preset = &sf2Bank->presets[i];
                if (preset->preset == 0) // Piano
                {
                    BAE_PRINTF("SF2 Debug: Using piano fallback (bank=%d, program=0)\n", preset->bank);
                    pInstrument = SF2_CreateInstrumentFromPreset(sf2Bank, preset->bank, 0, pErr);
                    if (pInstrument && *pErr == NO_ERR)
                    {
                        return pInstrument;
                    }
                }
            }
        }
        bankNode = bankNode->next;
    }

    // Percussion-specific fallback: if the original was percussion, try bank 128 first, then any drum-like preset
    if ((instrument / 128) % 2 == 1)
    {
        uint16_t noteNumber = (uint16_t)(instrument % 128);
        bankNode = g_sf2Manager.bankList;
        while (bankNode)
        {
            SF2_Bank *sf2Bank = bankNode->bank;
            if (sf2Bank && sf2Bank->presets)
            {
                // Pass 1: prefer explicit bank 128
                for (uint32_t i = 0; i < sf2Bank->numPresets; i++)
                {
                    SF2_Preset *preset = &sf2Bank->presets[i];
                    if (preset->bank == 128)
                    {
                        BAE_PRINTF("SF2 Debug: Percussion fallback using kit '%s' (bank=%u, prog=%u) for note %u\n",
                                   preset->name, (unsigned)preset->bank, (unsigned)preset->preset, (unsigned)noteNumber);
                        pInstrument = SF2_CreateInstrumentFromPresetWithNote(sf2Bank, preset->bank, preset->preset, noteNumber, pErr);
                        if (pInstrument && *pErr == NO_ERR)
                            return pInstrument;
                    }
                }
                // Pass 2: heuristics for non-128 banks
                for (uint32_t i = 0; i < sf2Bank->numPresets; i++)
                {
                    if (sf2Bank->presets[i].bank == 128)
                        continue; // already tried
                    if (PV_PresetLooksLikeDrumKit(sf2Bank, i))
                    {
                        SF2_Preset *preset = &sf2Bank->presets[i];
                        BAE_PRINTF("SF2 Debug: Percussion heuristic fallback using kit '%s' (bank=%u, prog=%u) for note %u\n",
                                   preset->name, (unsigned)preset->bank, (unsigned)preset->preset, (unsigned)noteNumber);
                        pInstrument = SF2_CreateInstrumentFromPresetWithNote(sf2Bank, preset->bank, preset->preset, noteNumber, pErr);
                        if (pInstrument && *pErr == NO_ERR)
                            return pInstrument;
                    }
                }
            }
            bankNode = bankNode->next;
        }
    }

    BAE_PRINTF("SF2 Debug: No matching SF2 instrument found (checked %d banks)\n", bankCount);
    // If we get here, no SF2 instrument was found
    *pErr = BAD_INSTRUMENT;
    return NULL;
}

// Helper function to create a waveform from an SF2 sample with generators
static OPErr PV_SF2_CreateWaveformFromSample(SF2_Bank *pBank, int32_t instrumentID, int16_t sampleID, uint32_t genStart, uint32_t genEnd, GM_Waveform *pWaveform)
{
    if (!pBank || sampleID < 0 || sampleID >= pBank->numSamples || !pWaveform)
    {
        return PARAM_ERR;
    }

    // Get fine and coarse tune from merged generators
    int16_t fineTune = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_FINE_TUNE, 0);
    int16_t coarseTune = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_COARSE_TUNE, 0);

    // Convert SF2 sample to miniBAE format
    SF2_Sample *sample = &pBank->sampleHeaders[sampleID];
    uint32_t sampleSize;
    uint32_t targetRate;
    // Apply per-zone generator address offsets (merged)
    int16_t startOfs = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_START_ADDRS_OFFSET, 0);
    int16_t endOfs = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_END_ADDRS_OFFSET, 0);
    int16_t startCoarse = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_START_ADDRS_COARSE_OFFSET, 0);
    int16_t endCoarse = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_END_ADDRS_COARSE_OFFSET, 0);
    int32_t effStart = (int32_t)sample->start + (int32_t)startOfs + ((int32_t)startCoarse * 32768);
    int32_t effEnd = (int32_t)sample->end + (int32_t)endOfs + ((int32_t)endCoarse * 32768);
    if (effStart < 0)
        effStart = 0;
    if (effEnd <= effStart)
        effEnd = effStart + 1;
    uint32_t originalFrames = (uint32_t)(effEnd - effStart);
    OPErr err = NO_ERR;
    SBYTE *convertedSample = PV_ConvertSF2Sample(pBank, sample, fineTune,
                                                 (uint32_t)effStart, (uint32_t)effEnd,
                                                 &sampleSize, &targetRate, &err);

    if (err != NO_ERR || !convertedSample)
    {
        return err;
    }

    // Calculate resampling ratio for loop point adjustment
    uint32_t resampledFrames = sampleSize / 2; // 16-bit samples
    float resampleRatio = (float)resampledFrames / (float)originalFrames;

    // Set up waveform data
    pWaveform->theWaveform = convertedSample;
    pWaveform->waveSize = sampleSize;
    pWaveform->waveFrames = resampledFrames;
    pWaveform->waveformID = 0;
    pWaveform->channels = 1; // SF2 samples are typically mono
    pWaveform->bitSize = 16;

    // Calculate loop points relative to the sample start, in FRAMES (not bytes)
    uint32_t loopStart = 0;
    uint32_t loopEnd = pWaveform->waveFrames; // Default to end of sample (exclusive)

    // Apply loop generator offsets as well (merged)
    int16_t startLoopOfs = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_STARTLOOP_ADDRS_OFFSET, 0);
    int16_t endLoopOfs = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_ENDLOOP_ADDRS_OFFSET, 0);
    int16_t startLoopCoarse = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_STARTLOOP_ADDRS_COARSE_OFFSET, 0);
    int16_t endLoopCoarse = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_ENDLOOP_ADDRS_COARSE_OFFSET, 0);
    int32_t effStartLoop = (int32_t)sample->startloop + (int32_t)startLoopOfs + ((int32_t)startLoopCoarse * 32768);
    int32_t effEndLoop = (int32_t)sample->endloop + (int32_t)endLoopOfs + ((int32_t)endLoopCoarse * 32768);

    // Remember whether the header actually had a loop
    XBOOL headerHadLoop = (sample->endloop > sample->startloop);

    // Clamp effective loop region to the effective sample window
    if (effStartLoop < effStart)
        effStartLoop = effStart;
    if (effEndLoop > effEnd)
        effEndLoop = effEnd;
    if (effEndLoop < effStartLoop)
        effEndLoop = effStartLoop;

    if (headerHadLoop &&
        effStartLoop >= effStart && effEndLoop >= effStart &&
        effStartLoop < effEnd && effEndLoop <= effEnd &&
        effStartLoop < effEndLoop)
    {
        // SF2 loop points are in samples; convert to resampled frame offsets
        uint32_t originalLoopStart = (uint32_t)(effStartLoop - effStart);
        uint32_t originalLoopEnd = (uint32_t)(effEndLoop - effStart);

        // Apply resampling ratio to loop points
        loopStart = (uint32_t)(originalLoopStart * resampleRatio);
        loopEnd = (uint32_t)(originalLoopEnd * resampleRatio);
    }
    else
    {
        // Only warn if the original header indicated a loop but we couldn't form a valid one
        if (headerHadLoop)
        {
            BAE_PRINTF("SF2 Debug: Invalid/overflowed loop after offsets (hdr %u-%u, eff %d-%d of %d-%d), disabling loop\n",
                       (unsigned)sample->startloop, (unsigned)sample->endloop,
                       (int)effStartLoop, (int)effEndLoop, (int)effStart, (int)effEnd);
        }
        loopStart = 0;
        loopEnd = 0;
    }

    // Clamp loop points to the available frame range
    if (loopEnd > pWaveform->waveFrames)
    {
        loopEnd = pWaveform->waveFrames;
    }
    if (loopStart > loopEnd)
    {
        loopStart = 0;
        loopEnd = 0;
    }

    pWaveform->startLoop = loopStart;
    pWaveform->endLoop = loopEnd;
    pWaveform->sampledRate = FLOAT_TO_XFIXED((float)targetRate);

    // Get the root key using merged generators
    int16_t zoneRootKey = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_OVERRIDING_ROOT_KEY, -1);
    int16_t keyRange = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_KEY_RANGE, 0x7F00);
    uint8_t keyLo = keyRange & 0xFF;
    uint8_t keyHi = (keyRange >> 8) & 0xFF;
    if (keyRange == 0x7F00 || keyHi == 0)
    {
        keyLo = 0;
        keyHi = 127;
    }

    pWaveform->baseMidiPitch = PV_EffectiveRootKey(pBank, sampleID, zoneRootKey, keyLo, keyHi);


    BAE_PRINTF("SF2 Debug: Created waveform - pBank=%u, sampleID=%d, rootKey=%d, size=%u frames, rate=%u Hz (loop %u-%u)\n",
               pBank, (int)sampleID, (int)pWaveform->baseMidiPitch,
               (unsigned)pWaveform->waveFrames, (unsigned)targetRate,
               (unsigned)pWaveform->startLoop, (unsigned)pWaveform->endLoop);

    return NO_ERR;
}

// Helper function to create a simple single-zone instrument (legacy behavior)
static GM_Instrument *PV_SF2_CreateSimpleInstrument(SF2_Bank *pBank, int32_t *instrumentIDs, uint32_t instrumentCount, OPErr *pErr)
{
    BAE_PRINTF("SF2 Debug: Creating simple instrument from %u instruments\n", (unsigned)instrumentCount);

    // Use the first instrument and its first valid zone
    for (uint32_t i = 0; i < instrumentCount; i++)
    {
        SF2_Instrument *instrument = &pBank->instruments[instrumentIDs[i]];
        uint32_t instBagStart = instrument->bagIndex;
        uint32_t instBagEnd = (instrumentIDs[i] + 1 < pBank->numInstruments) ? pBank->instruments[instrumentIDs[i] + 1].bagIndex : pBank->numInstBags;

        for (uint32_t bagIdx = instBagStart; bagIdx < instBagEnd; bagIdx++)
        {
            if (bagIdx >= pBank->numInstBags)
                break;

            SF2_Bag *bag = &pBank->instBags[bagIdx];
            uint32_t genStart = bag->genIndex;
            uint32_t genEnd = (bagIdx + 1 < pBank->numInstBags) ? pBank->instBags[bagIdx + 1].genIndex : pBank->numInstGens;

            // Check if this zone has a sample
            int16_t sampleID = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens,
                                                     genStart, genEnd, SF2_GEN_SAMPLE_ID, -1);

            if (sampleID != -1 && sampleID < pBank->numSamples)
            {
                BAE_PRINTF("SF2 Debug: Using sample %d from instrument %d\n", (int)sampleID, (int)instrumentIDs[i]);

                // Create the instrument using the existing logic
                GM_Instrument *pInstrument = (GM_Instrument *)XNewPtr(sizeof(GM_Instrument));
                if (!pInstrument)
                {
                    *pErr = MEMORY_ERR;
                    return NULL;
                }

                XSetMemory(pInstrument, sizeof(GM_Instrument), 0);

                // Initialize basic parameters
                pInstrument->doKeymapSplit = FALSE; // Simple instrument
                pInstrument->extendedFormat = FALSE;
                pInstrument->notPolyphonic = FALSE;
                pInstrument->useSampleRate = TRUE;
                pInstrument->disableSndLooping = FALSE;
                // Melodic simple instrument: allow transposition
                pInstrument->playAtSampledFreq = FALSE;
                pInstrument->sampleAndHold = FALSE;
                pInstrument->usageReferenceCount = 0;
                pInstrument->panPlacement = 0;

#if REVERB_USED != REVERB_DISABLED
                pInstrument->avoidReverb = FALSE;
#endif

                // Get root key
                int16_t zoneRootKey = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens,
                                                            genStart, genEnd, SF2_GEN_OVERRIDING_ROOT_KEY, -1);
                int16_t keyRange = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens,
                                                         genStart, genEnd, SF2_GEN_KEY_RANGE, 0x7F00);
                uint8_t keyLo = keyRange & 0xFF;
                uint8_t keyHi = (keyRange >> 8) & 0xFF;
                if (keyRange == 0x7F00 || keyHi == 0)
                {
                    keyLo = 0;
                    keyHi = 127;
                }

                int16_t rootKey = PV_EffectiveRootKey(pBank, sampleID, zoneRootKey, keyLo, keyHi);
                // For SF2, rely on waveform baseMidiPitch instead of masterRootKey to avoid double transposition
                pInstrument->masterRootKey = 0;

                // Create the waveform
                OPErr waveErr = PV_SF2_CreateWaveformFromSample(pBank, instrumentIDs[i], sampleID, genStart, genEnd, &pInstrument->u.w);
                if (waveErr != NO_ERR)
                {
                    XDisposePtr(pInstrument);
                    *pErr = waveErr;
                    return NULL;
                }

                BAE_PRINTF("SF2 Debug: Created simple instrument with rootKey=%d (masterRootKey=0)\n", (int)rootKey);

                // Fill ADSR from SF2 generators for this zone
                PV_SF2_FillVolumeADSR(pBank, instrumentIDs[i], genStart, genEnd, &pInstrument->volumeADSRRecord);

#if USE_SF2_SUPPORT == TRUE
                // Fill modulation envelope from SF2 generators
                PV_SF2_FillModulationADSR(pBank, instrumentIDs[i], genStart, genEnd, &pInstrument->modEnvelopeRecord);
                
                // Get modulation envelope to pitch/filter amounts
                pInstrument->modEnvelopeToPitch = PV_FindInstGenMerged(pBank, instrumentIDs[i], genStart, genEnd, SF2_GEN_MOD_ENV_TO_PITCH, 0) * 4; // cents -> engine units
                pInstrument->modEnvelopeToFilter = PV_FindInstGenMerged(pBank, instrumentIDs[i], genStart, genEnd, SF2_GEN_MOD_ENV_TO_FILTER_FC, 0) * 4; // cents -> engine units
                
                BAE_PRINTF("SF2 Debug: ModEnv amounts - toPitch: %d, toFilter: %d\n", 
                           (int)pInstrument->modEnvelopeToPitch, (int)pInstrument->modEnvelopeToFilter);
#endif

                // Fill LFO records from SF2 generators for this zone
                PV_SF2_FillLFORecords(pBank, instrumentIDs[i], genStart, genEnd, pInstrument);

                // Check SF2 sample modes to determine looping behavior
                // SF2 Spec: 0 = No looping, 1 = Continuous loop, 2 = Reserved, 3 = Loop + release
                int16_t sampleModes = PV_FindInstGenMerged(pBank, instrumentIDs[i], genStart, genEnd, SF2_GEN_SAMPLE_MODES, 0);
                if (sampleModes == 0 || sampleModes == 2) {
                    // No looping or reserved mode - disable looping
                    pInstrument->disableSndLooping = TRUE;
                    BAE_PRINTF("SF2 Debug: Sample modes = %d, disabling looping\n", sampleModes);
                } else if (sampleModes == 1 || sampleModes == 3) {
                    // Continuous loop or loop+release - enable looping
                    pInstrument->disableSndLooping = FALSE;
                    BAE_PRINTF("SF2 Debug: Sample modes = %d, enabling looping\n", sampleModes);
                } else {
                    // Unknown mode - keep default behavior
                    BAE_PRINTF("SF2 Debug: Unknown sample modes = %d, keeping default looping setting\n", sampleModes);
                }

                // Process SF2 preset and instrument modulators (PMOD/IMOD)
                PV_SF2_ProcessModulators(pBank, pInstrument, 0, 0, instrumentIDs[i], genStart, genEnd);
                
                // Apply SF2 default modulators (DMOD)
                PV_SF2_ApplyDefaultModulators(pInstrument);

                *pErr = NO_ERR;
                return pInstrument;
            }
        }
    }

    *pErr = BAD_INSTRUMENT;
    return NULL;
}

// Helper function to create a keymap split instrument for multi-zone instruments
static GM_Instrument *PV_SF2_CreateKeymapSplitInstrument(SF2_Bank *pBank, int32_t *instrumentIDs, uint32_t instrumentCount, int32_t presetID, OPErr *pErr)
{
    BAE_PRINTF("SF2 Debug: Creating keymap split instrument from %u instruments for preset %d\n", (unsigned)instrumentCount, (int)presetID);

    // Collect all zones with their key and velocity ranges
    typedef struct
    {
        int16_t sampleID;
        uint8_t lowKey, highKey;
        uint8_t lowVel, highVel;
        int16_t rootKey;
        int16_t coarseTune, fineTune;
        uint32_t genStart, genEnd;
        int32_t instrumentID;
        uint32_t presetGenStart, presetGenEnd; // For preset-level merging
    } ZoneInfo;


    ZoneInfo zones[MAX_SF2_ZONES];
    uint32_t zoneCount = 0;

    // Get preset global zone
    XBOOL hasPresetGlobal = FALSE;
    uint32_t presetGlobalStart = 0, presetGlobalEnd = 0;
    PV_GetPresetGlobalGenRange(pBank, presetID, &presetGlobalStart, &presetGlobalEnd, &hasPresetGlobal);

    // Scan all instruments for zones
    for (uint32_t i = 0; i < instrumentCount && zoneCount < MAX_SF2_ZONES; i++)
    {
        int32_t instID = instrumentIDs[i];
        SF2_Instrument *instrument = &pBank->instruments[instID];
        uint32_t instBagStart = instrument->bagIndex;
        uint32_t instBagEnd = (instID + 1 < pBank->numInstruments) ? pBank->instruments[instID + 1].bagIndex : pBank->numInstBags;

        // Get instrument global zone
        XBOOL hasGlobal = FALSE;
        uint32_t globalGenStart = 0, globalGenEnd = 0;
        PV_GetInstGlobalGenRange(pBank, instID, &globalGenStart, &globalGenEnd, &hasGlobal);

        // Get preset bag range for this instrument (assuming one preset bag per instrument for now)
    uint32_t presetGenStart = 0, presetGenEnd = 0;
    XBOOL foundPresetRange = FALSE;
        if (presetID >= 0 && presetID < pBank->numPresets)
        {
            SF2_Preset *preset = &pBank->presets[presetID];
            uint32_t pbagStart = preset->bagIndex;
            uint32_t pbagEnd = (presetID + 1 < pBank->numPresets) ? pBank->presets[presetID + 1].bagIndex : pBank->numPresetBags;
            // Find bag with this instrument
            // Find the full contiguous range of preset bags that map to this instrument.
            // Some SF2 files place multiple adjacent preset bags for the same instrument
            // (for velocity splits, etc.). Collect the min/max bag indices that match
            // so merged preset-level generators (e.g. VEL_RANGE) are considered correctly.
            int32_t firstMatch = -1;
            int32_t lastMatch = -1;
            for (uint32_t pbagIdx = pbagStart; pbagIdx < pbagEnd; pbagIdx++)
            {
                if (pbagIdx >= pBank->numPresetBags) break;
                SF2_Bag *pbag = &pBank->presetBags[pbagIdx];
                uint32_t pgenStart = pbag->genIndex;
                uint32_t pgenEnd = (pbagIdx + 1 < pBank->numPresetBags) ? pBank->presetBags[pbagIdx + 1].genIndex : pBank->numPresetGens;
                int16_t pInstID = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, pgenStart, pgenEnd, SF2_GEN_INSTRUMENT, -1);
                if (pInstID == instID)
                {
                    if (firstMatch == -1)
                        firstMatch = (int32_t)pbagIdx;
                    lastMatch = (int32_t)pbagIdx;
                }
            }
            if (firstMatch != -1)
            {
                // Compute presetGenStart from first matching bag, and presetGenEnd from the bag after lastMatch
                SF2_Bag *firstBagMatch = &pBank->presetBags[firstMatch];
                presetGenStart = firstBagMatch->genIndex;
                uint32_t afterLastIdx = (uint32_t)lastMatch + 1;
                if (afterLastIdx < pBank->numPresetBags)
                {
                    presetGenEnd = pBank->presetBags[afterLastIdx].genIndex;
                }
                else
                {
                    presetGenEnd = pBank->numPresetGens;
                }

                // If the immediately previous bag (within the same preset) has no INSTRUMENT
                // generator, include it so VEL_RANGE placed there is considered for this instrument.
                if ((uint32_t)firstMatch > pbagStart)
                {
                    uint32_t prevIdx = (uint32_t)firstMatch - 1;
                    if (prevIdx < pBank->numPresetBags)
                    {
                        SF2_Bag *prevBag = &pBank->presetBags[prevIdx];
                        uint32_t prevGStart = prevBag->genIndex;
                        uint32_t prevGEnd = (prevIdx + 1 < pBank->numPresetBags) ? pBank->presetBags[prevIdx + 1].genIndex : pBank->numPresetGens;
                        int16_t prevInst = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, prevGStart, prevGEnd, SF2_GEN_INSTRUMENT, -1);
                        if (prevInst == -1)
                        {
                            BAE_PRINTF("SF2 Debug: Including previous preset bag %u gen %u-%u into presetGen range for inst %d\n",
                                       (unsigned)prevIdx, (unsigned)prevGStart, (unsigned)prevGEnd, instID);
                            presetGenStart = prevGStart;
                        }
                    }
                }

                foundPresetRange = TRUE;
                BAE_PRINTF("SF2 Debug: Found preset gen range for inst %d: presetGenStart=%u presetGenEnd=%u\n", instID, (unsigned)presetGenStart, (unsigned)presetGenEnd);
            }
            if (!foundPresetRange)
            {
                BAE_PRINTF("SF2 Debug: No preset bag matched inst %d in preset %d; dumping all preset bags for this preset:\n", instID, (int)presetID);
                for (uint32_t dbg = pbagStart; dbg < pbagEnd && dbg < pBank->numPresetBags; ++dbg)
                {
                    SF2_Bag *dbgBag = &pBank->presetBags[dbg];
                    uint32_t dbgGStart = dbgBag->genIndex;
                    uint32_t dbgGEnd = (dbg + 1 < pBank->numPresetBags) ? pBank->presetBags[dbg + 1].genIndex : pBank->numPresetGens;
                    int16_t dbgInst = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, dbgGStart, dbgGEnd, SF2_GEN_INSTRUMENT, -1);
                    BAE_PRINTF(" SF2 Debug: presetBag[%u] gen %u-%u maps to inst=%d; gens:\n", (unsigned)dbg, (unsigned)dbgGStart, (unsigned)dbgGEnd, (int)dbgInst);
                }
                // Extra diagnostic: search entire bank for any preset bag mapping to this instrumentID
                BAE_PRINTF("SF2 Debug: Searching entire bank for preset bags that map to inst %d\n", instID);
                for (uint32_t pbi = 0; pbi < pBank->numPresetBags; ++pbi)
                {
                    SF2_Bag *pb = &pBank->presetBags[pbi];
                    uint32_t s = pb->genIndex;
                    uint32_t e = (pbi + 1 < pBank->numPresetBags) ? pBank->presetBags[pbi + 1].genIndex : pBank->numPresetGens;
                    int16_t mapInst = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, s, e, SF2_GEN_INSTRUMENT, -1);
                    if (mapInst == instID)
                    {
                        BAE_PRINTF("SF2 Debug: FOUND mapping in presetBag[%u] gen %u-%u -> inst=%d\n", (unsigned)pbi, (unsigned)s, (unsigned)e, (int)mapInst);
                        // Dump gens in that bag and explicitly check for VEL_RANGE
                        
                        int16_t v = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, s, e, SF2_GEN_VEL_RANGE, 0x7F00);
                        if (v != 0x7F00)
                        {
                            uint8_t lv = v & 0xFF;
                            uint8_t hv = (v >> 8) & 0xFF;
                            BAE_PRINTF("SF2 Debug: VEL_RANGE in that bag -> 0x%04X (vel %u-%u)\n", (unsigned)v, (unsigned)lv, (unsigned)hv);
                        }
                    }
                }
            }
        }

        for (uint32_t bagIdx = instBagStart; bagIdx < instBagEnd && zoneCount < MAX_SF2_ZONES; bagIdx++)
        {
            if (bagIdx >= pBank->numInstBags) break;

            SF2_Bag *bag = &pBank->instBags[bagIdx];
            uint32_t genStart = bag->genIndex;
            uint32_t genEnd = (bagIdx + 1 < pBank->numInstBags) ? pBank->instBags[bagIdx + 1].genIndex : pBank->numInstGens;

            // Check if this is a local zone
            int16_t sampleID = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_SAMPLE_ID, -1);
            // Diagnostic: dump all instrument generators in this zone to help find misplaced VEL_RANGE
            if (sampleID == -1 || sampleID >= pBank->numSamples) continue;

            // Get effective key range
            BAE_PRINTF("SF2 Debug: Zone merge context - inst=%d presetGen=%u-%u instGen=%u-%u\n",
                       instID, (unsigned)presetGenStart, (unsigned)presetGenEnd, (unsigned)genStart, (unsigned)genEnd);
            int16_t keyRange = PV_FindEffectiveGenValue(pBank, presetID, instID, presetGenStart, presetGenEnd, genStart, genEnd, SF2_GEN_KEY_RANGE, 0x7F00);
            uint8_t lowKey = keyRange & 0xFF;
            uint8_t highKey = (keyRange >> 8) & 0xFF;
            if (keyRange == 0x7F00 || highKey == 0)
            {
                lowKey = 0;
                highKey = 127;
            }
            int16_t zKeyNum = PV_FindEffectiveGenValue(pBank, presetID, instID, presetGenStart, presetGenEnd, genStart, genEnd, SF2_GEN_KEYNUM, -1);
            if (zKeyNum >= 0 && zKeyNum <= 127)
            {
                lowKey = (uint8_t)zKeyNum;
                highKey = (uint8_t)zKeyNum;
            }
            if (lowKey > highKey)
            {
                uint8_t t = lowKey;
                lowKey = highKey;
                highKey = t;
            }
            if (highKey > 127) highKey = 127;

            // Get effective velocity range
            // Diagnostic: show how merging would resolve VEL_RANGE
            int16_t instVel = PV_FindInstGenMerged(pBank, instID, genStart, genEnd, SF2_GEN_VEL_RANGE, 0x7F00);
            int16_t presetVelGlobal = 0;
            if (presetGlobalStart < presetGlobalEnd)
                presetVelGlobal = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, presetGlobalStart, presetGlobalEnd, SF2_GEN_VEL_RANGE, 0);
            int16_t presetVelLocal = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, presetGenStart, presetGenEnd, SF2_GEN_VEL_RANGE, presetVelGlobal);
            XBOOL presetHasLocalVel = PV_HasGeneratorInRange(pBank->presetGens, pBank->numPresetGens, presetGenStart, presetGenEnd, SF2_GEN_VEL_RANGE);
            BAE_PRINTF("SF2 Debug: VEL_RANGE merge check - instVel=0x%04X presetGlobal=0x%04X presetLocal=0x%04X presetHasLocal=%u\n",
                       (unsigned)(uint16_t)instVel, (unsigned)(uint16_t)presetVelGlobal, (unsigned)(uint16_t)presetVelLocal, (unsigned)presetHasLocalVel);
            int16_t velRange = PV_FindEffectiveGenValue(pBank, presetID, instID, presetGenStart, presetGenEnd, genStart, genEnd, SF2_GEN_VEL_RANGE, 0x7F00);
            uint8_t lowVel = velRange & 0xFF;
            uint8_t highVel = (velRange >> 8) & 0xFF;
            // Handle default sentinel robustly: accept either 0x7F00 or a zero high-byte (0x007F) as the "full" range
            if (velRange == 0x7F00 || highVel == 0)
            {
                lowVel = 0;
                highVel = 127;
            }

            // Fallback: if instrument-level merged lookup returned the default sentinel, try preset bag ranges
            if (velRange == 0x7F00)
            {
                // First try preset global zone (if present)
                if (hasPresetGlobal)
                {
                    int16_t presetVelGlobal = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, presetGlobalStart, presetGlobalEnd, SF2_GEN_VEL_RANGE, 0x7F00);
                    if (presetVelGlobal != 0x7F00)
                    {
                        velRange = presetVelGlobal;
                        lowVel = velRange & 0xFF;
                        highVel = (velRange >> 8) & 0xFF;
                        BAE_PRINTF("SF2 Debug: VEL_RANGE found in preset GLOBAL range -> 0x%04X (vel %u-%u)\n", (unsigned)velRange, (unsigned)lowVel, (unsigned)highVel);
                    }
                }

                if (velRange == 0x7F00 && presetID >= 0 && presetID < (int32_t)pBank->numPresets)
                {
                    SF2_Preset *preset = &pBank->presets[presetID];
                    uint32_t pbagStart = preset->bagIndex;
                    uint32_t pbagEnd = (presetID + 1 < (int32_t)pBank->numPresets) ? pBank->presets[presetID + 1].bagIndex : pBank->numPresetBags;
                    BAE_PRINTF("SF2 Debug: VEL_RANGE fallback: scanning preset bags %u-%u for preset %d\n", (unsigned)pbagStart, (unsigned)pbagEnd, (int)presetID);
                    for (uint32_t pbagIdx = pbagStart; pbagIdx < pbagEnd; ++pbagIdx)
                    {
                        if (pbagIdx >= pBank->numPresetBags)
                            break;
                        SF2_Bag *pbag = &pBank->presetBags[pbagIdx];
                        uint32_t pgenStart = pbag->genIndex;
                        uint32_t pgenEnd = (pbagIdx + 1 < pBank->numPresetBags) ? pBank->presetBags[pbagIdx + 1].genIndex : pBank->numPresetGens;
                        int16_t presetVel = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, pgenStart, pgenEnd, SF2_GEN_VEL_RANGE, 0x7F00);
                        if (presetVel != 0x7F00)
                        {
                            velRange = presetVel;
                            lowVel = velRange & 0xFF;
                            highVel = (velRange >> 8) & 0xFF;
                            BAE_PRINTF("SF2 Debug: VEL_RANGE found in preset bag %u -> 0x%04X (vel %u-%u)\n", (unsigned)pbagIdx, (unsigned)velRange, (unsigned)lowVel, (unsigned)highVel);
                            if (keyRange != 0x7F00)
                            {
                                BAE_PRINTF("SF2 Debug: NOTE: keyRange in instrument zone is non-default (0x%04X) while velRange was found in preset bag %u\n", (unsigned)keyRange, (unsigned)pbagIdx);
                            }
                            break;
                        }
                    }
                }
                // NOTE: deliberately avoid scanning unrelated preset bags in the entire bank
                // for VEL_RANGE. Using a VEL_RANGE from a different preset can produce
                // incorrect mappings. If velRange is still the sentinel, treat as full 0-127.
            }
            if (lowVel > highVel)
            {
                uint8_t t = lowVel;
                lowVel = highVel;
                highVel = t;
            }
            if (highVel > 127) highVel = 127;

            // Get effective root key
            int16_t zoneRootKey = PV_FindEffectiveGenValue(pBank, presetID, instID, presetGenStart, presetGenEnd, genStart, genEnd, SF2_GEN_OVERRIDING_ROOT_KEY, -1);
            int16_t rootKey = PV_EffectiveRootKey(pBank, sampleID, zoneRootKey, lowKey, highKey);

            // Get effective tuning
            int16_t coarseTune = PV_FindEffectiveGenValue(pBank, presetID, instID, presetGenStart, presetGenEnd, genStart, genEnd, SF2_GEN_COARSE_TUNE, 0);
            int16_t fineTune = PV_FindEffectiveGenValue(pBank, presetID, instID, presetGenStart, presetGenEnd, genStart, genEnd, SF2_GEN_FINE_TUNE, 0);

            // Get scaleTuning (cents per key, default 100)
            int16_t scaleTuning = PV_FindEffectiveGenValue(pBank, presetID, instID, presetGenStart, presetGenEnd, genStart, genEnd, SF2_GEN_SCALE_TUNING, 100);

            zones[zoneCount].sampleID = sampleID;
            zones[zoneCount].lowKey = lowKey;
            zones[zoneCount].highKey = highKey;
            zones[zoneCount].lowVel = lowVel;
            zones[zoneCount].highVel = highVel;
            zones[zoneCount].rootKey = rootKey;
            zones[zoneCount].coarseTune = coarseTune;
            zones[zoneCount].fineTune = fineTune;
            zones[zoneCount].genStart = genStart;
            zones[zoneCount].genEnd = genEnd;
            zones[zoneCount].instrumentID = instID;
            zones[zoneCount].presetGenStart = presetGenStart;
            zones[zoneCount].presetGenEnd = presetGenEnd;             

            BAE_PRINTF("SF2 Debug: Zone %u: sample=%d, key=%u-%u, vel=%u-%u, rootKey=%d, coarse=%d, fine=%d, scaleTune=%d\n",
                       (unsigned)zoneCount, (int)sampleID, (unsigned)lowKey, (unsigned)highKey,
                       (unsigned)lowVel, (unsigned)highVel, (int)rootKey, (int)coarseTune, (int)fineTune, (int)scaleTuning);

            zoneCount++;
        }
    }

    if (zoneCount == 0)
    {
        *pErr = BAD_INSTRUMENT;
        return NULL;
    }

    if (zoneCount > MAX_SF2_ZONES)
    {
        BAE_PRINTF("SF2 Debug: Too many zones (%u), limiting to %d\n", (unsigned)zoneCount, MAX_SF2_ZONES);
        zoneCount = MAX_SF2_ZONES;
    }

    // Sort zones by lowKey, then lowVel
    for (uint32_t a = 0; a + 1 < zoneCount; a++)
    {
        for (uint32_t b = a + 1; b < zoneCount; b++)
        {
            if (zones[b].lowKey < zones[a].lowKey ||
                (zones[b].lowKey == zones[a].lowKey && zones[b].lowVel < zones[a].lowVel))
            {
                ZoneInfo tmp = zones[a];
                zones[a] = zones[b];
                zones[b] = tmp;
            }
        }
    }

    // Allocate instrument
    size_t extraSplits = (zoneCount > 1) ? (zoneCount - 1) : 0;
    size_t extraBytes = extraSplits * sizeof(GM_KeymapSplit);
    size_t totalSize = sizeof(GM_Instrument) + extraBytes;
    GM_Instrument *pMainInstrument = (GM_Instrument *)XNewPtr(totalSize);
    if (!pMainInstrument)
    {
        *pErr = MEMORY_ERR;
        return NULL;
    }
    XSetMemory(pMainInstrument, totalSize, 0);

    // Initialize as keymap split instrument
    pMainInstrument->doKeymapSplit = TRUE;
    pMainInstrument->extendedFormat = FALSE;
    pMainInstrument->notPolyphonic = FALSE;
    pMainInstrument->useSampleRate = TRUE;
    pMainInstrument->disableSndLooping = FALSE;
    pMainInstrument->playAtSampledFreq = FALSE;
    pMainInstrument->sampleAndHold = FALSE;
    pMainInstrument->usageReferenceCount = 0;
    pMainInstrument->panPlacement = 0;
    pMainInstrument->masterRootKey = 0;

#if REVERB_USED != REVERB_DISABLED
    pMainInstrument->avoidReverb = FALSE;
#endif

    pMainInstrument->u.k.defaultInstrumentID = 0;
    pMainInstrument->u.k.KeymapSplitCount = zoneCount;

    // Create sub-instruments
    for (uint32_t i = 0; i < zoneCount; i++)
    {
        ZoneInfo *zone = &zones[i];

        GM_Instrument *pSubInstrument = (GM_Instrument *)XNewPtr(sizeof(GM_Instrument));
        if (!pSubInstrument)
        {
            for (uint32_t j = 0; j < i; j++)
            {
                if (pMainInstrument->u.k.keySplits[j].pSplitInstrument)
                {
                    XDisposePtr(pMainInstrument->u.k.keySplits[j].pSplitInstrument);
                }
            }
            XDisposePtr(pMainInstrument);
            *pErr = MEMORY_ERR;
            return NULL;
        }

        XSetMemory(pSubInstrument, sizeof(GM_Instrument), 0);

        pSubInstrument->doKeymapSplit = FALSE;
        pSubInstrument->extendedFormat = FALSE;
        pSubInstrument->notPolyphonic = FALSE;
        pSubInstrument->useSampleRate = TRUE;
        pSubInstrument->disableSndLooping = FALSE;
        pSubInstrument->playAtSampledFreq = FALSE;
        pSubInstrument->sampleAndHold = FALSE;
        pSubInstrument->usageReferenceCount = 0;
        pSubInstrument->panPlacement = 0;
        pSubInstrument->masterRootKey = 0;

#if REVERB_USED != REVERB_DISABLED
        pSubInstrument->avoidReverb = FALSE;
#endif

        // Fill ADSR and LFO with effective generators
        PV_SF2_FillVolumeADSR(pBank, zone->instrumentID, zone->genStart, zone->genEnd, &pSubInstrument->volumeADSRRecord);

#if USE_SF2_SUPPORT == TRUE
        // Fill modulation envelope from SF2 generators
        PV_SF2_FillModulationADSR(pBank, zone->instrumentID, zone->genStart, zone->genEnd, &pSubInstrument->modEnvelopeRecord);
        
        // Get modulation envelope to pitch/filter amounts
        pSubInstrument->modEnvelopeToPitch = PV_FindInstGenMerged(pBank, zone->instrumentID, zone->genStart, zone->genEnd, SF2_GEN_MOD_ENV_TO_PITCH, 0) * 4; // cents -> engine units
        pSubInstrument->modEnvelopeToFilter = PV_FindInstGenMerged(pBank, zone->instrumentID, zone->genStart, zone->genEnd, SF2_GEN_MOD_ENV_TO_FILTER_FC, 0) * 4; // cents -> engine units
        
        BAE_PRINTF("SF2 Debug: Keymap zone %u ModEnv amounts - toPitch: %d, toFilter: %d\n", 
                   i, (int)pSubInstrument->modEnvelopeToPitch, (int)pSubInstrument->modEnvelopeToFilter);
#endif
        
        PV_SF2_FillLFORecords(pBank, zone->instrumentID, zone->genStart, zone->genEnd, pSubInstrument);

        // Check SF2 sample modes to determine looping behavior for this zone
        // SF2 Spec: 0 = No looping, 1 = Continuous loop, 2 = Reserved, 3 = Loop + release
        int16_t sampleModes = PV_FindInstGenMerged(pBank, zone->instrumentID, zone->genStart, zone->genEnd, SF2_GEN_SAMPLE_MODES, 0);
        if (sampleModes == 0 || sampleModes == 2) {
            // No looping or reserved mode - disable looping
            pSubInstrument->disableSndLooping = TRUE;
            BAE_PRINTF("SF2 Debug: Zone %u sample modes = %d, disabling looping\n", i, sampleModes);
        } else if (sampleModes == 1 || sampleModes == 3) {
            // Continuous loop or loop+release - enable looping
            pSubInstrument->disableSndLooping = FALSE;
            BAE_PRINTF("SF2 Debug: Zone %u sample modes = %d, enabling looping\n", i, sampleModes);
        } else {
            // Unknown mode - keep default behavior
            BAE_PRINTF("SF2 Debug: Zone %u unknown sample modes = %d, keeping default looping setting\n", i, sampleModes);
        }

        // Process SF2 preset and instrument modulators (PMOD/IMOD) for this sub-instrument
        PV_SF2_ProcessModulators(pBank, pSubInstrument, zone->presetGenStart, zone->presetGenEnd, 
                               zone->instrumentID, zone->genStart, zone->genEnd);

        // Apply SF2 default modulators (DMOD) for this sub-instrument
        PV_SF2_ApplyDefaultModulators(pSubInstrument);

        // Create waveform
        OPErr waveErr = PV_SF2_CreateWaveformFromSample(pBank, zone->instrumentID, zone->sampleID, zone->genStart, zone->genEnd, &pSubInstrument->u.w);
        if (waveErr != NO_ERR)
        {
            for (uint32_t j = 0; j < i; j++)
            {
                if (pMainInstrument->u.k.keySplits[j].pSplitInstrument)
                {
                    XDisposePtr(pMainInstrument->u.k.keySplits[j].pSplitInstrument);
                }
            }
            XDisposePtr(pSubInstrument);
            XDisposePtr(pMainInstrument);
            *pErr = waveErr;
            return NULL;
        }

        // Apply tuning and scaleTuning
        int16_t scaleTuning = PV_FindEffectiveGenValue(pBank, presetID, zone->instrumentID, zone->presetGenStart, zone->presetGenEnd, zone->genStart, zone->genEnd, SF2_GEN_SCALE_TUNING, 100);
        float pitchFactor = powf(2.0f, (float)zone->fineTune / 1200.0f);
        if (scaleTuning != 100)
        {
            float scaleFactor = (float)scaleTuning / 100.0f;
            pitchFactor *= powf(2.0f, (float)(zone->rootKey - 60) * (scaleFactor - 1.0f) / 12.0f); // Adjust relative to C4
        }
        pSubInstrument->u.w.baseMidiPitch -= zone->coarseTune;
        pSubInstrument->u.w.sampledRate = (XFIXED)((double)pSubInstrument->u.w.sampledRate * (double)pitchFactor);

        // Set velocity and key ranges
        pMainInstrument->u.k.keySplits[i].lowMidi = zone->lowKey;
        pMainInstrument->u.k.keySplits[i].highMidi = zone->highKey;
        pMainInstrument->u.k.keySplits[i].velRange = ((uint16_t)zone->highVel << 8) | (uint16_t)zone->lowVel;
        pMainInstrument->u.k.keySplits[i].miscParameter2 = 100; // Use SF2_GEN_INITIAL_ATTENUATION if needed
        pMainInstrument->u.k.keySplits[i].pSplitInstrument = pSubInstrument;

        BAE_PRINTF("SF2 Debug: Created zone %u: keys=%u-%u, vel=%u-%u, rootKey=%d, scaleTune=%d, baseMidiPitch=%d\n",
                   (unsigned)i, (unsigned)zone->lowKey, (unsigned)zone->highKey,
                   (unsigned)zone->lowVel, (unsigned)zone->highVel, (int)zone->rootKey, (int)scaleTuning, (int)pSubInstrument->u.w.baseMidiPitch);

                   
    }

    BAE_PRINTF("SF2 Debug: Created keymap split instrument with %u zones\n", (unsigned)zoneCount);
    *pErr = NO_ERR;
    return pMainInstrument;
}

uint32_t SF2_LoadedBankCount(void)
{
    return g_sf2Manager.numBanks;
}

#endif // USE_SF2_SUPPORT
