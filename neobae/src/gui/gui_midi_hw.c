// gui_midi_hw.c - MIDI hardware integration

#include "gui_midi_hw.h"
#include "gui_midi.h"
#include "NeoBAE.h"

#if SUPPORT_MIDI_HW == TRUE
#include "BAE_API.h"
#include "gui_midi_hw_input.h"
#include "gui_midi_hw_output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL3/SDL.h>
#include <unistd.h>

// Forward declare the BAEGUI structure (defined in gui_main.old.c)
typedef struct
{
    BAEMixer mixer;
    BAESong song;
    BAESound sound;
    uint32_t song_length_us;
    bool song_loaded;
    bool is_audio_file;
    bool is_rmf_file;
    bool paused;
    bool is_playing;
    bool was_playing_before_export;
    bool loop_enabled_gui;
    bool loop_was_enabled_before_export;
    uint32_t position_us_before_export;
    bool audio_engaged_before_export;
    char loaded_path[1024];
    bool preserve_position_on_next_start;
    uint32_t preserved_start_position_us;
    bool song_finished;
    BAEBankToken bank_token;
    char bank_name[256];
    bool bank_loaded;
    char status_message[256];
    Uint32 status_message_time;
} BAEGUI;

// External globals from GUI modules
extern BAEGUI g_bae;
extern BAESong g_live_song;
extern float g_channel_vu[16];
extern float g_channel_peak_level[16];
extern Uint32 g_channel_peak_hold_until[16];
extern Uint32 g_channel_peak_hold_ms;

// Forward declaration
extern void set_status_message(const char *msg);
extern bool g_keyboard_active_notes_by_channel[16][128];

// MIDI state globals
bool g_midi_input_enabled = false;         // enable external MIDI input keyboard
bool g_midi_output_enabled = false;        // enable external MIDI output
int g_midi_input_device_index = 0;         // selected input device index
int g_midi_output_device_index = 0;        // selected output device index
bool g_midi_input_device_dd_open = false;  // dropdown open state
bool g_midi_output_device_dd_open = false; // dropdown open state for output
int g_midi_input_device_count = 0;         // cached input device count
int g_midi_output_device_count = 0;        // cached output device count

// MIDI recording state
bool g_midi_recording = false;               // are we currently recording incoming MIDI?
char g_midi_record_path[1024] = {0};         // final .mid path user requested
char g_midi_record_temp[1024] = {0};         // temporary track data file path
FILE *g_midi_record_temp_fp = NULL;          // temp file for writing raw track events
double g_midi_record_start_ts = 0.0;         // timestamp of first recorded event (seconds)
double g_midi_record_last_ts = 0.0;          // timestamp of last written event (seconds)
uint64_t g_midi_record_last_pc = 0;          // last performance counter for delta timing
double g_midi_perf_freq = 0.0;               // perf counter frequency
const int g_midi_record_division = 1000;     // ticks per quarter note for written MIDI file
const uint32_t g_midi_record_tempo = 500000; // default microseconds per quarter note (120 BPM)
bool g_midi_record_first_event = false;      // for first-event silence capture
uint64_t g_midi_record_start_pc = 0;         // perf counter at record start
SDL_Mutex *g_midi_record_mutex = NULL;       // guard record file writes/close
/* Refer to the single definition in gui_bae.c */
extern double g_last_requested_master_volume;

// MIDI service thread
SDL_Thread *g_midi_service_thread = NULL;
volatile int g_midi_service_quit = 0;

// MIDI device cache
char g_midi_device_name_cache[64][128];
int g_midi_device_api[64];
int g_midi_device_port[64];
int g_midi_device_count = 0;

// Per-channel bank tracking
unsigned char g_midi_bank[16] = {0};
unsigned char g_midi_bank_program[16] = {0};

// MIDI output control
bool g_master_muted_for_midi_out = false;
bool g_midi_output_suppressed_during_seek = false;

// PCM recording state
bool g_pcm_wav_recording = false;
bool g_pcm_mp3_recording = false; // platform MP3 recorder active (MIDI-in)
#if USE_FLAC_ENCODER != FALSE
bool g_pcm_flac_recording = false;
#endif
#if USE_VORBIS_ENCODER == TRUE
bool g_pcm_vorbis_recording = false;
#endif
FILE *g_pcm_wav_fp = NULL;
uint64_t g_pcm_wav_data_bytes = 0;
int g_pcm_wav_channels = 2;
int g_pcm_wav_sample_rate = 44100;
int g_pcm_wav_bits = 16;
bool g_pcm_wav_disengaged_audio = false;

#if USE_FLAC_ENCODER != FALSE
// FLAC recording state
static void *g_pcm_flac_accumulated_samples = NULL;
static uint32_t g_pcm_flac_accumulated_frames = 0;
static uint32_t g_pcm_flac_max_accumulated_frames = 0;
static char g_pcm_flac_output_path[1024] = {0};
#include "FLAC/stream_encoder.h"
#endif

#if USE_VORBIS_ENCODER == TRUE
// Vorbis recording state
static void *g_pcm_vorbis_accumulated_samples = NULL;
static uint32_t g_pcm_vorbis_accumulated_frames = 0;
static uint32_t g_pcm_vorbis_max_accumulated_frames = 0;
static char g_pcm_vorbis_output_path[1024] = {0};
static int g_pcm_vorbis_bitrate = 128000;
#include "vorbis/vorbisenc.h"
#include "ogg/ogg.h"
#endif

