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
static GM_Instrument *PV_SF2_CreateKeymapSplitInstrument(SF2_Bank *pBank, int32_t *instrumentIDs, uint32_t instrumentCount, OPErr *pErr);
// instrumentID is needed to merge instrument-global generators with local zone values
static OPErr PV_SF2_CreateWaveformFromSample(SF2_Bank *pBank, int32_t instrumentID, int16_t sampleID, uint32_t genStart, uint32_t genEnd, GM_Waveform *pWaveform);
// Forward declaration used by helpers below
static int16_t PV_FindGeneratorValue(SF2_Generator *generators, uint32_t genCount,
                                     uint32_t startIndex, uint32_t endIndex,
                                     SF2_GeneratorType genType, int16_t defaultValue);

// Helpers to resolve instrument-global + local generator values
static void PV_GetInstGlobalGenRange(SF2_Bank *pBank, int32_t instrumentID,
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
    if (timecents > 8000) // clamp very long values to ~5 minutes max
        timecents = 8000;

    // Correct SF2 formula: seconds = 2^(timecents/1200)
    float seconds = powf(2.0f, (float)timecents / 1200.0f);
    if (seconds < 0.0f)
        seconds = 0.0f;

    double usec = (double)seconds * 1000000.0;
    if (usec < 0.0)
        usec = 0.0;
    if (usec > 60000000.0) // clamp at 60s to avoid overflow/very long stages
        usec = 60000000.0;
    // For very small positive values, ensure a minimum of 1 microsecond (non-zero tc only)
    if (usec < 1.0)
        usec = 1.0;

    return (uint32_t)usec;
}

// Convert attenuation in centibels to a linear level scaled against fullLevel
// level = fullLevel * 10^(-cB/200)
static inline XSDWORD PV_SF2_LevelFromCentibels(int16_t centibels, XSDWORD fullLevel)
{
    float gain = powf(10.0f, -(float)centibels / 200.0f);
    double lvl = (double)fullLevel * (double)gain;
    if (lvl < 0.0)
        lvl = 0.0;
    if (lvl > (double)fullLevel)
        lvl = (double)fullLevel;

    XSDWORD result = (XSDWORD)lvl;

    BAE_PRINTF("SF2 Debug: PV_SF2_LevelFromCentibels(%d cB, %ld) = gain=%f, lvl=%f, result=%ld\n",
               (int)centibels, (long)fullLevel, gain, lvl, (long)result);
    return result;
}

