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
    Additional modifications (c) 2021-2026 zefie
    Licensed under the GNU Lesser General Public License v3.0 or later.
*/
/*****************************************************************************/
/*
**  BAE_EditorAPI.h
**
**  Public RMF / bank Creation & Editor API (formerly declared in NeoBAE.h).
**  Implementation: BAE_EditorAPI.c
*/
/*****************************************************************************/
#ifndef BAE_EDITOR_API_H
#define BAE_EDITOR_API_H

/* When included from NeoBAE.h, core types are already defined. Standalone
 * consumers can include this header directly. */
#ifndef BAE_AUDIO
#include "NeoBAE.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BAERmfEditorDocument BAERmfEditorDocument;

typedef struct BAERmfEditorTrackSetup
{
    unsigned char channel;
    uint16_t bank;
    unsigned char program;
    char *name;
} BAERmfEditorTrackSetup;

typedef struct BAERmfEditorTrackInfo
{
    char const *name;
    unsigned char channel;
    uint16_t bank;
    unsigned char program;
    unsigned char pan;
    unsigned char volume;
    int16_t transpose;
    uint32_t noteCount;
} BAERmfEditorTrackInfo;

typedef struct BAERmfEditorNoteInfo
{
    uint32_t startTick;
    uint32_t durationTicks;
    unsigned char note;
    unsigned char velocity;
    unsigned char channel;
    uint16_t bank;
    unsigned char program;
} BAERmfEditorNoteInfo;

typedef struct BAERmfEditorSampleSetup
{
    unsigned char program;
    unsigned char rootKey;
    unsigned char lowKey;
    unsigned char highKey;
    char *displayName;
} BAERmfEditorSampleSetup;

/* Compression options for embedded RMF samples.
 * BAE_EDITOR_COMPRESSION_DONT_CHANGE is only available for samples that were
 * loaded from an existing RMF file (hasOriginalData == TRUE in the sample info).
 * New samples and replaced samples default to BAE_EDITOR_COMPRESSION_PCM.
 */
typedef enum BAERmfEditorCompressionType
{
    BAE_EDITOR_COMPRESSION_DONT_CHANGE = 0,  /* Re-use original encoded data as-is */
    BAE_EDITOR_COMPRESSION_PCM         = 1,  /* RAW PCM (lossless, largest size) */
    BAE_EDITOR_COMPRESSION_ADPCM       = 2,  /* IMA ADPCM (4:1 ratio) */
    BAE_EDITOR_COMPRESSION_MP3_32K     = 3,  /* MP3 at 32 kbps */
    BAE_EDITOR_COMPRESSION_MP3_64K     = 4,  /* MP3 at 64 kbps */
    BAE_EDITOR_COMPRESSION_MP3_96K     = 5,  /* MP3 at 96 kbps */
    BAE_EDITOR_COMPRESSION_VORBIS_32K  = 6,  /* Ogg Vorbis at ~32 kbps */
    BAE_EDITOR_COMPRESSION_VORBIS_64K  = 7,  /* Ogg Vorbis at ~64 kbps */
    BAE_EDITOR_COMPRESSION_VORBIS_96K  = 8,  /* Ogg Vorbis at ~96 kbps */
    BAE_EDITOR_COMPRESSION_VORBIS_48K  = 24, /* Ogg Vorbis at ~48 kbps */
    BAE_EDITOR_COMPRESSION_VORBIS_80K  = 25, /* Ogg Vorbis at ~80 kbps */
    BAE_EDITOR_COMPRESSION_VORBIS_128K = 26, /* Ogg Vorbis at ~128 kbps */
    BAE_EDITOR_COMPRESSION_VORBIS_160K = 27, /* Ogg Vorbis at ~160 kbps */
    BAE_EDITOR_COMPRESSION_VORBIS_192K = 28, /* Ogg Vorbis at ~192 kbps */
    BAE_EDITOR_COMPRESSION_VORBIS_256K = 29, /* Ogg Vorbis at ~256 kbps */
    BAE_EDITOR_COMPRESSION_FLAC        = 9,  /* FLAC lossless (compression level 9) */
    BAE_EDITOR_COMPRESSION_OPUS_64K    = 10, /* Ogg Opus at 64 kbps */
    BAE_EDITOR_COMPRESSION_OPUS_96K    = 11, /* Ogg Opus at 96 kbps */
    BAE_EDITOR_COMPRESSION_OPUS_128K   = 12, /* Ogg Opus at 128 kbps */
    BAE_EDITOR_COMPRESSION_OPUS_256K   = 13, /* Ogg Opus at 256 kbps */
    BAE_EDITOR_COMPRESSION_OPUS_16K    = 14, /* Ogg Opus at 16 kbps */
    BAE_EDITOR_COMPRESSION_OPUS_32K    = 15, /* Ogg Opus at 32 kbps */
    BAE_EDITOR_COMPRESSION_MP3_48K     = 16, /* MP3 at 48 kbps */
    BAE_EDITOR_COMPRESSION_MP3_128K    = 17, /* MP3 at 128 kbps */
    BAE_EDITOR_COMPRESSION_MP3_192K    = 18, /* MP3 at 192 kbps */
    BAE_EDITOR_COMPRESSION_MP3_256K    = 19, /* MP3 at 256 kbps */
    BAE_EDITOR_COMPRESSION_MP3_320K    = 20, /* MP3 at 320 kbps */
    BAE_EDITOR_COMPRESSION_OPUS_12K    = 21, /* Ogg Opus at 12 kbps */
    BAE_EDITOR_COMPRESSION_OPUS_24K    = 22, /* Ogg Opus at 24 kbps */
    BAE_EDITOR_COMPRESSION_OPUS_48K    = 23, /* Ogg Opus at 48 kbps */
    BAE_EDITOR_COMPRESSION_OPUS_80K    = 30, /* Ogg Opus at 80 kbps */
    BAE_EDITOR_COMPRESSION_OPUS_160K   = 31, /* Ogg Opus at 160 kbps */
    BAE_EDITOR_COMPRESSION_OPUS_192K   = 32, /* Ogg Opus at 192 kbps */
    BAE_EDITOR_COMPRESSION_ALAW        = 33, /* G.711 A-law */
    BAE_EDITOR_COMPRESSION_ULAW        = 34, /* G.711 u-law */
#if USE_QOA_SUPPORT == TRUE
    BAE_EDITOR_COMPRESSION_QOA         = 35, /* Quite OK Audio (sample-only, ZMF required) */
#endif
#if USE_ZMF_SUPPORT == TRUE
    BAE_EDITOR_COMPRESSION_ADPCM_2BIT  = 36  /* headerless 2-bit IMA (ZMF v6) */
#endif
} BAERmfEditorCompressionType;

typedef enum BAERmfEditorMidiStorageType
{
    BAE_EDITOR_MIDI_STORAGE_CMID_BEST_EFFORT = 0,
    BAE_EDITOR_MIDI_STORAGE_ECMI,
    BAE_EDITOR_MIDI_STORAGE_EMID,
    BAE_EDITOR_MIDI_STORAGE_MIDI
} BAERmfEditorMidiStorageType;

typedef enum BAERmfEditorSndStorageType
{
    BAE_EDITOR_SND_STORAGE_ESND = 0, /* encrypted SND (esnd) – default */
    BAE_EDITOR_SND_STORAGE_CSND = 1, /* LZSS-compressed SND (csnd) */
    BAE_EDITOR_SND_STORAGE_SND  = 2  /* plain SND (snd ) */
} BAERmfEditorSndStorageType;

typedef enum BAERmfEditorOpusMode
{
    BAE_EDITOR_OPUS_MODE_AUDIO = 0,
    BAE_EDITOR_OPUS_MODE_VOICE = 1
} BAERmfEditorOpusMode;

typedef struct BAERmfEditorSampleInfo
{
    char const *displayName;
    char const *sourcePath;
    unsigned char program;
    unsigned char rootKey;
    unsigned char lowKey;
    unsigned char highKey;
    int16_t splitVolume;     /* per-split volume (miscParameter2), 0 = use default (100) */
    BAESampleInfo sampleInfo;
    uint32_t sampleSize;
    BAERmfEditorCompressionType compressionType; /* target compression for saving */
    BAE_BOOL hasOriginalData;                    /* TRUE if DONT_CHANGE is available */
    BAERmfEditorSndStorageType sndStorageType;   /* file-level container type (esnd/csnd/snd) */
    BAERmfEditorOpusMode opusMode;               /* Opus encoder application/hint mode */
    BAE_BOOL opusRoundTripResample;              /* For Opus: encode at 48kHz, play back time-stretched at source rate */
} BAERmfEditorSampleInfo;