// ===== MIDI Event Callback =====

void gui_midi_event_callback(void *threadContext, struct GM_Song *pSong, const unsigned char *midiMessage, int16_t length, uint32_t timeMicroseconds, void *ref)
{
    (void)threadContext;
    (void)pSong;
    (void)timeMicroseconds;
    (void)ref;
    if (!g_midi_output_enabled)
        return;
    if (g_midi_output_suppressed_during_seek)
        return;
    if (!midiMessage || length <= 0)
        return;
    // Send raw bytes to configured RtMidi output
    midi_output_send(midiMessage, length);
}

// ===== MIDI Recording Functions =====

bool midi_record_start(const char *out_path)
{
    if (!g_midi_input_enabled)
        return false;
    if (g_midi_recording)
        return false;
    if (!out_path || !out_path[0])
        return false;
    // Ensure background MIDI service thread is running so recording work is off the UI thread
    if (!g_midi_service_thread)
    {
        midi_service_start();
    }
    // create temp path next to output or in /tmp
    snprintf(g_midi_record_path, sizeof(g_midi_record_path), "%s", out_path);
#ifdef _WIN32
    snprintf(g_midi_record_temp, sizeof(g_midi_record_temp), "%s.tmp", out_path);
#else
    // prefer /tmp for atomic finalization
    snprintf(g_midi_record_temp, sizeof(g_midi_record_temp), "/tmp/neobae_midi_record_%ld.tmp", (long)getpid());
#endif
    if (!g_midi_record_mutex)
        g_midi_record_mutex = SDL_CreateMutex();
    if (g_midi_record_mutex)
        SDL_LockMutex(g_midi_record_mutex);
    g_midi_record_temp_fp = fopen(g_midi_record_temp, "wb+");
    if (!g_midi_record_temp_fp)
    {
        if (g_midi_record_mutex)
            SDL_UnlockMutex(g_midi_record_mutex);
        set_status_message("Failed to open temp file for MIDI record");
        return false;
    }
    // Attach a large buffer to reduce write syscall frequency during high-rate input
    {
        static unsigned char s_buf[256 * 1024];
        setvbuf(g_midi_record_temp_fp, (char *)s_buf, _IOFBF, sizeof(s_buf));
    }
    // reset timers
    g_midi_record_start_ts = 0.0;
    g_midi_record_last_ts = 0.0;
    // initialize performance counter based timing for accurate deltas
    g_midi_perf_freq = (double)SDL_GetPerformanceFrequency();
    g_midi_record_last_pc = SDL_GetPerformanceCounter();
    g_midi_record_start_pc = g_midi_record_last_pc;
    g_midi_record_first_event = true;

    // Write an initial tempo meta-event (delta=0) so the saved MIDI uses our conversion tempo.
    // Format: 00 FF 51 03 tt tt tt
    unsigned char tempo_evt[7];
    tempo_evt[0] = 0x00; // delta 0
    tempo_evt[1] = 0xFF;
    tempo_evt[2] = 0x51;
    tempo_evt[3] = 0x03;
    tempo_evt[4] = (unsigned char)((g_midi_record_tempo >> 16) & 0xFF);
    tempo_evt[5] = (unsigned char)((g_midi_record_tempo >> 8) & 0xFF);
    tempo_evt[6] = (unsigned char)(g_midi_record_tempo & 0xFF);
    fwrite(tempo_evt, 1, sizeof(tempo_evt), g_midi_record_temp_fp);
    // Also write initial Program Change / Bank Select MSB events (delta=0) so the
    // recorded MIDI file reflects the current instrument table in the engine.
    // Use the same target selection as the MIDI service thread.
    {
        BAESong target = g_bae.song ? g_bae.song : g_live_song;
        if (target)
        {
            for (unsigned char ch = 0; ch < 16; ++ch)
            {
                unsigned char program = 0, bank = 0;
                if (BAESong_GetProgramBank(target, ch, &program, &bank, TRUE) == BAE_NO_ERROR)
                {
                    unsigned char evt[4];
                    // Bank Select MSB (CC 0): delta 0 + 0xB0|ch, controller 0, value
                    evt[0] = 0x00; // delta 0
                    evt[1] = (unsigned char)(0xB0 | (ch & 0x0F));
                    evt[2] = 0x00; // controller 0 = bank MSB
                    evt[3] = (unsigned char)(bank & 0x7F);
                    fwrite(evt, 1, 4, g_midi_record_temp_fp);
                    // Program Change: delta 0 + 0xC0|ch, program
                    unsigned char pc[3];
                    pc[0] = 0x00; // delta 0
                    pc[1] = (unsigned char)(0xC0 | (ch & 0x0F));
                    pc[2] = (unsigned char)(program & 0x7F);
                    fwrite(pc, 1, 3, g_midi_record_temp_fp);
                }
            }
        }
    }
    if (g_midi_record_mutex)
        SDL_UnlockMutex(g_midi_record_mutex);

    g_midi_recording = true;
    set_status_message("MIDI recording started");
    return true;
}

