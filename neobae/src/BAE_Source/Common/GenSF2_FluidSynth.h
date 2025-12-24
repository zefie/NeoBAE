/****************************************************************************
 *
 * GenSF2_FluidSynth.h
 *
 * FluidSynth integration for NeoBAE
 * Provides SF2 soundfont support through FluidSynth when USE_SF2_SUPPORT is enabled
 *
 ****************************************************************************/

#ifndef G_FLUIDSYNTH_H
#define G_FLUIDSYNTH_H

#include "X_API.h"
#include "GenSnd.h"
#include "NeoBAE.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

#if USE_SF2_SUPPORT == TRUE && _USING_FLUIDSYNTH == TRUE
// Include FluidSynth headers
#include "fluidsynth.h"

// FluidSynth integration types
typedef struct GM_SF2Info
{
    fluid_synth_t*      sf2_synth;         // FluidSynth synthesizer handle
    fluid_settings_t*   sf2_settings;     // FluidSynth settings handle
    int                 sf2_soundfont_id; // FluidSynth soundfont ID
    XBOOL               sf2_active;        // TRUE if SF2 is handling this song
    char                sf2_path[256];     // path to loaded SF2 file
    XFIXED              sf2_master_volume; // master volume scaling
    int16_t             sf2_sample_rate;   // sample rate for SF2 rendering
    int16_t             sf2_max_voices;    // voice limit for SF2
    // Per-channel volume & expression (0..127); initialized to default GM values
    uint8_t             channelVolume[16];
    uint8_t             channelExpression[16];
    // Per-channel reverb & chorus send levels (0..127); CC 91 and CC 93
    uint8_t             channelReverb[16];
    uint8_t             channelChorus[16];
    // Channel mute states
    XBOOL               channelMuted[16];
} GM_SF2Info;

// Initialize FluidSynth support for the mixer
OPErr GM_InitializeSF2(void);
void GM_CleanupSF2(void);

// FluidSynth mixer mode management
void GM_SetMixerSF2Mode(XBOOL isSF2);
XBOOL GM_GetMixerSF2Mode(void);

// Load SF2 soundfont for FluidSynth rendering
OPErr GM_LoadSF2SoundfontFromMemory(const unsigned char *data, size_t size);
OPErr GM_LoadSF2Soundfont(const char* sf2_path);
void GM_UnloadSF2Soundfont(void);

// XMF overlay soundfont management
OPErr GM_LoadSF2SoundfontAsXMFOverlay(const unsigned char *data, size_t size);
void GM_UnloadXMFOverlaySoundFont(void);

bool is_libinstpatch_loaded(void);

// Check if a song should use FluidSynth rendering
XBOOL GM_IsSF2Song(GM_Song* pSong);

// Enable/disable FluidSynth rendering for a song
OPErr GM_EnableSF2ForSong(GM_Song* pSong, XBOOL enable);

void GM_SF2_SetGain(float volume);
float GM_SF2_GetGain();

// FluidSynth MIDI event processing (called from existing MIDI processors)
void GM_SF2_ProcessNoteOn(GM_Song* pSong, int16_t channel, int16_t note, int16_t velocity);
void GM_SF2_ProcessNoteOff(GM_Song* pSong, int16_t channel, int16_t note, int16_t velocity);
void GM_SF2_ProcessProgramChange(GM_Song* pSong, int16_t channel, int32_t program);
void GM_SF2_ProcessController(GM_Song* pSong, int16_t channel, int16_t controller, int16_t value);
void GM_SF2_ProcessPitchBend(GM_Song* pSong, int16_t channel, int16_t bendMSB, int16_t bendLSB);
void GM_SF2_ProcessSysEx(GM_Song* pSong, const unsigned char* message, int32_t length);

// FluidSynth audio rendering (called during mixer slice processing)
void GM_SF2_RenderAudioSlice(GM_Song* pSong, int32_t* mixBuffer, int32_t* reverbBuffer, int32_t* chorusBuffer, int32_t frameCount);

// FluidSynth channel management (respects NeoBAE mute/solo states)
void GM_SF2_MuteChannel(GM_Song* pSong, int16_t channel);
void GM_SF2_UnmuteChannel(GM_Song* pSong, int16_t channel);
void GM_SF2_KillChannelNotes(int16_t channel);
void GM_SF2_AllNotesOff(GM_Song* pSong);
void GM_SF2_AllNotesOffChannel(GM_Song* pSong, int16_t channel);
void GM_SF2_SilenceSong(GM_Song* pSong);

// FluidSynth configuration
void GM_SF2_SetMasterVolume(XFIXED volume);
XFIXED GM_SF2_GetMasterVolume(void);
int16_t GM_SF2_GetMaxVoices(void);
void GM_SF2_SetStereoMode(XBOOL stereo, XBOOL applyNow);
void GM_SF2_SetSampleRate(int32_t sampleRate);
void GM_SF2_KillAllNotes(void);

// FluidSynth status queries
uint16_t GM_SF2_GetActiveVoiceCount(void);
XBOOL GM_SF2_IsActive(void);

// FluidSynth reset
BAEResult GM_ResetSF2(void);
BAEResult GM_SoftResetSF2(void);

// FluidSynth channel amplitude monitoring
void sf2_get_channel_amplitudes(float channelAmplitudes[16][2]);

// Private helper functions
void GM_SF2_SetDefaultControllers(int16_t channel);
void PV_SF2_SetBankPreset(GM_Song* pSong, int16_t channel, int16_t bank, int16_t preset);

// Query whether the currently loaded soundfont exposes any presets at all.
// Optionally returns the counted number of presets.
XBOOL GM_SF2_CurrentFontHasAnyPreset(int *outPresetCount);
XBOOL GM_SF2_HasXmfEmbeddedBank();
XBOOL GM_SF2_XmfOverlayHasPreset(int bank, int program);
void GM_SF2_SetChannelBankAndProgram(int16_t channel, int16_t bank, int16_t program);
XBOOL GM_SF2_isDLS(void);
#endif // USE_SF2_SUPPORT

#ifdef __cplusplus
}
#endif

#endif // G_FLUIDSYNTH_H