/* Global sample asset model.
 * A sample asset represents shared audio content and compression policy.
 * Multiple instrument splits can reference one asset.
 */
typedef struct BAERmfEditorSampleAssetInfo
{
    uint32_t assetID;
    char const *displayName;
    char const *sourcePath;
    BAERmfEditorCompressionType compressionType;
    BAERmfEditorOpusMode opusMode;
    BAE_BOOL hasOriginalData;
    uint32_t usageCount;
} BAERmfEditorSampleAssetInfo;

/* ---------- Extended instrument data (ADSR, LFO, LPF, curves) ---------- */

/* Sentinel value for BAERmfEditorSample.instID indicating the sample has no assigned instrument ID.
 * Valid instIDs are small non-negative integers (0..~511); 0xFFFFFFFF is never a real ID. */
#define BAE_EDITOR_INST_ID_NONE    0xFFFFFFFFu

/* Sentinel for BAERmfEditorSample.sampleAssetID / document asset APIs.
 * Asset id 0 is valid (matches bank SND id 0). Never treat 0 as unset. */
#define BAE_EDITOR_SAMPLE_ASSET_ID_NONE    0xFFFFFFFFu

#define BAE_EDITOR_MAX_ADSR_STAGES 32
#define BAE_RMF_MAX_ADSR_STAGES     8  /* hard limit for RMF/HSB format; > 8 stages forces ZMF/ZSB */
#define BAE_EDITOR_MAX_LFOS        6
#define BAE_EDITOR_MAX_CURVES      4

typedef struct BAERmfEditorADSRStageInfo
{
    int32_t level;
    int32_t time;
    int32_t flags;  /* FOUR_CHAR: 'LINE', 'SUST', 'LAST', 'GOTO', 'GOST', 'RELS', or 0 (OFF) */
} BAERmfEditorADSRStageInfo;

typedef struct BAERmfEditorADSRInfo
{
    uint32_t stageCount;
    BAERmfEditorADSRStageInfo stages[BAE_EDITOR_MAX_ADSR_STAGES];
} BAERmfEditorADSRInfo;

typedef struct BAERmfEditorLFOInfo
{
    int32_t destination;  /* FOUR_CHAR: 'VOLU','PITC','SPAN','PAN ','LPFR','LPRE','LPAM' */
    int32_t period;
    int32_t waveShape;    /* FOUR_CHAR: 'SINE','TRIA','SQUA','SQU2','SAWT','SAW2' */
    int32_t DC_feed;
    int32_t level;
    BAERmfEditorADSRInfo adsr;
} BAERmfEditorLFOInfo;

typedef struct BAERmfEditorCurveInfo
{
    int32_t tieFrom;
    int32_t tieTo;
    int16_t curveCount;
    unsigned char from_Value[BAE_EDITOR_MAX_ADSR_STAGES];
    int16_t to_Scalar[BAE_EDITOR_MAX_ADSR_STAGES];
} BAERmfEditorCurveInfo;

typedef struct BAERmfEditorInstrumentExtInfo
{
    uint32_t instID;
    char const *displayName; /* INST resource name shown in instrument list/editor */
    BAE_BOOL hasExtendedData;
    unsigned char flags1;  /* ZBF_ bitmask from InstrumentResource flags1 */
    unsigned char flags2;  /* ZBF_ bitmask from InstrumentResource flags2 */
    char panPlacement;     /* stereo pan from INST header */
    int16_t midiRootKey;   /* master root key from INST header */
    int16_t miscParameter1; /* offset high-word when ZBF_enableSampleOffsetStart is set in flags2 */
    int16_t miscParameter2; /* volume level (100 = default) */
    BAE_BOOL hasDefaultMod; /* TRUE if INST_DEFAULT_MOD present (disables auto mod-wheel curve) */
    int32_t LPF_frequency;
    int32_t LPF_resonance;
    int32_t LPF_lowpassAmount;
    int16_t defaultReverbSend;  /* static reverb send level */
    int16_t defaultChorusSend;  /* static chorus send level */
    BAERmfEditorADSRInfo volumeADSR;
    uint32_t lfoCount;
    BAERmfEditorLFOInfo lfos[BAE_EDITOR_MAX_LFOS];
    uint32_t curveCount;
    BAERmfEditorCurveInfo curves[BAE_EDITOR_MAX_CURVES];
    /* Per-keysplit sample override (bank editor preview) */
    BAE_BOOL hasSampleOverride;     /* TRUE when the fields below are valid */
    uint32_t sampleOverrideIndex;   /* keysplit index (0 for non-split instruments) */
    unsigned char sampleRootKey;    /* baseMidiPitch for GM_Waveform */
    unsigned char sampleLowKey;     /* lowMidi for GM_KeymapSplit */
    unsigned char sampleHighKey;    /* highMidi for GM_KeymapSplit */
    int16_t sampleSplitVolume;      /* miscParameter2 for GM_KeymapSplit */
    uint32_t sampleRate;            /* integer Hz; converted to 16.16 for sampledRate */
    uint32_t sampleLoopStart;       /* startLoop for GM_Waveform */
    uint32_t sampleLoopEnd;         /* endLoop for GM_Waveform */

    /* Codec re-encode target (set by bank editor; consumed by dirtyParamsCallback).
       When sampleTargetCompression == BAE_EDITOR_COMPRESSION_DONT_CHANGE the other
       two fields are ignored and no re-encode is performed. */
    BAERmfEditorCompressionType sampleTargetCompression;
    BAERmfEditorSndStorageType  sampleTargetStorageType;
    BAERmfEditorOpusMode        sampleTargetOpusMode;

#if USE_ZMF_SUPPORT == TRUE
    /* Sample-free oscillator (INST unit OSCL). ZMF v6+. */
    BAE_BOOL useOscillator;
    int32_t oscWaveShape;   /* LFO shapes + 'PLSE'/'NOIS' ('SAWW' aliases 'SAWT') */
    int32_t oscPulseWidth;  /* 0..65536, 32768 = natural shape; PWM all osc waves except Noise */
    int32_t oscVolume;      /* 0..65536, 65536 = unity; default OSC_VOLUME_DEFAULT (25%) */
#endif
} BAERmfEditorInstrumentExtInfo;

// BAESong_PatchLoadedInstrumentExtInfo()
// --------------------------------------
// Modify the ADSR/LFO/LPF parameters of an already-loaded instrument
// in place, including all keysplit sub-instruments.  Used by the bank
// editor so that preview notes hear the user's parameter edits without
// requiring a full bank reload.
// --------------------------------------
BAEResult BAESong_PatchLoadedInstrumentExtInfo(BAESong song,
                                               BAE_INSTRUMENT instrument,
                                               BAERmfEditorInstrumentExtInfo const *info);

/* ----------------------------------------------------------------------- */

BAERmfEditorDocument *BAERmfEditorDocument_New(void);
BAERmfEditorDocument *BAERmfEditorDocument_LoadFromFile(BAEPathName filePath);
BAERmfEditorDocument *BAERmfEditorDocument_LoadFromMemory(void const *data,
                                                          uint32_t dataSize,
                                                          BAEFileType fileTypeHint);
BAEResult BAERmfEditorDocument_Delete(BAERmfEditorDocument *document);
BAEResult BAERmfEditorDocument_SetTempoBPM(BAERmfEditorDocument *document, uint32_t bpm);
BAEResult BAERmfEditorDocument_GetTempoBPM(BAERmfEditorDocument const *document, uint32_t *outBpm);
BAEResult BAERmfEditorDocument_AddTempoEvent(BAERmfEditorDocument *document,
                                             uint32_t tick,
                                             uint32_t microsecondsPerQuarter);
BAEResult BAERmfEditorDocument_SetTempoEvent(BAERmfEditorDocument *document,
                                             uint32_t eventIndex,
                                             uint32_t tick,
                                             uint32_t microsecondsPerQuarter);
BAEResult BAERmfEditorDocument_DeleteTempoEvent(BAERmfEditorDocument *document,
                                                uint32_t eventIndex);
