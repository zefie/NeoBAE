#ifndef GUI_MIDI_HW_H
#define GUI_MIDI_HW_H

#include "gui_common.h"
#include "gui_midi_hw_input.h"
#include "gui_midi_hw_output.h"
#include "MiniBAE.h"

#ifdef SUPPORT_MIDI_HW
#include "rtmidi_c.h"

// MIDI state globals
extern bool g_midi_input_enabled;
extern bool g_midi_output_enabled;
extern int g_midi_input_device_index;
extern int g_midi_output_device_index;
extern bool g_midi_input_device_dd_open;
extern bool g_midi_output_device_dd_open;
extern int g_midi_input_device_count;
extern int g_midi_output_device_count;

// MIDI recording state
extern bool g_midi_recording;
extern char g_midi_record_path[1024];
extern char g_midi_record_temp[1024];
extern FILE *g_midi_record_temp_fp;
extern double g_midi_record_start_ts;
extern double g_midi_record_last_ts;
extern uint64_t g_midi_record_last_pc;
extern double g_midi_perf_freq;
extern const int g_midi_record_division;
extern const uint32_t g_midi_record_tempo;
extern bool g_midi_record_first_event;
extern uint64_t g_midi_record_start_pc;
extern SDL_Mutex *g_midi_record_mutex;

// MIDI service thread
extern SDL_Thread *g_midi_service_thread;
extern volatile int g_midi_service_quit;

// MIDI device cache
extern char g_midi_device_name_cache[64][128];
extern int g_midi_device_api[64];
extern int g_midi_device_port[64];
extern int g_midi_device_count;

// Per-channel bank tracking
extern unsigned char g_midi_bank_msb[16];
extern unsigned char g_midi_bank_program[16];

// MIDI output control
extern bool g_master_muted_for_midi_out;
extern bool g_midi_output_suppressed_during_seek;

// PCM recording state
extern bool g_pcm_wav_recording;
extern bool g_pcm_mp3_recording;
#if USE_FLAC_ENCODER != FALSE
extern bool g_pcm_flac_recording;
#endif
#if USE_VORBIS_ENCODER == TRUE
extern bool g_pcm_vorbis_recording;
#endif
extern FILE *g_pcm_wav_fp;
extern uint64_t g_pcm_wav_data_bytes;
extern int g_pcm_wav_channels;
extern int g_pcm_wav_sample_rate;
extern int g_pcm_wav_bits;
extern bool g_pcm_wav_disengaged_audio;

// Function declarations
void gui_midi_event_callback(void *threadContext, struct GM_Song *pSong, const unsigned char *midiMessage, int16_t length, uint32_t timeMicroseconds, void *ref);

int midi_service_thread_fn(void *unused);
void midi_service_start(void);
void midi_service_stop(void);

bool midi_record_start(const char *out_path);
bool midi_record_stop(void);

bool pcm_wav_write_header(FILE *f, int channels, int sample_rate, int bits, uint64_t data_bytes);
bool pcm_wav_start(const char *path, int channels, int sample_rate, int bits);
void pcm_wav_finalize(void);
void pcm_wav_write_samples(int16_t *left, int16_t *right, int frames);

#if USE_FLAC_ENCODER != FALSE
bool pcm_flac_start(const char *path, int channels, int sample_rate, int bits);
void pcm_flac_finalize(void);
void pcm_flac_write_samples(int16_t *left, int16_t *right, int frames);
#endif

#if USE_VORBIS_ENCODER == TRUE
bool pcm_vorbis_start(const char *path, int channels, int sample_rate, int bits, int bitrate);
void pcm_vorbis_finalize(void);
void pcm_vorbis_write_samples(int16_t *left, int16_t *right, int frames);
#endif

#endif // SUPPORT_MIDI_HW

#endif // GUI_MIDI_HW_H