// Convert SF2 Hz frequency to LFO period in microseconds
// SF2 frequency is in absolute Hz (0.001 to 100 Hz)
// LFO period is in microseconds (1/freq * 1,000,000)
static inline uint32_t PV_SF2_FreqToLFOPeriod(int16_t frequency_centiHz)
{
    if (frequency_centiHz <= 0)
    {
        return 8000000; // Default to 8 second period (~0.125 Hz)
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
    l->waveShape = SINE_WAVE; // SF2 LFOs are sine waves
    l->DC_feed = 0;
    l->currentWaveValue = 0;
    l->currentTime = 0;
    l->LFOcurrentTime = 0;
    if (delay_tc > -12000)
    {
        uint32_t delayTime = PV_SF2_TimecentsToUSec(delay_tc);
        l->a.ADSRLevel[0] = 0;
        l->a.ADSRTime[0] = delayTime;
        l->a.ADSRFlags[0] = ADSR_LINEAR_RAMP;
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

    // Fallback: if vibrato LFO has a frequency but no explicit pitch depth, set up default MOD wheel control (DMOD)
    // Many SF2 soundfonts rely on modulators (PMOD/IMOD) for vibrato depth instead of generators.
    // Implement a default mapping: Mod Wheel controls vibrato pitch LFO depth (0..~50 cents).
    if (vibLfoToPitch == 0 && vibLfoFreq != 0 && lfoCount < MAX_LFOS)
    {
        // Create vibrato LFO with default maximum depth of ~50 cents, scaled by MOD wheel
        const int defaultVibDepthCents = 50; // typical default per SF2 default modulators
        pLFO = &pInstrument->LFORecords[lfoCount];
        PV_SF2_InitLFO(pLFO, PV_SF2_FreqToLFOPeriod(vibLfoFreq), vibLfoDelay);
        pLFO->where_to_feed = PITCH_LFO;
        pLFO->level = defaultVibDepthCents * 4; // cents -> engine units

        BAE_PRINTF("SF2 Debug: DMOD fallback - Created vibrato LFO %d (default depth %d cents), period=%u µs, delay=%d tc\n",
                   lfoCount, defaultVibDepthCents, pLFO->period, vibLfoDelay);

        // Add a simple 2-point curve: MOD_WHEEL 0 -> scalar 0, 127 -> scalar 256 (100%)
        if (pInstrument->curveRecordCount < MAX_CURVES)
        {
            GM_TieTo *curve = &pInstrument->curve[pInstrument->curveRecordCount];
            curve->tieFrom = MOD_WHEEL_CONTROL;
            curve->tieTo = PITCH_LFO;
            curve->curveCount = 2;
            curve->from_Value[0] = 0;
            curve->from_Value[1] = 127;
            curve->to_Scalar[0] = 0;   // 0%
            curve->to_Scalar[1] = 256; // 100%
            pInstrument->curveRecordCount++;
            BAE_PRINTF("SF2 Debug: DMOD fallback - Added MOD_WHEEL curve to scale vibrato LFO depth\n");
        }

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

    // If using extreme defaults, replace with more reasonable values
    if (tcAttack == -12000)
        tcAttack = -7973; // ~100ms
    if (tcDecay == -12000)
        tcDecay = -7973; // ~100ms
    if (tcRel == -12000)
        tcRel = -7973; // ~100ms

    // Sustain of 0 means sustain at full scale (no attenuation). Do not invent decay here.

    // Clamp timecents to prevent extreme values that could cause issues
    if (tcDelay < -12000)
        tcDelay = -12000;
    if (tcDelay > 8000)
        tcDelay = 8000;
    if (tcAttack < -12000)
        tcAttack = -12000;
    if (tcAttack > 8000)
        tcAttack = 8000;
    if (tcHold < -12000)
        tcHold = -12000;
    if (tcHold > 8000)
        tcHold = 8000;
    if (tcDecay < -12000)
        tcDecay = -12000;
    if (tcDecay > 8000)
        tcDecay = 8000;
    if (tcRel < -12000)
        tcRel = -12000;
    if (tcRel > 8000)
        tcRel = 8000;

    // Convert to engine units
    uint32_t tDelay = PV_SF2_TimecentsToUSec(tcDelay);
    uint32_t tAttack = PV_SF2_TimecentsToUSec(tcAttack);
    uint32_t tHold = PV_SF2_TimecentsToUSec(tcHold);
    uint32_t tDecay = PV_SF2_TimecentsToUSec(tcDecay);
    uint32_t tRel = PV_SF2_TimecentsToUSec(tcRel);

    BAE_PRINTF("SF2 Debug: Raw generators - Delay:%d, Attack:%d, Hold:%d, Decay:%d, Sustain:%d, Release:%d, InitAtt:%d\n",
               (int)tcDelay, (int)tcAttack, (int)tcHold, (int)tcDecay, (int)cBSus, (int)tcRel, (int)cBInitAtt);

    BAE_PRINTF("SF2 Debug: Converted times - tDelay:%uus, tAttack:%uus, tHold:%uus, tDecay:%uus, tRel:%uus\n",
               tDelay, tAttack, tHold, tDecay, tRel);

    // Ensure minimum timing to prevent zero-time stages and single-slice jumps.
    // Use the engine's slice time so ramps span at least one slice.
    // Ensure a minimum of four slices for audible smoothness across engine updates
    const uint32_t kMinStageUS = (uint32_t)BUFFER_SLICE_TIME;
    if (tAttack > 0 && tAttack < kMinStageUS)
        tAttack = kMinStageUS;
    if (tDecay > 0 && tDecay < kMinStageUS)
        tDecay = kMinStageUS;
    if (tRel > 0 && tRel < kMinStageUS)
        tRel = kMinStageUS;

    BAE_PRINTF("SF2 Debug: Final times - tAttack:%uus, tDecay:%uus, tRel:%uus\n",
               tAttack, tDecay, tRel);

    // Levels per SF2 spec:
    // - initial attenuation (cBInitAtt) defines the peak level reached at end of attack
    // - sustain (cBSus) is attenuation below FULL SCALE to hold during sustain
    // The decay target is the lower (quieter) of peak vs sustain.
    XSDWORD peakLevel = PV_SF2_LevelFromCentibels(cBInitAtt, VOLUME_RANGE);
    XSDWORD sustainAbsLevel = PV_SF2_LevelFromCentibels(cBSus, VOLUME_RANGE);
    XSDWORD sustainLevel = (sustainAbsLevel < peakLevel) ? sustainAbsLevel : peakLevel;

    BAE_PRINTF("SF2 Debug: Level calculations - initAtt:%d cB, sustain:%d cB, peakLevel:%d, sustainLevel:%d (decay target)\n",
               (int)cBInitAtt, (int)cBSus, (int)peakLevel, (int)sustainLevel);

    // Initialize ADSR - start from silence and ramp up
    pADSR->currentTime = 0;
    pADSR->currentPosition = 0;
    pADSR->currentLevel = 0; // Start from silence
    pADSR->previousTarget = 0;
    pADSR->mode = 0;

    // For SF2, use sustainingDecayLevel only during sustain phase.
    // Set it to 1.0 initially, and let the ADSR_SUSTAIN handler apply the attenuation.
    pADSR->sustainingDecayLevel = XFIXED_1;

    pADSR->isSF2Envelope = TRUE; // Mark as SF2

    // Build proper SF2 ADSR: Delay -> Attack -> Hold -> Decay -> Sustain -> Release
    // Skip very short delay/hold stages to avoid engine timing issues
    int stageIndex = 0;

    // Stage: Delay (optional - stay at silence) - skip if default
    if (stageIndex < ADSR_STAGES && tcDelay > -12000)
    { // Only if non-default
        pADSR->ADSRLevel[stageIndex] = 0;
        pADSR->ADSRTime[stageIndex] = tDelay;
        pADSR->ADSRFlags[stageIndex] = ADSR_LINEAR_RAMP;
        stageIndex++;
        BAE_PRINTF("SF2 Debug: Added delay stage %d: %uus\n", stageIndex - 1, tDelay);
    }

    // Stage: Attack (silence -> peak level) - always present
    if (stageIndex < ADSR_STAGES)
    {
        pADSR->ADSRLevel[stageIndex] = peakLevel;
        pADSR->ADSRTime[stageIndex] = tAttack;
        pADSR->ADSRFlags[stageIndex] = ADSR_LINEAR_RAMP;
        stageIndex++;
        BAE_PRINTF("SF2 Debug: Added attack stage %d: %uus -> %d\n", stageIndex - 1, tAttack, (int)peakLevel);
    }

    // Stage: Hold (optional - stay at peak) - skip if default
    if (stageIndex < ADSR_STAGES && tcHold > -12000)
    { // Only if non-default
        pADSR->ADSRLevel[stageIndex] = peakLevel;
        pADSR->ADSRTime[stageIndex] = tHold;
        pADSR->ADSRFlags[stageIndex] = ADSR_LINEAR_RAMP;
        stageIndex++;
        BAE_PRINTF("SF2 Debug: Added hold stage %d: %uus\n", stageIndex - 1, tHold);
    }

    // Stage: Decay (peak -> sustain level, if needed)
    if (sustainLevel < peakLevel && stageIndex < ADSR_STAGES)
    {
        pADSR->ADSRLevel[stageIndex] = sustainLevel;
        pADSR->ADSRTime[stageIndex] = tDecay;
        pADSR->ADSRFlags[stageIndex] = ADSR_LINEAR_RAMP;
        stageIndex++;
        BAE_PRINTF("SF2 Debug: Added decay stage %d: %uus -> %d\n", stageIndex - 1, tDecay, (int)sustainLevel);
    }

    // If sustain level is effectively zero, terminate after decay to avoid hanging at silent sustain
    if (sustainLevel == 0)
    {
        if (stageIndex < ADSR_STAGES)
        {
            pADSR->ADSRLevel[stageIndex] = 0;
            pADSR->ADSRTime[stageIndex] = 1; // minimal time
            pADSR->ADSRFlags[stageIndex] = ADSR_TERMINATE;
            stageIndex++;
            BAE_PRINTF("SF2 Debug: Sustain level is zero; added TERMINATE stage %d after decay\n", stageIndex - 1);
        }
    }
    else
    {
        // Stage: Sustain (use negative level to trigger BAE's sustainingDecayLevel mechanism)
        if (stageIndex < ADSR_STAGES)
        {
            // Convert sustainLevel to a negative attenuation value that BAE expects
            // HSB format uses negative values to modify sustainingDecayLevel during sustain
            XSDWORD negativeLevel = -((peakLevel - sustainLevel) * 50000L / peakLevel);
            pADSR->ADSRLevel[stageIndex] = negativeLevel;
            pADSR->ADSRTime[stageIndex] = 0; // Indefinite time
            pADSR->ADSRFlags[stageIndex] = ADSR_SUSTAIN;
            stageIndex++;
            BAE_PRINTF("SF2 Debug: Added sustain stage %d: negative level %ld (sustain attenuation)\n",
                       stageIndex - 1, (long)negativeLevel);
        }
    }

    // Stage: Release (peak -> silence on note-off)
    if (stageIndex < ADSR_STAGES)
    {
        pADSR->ADSRLevel[stageIndex] = 0;
        pADSR->ADSRTime[stageIndex] = tRel;
        pADSR->ADSRFlags[stageIndex] = ADSR_RELEASE;
        stageIndex++;
        BAE_PRINTF("SF2 Debug: Added release stage %d: %uus -> 0\n", stageIndex - 1, tRel);
    }

    // Terminate remaining stages
    for (int i = stageIndex; i < ADSR_STAGES; i++)
    {
        pADSR->ADSRLevel[i] = 0;
        pADSR->ADSRTime[i] = 1;
        pADSR->ADSRFlags[i] = ADSR_TERMINATE;
    }

    BAE_PRINTF("SF2 Debug: Full ADSR - Delay:%uus, Attack:%uus->%d, Hold:%uus, Decay:%uus->%d, Sustain:%d, Release:%uus (%d stages)\n",
               (unsigned)tDelay, (unsigned)tAttack, (int)peakLevel,
               (unsigned)tHold, (unsigned)tDecay, (int)sustainLevel,
               (int)sustainLevel, (unsigned)tRel, stageIndex);
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
            int16_t keyRange = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, genStart, genEnd, SF2_GEN_KEY_RANGE, 0x007F);
            uint8_t keyLo = keyRange & 0xFF;
            uint8_t keyHi = (keyRange >> 8) & 0xFF;
            if (keyRange == 0x007F || keyHi == 0)
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
                int16_t zKeyRange = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, igStart, igEnd, SF2_GEN_KEY_RANGE, 0x007F);
                uint8_t zLo = zKeyRange & 0xFF;
                uint8_t zHi = (zKeyRange >> 8) & 0xFF;
                if (zKeyRange == 0x007F || zHi == 0)
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
#else // big endian - need to swap both
    header->id = XSwapLong(header->id);
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
    // Fallback to zone midpoint if range is valid; else middle C
    if (keyLo <= keyHi && keyHi <= 127)
    {
        int16_t midpoint = (int16_t)((keyLo + keyHi) / 2);
        BAE_PRINTF("SF2 Debug EffectiveRootKey: Using zone midpoint=%d (range %d-%d)\n", midpoint, keyLo, keyHi);
        return midpoint;
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
#else // big endian
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
        // Don't swap size - it's already in correct format
#else // big endian - need to swap both
        chunk.id = XSwapLong(chunk.id);
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
#else // big endian - need to swap both
                    subchunk.id = XSwapLong(subchunk.id);
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
#else // big endian - need to swap both
                    subchunk.id = XSwapLong(subchunk.id);
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
    for (uint32_t i = startIndex; i < endIndex && i < genCount; i++)
    {
        if (generators[i].generator == genType)
        {
            return (int16_t)generators[i].amount;
        }
    }
    return defaultValue;
}

// Determine the instrument-level global generator range, if present.
// The first instrument bag is considered a global zone when it has no SAMPLE_ID generator.
static void PV_GetInstGlobalGenRange(SF2_Bank *pBank, int32_t instrumentID,
                                     uint32_t *pGStart, uint32_t *pGEnd, XBOOL *pHasGlobal)
{
    if (pGStart) *pGStart = 0;
    if (pGEnd) *pGEnd = 0;
    if (pHasGlobal) *pHasGlobal = FALSE;
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
        if (pGStart) *pGStart = gStart;
        if (pGEnd) *pGEnd = gEnd;
        if (pHasGlobal) *pHasGlobal = TRUE;
    }
}

// Find generator in local zone, else fall back to instrument-global zone if present.
static int16_t PV_FindInstGenMerged(SF2_Bank *pBank, int32_t instrumentID,
                                    uint32_t localStart, uint32_t localEnd,
                                    SF2_GeneratorType genType, int16_t defaultValue)
{
    // Try local zone first
    int16_t v = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, localStart, localEnd, genType, 0x7FFF);
    if (v != 0x7FFF)
        return v;

    // Then instrument-global
    uint32_t gStart = 0, gEnd = 0; XBOOL hasGlobal = FALSE;
    PV_GetInstGlobalGenRange(pBank, instrumentID, &gStart, &gEnd, &hasGlobal);
    if (hasGlobal)
    {
        v = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, gStart, gEnd, genType, 0x7FFF);
        if (v != 0x7FFF)
            return v;
    }
    return defaultValue;
}

