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
 * baescript_parser.c - Recursive-descent parser for BAEScript
 *
 * Grammar (simplified):
 *   program     = statement*
 *   statement   = varDecl | ifStmt | whileStmt | printStmt
 *               | assignment | exprStmt
 *   varDecl     = "var" IDENT "=" expr ";"
 *   ifStmt      = "if" "(" expr ")" block ("else" (ifStmt | block))?
 *   whileStmt   = "while" "(" expr ")" block
 *   forStmt     = "for" "(" init? ";" cond? ";" step? ")" block
 *   eventStmt   = "on" "." EVENT "(" block ")" ";"
 *   printStmt   = "print" "(" args ")" ";"
 *   assignment  = (IDENT "=" expr ";") | (chPropSet ";")
 *   chPropSet   = "ch" "[" expr "]" "." PROP "=" expr
 *   block       = "{" statement* "}"
 *   expr        = or_expr
 *   or_expr     = and_expr ("||" and_expr)*
 *   and_expr    = eq_expr  ("&&" eq_expr)*
 *   eq_expr     = cmp_expr (("==" | "!=") cmp_expr)*
 *   cmp_expr    = add_expr (("<" | ">" | "<=" | ">=") add_expr)*
 *   add_expr    = mul_expr (("+" | "-") mul_expr)*
 *   mul_expr    = unary    (("*" | "/" | "%") unary)*
 *   unary       = ("!" | "-") unary | primary
 *   primary     = NUMBER | STRING | "true" | "false" | chProp
 *               | midiProp | mixerProp | exporterProp | IDENT | "(" expr ")"
 *               | noteOn | noteOff
 *   chProp      = "ch" "[" expr "]" "." PROP
 *   midiProp    = "midi" "." ("timestamp" | "position" | "ticks" | "length" | "exporting" | "volume" | "tempo" | "tempobpm" | "transpose")
 *   mixerProp   = "mixer" "." ("volume" | "classicchorus" | "stereodcpanfix" | "reverbtype" | "webtvmode")
 *   exporterProp= "exporter" "." "loopcount"
 *   midiStop    = "midi" "." "stop" "(" ")" ";"
 *   midiAllOff  = "midi" "." "allnotesoff" "(" ")" ";"
 *   mixerReset  = "mixer" "." "reset" "(" ")" ";"
 *   noteOn      = "noteOn" "(" expr "," expr "," expr ")"
 *   noteOff     = "noteOff" "(" expr "," expr "," expr ")"
 ****************************************************************************/

#include "baescript_internal.h"

/* ── parser state ───────────────────────────────────────────────────── */

typedef struct {
    BAEScript_Lexer  lex;
    BAEScript_Token  current;
    int              had_error;
} Parser;

static void parser_init(Parser *p, const char *source)
{
    BAEScript_Lexer_Init(&p->lex, source);
    p->current   = BAEScript_Lexer_Next(&p->lex);
    p->had_error = 0;
}

static void parser_error(Parser *p, const char *msg)
{
    if (!p->had_error) {
        fprintf(stderr, "BAEScript error [line %d col %d]: %s\n",
                p->current.line, p->current.col, msg);
    }
    p->had_error = 1;
}

static BAEScript_Token parser_advance(Parser *p)
{
    BAEScript_Token prev = p->current;
    p->current = BAEScript_Lexer_Next(&p->lex);
    return prev;
}

static int parser_check(Parser *p, BAEScript_TokenType type)
{
    return p->current.type == type;
}

static int parser_match(Parser *p, BAEScript_TokenType type)
{
    if (parser_check(p, type)) { parser_advance(p); return 1; }
    return 0;
}

static void parser_expect(Parser *p, BAEScript_TokenType type, const char *msg)
{
    if (!parser_match(p, type)) parser_error(p, msg);
}

/* ── AST node constructors ──────────────────────────────────────────── */

static BAEScript_Node *new_node(BAEScript_NodeType type, int line)
{
    BAEScript_Node *n = (BAEScript_Node *)calloc(1, sizeof(BAEScript_Node));
    if (n) { n->type = type; n->line = line; }
    return n;
}

static BAEScript_Node *make_number_node(int line, int32_t value)
{
    BAEScript_Node *n = new_node(NODE_NUMBER, line);
    n->data.num = value;
    return n;
}

static BAEScript_Node *make_ident_node(int line, const char *name)
{
    BAEScript_Node *n = new_node(NODE_IDENT, line);
    strncpy(n->data.str, name, sizeof(n->data.str) - 1);
    n->data.str[sizeof(n->data.str) - 1] = '\0';
    return n;
}

static void block_push(BAEScript_Node *block, BAEScript_Node *stmt)
{
    if (!block || !stmt) return;
    if (block->data.block.count >= block->data.block.cap) {
        int newcap = block->data.block.cap < 8 ? 8 : block->data.block.cap * 2;
        BAEScript_Node **tmp = (BAEScript_Node **)realloc(
            block->data.block.stmts, sizeof(BAEScript_Node *) * newcap);
        if (!tmp) return;
        block->data.block.stmts = tmp;
        block->data.block.cap   = newcap;
    }
    block->data.block.stmts[block->data.block.count++] = stmt;
}

/* ── forward declarations ──────────────────────────────────────────── */

static BAEScript_Node *parse_expr(Parser *p);
static BAEScript_Node *parse_statement(Parser *p);
static BAEScript_Node *parse_block(Parser *p);

