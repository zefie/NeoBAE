// Simple MIDI input using RtMidi C wrapper. Non-blocking poll model.

#include "gui_midi_hw_input.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>
#ifndef _WIN32
#include <sys/time.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#endif

#include "rtmidi_c.h"

#define QUEUE_CAPACITY 16384u /* power-of-two ring buffer (larger to prevent overflow under bursts) */
#define QUEUE_MASK (QUEUE_CAPACITY - 1u)
#define MAX_MSG_SIZE 1024u

/* Ensure capacity is power of two for mask indexing */
_Static_assert((QUEUE_CAPACITY & (QUEUE_CAPACITY - 1u)) == 0, "QUEUE_CAPACITY must be power of two");

typedef struct MidiEvent
{
    double timestamp;
    unsigned int size;
    unsigned char data[MAX_MSG_SIZE];
} MidiEvent;

static RtMidiInPtr g_rtmidi = NULL;
static MidiEvent g_queue[QUEUE_CAPACITY];
/* Use 64-bit counters to avoid ABA within lifetime */
static atomic_ulong g_q_head = 0; /* consumer index */
static atomic_ulong g_q_tail = 0; /* producer index */
static atomic_uint g_drop_count = 0;

// Monotonic high-resolution timestamp in seconds for robust inter-event timing
static double midi_now_seconds(void)
{
#ifdef _WIN32
    static LARGE_INTEGER s_freq = {0};
    LARGE_INTEGER pc;
    if (s_freq.QuadPart == 0)
    {
        QueryPerformanceFrequency(&s_freq);
        if (s_freq.QuadPart == 0)
        {
            // Fallback: use timeGetTime-ish granularity via clock()
            return (double)clock() / (double)CLOCKS_PER_SEC;
        }
    }
    QueryPerformanceCounter(&pc);
    return (double)pc.QuadPart / (double)s_freq.QuadPart;
#else
    // Prefer monotonic clocks when available; otherwise fall back to gettimeofday
#if defined(CLOCK_MONOTONIC_RAW)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
#endif
#endif
}

/* Callback runs on RtMidi's thread: single producer */
static void midi_ccallback(double timeStamp, const unsigned char *message, size_t messageSize, void *userData)
{
    (void)userData;
    if (messageSize == 0)
        return;
    unsigned long tail = atomic_load_explicit(&g_q_tail, memory_order_relaxed);
    unsigned long head = atomic_load_explicit(&g_q_head, memory_order_acquire);
    unsigned long next = tail + 1UL;
    if ((next - head) <= QUEUE_CAPACITY)
    {
        MidiEvent *e = &g_queue[tail & QUEUE_MASK];
        // Use our own monotonic timestamp to avoid backend-dependent semantics of timeStamp
        (void)timeStamp; // unused – RtMidi provides absolute time on some APIs; we standardize
        e->timestamp = midi_now_seconds();
        unsigned int copy = (unsigned int)messageSize;
        if (copy > MAX_MSG_SIZE)
            copy = MAX_MSG_SIZE;
        e->size = copy;
        memcpy(e->data, message, copy);
        /* publish */
        atomic_store_explicit(&g_q_tail, next, memory_order_release);
    }
    else
    {
        /* queue full, drop and increment counter */
        atomic_fetch_add_explicit(&g_drop_count, 1u, memory_order_relaxed);
    }
}

bool midi_input_init(const char *client_name, int api_index, int port_index)
{
    if (g_rtmidi)
        return true; // already initialized
    /* reset indices */
    atomic_store(&g_q_head, 0UL);
    atomic_store(&g_q_tail, 0UL);
    atomic_store(&g_drop_count, 0u);
    // If an api_index is provided, try to create with that API, otherwise use default
    if (api_index >= 0)
    {
        enum RtMidiApi apis[16];
        int n = rtmidi_get_compiled_api(apis, (unsigned int)(sizeof(apis) / sizeof(apis[0])));
        if (api_index < n)
        {
            g_rtmidi = rtmidi_in_create(apis[api_index], client_name ? client_name : "miniBAE", 1000);
        }
    }
    if (!g_rtmidi)
        g_rtmidi = rtmidi_in_create_default();
    if (!g_rtmidi)
        return false;
    // try to set callback
    rtmidi_in_set_callback(g_rtmidi, midi_ccallback, NULL);
    // Ignore SysEx and system realtime messages (clock/sense) to prevent queue floods from large or frequent messages.
    // If SysEx recording is desired later, this can be made configurable.
    rtmidi_in_ignore_types(g_rtmidi, true, true, true);
    // open requested port if specified
    unsigned int count = rtmidi_get_port_count(g_rtmidi);
    if (port_index >= 0 && (unsigned int)port_index < count)
    {
        rtmidi_open_port(g_rtmidi, (unsigned int)port_index, client_name ? client_name : "miniBAE");
    }
    else if (count > 0)
    {
        // open first available port
        rtmidi_open_port(g_rtmidi, 0, client_name ? client_name : "miniBAE");
    }
    else
    {
        // create virtual port so user can connect
        rtmidi_open_virtual_port(g_rtmidi, client_name ? client_name : "miniBAE");
    }
    return true;
}

void midi_input_shutdown(void)
{
    if (!g_rtmidi)
        return;
    // cancel callback and close
    rtmidi_in_cancel_callback(g_rtmidi);
    rtmidi_close_port(g_rtmidi);
    rtmidi_in_free(g_rtmidi);
    g_rtmidi = NULL;
    /* clear queue indices */
    atomic_store(&g_q_head, 0UL);
    atomic_store(&g_q_tail, 0UL);
}

bool midi_input_poll(unsigned char *buffer, unsigned int *size_out, double *timestamp)
{
    if (!g_rtmidi)
        return false;
    bool have = false;
    unsigned long head = atomic_load_explicit(&g_q_head, memory_order_relaxed);
    unsigned long tail = atomic_load_explicit(&g_q_tail, memory_order_acquire);
    if (head != tail)
    {
        MidiEvent *e = &g_queue[head & QUEUE_MASK];
        if (e->size > 0)
        {
            if (buffer && size_out)
            {
                unsigned int copy = e->size;
                memcpy(buffer, e->data, copy);
                *size_out = copy;
                if (timestamp)
                    *timestamp = e->timestamp;
            }
            /* consume */
            atomic_store_explicit(&g_q_head, head + 1UL, memory_order_release);
            have = true;
        }
        else
        {
            /* size 0 shouldn't happen, but advance to avoid spin */
            atomic_store_explicit(&g_q_head, head + 1UL, memory_order_release);
        }
    }
    return have;
}

unsigned int midi_input_drops(void)
{
    return atomic_load_explicit(&g_drop_count, memory_order_relaxed);
}