// Simple linear interpolation resampler to convert samples to target rate
static SBYTE *PV_ResampleSample(SBYTE *inputData, uint32_t inputFrames, uint32_t inputRate,
                                uint32_t targetRate, SBYTE bitsPerSample, SBYTE channels, uint32_t *outputFrames)
{
    if (inputRate == targetRate)
    {
        // No resampling needed
        *outputFrames = inputFrames;
        return inputData;
    }

    uint32_t outputSampleCount = (uint32_t)((uint64_t)inputFrames * targetRate / inputRate);
    uint32_t bytesPerSample = (bitsPerSample == 8) ? 1 : 2;
    uint32_t bytesPerFrame = bytesPerSample * channels;
    uint32_t outputSize = outputSampleCount * bytesPerFrame;

    SBYTE *outputData = (SBYTE *)XNewPtr(outputSize);
    if (!outputData)
    {
        *outputFrames = inputFrames;
        return inputData;
    }

    // Linear interpolation resampling
    float ratio = (float)inputFrames / (float)outputSampleCount;

    if (bitsPerSample == 16)
    {
        short *input16 = (short *)inputData;
        short *output16 = (short *)outputData;

        for (uint32_t i = 0; i < outputSampleCount; i++)
        {
            float srcIndex = i * ratio;
            uint32_t index0 = (uint32_t)srcIndex;
            uint32_t index1 = (index0 + 1 < inputFrames) ? index0 + 1 : index0;
            float frac = srcIndex - index0;

            for (int ch = 0; ch < channels; ch++)
            {
                int32_t sample0 = input16[index0 * channels + ch];
                int32_t sample1 = input16[index1 * channels + ch];
                int32_t interpolated = sample0 + (int32_t)((sample1 - sample0) * frac);
                output16[i * channels + ch] = (short)interpolated;
            }
        }
    }
    else // 8-bit
    {
        for (uint32_t i = 0; i < outputSampleCount; i++)
        {
            float srcIndex = i * ratio;
            uint32_t index0 = (uint32_t)srcIndex;
            uint32_t index1 = (index0 + 1 < inputFrames) ? index0 + 1 : index0;
            float frac = srcIndex - index0;

            for (int ch = 0; ch < channels; ch++)
            {
                int32_t sample0 = inputData[index0 * channels + ch];
                int32_t sample1 = inputData[index1 * channels + ch];
                int32_t interpolated = sample0 + (int32_t)((sample1 - sample0) * frac);
                outputData[i * channels + ch] = (SBYTE)interpolated;
            }
        }
    }

    *outputFrames = outputSampleCount;
    return outputData;
}