static BAEScript_Node *parse_for_clause_assignment(Parser *p)
{
    BAEScript_Token name = parser_advance(p);
    if (name.type != TOK_IDENT) {
        parser_error(p, "Expected identifier in for-clause");
        return make_number_node(p->current.line, 0);
    }

    if (parser_match(p, TOK_ASSIGN)) {
        BAEScript_Node *n = new_node(NODE_ASSIGN, name.line);
        strncpy(n->data.assign.name, name.value.str, sizeof(n->data.assign.name) - 1);
        n->data.assign.name[sizeof(n->data.assign.name) - 1] = '\0';
        n->data.assign.value = parse_expr(p);
        return n;
    }

    if (parser_check(p, TOK_INC) || parser_check(p, TOK_DEC)) {
        BAEScript_Node *n = new_node(NODE_ASSIGN, name.line);
        BAEScript_TokenType op = p->current.type;
        parser_advance(p);
        strncpy(n->data.assign.name, name.value.str, sizeof(n->data.assign.name) - 1);
        n->data.assign.name[sizeof(n->data.assign.name) - 1] = '\0';

        BAEScript_Node *bin = new_node(NODE_BINOP, name.line);
        bin->data.binop.op = (op == TOK_DEC) ? TOK_MINUS : TOK_PLUS;
        bin->data.binop.left = make_ident_node(name.line, name.value.str);
        bin->data.binop.right = make_number_node(name.line, 1);
        n->data.assign.value = bin;
        return n;
    }

    parser_error(p, "Expected '=', '++', or '--' in for-clause assignment");
    return make_number_node(name.line, 0);
}

/* ── expression parsing (precedence climbing) ──────────────────────── */

static BAEScript_ChProp parse_ch_property(Parser *p)
{
    BAEScript_Token tok = parser_advance(p);
    if (tok.type == TOK_IDENT) {
        if (strcmp(tok.value.str, "instrument")  == 0) return CHPROP_INSTRUMENT;
        if (strcmp(tok.value.str, "program")     == 0) return CHPROP_INSTRUMENT;
        if (strcmp(tok.value.str, "volume")      == 0) return CHPROP_VOLUME;
        if (strcmp(tok.value.str, "pan")         == 0) return CHPROP_PAN;
        if (strcmp(tok.value.str, "expression")  == 0) return CHPROP_EXPRESSION;
        if (strcmp(tok.value.str, "pitchbend")   == 0) return CHPROP_PITCHBEND;
        if (strcmp(tok.value.str, "mute")        == 0) return CHPROP_MUTE;
        if (strcmp(tok.value.str, "reverb")      == 0) return CHPROP_REVERB;
        if (strcmp(tok.value.str, "chorus")      == 0) return CHPROP_CHORUS;
        if (strcmp(tok.value.str, "solo")        == 0) return CHPROP_SOLO;
    }
    parser_error(p, "Expected channel property: instrument, volume, pan, expression, pitchbend, mute, reverb, chorus, or solo");
    return CHPROP_INSTRUMENT;
}

/* ch[expr].prop */
static BAEScript_Node *parse_ch_access(Parser *p)
{
    int line = p->current.line;
    parser_expect(p, TOK_LBRACKET, "Expected '[' after 'ch'");
    BAEScript_Node *chan = parse_expr(p);
    parser_expect(p, TOK_RBRACKET, "Expected ']'");
    parser_expect(p, TOK_DOT,      "Expected '.' after ch[...]");
    BAEScript_ChProp prop = parse_ch_property(p);

    BAEScript_Node *n = new_node(NODE_CH_PROP, line);
    n->data.ch_prop.channel = chan;
    n->data.ch_prop.prop    = prop;
    return n;
}

/* mixer.prop */
static BAEScript_Node *parse_mixer_access(Parser *p)
{
    int line = p->current.line;
    parser_expect(p, TOK_DOT, "Expected '.' after 'mixer'");
    BAEScript_Token tok = parser_advance(p);
    BAEScript_MixerProp mp = MIXERPROP_VOLUME;
    if (tok.type == TOK_IDENT) {
        if (strcmp(tok.value.str, "volume") == 0) mp = MIXERPROP_VOLUME;
        else if (strcmp(tok.value.str, "classicchorus") == 0) mp = MIXERPROP_CLASSIC_CHORUS;
        else if (strcmp(tok.value.str, "stereodcpanfix") == 0) mp = MIXERPROP_STEREO_DCPAN_FIX;
        else if (strcmp(tok.value.str, "reverbtype") == 0) mp = MIXERPROP_REVERBTYPE;
        else if (strcmp(tok.value.str, "voices") == 0) mp = MIXERPROP_VOICES;
        else if (strcmp(tok.value.str, "webtvmode") == 0) mp = MIXERPROP_WEBTV_MODE;
        else parser_error(p, "Expected mixer property: volume, classicchorus, stereodcpanfix, reverbtype, voices, or webtvmode");
    } else {
        parser_error(p, "Expected mixer property name");
    }
    BAEScript_Node *n = new_node(NODE_MIXER_PROP, line);
    n->data.mixer_prop = mp;
    return n;
}