BAEResult BAERmfEditorDocument_GetMidiLoopMarkers(BAERmfEditorDocument const *document,
                                                  bool *outEnabled,
                                                  uint32_t *outStartTick,
                                                  uint32_t *outEndTick,
                                                  int32_t *outLoopCount);
BAEResult BAERmfEditorDocument_SetMidiLoopMarkers(BAERmfEditorDocument *document,
                                                  bool enabled,
                                                  uint32_t startTick,
                                                  uint32_t endTick,
                                                  int32_t loopCount);
BAEResult BAERmfEditorDocument_SetTicksPerQuarter(BAERmfEditorDocument *document, uint16_t ticksPerQuarter);
BAEResult BAERmfEditorDocument_GetTicksPerQuarter(BAERmfEditorDocument const *document, uint16_t *outTicksPerQuarter);
BAEResult BAERmfEditorDocument_SetInfo(BAERmfEditorDocument *document, BAEInfoType infoType, char const *value);
char const *BAERmfEditorDocument_GetInfo(BAERmfEditorDocument const *document, BAEInfoType infoType);
BAEResult BAERmfEditorDocument_SetEngineConfig(BAERmfEditorDocument *document, int32_t flags);
BAEResult BAERmfEditorDocument_GetEngineConfig(BAERmfEditorDocument const *document, int32_t *outFlags);
BAEResult BAERmfEditorDocument_SetVelocityCurve(BAERmfEditorDocument *document, int curveType);
BAEResult BAERmfEditorDocument_GetVelocityCurve(BAERmfEditorDocument const *document, int *outCurveType);
BAEResult BAERmfEditorDocument_AddTrack(BAERmfEditorDocument *document,
                                        BAERmfEditorTrackSetup const *setup,
                                        uint16_t *outTrackIndex);
BAEResult BAERmfEditorDocument_GetTrackCount(BAERmfEditorDocument const *document,
                                             uint16_t *outTrackCount);
BAEResult BAERmfEditorDocument_GetTrackInfo(BAERmfEditorDocument const *document,
                                            uint16_t trackIndex,
                                            BAERmfEditorTrackInfo *outTrackInfo);
BAEResult BAERmfEditorDocument_SetTrackInfo(BAERmfEditorDocument *document,
                                            uint16_t trackIndex,
                                            BAERmfEditorTrackInfo const *trackInfo);
BAEResult BAERmfEditorDocument_GetTrackEndOfTrackTick(BAERmfEditorDocument const *document,
                                                      uint16_t trackIndex,
                                                      uint32_t *outTick);
BAEResult BAERmfEditorDocument_SetTrackEndOfTrackTick(BAERmfEditorDocument *document,
                                                      uint16_t trackIndex,
                                                      uint32_t tick);
BAEResult BAERmfEditorDocument_SetTrackDefaultInstrument(BAERmfEditorDocument *document,
                                                         uint16_t trackIndex,
                                                         uint16_t bank,
                                                         unsigned char program);
/* Remap all references of one instrument pair to another across track defaults,
 * notes, and embedded MIDI event streams (bank/program changes). */
BAEResult BAERmfEditorDocument_RemapInstrumentReferences(BAERmfEditorDocument *document,
                                                         uint16_t sourceBank,
                                                         unsigned char sourceProgram,
                                                         uint16_t targetBank,
                                                         unsigned char targetProgram);
BAEResult BAERmfEditorDocument_DeleteTrack(BAERmfEditorDocument *document,
                                           uint16_t trackIndex);
BAEResult BAERmfEditorDocument_AddNote(BAERmfEditorDocument *document,
                                       uint16_t trackIndex,
                                       uint32_t startTick,
                                       uint32_t durationTicks,
                                       unsigned char note,
                                       unsigned char velocity);
BAEResult BAERmfEditorDocument_GetNoteCount(BAERmfEditorDocument const *document,
                                            uint16_t trackIndex,
                                            uint32_t *outNoteCount);
BAEResult BAERmfEditorDocument_GetNoteInfo(BAERmfEditorDocument const *document,
                                           uint16_t trackIndex,
                                           uint32_t noteIndex,
                                           BAERmfEditorNoteInfo *outNoteInfo);
BAEResult BAERmfEditorDocument_SetNoteInfo(BAERmfEditorDocument *document,
                                           uint16_t trackIndex,
                                           uint32_t noteIndex,
                                           BAERmfEditorNoteInfo const *noteInfo);
BAEResult BAERmfEditorDocument_DeleteNote(BAERmfEditorDocument *document,
                                          uint16_t trackIndex,
                                          uint32_t noteIndex);
/* Destructively trim MIDI data to boundaryTick.
 * Removes notes/events at or after boundary, truncates overlapping notes,
 * trims tempo events, and clamps track end-of-track ticks. */
BAEResult BAERmfEditorDocument_TrimToTick(BAERmfEditorDocument *document,
                                          uint32_t boundaryTick);
BAEResult BAERmfEditorDocument_AddSampleFromFile(BAERmfEditorDocument *document,
                                                 BAEPathName filePath,
                                                 BAERmfEditorSampleSetup const *setup,
                                                 BAESampleInfo *outSampleInfo);
BAEResult BAERmfEditorDocument_AddEmptySample(BAERmfEditorDocument *document,
                                              BAERmfEditorSampleSetup const *setup,
                                              uint32_t *outSampleIndex,
                                              BAESampleInfo *outSampleInfo);
BAEResult BAERmfEditorDocument_GetSampleCount(BAERmfEditorDocument const *document,
                                              uint32_t *outSampleCount);
BAEResult BAERmfEditorDocument_GetSampleInfo(BAERmfEditorDocument const *document,
                                             uint32_t sampleIndex,
                                             BAERmfEditorSampleInfo *outSampleInfo);
BAEResult BAERmfEditorDocument_GetRecommendedSampleRate(BAERmfEditorDocument const *document,
                                                        uint32_t sampleIndex,
                                                        BAERmfEditorCompressionType compressionType,
                                                        BAE_UNSIGNED_FIXED *outSampleRate);
BAEResult BAERmfEditorDocument_SetSampleInfo(BAERmfEditorDocument *document,
                                             uint32_t sampleIndex,
                                             BAERmfEditorSampleInfo const *sampleInfo);
BAEResult BAERmfEditorDocument_GetSampleAssetIDForSample(BAERmfEditorDocument const *document,
                                                         uint32_t sampleIndex,
                                                         uint32_t *outAssetID);
BAEResult BAERmfEditorDocument_SetSampleInstID(BAERmfEditorDocument *document,
                                               uint32_t sampleIndex,
                                               uint32_t instID);
BAEResult BAERmfEditorDocument_GetSampleAssetCount(BAERmfEditorDocument const *document,
                                                   uint32_t *outAssetCount);
BAEResult BAERmfEditorDocument_GetSampleAssetInfo(BAERmfEditorDocument const *document,
                                                  uint32_t assetIndex,
                                                  BAERmfEditorSampleAssetInfo *outAssetInfo);
BAEResult BAERmfEditorDocument_GetSampleAssetUsageCount(BAERmfEditorDocument const *document,
                                                        uint32_t assetID,
                                                        uint32_t *outUsageCount);
BAEResult BAERmfEditorDocument_GetSampleAssetSampleIndex(BAERmfEditorDocument const *document,
                                                         uint32_t assetID,
                                                         uint32_t usageIndex,
                                                         uint32_t *outSampleIndex);
BAEResult BAERmfEditorDocument_SetSampleAssetCompression(BAERmfEditorDocument *document,
                                                         uint32_t assetID,
                                                         BAERmfEditorCompressionType compressionType);
BAEResult BAERmfEditorDocument_SetSampleAssetForSample(BAERmfEditorDocument *document,
                                                       uint32_t sampleIndex,
                                                       uint32_t assetID);
BAEResult BAERmfEditorDocument_CloneSampleAssetForSample(BAERmfEditorDocument *document,
                                                         uint32_t sampleIndex,
                                                         uint32_t *outNewAssetID);
BAEResult BAERmfEditorDocument_DeleteSample(BAERmfEditorDocument *document,
                                            uint32_t sampleIndex);
BAEResult BAERmfEditorDocument_DeleteInstrument(BAERmfEditorDocument *document,
                                                uint32_t instID);