// Convert SF2 sample to miniBAE format
// Apply optional instrument fineTune (in cents) in addition to sample->pitchCorrection
static SBYTE *PV_ConvertSF2Sample(SF2_Bank *pBank, SF2_Sample *sample, int16_t instFineTune,
                                  uint32_t effectiveStart, uint32_t effectiveEnd,
                                  uint32_t *outSize, uint32_t *outTargetRate, OPErr *pErr)
{
    SBYTE *convertedSample = NULL;
    SBYTE *resampledData = NULL;
    uint32_t sampleSize;
    uint32_t originalFrames;
    uint32_t resampledFrames;

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
    // Apply sample header pitchCorrection (in cents) and any instrument fineTune by slightly adjusting the target rate
    // pitchCorrection is signed 8-bit in SF2 sample header; instFineTune is signed cents from instrument zone
    int totalCents = 0;
    if (sample->pitchCorrection != 0)
        totalCents += (int)sample->pitchCorrection;
    if (instFineTune != 0)
        totalCents += (int)instFineTune;
    if (totalCents != 0)
    {
        float cents = (float)totalCents;
        float centRatio = powf(2.0f, cents / 1200.0f);
        float adjustedRate = (float)targetRate * centRatio;
        if (adjustedRate < 1000.0f)
            adjustedRate = 1000.0f;
        if (adjustedRate > 192000.0f)
            adjustedRate = 192000.0f;
        targetRate = (uint32_t)adjustedRate;
        BAE_PRINTF("SF2 Debug: Applied combined pitchCorrection+fineTune %d cents, new targetRate=%u\n",
                   totalCents, targetRate);
    }

    // Resample only if sample rate differs from target rate or is out of allowed range
    resampledData = PV_ResampleSample(convertedSample, originalFrames, sample->sampleRate,
                                      targetRate, 16, 1, &resampledFrames);

    if (resampledData != convertedSample)
    {
        // Resampling created new data, free the original
        XDisposePtr(convertedSample);
        convertedSample = resampledData;
        sampleSize = resampledFrames * 2; // Update size for resampled data

        BAE_PRINTF("SF2 Debug: Resampled from %d to %d frames (rate %d -> %d)\n",
                   originalFrames, resampledFrames, sample->sampleRate, targetRate);
    }
    else
    {
        BAE_PRINTF("SF2 Debug: No resampling needed (keeping original rate %d Hz)\n", sample->sampleRate);
    }

    // Quick check if sample has any non-zero data
    int nonZeroCount = 0;
    int16_t *finalSamples = (int16_t *)convertedSample;
    uint32_t finalFrames = sampleSize / 2;
    for (uint32_t i = 0; i < finalFrames && i < 100; i++) // Check first 100 samples
    {
        if (finalSamples[i] != 0)
            nonZeroCount++;
    }
    BAE_PRINTF("SF2 Debug: Sample conversion complete - %d/%d samples have non-zero data\n",
               nonZeroCount, (finalFrames > 100) ? 100 : finalFrames);

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
        return PV_SF2_CreateSimpleInstrument(pBank, instrumentIDs, instrumentCount, pErr);
    }
    else
    {
        BAE_PRINTF("SF2 Debug: Multiple zones detected (%u), creating keymap split instrument\n", (unsigned)totalZones);
        return PV_SF2_CreateKeymapSplitInstrument(pBank, instrumentIDs, instrumentCount, pErr);
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
    pInstrument->disableSndLooping = (bankNum == 128) ? TRUE : FALSE; // Disable looping for percussion (one-shot drums)
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
        int16_t keyRange = PV_FindGeneratorValue(pBank->presetGens, pBank->numPresetGens, genStart, genEnd, SF2_GEN_KEY_RANGE, 0x007F);
        uint8_t keyLo = keyRange & 0xFF;
        uint8_t keyHi = (keyRange >> 8) & 0xFF;
        if (keyRange == 0x007F || keyHi == 0)
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
            int16_t keyRange = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_KEY_RANGE, 0x007F);
            uint8_t kLo = keyRange & 0xFF;
            uint8_t kHi = (keyRange >> 8) & 0xFF;
            if (keyRange == 0x007F || kHi == 0)
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
                                                     genStart, genEnd, SF2_GEN_KEY_RANGE, 0x007F); // Default 0-127

            uint8_t keyLo = keyRange & 0xFF;
            uint8_t keyHi = (keyRange >> 8) & 0xFF;
            if (keyRange == 0x007F || keyHi == 0)
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
                                                                  genStart, genEnd, SF2_GEN_KEY_RANGE, 0x007F);
                        uint8_t kLo = keyRange2 & 0xFF;
                        uint8_t kHi = (keyRange2 >> 8) & 0xFF;
                        if (keyRange2 == 0x007F || kHi == 0)
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
                int16_t keyRange = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_KEY_RANGE, 0x007F);
                uint8_t kLo = keyRange & 0xFF;
                uint8_t kHi = (keyRange >> 8) & 0xFF;
                if (keyRange == 0x007F || kHi == 0)
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
                    int16_t keyRange2 = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens, genStart, genEnd, SF2_GEN_KEY_RANGE, 0x007F);
                    uint8_t kLo = keyRange2 & 0xFF;
                    uint8_t kHi = (keyRange2 >> 8) & 0xFF;
                    if (keyRange2 == 0x007F || kHi == 0)
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

    // Fill LFO records from SF2 generators for this zone
    PV_SF2_FillLFORecords(pBank, bestInstID, selectedGenStart, selectedGenEnd, pInstrument);
    // Percussion: force base pitch to the triggering note so different keys select different samples,
    // not different transpositions of one sample.
    pInstrument->u.w.baseMidiPitch = note;

    // Apply pitch corrections (simplified)
    if (fineTune != 0 || coarseTune != 0)
    {
        int newBasePitch = pInstrument->u.w.baseMidiPitch + coarseTune;
        if (newBasePitch < 0)
            newBasePitch = 0;
        if (newBasePitch > 127)
            newBasePitch = 127;
        pInstrument->u.w.baseMidiPitch = newBasePitch;

        if (fineTune != 0)
        {
            float centRatio = 1.0f + (fineTune * 0.000578f);
            float adjustedRate = XFIXED_TO_FLOAT(pInstrument->u.w.sampledRate) * centRatio;

            if (adjustedRate < 1000.0f)
                adjustedRate = 1000.0f;
            if (adjustedRate > 192000.0f)
                adjustedRate = 192000.0f;

            // Use unsigned fixed conversion for adjusted floating sample rate
            pInstrument->u.w.sampledRate = FLOAT_TO_XFIXED(adjustedRate);
        }
    }

    BAE_PRINTF("SF2 Debug: Created note-specific instrument - note=%d, rootKey=%d, frames=%u\n",
               note, rootKey, (unsigned)pInstrument->u.w.waveFrames);

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
        BAE_PRINTF("SF2 Debug: Percussion instrument %d -> SF2 bank=128, preset=0, note=%d\n", instrument, noteNumber);
    }
    else if (isMSB128Perc)
    {
        // Treat explicit MIDI bank 128 as percussion
        // Keep requested kit program if provided; use note from low 7 bits if present
        uint16_t extProgram = midiProgram; // may indicate kit variant
        uint16_t noteGuess = midiProgram;  // best-effort note guess from instrument encoding
        midiBank = 128;                    // enforce SF2 percussion bank
        midiProgram = extProgram;          // try requested kit first, fall back later if needed
        BAE_PRINTF("SF2 Debug: Percussion (MSB 128) instrument %d -> SF2 bank=128, preset=%u, note~=%u\n",
                   instrument, (unsigned)extProgram, (unsigned)noteGuess);
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
                        BAE_PRINTF("SF2 Debug: Perc (odd map) using preset '%s' bank=%u prog=%u note=%u\n",
                                   preset->name, (unsigned)preset->bank, (unsigned)preset->preset, (unsigned)noteNumber);
                        pInstrument = SF2_CreateInstrumentFromPresetWithNote(sf2Bank, midiBank, midiProgram, noteNumber, pErr);
                    }
                    // Case B: direct SF2 drum bank requested (bank 128) but not odd mapping -> build full kit (keymap split)
                    else if (preset->bank == 128)
                    {
                        BAE_PRINTF("SF2 Debug: Perc (bank 128 kit) building keymap split for preset '%s'\n", preset->name);
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

    BAE_PRINTF("SF2 Debug Loop: sample=%u, headerHadLoop=%s, header loop %u-%u, eff loop %d-%d, window %d-%d\n",
               (unsigned)sampleID, headerHadLoop ? "YES" : "NO",
               (unsigned)sample->startloop, (unsigned)sample->endloop,
               (int)effStartLoop, (int)effEndLoop, (int)effStart, (int)effEnd);

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

    BAE_PRINTF("SF2 Debug: Final loop points set - start=%u, end=%u (frames=%u)\n",
               (unsigned)loopStart, (unsigned)loopEnd, (unsigned)pWaveform->waveFrames);

    // Get the root key using merged generators
    int16_t zoneRootKey = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_OVERRIDING_ROOT_KEY, -1);
    int16_t keyRange = PV_FindInstGenMerged(pBank, instrumentID, genStart, genEnd, SF2_GEN_KEY_RANGE, 0x007F);
    uint8_t keyLo = keyRange & 0xFF;
    uint8_t keyHi = (keyRange >> 8) & 0xFF;
    if (keyRange == 0x007F || keyHi == 0)
    {
        keyLo = 0;
        keyHi = 127;
    }

    pWaveform->baseMidiPitch = PV_EffectiveRootKey(pBank, sampleID, zoneRootKey, keyLo, keyHi);

    // Apply coarse tuning to base pitch
    if (coarseTune != 0)
    {
        int newBasePitch = pWaveform->baseMidiPitch + coarseTune;
        if (newBasePitch < 0)
            newBasePitch = 0;
        if (newBasePitch > 127)
            newBasePitch = 127;
        pWaveform->baseMidiPitch = newBasePitch;
    }

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
                                                         genStart, genEnd, SF2_GEN_KEY_RANGE, 0x007F);
                uint8_t keyLo = keyRange & 0xFF;
                uint8_t keyHi = (keyRange >> 8) & 0xFF;
                if (keyRange == 0x007F || keyHi == 0)
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

                // Fill LFO records from SF2 generators for this zone
                PV_SF2_FillLFORecords(pBank, instrumentIDs[i], genStart, genEnd, pInstrument);

                *pErr = NO_ERR;
                return pInstrument;
            }
        }
    }

    *pErr = BAD_INSTRUMENT;
    return NULL;
}