/* midi.prop */
static BAEScript_Node *parse_midi_access(Parser *p)
{
    int line = p->current.line;
    parser_expect(p, TOK_DOT, "Expected '.' after 'midi'");
    BAEScript_Token tok = parser_advance(p);
    BAEScript_MidiProp mp = MIDI_PROP_TIMESTAMP;
    if (tok.type == TOK_IDENT) {
        if (strcmp(tok.value.str, "timestamp") == 0) mp = MIDI_PROP_TIMESTAMP;
        else if (strcmp(tok.value.str, "position") == 0) mp = MIDI_PROP_TIMESTAMP;
        else if (strcmp(tok.value.str, "ticks") == 0) mp = MIDI_PROP_TICKS;
        else if (strcmp(tok.value.str, "length") == 0) mp = MIDI_PROP_LENGTH;
        else if (strcmp(tok.value.str, "exporting") == 0) mp = MIDI_PROP_EXPORTING;
        else if (strcmp(tok.value.str, "volume") == 0) mp = MIDI_PROP_VOLUME;
        else if (strcmp(tok.value.str, "tempo") == 0) mp = MIDI_PROP_TEMPO;
        else if (strcmp(tok.value.str, "tempobpm") == 0) mp = MIDI_PROP_TEMPO_BPM;
        else if (strcmp(tok.value.str, "transpose") == 0) mp = MIDI_PROP_TRANSPOSE;
        else parser_error(p, "Expected midi property: timestamp, position, ticks, length, exporting, volume, tempo, tempobpm, or transpose");
    } else {
        parser_error(p, "Expected midi property name");
    }
    BAEScript_Node *n = new_node(NODE_MIDI_PROP, line);
    n->data.midi_prop = mp;
    return n;
}

/* exporter.prop */
static BAEScript_Node *parse_exporter_access(Parser *p)
{
    int line = p->current.line;
    parser_expect(p, TOK_DOT, "Expected '.' after 'exporter'");
    BAEScript_Token tok = parser_advance(p);
    BAEScript_ExporterProp ep = EXPORTERPROP_LOOPCOUNT;

    if (tok.type == TOK_IDENT) {
        if (strcmp(tok.value.str, "loopcount") != 0)
            parser_error(p, "Expected exporter property: loopcount");
    } else {
        parser_error(p, "Expected exporter property name");
    }

    BAEScript_Node *n = new_node(NODE_EXPORTER_PROP, line);
    n->data.exporter_prop = ep;
    return n;
}

/* noteOn(ch, note, vel)  or  noteOff(ch, note, vel) */
static BAEScript_Node *parse_note_cmd(Parser *p, BAEScript_NodeType type)
{
    int line = p->current.line;
    parser_expect(p, TOK_LPAREN, "Expected '(' after noteOn/noteOff");
    BAEScript_Node *ch   = parse_expr(p);
    parser_expect(p, TOK_COMMA, "Expected ','");
    BAEScript_Node *note = parse_expr(p);
    parser_expect(p, TOK_COMMA, "Expected ','");
    BAEScript_Node *vel  = parse_expr(p);
    parser_expect(p, TOK_RPAREN, "Expected ')'");
    BAEScript_Node *n = new_node(type, line);
    n->data.note_cmd.channel  = ch;
    n->data.note_cmd.note     = note;
    n->data.note_cmd.velocity = vel;
    return n;
}

static BAEScript_Node *parse_primary(Parser *p)
{
    int line = p->current.line;

    /* number */
    if (parser_check(p, TOK_NUMBER)) {
        BAEScript_Token t = parser_advance(p);
        BAEScript_Node *n = new_node(NODE_NUMBER, line);
        n->data.num = t.value.num;
        return n;
    }

    /* string */
    if (parser_check(p, TOK_STRING)) {
        BAEScript_Token t = parser_advance(p);
        BAEScript_Node *n = new_node(NODE_STRING, line);
        strncpy(n->data.str, t.value.str, sizeof(n->data.str) - 1);
        return n;
    }

    /* booleans */
    if (parser_check(p, TOK_TRUE)) {
        parser_advance(p);
        BAEScript_Node *n = new_node(NODE_BOOL, line);
        n->data.boolval = 1;
        return n;
    }
    if (parser_check(p, TOK_FALSE)) {
        parser_advance(p);
        BAEScript_Node *n = new_node(NODE_BOOL, line);
        n->data.boolval = 0;
        return n;
    }

    /* ch[...].prop */
    if (parser_check(p, TOK_CH)) {
        parser_advance(p);
        return parse_ch_access(p);
    }

    /* midi.prop */
    if (parser_check(p, TOK_MIDI)) {
        parser_advance(p);
        return parse_midi_access(p);
    }

    /* mixer.prop */
    if (parser_check(p, TOK_MIXER)) {
        parser_advance(p);
        return parse_mixer_access(p);
    }

    /* exporter.prop */
    if (parser_check(p, TOK_EXPORTER)) {
        parser_advance(p);
        return parse_exporter_access(p);
    }

    /* identifier (variable or noteOn/noteOff) */
    if (parser_check(p, TOK_IDENT)) {
        BAEScript_Token t = parser_advance(p);
        /* built-in functions that look like identifiers */
        if (strcmp(t.value.str, "noteOn") == 0)  return parse_note_cmd(p, NODE_NOTE_ON);
        if (strcmp(t.value.str, "noteOff") == 0) return parse_note_cmd(p, NODE_NOTE_OFF);

        if (((strcmp(t.value.str, "abs") == 0) ||
            (strcmp(t.value.str, "min") == 0) ||
            (strcmp(t.value.str, "max") == 0) ||
            (strcmp(t.value.str, "clamp") == 0)) && parser_check(p, TOK_LPAREN)) {
            BAEScript_Node *n = new_node(NODE_FUNC_CALL, line);
            if (strcmp(t.value.str, "abs") == 0) n->data.func_call.fn = FUNC_ABS;
            else if (strcmp(t.value.str, "min") == 0) n->data.func_call.fn = FUNC_MIN;
            else if (strcmp(t.value.str, "max") == 0) n->data.func_call.fn = FUNC_MAX;
            else n->data.func_call.fn = FUNC_CLAMP;

            parser_expect(p, TOK_LPAREN, "Expected '(' after function name");
            n->data.func_call.a = parse_expr(p);
            if (n->data.func_call.fn == FUNC_ABS) {
                parser_expect(p, TOK_RPAREN, "Expected ')' after abs(arg)");
                return n;
            }

            parser_expect(p, TOK_COMMA, "Expected ',' separating function arguments");
            n->data.func_call.b = parse_expr(p);

            if (n->data.func_call.fn == FUNC_MIN || n->data.func_call.fn == FUNC_MAX) {
                parser_expect(p, TOK_RPAREN, "Expected ')' after min/max args");
                return n;
            }

            parser_expect(p, TOK_COMMA, "Expected ',' separating clamp arguments");
            n->data.func_call.c = parse_expr(p);
            parser_expect(p, TOK_RPAREN, "Expected ')' after clamp args");
            return n;
        }

        BAEScript_Node *n = new_node(NODE_IDENT, line);
        strncpy(n->data.str, t.value.str, sizeof(n->data.str) - 1);
        return n;
    }

    /* parenthesised expr */
    if (parser_match(p, TOK_LPAREN)) {
        BAEScript_Node *expr = parse_expr(p);
        parser_expect(p, TOK_RPAREN, "Expected ')'");
        return expr;
    }

    parser_error(p, "Expected expression");
    return new_node(NODE_NUMBER, line); /* dummy to avoid NULL */
}

