/****************************************************************************
 *
 * GenBassMidi.h
 *
 * BassMidi integration for miniBAE
 * Provides SF2 soundfont support through BassMidi when USE_SF2_SUPPORT is enabled
 *
 ****************************************************************************/

#ifndef G_BASSMIDI_H
#define G_BASSMIDI_H

#include "X_API.h"
#include "GenSnd.h"
#include "MiniBAE.h"

#ifdef __cplusplus
extern "C" {
#endif

#if USE_SF2_SUPPORT == TRUE
// Include BassMidi headers
#include "bass.h"
#include "bassmidi.h"

// BassMidi integration types
typedef struct GM_SF2Info
{
    HSTREAM         sf2_stream;        // SF2 stream handle
    HSOUNDFONT      sf2_soundfont;     // SF2 soundfont handle
    XBOOL           sf2_active;        // TRUE if SF2 is handling this song
    char            sf2_path[256]; // path to loaded SF2 file
    XFIXED          sf2_master_volume; // master volume scaling
    int16_t         sf2_sample_rate;   // sample rate for SF2 rendering
    int16_t         sf2_max_voices;    // voice limit for SF2
    // Per-channel volume & expression (0..127); initialized to default GM values
    uint8_t         channelVolume[16];
    uint8_t         channelExpression[16];
    // Channel mute states
    XBOOL           channelMuted[16];
} GM_SF2Info;

// Initialize BassMidi support for the mixer
OPErr GM_InitializeSF2(void);
void GM_CleanupSF2(void);

// Load SF2 soundfont for SF2 rendering
OPErr GM_LoadSF2Soundfont(const char* sf2_path);
void GM_UnloadSF2Soundfont(void);

// Check if a song should use SF2 rendering
XBOOL GM_IsSF2Song(GM_Song* pSong);

// Enable/disable BassMidi rendering for a song
OPErr GM_EnableSF2ForSong(GM_Song* pSong, XBOOL enable);

// BassMidi MIDI event processing (called from existing MIDI processors)
void GM_SF2_ProcessNoteOn(GM_Song* pSong, int16_t channel, int16_t note, int16_t velocity);
void GM_SF2_ProcessNoteOff(GM_Song* pSong, int16_t channel, int16_t note, int16_t velocity);
void GM_SF2_ProcessProgramChange(GM_Song* pSong, int16_t channel, int16_t program);
void GM_SF2_ProcessController(GM_Song* pSong, int16_t channel, int16_t controller, int16_t value);
void GM_SF2_ProcessPitchBend(GM_Song* pSong, int16_t channel, int16_t bendMSB, int16_t bendLSB);

// BassMidi audio rendering (called during mixer slice processing)
void GM_SF2_RenderAudioSlice(GM_Song* pSong, int32_t* mixBuffer, int32_t frameCount);

// BassMidi channel management (respects miniBAE mute/solo states)
void GM_SF2_MuteChannel(GM_Song* pSong, int16_t channel);
void GM_SF2_UnmuteChannel(GM_Song* pSong, int16_t channel);
void GM_SF2_AllNotesOff(GM_Song* pSong);
void GM_SF2_AllNotesOffChannel(GM_Song* pSong, int16_t channel);
// Force immediate silence (drop sustain, end or quick-end all active voices). Used for pause.
void GM_SF2_SilenceSong(GM_Song* pSong);

void GM_SF2_StoreRMFInstrumentIDs(uint32_t* rmf_instrumentIDs);

// BassMidi configuration
void GM_SF2_SetMasterVolume(XFIXED volume);
XFIXED GM_SF2_GetMasterVolume(void);
void GM_SF2_SetMaxVoices(int16_t maxVoices);
int16_t GM_SF2_GetMaxVoices(void);
void PV_SF2_SetBankPreset(GM_Song* pSong, int16_t channel, int16_t bank, int16_t preset);
void GM_SF2_SetSampleRate(int32_t sampleRate);
void GM_SF2_SetStereoMode(XBOOL stereo, XBOOL applyNow);

// BassMidi status queries
int16_t GM_SF2_GetActiveVoiceCount(void);
XBOOL GM_SF2_IsActive(void);
void GM_ResetSF2(void);
// Fills channelAmplitudes[16][2] with per-MIDI-channel RMS (L/R) in raw 0..~1.0 float space.
// If mono mode is active, left and right will be identical.
void sf2_get_channel_amplitudes(float channelAmplitudes[16][2]);
void GM_SF2_KillChannelNotes(int ch);
void GM_SF2_KillAllNotes(void);
void GM_SetMixerSF2Mode(XBOOL isSF2);
XBOOL GM_GetMixerSF2Mode(void);

#endif // USE_SF2_SUPPORT

#ifdef __cplusplus
}
#endif

#endif // G_BASSMIDI_H