// Helper function to create a keymap split instrument for multi-zone instruments
static GM_Instrument *PV_SF2_CreateKeymapSplitInstrument(SF2_Bank *pBank, int32_t *instrumentIDs, uint32_t instrumentCount, OPErr *pErr)
{
    BAE_PRINTF("SF2 Debug: Creating keymap split instrument from %u instruments\n", (unsigned)instrumentCount);

    // Collect all zones with their key ranges
    typedef struct
    {
        int16_t sampleID;
        uint8_t lowKey, highKey;
        int16_t rootKey;
        uint32_t genStart, genEnd;
        int32_t instrumentID;
    } ZoneInfo;

#define MAX_SF2_ZONES 32 // Reasonable upper bound for SF2 zones
    ZoneInfo zones[MAX_SF2_ZONES];
    uint32_t zoneCount = 0;

    // Scan all instruments for zones
    for (uint32_t i = 0; i < instrumentCount && zoneCount < MAX_SF2_ZONES; i++)
    {
        SF2_Instrument *instrument = &pBank->instruments[instrumentIDs[i]];
        uint32_t instBagStart = instrument->bagIndex;
        uint32_t instBagEnd = (instrumentIDs[i] + 1 < pBank->numInstruments) ? pBank->instruments[instrumentIDs[i] + 1].bagIndex : pBank->numInstBags;

        for (uint32_t bagIdx = instBagStart; bagIdx < instBagEnd && zoneCount < MAX_SF2_ZONES; bagIdx++)
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
                // Get key range or fixed GEN_KEYNUM; GEN_KEYNUM overrides range when present
                int16_t keyRange = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens,
                                                         genStart, genEnd, SF2_GEN_KEY_RANGE, 0x007F);
                uint8_t lowKey = keyRange & 0xFF;
                uint8_t highKey = (keyRange >> 8) & 0xFF;
                if (keyRange == 0x007F || highKey == 0)
                {
                    lowKey = 0;
                    highKey = 127;
                }
                int16_t zKeyNum = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens,
                                                        genStart, genEnd, SF2_GEN_KEYNUM, -1);
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
                if (highKey > 127)
                    highKey = 127;

                // Get root key
                int16_t zoneRootKey = PV_FindGeneratorValue(pBank->instGens, pBank->numInstGens,
                                                            genStart, genEnd, SF2_GEN_OVERRIDING_ROOT_KEY, -1);
                int16_t rootKey = PV_EffectiveRootKey(pBank, sampleID, zoneRootKey, lowKey, highKey);

                zones[zoneCount].sampleID = sampleID;
                zones[zoneCount].lowKey = lowKey;
                zones[zoneCount].highKey = highKey;
                zones[zoneCount].rootKey = rootKey;
                zones[zoneCount].genStart = genStart;
                zones[zoneCount].genEnd = genEnd;
                zones[zoneCount].instrumentID = instrumentIDs[i];

                BAE_PRINTF("SF2 Debug: Zone %u: sample=%d, range=%u-%u%s, rootKey=%d\n",
                           (unsigned)zoneCount, (int)sampleID, (unsigned)lowKey, (unsigned)highKey,
                           (zKeyNum >= 0 && zKeyNum <= 127) ? " (fixed)" : "",
                           (int)rootKey);

                zoneCount++;
            }
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
    // Sort zones by lowKey to keep order consistent
    for (uint32_t a = 0; a + 1 < zoneCount; a++)
    {
        for (uint32_t b = a + 1; b < zoneCount; b++)
        {
            if (zones[b].lowKey < zones[a].lowKey)
            {
                ZoneInfo tmp = zones[a];
                zones[a] = zones[b];
                zones[b] = tmp;
            }
        }
    }

    // Calculate extra bytes needed to store all key splits beyond the first in the flexible array
    size_t extraSplits = (zoneCount > 1) ? (zoneCount - 1) : 0;
    size_t extraBytes = extraSplits * sizeof(GM_KeymapSplit);

    // Allocate instrument with embedded larger KeymapSplitInfo in union storage
    size_t totalSize = sizeof(GM_Instrument) + extraBytes;
    GM_Instrument *pMainInstrument = (GM_Instrument *)XNewPtr(totalSize);
    if (!pMainInstrument)
    {
        *pErr = MEMORY_ERR;
        return NULL;
    }
    XSetMemory(pMainInstrument, totalSize, 0);

    // Initialize as keymap split instrument
    pMainInstrument->doKeymapSplit = TRUE; // This is the key difference!
    pMainInstrument->extendedFormat = FALSE;
    pMainInstrument->notPolyphonic = FALSE;
    pMainInstrument->useSampleRate = TRUE;
    pMainInstrument->disableSndLooping = FALSE;
    pMainInstrument->playAtSampledFreq = FALSE;
    pMainInstrument->sampleAndHold = FALSE;
    pMainInstrument->usageReferenceCount = 0;
    pMainInstrument->panPlacement = 0;
    // Keep parent masterRootKey 0 so routing uses the actual notePitch for split selection
    pMainInstrument->masterRootKey = 0;