static BAEScript_Node *parse_unary(Parser *p)
{
    int line = p->current.line;
    if (parser_check(p, TOK_NOT)) {
        parser_advance(p);
        BAEScript_Node *n = new_node(NODE_UNARYOP, line);
        n->data.unaryop.op      = TOK_NOT;
        n->data.unaryop.operand = parse_unary(p);
        return n;
    }
    if (parser_check(p, TOK_MINUS)) {
        parser_advance(p);
        BAEScript_Node *n = new_node(NODE_UNARYOP, line);
        n->data.unaryop.op      = TOK_MINUS;
        n->data.unaryop.operand = parse_unary(p);
        return n;
    }
    return parse_primary(p);
}

static BAEScript_Node *parse_mul(Parser *p)
{
    BAEScript_Node *left = parse_unary(p);
    while (parser_check(p, TOK_STAR) || parser_check(p, TOK_SLASH) || parser_check(p, TOK_PERCENT)) {
        BAEScript_Token op = parser_advance(p);
        BAEScript_Node *right = parse_unary(p);
        BAEScript_Node *n = new_node(NODE_BINOP, op.line);
        n->data.binop.op    = op.type;
        n->data.binop.left  = left;
        n->data.binop.right = right;
        left = n;
    }
    return left;
}

static BAEScript_Node *parse_add(Parser *p)
{
    BAEScript_Node *left = parse_mul(p);
    while (parser_check(p, TOK_PLUS) || parser_check(p, TOK_MINUS)) {
        BAEScript_Token op = parser_advance(p);
        BAEScript_Node *right = parse_mul(p);
        BAEScript_Node *n = new_node(NODE_BINOP, op.line);
        n->data.binop.op    = op.type;
        n->data.binop.left  = left;
        n->data.binop.right = right;
        left = n;
    }
    return left;
}

static BAEScript_Node *parse_cmp(Parser *p)
{
    BAEScript_Node *left = parse_add(p);
    while (parser_check(p, TOK_LT) || parser_check(p, TOK_GT) ||
           parser_check(p, TOK_LTE) || parser_check(p, TOK_GTE)) {
        BAEScript_Token op = parser_advance(p);
        BAEScript_Node *right = parse_add(p);
        BAEScript_Node *n = new_node(NODE_BINOP, op.line);
        n->data.binop.op    = op.type;
        n->data.binop.left  = left;
        n->data.binop.right = right;
        left = n;
    }
    return left;
}

static BAEScript_Node *parse_equality(Parser *p)
{
    BAEScript_Node *left = parse_cmp(p);
    while (parser_check(p, TOK_EQ) || parser_check(p, TOK_NEQ)) {
        BAEScript_Token op = parser_advance(p);
        BAEScript_Node *right = parse_cmp(p);
        BAEScript_Node *n = new_node(NODE_BINOP, op.line);
        n->data.binop.op    = op.type;
        n->data.binop.left  = left;
        n->data.binop.right = right;
        left = n;
    }
    return left;
}

static BAEScript_Node *parse_and(Parser *p)
{
    BAEScript_Node *left = parse_equality(p);
    while (parser_check(p, TOK_AND)) {
        BAEScript_Token op = parser_advance(p);
        BAEScript_Node *right = parse_equality(p);
        BAEScript_Node *n = new_node(NODE_BINOP, op.line);
        n->data.binop.op    = op.type;
        n->data.binop.left  = left;
        n->data.binop.right = right;
        left = n;
    }
    return left;
}

static BAEScript_Node *parse_or(Parser *p)
{
    BAEScript_Node *left = parse_and(p);
    while (parser_check(p, TOK_OR)) {
        BAEScript_Token op = parser_advance(p);
        BAEScript_Node *right = parse_and(p);
        BAEScript_Node *n = new_node(NODE_BINOP, op.line);
        n->data.binop.op    = op.type;
        n->data.binop.left  = left;
        n->data.binop.right = right;
        left = n;
    }
    return left;
}

static BAEScript_Node *parse_expr(Parser *p)
{
    return parse_or(p);
}

/* ── statement parsing ──────────────────────────────────────────────── */

