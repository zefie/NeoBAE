// Simple MIDI input using RtMidi C wrapper. Non-blocking poll model.

#include "midi_input.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#include "../src/thirdparty/rtmidi/rtmidi_c.h"

#define QUEUE_CAPACITY 1024
#define MAX_MSG_SIZE 1024

typedef struct MidiEvent {
    double timestamp;
    unsigned int size;
    unsigned char data[MAX_MSG_SIZE];
} MidiEvent;

static RtMidiInPtr g_rtmidi = NULL;
static MidiEvent g_queue[QUEUE_CAPACITY];
static unsigned int g_q_head = 0, g_q_tail = 0;
static pthread_mutex_t g_q_mutex = PTHREAD_MUTEX_INITIALIZER;

static void midi_ccallback(double timeStamp, const unsigned char* message, size_t messageSize, void *userData) {
    (void)userData;
    if(messageSize == 0) return;
    pthread_mutex_lock(&g_q_mutex);
    unsigned int next = (g_q_tail + 1) % QUEUE_CAPACITY;
    if(next != g_q_head) {
        MidiEvent *e = &g_queue[g_q_tail];
        e->timestamp = timeStamp;
        e->size = (unsigned int)messageSize;
        if(e->size > MAX_MSG_SIZE) e->size = MAX_MSG_SIZE;
        memcpy(e->data, message, e->size);
        g_q_tail = next;
    } else {
        // queue full, drop
    }
    pthread_mutex_unlock(&g_q_mutex);
}

bool midi_input_init(const char *client_name, int api_index, int port_index) {
    if(g_rtmidi) return true; // already initialized
    // If an api_index is provided, try to create with that API, otherwise use default
    if(api_index >= 0){
        enum RtMidiApi apis[16];
        int n = rtmidi_get_compiled_api(apis, (unsigned int)(sizeof(apis)/sizeof(apis[0])));
        if(api_index < n){
            g_rtmidi = rtmidi_in_create(apis[api_index], client_name ? client_name : "miniBAE", 1000);
        }
    }
    if(!g_rtmidi) g_rtmidi = rtmidi_in_create_default();
    if(!g_rtmidi) return false;
    // try to set callback
    rtmidi_in_set_callback(g_rtmidi, midi_ccallback, NULL);
    // ignore system realtime messages (clock/sense)
    rtmidi_in_ignore_types(g_rtmidi, false, true, true);
    // open requested port if specified
    unsigned int count = rtmidi_get_port_count(g_rtmidi);
    if(port_index >= 0 && (unsigned int)port_index < count){
        rtmidi_open_port(g_rtmidi, (unsigned int)port_index, client_name ? client_name : "miniBAE");
    } else if(count > 0){
        // open first available port
        rtmidi_open_port(g_rtmidi, 0, client_name ? client_name : "miniBAE");
    } else {
        // create virtual port so user can connect
        rtmidi_open_virtual_port(g_rtmidi, client_name ? client_name : "miniBAE");
    }
    return true;
}

void midi_input_shutdown(void) {
    if(!g_rtmidi) return;
    // cancel callback and close
    rtmidi_in_cancel_callback(g_rtmidi);
    rtmidi_close_port(g_rtmidi);
    rtmidi_in_free(g_rtmidi);
    g_rtmidi = NULL;
    // clear queue
    pthread_mutex_lock(&g_q_mutex);
    g_q_head = g_q_tail = 0;
    pthread_mutex_unlock(&g_q_mutex);
}

bool midi_input_poll(unsigned char *buffer, unsigned int *size_out, double *timestamp) {
    if(!g_rtmidi) return false;
    bool have = false;
    pthread_mutex_lock(&g_q_mutex);
    if(g_q_head != g_q_tail) {
        MidiEvent *e = &g_queue[g_q_head];
        if(e->size > 0) {
            if(buffer && size_out) {
                unsigned int copy = e->size;
                memcpy(buffer, e->data, copy);
                *size_out = copy;
                if(timestamp) *timestamp = e->timestamp;
            }
            g_q_head = (g_q_head + 1) % QUEUE_CAPACITY;
            have = true;
        }
    }
    pthread_mutex_unlock(&g_q_mutex);
    return have;
}
