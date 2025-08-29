/*
    Copyright (c) 2025 zefie. All rights reserved.
    
    SF2 (SoundFont 2) support for miniBAE
    
    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:
    
    Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
    
    Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
*/

#ifndef GENSF2_H
#define GENSF2_H

#include "X_API.h"
#include "GenSnd.h"

#ifdef __cplusplus
extern "C" {
#endif

// SF2 file format structures
#pragma pack(push, 1)

// RIFF chunk header
typedef struct
{
    uint32_t id;        // chunk ID (FOURCC)
    uint32_t size;      // chunk size (not including header)
} SF2_ChunkHeader;

// SF2 file header
typedef struct
{
    SF2_ChunkHeader riff;      // 'RIFF'
    uint32_t sfbk;             // 'sfbk' 
    SF2_ChunkHeader list_info; // 'LIST'
    uint32_t info;             // 'INFO'
} SF2_FileHeader;

// SF2 Sample header
typedef struct
{
    char name[20];          // Sample name
    uint32_t start;         // Start sample offset
    uint32_t end;           // End sample offset
    uint32_t startloop;     // Loop start
    uint32_t endloop;       // Loop end  
    uint32_t sampleRate;    // Sample rate in Hz
    uint8_t originalPitch;  // Original MIDI note
    int8_t pitchCorrection; // Pitch correction in cents
    uint16_t sampleLink;    // Link to stereo sample
    uint16_t sampleType;    // Sample type flags
} SF2_Sample;

// SF2 Instrument header
typedef struct
{
    char name[20];      // Instrument name
    uint16_t bagIndex;  // Index into instrument bag
} SF2_Instrument;

// SF2 Preset header
typedef struct
{
    char name[20];      // Preset name
    uint16_t preset;    // MIDI preset number
    uint16_t bank;      // MIDI bank number
    uint16_t bagIndex;  // Index into preset bag
    uint32_t library;   // Library (unused)
    uint32_t genre;     // Genre (unused)
    uint32_t morphology; // Morphology (unused)
} SF2_Preset;

// SF2 Generator types
typedef enum
{
    SF2_GEN_START_ADDRS_OFFSET = 0,
    SF2_GEN_END_ADDRS_OFFSET = 1,
    SF2_GEN_STARTLOOP_ADDRS_OFFSET = 2,
    SF2_GEN_ENDLOOP_ADDRS_OFFSET = 3,
    SF2_GEN_START_ADDRS_COARSE_OFFSET = 4,
    SF2_GEN_MOD_LFO_TO_PITCH = 5,
    SF2_GEN_VIB_LFO_TO_PITCH = 6,
    SF2_GEN_MOD_ENV_TO_PITCH = 7,
    SF2_GEN_INITIAL_FILTER_FC = 8,
    SF2_GEN_INITIAL_FILTER_Q = 9,
    SF2_GEN_MOD_LFO_TO_FILTER_FC = 10,
    SF2_GEN_MOD_ENV_TO_FILTER_FC = 11,
    SF2_GEN_END_ADDRS_COARSE_OFFSET = 12,
    SF2_GEN_MOD_LFO_TO_VOLUME = 13,
    SF2_GEN_CHORUS_EFFECTS_SEND = 15,
    SF2_GEN_REVERB_EFFECTS_SEND = 16,
    SF2_GEN_PAN = 17,
    SF2_GEN_DELAY_MOD_LFO = 21,
    SF2_GEN_FREQ_MOD_LFO = 22,
    SF2_GEN_DELAY_VIB_LFO = 23,
    SF2_GEN_FREQ_VIB_LFO = 24,
    SF2_GEN_DELAY_MOD_ENV = 25,
    SF2_GEN_ATTACK_MOD_ENV = 26,
    SF2_GEN_HOLD_MOD_ENV = 27,
    SF2_GEN_DECAY_MOD_ENV = 28,
    SF2_GEN_SUSTAIN_MOD_ENV = 29,
    SF2_GEN_RELEASE_MOD_ENV = 30,
    SF2_GEN_KEYNUM_TO_MOD_ENV_HOLD = 31,
    SF2_GEN_KEYNUM_TO_MOD_ENV_DECAY = 32,
    SF2_GEN_DELAY_VOL_ENV = 33,
    SF2_GEN_ATTACK_VOL_ENV = 34,
    SF2_GEN_HOLD_VOL_ENV = 35,
    SF2_GEN_DECAY_VOL_ENV = 36,
    SF2_GEN_SUSTAIN_VOL_ENV = 37,
    SF2_GEN_RELEASE_VOL_ENV = 38,
    SF2_GEN_KEYNUM_TO_VOL_ENV_HOLD = 39,
    SF2_GEN_KEYNUM_TO_VOL_ENV_DECAY = 40,
    SF2_GEN_INSTRUMENT = 41,
    SF2_GEN_KEY_RANGE = 43,
    SF2_GEN_VEL_RANGE = 44,
    SF2_GEN_STARTLOOP_ADDRS_COARSE_OFFSET = 45,
    SF2_GEN_KEYNUM = 46,
    SF2_GEN_VELOCITY = 47,
    SF2_GEN_INITIAL_ATTENUATION = 48,
    SF2_GEN_ENDLOOP_ADDRS_COARSE_OFFSET = 50,
    SF2_GEN_COARSE_TUNE = 51,
    SF2_GEN_FINE_TUNE = 52,
    SF2_GEN_SAMPLE_ID = 53,
    SF2_GEN_SAMPLE_MODES = 54,
    SF2_GEN_SCALE_TUNING = 56,
    SF2_GEN_EXCLUSIVE_CLASS = 57,
    SF2_GEN_OVERRIDING_ROOT_KEY = 58
} SF2_GeneratorType;

// SF2 Generator
typedef struct
{
    uint16_t generator;     // Generator type
    uint16_t amount;        // Generator amount
} SF2_Generator;

// SF2 Bag (zone)
typedef struct
{
    uint16_t genIndex;      // Index into generator list
    uint16_t modIndex;      // Index into modulator list
} SF2_Bag;

#pragma pack(push, 1)
// SF2 Modulator (SFModList)
typedef struct
{
    uint16_t srcOper;      // Source modulator
    uint16_t destOper;     // Destination generator (SF2_GeneratorType)
    int16_t amount;        // Modulation amount
    uint16_t amtSrcOper;   // Amount source modulator
    uint16_t transOper;    // Transform operator
} SF2_Modulator;
#pragma pack(pop)

#pragma pack(pop)

// SF2 Bank structure for miniBAE
typedef struct SF2_Bank
{
    char *samples;              // Raw sample data
    uint32_t samplesSize;       // Size of sample data
    SF2_Sample *sampleHeaders;  // Array of sample headers
    uint32_t numSamples;        // Number of samples
    SF2_Preset *presets;        // Array of presets
    uint32_t numPresets;        // Number of presets
    SF2_Instrument *instruments; // Array of instruments
    uint32_t numInstruments;    // Number of instruments
    SF2_Bag *presetBags;        // Preset bags
    uint32_t numPresetBags;     // Number of preset bags
    SF2_Generator *presetGens;  // Preset generators
    uint32_t numPresetGens;     // Number of preset generators
    SF2_Modulator *presetMods;  // Preset modulators
    uint32_t numPresetMods;     // Number of preset modulators
    SF2_Bag *instBags;          // Instrument bags
    uint32_t numInstBags;       // Number of instrument bags
    SF2_Generator *instGens;    // Instrument generators
    uint32_t numInstGens;       // Number of instrument generators
    SF2_Modulator *instMods;    // Instrument modulators
    uint32_t numInstMods;       // Number of instrument modulators
} SF2_Bank;

// SF2 Bank manager structure
typedef struct SF2_BankNode
{
    SF2_Bank *bank;
    char *filePath;
    struct SF2_BankNode *next;
} SF2_BankNode;

// Global SF2 bank manager
typedef struct
{
    SF2_BankNode *bankList;
    uint32_t numBanks;
} SF2_BankManager;

// Function prototypes
OPErr SF2_LoadBank(XFILENAME *file, SF2_Bank **ppBank);
void SF2_UnloadBank(SF2_Bank *pBank);
GM_Instrument *SF2_CreateInstrumentFromPreset(SF2_Bank *pBank, uint16_t bank, uint16_t preset, OPErr *pErr);
GM_Instrument *SF2_CreateInstrumentFromPresetWithNote(SF2_Bank *pBank, uint16_t bank, uint16_t preset, uint16_t note, OPErr *pErr);
OPErr SF2_GetPresetInfo(SF2_Bank *pBank, uint16_t index, char *name, uint16_t *bank, uint16_t *preset);
uint32_t SF2_LoadedBankCount(void);

// SF2 Bank manager functions
OPErr SF2_InitBankManager(void);
void SF2_ShutdownBankManager(void);
OPErr SF2_AddBankToManager(SF2_Bank *bank, const char *filePath);
void SF2_RemoveBankFromManager(SF2_Bank *bank);
SF2_Bank* SF2_FindBankByPath(const char *filePath);
OPErr SF2_LoadInstrumentFromAnyBank(uint16_t bankNum, uint16_t presetNum, GM_Instrument **ppInstrument);

// Private function for instrument loading integration
GM_Instrument* PV_GetSF2Instrument(GM_Song *pSong, XLongResourceID instrument, OPErr *pErr);

#ifdef __cplusplus
}
#endif

#endif // GENSF2_H