static BAEScript_Node *parse_block(Parser *p)
{
    int line = p->current.line;
    parser_expect(p, TOK_LBRACE, "Expected '{'");
    BAEScript_Node *blk = new_node(NODE_BLOCK, line);
    while (!parser_check(p, TOK_RBRACE) && !parser_check(p, TOK_EOF) && !p->had_error) {
        block_push(blk, parse_statement(p));
    }
    parser_expect(p, TOK_RBRACE, "Expected '}'");
    return blk;
}

static BAEScript_Node *parse_var_decl(Parser *p)
{
    int line = p->current.line;
    parser_advance(p); /* consume 'var' */
    BAEScript_Token name = parser_advance(p);
    if (name.type != TOK_IDENT) {
        parser_error(p, "Expected variable name after 'var'");
        return new_node(NODE_NUMBER, line);
    }
    BAEScript_Node *init = NULL;
    if (parser_match(p, TOK_ASSIGN)) {
        init = parse_expr(p);
    }
    parser_expect(p, TOK_SEMICOLON, "Expected ';' after variable declaration");

    BAEScript_Node *n = new_node(NODE_VAR_DECL, line);
    strncpy(n->data.var_decl.name, name.value.str, sizeof(n->data.var_decl.name) - 1);
    n->data.var_decl.init = init;
    return n;
}

static BAEScript_Node *parse_if(Parser *p)
{
    int line = p->current.line;
    parser_advance(p); /* consume 'if' */
    parser_expect(p, TOK_LPAREN, "Expected '(' after 'if'");
    BAEScript_Node *cond = parse_expr(p);
    parser_expect(p, TOK_RPAREN, "Expected ')'");
    BAEScript_Node *then_b = parse_block(p);
    BAEScript_Node *else_b = NULL;
    if (parser_match(p, TOK_ELSE)) {
        if (parser_check(p, TOK_IF)) {
            else_b = parse_if(p); /* else if */
        } else {
            else_b = parse_block(p);
        }
    }
    BAEScript_Node *n = new_node(NODE_IF, line);
    n->data.if_stmt.cond   = cond;
    n->data.if_stmt.then_b = then_b;
    n->data.if_stmt.else_b = else_b;
    return n;
}

static BAEScript_Node *parse_while(Parser *p)
{
    int line = p->current.line;
    parser_advance(p); /* consume 'while' */
    parser_expect(p, TOK_LPAREN, "Expected '(' after 'while'");
    BAEScript_Node *cond = parse_expr(p);
    parser_expect(p, TOK_RPAREN, "Expected ')'");
    BAEScript_Node *body = parse_block(p);
    BAEScript_Node *n = new_node(NODE_WHILE, line);
    n->data.while_stmt.cond = cond;
    n->data.while_stmt.body = body;
    return n;
}

static BAEScript_Node *parse_for(Parser *p)
{
    int line = p->current.line;
    parser_advance(p); /* consume 'for' */
    parser_expect(p, TOK_LPAREN, "Expected '(' after 'for'");

    BAEScript_Node *init = NULL;
    BAEScript_Node *cond = NULL;
    BAEScript_Node *step_stmt = NULL;

    if (!parser_check(p, TOK_SEMICOLON)) {
        if (parser_check(p, TOK_VAR)) {
            parser_advance(p); /* consume 'var' */
            BAEScript_Token name = parser_advance(p);
            if (name.type != TOK_IDENT) {
                parser_error(p, "Expected variable name in for initializer");
                init = make_number_node(line, 0);
            } else {
                BAEScript_Node *n = new_node(NODE_VAR_DECL, name.line);
                strncpy(n->data.var_decl.name, name.value.str, sizeof(n->data.var_decl.name) - 1);
                n->data.var_decl.name[sizeof(n->data.var_decl.name) - 1] = '\0';
                if (parser_match(p, TOK_ASSIGN))
                    n->data.var_decl.init = parse_expr(p);
                init = n;
            }
        } else if (parser_check(p, TOK_IDENT)) {
            init = parse_for_clause_assignment(p);
        } else {
            BAEScript_Node *expr = parse_expr(p);
            BAEScript_Node *n = new_node(NODE_EXPR_STMT, line);
            n->data.expr = expr;
            init = n;
        }
    }
    parser_expect(p, TOK_SEMICOLON, "Expected ';' after for initializer");

    if (!parser_check(p, TOK_SEMICOLON)) {
        cond = parse_expr(p);
    } else {
        BAEScript_Node *always = new_node(NODE_BOOL, line);
        always->data.boolval = 1;
        cond = always;
    }
    parser_expect(p, TOK_SEMICOLON, "Expected ';' after for condition");

    if (!parser_check(p, TOK_RPAREN)) {
        if (parser_check(p, TOK_IDENT)) {
            step_stmt = parse_for_clause_assignment(p);
        } else {
            BAEScript_Node *expr = parse_expr(p);
            BAEScript_Node *n = new_node(NODE_EXPR_STMT, line);
            n->data.expr = expr;
            step_stmt = n;
        }
    }
    parser_expect(p, TOK_RPAREN, "Expected ')' after for clauses");

    BAEScript_Node *body = parse_block(p);
    if (body && body->type == NODE_BLOCK && step_stmt)
        block_push(body, step_stmt);

    BAEScript_Node *while_n = new_node(NODE_WHILE, line);
    while_n->data.while_stmt.cond = cond;
    while_n->data.while_stmt.body = body;

    BAEScript_Node *block = new_node(NODE_BLOCK, line);
    if (init) block_push(block, init);
    block_push(block, while_n);
    return block;
}

