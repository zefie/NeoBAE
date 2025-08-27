// gui_logging.c - Logging functionality

#include "gui_logging.h"
#include "X_Assert.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// Helper function to write to gui.log
void write_to_log(const char *format, ...)
{
    if (!format)
        return;

    char log_path[1024];
    char execDir[1024];
    get_executable_directory(execDir, sizeof(execDir));

    // Build path to gui.log in executable directory. If execDir is empty,
    // fall back to a local "gui.log" in the current working directory.
    if (execDir[0] == '\0')
    {
        strncpy(log_path, "gui.log", sizeof(log_path));
        log_path[sizeof(log_path) - 1] = '\0';
    }
    else
    {
        // avoid duplicate slash/backslash if execDir already ends with one
#ifdef _WIN32
        const char sep = '\\';
#else
        const char sep = '/';
#endif
        size_t dlen = strnlen(execDir, sizeof(execDir));
        if (dlen > 0 && (execDir[dlen - 1] == '/' || execDir[dlen - 1] == '\\'))
        {
            snprintf(log_path, sizeof(log_path), "%s%czefidi.log", execDir, sep);
        }
        else
        {
            snprintf(log_path, sizeof(log_path), "%s%czefidi.log", execDir, sep);
        }
    }

    FILE *log_file = fopen(log_path, "a");
    if (!log_file)
        return;

    // Build the formatted message into a local buffer so we can ensure it
    // ends with a single newline and avoid partial writes.
    char msg[4096];
    va_list args;
    va_start(args, format);
    int mlen = vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);
    if (mlen < 0)
        msg[0] = '\0';

    // Timestamp
    time_t now = time(NULL);
    struct tm tm_info_storage;
    struct tm *tm_info = localtime(&now);
    if (tm_info == NULL)
    {
        // If localtime failed, just print without timestamp
        fprintf(log_file, "[-----] ");
    }
    else
    {
        fprintf(log_file, "[%04d-%02d-%02d %02d:%02d:%02d] ",
                tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    }

    // Write message and ensure trailing newline
    if (mlen >= (int)sizeof(msg))
    {
        // truncated
        fwrite(msg, 1, sizeof(msg) - 1, log_file);
        fputc('\n', log_file);
    }
    else
    {
        if (mlen > 0)
        {
            fwrite(msg, 1, (size_t)mlen, log_file);
            if (msg[mlen - 1] != '\n')
                fputc('\n', log_file);
        }
        else
        {
            fputc('\n', log_file);
        }
    }

    fclose(log_file);
}