// Helper to write final MIDI header and track chunk by merging temp track data
bool midi_record_stop(void)
{
    if (!g_midi_recording)
        return false;
    // Stop further writes from the MIDI thread before closing the file
    g_midi_recording = false;
    if (g_midi_record_mutex)
        SDL_LockMutex(g_midi_record_mutex);
    if (g_midi_record_temp_fp)
        fclose(g_midi_record_temp_fp);
    g_midi_record_temp_fp = NULL;
    if (g_midi_record_mutex)
        SDL_UnlockMutex(g_midi_record_mutex);
    // Open temp for reading and final output for writing
    FILE *tf = fopen(g_midi_record_temp, "rb");
    if (!tf)
    {
        set_status_message("No recorded MIDI data");
        g_midi_recording = false;
        return false;
    }
    FILE *of = fopen(g_midi_record_path, "wb");
    if (!of)
    {
        fclose(tf);
        set_status_message("Failed to create MIDI file");
        g_midi_recording = false;
        return false;
    }
    // Determine track data length (include 4 bytes for appended End-of-Track meta event)
    fseek(tf, 0, SEEK_END);
    long tracklen = ftell(tf);
    fseek(tf, 0, SEEK_SET);
    tracklen += 4; // we'll append 00 FF 2F 00
    // Write standard MIDI header (format 0, 1 track)
    unsigned char header[14];
    // 'MThd' + header length 6 + format 0 + ntrks 1 + division
    memcpy(header, "MThd", 4);
    header[4] = 0x00;
    header[5] = 0x00;
    header[6] = 0x00;
    header[7] = 0x06;
    header[8] = 0x00;
    header[9] = 0x00; // format 0
    header[10] = 0x00;
    header[11] = 0x01; // one track
    header[12] = (unsigned char)((g_midi_record_division >> 8) & 0xFF);
    header[13] = (unsigned char)(g_midi_record_division & 0xFF);
    fwrite(header, 1, sizeof(header), of);
    // Write track chunk header
    unsigned char trkh[8];
    memcpy(trkh, "MTrk", 4);
    uint32_t u32 = (uint32_t)tracklen;
    trkh[4] = (unsigned char)((u32 >> 24) & 0xFF);
    trkh[5] = (unsigned char)((u32 >> 16) & 0xFF);
    trkh[6] = (unsigned char)((u32 >> 8) & 0xFF);
    trkh[7] = (unsigned char)(u32 & 0xFF);
    fwrite(trkh, 1, 8, of);
    // Copy track data
    unsigned char buf[4096];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), tf)) > 0)
        fwrite(buf, 1, r, of);
    // Append All Notes Off (Controller 123) for all 16 channels (delta=0)
    for (unsigned char ch = 0; ch < 16; ++ch)
    {
        unsigned char anof[4];
        anof[0] = 0x00; // delta 0
        anof[1] = (unsigned char)(0xB0 | (ch & 0x0F));
        anof[2] = 0x7B; // Controller 123 = All Notes Off
        anof[3] = 0x00; // value
        fwrite(anof, 1, 4, of);
    }
    // Ensure track ends with End of Track meta event 00 FF 2F 00
    unsigned char eot[4] = {0x00, 0xFF, 0x2F, 0x00};
    fwrite(eot, 1, 4, of);
    fclose(tf);
    fclose(of);
    // Remove temp file
    remove(g_midi_record_temp);
    g_midi_recording = false;
    set_status_message("MIDI recording saved");
    return true;
}

// ===== PCM WAV Functions =====

