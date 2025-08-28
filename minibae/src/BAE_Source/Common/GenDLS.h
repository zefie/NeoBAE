/*
    Minimal DLS (Downloadable Sounds Level 1/2) loader for miniBAE

    Scope (v0):
    - Parse RIFF DLS container: ptbl, wvpl (LIST wave), LIST lins (instruments)
    - Support PCM mono/stereo 8/16-bit, unity note and loop points from wsmp
    - Build GM_Instrument with key-splits per DLS regions
    - No modulators; simple default ADSR

    This mirrors the SF2 integration style but is intentionally simpler.
*/

#ifndef GENDLS_H
#define GENDLS_H

#include "X_API.h"
#include "GenSnd.h"

#ifdef __cplusplus
extern "C" {
#endif

#if USE_DLS_SUPPORT == TRUE

typedef struct DLS_Wave
{
    // Basic PCM format
    uint32_t sampleRate; // Hz
    uint16_t channels;   // 1 or 2
    uint16_t bitsPerSample; // 8 or 16
    uint32_t frameCount; // total frames (per channel)
    // Loop points (frames)
    uint32_t loopStart;
    uint32_t loopEnd;
    // Tuning
    int16_t unityNote;     // MIDI note number, default 60
    int16_t fineTuneCents; // -50..+50 typical

    // Raw interleaved PCM (little-endian for 16-bit). Owned by bank.
    unsigned char *pcm;
    uint32_t pcmBytes;

    // Relative offset within wvpl list (for ptbl mapping)
    uint32_t wvplOffset;
} DLS_Wave;

typedef struct DLS_Region
{
    uint8_t keyLow, keyHigh;
    uint8_t velLow, velHigh;
    uint32_t waveIndex; // index into bank->waves
    // Optional per-region tuning overrides
    int16_t unityNote;     // -1 if not set
    int16_t fineTuneCents; // 0 default
    // Whether articulation from ART chunk has been parsed and applied
    uint8_t artInitialized;
    
    // DLS Articulation data (from art1/art2 chunks)
    struct {
        // Volume envelope (EG1)
        int32_t volEnvDelay;    // microseconds
        int32_t volEnvAttack;   // microseconds  
        int32_t volEnvHold;     // microseconds
        int32_t volEnvDecay;    // microseconds
        int32_t volEnvSustain;  // level (0-1000)
        int32_t volEnvRelease;  // microseconds
        
        // LFO parameters
        int32_t lfoFreq;        // frequency in centi-Hz
        int32_t lfoDelay;       // delay in microseconds
        int32_t lfoToPitch;     // pitch modulation depth in cents
        int32_t lfoToVolume;    // volume modulation depth in cB
        int32_t lfoToFilterFc;  // filter cutoff modulation depth in cents
    } articulation;
} DLS_Region;

typedef struct DLS_Instrument
{
    uint16_t bank;    // MIDI bank (0..16383); 120 == percussion by convention
    uint16_t program; // 0..127
    // Regions
    DLS_Region *regions;
    uint32_t regionCount;
    // Optional name
    char name[32];
} DLS_Instrument;

typedef struct DLS_Bank
{
    // Waves
    DLS_Wave *waves;
    uint32_t waveCount;

    // Instruments
    DLS_Instrument *instruments;
    uint32_t instrumentCount;

    // Wave pool table (ptbl)
    uint32_t *ptblOffsets;   // offsets relative to start of wvpl data (cdat+4)
    int32_t  *ptblToWave;    // mapping from ptbl index -> wave index
    uint32_t ptblCount;
    uint32_t wvplDataOffset; // absolute file offset of start of wvpl data

    // Mobile DLS aliasing (pgal chunk)
    uint8_t drumAliasTable[128];  // drum note aliasing: drumAliasTable[noteIn] = noteOut
    XBOOL hasDrumAliasing;        // TRUE if drum aliasing table is present
    
    // Melodic instrument aliasing entries
    struct {
        uint16_t srcBank;    // source bank (MSB:LSB 7:7 bits)
        uint8_t srcProgram;  // source program (0-127)
        uint16_t dstBank;    // destination bank (MSB:LSB 7:7 bits)
        uint8_t dstProgram;  // destination program (0-127)
    } *instrumentAliases;
    uint32_t instrumentAliasCount;

    // Keep original file alive (optional)
    XPTR ownedMemory; // entire file if read into memory
    uint32_t ownedSize;
} DLS_Bank;

// Bank manager (linked list like SF2)
typedef struct DLS_BankNode
{
    DLS_Bank *bank;
    char *filePath; // strdup
    struct DLS_BankNode *next;
} DLS_BankNode;

typedef struct DLS_BankManager
{
    DLS_BankNode *bankList;
    uint32_t bankCount;
} DLS_BankManager;

// API
OPErr DLS_LoadBank(XFILENAME *file, DLS_Bank **ppBank);
void DLS_UnloadBank(DLS_Bank *pBank);

// Manager
OPErr DLS_InitBankManager(void);
void DLS_ShutdownBankManager(void);
OPErr DLS_AddBankToManager(DLS_Bank *bank, const char *filePath);
void DLS_RemoveBankFromManager(DLS_Bank *bank);
DLS_Bank *DLS_FindBankByPath(const char *filePath);
uint32_t DLS_LoadedBankCount(void);

// Instrument lookup
OPErr DLS_LoadInstrumentFromAnyBank(uint16_t bankNum, uint16_t programNum, GM_Instrument **ppInstrument);

// Integration point for GenPatch
GM_Instrument *PV_GetDLSInstrument(GM_Song *pSong, XLongResourceID instrument, OPErr *pErr);

#endif // USE_DLS_SUPPORT

#ifdef __cplusplus
}
#endif

#endif // GENDLS_H