static BAEScript_Node *parse_print(Parser *p)
{
    int line = p->current.line;
    parser_advance(p); /* consume 'print' ident */
    parser_expect(p, TOK_LPAREN, "Expected '(' after 'print'");
    BAEScript_Node *n = new_node(NODE_PRINT, line);
    if (!parser_check(p, TOK_RPAREN)) {
        BAEScript_Node *arg = parse_expr(p);
        /* store in print_call */
        if (n->data.print_call.count >= n->data.print_call.cap) {
            int newcap = n->data.print_call.cap < 4 ? 4 : n->data.print_call.cap * 2;
            BAEScript_Node **tmp = (BAEScript_Node **)realloc(
                n->data.print_call.args, sizeof(BAEScript_Node *) * newcap);
            if (tmp) { n->data.print_call.args = tmp; n->data.print_call.cap = newcap; }
        }
        if (n->data.print_call.count < n->data.print_call.cap)
            n->data.print_call.args[n->data.print_call.count++] = arg;

        while (parser_match(p, TOK_COMMA)) {
            arg = parse_expr(p);
            if (n->data.print_call.count >= n->data.print_call.cap) {
                int newcap = n->data.print_call.cap * 2;
                BAEScript_Node **tmp = (BAEScript_Node **)realloc(
                    n->data.print_call.args, sizeof(BAEScript_Node *) * newcap);
                if (tmp) { n->data.print_call.args = tmp; n->data.print_call.cap = newcap; }
            }
            if (n->data.print_call.count < n->data.print_call.cap)
                n->data.print_call.args[n->data.print_call.count++] = arg;
        }
    }
    parser_expect(p, TOK_RPAREN,   "Expected ')' after print arguments");
    parser_expect(p, TOK_SEMICOLON, "Expected ';' after print statement");
    return n;
}

static BAEScript_Node *parse_event_decl(Parser *p)
{
    int line = p->current.line;
    parser_advance(p); /* consume 'on' */
    parser_expect(p, TOK_DOT, "Expected '.' after 'on'");

    BAEScript_Token event_name = parser_advance(p);
    if (event_name.type != TOK_IDENT) {
        parser_error(p, "Expected event name after 'on.'");
        return make_number_node(line, 0);
    }

    BAEScript_EventType event_type = EVENT_SCRIPT;
    if (strcmp(event_name.value.str, "start") == 0) event_type = EVENT_START;
    else if (strcmp(event_name.value.str, "pause") == 0) event_type = EVENT_PAUSE;
    else if (strcmp(event_name.value.str, "resume") == 0) event_type = EVENT_RESUME;
    else if (strcmp(event_name.value.str, "stop") == 0) event_type = EVENT_STOP;
    else if (strcmp(event_name.value.str, "script") == 0) event_type = EVENT_SCRIPT;
    else if (strcmp(event_name.value.str, "loop") == 0) event_type = EVENT_LOOP;
    else if (strcmp(event_name.value.str, "seek") == 0) event_type = EVENT_SEEK;
    else {
        parser_error(p, "Unknown event. Expected: start, pause, resume, stop, script, loop, or seek");
    }

    parser_expect(p, TOK_LPAREN, "Expected '(' after event name");
    BAEScript_Node *body = parse_block(p);
    parser_expect(p, TOK_RPAREN, "Expected ')' after event block");
    parser_expect(p, TOK_SEMICOLON, "Expected ';' after event declaration");

    BAEScript_Node *n = new_node(NODE_EVENT_DECL, line);
    n->data.event_decl.event_type = event_type;
    n->data.event_decl.body = body;
    return n;
}