BAEResult BAERmfEditorDocument_ReplaceSampleFromFile(BAERmfEditorDocument *document,
                                                     uint32_t sampleIndex,
                                                     BAEPathName filePath,
                                                     BAESampleInfo *outSampleInfo);
BAEResult BAERmfEditorDocument_ReplaceSampleFromPCM(BAERmfEditorDocument *document,
                                                    uint32_t sampleIndex,
                                                    void const *pcmData,
                                                    uint32_t frameCount,
                                                    uint16_t bitSize,
                                                    uint16_t channels,
                                                    BAE_UNSIGNED_FIXED sampledRate,
                                                    uint32_t startLoop,
                                                    uint32_t endLoop,
                                                    BAESampleInfo *outSampleInfo);
BAEResult BAERmfEditorDocument_PropagateReplacementToAsset(BAERmfEditorDocument *document,
                                                           uint32_t sourceSampleIndex);
BAEResult BAERmfEditorDocument_GetSampleWaveformData(BAERmfEditorDocument const *document,
                                                     uint32_t sampleIndex,
                                                     void const **outWaveData,
                                                     uint32_t *outFrameCount,
                                                     uint16_t *outBitSize,
                                                     uint16_t *outChannels,
                                                     BAE_UNSIGNED_FIXED *outSampleRate);
BAEResult BAERmfEditorDocument_GetSampleCodecDescription(BAERmfEditorDocument const *document,
                                                         uint32_t sampleIndex,
                                                         char *outCodec,
                                                         uint32_t outCodecSize);
BAEResult BAERmfEditorDocument_ExportSampleToFile(BAERmfEditorDocument const *document,
                                                  uint32_t sampleIndex,
                                                  BAEPathName filePath);
/* Extended instrument data API */
BAEResult BAERmfEditorDocument_GetInstIDForSample(BAERmfEditorDocument const *document,
                                                  uint32_t sampleIndex,
                                                  uint32_t *outInstID);
BAEResult BAERmfEditorDocument_GetInstrumentExtInfo(BAERmfEditorDocument const *document,
                                                    uint32_t instID,
                                                    BAERmfEditorInstrumentExtInfo *outInfo);
BAEResult BAERmfEditorDocument_SetInstrumentExtInfo(BAERmfEditorDocument *document,
                                                    uint32_t instID,
                                                    BAERmfEditorInstrumentExtInfo const *info);
/* Ghost instruments (RMF/ZMF only): INST stays in the song document, but
 * referenced bank SNDs are not embedded on RMF/ZMF save. Not used for HSB/ZSB
 * bank files (a bank cannot ghost to itself). Allowed on any MIDI bank number.
 * When ghost is TRUE, document samples for this INST become bank aliases;
 * when FALSE, bank-alias samples are re-embedded from bankToken. */
BAEResult BAERmfEditorDocument_SetInstrumentGhost(BAERmfEditorDocument *document,
                                                  uint32_t instID,
                                                  BAEBankToken bankToken,
                                                  BAE_BOOL ghost);
BAEResult BAERmfEditorDocument_IsInstrumentGhost(BAERmfEditorDocument const *document,
                                                 uint32_t instID,
                                                 BAE_BOOL *outGhost);
BAEResult BAERmfEditorDocument_CopyTempoMapFrom(BAERmfEditorDocument *dest,
                                                BAERmfEditorDocument const *src);
BAEResult BAERmfEditorDocument_GetTempoEventCount(BAERmfEditorDocument const *document,
                                                  uint32_t *outCount);
BAEResult BAERmfEditorDocument_GetTempoEvent(BAERmfEditorDocument const *document,
                                             uint32_t eventIndex,
                                             uint32_t *outTick,
                                             uint32_t *outMicrosecondsPerQuarter);
BAEResult BAERmfEditorDocument_GetTrackCCEventCount(BAERmfEditorDocument const *document,
                                                    uint16_t trackIndex,
                                                    unsigned char cc,
                                                    uint32_t *outCount);
BAEResult BAERmfEditorDocument_GetTrackCCEvent(BAERmfEditorDocument const *document,
                                               uint16_t trackIndex,
                                               unsigned char cc,
                                               uint32_t eventIndex,
                                               uint32_t *outTick,
                                               unsigned char *outValue);
BAEResult BAERmfEditorDocument_AddTrackCCEvent(BAERmfEditorDocument *document,
                                                              uint16_t trackIndex,
                                                              unsigned char cc,
                                                              uint32_t tick,
                                                              unsigned char value);
BAEResult BAERmfEditorDocument_SetTrackCCEvent(BAERmfEditorDocument *document,
                                                              uint16_t trackIndex,
                                                              unsigned char cc,
                                                              uint32_t eventIndex,
                                                              uint32_t tick,
                                                              unsigned char value);
BAEResult BAERmfEditorDocument_DeleteTrackCCEvent(BAERmfEditorDocument *document,
                                                                  uint16_t trackIndex,
                                                                  unsigned char cc,
                                                                  uint32_t eventIndex);
BAEResult BAERmfEditorDocument_GetTrackPitchBendEventCount(BAERmfEditorDocument const *document,
                                                           uint16_t trackIndex,
                                                           uint32_t *outCount);
BAEResult BAERmfEditorDocument_GetTrackPitchBendEvent(BAERmfEditorDocument const *document,
                                                      uint16_t trackIndex,
                                                      uint32_t eventIndex,
                                                      uint32_t *outTick,
                                                      uint16_t *outValue);
BAEResult BAERmfEditorDocument_AddTrackPitchBendEvent(BAERmfEditorDocument *document,
                                                      uint16_t trackIndex,
                                                      uint32_t tick,
                                                      uint16_t value);
BAEResult BAERmfEditorDocument_AddTrackProgramChange(BAERmfEditorDocument *document,
                                                     uint16_t trackIndex,
                                                     uint32_t tick,
                                                     unsigned char program);
BAEResult BAERmfEditorDocument_SetTrackPitchBendEvent(BAERmfEditorDocument *document,
                                                      uint16_t trackIndex,
                                                      uint32_t eventIndex,
                                                      uint32_t tick,
                                                      uint16_t value);
BAEResult BAERmfEditorDocument_DeleteTrackPitchBendEvent(BAERmfEditorDocument *document,
                                                         uint16_t trackIndex,
                                                         uint32_t eventIndex);

/* Meta events (including karaoke lyrics). typeFilter 0 = all types; else match that
 * meta type (e.g. 0x05 Lyric, 0x01 Generic Text). Indices are among filtered events. */
BAEResult BAERmfEditorDocument_GetTrackMetaEventCount(BAERmfEditorDocument const *document,
                                                      uint16_t trackIndex,
                                                      unsigned char typeFilter,
                                                      uint32_t *outCount);
/* outData may be NULL to query size only. dataCapacity is bytes available in outData.
 * outSize receives the full event size (may exceed dataCapacity; data is truncated). */
BAEResult BAERmfEditorDocument_GetTrackMetaEvent(BAERmfEditorDocument const *document,
                                                 uint16_t trackIndex,
                                                 unsigned char typeFilter,
                                                 uint32_t eventIndex,
                                                 uint32_t *outTick,
                                                 unsigned char *outType,
                                                 unsigned char *outData,
                                                 uint32_t dataCapacity,
                                                 uint32_t *outSize);
BAEResult BAERmfEditorDocument_AddTrackMetaEvent(BAERmfEditorDocument *document,
                                                 uint16_t trackIndex,
                                                 uint32_t tick,
                                                 unsigned char type,
                                                 unsigned char const *data,
                                                 uint32_t size);
BAEResult BAERmfEditorDocument_SetTrackMetaEvent(BAERmfEditorDocument *document,
                                                 uint16_t trackIndex,
                                                 unsigned char typeFilter,
                                                 uint32_t eventIndex,
                                                 uint32_t tick,
                                                 unsigned char type,
                                                 unsigned char const *data,
                                                 uint32_t size);
BAEResult BAERmfEditorDocument_DeleteTrackMetaEvent(BAERmfEditorDocument *document,
                                                    uint16_t trackIndex,
                                                    unsigned char typeFilter,
                                                    uint32_t eventIndex);

/* Karaoke lyric helpers (MIDI meta 0x05). Display text has leading '/' or '\\'
 * stripped into outStartsNewLine. */