bool pcm_wav_write_header(FILE *f, int channels, int sample_rate, int bits, uint64_t data_bytes)
{
    if (!f)
        return false;
    // RIFF header with placeholder sizes
    uint32_t byte_rate = sample_rate * channels * (bits / 8);
    uint16_t block_align = channels * (bits / 8);
    // RIFF chunk
    fwrite("RIFF", 1, 4, f);
    uint32_t chunk_size = (uint32_t)(36 + data_bytes);
    fwrite(&chunk_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    // fmt subchunk
    fwrite("fmt ", 1, 4, f);
    uint32_t subchunk1_size = 16;
    fwrite(&subchunk1_size, 4, 1, f);
    uint16_t audio_format = 1;
    fwrite(&audio_format, 2, 1, f); // PCM
    uint16_t num_channels = (uint16_t)channels;
    fwrite(&num_channels, 2, 1, f);
    uint32_t sr = (uint32_t)sample_rate;
    fwrite(&sr, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    uint16_t bits_per_sample = (uint16_t)bits;
    fwrite(&bits_per_sample, 2, 1, f);
    // data subchunk
    fwrite("data", 1, 4, f);
    uint32_t data_size_32 = (uint32_t)data_bytes;
    fwrite(&data_size_32, 4, 1, f);
    return true;
}

bool pcm_wav_start(const char *path, int channels, int sample_rate, int bits)
{
    if (!path)
        return false;
    if (g_pcm_wav_recording)
        return false;
    // Use platform PCM recorder which captures directly from the audio callback
    // and therefore must not disengage audio hardware.
    int res = BAE_Platform_PCMRecorder_Start(path, (uint32_t)channels, (uint32_t)sample_rate, (uint32_t)bits);
    if (res != 0)
        return false;
    g_pcm_wav_channels = channels;
    g_pcm_wav_sample_rate = sample_rate;
    g_pcm_wav_bits = bits;
    g_pcm_wav_data_bytes = 0;
    g_pcm_wav_recording = true;
    g_midi_recording = true;
    g_pcm_wav_disengaged_audio = false; // platform recorder records from callback, no disengage
    set_status_message("WAV recording started");
    return true;
}

void pcm_wav_finalize()
{
    // Stop platform recorder which will finalize the WAV header
    BAE_Platform_PCMRecorder_Stop();
    g_pcm_wav_recording = false;
    g_midi_recording = false;
    g_pcm_wav_data_bytes = 0;
    set_status_message("WAV recording saved");
}

// Append interleaved 16-bit samples
void pcm_wav_write_samples(int16_t *left, int16_t *right, int frames)
{
    if (!g_pcm_wav_fp || !g_pcm_wav_recording)
        return;
    if (frames <= 0)
        return;
    if (g_pcm_wav_channels == 1)
    {
        for (int i = 0; i < frames; i++)
        {
            int16_t s = left ? left[i] : right[i];
            fwrite(&s, 2, 1, g_pcm_wav_fp);
            g_pcm_wav_data_bytes += 2;
        }
    }
    else
    {
        for (int i = 0; i < frames; i++)
        {
            int16_t l = left ? left[i] : 0;
            int16_t r = right ? right[i] : 0;
            fwrite(&l, 2, 1, g_pcm_wav_fp);
            fwrite(&r, 2, 1, g_pcm_wav_fp);
            g_pcm_wav_data_bytes += 4;
        }
    }
}

// ===== MIDI Service Thread =====

int midi_service_thread_fn(void *unused)
{
    (void)unused;
    // Avoid CPU spin: short sleep when idle
    const Uint32 idle_sleep_ms = 2;
    while (!g_midi_service_quit)
    {
        if (!(g_midi_input_enabled))
        {
            SDL_Delay(idle_sleep_ms);
            continue;
        }
        // Ensure a valid target exists
        BAESong target = g_bae.song ? g_bae.song : g_live_song;
        if (!target)
        {
            SDL_Delay(idle_sleep_ms);
            continue;
        }
        // Drain queued MIDI quickly
        unsigned char midi_buf[1024];
        unsigned int midi_sz = 0;
        double midi_ts = 0.0;
        bool had_any = false;
        while (!g_midi_service_quit && midi_input_poll(midi_buf, &midi_sz, &midi_ts))
        {
            had_any = true;
            if (midi_sz < 1)
                continue;
            // If recording is active, write the event to temporary track storage
            if (g_midi_recording && g_midi_record_temp_fp)
            {
                // Use absolute monotonic timestamps captured at input time and compute deltas here.
                // Special-case the very first event to include initial silence from record start.
                double delta_us;
                if (g_midi_record_first_event && g_midi_perf_freq > 0.0)
                {
                    uint64_t pc = SDL_GetPerformanceCounter();
                    if (g_midi_record_start_pc == 0)
                        g_midi_record_start_pc = pc;
                    delta_us = ((double)(pc - g_midi_record_start_pc) / g_midi_perf_freq) * 1000000.0;
                    g_midi_record_last_pc = pc;
                    g_midi_record_first_event = false;
                    // Anchor last-ts baseline for subsequent events
                    if (midi_ts > 0.0)
                        g_midi_record_last_ts = midi_ts;
                }
                else if (midi_ts > 0.0 && g_midi_record_last_ts > 0.0)
                {
                    double dsec = midi_ts - g_midi_record_last_ts;
                    if (dsec < 0.0)
                        dsec = 0.0; // guard clock anomalies
                    delta_us = dsec * 1000000.0;
                    g_midi_record_last_ts = midi_ts;
                    g_midi_record_last_pc = SDL_GetPerformanceCounter();
                }
                else if (g_midi_perf_freq > 0.0)
                {
                    uint64_t pc = SDL_GetPerformanceCounter();
                    if (g_midi_record_last_pc == 0)
                        g_midi_record_last_pc = pc;
                    delta_us = ((double)(pc - g_midi_record_last_pc) / g_midi_perf_freq) * 1000000.0;
                    g_midi_record_last_pc = pc;
                }
                else
                {
                    static Uint32 s_last_ms = 0;
                    Uint32 now_ms = SDL_GetTicks();
                    if (s_last_ms == 0)
                        s_last_ms = now_ms;
                    Uint32 dms = now_ms - s_last_ms;
                    s_last_ms = now_ms;
                    delta_us = (double)dms * 1000.0;
                }

                if (delta_us < 0.0)
                    delta_us = 0.0;
                // Convert delta_us to ticks using division and tempo (us/qn)
                uint32_t delta_ticks = (uint32_t)((delta_us * (double)g_midi_record_division) / (double)g_midi_record_tempo + 0.5);
                // Write VLQ delta
                uint32_t v = delta_ticks;
                unsigned char vlq[5];
                int vlq_len = 0;
                unsigned char tmp[5];
                int tmp_len = 0;
                tmp[tmp_len++] = (unsigned char)(v & 0x7F);
                v >>= 7;
                while (v)
                {
                    tmp[tmp_len++] = (unsigned char)(0x80 | (v & 0x7F));
                    v >>= 7;
                }
                for (int i = tmp_len - 1; i >= 0; --i)
                    vlq[vlq_len++] = tmp[i];
                if (g_midi_record_mutex)
                    SDL_LockMutex(g_midi_record_mutex);
                if (g_midi_record_temp_fp)
                {
                    fwrite(vlq, 1, vlq_len, g_midi_record_temp_fp);
                    // Write MIDI message bytes (buffered; no per-event flush to avoid stalls)
                    fwrite(midi_buf, 1, midi_sz, g_midi_record_temp_fp);
                }
                if (g_midi_record_mutex)
                    SDL_UnlockMutex(g_midi_record_mutex);
            }
            unsigned char status = midi_buf[0];
            unsigned char mtype = status & 0xF0;
            unsigned char mch = status & 0x0F;

// Mirror to external MIDI out if enabled
#define FORWARD_OUT_T(buf, len)             \
    do                                      \
    {                                       \
        if (g_midi_output_enabled)          \
            midi_output_send((buf), (len)); \
    } while (0)

            switch (mtype)
            {
            case 0x80: // Note Off
                if (midi_sz >= 3)
                {
                    unsigned char note = midi_buf[1];
                    unsigned char vel = midi_buf[2];
                    // Always forward NoteOff to engine to prevent stuck notes even if channel currently muted
                    BAESong_NoteOff(target, (unsigned char)mch, note, 0, 0);
                    unsigned char out[3] = {(unsigned char)(0x80 | (mch & 0x0F)), note, vel};
                    FORWARD_OUT_T(out, 3);
                    g_keyboard_active_notes_by_channel[mch][note] = 0;
                }
                break;
            case 0x90: // Note On (or velocity 0 => off)
                if (midi_sz >= 3)
                {
                    unsigned char note = midi_buf[1];
                    unsigned char vel = midi_buf[2];
                    if (vel)
                    {
                        if (g_thread_ch_enabled[mch])
                        {
                            BAESong_NoteOnWithLoad(target, (unsigned char)mch, note, vel, 0);
                            g_keyboard_active_notes_by_channel[mch][note] = 1;
                            float lvl_in = (float)vel / 127.0f;
                            if (lvl_in > g_channel_vu[mch])
                                g_channel_vu[mch] = lvl_in;
                            if (lvl_in > g_channel_peak_level[mch])
                            {
                                g_channel_peak_level[mch] = lvl_in;
                                g_channel_peak_hold_until[mch] = SDL_GetTicks() + g_channel_peak_hold_ms;
                            }
                        }
                        unsigned char out[3] = {(unsigned char)(0x90 | (mch & 0x0F)), note, vel};
                        FORWARD_OUT_T(out, 3);
                    }
                    else
                    {
                        // Velocity 0 Note On == Note Off — always deliver to engine
                        BAESong_NoteOff(target, (unsigned char)mch, note, 0, 0);
                        unsigned char out[3] = {(unsigned char)(0x80 | (mch & 0x0F)), note, 0};
                        FORWARD_OUT_T(out, 3);
                        g_keyboard_active_notes_by_channel[mch][note] = 0;
                    }
                }
                break;
            case 0xA0: // poly aftertouch
                if (midi_sz >= 3)
                {
                    unsigned char note = midi_buf[1];
                    unsigned char pressure = midi_buf[2];
                    if (g_thread_ch_enabled[mch])
                    {
                        BAESong_KeyPressure(target, (unsigned char)mch, note, pressure, 0);
                    }
                    unsigned char out[3] = {(unsigned char)(0xA0 | (mch & 0x0F)), note, pressure};
                    FORWARD_OUT_T(out, 3);
                }
                break;
            case 0xB0: // CC
                if (midi_sz >= 3)
                {
                    unsigned char cc = midi_buf[1];
                    unsigned char val = midi_buf[2];
                    if (cc == 0)
                        g_midi_bank[mch] = val;
                    else if (cc == 32)
                        g_midi_bank_program[mch] = val;
                    // Always route All Notes Off / All Sound Off regardless of mute state to prevent hangs
                    if (cc == 120 || cc == 123)
                    {
                        BAESong_ControlChange(target, (unsigned char)mch, cc, val, 0);
                    }
                    else if (g_thread_ch_enabled[mch])
                    {
                        BAESong_ControlChange(target, (unsigned char)mch, cc, val, 0);
                    }
                    unsigned char out[3] = {(unsigned char)(0xB0 | (mch & 0x0F)), cc, val};
                    FORWARD_OUT_T(out, 3);
                }
                break;
            case 0xC0: // Program Change
                if (midi_sz >= 2)
                {
                    unsigned char prog = midi_buf[1];
                    if (g_thread_ch_enabled[mch])
                    {
                        BAESong_ProgramChange(target, (unsigned char)mch, prog, 0);
                    }
                    unsigned char out[2] = {(unsigned char)(0xC0 | (mch & 0x0F)), prog};
                    FORWARD_OUT_T(out, 2);
                }
                break;
            case 0xD0: // Channel pressure
                if (midi_sz >= 2)
                {
                    unsigned char press = midi_buf[1];
                    if (g_thread_ch_enabled[mch])
                    {
                        BAESong_ChannelPressure(target, (unsigned char)mch, press, 0);
                    }
                    unsigned char out[2] = {(unsigned char)(0xD0 | (mch & 0x0F)), press};
                    FORWARD_OUT_T(out, 2);
                }
                break;
            case 0xE0: // Pitch bend
                if (midi_sz >= 3)
                {
                    unsigned char lsb = midi_buf[1];
                    unsigned char msb = midi_buf[2];
                    if (g_thread_ch_enabled[mch])
                    {
                        BAESong_PitchBend(target, (unsigned char)mch, lsb, msb, 0);
                    }
                    unsigned char out[3] = {(unsigned char)(0xE0 | (mch & 0x0F)), lsb, msb};
                    FORWARD_OUT_T(out, 3);
                }
                break;
            default:
                // Ignore system messages here; rtmidi_in_ignore_types filtered realtime already.
                break;
            }
        }
        if (!had_any)
            SDL_Delay(idle_sleep_ms);
    }
    return 0;
}

void midi_service_start(void)
{
    if (g_midi_service_thread)
        return;
    g_midi_service_quit = 0;
    g_midi_service_thread = SDL_CreateThread(midi_service_thread_fn, "midi_svc", NULL);
}

void midi_service_stop(void)
{
    if (!g_midi_service_thread)
        return;
    g_midi_service_quit = 1;
    SDL_WaitThread(g_midi_service_thread, NULL);
    g_midi_service_thread = NULL;
}

#if USE_FLAC_ENCODER != FALSE
// FLAC recording functions
bool pcm_flac_start(const char *path, int channels, int sample_rate, int bits)
{
    BAE_PRINTF("FLAC recording start attempt: %s (%d Hz, %d ch, %d bits)\n", path, sample_rate, channels, bits);

    if (!path)
    {
        BAE_PRINTF("FLAC recording: null path\n");
        return false;
    }

    if (g_pcm_flac_recording)
    {
        BAE_PRINTF("FLAC recording: already recording\n");
        return false;
    }

    // Store recording parameters
    g_pcm_wav_channels = channels;
    g_pcm_wav_sample_rate = sample_rate;
    g_pcm_wav_bits = bits;

    strncpy(g_pcm_flac_output_path, path, sizeof(g_pcm_flac_output_path) - 1);
    g_pcm_flac_output_path[sizeof(g_pcm_flac_output_path) - 1] = '\0';

    // Allocate buffer for accumulating samples (2 minutes max)
    g_pcm_flac_max_accumulated_frames = sample_rate * 120; // 2 minutes
    g_pcm_flac_accumulated_samples = malloc(g_pcm_flac_max_accumulated_frames * channels * (bits / 8));
    if (!g_pcm_flac_accumulated_samples)
    {
        BAE_PRINTF("FLAC recording: failed to allocate %u bytes for buffer\n",
                   (unsigned)(g_pcm_flac_max_accumulated_frames * channels * (bits / 8)));
        return false;
    }

    g_pcm_flac_accumulated_frames = 0;
    g_pcm_flac_recording = true;

    // Register the callback to capture audio from the audio callback
    BAE_Platform_SetFlacRecorderCallback(pcm_flac_write_samples);

    set_status_message("FLAC recording started");
    BAE_PRINTF("FLAC recording started: %s (%d Hz, %d ch, %d bits)\n", path, sample_rate, channels, bits);
    return true;
}

void pcm_flac_finalize(void)
{
    if (!g_pcm_flac_recording || !g_pcm_flac_accumulated_samples)
        return;

    // Create FLAC encoder and encode all accumulated data
    FLAC__StreamEncoder *encoder = FLAC__stream_encoder_new();
    if (encoder)
    {
        // Configure encoder
        FLAC__stream_encoder_set_verify(encoder, true);
        FLAC__stream_encoder_set_compression_level(encoder, 5);
        FLAC__stream_encoder_set_channels(encoder, g_pcm_wav_channels);
        FLAC__stream_encoder_set_bits_per_sample(encoder, g_pcm_wav_bits);
        FLAC__stream_encoder_set_sample_rate(encoder, g_pcm_wav_sample_rate);
        FLAC__stream_encoder_set_total_samples_estimate(encoder, g_pcm_flac_accumulated_frames);

        // Initialize encoder to write to file
        FLAC__StreamEncoderInitStatus init_status = FLAC__stream_encoder_init_file(encoder, g_pcm_flac_output_path, NULL, NULL);
        if (init_status == FLAC__STREAM_ENCODER_INIT_STATUS_OK)
        {
            // Process all accumulated samples
            if (g_pcm_wav_bits == 16)
            {
                // Convert 16-bit samples to 32-bit for FLAC
                uint32_t frames_to_process = g_pcm_flac_accumulated_frames;
                const int16_t *src = (const int16_t *)g_pcm_flac_accumulated_samples;

                // Process in chunks to avoid large stack allocation
                const uint32_t chunk_size = 4096;
                FLAC__int32 *buffer = (FLAC__int32 *)malloc(chunk_size * g_pcm_wav_channels * sizeof(FLAC__int32));
                if (buffer)
                {
                    uint32_t frames_processed = 0;
                    while (frames_processed < frames_to_process)
                    {
                        uint32_t frames_this_chunk = frames_to_process - frames_processed;
                        if (frames_this_chunk > chunk_size)
                            frames_this_chunk = chunk_size;

                        // Convert 16-bit to 32-bit
                        for (uint32_t i = 0; i < frames_this_chunk * g_pcm_wav_channels; i++)
                        {
                            buffer[i] = (FLAC__int32)src[frames_processed * g_pcm_wav_channels + i];
                        }

                        // Encode chunk
                        FLAC__stream_encoder_process_interleaved(encoder, buffer, frames_this_chunk);
                        frames_processed += frames_this_chunk;
                    }
                    free(buffer);
                }
            }

            FLAC__stream_encoder_finish(encoder);
            set_status_message("FLAC recording saved");
        }
        else
        {
            set_status_message("FLAC encoding failed");
        }

        FLAC__stream_encoder_delete(encoder);
    }

    // Clear the audio callback
    BAE_Platform_ClearFlacRecorderCallback();

    // Clean up
    if (g_pcm_flac_accumulated_samples)
    {
        free(g_pcm_flac_accumulated_samples);
        g_pcm_flac_accumulated_samples = NULL;
    }
    g_pcm_flac_accumulated_frames = 0;
    g_pcm_flac_recording = false;
    g_midi_recording = false;
}

void pcm_flac_write_samples(int16_t *left, int16_t *right, int frames)
{
    if (!g_pcm_flac_recording || !g_pcm_flac_accumulated_samples || frames <= 0)
        return;

    // Check if we have room in the accumulation buffer
    if (g_pcm_flac_accumulated_frames + frames > g_pcm_flac_max_accumulated_frames)
    {
        // Buffer overflow - just ignore the extra samples
        static int warned = 0;
        if (!warned)
        {
            set_status_message("FLAC buffer full, recording may be truncated");
            warned = 1;
        }
        return;
    }

    // Append samples to accumulation buffer
    int16_t *dest = (int16_t *)g_pcm_flac_accumulated_samples +
                    (g_pcm_flac_accumulated_frames * g_pcm_wav_channels);

    if (g_pcm_wav_channels == 1)
    {
        // Mono - use left channel if available, otherwise right
        int16_t *src = left ? left : right;
        for (int i = 0; i < frames; i++)
        {
            dest[i] = src[i];
        }
    }
    else
    {
        // Stereo
        for (int i = 0; i < frames; i++)
        {
            dest[i * 2] = left ? left[i] : 0;
            dest[i * 2 + 1] = right ? right[i] : 0;
        }
    }

    g_pcm_flac_accumulated_frames += frames;
}
#endif // USE_FLAC_ENCODER

#if USE_VORBIS_ENCODER == TRUE
// Vorbis recording functions
bool pcm_vorbis_start(const char *path, int channels, int sample_rate, int bits, int bitrate)
{
    BAE_PRINTF("Vorbis recording start attempt: %s (%d Hz, %d ch, %d bits, %d bps)\n", path, sample_rate, channels, bits, bitrate);

    if (!path)
    {
        BAE_PRINTF("Vorbis recording: null path\n");
        return false;
    }

    if (g_pcm_vorbis_recording)
    {
        BAE_PRINTF("Vorbis recording: already recording\n");
        return false;
    }

    // Store recording parameters
    g_pcm_wav_channels = channels;
    g_pcm_wav_sample_rate = sample_rate;
    g_pcm_wav_bits = bits;
    g_pcm_vorbis_bitrate = bitrate;

    strncpy(g_pcm_vorbis_output_path, path, sizeof(g_pcm_vorbis_output_path) - 1);
    g_pcm_vorbis_output_path[sizeof(g_pcm_vorbis_output_path) - 1] = '\0';

    // Allocate buffer for accumulating samples (2 minutes max)
    g_pcm_vorbis_max_accumulated_frames = sample_rate * 120; // 2 minutes
    g_pcm_vorbis_accumulated_samples = malloc(g_pcm_vorbis_max_accumulated_frames * channels * (bits / 8));
    if (!g_pcm_vorbis_accumulated_samples)
    {
        BAE_PRINTF("Vorbis recording: failed to allocate %u bytes for buffer\n",
                   (unsigned)(g_pcm_vorbis_max_accumulated_frames * channels * (bits / 8)));
        return false;
    }

    g_pcm_vorbis_accumulated_frames = 0;
    g_pcm_vorbis_recording = true;

    // Register the callback to capture audio from the audio callback
    BAE_Platform_SetVorbisRecorderCallback(pcm_vorbis_write_samples);

    set_status_message("Vorbis recording started");
    BAE_PRINTF("Vorbis recording started: %s (%d Hz, %d ch, %d bits, %d bps)\n", path, sample_rate, channels, bits, bitrate);
    return true;
}

void pcm_vorbis_finalize(void)
{
    if (!g_pcm_vorbis_recording || !g_pcm_vorbis_accumulated_samples)
        return;

    BAE_PRINTF("Vorbis finalize: %u frames accumulated\n", g_pcm_vorbis_accumulated_frames);

    if (g_pcm_vorbis_accumulated_frames == 0)
    {
        set_status_message("No Vorbis audio data to save");
        BAE_Platform_ClearVorbisRecorderCallback();
        if (g_pcm_vorbis_accumulated_samples)
        {
            free(g_pcm_vorbis_accumulated_samples);
            g_pcm_vorbis_accumulated_samples = NULL;
        }
        g_pcm_vorbis_recording = false;
        g_midi_recording = false;
        return;
    }

    // Create Vorbis encoder and encode all accumulated data
    vorbis_info vi;
    vorbis_comment vc;
    vorbis_dsp_state vd;
    vorbis_block vb;

    vorbis_info_init(&vi);
    
    // Initialize Vorbis encoder with VBR mode
    int ret = vorbis_encode_init(&vi, g_pcm_wav_channels, g_pcm_wav_sample_rate, -1, g_pcm_vorbis_bitrate, -1);
    if (ret == 0)
    {
        vorbis_comment_init(&vc);
        vorbis_comment_add_tag(&vc, "ENCODER", "NeoBAE");

        vorbis_analysis_init(&vd, &vi);
        vorbis_block_init(&vd, &vb);

        // Open output file
        FILE *fp = fopen(g_pcm_vorbis_output_path, "wb");
        if (fp)
        {
            ogg_stream_state os;
            ogg_page og;
            ogg_packet header_main, header_comments, header_codebooks;

            ogg_stream_init(&os, rand());

            // Write Vorbis headers - need separate packets for each header
            vorbis_analysis_headerout(&vd, &vc, &header_main, &header_comments, &header_codebooks);
            
            // Write the three headers to the Ogg stream
            ogg_stream_packetin(&os, &header_main);
            ogg_stream_packetin(&os, &header_comments);
            ogg_stream_packetin(&os, &header_codebooks);

            // Flush headers to file
            while (ogg_stream_flush(&os, &og) != 0)
            {
                fwrite(og.header, 1, og.header_len, fp);
                fwrite(og.body, 1, og.body_len, fp);
            }

            // Process all accumulated samples
            if (g_pcm_wav_bits == 16)
            {
                const int16_t *src = (const int16_t *)g_pcm_vorbis_accumulated_samples;
                uint32_t frames_to_process = g_pcm_vorbis_accumulated_frames;

                // Process in chunks
                const uint32_t chunk_size = 4096;
                uint32_t frames_processed = 0;

                while (frames_processed < frames_to_process)
                {
                    uint32_t frames_this_chunk = frames_to_process - frames_processed;
                    if (frames_this_chunk > chunk_size)
                        frames_this_chunk = chunk_size;

                    float **buffer = vorbis_analysis_buffer(&vd, frames_this_chunk);
                    
                    // Convert 16-bit samples to float
                    for (uint32_t i = 0; i < frames_this_chunk; i++)
                    {
                        for (int ch = 0; ch < g_pcm_wav_channels; ch++)
                        {
                            buffer[ch][i] = src[(frames_processed + i) * g_pcm_wav_channels + ch] / 32768.0f;
                        }
                    }

                    vorbis_analysis_wrote(&vd, frames_this_chunk);
                    frames_processed += frames_this_chunk;

                    // Encode and write packets
                    while (vorbis_analysis_blockout(&vd, &vb) == 1)
                    {
                        vorbis_analysis(&vb, NULL);
                        vorbis_bitrate_addblock(&vb);

                        ogg_packet op;
                        while (vorbis_bitrate_flushpacket(&vd, &op))
                        {
                            ogg_stream_packetin(&os, &op);

                            while (ogg_stream_pageout(&os, &og) != 0)
                            {
                                fwrite(og.header, 1, og.header_len, fp);
                                fwrite(og.body, 1, og.body_len, fp);
                            }
                        }
                    }
                }

                // Signal end of data
                vorbis_analysis_wrote(&vd, 0);

                // Flush remaining packets
                while (vorbis_analysis_blockout(&vd, &vb) == 1)
                {
                    vorbis_analysis(&vb, NULL);
                    vorbis_bitrate_addblock(&vb);

                    ogg_packet op;
                    while (vorbis_bitrate_flushpacket(&vd, &op))
                    {
                        ogg_stream_packetin(&os, &op);

                        while (ogg_stream_pageout(&os, &og) != 0)
                        {
                            fwrite(og.header, 1, og.header_len, fp);
                            fwrite(og.body, 1, og.body_len, fp);
                        }
                    }
                }

                // Flush remaining pages
                while (ogg_stream_flush(&os, &og) != 0)
                {
                    fwrite(og.header, 1, og.header_len, fp);
                    fwrite(og.body, 1, og.body_len, fp);
                }
            }

            ogg_stream_clear(&os);
            fclose(fp);
            set_status_message("Vorbis recording saved");
        }
        else
        {
            set_status_message("Vorbis file creation failed");
        }

        vorbis_block_clear(&vb);
        vorbis_dsp_clear(&vd);
        vorbis_comment_clear(&vc);
    }
    else
    {
        set_status_message("Vorbis encoder initialization failed");
    }

    vorbis_info_clear(&vi);

    // Clear the audio callback
    BAE_Platform_ClearVorbisRecorderCallback();

    // Clean up
    if (g_pcm_vorbis_accumulated_samples)
    {
        free(g_pcm_vorbis_accumulated_samples);
        g_pcm_vorbis_accumulated_samples = NULL;
    }
    g_pcm_vorbis_accumulated_frames = 0;
    g_pcm_vorbis_recording = false;
    g_midi_recording = false;
}

void pcm_vorbis_write_samples(int16_t *left, int16_t *right, int frames)
{
    if (!g_pcm_vorbis_recording || !g_pcm_vorbis_accumulated_samples || frames <= 0)
        return;

    // Check if we have room in the accumulation buffer
    if (g_pcm_vorbis_accumulated_frames + frames > g_pcm_vorbis_max_accumulated_frames)
    {
        // Buffer overflow - just ignore the extra samples
        static int warned = 0;
        if (!warned)
        {
            set_status_message("Vorbis buffer full, recording may be truncated");
            warned = 1;
        }
        return;
    }

    // Append samples to accumulation buffer
    int16_t *dest = (int16_t *)g_pcm_vorbis_accumulated_samples +
                    (g_pcm_vorbis_accumulated_frames * g_pcm_wav_channels);

    if (g_pcm_wav_channels == 1)
    {
        // Mono - use left channel if available, otherwise right
        int16_t *src = left ? left : right;
        for (int i = 0; i < frames; i++)
        {
            dest[i] = src[i];
        }
    }
    else
    {
        // Stereo
        for (int i = 0; i < frames; i++)
        {
            dest[i * 2] = left ? left[i] : 0;
            dest[i * 2 + 1] = right ? right[i] : 0;
        }
    }

    g_pcm_vorbis_accumulated_frames += frames;
}
#endif // USE_VORBIS_ENCODER

#endif // SUPPORT_MIDI_HW