static BAEScript_Node *parse_statement(Parser *p)
{
    if (p->had_error) return new_node(NODE_NUMBER, p->current.line);

    /* var declaration */
    if (parser_check(p, TOK_VAR)) return parse_var_decl(p);

    /* if */
    if (parser_check(p, TOK_IF)) return parse_if(p);

    /* while */
    if (parser_check(p, TOK_WHILE)) return parse_while(p);

    /* for */
    if (parser_check(p, TOK_FOR)) return parse_for(p);

    /* on.event({ ... }); */
    if (parser_check(p, TOK_ON)) return parse_event_decl(p);

    /* print(...) */
    if (parser_check(p, TOK_IDENT) && strcmp(p->current.value.str, "print") == 0)
        return parse_print(p);

    /* help() */
    if (parser_check(p, TOK_IDENT) && strcmp(p->current.value.str, "help") == 0) {
        int line = p->current.line;
        parser_advance(p); /* consume 'help' */
        parser_expect(p, TOK_LPAREN,    "Expected '(' after 'help'");
        parser_expect(p, TOK_RPAREN,    "Expected ')' after 'help('");
        parser_expect(p, TOK_SEMICOLON, "Expected ';' after 'help()'");
        return new_node(NODE_HELP, line);
    }

    /* midi.stop(), midi.allnotesoff(), or midi.prop = expr; */
    if (parser_check(p, TOK_MIDI)) {
        int line = p->current.line;
        parser_advance(p); /* consume 'midi' */
        /* Check for midi.stop() */
        {
            BAEScript_Lexer saved_lex = p->lex;
            BAEScript_Token saved_cur = p->current;
            if (parser_match(p, TOK_DOT)) {
                BAEScript_Token tok = p->current;
                if (tok.type == TOK_IDENT && strcmp(tok.value.str, "stop") == 0) {
                    parser_advance(p); /* consume 'stop' */
                    parser_expect(p, TOK_LPAREN,    "Expected '(' after 'midi.stop'");
                    parser_expect(p, TOK_RPAREN,    "Expected ')' after 'midi.stop('");
                    parser_expect(p, TOK_SEMICOLON, "Expected ';' after 'midi.stop()'");
                    return new_node(NODE_MIDI_STOP, line);
                }
                if (tok.type == TOK_IDENT && strcmp(tok.value.str, "allnotesoff") == 0) {
                    parser_advance(p); /* consume 'allnotesoff' */
                    parser_expect(p, TOK_LPAREN,    "Expected '(' after 'midi.allnotesoff'");
                    parser_expect(p, TOK_RPAREN,    "Expected ')' after 'midi.allnotesoff('");
                    parser_expect(p, TOK_SEMICOLON, "Expected ';' after 'midi.allnotesoff()'");
                    return new_node(NODE_MIDI_ALL_NOTES_OFF, line);
                }
            }
            /* Not stop - rewind and fall through to property parsing */
            p->lex     = saved_lex;
            p->current = saved_cur;
        }
        BAEScript_Node *rd = parse_midi_access(p);
        BAEScript_MidiProp mp = rd->data.midi_prop;
        if (parser_match(p, TOK_ASSIGN)) {
            BAEScript_Node *val = parse_expr(p);
            parser_expect(p, TOK_SEMICOLON, "Expected ';'");
            BAEScript_Node *n = new_node(NODE_MIDI_PROP_SET, line);
            n->data.midi_prop_set.prop  = mp;
            n->data.midi_prop_set.value = val;
            free(rd);
            return n;
        } else {
            /* expression statement (read) */
            parser_expect(p, TOK_SEMICOLON, "Expected ';'");
            BAEScript_Node *n = new_node(NODE_EXPR_STMT, line);
            n->data.expr = rd;
            return n;
        }
    }

    /* mixer.reset() or mixer.prop = expr; */
    if (parser_check(p, TOK_MIXER)) {
        int line = p->current.line;
        parser_advance(p); /* consume 'mixer' */
        /* Check for mixer.reset() */
        {
            BAEScript_Lexer saved_lex = p->lex;
            BAEScript_Token saved_cur = p->current;
            if (parser_match(p, TOK_DOT)) {
                BAEScript_Token tok = p->current;
                if (tok.type == TOK_IDENT && strcmp(tok.value.str, "reset") == 0) {
                    parser_advance(p); /* consume 'reset' */
                    parser_expect(p, TOK_LPAREN,    "Expected '(' after 'mixer.reset'");
                    parser_expect(p, TOK_RPAREN,    "Expected ')' after 'mixer.reset('");
                    parser_expect(p, TOK_SEMICOLON, "Expected ';' after 'mixer.reset()'");
                    return new_node(NODE_MIXER_RESET, line);
                }
            }
            /* Not reset - rewind and parse as property access */
            p->lex     = saved_lex;
            p->current = saved_cur;
        }
        BAEScript_Node *rd = parse_mixer_access(p);
        BAEScript_MixerProp mp = rd->data.mixer_prop;
        if (parser_match(p, TOK_ASSIGN)) {
            BAEScript_Node *val = parse_expr(p);
            parser_expect(p, TOK_SEMICOLON, "Expected ';'");
            BAEScript_Node *n = new_node(NODE_MIXER_PROP_SET, line);
            n->data.mixer_prop_set.prop  = mp;
            n->data.mixer_prop_set.value = val;
            free(rd);
            return n;
        } else {
            parser_expect(p, TOK_SEMICOLON, "Expected ';'");
            BAEScript_Node *n = new_node(NODE_EXPR_STMT, line);
            n->data.expr = rd;
            return n;
        }
    }

    /* exporter.loopcount = expr; */
    if (parser_check(p, TOK_EXPORTER)) {
        int line = p->current.line;
        parser_advance(p); /* consume 'exporter' */
        BAEScript_Node *rd = parse_exporter_access(p);
        BAEScript_ExporterProp ep = rd->data.exporter_prop;
        if (parser_match(p, TOK_ASSIGN)) {
            BAEScript_Node *val = parse_expr(p);
            parser_expect(p, TOK_SEMICOLON, "Expected ';'");
            BAEScript_Node *n = new_node(NODE_EXPORTER_PROP_SET, line);
            n->data.exporter_prop_set.prop  = ep;
            n->data.exporter_prop_set.value = val;
            free(rd);
            return n;
        } else {
            parser_expect(p, TOK_SEMICOLON, "Expected ';'");
            BAEScript_Node *n = new_node(NODE_EXPR_STMT, line);
            n->data.expr = rd;
            return n;
        }
    }

    /* ch[expr].prop = expr; */
    if (parser_check(p, TOK_CH)) {
        /* Could be either read (expression statement) or write (assignment).
         * We parse ch[expr].prop first, then check for '='. */
        int line = p->current.line;
        parser_advance(p); /* consume 'ch' */
        parser_expect(p, TOK_LBRACKET, "Expected '[' after 'ch'");
        BAEScript_Node *chan = parse_expr(p);
        parser_expect(p, TOK_RBRACKET, "Expected ']'");
        parser_expect(p, TOK_DOT,      "Expected '.' after ch[...]");
        BAEScript_ChProp prop = parse_ch_property(p);

        if (parser_match(p, TOK_ASSIGN)) {
            /* ch[expr].prop = expr; */
            BAEScript_Node *val = parse_expr(p);
            parser_expect(p, TOK_SEMICOLON, "Expected ';'");
            BAEScript_Node *n = new_node(NODE_CH_PROP_SET, line);
            n->data.ch_prop_set.channel = chan;
            n->data.ch_prop_set.prop    = prop;
            n->data.ch_prop_set.value   = val;
            return n;
        } else {
            /* expression statement (just reading, rare but allowed) */
            BAEScript_Node *chnode = new_node(NODE_CH_PROP, line);
            chnode->data.ch_prop.channel = chan;
            chnode->data.ch_prop.prop    = prop;
            parser_expect(p, TOK_SEMICOLON, "Expected ';'");
            BAEScript_Node *n = new_node(NODE_EXPR_STMT, line);
            n->data.expr = chnode;
            return n;
        }
    }

    /* identifier - could be assignment (x = ...) or expression statement or noteOn/noteOff */
    if (parser_check(p, TOK_IDENT)) {
        /* peek ahead: IDENT '=' => assignment */
        BAEScript_Lexer saved_lex = p->lex;
        BAEScript_Token saved_cur = p->current;
        BAEScript_Token name = parser_advance(p);

        if (parser_check(p, TOK_ASSIGN)) {
            parser_advance(p); /* consume '=' */
            BAEScript_Node *val = parse_expr(p);
            parser_expect(p, TOK_SEMICOLON, "Expected ';' after assignment");
            BAEScript_Node *n = new_node(NODE_ASSIGN, name.line);
            strncpy(n->data.assign.name, name.value.str, sizeof(n->data.assign.name) - 1);
            n->data.assign.value = val;
            return n;
        }

        /* Not an assignment - rewind and parse as expression statement */
        p->lex     = saved_lex;
        p->current = saved_cur;
        BAEScript_Node *expr = parse_expr(p);
        parser_expect(p, TOK_SEMICOLON, "Expected ';' after expression");
        BAEScript_Node *n = new_node(NODE_EXPR_STMT, name.line);
        n->data.expr = expr;
        return n;
    }

    parser_error(p, "Unexpected token in statement");
    parser_advance(p); /* skip to avoid infinite loop */
    return new_node(NODE_NUMBER, p->current.line);
}