typedef struct BAERmfEditorLyricFragment
{
    uint32_t tick;
    char const *text;     /* NUL-terminated display text (no line delimiter) */
    BAE_BOOL startsNewLine; /* TRUE => encode leading '/' on write */
} BAERmfEditorLyricFragment;

BAEResult BAERmfEditorDocument_GetLyricEventCount(BAERmfEditorDocument const *document,
                                                  uint16_t trackIndex,
                                                  uint32_t *outCount);
BAEResult BAERmfEditorDocument_GetLyricEvent(BAERmfEditorDocument const *document,
                                             uint16_t trackIndex,
                                             uint32_t eventIndex,
                                             uint32_t *outTick,
                                             char *outText,
                                             uint32_t textCapacity,
                                             BAE_BOOL *outStartsNewLine);
BAEResult BAERmfEditorDocument_AddLyricEvent(BAERmfEditorDocument *document,
                                             uint16_t trackIndex,
                                             uint32_t tick,
                                             char const *text,
                                             BAE_BOOL startsNewLine);
BAEResult BAERmfEditorDocument_SetLyricEvent(BAERmfEditorDocument *document,
                                             uint16_t trackIndex,
                                             uint32_t eventIndex,
                                             uint32_t tick,
                                             char const *text,
                                             BAE_BOOL startsNewLine);
BAEResult BAERmfEditorDocument_DeleteLyricEvent(BAERmfEditorDocument *document,
                                                uint16_t trackIndex,
                                                uint32_t eventIndex);
/* Remove meta 0x05. If alsoClearGenericText01, also remove non-@ meta 0x01. */
BAEResult BAERmfEditorDocument_ClearTrackLyricEvents(BAERmfEditorDocument *document,
                                                     uint16_t trackIndex,
                                                     BAE_BOOL alsoClearGenericText01);
/* Clear lyric metas on track, then write fragments as meta 0x05 (KAR '/' newlines). */
BAEResult BAERmfEditorDocument_ReplaceTrackLyrics(BAERmfEditorDocument *document,
                                                  uint16_t trackIndex,
                                                  BAERmfEditorLyricFragment const *fragments,
                                                  uint32_t fragmentCount);

BAEResult BAERmfEditorDocument_CopySamplesFrom(BAERmfEditorDocument *dest,
                                               BAERmfEditorDocument const *src);
BAEResult BAERmfEditorDocument_CopySamplesForPrograms(BAERmfEditorDocument *dest,
                                                      BAERmfEditorDocument const *src,
                                                      unsigned char const *programFlags128,
                                                      uint32_t *outCopiedCount);
BAEResult BAERmfEditorDocument_SetMidiStorageType(BAERmfEditorDocument *document,
                                                  BAERmfEditorMidiStorageType storageType);
BAEResult BAERmfEditorDocument_GetMidiStorageType(BAERmfEditorDocument const *document,
                                                  BAERmfEditorMidiStorageType *outStorageType);
BAEResult BAERmfEditorDocument_SaveAsRmfToMemory(BAERmfEditorDocument *document,
                                                 bool useZmfContainer,
                                                 unsigned char **outData,
                                                 uint32_t *outSize);
/* packForShip: TRUE = Pack+LZMA (disk export); FALSE = flat finalize (preview/playback). */
BAEResult BAERmfEditorDocument_SaveAsRmfToMemoryEx(BAERmfEditorDocument *document,
                                                   bool useZmfContainer,
                                                   bool packForShip,
                                                   unsigned char **outData,
                                                   uint32_t *outSize);
BAEResult BAERmfEditorDocument_SaveAsRmf(BAERmfEditorDocument *document,
                                         BAEPathName filePath);
BAEResult BAERmfEditorDocument_SaveAsRmfPreserveMidi(BAERmfEditorDocument *document,
                                                     BAEPathName filePath);
BAEResult BAERmfEditorDocument_SaveAsMidi(BAERmfEditorDocument *document,
                                          BAEPathName filePath);
/* Build Standard MIDI File bytes from the document tracks (ignores sampleCount gate).
 * Caller frees *outData with XDisposePtr. */
BAEResult BAERmfEditorDocument_SaveAsMidiToMemory(BAERmfEditorDocument *document,
                                                  unsigned char **outData,
                                                  uint32_t *outSize);
BAE_BOOL BAERmfEditorDocument_CanSaveAsMidi(BAERmfEditorDocument const *document);
BAEResult BAERmfEditorDocument_DebugReportMidiRoundTripDiff(BAERmfEditorDocument *document);
BAEResult BAERmfEditorDocument_Validate(BAERmfEditorDocument *document);
BAE_BOOL BAERmfEditorDocument_RequiresZmf(BAERmfEditorDocument const *document, uint32_t *outReason);
BAE_BOOL BAERmfEditorBank_RequiresZsb(BAEBankToken bankToken, uint32_t *outReason);

/* Read the resource-map header of an RMF/ZMF file and return its format version.
    outVersion receives the raw version field (ZMF: 5 without OSCL, 6 with OSCL).
    Returns BAE_PARAM_ERR, BAE_FILE_NOT_FOUND, BAE_FILE_IO_ERROR, or BAE_BAD_FILE on failure. */
BAEResult BAERmfEditorDocument_GetFileVersion(BAEPathName filePath, int32_t *outVersion);

/* Upgrade a ZMF/RMF file to the current format version and save it to dstPath.
    If outFromVersion is non-NULL it receives the original file version on return.
    Returns BAE_NO_ERROR on successful upgrade, BAE_ALREADY_EXISTS if the file is
    already at the current version (dstPath is not written in that case), or another
    BAEResult error code on failure. */
BAEResult BAERmfEditorDocument_UpgradeFile(BAEPathName srcPath,
                                                         BAEPathName dstPath,
                                                         int32_t *outFromVersion);


void BAE_SetDebugOutputCallback(void (*callback)(const char *message));

enum BAEZMFReasonCode
{
    BAEZMF_REASON_NONE = 0,
    BAEZMF_REASON_LOOP_TOO_SHORT = 1, /* A sample's loop length is less than 20 samples (unsupported in RMF) */
    BAEZMF_REASON_MODERN_CODEC = 2, /* A sample is using a modern codec not compatible with RMF */
    BAEZMF_REASON_CUBIC_INTERPOLATION = 4,
    BAEZMF_REASON_EXTENDED_PITCH_RANGE = 8,
    BAEZMF_REASON_CLASSIC_CHORUS = 16, /* song is flagged to use the classic chorus ordering */
    BAEZMF_REASON_PANFIX = 32, /* song is flagged to use the pan fix */
    BAEZMF_REASON_EXTENDED_ADSR = 64,  /* any instrument has > 8 ADSR stages */
    BAEZMF_REASON_LPF_FILTER = 128, /* LPF/resonance on a stereo sample (mono LPF is classic HSB/RMF OK) */
    BAEZMF_REASON_OSCILLATOR = 256, /* instrument uses OSCL sample-free oscillator (ZMF v6) */
    BAEZMF_REASON_ADPCM_2BIT = 512, /* sample uses headerless 2-bit IMA (ZMF v6) */
    BAEZMF_ALREADY_ZMF = 0x40000000u,
    BAEZMF_REASON_OTHER = 0x80000000u
};

typedef enum BAEZMFReasonCode BAEZMFReasonCode;

void BAEZMFReasonCodeToString(uint32_t reason, char *outBuffer, uint32_t bufferSize);



/* ---------- Bank instrument enumeration and cloning ---------- */

typedef struct BAERmfEditorBankInstrumentInfo
{
    uint32_t instID;                /* INST resource ID from bank */
    char name[256];                 /* INST resource name from bank */
    unsigned char program;          /* instID % 128 (7-bit program within melodic/percussion half) */
    uint16_t bank;                  /* instID / 256 (user-facing bank: 0-255=bank 0, 256-511=bank 1, etc.) */
    int16_t keySplitCount;          /* number of key splits (0 = non-split) */
    unsigned char flags1;           /* ZBF_ bitmask from INST header */
    unsigned char flags2;           /* ZBF_ bitmask from INST header */
} BAERmfEditorBankInstrumentInfo;

/* Count INST resources available in a loaded bank file. */
BAEResult BAERmfEditorBank_GetInstrumentCount(BAEBankToken bankToken,
                                              uint32_t *outCount);

