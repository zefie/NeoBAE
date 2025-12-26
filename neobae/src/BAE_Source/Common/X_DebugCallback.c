// X_DebugCallback.c - Debug output callback for BAE_PRINTF
// Provides a hook for GUI applications to capture debug output

#ifdef _DEBUG

#include <stdarg.h>
#include <stdio.h>

// Function pointer for debug output callback
static void (*g_debug_output_callback)(const char *message) = NULL;

// Set the debug output callback (called by GUI on init)
void BAE_SetDebugOutputCallback(void (*callback)(const char *message))
{
    g_debug_output_callback = callback;
}

// Send message to debug output callback
void debug_console_append(const char *message)
{
    if (g_debug_output_callback) {
        g_debug_output_callback(message);
    }
    // If no callback registered, do nothing (messages go to stderr via BAE_STDERR)
}

#endif // _DEBUG
