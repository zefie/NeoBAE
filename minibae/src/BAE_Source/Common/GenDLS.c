/*
   Minimal DLS loader for miniBAE

   Notes:
   - Supports PCM waves from wvpl LIST, loop from wsmp chunk, format from fmt
   - Creates GM_Instrument per request by bank/program mapping
   - No modulators or complex envelopes; default ADSR and pan=0
   - Percussion: treat bank 120 as drum kit; use note mapping regions
*/

#include "GenDLS.h"
#include "X_API.h"
#include "X_Formats.h"
#include "GenPriv.h"
#include "X_Assert.h"
#include "MiniBAE.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#if USE_DLS_SUPPORT == TRUE

// Helper function to convert DLS level to engine level (0-4096)
static XSDWORD DLS_LevelToEngineLevel(XSDWORD dls_level)
{
    // DLS uses 0.1% units (0-1000), convert to 0-4096 range
    return (dls_level * 4096) / 1000;
}

// Convert DLS LFO "frequency" (DLS cents around 8.176 Hz) to period in microseconds
static inline uint32_t DLS_FreqToLFOPeriodUS(int32_t dlsFreqCents)
{
    // Per DLS spec, oscillator frequency is in log2 cents relative to 8.176 Hz
    // Use same base as SF2: 8.176 * 2^(cents/1200)
    float freq_hz = 8.176f;
    if (dlsFreqCents != 0)
        freq_hz = 8.176f * powf(2.0f, (float)dlsFreqCents / 1200.0f);
    if (freq_hz < 0.001f)
        freq_hz = 0.001f; // cap to 1000s period
    if (freq_hz > 100.0f)
        freq_hz = 100.0f; // cap to 10ms period
    uint32_t period_us = (uint32_t)(1000000.0f / freq_hz);
    printf("DLS Debug: LFO freq conversion - cents=%d -> %.3f Hz -> %u us period\n", 
           dlsFreqCents, freq_hz, period_us);
    return period_us;
}

// Clamp microsecond durations to safe bounds and optionally enforce a minimum slice
static inline uint32_t DLS_ClampUS(uint32_t usec)
{
    const uint32_t kMax = 60000000U; // 60s per stage cap
    if (usec > kMax)
        usec = kMax;
    return usec;
}

// Convert DLS/SF2 timecents to microseconds
// timecents tc: seconds = 2^(tc/1200); microseconds = seconds * 1e6
static inline uint32_t DLS_TimecentsToUS(int32_t timecents)
{
    // Per spec, very negative values approach 0; clamp to 0 usec
    if (timecents <= -32768)
        return 0;
    float seconds = powf(2.0f, (float)timecents / 1200.0f);
    if (seconds < 0.0f)
        seconds = 0.0f;
    // Cap absurdly long
    if (seconds > 60.0f)
        seconds = 60.0f;
    return (uint32_t)(seconds * 1000000.0f);
}

// Convert microseconds to engine ticks
static inline XSDWORD DLS_MicrosecondsToTicks(uint32_t usec)
{
    if (usec == 0) return 0;
    
    // Get the current audio sample rate
    uint32_t sampleRate = GM_ConvertFromOutputRateToRate(MusicGlobals->outputRate);
    
    // Convert: ticks = (usec * sampleRate) / 1000000
    uint64_t ticks = ((uint64_t)usec * sampleRate) / 1000000ULL;
    
    // Clamp to reasonable range
    if (ticks > 0x7FFFFFFFL) ticks = 0x7FFFFFFFL;
    if (ticks < 1) ticks = 1; // minimum 1 tick to avoid zero-time stages
    
    return (XSDWORD)ticks;
}

// Helper function to parse DLS articulation data into a GM_Instrument ADSR/LFOs
static void DLS_ParseArticulation(const void *pArticulationData, GM_Instrument *pInstrument)
{
    const struct
    {
        int32_t volEnvDelay;
        int32_t volEnvAttack;
        int32_t volEnvHold;
        int32_t volEnvDecay;
        int32_t volEnvSustain;
        int32_t volEnvRelease;
        int32_t lfoFreq;
        int32_t lfoDelay;
        int32_t lfoToPitch;
        int32_t lfoToVolume;
        int32_t lfoToFilterFc;
    } *pArticulation = (const void *)pArticulationData;

    // Debug articulation parsing
    printf("DLS Debug: Articulation (us): delay=%d, attack=%d, hold=%d, decay=%d, release=%d; sustain(0..1000)=%d\n",
           (int)pArticulation->volEnvDelay, (int)pArticulation->volEnvAttack, (int)pArticulation->volEnvHold,
           (int)pArticulation->volEnvDecay, (int)pArticulation->volEnvRelease, (int)pArticulation->volEnvSustain);
    
    printf("DLS Debug: LFO params: freq=%d, delay=%d, toPitch=%d, toVolume=%d, toFilter=%d\n",
           (int)pArticulation->lfoFreq, (int)pArticulation->lfoDelay, (int)pArticulation->lfoToPitch,
           (int)pArticulation->lfoToVolume, (int)pArticulation->lfoToFilterFc);
    
    // Initialize ADSR envelope state
    GM_ADSR *a = &pInstrument->volumeADSRRecord;
    XSetMemory(a, sizeof(GM_ADSR), 0);
    a->sustainingDecayLevel = XFIXED_1; // Start with full scale

    // Gather and clamp DLS times (already in microseconds from parser)
    uint32_t tDelay = DLS_ClampUS((pArticulation->volEnvDelay > 0) ? (uint32_t)pArticulation->volEnvDelay : 0);
    uint32_t tAttack = DLS_ClampUS((pArticulation->volEnvAttack > 0) ? (uint32_t)pArticulation->volEnvAttack : 1000);
    uint32_t tHold = DLS_ClampUS((pArticulation->volEnvHold > 0) ? (uint32_t)pArticulation->volEnvHold : 0);
    uint32_t tDecay = DLS_ClampUS((pArticulation->volEnvDecay > 0) ? (uint32_t)pArticulation->volEnvDecay : 100000);
    uint32_t tRel = DLS_ClampUS((pArticulation->volEnvRelease > 0) ? (uint32_t)pArticulation->volEnvRelease : 100000);

    // Enforce minimal non-zero stage times to at least one engine slice to avoid zippering
    const uint32_t kMinStageUS = 1000; // 1ms minimum
    if (tAttack > 0 && tAttack < kMinStageUS)
        tAttack = kMinStageUS;
    if (tDecay > 0 && tDecay < kMinStageUS)
        tDecay = kMinStageUS;
    if (tRel > 0 && tRel < kMinStageUS)
        tRel = kMinStageUS;

    // DLS sustain is 0..1000 tenths of a percent, convert to 0-4096 scale
    int32_t sustainLevelRaw = (pArticulation->volEnvSustain >= 0) ? pArticulation->volEnvSustain : 1000;
    if (sustainLevelRaw > 1000) sustainLevelRaw = 1000;
    XSDWORD sustainLevel = (sustainLevelRaw * VOLUME_RANGE) / 1000;

    // Build ADSR following HSB pattern: Delay -> Attack -> Hold -> Decay -> Sustain -> Release -> Terminate
    int stage = 0;
    a->currentLevel = 0;
    a->previousTarget = 0;
    a->currentTime = 0;
    a->currentPosition = 0;
    a->mode = 0;

    // Optional delay at silence
    if (tDelay > 0 && stage < ADSR_STAGES)
    {
        a->ADSRLevel[stage] = 0;
        a->ADSRTime[stage] = DLS_MicrosecondsToTicks(tDelay);
        a->ADSRFlags[stage] = ADSR_LINEAR_RAMP;
        stage++;
    }
    
    // Attack to full scale
    if (stage < ADSR_STAGES)
    {
        a->ADSRLevel[stage] = VOLUME_RANGE;
        a->ADSRTime[stage] = DLS_MicrosecondsToTicks(tAttack);
        a->ADSRFlags[stage] = ADSR_LINEAR_RAMP;
        stage++;
    }
    
    // Optional hold at peak
    if (tHold > 0 && stage < ADSR_STAGES)
    {
        a->ADSRLevel[stage] = VOLUME_RANGE;
        a->ADSRTime[stage] = DLS_MicrosecondsToTicks(tHold);
        a->ADSRFlags[stage] = ADSR_LINEAR_RAMP;
        stage++;
    }
    
    // Decay to sustain level (if below peak)
    if (sustainLevel < VOLUME_RANGE && stage < ADSR_STAGES)
    {
        a->ADSRLevel[stage] = sustainLevel;
        a->ADSRTime[stage] = DLS_MicrosecondsToTicks(tDecay);
        a->ADSRFlags[stage] = ADSR_LINEAR_RAMP;
        stage++;
    }
    
    // Sustain phase: Follow HSB pattern exactly
    if (stage < ADSR_STAGES)
    {
        if (sustainLevel == 0)
        {
            // No sustain - go directly to silence (like percussion)
            a->ADSRLevel[stage] = 0;
        }
        else if (sustainLevel < VOLUME_RANGE)
        {
            // HSB uses negative values to trigger sustainingDecayLevel processing
            // Use time-based decay value like HSB instruments
            XSDWORD decayTime = tDecay; // Use decay time as basis
            if (decayTime < 50000) decayTime = 50000; // minimum 50ms
            if (decayTime > 15000000) decayTime = 15000000; // maximum 15s
            
            // Convert to HSB-style negative level (time in 50ms units)
            XSDWORD negativeLevel = -(decayTime / 50000);
            a->ADSRLevel[stage] = negativeLevel;
        }
        else
        {
            // Full sustain level - hold steady
            a->ADSRLevel[stage] = sustainLevel;
        }
        a->ADSRTime[stage] = 0; // indefinite
        a->ADSRFlags[stage] = ADSR_SUSTAIN;
        stage++;
    }
    
    // Release to zero on note-off
    if (stage < ADSR_STAGES)
    {
        a->ADSRLevel[stage] = 0;
        a->ADSRTime[stage] = DLS_MicrosecondsToTicks(tRel);
        a->ADSRFlags[stage] = ADSR_RELEASE;
        stage++;
    }
    
    // Terminate
    if (stage < ADSR_STAGES)
    {
        a->ADSRLevel[stage] = 0;
        a->ADSRTime[stage] = 1;
        a->ADSRFlags[stage] = ADSR_TERMINATE;
        stage++;
    }
    
    // Clear remaining stages
    for (int i = stage; i < ADSR_STAGES; ++i)
    {
        a->ADSRLevel[i] = 0;
        a->ADSRTime[i] = 1;
        a->ADSRFlags[i] = ADSR_TERMINATE;
    }

    // Parse LFO data (optional)
    int createdLFOs = 0;
    if (pInstrument->LFORecordCount < MAX_LFOS)
    {
        // Choose destination priority: pitch, then volume, then filter
        // Apply pitch LFO with conservative scaling
        if (pArticulation->lfoToPitch != 0)
        {
            GM_LFO *lfo = &pInstrument->LFORecords[pInstrument->LFORecordCount++];
            XSetMemory(lfo, sizeof(GM_LFO), 0);
            lfo->period = DLS_FreqToLFOPeriodUS(pArticulation->lfoFreq);
            lfo->where_to_feed = PITCH_LFO;
            // Scale pitch LFO much more conservatively - DLS cents may be too aggressive
            lfo->level = (INT16)(pArticulation->lfoToPitch / 8); // very conservative scaling
            printf("DLS Debug: Applied pitch LFO - freq=%d, level=%d cents (engine=%d), period=%u us\n",
                   (int)pArticulation->lfoFreq, (int)pArticulation->lfoToPitch, lfo->level, lfo->period);
            // Simple delay ADSR for LFO depth
            if (pArticulation->lfoDelay > 0)
            {
                uint32_t lDelay = DLS_ClampUS((uint32_t)pArticulation->lfoDelay);
                lfo->a.ADSRLevel[0] = 0;
                lfo->a.ADSRTime[0] = DLS_MicrosecondsToTicks(lDelay);
                lfo->a.ADSRFlags[0] = ADSR_LINEAR_RAMP;
                lfo->a.ADSRLevel[1] = 65536;
                lfo->a.ADSRTime[1] = 1;
                lfo->a.ADSRFlags[1] = ADSR_TERMINATE;
                printf("DLS Debug: LFO delay - %u us -> %d ticks\n", lDelay, lfo->a.ADSRTime[0]);
            }
            else
            {
                printf("DLS Debug: No LFO delay\n");
            }
            lfo->a.sustainingDecayLevel = XFIXED_1;
            createdLFOs++;
        }
        if (pArticulation->lfoToVolume != 0 && pInstrument->LFORecordCount < MAX_LFOS)
        {
            GM_LFO *lfo = &pInstrument->LFORecords[pInstrument->LFORecordCount++];
            XSetMemory(lfo, sizeof(GM_LFO), 0);
            lfo->period = DLS_FreqToLFOPeriodUS(pArticulation->lfoFreq);
            lfo->where_to_feed = VOLUME_LFO;
            lfo->level = (INT16)(pArticulation->lfoToVolume * 16); // cB -> engine units
            if (pArticulation->lfoDelay > 0)
            {
                uint32_t lDelay = DLS_ClampUS((uint32_t)pArticulation->lfoDelay);
                lfo->a.ADSRLevel[0] = 0;
                lfo->a.ADSRTime[0] = DLS_MicrosecondsToTicks(lDelay);
                lfo->a.ADSRFlags[0] = ADSR_LINEAR_RAMP;
                lfo->a.ADSRLevel[1] = 65536;
                lfo->a.ADSRTime[1] = 1;
                lfo->a.ADSRFlags[1] = ADSR_TERMINATE;
            }
            lfo->a.sustainingDecayLevel = XFIXED_1;
            createdLFOs++;
        }
        if (pArticulation->lfoToFilterFc != 0 && pInstrument->LFORecordCount < MAX_LFOS)
        {
            GM_LFO *lfo = &pInstrument->LFORecords[pInstrument->LFORecordCount++];
            XSetMemory(lfo, sizeof(GM_LFO), 0);
            lfo->period = DLS_FreqToLFOPeriodUS(pArticulation->lfoFreq);
            lfo->where_to_feed = LPF_FREQUENCY;
            lfo->level = (INT16)(pArticulation->lfoToFilterFc * 4); // cents -> engine units
            if (pArticulation->lfoDelay > 0)
            {
                uint32_t lDelay = DLS_ClampUS((uint32_t)pArticulation->lfoDelay);
                lfo->a.ADSRLevel[0] = 0;
                lfo->a.ADSRTime[0] = DLS_MicrosecondsToTicks(lDelay);
                lfo->a.ADSRFlags[0] = ADSR_LINEAR_RAMP;
                lfo->a.ADSRLevel[1] = 65536;
                lfo->a.ADSRTime[1] = 1;
                lfo->a.ADSRFlags[1] = ADSR_TERMINATE;
            }
            lfo->a.sustainingDecayLevel = XFIXED_1;
            createdLFOs++;
        }
    }
}