/* Retrieve info about the Nth INST resource in a loaded bank. */
BAEResult BAERmfEditorBank_GetInstrumentInfo(BAEBankToken bankToken,
                                             uint32_t instrumentIndex,
                                             BAERmfEditorBankInstrumentInfo *outInfo);

/* Clone a full instrument (INST + all SND samples) from a bank into the document.
 * targetProgram is the MIDI program number (0-127) to assign in the document. */
BAEResult BAERmfEditorDocument_CloneInstrumentFromBank(BAERmfEditorDocument *document,
                                                       BAEBankToken bankToken,
                                                       uint32_t instrumentIndex,
                                                       unsigned char targetProgram);

/* Alias an instrument from a bank: creates INST metadata referencing the bank's SND
 * resources without copying sample data. The bank must remain loaded for playback. */
BAEResult BAERmfEditorDocument_AliasInstrumentFromBank(BAERmfEditorDocument *document,
                                                       BAEBankToken bankToken,
                                                       uint32_t instrumentIndex,
                                                       unsigned char targetProgram);

/* Add one sample to a song instrument as a bank alias pointer to an existing
 * SND in the loaded bank, without embedding PCM/sample blob in the document. */
BAEResult BAERmfEditorDocument_AddBankAliasSample(BAERmfEditorDocument *document,
                                                  BAEBankToken bankToken,
                                                  uint32_t targetInstID,
                                                  unsigned char targetProgram,
                                                  XShortResourceID sndID,
                                                  char const *displayName,
                                                  unsigned char rootKey,
                                                  unsigned char lowKey,
                                                  unsigned char highKey,
                                                  uint32_t *outSampleIndex,
                                                  BAERmfEditorSampleInfo *outSampleInfo);

/* Resolve an INST ID to a concrete instrument index in the bank.
 * Checks real INST resources first, then falls back to the bank's ID_ALIAS table.
 * On success outResolvedInstID is the concrete INST ID (may differ when aliased)
 * and outInstrumentIndex is the ordinal instrument index inside the bank file. */
BAEResult BAERmfEditorBank_ResolveInstID(BAEBankToken bankToken,
                                         uint32_t instID,
                                         uint32_t *outResolvedInstID,
                                         uint32_t *outInstrumentIndex);

/* Clone a full instrument from a bank, placing it at an explicit target INST ID.
 * Unlike CloneInstrumentFromBank (which hardcodes INST 512+program), this lets
 * the caller choose the destination INST ID (e.g. 640+note for percussion). */
BAEResult BAERmfEditorDocument_CloneInstrumentFromBankToInstID(
    BAERmfEditorDocument *document,
    BAEBankToken bankToken,
    uint32_t instrumentIndex,
    uint32_t targetInstID,
    unsigned char targetProgram);

#define BAE_RMF_EDITOR_MAX_CLONE_MAPPINGS 256

typedef struct BAERmfEditorCloneUsedMapping
{
    uint16_t sourceBank;
    unsigned char sourceProgram;
    unsigned char isPercussion;
    uint32_t requestedInstID;
    uint32_t resolvedInstID;
    uint32_t targetInstID;
    uint32_t sampleCount;
    char resolvedName[256];
} BAERmfEditorCloneUsedMapping;

typedef struct BAERmfEditorCloneUsedResult
{
    uint32_t pitchedCount;
    uint32_t percussionCount;
    uint32_t mappingCount;
    BAERmfEditorCloneUsedMapping mappings[BAE_RMF_EDITOR_MAX_CLONE_MAPPINGS];
} BAERmfEditorCloneUsedResult;

/* Clone every bank instrument referenced by the document and remap those
 * references to the embedded instrument namespace. */
BAEResult BAERmfEditorDocument_CloneUsedInstrumentsFromBank(
    BAERmfEditorDocument *document,
    BAEBankToken bankToken,
    BAERmfEditorCloneUsedResult *outResult);

/* RMF/ZMF export helper: for each bank instrument used by the song, either
 * embed (clone) or bank-alias (ghost) based on embedResolvedInstIDs.
 * Instruments whose resolved INST id is in the embed list are cloned; all
 * other used instruments are aliased (SND not embedded). Remaps notes like
 * CloneUsedInstrumentsFromBank. embedResolvedInstIDs may be NULL when
 * embedCount is 0 (alias everything used). */
BAEResult BAERmfEditorDocument_ExportUsedInstrumentsFromBank(
    BAERmfEditorDocument *document,
    BAEBankToken bankToken,
    uint32_t const *embedResolvedInstIDs,
    uint32_t embedCount,
    BAERmfEditorCloneUsedResult *outResult);

/* After RMF/ZMF song save: copy any missing ID_INST resources from bankToken
 * into the song file. Used by export so oscillator (sample-free) instruments
 * cannot be dropped when the document writer emits ALIS without INST bodies. */
BAEResult BAERmfEditor_InjectMissingInstFromBankIntoSongFile(
    BAEPathName songPath,
    BAEBankToken bankToken,
    uint32_t const *instIDs,
    uint32_t instCount);

/* After song save: ensure every listed INST id exists in the file. Prefers
 * sample-free OSCL rebuild from document, then bank INST copy. Fails if any
 * id is still missing (refuses silent ALIS-without-INST exports). */
BAEResult BAERmfEditorDocument_PatchMissingInstIntoSongFile(
    BAERmfEditorDocument *document,
    BAEPathName songPath,
    BAEBankToken bankToken,
    uint32_t const *instIDs,
    uint32_t instCount);

/* Query whether a sample is a bank alias (pointer to bank SND, no embedded data). */
BAEResult BAERmfEditorDocument_IsSampleBankAlias(BAERmfEditorDocument const *document,
                                                  uint32_t sampleIndex,
                                                  bool *outIsAlias);

/* Wrapper that accepts a BAEMixer (opaque) and returns whether the audio tail is active.
 * Implemented in NeoBAE.c so callers without access to sBAEMixer internals can use it. */
bool BAEMixer_IsAudioTailActive(BAEMixer mixer);

/* ---------- Bank sample enumeration and editing ---------- */

/* Information about a sample/key split within a bank instrument.
 * For non-split instruments (keySplitCount == 0), sampleIndex 0 refers to the base sample.
 * For split instruments, sampleIndex 0..keySplitCount-1 refers to each key split. */
typedef struct BAERmfEditorBankSampleInfo
{
    uint32_t instID;                     /* INST resource ID this sample belongs to */
    uint32_t sampleIndex;                 /* Index of this sample within the instrument (0 for base) */
    unsigned char lowKey;                 /* Low MIDI key range (for splits) */
    unsigned char highKey;               /* High MIDI key range (for splits) */
    unsigned char rootKey;               /* Root key for this sample */
    int16_t splitVolume;                 /* Per-split volume (miscParameter2), 0 = use default (100) */
    XShortResourceID sndResourceID;       /* SND resource ID in the bank file */
    uint32_t sampleRate;                  /* Sample rate in Hz */
    uint32_t frameCount;                 /* Number of audio frames */
    int16_t bitDepth;                    /* Sample bit depth (8 or 16) */
    int16_t channels;                    /* Mono (1) or stereo (2) */
    uint32_t loopStart;                  /* Loop start frame */
    uint32_t loopEnd;                    /* Loop end frame */
    XResourceType compressionType;       /* Compression type (X_UNCOMPRESSED, X_FLAC, etc.) */
    uint32_t compressionSubType;          /* Compression sub-type (CS_VORBIS_*K, CS_OPUS_*K, or CS_DEFAULT) */
    BAERmfEditorSndStorageType sndStorageType; /* Container type of the SND resource (esnd/csnd/snd) */
    bool opusRoundTripResample;          /* For Opus: TRUE if round-trip resampling flag is set */
} BAERmfEditorBankSampleInfo;

/* Count the number of samples (key splits) in a bank instrument.
 * If the instrument has no key splits (keySplitCount == 0), returns 1.
 * If the instrument has key splits, returns keySplitCount. */
BAEResult BAERmfEditorBank_GetInstrumentSampleCount(BAEBankToken bankToken,
                                                     uint32_t instrumentIndex,
                                                     uint32_t *outCount);

/* Get information about a specific sample (or key split) within a bank instrument.
 * For sampleIndex: use 0 for non-split instruments, or 0..keySplitCount-1 for split instruments. */
