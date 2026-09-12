/*
 * © 2021–2026 zefie
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/****************************************************************************
 * baescript.h - BAEScript: a lightweight JavaScript-like scripting engine
 *              for real-time MIDI manipulation in NeoBAE.
 *
 * The script runs once per playback tick (~15 ms).  Channel properties
 * (instrument, volume, pan, expression, pitch bend, mute) are readable
 * and writable; writes immediately issue the corresponding BAE API call.
 *
 * Language overview
 * ─────────────────
 *   var x = 42;                       // variable declarations
 *   x = x + 1;                        // assignment & arithmetic
 *   if (condition) { ... }            // if / else if / else
 *   while (condition) { ... }         // while loops (use carefully!)
 *   for (i = 0; i < 4; i++) { ... }   // for loops
 *   on.start({ ... });                // event handlers (one-shot per occurrence)
 *   on.pause({ ... });
 *   on.resume({ ... });
 *   on.stop({ ... });
 *   on.script({ ... });
 *   on.loop({ ... });
 *   on.seek({ ... });
 *   ch[1].instrument                  // read channel 1 instrument
 *   ch[1].instrument = 100;           // set channel 1 instrument
 *   ch[1].volume                      // channel volume  (CC 7,  0-127)
 *   ch[1].pan                         // channel pan     (CC 10, 0-127)
 *   ch[1].expression                  // expression      (CC 11, 0-127)
 *   ch[1].pitchbend                   // pitch bend      (0-16383, 8192=center)
 *   ch[1].mute                        // 0 or 1
 *   ch[1].reverb                      // reverb send     (CC 91, 0-127)
 *   ch[1].chorus                      // chorus send     (CC 93, 0-127)
 *   ch[1].solo                        // 0 or 1
 *   midi.timestamp                    // current position in ms (read/write)
 *   midi.position                     // alias for midi.timestamp
 *   midi.ticks                        // current position in MIDI ticks (read/write)
 *   midi.length                       // total song length in ms (read-only)
 *   midi.exporting                    // 1 if exporting to file, 0 otherwise
 *   midi.volume                       // BAESong volume percent (0-100)
 *   midi.tempo                        // tempo percent (100 = normal)
 *   midi.tempobpm                     // raw BPM tempo
 *   midi.transpose                    // semitone transpose
 *   midi.allnotesoff();               // send all notes off
 *   mixer.volume                      // mixer volume percent (0-100, pre-limiter)
 *   mixer.voices                      // currently active voices (read-only)
 *   mixer.reverbtype                  // BAEReverbType enum value
 *   mixer.classicchorus               // 0 or 1 (default 0)
 *   mixer.stereodcpanfix              // 0 or 1 (default 1)
 *   mixer.reset();                    // restore default mixer values
 *   abs(x), min(a,b), max(a,b), clamp(x,lo,hi)
 *   midi.stop();                      // stop playback and export
 *   exporter.loopcount = 2;           // export-only song loop count override
 *   print("hello");                   // debug output
 *   print(expression);                // print numeric or string values
 *
 * Operators: + - * / % == != < > <= >= && || !
 * Parens, braces, semicolons work as in JavaScript.
 * Comments: // line  and  /⁎ block ⁎/
 *
 ****************************************************************************/

#ifndef BAESCRIPT_H
#define BAESCRIPT_H

#include <NeoBAE.h>
#include <stdint.h>

#ifndef BAESCRIPT_EXPORTER_LOOPCOUNT
#define BAESCRIPT_EXPORTER_LOOPCOUNT TRUE
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle returned by BAEScript_Load*() */
typedef struct BAEScript_Context BAEScript_Context;

/**
 * Callback for print() output. If set, print() sends text here
 * instead of stderr.
 */
typedef void (*BAEScript_OutputFn)(const char *text, void *userdata);

/**
 * Callback for midi.stop(). If set, midi.stop() calls this;
 * otherwise it falls back to BAESong_Stop on the bound song.
 */
typedef void (*BAEScript_StopFn)(void *userdata);

/**
 * Set a callback to receive print() output.
 */
void BAEScript_SetOutputCallback(BAEScript_Context *ctx,
                                 BAEScript_OutputFn fn,
                                 void *userdata);

/**
 * Set a callback for midi.stop().
 */
void BAEScript_SetStopCallback(BAEScript_Context *ctx,
                               BAEScript_StopFn fn,
                               void *userdata);

/**
 * Load a script from a file path.
 * Returns NULL on error (parse failure printed to stderr).
 */
BAEScript_Context *BAEScript_LoadFile(const char *path);

/**
 * Load a script from an in-memory NUL-terminated string.
 * Returns NULL on error.
 */
BAEScript_Context *BAEScript_LoadString(const char *source);

/**
 * Bind the script to a live BAESong so that channel read/write
 * operations go through the engine.  Must be called before Tick().
 */
void BAEScript_SetSong(BAEScript_Context *ctx, BAESong song);

/**
 * Set the exporting flag so scripts can query midi.exporting.
 */
void BAEScript_SetExporting(BAEScript_Context *ctx, int exporting);

#if BAESCRIPT_EXPORTER_LOOPCOUNT == TRUE
/**
 * Clear exporter-scoped script options (e.g. exporter.loopcount).
 */
void BAEScript_ResetExporterOptions(BAEScript_Context *ctx);

/**
 * Get exporter.loopcount if the script set it this export pass.
 * Returns non-zero when a value is available.
 */
int BAEScript_GetExporterLoopCount(BAEScript_Context *ctx, int *outLoopCount);
#endif

/**
 * Execute one tick of the script.  Call this once per playback
 * loop iteration (~15 ms).  `timestamp_ms` is the current playback
 * position in milliseconds; `length_ms` is the total song length.
 */
void BAEScript_Tick(BAEScript_Context *ctx,
                    uint32_t timestamp_ms,
                    uint32_t length_ms);

/**
 * Free all resources associated with a script context.
 */
void BAEScript_Free(BAEScript_Context *ctx);

#ifdef __cplusplus
}
#endif

#endif /* BAESCRIPT_H */
