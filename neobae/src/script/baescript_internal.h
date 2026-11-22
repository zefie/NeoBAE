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
 * baescript_internal.h - Shared internal types for BAEScript
 ****************************************************************************/

#ifndef BAESCRIPT_INTERNAL_H
#define BAESCRIPT_INTERNAL_H

#include "baescript.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef __cplusplus
extern "C" {
#endif

/*====================================================================
 *  LEXER - Tokens
 *====================================================================*/

typedef enum {
    /* literals / identifiers */
    TOK_NUMBER,         /* integer literal                  */
    TOK_STRING,         /* "..." string literal             */
    TOK_IDENT,          /* identifier                       */

    /* keywords */
    TOK_VAR,            /* var                              */
    TOK_IF,             /* if                               */
    TOK_ELSE,           /* else                             */
    TOK_WHILE,          /* while                            */
    TOK_FOR,            /* for                              */
    TOK_ON,             /* on                               */
    TOK_TRUE,           /* true                             */
    TOK_FALSE,          /* false                            */

    /* built-in objects */
    TOK_CH,             /* ch                               */
    TOK_MIDI,           /* midi                             */
    TOK_MIXER,          /* mixer                            */
    TOK_EXPORTER,       /* exporter                         */

    /* operators */
    TOK_PLUS,           /* +  */
    TOK_INC,            /* ++ */
    TOK_MINUS,          /* -  */
    TOK_DEC,            /* -- */
    TOK_STAR,           /* *  */
    TOK_SLASH,          /* /  */
    TOK_PERCENT,        /* %  */
    TOK_EQ,             /* == */
    TOK_NEQ,            /* != */
    TOK_LT,             /* <  */
    TOK_GT,             /* >  */
    TOK_LTE,            /* <= */
    TOK_GTE,            /* >= */
    TOK_AND,            /* && */
    TOK_OR,             /* || */
    TOK_NOT,            /* !  */
    TOK_ASSIGN,         /* =  */

    /* delimiters */
    TOK_LPAREN,         /* (  */
    TOK_RPAREN,         /* )  */
    TOK_LBRACE,         /* {  */
    TOK_RBRACE,         /* }  */
    TOK_LBRACKET,       /* [  */
    TOK_RBRACKET,       /* ]  */
    TOK_DOT,            /* .  */
    TOK_COMMA,          /* ,  */
    TOK_SEMICOLON,      /* ;  */

    TOK_EOF,            /* end of input                     */
    TOK_ERROR           /* lexer error                      */
} BAEScript_TokenType;

typedef struct {
    BAEScript_TokenType type;
    int                 line;       /* 1-based source line          */
    int                 col;        /* 1-based column               */
    union {
        int32_t         num;        /* TOK_NUMBER value             */
        char            str[256];   /* TOK_STRING / TOK_IDENT text  */
    } value;
} BAEScript_Token;

/* Lexer state */
typedef struct {
    const char *src;        /* full source text                 */
    int         pos;        /* current byte offset              */
    int         len;        /* total source length              */
    int         line;       /* current line (1-based)           */
    int         col;        /* current col  (1-based)           */
} BAEScript_Lexer;

void              BAEScript_Lexer_Init(BAEScript_Lexer *lex, const char *source);
BAEScript_Token   BAEScript_Lexer_Next(BAEScript_Lexer *lex);
BAEScript_Token   BAEScript_Lexer_Peek(BAEScript_Lexer *lex);

/*====================================================================
 *  PARSER - AST nodes
 *====================================================================*/

typedef enum {
    NODE_BLOCK,             /* { stmt; stmt; ... }              */
    NODE_VAR_DECL,          /* var x = expr;                    */
    NODE_ASSIGN,            /* x = expr;                        */
    NODE_IF,                /* if (c) { } else { }              */
    NODE_WHILE,             /* while (c) { }                    */
    NODE_PRINT,             /* print(expr, ...);                */
    NODE_EXPR_STMT,         /* expression as statement          */

    /* expressions */
    NODE_NUMBER,            /* integer literal                  */
    NODE_STRING,            /* string literal                   */
    NODE_BOOL,              /* true / false                     */
    NODE_IDENT,             /* variable reference               */
    NODE_BINOP,             /* left OP right                    */
    NODE_UNARYOP,           /* !expr  or -expr                  */
    NODE_FUNC_CALL,         /* builtin function call            */
    NODE_CH_PROP,           /* ch[expr].property  (read)        */
    NODE_CH_PROP_SET,       /* ch[expr].property = expr (write) */
    NODE_MIDI_PROP,         /* midi.property  (read)            */
    NODE_MIDI_PROP_SET,     /* midi.property = expr (write)     */
    NODE_MIXER_PROP,        /* mixer.property (read)            */
    NODE_MIXER_PROP_SET,    /* mixer.property = expr (write)    */
    NODE_EXPORTER_PROP,     /* exporter.property (read)         */
    NODE_EXPORTER_PROP_SET, /* exporter.property = expr (write) */
    NODE_MIXER_RESET,       /* mixer.reset()                    */
    NODE_NOTE_ON,           /* noteOn(ch, note, vel)            */
    NODE_NOTE_OFF,          /* noteOff(ch, note, vel)           */
    NODE_MIDI_STOP,         /* midi.stop()                      */
    NODE_MIDI_ALL_NOTES_OFF,/* midi.allnotesoff()               */
    NODE_HELP,              /* help()                           */
    NODE_EVENT_DECL,        /* on.event({ ... });               */
} BAEScript_NodeType;

typedef enum {
    EVENT_START = 0,
    EVENT_PAUSE,
    EVENT_RESUME,
    EVENT_STOP,
    EVENT_SCRIPT,
    EVENT_LOOP,
    EVENT_SEEK,
    EVENT_COUNT
} BAEScript_EventType;

/* Channel properties the user can access */
typedef enum {
    CHPROP_INSTRUMENT,
    CHPROP_VOLUME,
    CHPROP_PAN,
    CHPROP_EXPRESSION,
    CHPROP_PITCHBEND,
    CHPROP_MUTE,
    CHPROP_REVERB,
    CHPROP_CHORUS,
    CHPROP_SOLO,
} BAEScript_ChProp;

/* MIDI global properties */
typedef enum {
    MIDI_PROP_TIMESTAMP,
    MIDI_PROP_TICKS,
    MIDI_PROP_LENGTH,
    MIDI_PROP_EXPORTING,
    MIDI_PROP_VOLUME,
    MIDI_PROP_TEMPO,
    MIDI_PROP_TEMPO_BPM,
    MIDI_PROP_TRANSPOSE,
} BAEScript_MidiProp;

/* Mixer global properties */
typedef enum {
    MIXERPROP_VOLUME,
    MIXERPROP_CLASSIC_CHORUS,
    MIXERPROP_STEREO_DCPAN_FIX,
    MIXERPROP_REVERBTYPE,
    MIXERPROP_VOICES,
    MIXERPROP_WEBTV_MODE,
} BAEScript_MixerProp;

typedef enum {
    EXPORTERPROP_LOOPCOUNT,
} BAEScript_ExporterProp;

typedef enum {
    FUNC_ABS,
    FUNC_MIN,
    FUNC_MAX,
    FUNC_CLAMP,
} BAEScript_BuiltinFunc;

typedef struct BAEScript_Node BAEScript_Node;

struct BAEScript_Node {
    BAEScript_NodeType type;
    int                line;    /* source line for error messages    */

    union {
        /* NODE_NUMBER */
        int32_t num;

        /* NODE_STRING, NODE_IDENT */
        char str[256];

        /* NODE_BOOL */
        int boolval;

        /* NODE_BLOCK */
        struct { BAEScript_Node **stmts; int count; int cap; } block;

        /* NODE_VAR_DECL: name + initialiser */
        struct { char name[128]; BAEScript_Node *init; } var_decl;

        /* NODE_ASSIGN: name = value */
        struct { char name[128]; BAEScript_Node *value; } assign;

        /* NODE_IF: condition, then-block, else-block (nullable) */
        struct { BAEScript_Node *cond; BAEScript_Node *then_b; BAEScript_Node *else_b; } if_stmt;

        /* NODE_WHILE: condition, body */
        struct { BAEScript_Node *cond; BAEScript_Node *body; } while_stmt;

        /* NODE_PRINT */
        struct { BAEScript_Node **args; int count; int cap; } print_call;

        /* NODE_BINOP */
        struct { BAEScript_TokenType op; BAEScript_Node *left; BAEScript_Node *right; } binop;

        /* NODE_UNARYOP */
        struct { BAEScript_TokenType op; BAEScript_Node *operand; } unaryop;

        /* NODE_FUNC_CALL */
        struct { BAEScript_BuiltinFunc fn; BAEScript_Node *a; BAEScript_Node *b; BAEScript_Node *c; } func_call;

        /* NODE_CH_PROP (read) */
        struct { BAEScript_Node *channel; BAEScript_ChProp prop; } ch_prop;

        /* NODE_CH_PROP_SET (write) */
        struct { BAEScript_Node *channel; BAEScript_ChProp prop; BAEScript_Node *value; } ch_prop_set;

        /* NODE_MIDI_PROP */
        BAEScript_MidiProp midi_prop;

        /* NODE_MIDI_PROP_SET (write) */
        struct { BAEScript_MidiProp prop; BAEScript_Node *value; } midi_prop_set;

        /* NODE_MIXER_PROP */
        BAEScript_MixerProp mixer_prop;

        /* NODE_MIXER_PROP_SET (write) */
        struct { BAEScript_MixerProp prop; BAEScript_Node *value; } mixer_prop_set;

        /* NODE_EXPORTER_PROP */
        BAEScript_ExporterProp exporter_prop;

        /* NODE_EXPORTER_PROP_SET (write) */
        struct { BAEScript_ExporterProp prop; BAEScript_Node *value; } exporter_prop_set;

        /* NODE_NOTE_ON / NODE_NOTE_OFF */
        struct { BAEScript_Node *channel; BAEScript_Node *note; BAEScript_Node *velocity; } note_cmd;

        /* NODE_EXPR_STMT */
        BAEScript_Node *expr;

        /* NODE_EVENT_DECL */
        struct { BAEScript_EventType event_type; BAEScript_Node *body; } event_decl;
    } data;
};

/* Parser entry point: returns a NODE_BLOCK representing the whole program */
BAEScript_Node *BAEScript_Parse(const char *source);

/* Free an AST tree recursively */
void BAEScript_FreeNode(BAEScript_Node *node);

/*====================================================================
 *  VM - Interpreter state (the opaque context)
 *====================================================================*/

#define BAESCRIPT_MAX_VARS 256

typedef struct {
    char    name[128];
    int32_t value;
} BAEScript_Var;

struct BAEScript_Context {
    BAEScript_Node *program;        /* root AST (NODE_BLOCK)        */
    BAESong         song;           /* bound BAESong                */
    uint32_t        timestamp_ms;   /* current playback time        */
    uint32_t        length_ms;      /* total song length            */
    BAEScript_Var   vars[BAESCRIPT_MAX_VARS];
    int             var_count;
    BAEScript_OutputFn output_fn;   /* optional print callback      */
    void              *output_ud;   /* userdata for output callback */
    BAEScript_StopFn   stop_fn;     /* optional stop callback       */
    void              *stop_ud;     /* userdata for stop callback   */
    int               exporting;    /* non-zero when exporting      */
    int               help_shown;   /* help() only fires once       */
    BAEScript_Node   *event_handlers[EVENT_COUNT];
    int               events_initialized;
    int               script_event_fired;
    int               started;
    int               was_paused;
    int               was_done;
    int               has_prev_tick;
    uint32_t          prev_tick_pos;
    int               self_seek_this_tick;
    int               last_self_seek;
    int               exporter_loopcount;
    int               exporter_loopcount_set;
};

/* Evaluate a node tree; returns the result as int32_t */
int32_t BAEScript_Eval(BAEScript_Context *ctx, BAEScript_Node *node);

/* Execute a statement node */
void BAEScript_Exec(BAEScript_Context *ctx, BAEScript_Node *node);

/* Execute a registered event handler block by event type. */
void BAEScript_DispatchEvent(BAEScript_Context *ctx, BAEScript_EventType event_type);

#ifdef __cplusplus
}
#endif

#endif /* BAESCRIPT_INTERNAL_H */
