/*
    Copyright (c) 2009 Beatnik, Inc All rights reserved.
    
    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:
    
    Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
    
    Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    
    Neither the name of the Beatnik, Inc nor the names of its contributors
    may be used to endorse or promote products derived from this software
    without specific prior written permission.
    
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
    IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
    TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
    TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
    PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
    LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
    NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/*****************************************************************************/
/*
**  X_Assert.h
**
**  This provides for platform specfic functions that need to be rewitten,
**  but will provide a API that is stable across all platforms
**
**  © Copyright 1995-2000 Beatnik, Inc, All Rights Reserved.
**  Written by Christopher Schardt
**
**  Beatnik products contain certain trade secrets and confidential and
**  proprietary information of Beatnik.  Use, reproduction, disclosure
**  and distribution by any means are prohibited, except pursuant to
**  a written license from Beatnik. Use of copyright notice is
**  precautionary and does not imply publication or disclosure.
**
**  Restricted Rights Legend:
**  Use, duplication, or disclosure by the Government is subject to
**  restrictions as set forth in subparagraph (c)(1)(ii) of The
**  Rights in Technical Data and Computer Software clause in DFARS
**  252.227-7013 or subparagraphs (c)(1) and (2) of the Commercial
**  Computer Software--Restricted Rights at 48 CFR 52.227-19, as
**  applicable.
**
**  Confidential-- Internal use only
**
** Overview
**  platform-dependent BAE_ASSERT() and BAE_VERIFY() macros
**
**  History -
**  5/7/99      MOE: created
**  7/13/99     Renamed HAE to BAE
**  2/4/2000    Changed copyright. We're Y2K compliant!
**  5/29/2001   sh  Added new debugging system with BAE_PRINTF
**  6/4/2001    sh  Eliminated use of ... in macro for windows
*/
/*****************************************************************************/

#ifndef X_Assert_H
#define X_Assert_H


#include "X_API.h"
#include <stdio.h>

// new BAE_PRINTF system
//  BAE_PRINTF("This is a test of me %d %s\n", 34, "hello");
//      actaully does work because the BAE_PRINTF macro is defined as printf.
//      To disable you define BAE_PRINTF(...) as blank and it eats everything
//      in between the () and does nothing.

#include <stdlib.h>
#include <string.h>

#ifdef OUTPUT_TO_LOGFILE
#ifdef _WIN32
    #include <windows.h>
    static void get_executable_directory(char *buffer, size_t size) {
        if (buffer && size > 0) {
            DWORD length = GetModuleFileNameA(NULL, buffer, (DWORD)size);
            if (length > 0 && length < size) {
                char *lastSlash = strrchr(buffer, '\\');
                if (lastSlash) {
                    *lastSlash = '\0';
                }
            } else {
                buffer[0] = '\0'; // Fallback to empty string on error
            }
        }
    }
#else
    #include <unistd.h>
    #include <libgen.h>
    static void get_executable_directory(char *buffer, size_t size) {
        if (buffer && size > 0) {
            ssize_t length = readlink("/proc/self/exe", buffer, size - 1);
            if (length > 0) {
                buffer[length] = '\0';
                char *dir = dirname(buffer);
                strncpy(buffer, dir, size - 1);
                buffer[size - 1] = '\0'; // Ensure null-termination
            } else {
                buffer[0] = '\0'; // Fallback to empty string on error
            }
        }
    }
#endif

#ifdef _ZEFI_GUI
    #define LOGFILE_NAME "/zefidi.log"
#else
    #define LOGFILE_NAME "/minibae.log"
#endif
#define BAE_STDOUT(...)                \
    do {                                \
        char logPath[1024];             \
        get_executable_directory(logPath, sizeof(logPath)); \
        strncat(logPath, LOGFILE_NAME, sizeof(logPath) - strlen(logPath) - 1); \
        FILE *logFile = fopen(logPath, "a"); \
        if (logFile) {                  \
            fprintf(logFile, __VA_ARGS__); \
            fclose(logFile);            \
        }                               \
    } while (0)
    #define BAE_STDERR         BAE_STDOUT
#else 
    #define BAE_STDOUT		printf
    #define BAE_STDERR(...)         fprintf (stderr, __VA_ARGS__)
#endif

#ifndef _DEBUG
    #if (X_PLATFORM == X_WIN95) || (X_PLATFORM == X_WIN_HARDWARE) || (X_PLATFORM == X_MACINTOSH) || (X_PLATFORM == X_IOS) || (X_PLATFORM == X_ANSI) || (X_PLATFORM == X_SDL2)
        #define BAE_PRINTF
    #elif __ANDROID__
        #define BAE_PRINTF(...)
    #else
        #define BAE_PRINTF(...)
    #endif
    #define BAE_ASSERT(exp)         ((void)0)
    #define BAE_VERIFY(exp)         (exp)
#else
    #if (X_PLATFORM == X_WIN95) || (X_PLATFORM == X_WIN_HARDWARE) || (X_PLATFORM == X_MACINTOSH) || (X_PLATFORM == X_IOS) || (X_PLATFORM == X_ANSI) || (X_PLATFORM == X_SDL2)
        #define BAE_PRINTF		BAE_STDERR
        #ifdef ASSERT
            #define BAE_ASSERT(exp)     ASSERT(exp)
            #define BAE_VERIFY(exp)     ASSERT(exp)
        #else
            #include <assert.h>
            #define BAE_ASSERT(exp)     assert(exp)
            #define BAE_VERIFY(exp)     assert(exp)
        #endif
    #else
        #ifdef  __ANDROID__
            #define BAE_STDOUT(...) __android_log_print(ANDROID_LOG_INFO, "miniBAE", __VA_ARGS__)
            #define BAE_STDERR(...) __android_log_print(ANDROID_LOG_ERROR, "miniBAE", __VA_ARGS__)
            #define BAE_PRINTF(...) BAE_STDOUT(__VA_ARGS__)
        #endif    
        #include <assert.h>
        #define BAE_ASSERT(exp)     assert(exp)
        #define BAE_VERIFY(exp)     assert(exp)
    #endif

#endif
    
#define HAE_ASSERT                      BAE_ASSERT
#define HAE_VERIFY                      BAE_VERIFY

#endif  // X_Assert_H


