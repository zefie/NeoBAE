/****************************************************************************
 *
 * GenTSF.h
 *
 * TSF (TinySoundFont) integration for miniBAE
 * Provides SF2 soundfont support through TSF when USE_SF2_SUPPORT is enabled
 *
 ****************************************************************************/

#ifndef G_TSF_H
#define G_TSF_H

#include "X_API.h"
#include "GenSnd.h"

#ifdef __cplusplus
extern "C" {
#endif

#if USE_SF2_SUPPORT == TRUE
// Include TSF headers
#include "tsf.h"

// TSF integration types
typedef struct GM_TSFInfo
{
    void*           tsf_soundfont;      // tsf* handle
    XBOOL           tsf_active;         // TRUE if TSF is handling this song
    char            tsf_sf2_path[256];  // path to loaded SF2 file
    XFIXED          tsf_master_volume;  // master volume scaling
    int16_t         tsf_sample_rate;    // sample rate for TSF rendering
    int16_t         tsf_max_voices;     // voice limit for TSF
    // Per-channel volume & expression (0..127); initialized to default GM values
    uint8_t         channelVolume[16];
    uint8_t         channelExpression[16];
} GM_TSFInfo;

// Initialize TSF support for the mixer
OPErr GM_InitializeTSF(void);
void GM_CleanupTSF(void);

// Load SF2 soundfont for TSF rendering
OPErr GM_LoadTSFSoundfont(const char* sf2_path);
void GM_UnloadTSFSoundfont(void);

// Check if a song should use TSF rendering
XBOOL GM_IsTSFSong(GM_Song* pSong);

// Enable/disable TSF rendering for a song
OPErr GM_EnableTSFForSong(GM_Song* pSong, XBOOL enable);

// TSF MIDI event processing (called from existing MIDI processors)
void GM_TSF_ProcessNoteOn(GM_Song* pSong, int16_t channel, int16_t note, int16_t velocity);
void GM_TSF_ProcessNoteOff(GM_Song* pSong, int16_t channel, int16_t note, int16_t velocity);
void GM_TSF_ProcessProgramChange(GM_Song* pSong, int16_t channel, int16_t program);
void GM_TSF_ProcessController(GM_Song* pSong, int16_t channel, int16_t controller, int16_t value);
void GM_TSF_ProcessPitchBend(GM_Song* pSong, int16_t channel, int16_t bendMSB, int16_t bendLSB);

// TSF audio rendering (called during mixer slice processing)
void GM_TSF_RenderAudioSlice(GM_Song* pSong, int32_t* mixBuffer, int32_t frameCount);

// TSF channel management (respects miniBAE mute/solo states)
void GM_TSF_MuteChannel(GM_Song* pSong, int16_t channel);
void GM_TSF_UnmuteChannel(GM_Song* pSong, int16_t channel);
void GM_TSF_AllNotesOff(GM_Song* pSong);
void GM_TSF_AllNotesOffChannel(GM_Song* pSong, int16_t channel);
// Force immediate silence (drop sustain, end or quick-end all active voices). Used for pause.
void GM_TSF_SilenceSong(GM_Song* pSong);

void GM_TSF_StoreRMFInstrumentIDs(uint32_t* rmf_instrumentIDs);

// TSF configuration
void GM_TSF_SetMasterVolume(XFIXED volume);
XFIXED GM_TSF_GetMasterVolume(void);
void GM_TSF_SetMaxVoices(int16_t maxVoices);
int16_t GM_TSF_GetMaxVoices(void);
void PV_TSF_SetBankPreset(GM_Song* pSong, int16_t channel, int16_t bank, int16_t preset);

// TSF status queries
int16_t GM_TSF_GetActiveVoiceCount(void);
XBOOL GM_TSF_IsActive(void);
void GM_ResetTSF(void);
void tsf_get_channel_amplitudes(float* channelAmplitudes);


#endif // USE_SF2_SUPPORT

#ifdef __cplusplus
}
#endif

#endif // G_TSF_H