// Resample DLS sample to target rate with linear interpolation
static SBYTE *PV_ResampleDLSSample(SBYTE *inputData, uint32_t inputFrames, uint32_t inputRate,
                                   uint32_t targetRate, SBYTE bitsPerSample, SBYTE channels, uint32_t *outputFrames)
{
    if (inputRate == targetRate || inputFrames == 0)
    {
        // No resampling needed
        *outputFrames = inputFrames;
        return inputData;
    }

    uint32_t outputSampleCount = (uint32_t)((uint64_t)inputFrames * targetRate / inputRate);
    if (outputSampleCount == 0)
    {
        *outputFrames = inputFrames;
        return inputData;
    }

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
        int16_t *input16 = (int16_t *)inputData;
        int16_t *output16 = (int16_t *)outputData;

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
                // Clamp to 16-bit range
                if (interpolated > 32767)
                    interpolated = 32767;
                if (interpolated < -32768)
                    interpolated = -32768;
                output16[i * channels + ch] = (int16_t)interpolated;
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
                // Clamp to 8-bit range
                if (interpolated > 127)
                    interpolated = 127;
                if (interpolated < -128)
                    interpolated = -128;
                outputData[i * channels + ch] = (SBYTE)interpolated;
            }
        }
    }

    *outputFrames = outputSampleCount;
    return outputData;
}

#define FOURCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

enum
{
    FCC_RIFF = FOURCC('R', 'I', 'F', 'F'),
    FCC_WAVE = FOURCC('W', 'A', 'V', 'E'),
    FCC_DLS = FOURCC('D', 'L', 'S', ' '),
    FCC_LIST = FOURCC('L', 'I', 'S', 'T'),
    FCC_WVPL = FOURCC('w', 'v', 'p', 'l'),
    FCC_PTBL = FOURCC('p', 't', 'b', 'l'),
    FCC_WAVE_LIST = FOURCC('w', 'a', 'v', 'e'),
    FCC_FMT = FOURCC('f', 'm', 't', ' '),
    FCC_DATA = FOURCC('d', 'a', 't', 'a'),
    FCC_WSMP = FOURCC('w', 's', 'm', 'p'),
    FCC_LINS = FOURCC('l', 'i', 'n', 's'),
    FCC_INS = FOURCC('i', 'n', 's', ' '), // DLS instrument list type
    FCC_RGN = FOURCC('r', 'g', 'n', ' '),
    FCC_RGN2 = FOURCC('r', 'g', 'n', '2'),
    FCC_LRGN = FOURCC('l', 'r', 'g', 'n'),
    FCC_INFO = FOURCC('I', 'N', 'F', 'O'),
    FCC_ART1 = FOURCC('a', 'r', 't', '1'), // DLS1 articulation
    FCC_ART2 = FOURCC('a', 'r', 't', '2'), // DLS2 articulation
    FCC_LART = FOURCC('l', 'a', 'r', 't'), // LIST articulation (DLS1)
    FCC_LAR2 = FOURCC('l', 'a', 'r', '2'), // LIST articulation (DLS2)
    FCC_PGAL = FOURCC('p', 'g', 'a', 'l')  // Mobile DLS instrument aliasing
};

// DLS Connection block sources (from DLS spec)
enum
{
    CONN_SRC_NONE = 0x0000,
    CONN_SRC_LFO = 0x0001,
    CONN_SRC_KEYONVELOCITY = 0x0002,
    CONN_SRC_KEYNUMBER = 0x0003,
    CONN_SRC_EG1 = 0x0004,
    CONN_SRC_EG2 = 0x0005,
    CONN_SRC_PITCHWHEEL = 0x0006,
    CONN_SRC_CC1 = 0x0081,  // Mod wheel
    CONN_SRC_CC7 = 0x0087,  // Volume
    CONN_SRC_CC10 = 0x008A, // Pan
    CONN_SRC_CC11 = 0x008B  // Expression
};

// DLS Connection block destinations (from DLS spec)
enum
{
    CONN_DST_NONE = 0x0000,
    CONN_DST_ATTENUATION = 0x0001,
    CONN_DST_PITCH = 0x0003,
    CONN_DST_PAN = 0x0004,
    CONN_DST_LFO_FREQUENCY = 0x0104,
    CONN_DST_LFO_STARTDELAY = 0x0105,
    CONN_DST_EG1_ATTACKTIME = 0x0206,
    CONN_DST_EG1_DECAYTIME = 0x0207,
    CONN_DST_EG1_RELEASETIME = 0x0209,
    CONN_DST_EG1_SUSTAINLEVEL = 0x020A,
    CONN_DST_EG1_DELAYTIME = 0x020B,
    CONN_DST_EG1_HOLDTIME = 0x020C,
    CONN_DST_EG2_ATTACKTIME = 0x030A,
    CONN_DST_EG2_DECAYTIME = 0x030B,
    CONN_DST_EG2_RELEASETIME = 0x030D,
    CONN_DST_EG2_SUSTAINLEVEL = 0x030E
};

typedef struct
{
    uint16_t formatTag;
    uint16_t channels;
    uint32_t samplesPerSec;
    uint32_t avgBytesPerSec;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
} WAVEFMT;

// wsmp chunk minimal
typedef struct
{
    uint32_t size;      // bytes after this
    uint16_t unityNote; // MIDI note
    int16_t fineTune;   // cents
    int32_t gain;       // not used
    uint32_t options;   // bit 0=loops present
    uint32_t loopCount;
    // then loop structs
} WSMP_HEADER;

typedef struct
{
    uint32_t cbSize;
    uint32_t loopType;   // 0 forward
    uint32_t loopStart;  // in samples
    uint32_t loopLength; // samples
} WSMP_LOOP;

// DLS Connection block structure
typedef struct
{
    uint16_t usSource;
    uint16_t usControl;
    uint16_t usDestination;
    uint16_t usTransform;
    int32_t lScale;
} DLS_CONNECTION;

// DLS articulation header
typedef struct
{
    uint32_t cbSize;
    uint32_t cConnections;
    // followed by DLS_CONNECTION array
} DLS_ART_HEADER;

static DLS_BankManager g_dlsManager = {NULL, 0};