BAEResult BAERmfEditorBank_GetInstrumentSampleInfo(BAEBankToken bankToken,
                                                    uint32_t instrumentIndex,
                                                    uint32_t sampleIndex,
                                                    BAERmfEditorBankSampleInfo *outInfo);

/* Get extended instrument data (ADSR, LFO, LPF, flags) for a bank instrument.
 * This parses the extended INST data present after the key split array. */
BAEResult BAERmfEditorBank_GetInstrumentExtInfo(BAEBankToken bankToken,
                                                 uint32_t instrumentIndex,
                                                 BAERmfEditorInstrumentExtInfo *outInfo);

/* Set extended instrument data (ADSR, LFO, LPF, flags) for a bank instrument.
 * This modifies the in-memory bank state. Call BAERmfEditorBank_SaveToFile to persist. */
BAEResult BAERmfEditorBank_SetInstrumentExtInfo(BAEBankToken bankToken,
                                                uint32_t instrumentIndex,
                                                BAERmfEditorInstrumentExtInfo const *info);

/* Set sample info for a specific sample within a bank instrument.
 * This modifies the in-memory bank state. Call BAERmfEditorBank_SaveToFile to persist.
 * Pass sampleRate == 0 to update INST keymap fields only (Vol/Low/High/root-in-INST)
 * without opening or rewriting the SND resource — used by the instrument editor keymap. */
BAEResult BAERmfEditorBank_SetInstrumentSampleInfo(BAEBankToken bankToken,
                                                    uint32_t instrumentIndex,
                                                    uint32_t sampleIndex,
                                                    BAERmfEditorBankSampleInfo const *info);

/* Scale the splitVolume field of every split (or header miscParameter2 for
 * non-split instruments) by 'scalar' (0.0=silence, 1.0=unchanged, 2.0=double),
 * clamped to 0-800. Does one INST load + one bank rebuild regardless of split count.
 * Use this for igain instead of calling SetInstrumentSampleInfo per split. */
BAEResult BAERmfEditorBank_ScaleAllSplitVolumes(BAEBankToken bankToken,
                                                uint32_t instrumentIndex,
                                                double scalar);

/* Batch SND write support for --sgain.
 * BeginBatchSnd: enter batch mode; all subsequent PV_BankReplaceSndResourceInPlace
 *   calls accumulate replacements rather than rebuilding the bank.
 * CommitBatchSnd: flush all pending replacements in a single bank rebuild.
 * AbortBatchSnd: discard pending replacements and exit batch mode (on error). */
BAEResult BAERmfEditorBank_BeginBatchSnd(BAEBankToken bankToken);
BAEResult BAERmfEditorBank_CommitBatchSnd(BAEBankToken bankToken);
void      BAERmfEditorBank_AbortBatchSnd(BAEBankToken bankToken);

/* Set only the SND resource reference for a sample slot within an instrument.
 * Unlike BAERmfEditorBank_SetInstrumentSampleInfo, this does not rewrite SND
 * waveform metadata (sample rate/loop/etc). */
BAEResult BAERmfEditorBank_SetInstrumentSampleSndID(BAEBankToken bankToken,
                                                     uint32_t instrumentIndex,
                                                     uint32_t sampleIndex,
                                                     XShortResourceID sndResourceID);

/* Set SND resource ids for every keysplit in one INST write (no per-split rebuild). */
BAEResult BAERmfEditorBank_SetInstrumentSampleSndIDs(BAEBankToken bankToken,
                                                      uint32_t instrumentIndex,
                                                      XShortResourceID const *sndResourceIDs,
                                                      uint32_t sndResourceIDCount);

/* Convert a sample's backing resource between SND/CSND/ESND by rewrapping
 * container data (including CSND compression/encryption handling) without
 * PCM decode/re-encode. */
BAEResult BAERmfEditorBank_SetSampleSndStorageType(BAEBankToken bankToken,
                                                   uint32_t instrumentIndex,
                                                   uint32_t sampleIndex,
                                                   BAERmfEditorSndStorageType sndStorageType);

/* Ensure a bank instrument has at least desiredSampleCount sample slots.
 * For non-split instruments this converts the instrument to key-split mode when
 * desiredSampleCount > 1 and preserves existing sample data in slot 0.
 * Returns BAE_NO_ERROR when no growth is needed. */
BAEResult BAERmfEditorBank_GrowInstrumentSampleSlots(BAEBankToken bankToken,
                                                      uint32_t instrumentIndex,
                                                      uint32_t desiredSampleCount);

/* Delete a sample slot from a bank instrument.
 * For split instruments this removes the selected split and compacts the split list.
 * For non-split instruments (sampleIndex == 0), this clears sndResourceID.
 * When deleteSndIfUnreferenced is TRUE, the underlying SND/CSND/ESND resource is
 * removed if no other bank instrument still references it. */
BAEResult BAERmfEditorBank_DeleteInstrumentSample(BAEBankToken bankToken,
                                                   uint32_t instrumentIndex,
                                                   uint32_t sampleIndex,
                                                   bool deleteSndIfUnreferenced);

/* Delete an instrument (ID_INST resource) from the bank at the given index.
 * Any alias entries pointing to this instrument are also removed.
 * The bank must be reloaded/refreshed after this call. */
BAEResult BAERmfEditorBank_DeleteInstrument(BAEBankToken bankToken,
                                            uint32_t instrumentIndex);

/* Remove a single alias entry from the bank's ID_ALIAS resource by its aliasFrom ID.
 * Does NOT delete the underlying INST resource. */
BAEResult BAERmfEditorBank_DeleteAlias(BAEBankToken bankToken,
                                       uint32_t aliasFromInstID);

/* Remove ALIS entries whose aliasTo equals instID. No-op if none match. */
BAEResult BAERmfEditorBank_PruneAliasesToInstID(BAEBankToken bankToken,
                                                uint32_t instID);

/* Rewrite ALIS entries with aliasTo == oldToInstID so they point at newToInstID. */
BAEResult BAERmfEditorBank_RetargetAliases(BAEBankToken bankToken,
                                           uint32_t oldToInstID,
                                           uint32_t newToInstID);

/* Remove ALIS entries whose aliasTo INST resource does not exist.
 * outRemoved may be NULL. */
BAEResult BAERmfEditorBank_PruneOrphanAliases(BAEBankToken bankToken,
                                              uint32_t *outRemoved);

/* Count ALIS entries whose aliasTo INST resource does not exist. */
BAEResult BAERmfEditorBank_CountOrphanAliases(BAEBankToken bankToken,
                                              uint32_t *outCount);

/* Count ALIS entries whose aliasTo equals instID. */
BAEResult BAERmfEditorBank_CountAliasesToInstID(BAEBankToken bankToken,
                                                uint32_t instID,
                                                uint32_t *outCount);

/* Promote a mixer-loaded bank (typically read-only disk/memory) to an in-memory
 * writable/resizable resource image so XAddFileResource and other mutations work.
 * No-op if the bank is already writable. Safe to call repeatedly. */
BAEResult BAERmfEditorBank_EnsureWritable(BAEBankToken bankToken);

/* Rename a bank SND/CSND/ESND resource (same id/type/data, new resource name). */
BAEResult BAERmfEditorBank_RenameSampleResource(BAEBankToken bankToken,
                                                XShortResourceID sndID,
                                                char const *displayName);

/* Replace/add INST and SND/CSND/ESND resources from sourceFile into dest bank
 * in a single memory-safe rebuild. XDeleteFileResource is a no-op on in-memory
 * banks, so callers must not delete+XAddFileResource for overlays — use this.
 * For each snd id, any existing SND/CSND/ESND with that id is removed before
 * the donor's container type is copied. Missing donor ids are skipped. */
BAEResult BAERmfEditorBank_ImportInstAndSndFromFile(BAEBankToken bankToken,
                                                    void *sourceFile /* XFILE */,
                                                    XShortResourceID const *instIds,
                                                    uint32_t instCount,
                                                    XShortResourceID const *sndIds,
                                                    uint32_t sndCount);

/* Clone an instrument to a new instID within the same bank.
 * If deepClone is TRUE, all referenced SND/CSND/ESND resources are also duplicated.
 * If deepClone is FALSE, the new instrument shares the same SND resource IDs (pointers). */