/* ── public entry point ─────────────────────────────────────────────── */

BAEScript_Node *BAEScript_Parse(const char *source)
{
    Parser p;
    parser_init(&p, source);

    BAEScript_Node *program = new_node(NODE_BLOCK, 1);
    while (!parser_check(&p, TOK_EOF) && !p.had_error) {
        block_push(program, parse_statement(&p));
    }

    if (p.had_error) {
        BAEScript_FreeNode(program);
        return NULL;
    }
    return program;
}

/* ── AST cleanup ────────────────────────────────────────────────────── */

void BAEScript_FreeNode(BAEScript_Node *node)
{
    if (!node) return;

    switch (node->type) {
        case NODE_BLOCK:
            for (int i = 0; i < node->data.block.count; i++)
                BAEScript_FreeNode(node->data.block.stmts[i]);
            free(node->data.block.stmts);
            break;

        case NODE_VAR_DECL:
            BAEScript_FreeNode(node->data.var_decl.init);
            break;

        case NODE_ASSIGN:
            BAEScript_FreeNode(node->data.assign.value);
            break;

        case NODE_IF:
            BAEScript_FreeNode(node->data.if_stmt.cond);
            BAEScript_FreeNode(node->data.if_stmt.then_b);
            BAEScript_FreeNode(node->data.if_stmt.else_b);
            break;

        case NODE_WHILE:
            BAEScript_FreeNode(node->data.while_stmt.cond);
            BAEScript_FreeNode(node->data.while_stmt.body);
            break;

        case NODE_PRINT:
            for (int i = 0; i < node->data.print_call.count; i++)
                BAEScript_FreeNode(node->data.print_call.args[i]);
            free(node->data.print_call.args);
            break;

        case NODE_BINOP:
            BAEScript_FreeNode(node->data.binop.left);
            BAEScript_FreeNode(node->data.binop.right);
            break;

        case NODE_UNARYOP:
            BAEScript_FreeNode(node->data.unaryop.operand);
            break;

        case NODE_FUNC_CALL:
            BAEScript_FreeNode(node->data.func_call.a);
            BAEScript_FreeNode(node->data.func_call.b);
            BAEScript_FreeNode(node->data.func_call.c);
            break;

        case NODE_CH_PROP:
            BAEScript_FreeNode(node->data.ch_prop.channel);
            break;

        case NODE_CH_PROP_SET:
            BAEScript_FreeNode(node->data.ch_prop_set.channel);
            BAEScript_FreeNode(node->data.ch_prop_set.value);
            break;

        case NODE_NOTE_ON:
        case NODE_NOTE_OFF:
            BAEScript_FreeNode(node->data.note_cmd.channel);
            BAEScript_FreeNode(node->data.note_cmd.note);
            BAEScript_FreeNode(node->data.note_cmd.velocity);
            break;

        case NODE_MIDI_PROP_SET:
            BAEScript_FreeNode(node->data.midi_prop_set.value);
            break;

        case NODE_MIXER_PROP_SET:
            BAEScript_FreeNode(node->data.mixer_prop_set.value);
            break;

        case NODE_EXPORTER_PROP_SET:
            BAEScript_FreeNode(node->data.exporter_prop_set.value);
            break;

        case NODE_EVENT_DECL:
            BAEScript_FreeNode(node->data.event_decl.body);
            break;

        case NODE_EXPR_STMT:
            BAEScript_FreeNode(node->data.expr);
            break;

        case NODE_NUMBER:
        case NODE_STRING:
        case NODE_BOOL:
        case NODE_IDENT:
        case NODE_MIDI_PROP:
        case NODE_MIXER_PROP:
        case NODE_EXPORTER_PROP:
        case NODE_MIDI_STOP:
        case NODE_MIDI_ALL_NOTES_OFF:
        case NODE_MIXER_RESET:
        case NODE_HELP:
            /* leaf nodes - nothing to free */
            break;
    }
    free(node);
}