static uint32_t rd32(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t rd32le(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16le(const unsigned char *p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }

// Simple array grow helpers
static void *xrealloc_copy(void *ptr, size_t oldCount, size_t newCount, size_t elem)
{
    size_t newSz = newCount * elem;
    void *n = XNewPtr((int32_t)newSz);
    if (!n)
        return NULL;
    if (ptr && oldCount)
    {
        size_t oldSz = oldCount * elem;
        if (oldSz > newSz)
            oldSz = newSz;
        XBlockMove(ptr, n, (int32_t)oldSz);
        XDisposePtr(ptr);
    }
    return n;
}

// Parse Mobile DLS instrument aliasing (pgal chunk)
static void PV_ParseMobileDLSAliasing(const unsigned char *data, uint32_t size, DLS_Bank *bank)
{
    if (!data || size < 12 || !bank)
        return;
        
    printf("DLS: Parsing Mobile DLS pgal chunk (size=%u)\n", size);
    
    // Skip header (4 bytes 'pgal' + 8 bytes unknown)
    uint32_t pos = 12;
    
    if (pos + 128 > size) {
        printf("DLS: pgal chunk too small for drum alias table\n");
        return;
    }
    
    // Parse drum note aliasing table (128 bytes)
    bank->hasDrumAliasing = TRUE;
    for (int i = 0; i < 128; i++) {
        bank->drumAliasTable[i] = data[pos + i];
    }
    pos += 128;
    
    printf("DLS: Loaded drum aliasing table\n");
    
    // Skip 3 unknown bytes
    if (pos + 3 > size) {
        printf("DLS: pgal chunk ends after drum table\n");
        return;
    }
    pos += 3;
    
    // Parse melodic instrument aliasing entries (9 bytes each)
    uint32_t instrumentAliasCount = (size - pos) / 9;
    if (instrumentAliasCount > 0) {
        bank->instrumentAliases = (typeof(bank->instrumentAliases))XNewPtr(
            instrumentAliasCount * sizeof(*bank->instrumentAliases));
        if (!bank->instrumentAliases) {
            printf("DLS: Failed to allocate instrument alias table\n");
            return;
        }
        
        bank->instrumentAliasCount = instrumentAliasCount;
        printf("DLS: Parsing %u instrument aliases\n", instrumentAliasCount);
        
        for (uint32_t i = 0; i < instrumentAliasCount; i++) {
            if (pos + 9 > size) break;
            
            // Skip null byte
            pos++;
            
            // Source instrument: bank MSB/LSB (14 bits) + program
            uint16_t srcBankField = rd16le(data + pos);
            pos += 2;
            uint8_t srcProgram = data[pos++];
            
            // Skip null byte  
            pos++;
            
            // Destination instrument: bank MSB/LSB (14 bits) + program
            uint16_t dstBankField = rd16le(data + pos);
            pos += 2;
            uint8_t dstProgram = data[pos++];
            
            // Skip null byte
            pos++;
            
            bank->instrumentAliases[i].srcBank = srcBankField & 0x3FFF; // 14 bits
            bank->instrumentAliases[i].srcProgram = srcProgram;
            bank->instrumentAliases[i].dstBank = dstBankField & 0x3FFF; // 14 bits
            bank->instrumentAliases[i].dstProgram = dstProgram;
            
            printf("DLS: Alias %u: bank %u prog %u -> bank %u prog %u\n", i,
                   bank->instrumentAliases[i].srcBank, bank->instrumentAliases[i].srcProgram,
                   bank->instrumentAliases[i].dstBank, bank->instrumentAliases[i].dstProgram);
        }
    }
}

// Parse DLS articulation data from art1/art2 chunks
static void PV_ParseDLSArticulation(const unsigned char *data, uint32_t size, DLS_Region *region)
{
    if (!data || size < sizeof(DLS_ART_HEADER) || !region)
        return;

    // Initialize defaults (similar to SF2 defaults) only once per region
    if (!region->artInitialized)
    {
        region->articulation.volEnvDelay = 0;
        region->articulation.volEnvAttack = 10000; // ~10ms default
        region->articulation.volEnvHold = 0;
        region->articulation.volEnvDecay = 300000;   // ~300ms default
        region->articulation.volEnvSustain = 1000;   // full sustain
        region->articulation.volEnvRelease = 100000; // ~100ms default
        region->articulation.lfoFreq = 0;            // no LFO by default
        region->articulation.lfoDelay = 0;
        region->articulation.lfoToPitch = 0;
        region->articulation.lfoToVolume = 0;
        region->articulation.lfoToFilterFc = 0;
        region->artInitialized = 1;
    }

    const DLS_ART_HEADER *header = (const DLS_ART_HEADER *)data;
    uint32_t connections = rd32le((const unsigned char *)&header->cConnections);

    printf("DLS: Parsing articulation with %u connections\n", connections);

    const DLS_CONNECTION *conn = (const DLS_CONNECTION *)(data + sizeof(DLS_ART_HEADER));
    for (uint32_t i = 0; i < connections && (i + 1) * sizeof(DLS_CONNECTION) <= size - sizeof(DLS_ART_HEADER); i++)
    {
        uint16_t src = rd16le((const unsigned char *)&conn[i].usSource);
        uint16_t dst = rd16le((const unsigned char *)&conn[i].usDestination);
        int32_t scale = (int32_t)rd32le((const unsigned char *)&conn[i].lScale);

        printf("DLS: Connection %u: src=0x%04x dst=0x%04x scale=%d\n", i, src, dst, scale);

        // Parse envelope connections (EG1 = volume envelope)
        if (src == CONN_SRC_NONE)
        {
            switch (dst)
            {
            case CONN_DST_EG1_ATTACKTIME:
                region->articulation.volEnvAttack = (int32_t)DLS_ClampUS(DLS_TimecentsToUS(scale >> 16));
                break;
            case CONN_DST_EG1_DECAYTIME:
                region->articulation.volEnvDecay = (int32_t)DLS_ClampUS(DLS_TimecentsToUS(scale >> 16));
                break;
            case CONN_DST_EG1_RELEASETIME:
                region->articulation.volEnvRelease = (int32_t)DLS_ClampUS(DLS_TimecentsToUS(scale >> 16));
                break;
            case CONN_DST_EG1_SUSTAINLEVEL:
            case 0x0208: // some banks use this for sustain level
                // DLS uses 0.1% units (0..1000); here lScale appears as integer (logs show ~984)
                int32_t v = scale;
                if (v < 0)
                    v = 0;
                if (v > 1000)
                    v = 1000;
                region->articulation.volEnvSustain = v;
                break;
            case CONN_DST_EG1_DELAYTIME:
                region->articulation.volEnvDelay = (int32_t)DLS_ClampUS(DLS_TimecentsToUS(scale >> 16));
                break;
            case CONN_DST_EG1_HOLDTIME:
                region->articulation.volEnvHold = (int32_t)DLS_ClampUS(DLS_TimecentsToUS(scale >> 16));
                break;
            case CONN_DST_LFO_FREQUENCY:
                // Static LFO frequency specified in cents around 8.176 Hz
                region->articulation.lfoFreq = (scale >> 16);
                break;
            case CONN_DST_LFO_STARTDELAY:
                // Static LFO start delay in timecents
                region->articulation.lfoDelay = (int32_t)DLS_ClampUS(DLS_TimecentsToUS(scale >> 16));
                break;
            }
        }
        // Parse LFO connections
        else if (src == CONN_SRC_LFO)
        {
            switch (dst)
            {
            case CONN_DST_PITCH:
                // Depth in cents
                region->articulation.lfoToPitch = (scale >> 16);
                break;
            case CONN_DST_ATTENUATION:
                // Depth in centibels
                region->articulation.lfoToVolume = (scale >> 16);
                break;
            case CONN_DST_LFO_FREQUENCY:
                // Some banks may specify via src=LFO as well; accept
                region->articulation.lfoFreq = (scale >> 16);
                break;
            case CONN_DST_LFO_STARTDELAY:
                region->articulation.lfoDelay = (int32_t)DLS_ClampUS(DLS_TimecentsToUS(scale >> 16));
                break;
            }
        }
    }

    // Summary after parse
    printf("DLS: Parsed ART -> delay=%d us, attack=%d us, hold=%d us, decay=%d us, release=%d us, sustain=%d/1000\n",
           region->articulation.volEnvDelay, region->articulation.volEnvAttack, region->articulation.volEnvHold,
           region->articulation.volEnvDecay, region->articulation.volEnvRelease, region->articulation.volEnvSustain);
}

// Parse DLS into a simple in-memory bank
OPErr DLS_LoadBank(XFILENAME *file, DLS_Bank **ppBank)
{
    if (!file || !ppBank)
        return PARAM_ERR;
    *ppBank = NULL;

    int32_t fsize = 0;
    XPTR data = NULL;
    if (XGetFileAsData(file, &data, &fsize))
        return BAD_FILE;
    if (fsize < 12)
    {
        XDisposePtr(data);
        return BAD_FILE;
    }

    const unsigned char *ub = (const unsigned char *)data;
    if (rd32(ub) != FCC_RIFF)
    {
        XDisposePtr(data);
        return BAD_FILE;
    }
    if (rd32(ub + 8) != FCC_DLS)
    {
        XDisposePtr(data);
        return BAD_FILE;
    }

    DLS_Bank *bank = (DLS_Bank *)XNewPtr(sizeof(DLS_Bank));
    if (!bank)
    {
        XDisposePtr(data);
        return MEMORY_ERR;
    }
    bank->ownedMemory = data;
    bank->ownedSize = (uint32_t)fsize;
    bank->ptblOffsets = NULL;
    bank->ptblToWave = NULL;
    bank->ptblCount = 0;
    bank->wvplDataOffset = 0;
    
    // Initialize Mobile DLS aliasing fields
    bank->hasDrumAliasing = FALSE;
    for (int i = 0; i < 128; i++) {
        bank->drumAliasTable[i] = (uint8_t)i; // default: no aliasing
    }
    bank->instrumentAliases = NULL;
    bank->instrumentAliasCount = 0;

    // Iterate chunks
    uint32_t riffSize = rd32le(ub + 4);
    uint32_t pos = 12;
    while (pos + 8 <= (uint32_t)fsize && pos < riffSize + 8)
    {
        uint32_t cid = rd32(ub + pos);
        uint32_t csz = rd32le(ub + pos + 4);
        uint32_t cdat = pos + 8;

        // Debug: print chunk info
        char cname[5] = {0};
        cname[0] = (char)((cid >> 24) & 0xFF);
        cname[1] = (char)((cid >> 16) & 0xFF);
        cname[2] = (char)((cid >> 8) & 0xFF);
        cname[3] = (char)(cid & 0xFF);
        printf("DLS chunk: '%s' size=%u\n", cname, csz);

        if (cid == FCC_LIST)
        {
            if (cdat + 4 <= (uint32_t)fsize)
            {
                uint32_t ltype = rd32(ub + cdat);
                char lname[5] = {0};
                lname[0] = (char)((ltype >> 24) & 0xFF);
                lname[1] = (char)((ltype >> 16) & 0xFF);
                lname[2] = (char)((ltype >> 8) & 0xFF);
                lname[3] = (char)(ltype & 0xFF);
                printf("DLS LIST type: '%s'\n", lname);

                if (ltype == FCC_WVPL)
                {
                    // Remember start of wvpl data for ptbl-relative offsets
                    bank->wvplDataOffset = cdat + 4;
                    // Parse waves (LIST 'wave' ...)
                    uint32_t lpos = cdat + 4;
                    while (lpos + 8 <= pos + 8 + csz)
                    {
                        uint32_t scid = rd32(ub + lpos);
                        uint32_t scsz = rd32le(ub + lpos + 4);
                        uint32_t sdat = lpos + 8;
                        if (scid == FCC_LIST && sdat + 4 <= (uint32_t)fsize && rd32(ub + sdat) == FCC_WAVE_LIST)
                        {
                            // One wave
                            WAVEFMT fmt = {0};
                            const unsigned char *pcm = NULL;
                            uint32_t pcmBytes = 0;
                            uint32_t loopStart = 0, loopEnd = 0;
                            int hasLoop = 0;
                            int16_t unity = 60, fine = 0;
                            uint32_t wwpos = sdat + 4;
                            while (wwpos + 8 <= lpos + 8 + scsz)
                            {
                                uint32_t wid = rd32(ub + wwpos);
                                uint32_t wsz = rd32le(ub + wwpos + 4);
                                uint32_t wdat = wwpos + 8;
                                if (wid == FCC_FMT && wsz >= 16)
                                {
                                    fmt.formatTag = rd16le(ub + wdat + 0);
                                    fmt.channels = rd16le(ub + wdat + 2);
                                    fmt.samplesPerSec = rd32le(ub + wdat + 4);
                                    fmt.avgBytesPerSec = rd32le(ub + wdat + 8);
                                    fmt.blockAlign = rd16le(ub + wdat + 12);
                                    // For PCM, fmt chunk is 16 bytes and includes bitsPerSample at offset 14.
                                    // For compressed formats, bitsPerSample may represent the decoded precision.
                                    if (wsz >= 16)
                                        fmt.bitsPerSample = rd16le(ub + wdat + 14);
                                }
                                else if (wid == FCC_DATA)
                                {
                                    pcm = ub + wdat;
                                    pcmBytes = wsz;
                                }
                                else if (wid == FCC_WSMP && wsz >= sizeof(WSMP_HEADER))
                                {
                                    const unsigned char *wp = ub + wdat;
                                    uint32_t hdrSize = rd32le(wp + 0);
                                    if (wsz >= 20)
                                    {
                                        unity = (int16_t)rd16le(wp + 4);
                                        fine = (int16_t)(int16_t)rd16le(wp + 6);
                                        uint32_t options = rd32le(wp + 12);
                                        uint32_t lcount = rd32le(wp + 16);
                                        printf("DLS Debug: wsmp - unity=%d, fine=%d, options=0x%x, loopCount=%u\n",
                                               unity, fine, options, lcount);
                                        const unsigned char *lp = wp + 20;
                                        
                                        // Check if loops are present (either enabled or just defined)
                                        if (lcount > 0 && wsz >= 20 + 16)
                                        {
                                            // read first loop
                                            lp += 4; // cbsize
                                            uint32_t loopType = rd32le(lp); lp += 4;
                                            loopStart = rd32le(lp);
                                            lp += 4;
                                            uint32_t loopLen = rd32le(lp);
                                            lp += 4;
                                            loopEnd = loopStart + loopLen;
                                            
                                            // For sustained instruments, enable loops even if DLS says not to
                                            // Use loops if they are defined and valid
                                            if (loopLen > 0 && loopStart < loopEnd)
                                            {
                                                hasLoop = 1;
                                                printf("DLS Debug: Using loop - type=%u, start=%u, len=%u, end=%u\n",
                                                       loopType, loopStart, loopLen, loopEnd);
                                            }
                                            else
                                            {
                                                printf("DLS Debug: Invalid loop - type=%u, start=%u, len=%u, end=%u (skipped)\n",
                                                       loopType, loopStart, loopLen, loopEnd);
                                            }
                                        }
                                        else
                                        {
                                            printf("DLS Debug: No loop found (lcount = %u)\n", lcount);
                                        }
                                        (void)hdrSize;
                                    }
                                }
                                wwpos = wdat + ((wsz + 1) & ~1U);
                            }
                            if (pcm && fmt.channels >= 1)
                            {
                                if (fmt.formatTag == 1 && (fmt.bitsPerSample == 8 || fmt.bitsPerSample == 16))
                                {
                                    // PCM path: copy directly
                                    bank->waves = (DLS_Wave *)xrealloc_copy(bank->waves, bank->waveCount, bank->waveCount + 1, sizeof(DLS_Wave));
                                    if (!bank->waves)
                                    {
                                        DLS_UnloadBank(bank);
                                        return MEMORY_ERR;
                                    }
                                    DLS_Wave *w = &bank->waves[bank->waveCount++];
                                    w->sampleRate = fmt.samplesPerSec ? fmt.samplesPerSec : 22050;
                                    w->channels = fmt.channels;
                                    w->bitsPerSample = fmt.bitsPerSample;
                                    w->pcmBytes = pcmBytes;
                                    w->pcm = (unsigned char *)XNewPtr((int32_t)pcmBytes);
                                    if (!w->pcm)
                                    {
                                        DLS_UnloadBank(bank);
                                        return MEMORY_ERR;
                                    }
                                    XBlockMove((void *)pcm, w->pcm, (int32_t)pcmBytes);
                                    uint32_t bytesPerFrame = (fmt.bitsPerSample / 8) * fmt.channels;
                                    w->frameCount = bytesPerFrame ? (pcmBytes / bytesPerFrame) : 0;
                                    if (hasLoop)
                                    {
                                        if (loopEnd > w->frameCount)
                                            loopEnd = w->frameCount;
                                        w->loopStart = loopStart;
                                        w->loopEnd = loopEnd;
                                    }
                                    else
                                    {
                                        w->loopStart = 0;
                                        w->loopEnd = 0;
                                    }
                                    w->unityNote = (unity >= 0 && unity <= 127) ? unity : 60;
                                    w->fineTuneCents = fine;
                                    // Compute relative offset of this 'wave' LIST from start of wvpl (for ptbl mapping)
                                    w->wvplOffset = (lpos - (cdat + 4));
                                }
                                else if (fmt.formatTag == 0x0011) // WAVE_FORMAT_IMA_ADPCM
                                {
                                    // Decode IMA ADPCM to 16-bit PCM using existing decoder
                                    printf("DLS: Decoding IMA ADPCM wave (channels=%u, blockAlign=%u, bytes=%u)\n",
                                           (unsigned)fmt.channels, (unsigned)fmt.blockAlign, (unsigned)pcmBytes);
                                    XDWORD srcBytesPerBlock = (XDWORD)(fmt.blockAlign ? fmt.blockAlign : 0);
                                    XDWORD dstBitsPerSample = 16;
                                    XDWORD channelCount = (XDWORD)fmt.channels;
                                    XDWORD srcBytes = (XDWORD)pcmBytes;

                                    // Worst-case destination buffer: ~4x compressed size
                                    XDWORD destMax = srcBytes * 4 + 64;
                                    XBYTE *tmp = (XBYTE *)XNewPtr((int32_t)destMax);
                                    if (!tmp)
                                    {
                                        DLS_UnloadBank(bank);
                                        return MEMORY_ERR;
                                    }

                                    XDWORD outBytes = XExpandWavIma((XBYTE const *)pcm, srcBytesPerBlock, (void *)tmp,
                                                                    dstBitsPerSample, srcBytes, channelCount);
                                    if (outBytes == 0)
                                    {
                                        printf("DLS: IMA ADPCM decode failed; skipping wave.\n");
                                        XDisposePtr(tmp);
                                    }
                                    else
                                    {
                                        bank->waves = (DLS_Wave *)xrealloc_copy(bank->waves, bank->waveCount, bank->waveCount + 1, sizeof(DLS_Wave));
                                        if (!bank->waves)
                                        {
                                            XDisposePtr(tmp);
                                            DLS_UnloadBank(bank);
                                            return MEMORY_ERR;
                                        }
                                        DLS_Wave *w = &bank->waves[bank->waveCount++];
                                        w->sampleRate = fmt.samplesPerSec ? fmt.samplesPerSec : 22050;
                                        w->channels = fmt.channels;
                                        w->bitsPerSample = 16;
                                        w->pcmBytes = (uint32_t)outBytes;
                                        w->pcm = (unsigned char *)XNewPtr((int32_t)outBytes);
                                        if (!w->pcm)
                                        {
                                            XDisposePtr(tmp);
                                            DLS_UnloadBank(bank);
                                            return MEMORY_ERR;
                                        }
                                        XBlockMove(tmp, w->pcm, (int32_t)outBytes);
                                        XDisposePtr(tmp);

                                        uint32_t bytesPerFrame = (w->bitsPerSample / 8) * w->channels;
                                        w->frameCount = bytesPerFrame ? (w->pcmBytes / bytesPerFrame) : 0;
                                        if (hasLoop)
                                        {
                                            if (loopEnd > w->frameCount)
                                                loopEnd = w->frameCount;
                                            w->loopStart = loopStart;
                                            w->loopEnd = loopEnd;
                                        }
                                        else
                                        {
                                            w->loopStart = 0;
                                            w->loopEnd = 0;
                                        }
                                        w->unityNote = (unity >= 0 && unity <= 127) ? unity : 60;
                                        w->fineTuneCents = fine;
                                        // Compute relative offset of this 'wave' LIST from start of wvpl (for ptbl mapping)
                                        w->wvplOffset = (lpos - (cdat + 4));
                                    }
                                }
                                else
                                {
                                    // Unsupported encoding for now
                                    printf("DLS: Unsupported wave formatTag=0x%04x; skipping.\n", fmt.formatTag);
                                }
                            }
                        }
                        lpos = sdat + ((scsz + 1) & ~1U);
                    }
                }
                else if (ltype == FCC_LINS)
                {
                    // Instruments
                    printf("DLS: Found LINS list, parsing instruments...\n");
                    uint32_t ipos = cdat + 4;
                    while (ipos + 8 <= pos + 8 + csz)
                    {
                        uint32_t icid = rd32(ub + ipos);
                        uint32_t isz = rd32le(ub + ipos + 4);
                        uint32_t idat = ipos + 8;
                        if (icid == FCC_LIST)
                        {
                            uint32_t listType = (idat + 4 <= (uint32_t)fsize) ? rd32(ub + idat) : 0;
                            printf("DLS: Instrument LIST type: %c%c%c%c\n",
                                   (char)((listType >> 24) & 0xFF), (char)((listType >> 16) & 0xFF),
                                   (char)((listType >> 8) & 0xFF), (char)(listType & 0xFF));
                            if (listType == FOURCC('i', 'n', 's', ' '))
                            {
                                printf("DLS: Creating instrument %u\n", bank->instrumentCount);
                                // Start new instrument
                                bank->instruments = (DLS_Instrument *)xrealloc_copy(bank->instruments, bank->instrumentCount, bank->instrumentCount + 1, sizeof(DLS_Instrument));
                                if (!bank->instruments)
                                {
                                    DLS_UnloadBank(bank);
                                    return MEMORY_ERR;
                                }
                                DLS_Instrument *ins = &bank->instruments[bank->instrumentCount++];
                                memset(ins, 0, sizeof(*ins));
                                ins->bank = 0;
                                ins->program = 0;
                                strncpy(ins->name, "DLS", sizeof(ins->name) - 1);
                                // Instrument-level default articulation (applied to all regions unless overridden)
                                struct
                                {
                                    int32_t volEnvDelay, volEnvAttack, volEnvHold, volEnvDecay, volEnvSustain, volEnvRelease;
                                    int32_t lfoFreq, lfoDelay, lfoToPitch, lfoToVolume, lfoToFilterFc;
                                } insDefaultArt;
                                memset(&insDefaultArt, 0, sizeof(insDefaultArt));
                                int hasInsDefaultArt = 0;
                                uint32_t inpos = idat + 4;
                                while (inpos + 8 <= ipos + 8 + isz)
                                {
                                    uint32_t nid = rd32(ub + inpos);
                                    uint32_t nsz = rd32le(ub + inpos + 4);
                                    uint32_t nd = inpos + 8;
                                    printf("DLS: ins subchunk: nid=0x%08x (%c%c%c%c), size=%u\n", nid, (char)((nid >> 24) & 0xFF), (char)((nid >> 16) & 0xFF), (char)((nid >> 8) & 0xFF), (char)(nid & 0xFF), nsz);
                                    if (nid == FCC_LIST && nd + 4 <= (uint32_t)fsize)
                                    {
                                        uint32_t nl = rd32(ub + nd);
                                        printf("DLS: ins LIST type: 0x%08x (%c%c%c%c)\n", nl, (char)((nl >> 24) & 0xFF), (char)((nl >> 16) & 0xFF), (char)((nl >> 8) & 0xFF), (char)(nl & 0xFF));
                                        if (nl == FCC_LART || nl == FCC_LAR2)
                                        {
                                            // Instrument-level articulation list
                                            uint32_t ap = nd + 4;
                                            while (ap + 8 <= inpos + 8 + nsz)
                                            {
                                                uint32_t aid = rd32(ub + ap);
                                                uint32_t asz = rd32le(ub + ap + 4);
                                                uint32_t ad = ap + 8;
                                                if ((aid == FOURCC('a', 'r', 't', '1') || aid == FOURCC('a', 'r', 't', '2')) && asz >= 8)
                                                {
                                                    // Parse into a temp region to reuse parser
                                                    DLS_Region tmpRg;
                                                    memset(&tmpRg, 0, sizeof(tmpRg));
                                                    PV_ParseDLSArticulation(ub + ad, asz, &tmpRg);
                                                    // Copy out as instrument defaults
                                                    insDefaultArt.volEnvDelay = tmpRg.articulation.volEnvDelay;
                                                    insDefaultArt.volEnvAttack = tmpRg.articulation.volEnvAttack;
                                                    insDefaultArt.volEnvHold = tmpRg.articulation.volEnvHold;
                                                    insDefaultArt.volEnvDecay = tmpRg.articulation.volEnvDecay;
                                                    insDefaultArt.volEnvSustain = tmpRg.articulation.volEnvSustain;
                                                    insDefaultArt.volEnvRelease = tmpRg.articulation.volEnvRelease;
                                                    insDefaultArt.lfoFreq = tmpRg.articulation.lfoFreq;
                                                    insDefaultArt.lfoDelay = tmpRg.articulation.lfoDelay;
                                                    insDefaultArt.lfoToPitch = tmpRg.articulation.lfoToPitch;
                                                    insDefaultArt.lfoToVolume = tmpRg.articulation.lfoToVolume;
                                                    insDefaultArt.lfoToFilterFc = tmpRg.articulation.lfoToFilterFc;
                                                    hasInsDefaultArt = 1;
                                                    printf("DLS: Captured instrument-level ART defaults.\n");
                                                }
                                                ap = ad + ((asz + 1) & ~1U);
                                            }
                                        }
                                        if (nl == FCC_LRGN)
                                        {
                                            printf("DLS: Found lrgn\n");
                                            // one or more regions inside
                                            uint32_t rpos = nd + 4;
                                            while (rpos + 8 <= inpos + 8 + nsz)
                                            {
                                                uint32_t rid = rd32(ub + rpos);
                                                uint32_t rsz = rd32le(ub + rpos + 4);
                                                uint32_t rd = rpos + 8;
                                                printf("DLS: lrgn chunk: rid=0x%08x (%c%c%c%c), size=%u\n", rid, (char)((rid >> 24) & 0xFF), (char)((rid >> 16) & 0xFF), (char)((rid >> 8) & 0xFF), (char)(rid & 0xFF), rsz);
                                                if (rid == FCC_LIST && rd + 4 <= (uint32_t)fsize && (rd32(ub + rd) == FCC_RGN || rd32(ub + rd) == FCC_RGN2))
                                                {
                                                    printf("DLS: Found rgn\n");
                                                    // region LIST
                                                    uint8_t keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
                                                    int32_t waveIndex = -1;
                                                    int16_t rUnity = -1;
                                                    int16_t rFine = 0;
                                                    uint32_t rsub = rd + 4;
                                                    while (rsub + 8 <= rpos + 8 + rsz)
                                                    {
                                                        uint32_t sid = rd32(ub + rsub);
                                                        uint32_t ssz = rd32le(ub + rsub + 4);
                                                        uint32_t sd = rsub + 8;
                                                        printf("DLS: region subchunk: sid=0x%08x (%c%c%c%c), size=%u\n", sid, (char)((sid >> 24) & 0xFF), (char)((sid >> 16) & 0xFF), (char)((sid >> 8) & 0xFF), (char)(sid & 0xFF), ssz);
                                                        // Minimal set: RGNH (range), WLNK (wave link), WSMP (tuning)
                                                        if (sid == FOURCC('r', 'g', 'n', 'h') && ssz >= 12)
                                                        {
                                                            keyLo = (uint8_t)rd16le(ub + sd + 0);
                                                            keyHi = (uint8_t)rd16le(ub + sd + 2);
                                                            velLo = (uint8_t)rd16le(ub + sd + 4);
                                                            velHi = (uint8_t)rd16le(ub + sd + 6);
                                                            printf("DLS: RGNH parsed - key range %u-%u, vel range %u-%u\n", keyLo, keyHi, velLo, velHi);
                                                        }
                                                        else if (sid == FOURCC('w', 'l', 'n', 'k') && ssz >= 12)
                                                        {
                                                            // skip options/phaseGroup
                                                            uint32_t tableIndex = rd32le(ub + sd + 8);
                                                            waveIndex = (int32_t)tableIndex; // assume 1:1 wav index
                                                            printf("DLS: Found wlnk, tableIndex=%u\n", tableIndex);
                                                        }
                                                        else if (sid == FCC_WSMP && ssz >= 20)
                                                        {
                                                            rUnity = (int16_t)rd16le(ub + sd + 4);
                                                            rFine = (int16_t)(int16_t)rd16le(ub + sd + 6);
                                                        }
                                                        rsub = sd + ((ssz + 1) & ~1U);
                                                    }
                                                    if (waveIndex >= 0)
                                                    {
                                                        // Keep region - we'll validate wave indices after all chunks are parsed
                                                        printf("DLS: Adding region with ptbl index=%d, key range %u-%u\n", waveIndex, keyLo, keyHi);
                                                        ins->regions = (DLS_Region *)xrealloc_copy(ins->regions, ins->regionCount, ins->regionCount + 1, sizeof(DLS_Region));
                                                        if (!ins->regions)
                                                        {
                                                            DLS_UnloadBank(bank);
                                                            return MEMORY_ERR;
                                                        }
                                                        DLS_Region *rg = &ins->regions[ins->regionCount++];
                                                        rg->keyLow = keyLo;
                                                        rg->keyHigh = keyHi;
                                                        rg->velLow = velLo;
                                                        rg->velHigh = velHi;
                                                        rg->waveIndex = (uint32_t)waveIndex;
                                                        rg->unityNote = rUnity;
                                                        rg->fineTuneCents = rFine;
                                                        rg->artInitialized = 0;

                                                        // Initialize articulation to default values
                                                        if (hasInsDefaultArt)
                                                        {
                                                            // Apply instrument defaults as baseline
                                                            rg->articulation.volEnvDelay = insDefaultArt.volEnvDelay;
                                                            rg->articulation.volEnvAttack = insDefaultArt.volEnvAttack;
                                                            rg->articulation.volEnvHold = insDefaultArt.volEnvHold;
                                                            rg->articulation.volEnvDecay = insDefaultArt.volEnvDecay;
                                                            rg->articulation.volEnvSustain = insDefaultArt.volEnvSustain;
                                                            rg->articulation.volEnvRelease = insDefaultArt.volEnvRelease;
                                                            rg->articulation.lfoFreq = insDefaultArt.lfoFreq;
                                                            rg->articulation.lfoDelay = insDefaultArt.lfoDelay;
                                                            rg->articulation.lfoToPitch = insDefaultArt.lfoToPitch;
                                                            rg->articulation.lfoToVolume = insDefaultArt.lfoToVolume;
                                                            rg->articulation.lfoToFilterFc = insDefaultArt.lfoToFilterFc;
                                                            rg->artInitialized = 1;
                                                            printf("DLS: Region baseline from instrument ART applied.\n");
                                                        }
                                                        else
                                                        {
                                                            rg->articulation.volEnvDelay = 0;
                                                            rg->articulation.volEnvAttack = 1000; // 1ms default
                                                            rg->articulation.volEnvHold = 0;
                                                            rg->articulation.volEnvDecay = 100000;   // 100ms default
                                                            rg->articulation.volEnvSustain = 700;    // 70% level
                                                            rg->articulation.volEnvRelease = 500000; // 500ms default
                                                            rg->articulation.lfoFreq = 0;            // No LFO by default
                                                            rg->articulation.lfoDelay = 0;
                                                            rg->articulation.lfoToPitch = 0;
                                                            rg->articulation.lfoToVolume = 0;
                                                            rg->articulation.lfoToFilterFc = 0;
                                                        }

                                                        // Parse articulation if present in the region
                                                        uint32_t art_rsub = rd + 4;
                                                        while (art_rsub + 8 <= rpos + 8 + rsz)
                                                        {
                                                            uint32_t art_sid = rd32(ub + art_rsub);
                                                            uint32_t art_ssz = rd32le(ub + art_rsub + 4);
                                                            uint32_t art_sd = art_rsub + 8;
                                                            if ((art_sid == FOURCC('a', 'r', 't', '1') || art_sid == FOURCC('a', 'r', 't', '2')) && art_ssz >= 8)
                                                            {
                                                                PV_ParseDLSArticulation(ub + art_sd, art_ssz, rg);
                                                            }
                                                            else if (art_sid == FCC_LIST && art_sd + 4 <= (uint32_t)fsize && (rd32(ub + art_sd) == FCC_LART || rd32(ub + art_sd) == FCC_LAR2))
                                                            {
                                                                // Descend into LIST 'lart' to find art1/art2
                                                                uint32_t lpos2 = art_sd + 4;
                                                                while (lpos2 + 8 <= art_sd + ((art_ssz + 1) & ~1U))
                                                                {
                                                                    uint32_t lid = rd32(ub + lpos2);
                                                                    uint32_t lsz = rd32le(ub + lpos2 + 4);
                                                                    uint32_t ldat = lpos2 + 8;
                                                                    if ((lid == FOURCC('a', 'r', 't', '1') || lid == FOURCC('a', 'r', 't', '2')) && lsz >= 8)
                                                                    {
                                                                        PV_ParseDLSArticulation(ub + ldat, lsz, rg);
                                                                    }
                                                                    lpos2 = ldat + ((lsz + 1) & ~1U);
                                                                }
                                                            }
                                                            art_rsub = art_sd + ((art_ssz + 1) & ~1U);
                                                        }
                                                    }
                                                    else
                                                    {
                                                        printf("DLS: Skipping region, invalid waveIndex=%d\n", waveIndex);
                                                    }
                                                }
                                                rpos = rd + ((rsz + 1) & ~1U);
                                            }
                                        }
                                        else if (nl == FCC_INFO)
                                        {
                                            // optional name chunks 'INAM'
                                            uint32_t ip = nd + 4;
                                            while (ip + 8 <= inpos + 8 + nsz)
                                            {
                                                uint32_t iid = rd32(ub + ip);
                                                uint32_t isz2 = rd32le(ub + ip + 4);
                                                uint32_t id2 = ip + 8;
                                                if (iid == FOURCC('I', 'N', 'A', 'M'))
                                                {
                                                    uint32_t cpy = isz2 < sizeof(ins->name) - 1 ? isz2 : sizeof(ins->name) - 1;
                                                    memcpy(ins->name, ub + id2, cpy);
                                                    ins->name[cpy] = 0;
                                                }
                                                ip = id2 + ((isz2 + 1) & ~1U);
                                            }
                                        }
                                    }
                                    else if ((nid == FCC_ART1 || nid == FCC_ART2) && nsz >= 8)
                                    {
                                        // Instrument-level art chunk directly under instrument
                                        DLS_Region tmpRg;
                                        memset(&tmpRg, 0, sizeof(tmpRg));
                                        PV_ParseDLSArticulation(ub + nd, nsz, &tmpRg);
                                        insDefaultArt.volEnvDelay = tmpRg.articulation.volEnvDelay;
                                        insDefaultArt.volEnvAttack = tmpRg.articulation.volEnvAttack;
                                        insDefaultArt.volEnvHold = tmpRg.articulation.volEnvHold;
                                        insDefaultArt.volEnvDecay = tmpRg.articulation.volEnvDecay;
                                        insDefaultArt.volEnvSustain = tmpRg.articulation.volEnvSustain;
                                        insDefaultArt.volEnvRelease = tmpRg.articulation.volEnvRelease;
                                        insDefaultArt.lfoFreq = tmpRg.articulation.lfoFreq;
                                        insDefaultArt.lfoDelay = tmpRg.articulation.lfoDelay;
                                        insDefaultArt.lfoToPitch = tmpRg.articulation.lfoToPitch;
                                        insDefaultArt.lfoToVolume = tmpRg.articulation.lfoToVolume;
                                        insDefaultArt.lfoToFilterFc = tmpRg.articulation.lfoToFilterFc;
                                        hasInsDefaultArt = 1;
                                        printf("DLS: Captured instrument-level ART defaults (direct).\n");
                                    }
                                    else if (nid == FOURCC('i', 'n', 's', 'h') && nsz >= 12)
                                    {
                                        // instrument header: bank MSB/LSB and program  
                                        // DLS format: regions(4), bank(4), program(4)
                                        // Bank field: bit 31 = percussion flag, bits 0-13 = bank number
                                        uint32_t regions = rd32le(ub + nd + 0);
                                        uint32_t bankField = rd32le(ub + nd + 4);
                                        uint32_t program = rd32le(ub + nd + 8);
                                        
                                        // Extract actual bank number and percussion flag
                                        XBOOL isPercussion = (bankField & 0x80000000) != 0;
                                        uint16_t actualBank;
                                        
                                        if (isPercussion) {
                                            // For percussion instruments, use bank 120 (standard MIDI percussion)
                                            actualBank = 120;
                                        } else {
                                            // For melodic instruments, bank number appears to be in bits 8-15 (MSB)
                                            // This gives us: 0x00000100 = bank 1, 0x00000200 = bank 2, etc.
                                            actualBank = (uint16_t)((bankField >> 8) & 0xFF);
                                        }
                                        
                                        printf("DLS Debug: insh chunk - regions=%u, bankField=0x%08x, program=%u, isPerc=%d, actualBank=%u\n",
                                               regions, bankField, program, isPercussion ? 1 : 0, actualBank);
                                        
                                        ins->bank = actualBank;
                                        ins->program = (uint16_t)program;
                                    }
                                    inpos = nd + ((nsz + 1) & ~1U);
                                }
                                // After processing instrument subchunks, if we captured instrument-level ART
                                // apply it to any regions that didn't get region-level articulation.
                                if (hasInsDefaultArt && ins->regions && ins->regionCount > 0)
                                {
                                    for (uint32_t ri = 0; ri < ins->regionCount; ++ri)
                                    {
                                        DLS_Region *rg = &ins->regions[ri];
                                        if (rg->artInitialized == 0)
                                        {
                                            rg->articulation.volEnvDelay = insDefaultArt.volEnvDelay;
                                            rg->articulation.volEnvAttack = insDefaultArt.volEnvAttack;
                                            rg->articulation.volEnvHold = insDefaultArt.volEnvHold;
                                            rg->articulation.volEnvDecay = insDefaultArt.volEnvDecay;
                                            rg->articulation.volEnvSustain = insDefaultArt.volEnvSustain;
                                            rg->articulation.volEnvRelease = insDefaultArt.volEnvRelease;
                                            rg->articulation.lfoFreq = insDefaultArt.lfoFreq;
                                            rg->articulation.lfoDelay = insDefaultArt.lfoDelay;
                                            rg->articulation.lfoToPitch = insDefaultArt.lfoToPitch;
                                            rg->articulation.lfoToVolume = insDefaultArt.lfoToVolume;
                                            rg->articulation.lfoToFilterFc = insDefaultArt.lfoToFilterFc;
                                            rg->artInitialized = 1;
                                            printf("DLS: Applied ins default ART to region %u (keys %u-%u)\n", ri, rg->keyLow, rg->keyHigh);
                                        }
                                    }
                                }
                                printf("DLS Debug: Created instrument '%s' bank=%u program=%u with %u regions\n", 
                                       ins->name, ins->bank, ins->program, ins->regionCount);
                            }
                        }
                        ipos = idat + ((isz + 1) & ~1U);
                    }
                }
            }
        }
        else if (cid == FCC_PTBL)
        {
            // Wave pool table: maps wave indices to offsets within wvpl
            // Structure: cbSize (4), cCues (4), then array of DWORD offsets
            if (csz >= 8)
            {
                uint32_t cbSize = rd32le(ub + cdat + 0);
                uint32_t cCues = rd32le(ub + cdat + 4);
                printf("DLS: ptbl cbSize=%u cCues=%u\n", cbSize, cCues);
                if (cCues > 0 && cdat + 8 + cCues * 4 <= (uint32_t)fsize)
                {
                    bank->ptblCount = cCues;
                    bank->ptblOffsets = (uint32_t *)XNewPtr((int32_t)(cCues * sizeof(uint32_t)));
                    bank->ptblToWave = (int32_t *)XNewPtr((int32_t)(cCues * sizeof(int32_t)));
                    if (!bank->ptblOffsets || !bank->ptblToWave)
                    {
                        DLS_UnloadBank(bank);
                        return MEMORY_ERR;
                    }
                    for (uint32_t i = 0; i < cCues; i++)
                    {
                        bank->ptblOffsets[i] = rd32le(ub + cdat + 8 + i * 4);
                        bank->ptblToWave[i] = -1;
                        // Debug
                        // printf("DLS: ptbl[%u]=%u\n", i, bank->ptblOffsets[i]);
                    }
                }
            }
        }
        else if (cid == FCC_PGAL)
        {
            // Mobile DLS instrument aliasing chunk
            if (csz >= 12)
            {
                PV_ParseMobileDLSAliasing(ub + cdat, csz, bank);
            }
        }
        pos = cdat + ((csz + 1) & ~1U);
    }

    // Resolve ptbl offsets to wave indices
    if (bank->ptblOffsets && bank->waveCount > 0)
    {
        for (uint32_t pi = 0; pi < bank->ptblCount; ++pi)
        {
            uint32_t off = bank->ptblOffsets[pi];
            // Find wave whose wvplOffset matches this ptbl offset
            for (uint32_t wi = 0; wi < bank->waveCount; ++wi)
            {
                if (bank->waves[wi].wvplOffset == off)
                {
                    bank->ptblToWave[pi] = (int32_t)wi;
                    break;
                }
            }
            // printf("DLS: ptblToWave[%u]=%d\n", pi, bank->ptblToWave[pi]);
        }
    }

    // Post-validate region wave indices after ptbl resolution
    if (bank->instrumentCount > 0)
    {
        for (uint32_t ii = 0; ii < bank->instrumentCount; ++ii)
        {
            DLS_Instrument *ins = &bank->instruments[ii];
            uint32_t validRegions = 0;
            for (uint32_t ri = 0; ri < ins->regionCount; ++ri)
            {
                DLS_Region *rg = &ins->regions[ri];
                uint32_t idx = rg->waveIndex;

                // Try ptbl mapping first
                if (bank->ptblToWave && idx < bank->ptblCount)
                {
                    int32_t wi = bank->ptblToWave[idx];
                    if (wi >= 0 && wi < (int32_t)bank->waveCount)
                    {
                        rg->waveIndex = (uint32_t)wi;
                        validRegions++;
                    }
                    else
                    {
                        printf("DLS: ptbl[%u] unresolved or invalid wave %d (waveCount=%u)\n", idx, wi, bank->waveCount);
                    }
                }
                // Direct wave index fallback
                else if (idx < bank->waveCount)
                {
                    printf("DLS: Using direct wave index %u\n", idx);
                    validRegions++;
                }
                else
                {
                    printf("DLS: Invalid wave index %u (waveCount=%u, ptblCount=%u)\n", idx, bank->waveCount, bank->ptblCount);
                }
            }
            printf("DLS: Instrument '%s' has %u valid regions out of %u total\n", ins->name, validRegions, ins->regionCount);
        }
    }

    printf("DLS: Final bank - waves=%u, instruments=%u\n", bank->waveCount, bank->instrumentCount);
    *ppBank = bank;
    return NO_ERR;
}

void DLS_UnloadBank(DLS_Bank *pBank)
{
    if (!pBank)
        return;
    if (pBank->waves)
    {
        for (uint32_t i = 0; i < pBank->waveCount; i++)
        {
            if (pBank->waves[i].pcm)
                XDisposePtr(pBank->waves[i].pcm);
        }
        XDisposePtr(pBank->waves);
    }
    if (pBank->ptblOffsets)
        XDisposePtr(pBank->ptblOffsets);
    if (pBank->ptblToWave)
        XDisposePtr(pBank->ptblToWave);
    if (pBank->instruments)
    {
        for (uint32_t j = 0; j < pBank->instrumentCount; j++)
        {
            if (pBank->instruments[j].regions)
                XDisposePtr(pBank->instruments[j].regions);
        }
        XDisposePtr(pBank->instruments);
    }
    // Free Mobile DLS aliasing data
    if (pBank->instrumentAliases)
        XDisposePtr(pBank->instrumentAliases);
    if (pBank->ownedMemory)
        XDisposePtr(pBank->ownedMemory);
    XDisposePtr(pBank);
}

OPErr DLS_InitBankManager(void)
{
    g_dlsManager.bankList = NULL;
    g_dlsManager.bankCount = 0;
    return NO_ERR;
}

void DLS_ShutdownBankManager(void)
{
    DLS_BankNode *n = g_dlsManager.bankList;
    while (n)
    {
        DLS_BankNode *next = n->next;
        if (n->filePath)
            XDisposePtr(n->filePath);
        if (n->bank)
            DLS_UnloadBank(n->bank);
        XDisposePtr(n);
        n = next;
    }
    g_dlsManager.bankList = NULL;
    g_dlsManager.bankCount = 0;
}

OPErr DLS_AddBankToManager(DLS_Bank *bank, const char *filePath)
{
    if (!bank)
        return PARAM_ERR;
    DLS_BankNode *node = (DLS_BankNode *)XNewPtr(sizeof(DLS_BankNode));
    if (!node)
        return MEMORY_ERR;
    node->bank = bank;
    if (filePath)
    {
        size_t l = strlen(filePath);
        node->filePath = (char *)XNewPtr((int32_t)(l + 1));
        if (node->filePath)
            memcpy(node->filePath, filePath, l + 1);
    }
    else
        node->filePath = NULL;
    node->next = g_dlsManager.bankList;
    g_dlsManager.bankList = node;
    g_dlsManager.bankCount++;
    return NO_ERR;
}

void DLS_RemoveBankFromManager(DLS_Bank *bank)
{
    DLS_BankNode **pp = &g_dlsManager.bankList;
    while (*pp)
    {
        if ((*pp)->bank == bank)
        {
            DLS_BankNode *dead = *pp;
            *pp = dead->next;
            if (dead->filePath)
                XDisposePtr(dead->filePath);
            if (dead->bank)
                DLS_UnloadBank(dead->bank);
            XDisposePtr(dead);
            g_dlsManager.bankCount--;
            return;
        }
        pp = &(*pp)->next;
    }
}

DLS_Bank *DLS_FindBankByPath(const char *filePath)
{
    if (!filePath)
        return NULL;
    DLS_BankNode *n = g_dlsManager.bankList;
    while (n)
    {
        if (n->filePath && XStrCmp(n->filePath, filePath) == 0)
            return n->bank;
        n = n->next;
    }
    return NULL;
}

// Create a note-specific instrument from a DLS instrument (for percussion)
GM_Instrument *DLS_CreateInstrumentFromNote(DLS_Bank *bank, uint16_t bankNum, uint16_t programNum, uint16_t note, OPErr *pErr)
{
    GM_Instrument *pInstrument = NULL;
    OPErr err = BAD_INSTRUMENT;
    uint32_t instrumentIndex;
    DLS_Instrument *instrument = NULL;

    if (!bank || !pErr)
    {
        if (pErr)
            *pErr = PARAM_ERR;
        return NULL;
    }

    // Apply Mobile DLS aliasing if present
    uint16_t originalBankNum = bankNum;
    uint16_t originalProgramNum = programNum;
    uint16_t originalNote = note;
    
    // Apply drum note aliasing for percussion banks
    if (bankNum == 120 && bank->hasDrumAliasing && note < 128)
    {
        uint8_t aliasedNote = bank->drumAliasTable[note];
        if (aliasedNote != note)
        {
            printf("DLS: Drum note aliasing: %u -> %u\n", note, aliasedNote);
            note = aliasedNote;
        }
    }
    
    // Apply melodic instrument aliasing
    for (uint32_t i = 0; i < bank->instrumentAliasCount; i++)
    {
        if (bank->instrumentAliases[i].srcBank == bankNum &&
            bank->instrumentAliases[i].srcProgram == programNum)
        {
            printf("DLS: Instrument aliasing: bank %u prog %u -> bank %u prog %u\n",
                   bankNum, programNum, 
                   bank->instrumentAliases[i].dstBank, 
                   bank->instrumentAliases[i].dstProgram);
            bankNum = bank->instrumentAliases[i].dstBank;
            programNum = bank->instrumentAliases[i].dstProgram;
            break;
        }
    }

    // Find the instrument
    for (instrumentIndex = 0; instrumentIndex < bank->instrumentCount; instrumentIndex++)
    {
        if (bank->instruments[instrumentIndex].bank == bankNum &&
            bank->instruments[instrumentIndex].program == programNum)
        {
            instrument = &bank->instruments[instrumentIndex];
            break;
        }
    }

    if (!instrument)
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

    BAE_PRINTF("DLS Debug: Creating instrument for note %d from instrument bank=%d, program=%d\n",
               note, bankNum, programNum);

    // Initialize basic instrument parameters
    pInstrument->doKeymapSplit = FALSE;
    pInstrument->extendedFormat = FALSE;
    pInstrument->notPolyphonic = FALSE;
    pInstrument->useSampleRate = TRUE;
    pInstrument->disableSndLooping = (bankNum == 120) ? TRUE : FALSE; // Disable looping for percussion (DLS bank 120)
    pInstrument->playAtSampledFreq = FALSE; // Always allow pitching - percussion should be pitched too
    pInstrument->sampleAndHold = FALSE;
    pInstrument->usageReferenceCount = 0;
    pInstrument->panPlacement = 0; // Center pan

#if REVERB_USED != REVERB_DISABLED
    pInstrument->avoidReverb = FALSE;
#endif

    // Find the best matching region for this note
    DLS_Region *bestRegion = NULL;
    uint32_t bestDistance = 0xFFFFFFFFU;
    int bestScore = 0x7FFFFFFF; // lower is better

    for (uint32_t ri = 0; ri < instrument->regionCount; ri++)
    {
        DLS_Region *rg = &instrument->regions[ri];
        if (rg->waveIndex >= bank->waveCount)
            continue;

        // Check if note is in range
        if (note >= rg->keyLow && note <= rg->keyHigh)
        {
            // Exact match is best
            bestRegion = rg;
            bestDistance = 0;
            bestScore = 0;
            break;
        }
        else
        {
            // Calculate distance to range
            uint32_t distance;
            if (note < rg->keyLow)
                distance = (uint32_t)(rg->keyLow - note);
            else if (note > rg->keyHigh)
                distance = (uint32_t)(note - rg->keyHigh);
            else
                distance = 0;

            if (distance < bestDistance)
            {
                bestRegion = rg;
                bestDistance = distance;
            }
        }
    }

    if (!bestRegion || bestRegion->waveIndex >= bank->waveCount)
    {
        BAE_PRINTF("DLS Debug: No suitable region found for note %d\n", note);
        XDisposePtr(pInstrument);
        *pErr = BAD_INSTRUMENT;
        return NULL;
    }

    // Use the best region to create the instrument
    DLS_Region *rg = bestRegion;
    const DLS_Wave *w = &bank->waves[rg->waveIndex];

    printf("DLS Debug: Using region art -> delay=%d, attack=%d, hold=%d, decay=%d, release=%d, sustain=%d for note %u (bank=%u prog=%u)\n",
           rg->articulation.volEnvDelay, rg->articulation.volEnvAttack, rg->articulation.volEnvHold,
           rg->articulation.volEnvDecay, rg->articulation.volEnvRelease, rg->articulation.volEnvSustain, (unsigned)note, bankNum, programNum);

    // Copy original PCM data first
    SBYTE *originalWaveform = (SBYTE *)XNewPtr((int32_t)w->pcmBytes);
    if (!originalWaveform)
    {
        XDisposePtr(pInstrument);
        *pErr = MEMORY_ERR;
        return NULL;
    }
    XBlockMove(w->pcm, originalWaveform, (int32_t)w->pcmBytes);

    // DLS 8-bit PCM is unsigned; engine expects signed. Convert in-place.
    if (w->bitsPerSample == 8 && originalWaveform && w->pcmBytes > 0)
    {
        XPhase8BitWaveform((XBYTE *)originalWaveform, w->pcmBytes);
    }

    // Calculate original frame count
    uint32_t originalFrames = w->frameCount;
    uint32_t bytesPerFrame = (w->bitsPerSample / 8) * w->channels;
    uint32_t maxFrames = bytesPerFrame ? (w->pcmBytes / bytesPerFrame) : 0;
    if (originalFrames > maxFrames)
    {
        BAE_PRINTF("DLS Warn: frameCount>%u clamping to %u (bytes=%u, bpf=%u)\n",
                   (unsigned)originalFrames, (unsigned)maxFrames, (unsigned)w->pcmBytes, (unsigned)bytesPerFrame);
        originalFrames = maxFrames;
    }

    // Don't resample - let the engine handle pitch via baseMidiPitch and fine tuning
    SBYTE *finalWaveform = originalWaveform;
    uint32_t finalFrames = originalFrames;

    // Set final waveform data
    pInstrument->u.w.theWaveform = finalWaveform;
    pInstrument->u.w.bitSize = (INT16)w->bitsPerSample;
    pInstrument->u.w.channels = (INT16)w->channels;
    pInstrument->u.w.waveSize = (int32_t)(finalFrames * bytesPerFrame);
    pInstrument->u.w.waveFrames = (int32_t)finalFrames;
    
    // Set loop points (no scaling needed since we didn't resample)
    INT32 lStart = (INT32)w->loopStart;
    INT32 lEnd = (INT32)w->loopEnd;
    if (lStart < 0)
        lStart = 0;
    if (lEnd < 0)
        lEnd = 0;
    if (lEnd > (INT32)finalFrames)
        lEnd = (INT32)finalFrames;
    if (lStart >= lEnd)
    {
        lStart = 0;
        lEnd = 0;
    }
    pInstrument->u.w.startLoop = lStart;
    pInstrument->u.w.endLoop = lEnd;

    // Base pitch: region override, else wave unity
    int base = (rg->unityNote >= 0 ? rg->unityNote : w->unityNote);
    if (base < 0)
        base = 60;
    if (base > 127)
        base = 127;
    
    // For percussion, override base pitch to the triggering note for correct pitch
    if (bankNum == 120)
    {
        base = note;  // Force percussion to play at the correct pitch
    }
    
    pInstrument->u.w.baseMidiPitch = (unsigned char)base;

    // Sample rate: store the original sample rate in 16.16 fixed format
    // Fine-tuning should be handled by the engine, not by resampling
    pInstrument->u.w.sampledRate = (XSDWORD)(w->sampleRate << 16);

    // Apply DLS articulation (ADSR envelope)
    DLS_ParseArticulation(&rg->articulation, pInstrument);

    // For percussion, additional setup
    if (bankNum == 120)
    {
        // Modest percussion volume boost - only if current level seems low
        if (pInstrument->volumeADSRRecord.sustainingDecayLevel <= XFIXED_1)
        {
            pInstrument->volumeADSRRecord.sustainingDecayLevel = (XFIXED_1 * 3) / 2; // 1.5x volume boost
        }
        
        printf("DLS Debug: PERCUSSION instrument for note %d - looping=%s, playAtSampledFreq=%s\n",
               note,
               pInstrument->disableSndLooping ? "DISABLED" : "enabled",
               pInstrument->playAtSampledFreq ? "YES" : "no");
        printf("DLS Debug: PERCUSSION sample - start=%d, end=%d, frames=%u\n",
               pInstrument->u.w.startLoop, pInstrument->u.w.endLoop,
               (unsigned)pInstrument->u.w.waveFrames);
        // Force disable looping for percussion to avoid pops/clicks
        pInstrument->u.w.startLoop = 0;
        pInstrument->u.w.endLoop = 0;
    }

    BAE_PRINTF("DLS Debug: Created note-specific instrument - note=%d, rootKey=%d, frames=%u\n",
               note, base, (unsigned)pInstrument->u.w.waveFrames);

    *pErr = NO_ERR;
    return pInstrument;
}

// Build a GM_Instrument from a DLS instrument
GM_Instrument *DLS_BuildInstrument(DLS_Bank *bank, DLS_Instrument *ins, OPErr *pErr)
{
    if (!bank || !ins || !pErr)
        return NULL;
    *pErr = NO_ERR;

    printf("DLS Debug: DLS_BuildInstrument called for '%s' with %u regions\n", ins->name, ins->regionCount);

    if (ins->regionCount == 0)
    {
        printf("DLS Debug: No regions in instrument!\n");
        *pErr = BAD_INSTRUMENT;
        return NULL;
    }

    GM_Instrument *pI = NULL;
    if (ins->regionCount == 1)
    {
        const DLS_Region *rg = &ins->regions[0];
        if (rg->waveIndex >= bank->waveCount)
        {
            printf("DLS Debug: Single region has invalid waveIndex %u >= %u waves\n", rg->waveIndex, bank->waveCount);
            *pErr = BAD_INSTRUMENT;
            return NULL;
        }
        const DLS_Wave *w = &bank->waves[rg->waveIndex];
        printf("DLS Debug: Single region using wave %u: %u frames, %u Hz, %u-bit, %u channels\n",
               rg->waveIndex, w->frameCount, w->sampleRate, w->bitsPerSample, w->channels);

        // Create a sample cache entry from raw PCM in memory via GM_ReadFileIntoMemory style? We already have PCM.
        // Use XSndHeader3 builder like SF2 path expects: easiest is to synthesize an XSndHeader3 block and load via PV_CreateInstrumentFromResource.
        // But here we can construct GM_Instrument directly using sample cache API by creating a virtual sample entry. For minimalism, reuse PV_CreateInstrumentFromResource requires a SND resource.
        // Simpler: allocate GM_Instrument and fill wavefields, but we need the sample to be in cache; mimic PV_CreateInstrumentFromResource path by creating a temporary sample in cache is non-trivial.
        // Minimal approach: Use GM_ConvertRawPCMToWaveform helper doesn't exist. So fallback: create an in-memory XSndHeader3 blob.

        // Build XSndHeader3 PCM block
        // Header is parsed by cache builder when passed as external SND? Here, we'll use GM_CreateWaveformFromMemory equivalent doesn't exist. So feed through XGetResource APIs isn't available.
        // Given complexity, choose path identical to SF2 CreateWaveformFromSample is internal. For v0, approximate by creating a basic GM_Instrument with references to a newly allocated sample in cache via GMCache_BuildSampleCacheEntryFromPCM (not present).

        // Fallback very simple: duplicate a minimal one-shot instrument with no cache participation by copying PCM to GM_Instrument.u.w.theWaveform and setting frames. Engine expects cache. We'll still try; on failure return BAD_INSTRUMENT.

        pI = (GM_Instrument *)XNewPtr(sizeof(GM_Instrument));
        if (!pI)
        {
            *pErr = MEMORY_ERR;
            return NULL;
        }
        memset(pI, 0, sizeof(GM_Instrument));
        pI->doKeymapSplit = FALSE;
        // Match SF2 defaults for playback behavior
        pI->extendedFormat = FALSE;
        pI->notPolyphonic = FALSE;
        pI->useSampleRate = TRUE;
        pI->disableSndLooping = FALSE;
        pI->playAtSampledFreq = FALSE;
        pI->sampleAndHold = FALSE;
        pI->panPlacement = 0;
        pI->u.w.theWaveform = (SBYTE *)XNewPtr((int32_t)w->pcmBytes);
        if (!pI->u.w.theWaveform)
        {
            XDisposePtr(pI);
            *pErr = MEMORY_ERR;
            return NULL;
        }
        XBlockMove(w->pcm, pI->u.w.theWaveform, (int32_t)w->pcmBytes);
        pI->u.w.bitSize = (INT16)w->bitsPerSample;
        pI->u.w.channels = (INT16)w->channels;
        pI->u.w.waveSize = (int32_t)w->pcmBytes;
        printf("DLS Debug: Sample details - %u-bit, %u channels, %u bytes, %u frames\n",
               w->bitsPerSample, w->channels, w->pcmBytes, w->frameCount);
        printf("DLS Debug: Loop points - start=%u, end=%u\n", w->loopStart, w->loopEnd);

        if (w->bitsPerSample == 8 && pI->u.w.theWaveform && pI->u.w.waveSize > 0)
        {
            XPhase8BitWaveform((XBYTE *)pI->u.w.theWaveform, pI->u.w.waveSize);
            printf("DLS Debug: Applied 8-bit phase conversion\n");
        }
        {
            uint32_t bpf = (w->bitsPerSample / 8) * w->channels;
            uint32_t maxF = bpf ? (w->pcmBytes / bpf) : 0;
            if (w->frameCount > maxF)
            {
                BAE_PRINTF("DLS Warn: frameCount>%u clamping to %u (bytes=%u, bpf=%u)\n", (unsigned)w->frameCount, (unsigned)maxF, (unsigned)w->pcmBytes, (unsigned)bpf);
                pI->u.w.waveFrames = (int32_t)maxF;
                pI->u.w.waveSize = (int32_t)(maxF * bpf);
            }
            else
            {
                pI->u.w.waveFrames = (int32_t)w->frameCount;
            }
        }
        // Clamp and validate loop points
        INT32 lStart = (INT32)w->loopStart;
        INT32 lEnd = (INT32)w->loopEnd;
        if (lStart < 0)
            lStart = 0;
        if (lEnd < 0)
            lEnd = 0;
        if (lEnd > (INT32)w->frameCount)
            lEnd = (INT32)w->frameCount;
        if (lStart >= lEnd)
        {
            lStart = 0;
            lEnd = 0;
        }
        pI->u.w.startLoop = lStart;
        pI->u.w.endLoop = lEnd;
        printf("DLS Debug: Final loop points - start=%d, end=%d (frames=%d)\n",
               lStart, lEnd, pI->u.w.waveFrames);
        // Base pitch: region override, else wave unity
        int base = (rg->unityNote >= 0 ? rg->unityNote : w->unityNote);
        if (base < 0)
            base = 60;
        if (base > 127)
            base = 127;
        pI->u.w.baseMidiPitch = (unsigned char)base;
        // Sample rate with fine tune
        {
            // Store the original sample rate in 16.16 fixed format
            // Don't apply fine-tuning here - let the engine handle it
            pI->u.w.sampledRate = (XSDWORD)(w->sampleRate << 16);
        }
        // Apply DLS articulation (ADSR envelope)
        DLS_ParseArticulation(&rg->articulation, pI);
        return pI;
    }
    else
    {
        // Build split instrument
        // First, count valid regions (with resolvable wave and supported PCM)
        uint32_t validCount = 0;
        for (uint32_t i = 0; i < ins->regionCount; i++)
        {
            const DLS_Region *rg = &ins->regions[i];
            if (rg->waveIndex >= bank->waveCount)
            {
                printf("DLS Debug: Split region %u has invalid waveIndex %u >= %u waves\n", i, rg->waveIndex, bank->waveCount);
                continue;
            }
            const DLS_Wave *w = &bank->waves[rg->waveIndex];
            printf("DLS Debug: Split region %u: keys %u-%u, wave %u (%u frames)\n",
                   i, rg->keyLow, rg->keyHigh, rg->waveIndex, w->frameCount);
            if (w->bitsPerSample != 8 && w->bitsPerSample != 16)
                continue;
            if (w->channels < 1)
                continue;
            validCount++;
        }
        if (validCount == 0)
        {
            *pErr = BAD_INSTRUMENT;
            return NULL;
        }

        // Allocate GM_Instrument with extra space for (validCount - 1) splits
        size_t totalSize = sizeof(GM_Instrument) + ((validCount > 1) ? ((validCount - 1) * sizeof(GM_KeymapSplit)) : 0);
        pI = (GM_Instrument *)XNewPtr((int32_t)totalSize);
        if (!pI)
        {
            *pErr = MEMORY_ERR;
            return NULL;
        }
        memset(pI, 0, (int32_t)totalSize);
        pI->doKeymapSplit = TRUE;
        // Split container defaults
        pI->extendedFormat = FALSE;
        pI->notPolyphonic = FALSE;
        pI->useSampleRate = TRUE;
        pI->disableSndLooping = FALSE;
        pI->playAtSampledFreq = FALSE;
        pI->sampleAndHold = FALSE;
        pI->panPlacement = 0;
        pI->u.k.KeymapSplitCount = (INT16)validCount;
        // For each valid region, create a child instrument with copied PCM
        uint32_t si = 0;
        for (uint32_t i = 0; i < ins->regionCount && si < validCount; i++)
        {
            const DLS_Region *rg = &ins->regions[i];
            if (rg->waveIndex >= bank->waveCount)
                continue;
            const DLS_Wave *w = &bank->waves[rg->waveIndex];
            if (w->bitsPerSample != 8 && w->bitsPerSample != 16)
                continue; // unsupported PCM
            if (w->channels < 1)
                continue;

            pI->u.k.keySplits[si].lowMidi = rg->keyLow;
            pI->u.k.keySplits[si].highMidi = rg->keyHigh;
            pI->u.k.keySplits[si].miscParameter1 = 0;
            pI->u.k.keySplits[si].miscParameter2 = 100;
            GM_Instrument *child = (GM_Instrument *)XNewPtr(sizeof(GM_Instrument));
            if (!child)
            {
                *pErr = MEMORY_ERR;
                break;
            }
            memset(child, 0, sizeof(GM_Instrument));
            // Child defaults to match SF2 behavior
            child->extendedFormat = FALSE;
            child->notPolyphonic = FALSE;
            child->useSampleRate = TRUE;
            child->disableSndLooping = FALSE;
            child->playAtSampledFreq = FALSE;
            child->sampleAndHold = FALSE;
            child->panPlacement = 0;
            // Copy original PCM data first
            SBYTE *originalWaveform = (SBYTE *)XNewPtr((int32_t)w->pcmBytes);
            if (!originalWaveform)
            {
                XDisposePtr(child);
                *pErr = MEMORY_ERR;
                break;
            }
            XBlockMove(w->pcm, originalWaveform, (int32_t)w->pcmBytes);

            // DLS 8-bit PCM is unsigned; engine expects signed. Convert in-place.
            if (w->bitsPerSample == 8 && originalWaveform && w->pcmBytes > 0)
            {
                XPhase8BitWaveform((XBYTE *)originalWaveform, w->pcmBytes);
            }

            // Calculate original frame count
            uint32_t originalFrames = w->frameCount;
            uint32_t bytesPerFrame = (w->bitsPerSample / 8) * w->channels;
            uint32_t maxFrames = bytesPerFrame ? (w->pcmBytes / bytesPerFrame) : 0;
            if (originalFrames > maxFrames)
            {
                BAE_PRINTF("DLS Warn: frameCount>%u clamping to %u (bytes=%u, bpf=%u)\n",
                           (unsigned)originalFrames, (unsigned)maxFrames, (unsigned)w->pcmBytes, (unsigned)bytesPerFrame);
                originalFrames = maxFrames;
            }

            // Don't resample - let the engine handle pitch via baseMidiPitch and fine tuning
            SBYTE *finalWaveform = originalWaveform;
            uint32_t finalFrames = originalFrames;

            // Set final waveform data
            child->u.w.theWaveform = finalWaveform;
            child->u.w.bitSize = (INT16)w->bitsPerSample;
            child->u.w.channels = (INT16)w->channels;
            child->u.w.waveSize = (int32_t)(finalFrames * bytesPerFrame);
            child->u.w.waveFrames = (int32_t)finalFrames;
            
            // Set loop points (no scaling needed since we didn't resample)  
            INT32 clStart = (INT32)w->loopStart;
            INT32 clEnd = (INT32)w->loopEnd;
            if (clStart < 0)
                clStart = 0;
            if (clEnd < 0)
                clEnd = 0;
            if (clEnd > (INT32)finalFrames)
                clEnd = (INT32)finalFrames;
            if (clStart >= clEnd)
            {
                clStart = 0;
                clEnd = 0;
            }
            child->u.w.startLoop = clStart;
            child->u.w.endLoop = clEnd;
            int base = (rg->unityNote >= 0 ? rg->unityNote : w->unityNote);
            if (base < 0)
                base = 60;
            if (base > 127)
                base = 127;
            child->u.w.baseMidiPitch = (unsigned char)base;
            // Sample rate: store the original sample rate in 16.16 fixed format
            // Fine-tuning should be handled by the engine, not by resampling
            child->u.w.sampledRate = (XSDWORD)(w->sampleRate << 16);
            // Apply DLS articulation (ADSR envelope) for this region
            printf("DLS Debug: Split region %u art -> delay=%d, attack=%d, hold=%d, decay=%d, release=%d, sustain=%d\n",
                   si, rg->articulation.volEnvDelay, rg->articulation.volEnvAttack, rg->articulation.volEnvHold,
                   rg->articulation.volEnvDecay, rg->articulation.volEnvRelease, rg->articulation.volEnvSustain);
            DLS_ParseArticulation(&rg->articulation, child);
            pI->u.k.keySplits[si].pSplitInstrument = child;
            si++;
        }
        // If we failed to build any children, bail
        if (si == 0)
        {
            XDisposePtr(pI);
            *pErr = BAD_INSTRUMENT;
            return NULL;
        }
        // Adjust count down if we built fewer splits than expected
        pI->u.k.KeymapSplitCount = (INT16)si;
        return pI;
    }
}

OPErr DLS_LoadInstrumentFromAnyBank(uint16_t bankNum, uint16_t programNum, GM_Instrument **ppInstrument)
{
    if (!ppInstrument)
        return PARAM_ERR;
    *ppInstrument = NULL;

    printf("DLS Debug: Looking for instrument bank=%u, program=%u\n", bankNum, programNum);
    printf("DLS Debug: Manager has %u banks\n", g_dlsManager.bankCount);

    DLS_BankNode *n = g_dlsManager.bankList;
    uint32_t bankIndex = 0;
    while (n)
    {
        DLS_Bank *b = n->bank;
        printf("DLS Debug: Checking bank %u with %u instruments\n", bankIndex, b ? b->instrumentCount : 0);
        if (b && b->instruments)
        {
            for (uint32_t i = 0; i < b->instrumentCount; i++)
            {
                DLS_Instrument *ins = &b->instruments[i];
                if (ins->bank == bankNum && ins->program == programNum)
                {
                    printf("DLS Debug: Found matching instrument!\n");
                    OPErr e = NO_ERR;
                    GM_Instrument *gi = DLS_BuildInstrument(b, ins, &e);
                    if (gi && e == NO_ERR)
                    {
                        printf("DLS Debug: Successfully built GM_Instrument\n");
                        *ppInstrument = gi;
                        return NO_ERR;
                    }
                    else
                    {
                        printf("DLS Debug: Failed to build GM_Instrument, error=%d\n", e);
                    }
                }
            }
        }
        n = n->next;
        bankIndex++;
    }
    printf("DLS Debug: No matching instrument found\n");
    return BAD_INSTRUMENT;
}

uint32_t DLS_LoadedBankCount(void)
{
    return g_dlsManager.bankCount;
}

GM_Instrument *PV_GetDLSInstrument(GM_Song *pSong, XLongResourceID instrument, OPErr *pErr)
{
    DLS_BankNode *bankNode;
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
    // 2) Direct MIDI bank MSB 120 (DLS percussion bank convention)
    XBOOL isOddBankPerc = ((midiBank % 2) == 1);
    XBOOL isMSB120Perc = FALSE;

    if (!isOddBankPerc)
    {
        // If not odd mapping, treat direct bank 120 as percussion
        // Convert back to MIDI bank first to test the external value
        uint16_t extBank = midiBank / 2; // internal even bank encodes extBank*2
        if (extBank == 120)
            isMSB120Perc = TRUE;
    }

    if (isOddBankPerc)
    {
        // Odd banks are percussion in miniBAE mapping
        uint16_t noteNumber = midiProgram; // In percussion mapping, program field carries the note
        midiBank = (midiBank - 1) / 2;     // Convert back to external MIDI bank
        // Route to DLS percussion bank
        midiProgram = 0; // Standard drum kit preset
        midiBank = 120;  // DLS percussion bank
    }
    else if (isMSB120Perc)
    {
        // Treat explicit MIDI bank 120 as percussion
        // Keep requested kit program if provided; use note from low 7 bits if present
        uint16_t extProgram = midiProgram; // may indicate kit variant
        uint16_t noteGuess = midiProgram;  // best-effort note guess from instrument encoding
        midiBank = 120;                    // enforce DLS percussion bank
        midiProgram = extProgram;          // try requested kit first, fall back later if needed
    }
    else
    {
        // Melodic mapping
        midiBank = midiBank / 2; // Convert back to external MIDI bank
        // midiProgram stays as-is for melodic instruments
    }

    // Search through loaded DLS banks for a matching preset
    bankNode = g_dlsManager.bankList;
    int bankCount = 0;
    while (bankNode)
    {
        BAE_PRINTF("DLS Debug: Looking for instrument %d -> bank=%d, program=%d\n",
                   instrument, midiBank, midiProgram);
        DLS_Bank *dlsBank = bankNode->bank;
        bankCount++;
        BAE_PRINTF("DLS Debug: Checking DLS bank %d with %u instruments\n", bankCount, dlsBank ? dlsBank->instrumentCount : 0);

        if (dlsBank && dlsBank->instruments)
        {
            // Look for instrument matching this bank/program
            for (uint32_t i = 0; i < dlsBank->instrumentCount; i++)
            {
                DLS_Instrument *dlsInstrument = &dlsBank->instruments[i];

                if (dlsInstrument->bank == midiBank && dlsInstrument->program == midiProgram)
                {
                    BAE_PRINTF("DLS Debug: Found matching instrument! Creating GM_Instrument...\n");
                    // Found matching instrument, create GM_Instrument
                    // Case A: odd internal mapping -> per-note drum (single sample instrument)
                    if (((instrument / 128) % 2) == 1)
                    {
                        uint16_t noteNumber = (uint16_t)(instrument % 128);
                        BAE_PRINTF("DLS Debug: Perc (odd map) using instrument bank=%u prog=%u note=%u\n",
                                   (unsigned)dlsInstrument->bank, (unsigned)dlsInstrument->program, (unsigned)noteNumber);
                        pInstrument = DLS_CreateInstrumentFromNote(dlsBank, midiBank, midiProgram, noteNumber, pErr);
                    }
                    // Case B: direct DLS drum bank requested (bank 120) but not odd mapping -> build full kit (keymap split)
                    else if (dlsInstrument->bank == 120)
                    {
                        BAE_PRINTF("DLS Debug: Perc (bank 120 kit) building keymap split for instrument bank=%u prog=%u\n",
                                   (unsigned)dlsInstrument->bank, (unsigned)dlsInstrument->program);
                        pInstrument = DLS_BuildInstrument(dlsBank, dlsInstrument, pErr);
                    }
                    else
                    {
                        // Regular melodic instrument
                        pInstrument = DLS_BuildInstrument(dlsBank, dlsInstrument, pErr);
                    }

                    if (pInstrument && *pErr == NO_ERR)
                    {
                        BAE_PRINTF("DLS: Loaded instrument %d (bank=%d, program=%d) from DLS\n",
                                   instrument, midiBank, midiProgram);
                        return pInstrument;
                    }
                    else
                    {
                        BAE_PRINTF("DLS Debug: Failed to create instrument, err=%d\n", *pErr);
                    }
                }
            }
        }
        bankNode = bankNode->next;
    }

    // If original intent was percussion, try percussion-specific fallbacks FIRST and bail out if found.
    if (isOddBankPerc || isMSB120Perc)
    {
        uint16_t noteNumber = (uint16_t)(instrument % 128);
        bankNode = g_dlsManager.bankList;
        while (bankNode)
        {
            DLS_Bank *dlsBank = bankNode->bank;
            if (dlsBank && dlsBank->instruments)
            {
                // Pass 1: explicit bank 120
                for (uint32_t i = 0; i < dlsBank->instrumentCount; i++)
                {
                    DLS_Instrument *dlsInstrument = &dlsBank->instruments[i];
                    if (dlsInstrument->bank == 120)
                    {
                        pInstrument = DLS_CreateInstrumentFromNote(dlsBank, dlsInstrument->bank, dlsInstrument->program, noteNumber, pErr);
                        if (pInstrument && *pErr == NO_ERR)
                            return pInstrument;
                    }
                }
                // Pass 2: any percussion-like instruments by name (any bank)
                for (uint32_t i = 0; i < dlsBank->instrumentCount; i++)
                {
                    DLS_Instrument *dlsInstrument = &dlsBank->instruments[i];
                    if (dlsInstrument->bank == 120)
                        continue; // already tried
                    // Simple name-based heuristic for drum kits
                    XBOOL looksLikeDrum = FALSE;
                    if (XStrStr(dlsInstrument->name, "drum") || XStrStr(dlsInstrument->name, "Drum") ||
                        XStrStr(dlsInstrument->name, "DRUM") || XStrStr(dlsInstrument->name, "kit") ||
                        XStrStr(dlsInstrument->name, "Kit") || XStrStr(dlsInstrument->name, "KIT") ||
                        XStrStr(dlsInstrument->name, "perc") || XStrStr(dlsInstrument->name, "Perc") ||
                        XStrStr(dlsInstrument->name, "Steel") || XStrStr(dlsInstrument->name, "Synth") ||
                        XStrStr(dlsInstrument->name, "Elec"))
                    {
                        looksLikeDrum = TRUE;
                    }
                    if (looksLikeDrum)
                    {
                        BAE_PRINTF("DLS Debug: Percussion heuristic trying '%s' bank=%u prog=%u for note %u\n",
                                   dlsInstrument->name, (unsigned)dlsInstrument->bank, (unsigned)dlsInstrument->program, (unsigned)noteNumber);
                        pInstrument = DLS_CreateInstrumentFromNote(dlsBank, dlsInstrument->bank, dlsInstrument->program, noteNumber, pErr);
                        if (pInstrument && *pErr == NO_ERR)
                            return pInstrument;
                    }
                }
                // Pass 3: try common percussion programs (e.g. Steel Drums = 114, Synth Drum = 118) in any bank
                uint16_t percPrograms[] = {114, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127};
                for (int p = 0; p < sizeof(percPrograms)/sizeof(percPrograms[0]); p++)
                {
                    for (uint32_t i = 0; i < dlsBank->instrumentCount; i++)
                    {
                        DLS_Instrument *dlsInstrument = &dlsBank->instruments[i];
                        if (dlsInstrument->program == percPrograms[p])
                        {
                            BAE_PRINTF("DLS Debug: Percussion prog fallback trying '%s' bank=%u prog=%u for note %u\n",
                                       dlsInstrument->name, (unsigned)dlsInstrument->bank, (unsigned)dlsInstrument->program, (unsigned)noteNumber);
                            pInstrument = DLS_CreateInstrumentFromNote(dlsBank, dlsInstrument->bank, dlsInstrument->program, noteNumber, pErr);
                            if (pInstrument && *pErr == NO_ERR)
                                return pInstrument;
                        }
                    }
                }
            }
            bankNode = bankNode->next;
        }
        // If we intended percussion and couldn't find any, don't fall back to melodic instruments
        BAE_PRINTF("DLS Debug: No percussion instruments found for note %u\n", (unsigned)noteNumber);
        *pErr = BAD_INSTRUMENT;
        return NULL;
    }

    // Fallback 1: Try program in bank 0 (General MIDI)
    if (midiBank != 0)
    {
        bankNode = g_dlsManager.bankList;
        while (bankNode)
        {
            DLS_Bank *dlsBank = bankNode->bank;
            if (dlsBank && dlsBank->instruments)
            {
                for (uint32_t i = 0; i < dlsBank->instrumentCount; i++)
                {
                    DLS_Instrument *dlsInstrument = &dlsBank->instruments[i];
                    if (dlsInstrument->bank == 0 && dlsInstrument->program == midiProgram)
                    {
                        BAE_PRINTF("DLS Debug: Found fallback in GM bank (bank=0, program=%d)\n", midiProgram);
                        pInstrument = DLS_BuildInstrument(dlsBank, dlsInstrument, pErr);
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
    // Some DLS sets don't populate the bank field consistently; try a looser match
    // before falling back to piano.
    bankNode = g_dlsManager.bankList;
    while (bankNode)
    {
        DLS_Bank *dlsBank = bankNode->bank;
        if (dlsBank && dlsBank->instruments)
        {
            for (uint32_t i = 0; i < dlsBank->instrumentCount; i++)
            {
                DLS_Instrument *dlsInstrument = &dlsBank->instruments[i];
                if (dlsInstrument->program == midiProgram)
                {
                    BAE_PRINTF("DLS Debug: Found program-only fallback (program=%d) in bank=%d\n", midiProgram, dlsInstrument->bank);
                    // For percussion we still need note-specific loading
                    if ((instrument / 128) % 2 == 1)
                    {
                        uint16_t noteNumber = (uint16_t)(instrument % 128);
                        pInstrument = DLS_CreateInstrumentFromNote(dlsBank, dlsInstrument->bank, dlsInstrument->program, noteNumber, pErr);
                    }
                    else
                    {
                        pInstrument = DLS_BuildInstrument(dlsBank, dlsInstrument, pErr);
                    }

                    if (pInstrument && *pErr == NO_ERR)
                    {
                        BAE_PRINTF("DLS: Loaded instrument via program-only fallback (bank=%d, program=%d)\n", dlsInstrument->bank, dlsInstrument->program);
                        return pInstrument;
                    }
                }
            }
        }
        bankNode = bankNode->next;
    }

    // Fallback 2: Use piano (program 0) from any bank
    bankNode = g_dlsManager.bankList;
    while (bankNode)
    {
        DLS_Bank *dlsBank = bankNode->bank;
        if (dlsBank && dlsBank->instruments)
        {
            for (uint32_t i = 0; i < dlsBank->instrumentCount; i++)
            {
                DLS_Instrument *dlsInstrument = &dlsBank->instruments[i];
                if (dlsInstrument->program == 0) // Piano
                {
                    BAE_PRINTF("DLS Debug: Using piano fallback (bank=%d, program=0)\n", dlsInstrument->bank);
                    pInstrument = DLS_BuildInstrument(dlsBank, dlsInstrument, pErr);
                    if (pInstrument && *pErr == NO_ERR)
                    {
                        return pInstrument;
                    }
                }
            }
        }
        bankNode = bankNode->next;
    }

    // Percussion-specific fallback: if the original was percussion, try bank 120 first, then any drum-like preset
    if ((instrument / 128) % 2 == 1)
    {
        uint16_t noteNumber = (uint16_t)(instrument % 128);
        bankNode = g_dlsManager.bankList;
        while (bankNode)
        {
            DLS_Bank *dlsBank = bankNode->bank;
            if (dlsBank && dlsBank->instruments)
            {
                // Pass 1: prefer explicit bank 120
                for (uint32_t i = 0; i < dlsBank->instrumentCount; i++)
                {
                    DLS_Instrument *dlsInstrument = &dlsBank->instruments[i];
                    if (dlsInstrument->bank == 120)
                    {
                        BAE_PRINTF("DLS Debug: Percussion fallback using kit bank=%u, prog=%u for note %u\n",
                                   (unsigned)dlsInstrument->bank, (unsigned)dlsInstrument->program, (unsigned)noteNumber);
                        pInstrument = DLS_CreateInstrumentFromNote(dlsBank, dlsInstrument->bank, dlsInstrument->program, noteNumber, pErr);
                        if (pInstrument && *pErr == NO_ERR)
                            return pInstrument;
                    }
                }
                // Pass 2: heuristics for non-120 banks
                for (uint32_t i = 0; i < dlsBank->instrumentCount; i++)
                {
                    DLS_Instrument *dlsInstrument = &dlsBank->instruments[i];
                    if (dlsInstrument->bank == 120)
                        continue; // already tried
                    // Simple name-based heuristic for drum kits
                    XBOOL looksLikeDrum = FALSE;
                    if (XStrStr(dlsInstrument->name, "drum") || XStrStr(dlsInstrument->name, "Drum") ||
                        XStrStr(dlsInstrument->name, "DRUM") || XStrStr(dlsInstrument->name, "kit") ||
                        XStrStr(dlsInstrument->name, "Kit") || XStrStr(dlsInstrument->name, "KIT") ||
                        XStrStr(dlsInstrument->name, "perc") || XStrStr(dlsInstrument->name, "Perc"))
                    {
                        looksLikeDrum = TRUE;
                    }
                    if (looksLikeDrum)
                    {
                        BAE_PRINTF("DLS Debug: Percussion heuristic fallback using kit bank=%u, prog=%u for note %u\n",
                                   (unsigned)dlsInstrument->bank, (unsigned)dlsInstrument->program, (unsigned)noteNumber);
                        pInstrument = DLS_CreateInstrumentFromNote(dlsBank, dlsInstrument->bank, dlsInstrument->program, noteNumber, pErr);
                        if (pInstrument && *pErr == NO_ERR)
                            return pInstrument;
                    }
                }
            }
            bankNode = bankNode->next;
        }
    }
    if (bankCount > 0)
    {
        BAE_PRINTF("DLS Debug: No matching DLS instrument found (checked %d banks)\n", bankCount);
    }
    // If we get here, no DLS instrument was found
    *pErr = BAD_INSTRUMENT;
    return NULL;
}

#endif // USE_DLS_SUPPORT