BAEResult BAERmfEditorBank_CloneInstrument(BAEBankToken bankToken,
                                           uint32_t instrumentIndex,
                                           uint32_t destInstID,
                                           bool deepClone);

/* Add an alias entry mapping aliasInstID -> the instrument at instrumentIndex.
 * The alias is stored in the bank's ID_ALIAS resource. */
BAEResult BAERmfEditorBank_AliasInstrument(BAEBankToken bankToken,
                                           uint32_t instrumentIndex,
                                           uint32_t aliasInstID);

/* Save the modified bank to a file.
 * filePath should have extension .hsb for IREZ format or .zsb for ZREZ format.
 * The format is auto-detected based on the extension (and codec content). */
BAEResult BAERmfEditorBank_SaveToFile(BAEBankToken bankToken,
                                      BAEPathName filePath);

/* Serialize the modified bank to memory.
 * Caller must free *outData with XDisposePtr when done. */
BAEResult BAERmfEditorBank_SaveToMemory(BAEBankToken bankToken,
                                        unsigned char **outData,
                                        uint32_t *outSize);

/* Like SaveToMemory, but overrideResourceID may force IREZ or ZREZ
 * (XFILERESOURCE_ID / XFILERESOURCE_ZMF_ID). Pass 0 to keep source format.
 * Packs for ship (LZMA) when converting formats. */
BAEResult BAERmfEditorBank_SaveToMemoryEx(BAEBankToken bankToken,
                                          int32_t overrideResourceID,
                                          unsigned char **outData,
                                          uint32_t *outSize);
/* packForShip: TRUE = Pack on IREZ→ZREZ / format convert (export);
 * FALSE = stamp flat ZREZ only (session / editor clone). */
BAEResult BAERmfEditorBank_SaveToMemoryExFlags(BAEBankToken bankToken,
                                               int32_t overrideResourceID,
                                               BAE_BOOL packForShip,
                                               unsigned char **outData,
                                               uint32_t *outSize);

/* Decode waveform data for a specific sample within a bank instrument.
 * This loads and decodes the SND resource to PCM. The caller must call
 * BAERmfEditorBank_FreeWaveformData() to free outWaveData when done. */
BAEResult BAERmfEditorBank_GetSampleWaveformData(BAEBankToken bankToken,
                                                  uint32_t instrumentIndex,
                                                  uint32_t sampleIndex,
                                                  void **outWaveData,
                                                  uint32_t *outFrameCount,
                                                  uint16_t *outBitSize,
                                                  uint16_t *outChannels,
                                                  BAE_UNSIGNED_FIXED *outSampleRate);

/* Decode waveform data for a bank SND/CSND/ESND by resource id (assigned or
 * unassigned). Caller must free outWaveData with BAERmfEditorBank_FreeWaveformData. */
BAEResult BAERmfEditorBank_GetSndWaveformData(BAEBankToken bankToken,
                                               uint32_t sndResourceID,
                                               void **outWaveData,
                                               uint32_t *outFrameCount,
                                               uint16_t *outBitSize,
                                               uint16_t *outChannels,
                                               BAE_UNSIGNED_FIXED *outSampleRate);

/* Free waveform data returned by BAERmfEditorBank_GetSampleWaveformData. */
void BAERmfEditorBank_FreeWaveformData(void *waveData);

/* Export a bank SND resource to a native audio file. Compressed codecs write
 * their bitstream (FLAC/Vorbis/Opus/MPEG/QOA); PCM/IMA/ADPCM write WAV (or AIFF
 * if the path ends in .aif/.aiff). Works for assigned and unassigned samples. */
BAEResult BAERmfEditorBank_ExportSndResourceToFile(BAEBankToken bankToken,
                                                   uint32_t sndResourceID,
                                                   BAEPathName filePath);

/* Export a sample (key split) from a bank instrument. Resolves the SND id then
 * calls BAERmfEditorBank_ExportSndResourceToFile. */
BAEResult BAERmfEditorBank_ExportSampleToFile(BAEBankToken bankToken,
                                              uint32_t instrumentIndex,
                                              uint32_t sampleIndex,
                                              BAEPathName filePath);

/* Re-encode the audio data for a specific sample in a bank instrument using the
 * specified codec and container type.  The sample PCM is decoded from the current
 * SND resource, re-encoded with compressionType, wrapped in sndStorageType, and
 * stored back into the bank's in-memory resource image.
 * Returns BAE_NO_ERROR on success, or an error code on failure.
 * Does nothing (returns BAE_NO_ERROR) when compressionType is
 * BAE_EDITOR_COMPRESSION_DONT_CHANGE. */
BAEResult BAERmfEditorBank_ReEncodeSample(BAEBankToken bankToken,
                                           uint32_t instrumentIndex,
                                           uint32_t sampleIndex,
                                           BAERmfEditorCompressionType compressionType,
                                           BAERmfEditorSndStorageType sndStorageType,
                                           BAERmfEditorOpusMode opusMode);

/* Re-encode a bank sample using caller-supplied original PCM data.
 * Use this variant when you have cached the original clean PCM to avoid
 * re-decoding from already-compressed data.  sourcePcm is borrowed (not
 * transferred); the function makes an internal copy before encoding. */
BAEResult BAERmfEditorBank_ReEncodeSampleFromPCM(BAEBankToken bankToken,
                                                  uint32_t instrumentIndex,
                                                  uint32_t sampleIndex,
                                                  BAERmfEditorCompressionType compressionType,
                                                  BAERmfEditorSndStorageType sndStorageType,
                                                  BAERmfEditorOpusMode opusMode,
                                                  const void *sourcePcm,
                                                  uint32_t frameCount,
                                                  uint16_t bitSize,
                                                  uint16_t channels,
                                                  BAE_UNSIGNED_FIXED sampleRate);

/* Extended variant with explicit Opus round-trip mode.
 * When opusRoundTripResample is TRUE and compressionType is Opus, the encoder
 * writes a round-trip Opus stream and preserves source-rate playback semantics. */
BAEResult BAERmfEditorBank_ReEncodeSampleFromPCMEx(BAEBankToken bankToken,
                                                    uint32_t instrumentIndex,
                                                    uint32_t sampleIndex,
                                                    BAERmfEditorCompressionType compressionType,
                                                    BAERmfEditorSndStorageType sndStorageType,
                                                    BAERmfEditorOpusMode opusMode,
                                                    bool opusRoundTripResample,
                                                    const void *sourcePcm,
                                                    uint32_t frameCount,
                                                    uint16_t bitSize,
                                                    uint16_t channels,
                                                    BAE_UNSIGNED_FIXED sampleRate);

/* Fast path for callers that already own mutable PCM memory and do not need
 * an internal defensive copy before encode. The buffer may be modified in place. */
BAEResult BAERmfEditorBank_ReEncodeSampleFromMutablePCMEx(BAEBankToken bankToken,
                                                           uint32_t instrumentIndex,
                                                           uint32_t sampleIndex,
                                                           BAERmfEditorCompressionType compressionType,
                                                           BAERmfEditorSndStorageType sndStorageType,
                                                           BAERmfEditorOpusMode opusMode,
                                                           bool opusRoundTripResample,
                                                           void *mutablePcm,
                                                           uint32_t frameCount,
                                                           uint16_t bitSize,
                                                           uint16_t channels,
                                                           BAE_UNSIGNED_FIXED sampleRate);

/* In-memory encode→decode for sample-editor compression preview.
 * Does not modify any bank. Free outWaveData with BAERmfEditorBank_FreeWaveformData. */
BAEResult BAERmfEditor_PreviewCompressPCM(const void *sourcePcm,
                                          uint32_t frameCount,
                                          uint16_t bitSize,
                                          uint16_t channels,
                                          BAE_UNSIGNED_FIXED sampleRate,
                                          uint32_t loopStart,
                                          uint32_t loopEnd,
                                          int16_t rootKey,
                                          uint32_t metadataSampleRateHz,
                                          BAERmfEditorCompressionType compressionType,
                                          BAERmfEditorOpusMode opusMode,
                                          bool opusRoundTripResample,
                                          void **outWaveData,
                                          uint32_t *outFrameCount,
                                          uint16_t *outBitSize,
                                          uint16_t *outChannels,
                                          BAE_UNSIGNED_FIXED *outSampleRate,
                                          uint32_t *outEncodedBytes);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BAE_EDITOR_API_H */