#if REVERB_USED != REVERB_DISABLED
    pMainInstrument->avoidReverb = FALSE;
#endif

    // Initialize keymap header
    pMainInstrument->u.k.defaultInstrumentID = 0; // No default
    pMainInstrument->u.k.KeymapSplitCount = zoneCount;

    // Create sub-instruments for each zone
    for (uint32_t i = 0; i < zoneCount; i++)
    {
        ZoneInfo *zone = &zones[i];

        // Allocate sub-instrument
        GM_Instrument *pSubInstrument = (GM_Instrument *)XNewPtr(sizeof(GM_Instrument));
        if (!pSubInstrument)
        {
            // Cleanup previous sub-instruments
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

        // Initialize sub-instrument (like simple instrument)
        pSubInstrument->doKeymapSplit = FALSE;
        pSubInstrument->extendedFormat = FALSE;
        pSubInstrument->notPolyphonic = FALSE;
        pSubInstrument->useSampleRate = TRUE;
        pSubInstrument->disableSndLooping = FALSE;
        // Melodic multi-zone instruments should transpose as needed
        pSubInstrument->playAtSampledFreq = FALSE;
        pSubInstrument->sampleAndHold = FALSE;
        pSubInstrument->usageReferenceCount = 0;
        pSubInstrument->panPlacement = 0;
        // Important: leave sub-instrument masterRootKey at 0 so playPitch stays as the MIDI note
        // Pitch offset will be computed using waveform baseMidiPitch (set from SF2 root key)
        pSubInstrument->masterRootKey = 0;

#if REVERB_USED != REVERB_DISABLED
        pSubInstrument->avoidReverb = FALSE;
#endif

        // Fill ADSR from SF2 generators for this zone
    PV_SF2_FillVolumeADSR(pBank, zone->instrumentID, zone->genStart, zone->genEnd, &pSubInstrument->volumeADSRRecord);

        // Fill LFO records from SF2 generators for this zone
    PV_SF2_FillLFORecords(pBank, zone->instrumentID, zone->genStart, zone->genEnd, pSubInstrument);

        // Create waveform for this zone
    OPErr waveErr = PV_SF2_CreateWaveformFromSample(pBank, zone->instrumentID, zone->sampleID, zone->genStart, zone->genEnd, &pSubInstrument->u.w);
        if (waveErr != NO_ERR)
        {
            // Cleanup
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

        // Set up keymap split entry
        pMainInstrument->u.k.keySplits[i].lowMidi = zone->lowKey;
        pMainInstrument->u.k.keySplits[i].highMidi = zone->highKey;
        pMainInstrument->u.k.keySplits[i].miscParameter1 = 0;   // No modifier
        pMainInstrument->u.k.keySplits[i].miscParameter2 = 100; // 100% volume
        pMainInstrument->u.k.keySplits[i].pSplitInstrument = pSubInstrument;

        BAE_PRINTF("SF2 Debug: Created zone %u: keys %u-%u -> rootKey=%d\n",
                   (unsigned)i, (unsigned)zone->lowKey, (unsigned)zone->highKey, (int)zone->rootKey);
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
