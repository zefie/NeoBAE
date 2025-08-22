#ifndef GUI_LOGGING_H
#define GUI_LOGGING_H

#include "gui_common.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Append a formatted message to gui.log located in the executable directory.
    // This is a simple helper intended for debugging output from the GUI.
    void write_to_log(const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif // GUI_LOGGING_H