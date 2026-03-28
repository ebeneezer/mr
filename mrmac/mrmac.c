#include "mrmac.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define INITIAL_CODE_CAPACITY 256
#define MAX_SYMBOLS 200
#define MAX_SYMBOL_NAME 20
#define MAX_LABELS 500
#define MAX_PENDING_REFS 1000
#define MAX_STRING_LITERAL 254

typedef struct
{
    char name[MAX_SYMBOL_NAME + 1];
    int type;
} SymbolEntry;

typedef struct
{
    char name[MAX_SYMBOL_NAME + 1];
    int target_pos;
} LabelDef;

typedef enum
{
    REF_GOTO = 1,
    REF_CALL = 2
} PendingRefKind;

typedef struct
{
    char name[MAX_SYMBOL_NAME + 1];
    size_t patch_pos;
    int line;
    PendingRefKind kind;
} PendingRef;

typedef struct
{
    char *name;
    int entry_pos;
    unsigned flags;
} CompiledMacroInfo;

#define MAX_COMPILED_MACROS 256

static unsigned char *g_code = NULL;
static size_t g_code_size = 0;
static size_t g_code_capacity = 0;

static SymbolEntry g_symbols[MAX_SYMBOLS];
static int g_symbol_count = 0;

static LabelDef g_labels[MAX_LABELS];
static int g_label_count = 0;

static PendingRef g_pending_refs[MAX_PENDING_REFS];
static int g_pending_ref_count = 0;

static CompiledMacroInfo g_compiled_macros[MAX_COMPILED_MACROS];
static int g_compiled_macro_count = 0;
static char *g_compiled_macro_file_name = NULL;

static char g_last_error[512];
static int g_last_error_line = 0;

typedef enum
{
    TOK_EOF = 0,
    TOK_IDENTIFIER,
    TOK_STRING_LITERAL,
    TOK_INTEGER_LITERAL,
    TOK_REAL_LITERAL,
    TOK_KEYSPEC,
    TOK_MACRO,
    TOK_MACRO_FILE,
    TOK_END_MACRO,
    TOK_DEF_INT,
    TOK_DEF_STR,
    TOK_DEF_CHAR,
    TOK_DEF_REAL,
    TOK_IF,
    TOK_THEN,
    TOK_ELSE,
    TOK_END,
    TOK_WHILE,
    TOK_DO,
    TOK_TVCALL,
    TOK_CALL,
    TOK_RET,
    TOK_GOTO,
    TOK_TO,
    TOK_FROM,
    TOK_TRANS,
    TOK_DUMP,
    TOK_PERM,
    TOK_ASSIGN,
    TOK_EQ,
    TOK_NE,
    TOK_LE,
    TOK_GE,
    TOK_LT,
    TOK_GT,
    TOK_PLUS,
    TOK_MINUS,
    TOK_MULT,
    TOK_DIV,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    TOK_SHL,
    TOK_SHR,
    TOK_MOD,
    TOK_COMMA,
    TOK_SEMICOLON,
    TOK_COLON,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_ERROR
} TokenKind;

typedef struct
{
    TokenKind kind;
    char *text;
    int ival;
    double rval;
    int line;
} Token;

typedef struct
{
    const char *src;
    const char *p;
    int line;
} Lexer;

typedef struct
{
    Lexer lex;
    Token tok;
    int macro_file_seen;
} Parser;

typedef struct
{
    int type;
} ExprInfo;

static char *xstrdup(const char *s);

static void reset_code_buffer(void)
{
    free(g_code);
    g_code = NULL;
    g_code_size = 0;
    g_code_capacity = 0;
}

static void reset_error_state(void)
{
    g_last_error[0] = '\0';
    g_last_error_line = 0;
}

static void reset_macro_context(void)
{
    g_label_count = 0;
    g_pending_ref_count = 0;
    clear_symbols();
}

static void reset_compiled_macro_info(void)
{
    int i;
    for (i = 0; i < g_compiled_macro_count; ++i)
    {
        free(g_compiled_macros[i].name);
        g_compiled_macros[i].name = NULL;
        g_compiled_macros[i].entry_pos = 0;
        g_compiled_macros[i].flags = 0;
    }
    g_compiled_macro_count = 0;
    free(g_compiled_macro_file_name);
    g_compiled_macro_file_name = NULL;
}

static int add_compiled_macro_info(const char *name, int entry_pos, unsigned flags)
{
    if (g_compiled_macro_count >= MAX_COMPILED_MACROS)
    {
        set_compile_error(0, "Too many macros in source.");
        return -1;
    }

    g_compiled_macros[g_compiled_macro_count].name = xstrdup(name);
    if (g_compiled_macros[g_compiled_macro_count].name == NULL)
    {
        set_compile_error(0, "Out of memory.");
        return -1;
    }
    g_compiled_macros[g_compiled_macro_count].entry_pos = entry_pos;
    g_compiled_macros[g_compiled_macro_count].flags = flags;
    g_compiled_macro_count++;
    return 0;
}

static int set_compiled_macro_file_name(const char *name)
{
    char *dup = xstrdup(name);
    if (dup == NULL)
    {
        set_compile_error(0, "Out of memory.");
        return -1;
    }
    free(g_compiled_macro_file_name);
    g_compiled_macro_file_name = dup;
    return 0;
}

static int ensure_code_capacity(size_t extra)
{
    unsigned char *new_buf;
    size_t required;
    size_t new_capacity;

    if (extra == 0)
        return 0;

    required = g_code_size + extra;
    if (required <= g_code_capacity)
        return 0;

    new_capacity = (g_code_capacity == 0) ? INITIAL_CODE_CAPACITY : g_code_capacity;
    while (new_capacity < required)
        new_capacity *= 2;

    new_buf = (unsigned char *) realloc(g_code, new_capacity);
    if (new_buf == NULL)
    {
        set_compile_error(0, "Out of memory.");
        return -1;
    }

    g_code = new_buf;
    g_code_capacity = new_capacity;
    return 0;
}

void emit_byte(unsigned char byte)
{
    if (ensure_code_capacity(1) != 0)
        return;
    g_code[g_code_size++] = byte;
}

void emit_int(int value)
{
    if (ensure_code_capacity(sizeof(int)) != 0)
        return;
    memcpy(&g_code[g_code_size], &value, sizeof(int));
    g_code_size += sizeof(int);
}

void emit_double(double value)
{
    if (ensure_code_capacity(sizeof(double)) != 0)
        return;
    memcpy(&g_code[g_code_size], &value, sizeof(double));
    g_code_size += sizeof(double);
}

void emit_string(const char *s)
{
    size_t len;

    if (s == NULL)
        s = "";

    len = strlen(s) + 1;
    if (ensure_code_capacity(len) != 0)
        return;

    memcpy(&g_code[g_code_size], s, len);
    g_code_size += len;
}

size_t emit_get_pos(void)
{
    return g_code_size;
}

void emit_patch_int(size_t pos, int value)
{
    if (pos + sizeof(int) > g_code_size)
    {
        set_compile_error(0, "Internal compiler error: invalid patch position.");
        return;
    }
    memcpy(&g_code[pos], &value, sizeof(int));
}

void set_compile_error(int line, const char *msg)
{
    if (g_last_error[0] != '\0')
        return;

    if (msg == NULL)
        msg = "Unknown compile error.";

    g_last_error_line = line;

    if (line > 0)
        snprintf(g_last_error, sizeof(g_last_error), "Line %d: %s", line, msg);
    else
        snprintf(g_last_error, sizeof(g_last_error), "%s", msg);
}

const char *get_last_compile_error(void)
{
    return g_last_error;
}

void clear_symbols(void)
{
    g_symbol_count = 0;
}

int add_symbol(const char *name, int type)
{
    size_t len;
    int i;

    if (name == NULL || *name == '\0')
    {
        set_compile_error(0, "Variable name expected.");
        return -1;
    }

    len = strlen(name);
    if (len > MAX_SYMBOL_NAME)
    {
        set_compile_error(0, "Variable name too long.");
        return -1;
    }

    for (i = 0; i < g_symbol_count; i++)
    {
        if (strcasecmp(g_symbols[i].name, name) == 0)
        {
            set_compile_error(0, "Duplicate variable.");
            return -1;
        }
    }

    if (g_symbol_count >= MAX_SYMBOLS)
    {
        set_compile_error(0, "Out of variables.");
        return -1;
    }

    strcpy(g_symbols[g_symbol_count].name, name);
    g_symbols[g_symbol_count].type = type;
    g_symbol_count++;
    return 0;
}

int lookup_symbol(const char *name, int *out_type)
{
    int i;

    if (name == NULL || *name == '\0')
        return -1;

    for (i = 0; i < g_symbol_count; i++)
    {
        if (strcasecmp(g_symbols[i].name, name) == 0)
        {
            if (out_type != NULL)
                *out_type = g_symbols[i].type;
            return i;
        }
    }

    return -1;
}

static char *xstrdup(const char *s)
{
    size_t len;
    char *copy;

    if (s == NULL)
        s = "";

    len = strlen(s) + 1;
    copy = (char *) malloc(len);
    if (copy != NULL)
        memcpy(copy, s, len);
    return copy;
}

static int is_ident_start(int ch)
{
    return isalpha((unsigned char) ch) || ch == '_';
}

static int is_ident_part(int ch)
{
    return isalnum((unsigned char) ch) || ch == '_';
}

static void token_free(Token *tok)
{
    free(tok->text);
    tok->text = NULL;
}

static void lexer_init(Lexer *lex, const char *source)
{
    lex->src = source;
    lex->p = source;
    lex->line = 1;
}

static void skip_ws_and_comments(Lexer *lex)
{
    for (;;)
    {
        if (*lex->p == '\0')
            return;

        if (*lex->p == ' ' || *lex->p == '\t' || *lex->p == '\r')
        {
            ++lex->p;
            continue;
        }

        if (*lex->p == '\n')
        {
            ++lex->line;
            ++lex->p;
            continue;
        }

        if ((unsigned char) lex->p[0] == 0xEF && (unsigned char) lex->p[1] == 0xBB && (unsigned char) lex->p[2] == 0xBF)
        {
            lex->p += 3;
            continue;
        }

        if (*lex->p == '{')
        {
            int depth = 1;
            ++lex->p;
            while (*lex->p != '\0' && depth > 0)
            {
                if (*lex->p == '{')
                    depth++;
                else if (*lex->p == '}')
                    depth--;
                else if (*lex->p == '\n')
                    lex->line++;
                ++lex->p;
            }
            continue;
        }

        return;
    }
}

static int ident_eq(const char *a, const char *b)
{
    return strcasecmp(a, b) == 0;
}

static void lexer_next(Lexer *lex, Token *tok)
{
    const char *start;
    char *text;
    size_t len;

    token_free(tok);
    tok->kind = TOK_EOF;
    tok->ival = 0;
    tok->rval = 0.0;
    tok->line = lex->line;

    skip_ws_and_comments(lex);
    tok->line = lex->line;

    if (*lex->p == '\0')
    {
        tok->kind = TOK_EOF;
        return;
    }

    if (*lex->p == '$')
    {
        if (isxdigit((unsigned char) lex->p[1]))
        {
            char *endp;
            tok->kind = TOK_INTEGER_LITERAL;
            tok->ival = (int) strtol(lex->p + 1, &endp, 16);
            tok->text = xstrdup("");
            lex->p = endp;
            return;
        }

        if (strncasecmp(lex->p, "$MACRO_FILE", 11) == 0 && !is_ident_part((unsigned char) lex->p[11]))
        {
            tok->kind = TOK_MACRO_FILE;
            tok->text = xstrdup("$MACRO_FILE");
            lex->p += 11;
            return;
        }
        if (strncasecmp(lex->p, "$MACRO", 6) == 0 && !is_ident_part((unsigned char) lex->p[6]))
        {
            tok->kind = TOK_MACRO;
            tok->text = xstrdup("$MACRO");
            lex->p += 6;
            return;
        }
    }

    if (*lex->p == '<')
    {
        const char *q = lex->p + 1;
        while (*q != '\0' && *q != '>' && *q != '\n')
            ++q;
        if (*q == '>')
        {
            len = (size_t) (q - lex->p + 1);
            text = (char *) malloc(len + 1);
            if (text == NULL)
            {
                tok->kind = TOK_ERROR;
                tok->text = xstrdup("Out of memory.");
                return;
            }
            memcpy(text, lex->p, len);
            text[len] = '\0';
            tok->kind = TOK_KEYSPEC;
            tok->text = text;
            lex->p = q + 1;
            return;
        }
    }

    if (*lex->p == '\'')
    {
        char buffer[MAX_STRING_LITERAL + 1];
        size_t out = 0;
        ++lex->p;
        while (*lex->p != '\0')
        {
            if (*lex->p == '\'')
            {
                if (lex->p[1] == '\'')
                {
                    if (out >= MAX_STRING_LITERAL)
                    {
                        tok->kind = TOK_ERROR;
                        tok->text = xstrdup("String constant too long.");
                        return;
                    }
                    buffer[out++] = '\'';
                    lex->p += 2;
                    continue;
                }
                ++lex->p;
                buffer[out] = '\0';
                tok->kind = TOK_STRING_LITERAL;
                tok->text = xstrdup(buffer);
                return;
            }
            if (*lex->p == '\n')
                lex->line++;
            if (out >= MAX_STRING_LITERAL)
            {
                tok->kind = TOK_ERROR;
                tok->text = xstrdup("String constant too long.");
                return;
            }
            buffer[out++] = *lex->p++;
        }
        tok->kind = TOK_ERROR;
        tok->text = xstrdup("Premature end of file.");
        return;
    }

    if (isdigit((unsigned char) *lex->p))
    {
        const char *q = lex->p;
        int is_real = 0;
        char *endp;

        while (isdigit((unsigned char) *q))
            ++q;
        if (*q == '.' && isdigit((unsigned char) q[1]))
        {
            is_real = 1;
            ++q;
            while (isdigit((unsigned char) *q))
                ++q;
        }
        if (*q == 'e' || *q == 'E')
        {
            const char *r = q + 1;
            if (*r == '+' || *r == '-')
                ++r;
            if (isdigit((unsigned char) *r))
            {
                is_real = 1;
                q = r + 1;
                while (isdigit((unsigned char) *q))
                    ++q;
            }
        }

        len = (size_t) (q - lex->p);
        text = (char *) malloc(len + 1);
        if (text == NULL)
        {
            tok->kind = TOK_ERROR;
            tok->text = xstrdup("Out of memory.");
            return;
        }
        memcpy(text, lex->p, len);
        text[len] = '\0';
        lex->p = q;

        if (is_real)
        {
            tok->kind = TOK_REAL_LITERAL;
            tok->rval = strtod(text, &endp);
        }
        else
        {
            tok->kind = TOK_INTEGER_LITERAL;
            tok->ival = (int) strtol(text, &endp, 10);
        }
        tok->text = text;
        return;
    }

    if (is_ident_start((unsigned char) *lex->p))
    {
        start = lex->p;
        ++lex->p;
        while (is_ident_part((unsigned char) *lex->p))
            ++lex->p;

        len = (size_t) (lex->p - start);
        text = (char *) malloc(len + 1);
        if (text == NULL)
        {
            tok->kind = TOK_ERROR;
            tok->text = xstrdup("Out of memory.");
            return;
        }
        memcpy(text, start, len);
        text[len] = '\0';
        tok->text = text;

        if (ident_eq(text, "END_MACRO")) tok->kind = TOK_END_MACRO;
        else if (ident_eq(text, "DEF_INT")) tok->kind = TOK_DEF_INT;
        else if (ident_eq(text, "DEF_STR")) tok->kind = TOK_DEF_STR;
        else if (ident_eq(text, "DEF_CHAR")) tok->kind = TOK_DEF_CHAR;
        else if (ident_eq(text, "DEF_REAL")) tok->kind = TOK_DEF_REAL;
        else if (ident_eq(text, "IF")) tok->kind = TOK_IF;
        else if (ident_eq(text, "THEN")) tok->kind = TOK_THEN;
        else if (ident_eq(text, "ELSE")) tok->kind = TOK_ELSE;
        else if (ident_eq(text, "END")) tok->kind = TOK_END;
        else if (ident_eq(text, "WHILE")) tok->kind = TOK_WHILE;
        else if (ident_eq(text, "DO")) tok->kind = TOK_DO;
        else if (ident_eq(text, "TVCALL")) tok->kind = TOK_TVCALL;
        else if (ident_eq(text, "CALL")) tok->kind = TOK_CALL;
        else if (ident_eq(text, "RET")) tok->kind = TOK_RET;
        else if (ident_eq(text, "GOTO")) tok->kind = TOK_GOTO;
        else if (ident_eq(text, "TO")) tok->kind = TOK_TO;
        else if (ident_eq(text, "FROM")) tok->kind = TOK_FROM;
        else if (ident_eq(text, "TRANS")) tok->kind = TOK_TRANS;
        else if (ident_eq(text, "DUMP")) tok->kind = TOK_DUMP;
        else if (ident_eq(text, "PERM")) tok->kind = TOK_PERM;
        else if (ident_eq(text, "AND")) tok->kind = TOK_AND;
        else if (ident_eq(text, "OR")) tok->kind = TOK_OR;
        else if (ident_eq(text, "NOT")) tok->kind = TOK_NOT;
        else if (ident_eq(text, "SHL")) tok->kind = TOK_SHL;
        else if (ident_eq(text, "SHR")) tok->kind = TOK_SHR;
        else if (ident_eq(text, "MOD")) tok->kind = TOK_MOD;
        else tok->kind = TOK_IDENTIFIER;
        return;
    }

    if (lex->p[0] == ':' && lex->p[1] == '=')
    {
        tok->kind = TOK_ASSIGN;
        tok->text = xstrdup(":=");
        lex->p += 2;
        return;
    }
    if (lex->p[0] == '<' && lex->p[1] == '>')
    {
        tok->kind = TOK_NE;
        tok->text = xstrdup("<>");
        lex->p += 2;
        return;
    }
    if (lex->p[0] == '<' && lex->p[1] == '=')
    {
        tok->kind = TOK_LE;
        tok->text = xstrdup("<=");
        lex->p += 2;
        return;
    }
    if (lex->p[0] == '>' && lex->p[1] == '=')
    {
        tok->kind = TOK_GE;
        tok->text = xstrdup(">=");
        lex->p += 2;
        return;
    }

    switch (*lex->p)
    {
    case '=': tok->kind = TOK_EQ; tok->text = xstrdup("="); ++lex->p; return;
    case '<': tok->kind = TOK_LT; tok->text = xstrdup("<"); ++lex->p; return;
    case '>': tok->kind = TOK_GT; tok->text = xstrdup(">"); ++lex->p; return;
    case '+': tok->kind = TOK_PLUS; tok->text = xstrdup("+"); ++lex->p; return;
    case '-': tok->kind = TOK_MINUS; tok->text = xstrdup("-"); ++lex->p; return;
    case '*': tok->kind = TOK_MULT; tok->text = xstrdup("*"); ++lex->p; return;
    case '/': tok->kind = TOK_DIV; tok->text = xstrdup("/"); ++lex->p; return;
    case ',': tok->kind = TOK_COMMA; tok->text = xstrdup(","); ++lex->p; return;
    case ';': tok->kind = TOK_SEMICOLON; tok->text = xstrdup(";"); ++lex->p; return;
    case ':': tok->kind = TOK_COLON; tok->text = xstrdup(":"); ++lex->p; return;
    case '(': tok->kind = TOK_LPAREN; tok->text = xstrdup("("); ++lex->p; return;
    case ')': tok->kind = TOK_RPAREN; tok->text = xstrdup(")"); ++lex->p; return;
    default:
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "Unexpected character: '%c'", *lex->p);
            tok->kind = TOK_ERROR;
            tok->text = xstrdup(buf);
            ++lex->p;
            return;
        }
    }
}

static void parser_init(Parser *ps, const char *source)
{
    memset(ps, 0, sizeof(*ps));
    lexer_init(&ps->lex, source);
    ps->tok.text = NULL;
    lexer_next(&ps->lex, &ps->tok);
}

static void parser_next(Parser *ps)
{
    lexer_next(&ps->lex, &ps->tok);
}

static int parser_accept(Parser *ps, TokenKind kind)
{
    if (ps->tok.kind == kind)
    {
        parser_next(ps);
        return 1;
    }
    return 0;
}

static int parser_expect(Parser *ps, TokenKind kind, const char *msg)
{
    if (ps->tok.kind != kind)
    {
        set_compile_error(ps->tok.line, msg);
        return -1;
    }
    parser_next(ps);
    return 0;
}

static int is_numeric_type(int type)
{
    return type == TYPE_INT || type == TYPE_REAL;
}

static int is_stringlike_type(int type)
{
    return type == TYPE_STR || type == TYPE_CHAR;
}

static int validate_keyspec(const char *text, int line)
{
    size_t len;

    if (text == NULL)
    {
        set_compile_error(line, "Keycode expected.");
        return -1;
    }

    len = strlen(text);
    if (len < 3 || text[0] != '<' || text[len - 1] != '>')
    {
        set_compile_error(line, "Keycode expected.");
        return -1;
    }
    return 0;
}

static int validate_mode(const char *text, int line)
{
    if (text == NULL)
    {
        set_compile_error(line, "Mode expected.");
        return -1;
    }
    if (strcasecmp(text, "EDIT") == 0 || strcasecmp(text, "DOS_SHELL") == 0 || strcasecmp(text, "ALL") == 0)
        return 0;
    set_compile_error(line, "Mode expected.");
    return -1;
}

static int find_label_index(const char *name)
{
    int i;
    for (i = 0; i < g_label_count; ++i)
        if (strcasecmp(g_labels[i].name, name) == 0)
            return i;
    return -1;
}

static int define_label(const char *name, int line)
{
    size_t len = strlen(name);

    if (len == 0 || len > MAX_SYMBOL_NAME)
    {
        set_compile_error(line, "Label too long.");
        return -1;
    }
    if (g_label_count >= MAX_LABELS)
    {
        set_compile_error(line, "Too many labels.");
        return -1;
    }
    if (find_label_index(name) >= 0)
    {
        set_compile_error(line, "Duplicate label.");
        return -1;
    }

    strncpy(g_labels[g_label_count].name, name, MAX_SYMBOL_NAME);
    g_labels[g_label_count].name[MAX_SYMBOL_NAME] = '\0';
    g_labels[g_label_count].target_pos = (int) emit_get_pos();
    g_label_count++;
    return 0;
}

static int add_pending_ref(const char *name, size_t patch_pos, int line, PendingRefKind kind)
{
    size_t len = strlen(name);

    if (len == 0 || len > MAX_SYMBOL_NAME)
    {
        set_compile_error(line, "Label too long.");
        return -1;
    }
    if (g_pending_ref_count >= MAX_PENDING_REFS)
    {
        set_compile_error(line, "Too many labels.");
        return -1;
    }

    strncpy(g_pending_refs[g_pending_ref_count].name, name, MAX_SYMBOL_NAME);
    g_pending_refs[g_pending_ref_count].name[MAX_SYMBOL_NAME] = '\0';
    g_pending_refs[g_pending_ref_count].patch_pos = patch_pos;
    g_pending_refs[g_pending_ref_count].line = line;
    g_pending_refs[g_pending_ref_count].kind = kind;
    g_pending_ref_count++;
    return 0;
}

static int resolve_pending_refs(void)
{
    int i;
    char err_buf[256];

    for (i = 0; i < g_pending_ref_count; ++i)
    {
        int idx = find_label_index(g_pending_refs[i].name);
        if (idx < 0)
        {
            snprintf(err_buf, sizeof(err_buf), "Label %s not found.", g_pending_refs[i].name);
            set_compile_error(g_pending_refs[i].line, err_buf);
            return -1;
        }
        emit_patch_int(g_pending_refs[i].patch_pos, g_labels[idx].target_pos);
    }
    return 0;
}

static int begin_macro(const char *name)
{
    (void) name;
    reset_macro_context();
    return 0;
}

static int emit_define_variable(const char *name, int type)
{
    emit_byte(OP_DEF_VAR);
    emit_byte((unsigned char) type);
    emit_string(name);
    return 0;
}

static int can_assign_type(int target, int source)
{
    if (target == TYPE_INT)
        return source == TYPE_INT;
    if (target == TYPE_REAL)
        return source == TYPE_REAL || source == TYPE_INT;
    if (target == TYPE_STR)
        return source == TYPE_STR || source == TYPE_CHAR;
    if (target == TYPE_CHAR)
        return source == TYPE_CHAR || source == TYPE_STR;
    return 0;
}

static int lookup_builtin_constant(const char *name, int *out_value)
{
    struct BuiltinConstant { const char *name; int value; };
    static const struct BuiltinConstant constants[] = {
        {"TRUE", 1}, {"FALSE", 0},
        {"BLACK", 0}, {"BLUE", 1}, {"GREEN", 2}, {"CYAN", 3},
        {"RED", 4}, {"MAGENTA", 5}, {"BROWN", 6}, {"LIGHTGRAY", 7},
        {"DARKGRAY", 8}, {"LIGHTBLUE", 9}, {"LIGHTGREEN", 10}, {"LIGHTCYAN", 11},
        {"LIGHTRED", 12}, {"LIGHTMAGENTA", 13}, {"YELLOW", 14}, {"WHITE", 15},
        {"EDIT", 0}, {"DOS_SHELL", 1}, {"ALL", 255},
        {NULL, 0}
    };
    int i;
    for (i = 0; constants[i].name != NULL; ++i)
        if (strcasecmp(name, constants[i].name) == 0)
        {
            if (out_value != NULL)
                *out_value = constants[i].value;
            return 1;
        }
    return 0;
}

static int lookup_builtin_variable(const char *name, int *out_type)
{
    if (strcasecmp(name, "RETURN_INT") == 0 || strcasecmp(name, "ERROR_LEVEL") == 0 ||
        strcasecmp(name, "FIRST_RUN") == 0 || strcasecmp(name, "IGNORE_CASE") == 0 ||
        strcasecmp(name, "FIRST_SAVE") == 0 || strcasecmp(name, "EOF_IN_MEM") == 0 ||
        strcasecmp(name, "BUFFER_ID") == 0 || strcasecmp(name, "TMP_FILE") == 0 ||
        strcasecmp(name, "FILE_CHANGED") == 0 || strcasecmp(name, "PARAM_COUNT") == 0 ||
        strcasecmp(name, "CPU") == 0 || strcasecmp(name, "C_COL") == 0 ||
        strcasecmp(name, "C_LINE") == 0 || strcasecmp(name, "C_ROW") == 0 ||
        strcasecmp(name, "AT_EOF") == 0 ||
        strcasecmp(name, "AT_EOL") == 0 ||
        strcasecmp(name, "BLOCK_STAT") == 0 || strcasecmp(name, "BLOCK_LINE1") == 0 ||
        strcasecmp(name, "BLOCK_LINE2") == 0 || strcasecmp(name, "BLOCK_COL1") == 0 ||
        strcasecmp(name, "BLOCK_COL2") == 0 || strcasecmp(name, "MARKING") == 0 ||
        strcasecmp(name, "TAB_EXPAND") == 0 || strcasecmp(name, "INSERT_MODE") == 0 ||
        strcasecmp(name, "INDENT_LEVEL") == 0)
    {
        if (out_type != NULL)
            *out_type = TYPE_INT;
        return 1;
    }
    if (strcasecmp(name, "RETURN_STR") == 0 || strcasecmp(name, "MPARM_STR") == 0 ||
        strcasecmp(name, "DATE") == 0 || strcasecmp(name, "TIME") == 0 ||
        strcasecmp(name, "FIRST_MACRO") == 0 || strcasecmp(name, "NEXT_MACRO") == 0 ||
        strcasecmp(name, "LAST_FILE_NAME") == 0 || strcasecmp(name, "TMP_FILE_NAME") == 0 ||
        strcasecmp(name, "FILE_NAME") == 0 || strcasecmp(name, "COMSPEC") == 0 ||
        strcasecmp(name, "MR_PATH") == 0 || strcasecmp(name, "OS_VERSION") == 0 ||
        strcasecmp(name, "GET_LINE") == 0)
    {
        if (out_type != NULL)
            *out_type = TYPE_STR;
        return 1;
    }
    if (strcasecmp(name, "CUR_CHAR") == 0)
    {
        if (out_type != NULL)
            *out_type = TYPE_CHAR;
        return 1;
    }
    return 0;
}

static int binary_precedence(TokenKind kind)
{
    switch (kind)
    {
    case TOK_OR: return 1;
    case TOK_AND: return 2;
    case TOK_EQ:
    case TOK_NE:
    case TOK_LT:
    case TOK_GT:
    case TOK_LE:
    case TOK_GE:
        return 3;
    case TOK_SHL:
    case TOK_SHR:
        return 4;
    case TOK_PLUS:
    case TOK_MINUS:
        return 5;
    case TOK_MULT:
    case TOK_DIV:
    case TOK_MOD:
        return 6;
    default:
        return -1;
    }
}

static int emit_intrinsic_call(const char *name, int argc)
{
    emit_byte(OP_INTRINSIC);
    emit_string(name);
    emit_byte((unsigned char) argc);
    return 0;
}

static int emit_proc_call(const char *name, int argc)
{
    emit_byte(OP_PROC);
    emit_string(name);
    emit_byte((unsigned char) argc);
    return 0;
}

static int emit_proc_var_call(const char *name, const char *var_name)
{
    emit_byte(OP_PROC_VAR);
    emit_string(name);
    emit_string(var_name);
    return 0;
}

static int parse_expression(Parser *ps, int min_prec, ExprInfo *out);

static int parse_argument_expressions(Parser *ps, ExprInfo *arg_types, int *out_count, int max_args)
{
    int count = 0;

    if (ps->tok.kind == TOK_RPAREN)
    {
        *out_count = 0;
        return 0;
    }

    for (;;)
    {
        if (count >= max_args)
        {
            set_compile_error(ps->tok.line, "Too many arguments.");
            return -1;
        }
        if (parse_expression(ps, 1, &arg_types[count]) != 0)
            return -1;
        count++;
        if (!parser_accept(ps, TOK_COMMA))
            break;
    }

    *out_count = count;
    return 0;
}

static int parse_primary(Parser *ps, ExprInfo *out)
{
    char *name;
    int var_type;

    if (ps->tok.kind == TOK_INTEGER_LITERAL)
    {
        emit_byte(OP_PUSH_I);
        emit_int(ps->tok.ival);
        out->type = TYPE_INT;
        parser_next(ps);
        return 0;
    }

    if (ps->tok.kind == TOK_REAL_LITERAL)
    {
        emit_byte(OP_PUSH_R);
        emit_double(ps->tok.rval);
        out->type = TYPE_REAL;
        parser_next(ps);
        return 0;
    }

    if (ps->tok.kind == TOK_STRING_LITERAL)
    {
        emit_byte(OP_PUSH_S);
        emit_string(ps->tok.text);
        out->type = TYPE_STR;
        parser_next(ps);
        return 0;
    }

    if (parser_accept(ps, TOK_LPAREN))
    {
        if (parse_expression(ps, 1, out) != 0)
            return -1;
        return parser_expect(ps, TOK_RPAREN, "')' expected.");
    }

    if (ps->tok.kind == TOK_IDENTIFIER)
    {
        ExprInfo args[8];
        int argc = 0;
        int line = ps->tok.line;
        name = xstrdup(ps->tok.text);
        if (name == NULL)
        {
            set_compile_error(ps->tok.line, "Out of memory.");
            return -1;
        }
        parser_next(ps);

        {
            int builtin_value;
            int builtin_var_type;
            if (lookup_builtin_constant(name, &builtin_value))
            {
                emit_byte(OP_PUSH_I);
                emit_int(builtin_value);
                out->type = TYPE_INT;
                free(name);
                return 0;
            }
            if (lookup_builtin_variable(name, &builtin_var_type))
            {
                emit_byte(OP_LOAD_VAR);
                emit_string(name);
                out->type = builtin_var_type;
                free(name);
                return 0;
            }
        }

        if (strcasecmp(name, "NEXT_FILE") == 0)
        {
            emit_intrinsic_call("NEXT_FILE", 0);
            out->type = TYPE_INT;
            free(name);
            return 0;
        }

        if (parser_accept(ps, TOK_LPAREN))
        {
            if (strcasecmp(name, "VAL") == 0 || strcasecmp(name, "RVAL") == 0)
            {
                char *target_name;
                int target_type;
                ExprInfo source_expr;
                int is_real_target = (strcasecmp(name, "RVAL") == 0);

                if (ps->tok.kind != TOK_IDENTIFIER)
                {
                    set_compile_error(ps->tok.line, "Variable expected.");
                    free(name);
                    return -1;
                }

                target_name = xstrdup(ps->tok.text);
                if (target_name == NULL)
                {
                    set_compile_error(ps->tok.line, "Out of memory.");
                    free(name);
                    return -1;
                }
                if (lookup_symbol(target_name, &target_type) < 0)
                {
                    set_compile_error(ps->tok.line, "Variable expected.");
                    free(target_name);
                    free(name);
                    return -1;
                }
                if ((is_real_target && target_type != TYPE_REAL) || (!is_real_target && target_type != TYPE_INT))
                {
                    set_compile_error(ps->tok.line, "Type mismatch or syntax error.");
                    free(target_name);
                    free(name);
                    return -1;
                }
                parser_next(ps);
                if (parser_expect(ps, TOK_COMMA, "',' expected.") != 0)
                {
                    free(target_name);
                    free(name);
                    return -1;
                }
                if (parse_expression(ps, 1, &source_expr) != 0)
                {
                    free(target_name);
                    free(name);
                    return -1;
                }
                if (!is_stringlike_type(source_expr.type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(target_name);
                    free(name);
                    return -1;
                }
                if (parser_expect(ps, TOK_RPAREN, "')' expected.") != 0)
                {
                    free(target_name);
                    free(name);
                    return -1;
                }

                emit_byte(is_real_target ? OP_RVAL : OP_VAL);
                emit_string(target_name);
                out->type = TYPE_INT;
                free(target_name);
                free(name);
                return 0;
            }

            if (strcasecmp(name, "FIRST_GLOBAL") == 0 || strcasecmp(name, "NEXT_GLOBAL") == 0)
            {
                char *target_name;
                int target_type;
                if (ps->tok.kind != TOK_IDENTIFIER)
                {
                    set_compile_error(ps->tok.line, "Variable expected.");
                    free(name);
                    return -1;
                }
                target_name = xstrdup(ps->tok.text);
                if (target_name == NULL)
                {
                    set_compile_error(ps->tok.line, "Out of memory.");
                    free(name);
                    return -1;
                }
                if (lookup_symbol(target_name, &target_type) < 0 || target_type != TYPE_INT)
                {
                    set_compile_error(ps->tok.line, "Type mismatch or syntax error.");
                    free(target_name);
                    free(name);
                    return -1;
                }
                parser_next(ps);
                if (parser_expect(ps, TOK_RPAREN, "')' expected.") != 0)
                {
                    free(target_name);
                    free(name);
                    return -1;
                }
                emit_byte(strcasecmp(name, "FIRST_GLOBAL") == 0 ? OP_FIRST_GLOBAL : OP_NEXT_GLOBAL);
                emit_string(target_name);
                out->type = TYPE_STR;
                free(target_name);
                free(name);
                return 0;
            }

            if (parse_argument_expressions(ps, args, &argc, 8) != 0)
            {
                free(name);
                return -1;
            }
            if (parser_expect(ps, TOK_RPAREN, "')' expected.") != 0)
            {
                free(name);
                return -1;
            }

            if (strcasecmp(name, "STR") == 0)
            {
                if (argc != 1 || args[0].type != TYPE_INT)
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("STR", argc);
                out->type = TYPE_STR;
            }
            else if (strcasecmp(name, "CHAR") == 0)
            {
                if (argc != 1 || args[0].type != TYPE_INT)
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("CHAR", argc);
                out->type = TYPE_CHAR;
            }
            else if (strcasecmp(name, "ASCII") == 0)
            {
                if (argc != 1 || !is_stringlike_type(args[0].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("ASCII", argc);
                out->type = TYPE_INT;
            }
            else if (strcasecmp(name, "CAPS") == 0)
            {
                if (argc != 1 || !is_stringlike_type(args[0].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("CAPS", argc);
                out->type = TYPE_STR;
            }
            else if (strcasecmp(name, "COPY") == 0)
            {
                if (argc != 3 || !is_stringlike_type(args[0].type) || args[1].type != TYPE_INT || args[2].type != TYPE_INT)
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("COPY", argc);
                out->type = TYPE_STR;
            }
            else if (strcasecmp(name, "LENGTH") == 0)
            {
                if (argc != 1 || !is_stringlike_type(args[0].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("LENGTH", argc);
                out->type = TYPE_INT;
            }
            else if (strcasecmp(name, "POS") == 0)
            {
                if (argc != 2 || !is_stringlike_type(args[0].type) || !is_stringlike_type(args[1].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("POS", argc);
                out->type = TYPE_INT;
            }
            else if (strcasecmp(name, "XPOS") == 0)
            {
                if (argc != 3 || !is_stringlike_type(args[0].type) || !is_stringlike_type(args[1].type) || args[2].type != TYPE_INT)
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("XPOS", argc);
                out->type = TYPE_INT;
            }
            else if (strcasecmp(name, "STR_DEL") == 0)
            {
                if (argc != 3 || !is_stringlike_type(args[0].type) || args[1].type != TYPE_INT || args[2].type != TYPE_INT)
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("STR_DEL", argc);
                out->type = TYPE_STR;
            }
            else if (strcasecmp(name, "STR_INS") == 0)
            {
                if (argc != 3 || !is_stringlike_type(args[0].type) || !is_stringlike_type(args[1].type) || args[2].type != TYPE_INT)
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("STR_INS", argc);
                out->type = TYPE_STR;
            }
            else if (strcasecmp(name, "REAL_I") == 0)
            {
                if (argc != 1 || args[0].type != TYPE_INT)
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("REAL_I", argc);
                out->type = TYPE_REAL;
            }
            else if (strcasecmp(name, "INT_R") == 0)
            {
                if (argc != 1 || args[0].type != TYPE_REAL)
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("INT_R", argc);
                out->type = TYPE_INT;
            }
            else if (strcasecmp(name, "RSTR") == 0)
            {
                if (argc != 3 || args[0].type != TYPE_REAL || args[1].type != TYPE_INT || args[2].type != TYPE_INT)
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("RSTR", argc);
                out->type = TYPE_STR;
            }
            else if (strcasecmp(name, "REMOVE_SPACE") == 0)
            {
                if (argc != 1 || !is_stringlike_type(args[0].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("REMOVE_SPACE", argc);
                out->type = TYPE_STR;
            }
            else if (strcasecmp(name, "GET_EXTENSION") == 0)
            {
                if (argc != 1 || !is_stringlike_type(args[0].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("GET_EXTENSION", argc);
                out->type = TYPE_STR;
            }
            else if (strcasecmp(name, "GET_PATH") == 0)
            {
                if (argc != 1 || !is_stringlike_type(args[0].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("GET_PATH", argc);
                out->type = TYPE_STR;
            }
            else if (strcasecmp(name, "TRUNCATE_EXTENSION") == 0)
            {
                if (argc != 1 || !is_stringlike_type(args[0].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("TRUNCATE_EXTENSION", argc);
                out->type = TYPE_STR;
            }
            else if (strcasecmp(name, "TRUNCATE_PATH") == 0)
            {
                if (argc != 1 || !is_stringlike_type(args[0].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("TRUNCATE_PATH", argc);
                out->type = TYPE_STR;
            }
            else if (strcasecmp(name, "FILE_EXISTS") == 0)
            {
                if (argc != 1 || !is_stringlike_type(args[0].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("FILE_EXISTS", argc);
                out->type = TYPE_INT;
            }
            else if (strcasecmp(name, "FIRST_FILE") == 0)
            {
                if (argc != 1 || !is_stringlike_type(args[0].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("FIRST_FILE", argc);
                out->type = TYPE_INT;
            }
            else if (strcasecmp(name, "NEXT_FILE") == 0)
            {
                if (argc != 0)
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("NEXT_FILE", argc);
                out->type = TYPE_INT;
            }
            else if (strcasecmp(name, "GET_ENVIRONMENT") == 0)
            {
                if (argc != 1 || !is_stringlike_type(args[0].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("GET_ENVIRONMENT", argc);
                out->type = TYPE_STR;
            }
            else if (strcasecmp(name, "GET_WORD") == 0)
            {
                if (argc != 1 || !is_stringlike_type(args[0].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("GET_WORD", argc);
                out->type = TYPE_STR;
            }
            else if (strcasecmp(name, "PARAM_STR") == 0)
            {
                if (argc != 1 || args[0].type != TYPE_INT)
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("PARAM_STR", argc);
                out->type = TYPE_STR;
            }
            else if (strcasecmp(name, "GLOBAL_STR") == 0)
            {
                if (argc != 1 || !is_stringlike_type(args[0].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("GLOBAL_STR", argc);
                out->type = TYPE_STR;
            }
            else if (strcasecmp(name, "GLOBAL_INT") == 0)
            {
                if (argc != 1 || !is_stringlike_type(args[0].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("GLOBAL_INT", argc);
                out->type = TYPE_INT;
            }
            else if (strcasecmp(name, "PARSE_STR") == 0)
            {
                if (argc != 2 || !is_stringlike_type(args[0].type) || !is_stringlike_type(args[1].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("PARSE_STR", argc);
                out->type = TYPE_STR;
            }
            else if (strcasecmp(name, "PARSE_INT") == 0)
            {
                if (argc != 2 || !is_stringlike_type(args[0].type) || !is_stringlike_type(args[1].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("PARSE_INT", argc);
                out->type = TYPE_INT;
            }
            else if (strcasecmp(name, "INQ_MACRO") == 0)
            {
                if (argc != 1 || !is_stringlike_type(args[0].type))
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call("INQ_MACRO", argc);
                out->type = TYPE_INT;
            }
            else if (strcasecmp(name, "SEARCH_FWD") == 0 || strcasecmp(name, "SEARCH_BWD") == 0)
            {
                if (argc != 2 || !is_stringlike_type(args[0].type) || args[1].type != TYPE_INT)
                {
                    set_compile_error(line, "Type mismatch or syntax error.");
                    free(name);
                    return -1;
                }
                emit_intrinsic_call(strcasecmp(name, "SEARCH_FWD") == 0 ? "SEARCH_FWD" : "SEARCH_BWD", argc);
                out->type = TYPE_INT;
            }
            else
            {
                set_compile_error(line, "Type mismatch or syntax error.");
                free(name);
                return -1;
            }

            free(name);
            return 0;
        }

        if (lookup_symbol(name, &var_type) < 0)
        {
            set_compile_error(ps->tok.line, "Variable expected.");
            free(name);
            return -1;
        }
        emit_byte(OP_LOAD_VAR);
        emit_string(name);
        out->type = var_type;
        free(name);
        return 0;
    }

    set_compile_error(ps->tok.line, "Type mismatch or syntax error.");
    return -1;
}

static int parse_unary(Parser *ps, ExprInfo *out)
{
    if (parser_accept(ps, TOK_MINUS))
    {
        if (parse_unary(ps, out) != 0)
            return -1;
        if (!is_numeric_type(out->type))
        {
            set_compile_error(ps->tok.line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_byte(OP_NEG);
        return 0;
    }

    if (parser_accept(ps, TOK_NOT))
    {
        if (parse_unary(ps, out) != 0)
            return -1;
        if (out->type != TYPE_INT)
        {
            set_compile_error(ps->tok.line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_byte(OP_NOT);
        out->type = TYPE_INT;
        return 0;
    }

    return parse_primary(ps, out);
}

static int combine_binary(TokenKind op, int line, ExprInfo *lhs, const ExprInfo *rhs)
{
    if (op == TOK_PLUS)
    {
        if (is_stringlike_type(lhs->type) && is_stringlike_type(rhs->type))
        {
            emit_byte(OP_ADD);
            lhs->type = TYPE_STR;
            return 0;
        }
        if (is_numeric_type(lhs->type) && is_numeric_type(rhs->type))
        {
            emit_byte(OP_ADD);
            lhs->type = (lhs->type == TYPE_REAL || rhs->type == TYPE_REAL) ? TYPE_REAL : TYPE_INT;
            return 0;
        }
    }
    else if (op == TOK_MINUS || op == TOK_MULT || op == TOK_DIV)
    {
        if (is_numeric_type(lhs->type) && is_numeric_type(rhs->type))
        {
            if (op == TOK_MINUS) emit_byte(OP_SUB);
            else if (op == TOK_MULT) emit_byte(OP_MUL);
            else emit_byte(OP_DIV);
            lhs->type = (lhs->type == TYPE_REAL || rhs->type == TYPE_REAL) ? TYPE_REAL : TYPE_INT;
            return 0;
        }
    }
    else if (op == TOK_MOD || op == TOK_SHL || op == TOK_SHR || op == TOK_AND || op == TOK_OR)
    {
        if (lhs->type == TYPE_INT && rhs->type == TYPE_INT)
        {
            if (op == TOK_MOD) emit_byte(OP_MOD);
            else if (op == TOK_SHL) emit_byte(OP_SHL);
            else if (op == TOK_SHR) emit_byte(OP_SHR);
            else if (op == TOK_AND) emit_byte(OP_AND);
            else emit_byte(OP_OR);
            lhs->type = TYPE_INT;
            return 0;
        }
    }
    else if (op == TOK_EQ || op == TOK_NE)
    {
        if ((is_numeric_type(lhs->type) && is_numeric_type(rhs->type)) ||
            (is_stringlike_type(lhs->type) && is_stringlike_type(rhs->type)))
        {
            emit_byte(op == TOK_EQ ? OP_CMP_EQ : OP_CMP_NE);
            lhs->type = TYPE_INT;
            return 0;
        }
    }
    else if (op == TOK_LT || op == TOK_GT || op == TOK_LE || op == TOK_GE)
    {
        if (is_numeric_type(lhs->type) && is_numeric_type(rhs->type))
        {
            if (op == TOK_LT) emit_byte(OP_CMP_LT);
            else if (op == TOK_GT) emit_byte(OP_CMP_GT);
            else if (op == TOK_LE) emit_byte(OP_CMP_LE);
            else emit_byte(OP_CMP_GE);
            lhs->type = TYPE_INT;
            return 0;
        }
    }

    set_compile_error(line, "Type mismatch or syntax error.");
    return -1;
}

static int parse_expression(Parser *ps, int min_prec, ExprInfo *out)
{
    ExprInfo lhs;

    if (parse_unary(ps, &lhs) != 0)
        return -1;

    while (1)
    {
        int prec = binary_precedence(ps->tok.kind);
        TokenKind op = ps->tok.kind;
        int line = ps->tok.line;
        ExprInfo rhs;

        if (prec < min_prec)
            break;

        parser_next(ps);
        if (parse_expression(ps, prec + 1, &rhs) != 0)
            return -1;
        if (combine_binary(op, line, &lhs, &rhs) != 0)
            return -1;
    }

    *out = lhs;
    return 0;
}

static int parse_statement_list(Parser *ps, TokenKind end1, TokenKind end2, TokenKind end3);

static int parse_variable_declaration(Parser *ps, int decl_type)
{
    if (parser_expect(ps, TOK_LPAREN, "'(' expected.") != 0)
        return -1;

    for (;;)
    {
        char *name;
        if (ps->tok.kind != TOK_IDENTIFIER)
        {
            set_compile_error(ps->tok.line, "Variable name expected.");
            return -1;
        }
        name = xstrdup(ps->tok.text);
        if (name == NULL)
        {
            set_compile_error(ps->tok.line, "Out of memory.");
            return -1;
        }
        if (add_symbol(name, decl_type) != 0)
        {
            free(name);
            return -1;
        }
        emit_define_variable(name, decl_type);
        free(name);
        parser_next(ps);
        if (!parser_accept(ps, TOK_COMMA))
            break;
    }

    return parser_expect(ps, TOK_RPAREN, "')' expected.");
}

static int parse_assignment_after_name(Parser *ps, const char *name, int line)
{
    ExprInfo expr;
    int var_type = 0;

    if (lookup_symbol(name, &var_type) < 0 && !lookup_builtin_variable(name, &var_type))
    {
        set_compile_error(line, "Variable expected.");
        return -1;
    }

    if (parser_expect(ps, TOK_ASSIGN, "':=' expected.") != 0)
        return -1;
    if (parse_expression(ps, 1, &expr) != 0)
        return -1;
    if (!can_assign_type(var_type, expr.type))
    {
        set_compile_error(line, "Type mismatch or syntax error.");
        return -1;
    }

    emit_byte(OP_STORE_VAR);
    emit_byte((unsigned char) var_type);
    emit_string(name);
    return 0;
}

static int parse_goto_statement(Parser *ps)
{
    char *name;
    size_t patch_pos;
    int line = ps->tok.line;

    if (parser_expect(ps, TOK_GOTO, "Syntax Error.") != 0)
        return -1;
    if (ps->tok.kind != TOK_IDENTIFIER)
    {
        set_compile_error(ps->tok.line, "Label expected.");
        return -1;
    }
    name = xstrdup(ps->tok.text);
    parser_next(ps);

    emit_byte(OP_GOTO);
    patch_pos = emit_get_pos();
    emit_int(-1);
    if (add_pending_ref(name, patch_pos, line, REF_GOTO) != 0)
    {
        free(name);
        return -1;
    }
    free(name);
    return 0;
}

static int parse_call_statement(Parser *ps)
{
    char *name;
    size_t patch_pos;
    int line = ps->tok.line;

    if (parser_expect(ps, TOK_CALL, "Syntax Error.") != 0)
        return -1;
    if (ps->tok.kind != TOK_IDENTIFIER)
    {
        set_compile_error(ps->tok.line, "Label expected.");
        return -1;
    }
    name = xstrdup(ps->tok.text);
    parser_next(ps);

    emit_byte(OP_CALL);
    patch_pos = emit_get_pos();
    emit_int(-1);
    if (add_pending_ref(name, patch_pos, line, REF_CALL) != 0)
    {
        free(name);
        return -1;
    }
    free(name);
    return 0;
}

static int parse_tvcall_statement(Parser *ps)
{
    char *name;
    ExprInfo args[32];
    int argc = 0;

    if (parser_expect(ps, TOK_TVCALL, "Syntax Error.") != 0)
        return -1;
    if (ps->tok.kind != TOK_IDENTIFIER)
    {
        set_compile_error(ps->tok.line, "Identifier expected.");
        return -1;
    }
    name = xstrdup(ps->tok.text);
    parser_next(ps);
    if (parser_expect(ps, TOK_LPAREN, "'(' expected.") != 0)
    {
        free(name);
        return -1;
    }
    if (parse_argument_expressions(ps, args, &argc, 32) != 0)
    {
        free(name);
        return -1;
    }
    if (parser_expect(ps, TOK_RPAREN, "')' expected.") != 0)
    {
        free(name);
        return -1;
    }

    emit_byte(OP_TVCALL);
    emit_string(name);
    emit_byte((unsigned char) argc);
    free(name);
    return 0;
}

static int parse_proc_statement_after_name(Parser *ps, const char *name, int line)
{
    ExprInfo args[8];
    int argc = 0;

    if (parser_expect(ps, TOK_LPAREN, "'(' expected.") != 0)
        return -1;

    if (strcasecmp(name, "CRUNCH_TABS") == 0 || strcasecmp(name, "EXPAND_TABS") == 0 ||
        strcasecmp(name, "TABS_TO_SPACES") == 0)
    {
        char *var_name;
        int var_type;
        if (ps->tok.kind != TOK_IDENTIFIER)
        {
            set_compile_error(ps->tok.line, "Variable expected.");
            return -1;
        }
        var_name = xstrdup(ps->tok.text);
        if (var_name == NULL)
        {
            set_compile_error(ps->tok.line, "Out of memory.");
            return -1;
        }
        if (lookup_symbol(var_name, &var_type) < 0 || var_type != TYPE_STR)
        {
            free(var_name);
            set_compile_error(ps->tok.line, "Type mismatch or syntax error.");
            return -1;
        }
        parser_next(ps);
        if (parser_expect(ps, TOK_RPAREN, "')' expected.") != 0)
        {
            free(var_name);
            return -1;
        }
        emit_proc_var_call(name, var_name);
        free(var_name);
        return 0;
    }

    if (parse_argument_expressions(ps, args, &argc, 8) != 0)
        return -1;
    if (parser_expect(ps, TOK_RPAREN, "')' expected.") != 0)
        return -1;

    if (strcasecmp(name, "SET_GLOBAL_STR") == 0)
    {
        if (argc != 2 || !is_stringlike_type(args[0].type) || !is_stringlike_type(args[1].type))
        {
            set_compile_error(line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_proc_call("SET_GLOBAL_STR", argc);
        return 0;
    }
    if (strcasecmp(name, "SET_GLOBAL_INT") == 0)
    {
        if (argc != 2 || !is_stringlike_type(args[0].type) || args[1].type != TYPE_INT)
        {
            set_compile_error(line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_proc_call("SET_GLOBAL_INT", argc);
        return 0;
    }
    if (strcasecmp(name, "RUN_MACRO") == 0)
    {
        if (argc != 1 || !is_stringlike_type(args[0].type))
        {
            set_compile_error(line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_proc_call("RUN_MACRO", argc);
        return 0;
    }
    if (strcasecmp(name, "LOAD_MACRO_FILE") == 0)
    {
        if (argc != 1 || !is_stringlike_type(args[0].type))
        {
            set_compile_error(line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_proc_call("LOAD_MACRO_FILE", argc);
        return 0;
    }
    if (strcasecmp(name, "UNLOAD_MACRO") == 0)
    {
        if (argc != 1 || !is_stringlike_type(args[0].type))
        {
            set_compile_error(line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_proc_call("UNLOAD_MACRO", argc);
        return 0;
    }
    if (strcasecmp(name, "CHANGE_DIR") == 0)
    {
        if (argc != 1 || !is_stringlike_type(args[0].type))
        {
            set_compile_error(line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_proc_call("CHANGE_DIR", argc);
        return 0;
    }
    if (strcasecmp(name, "DEL_FILE") == 0)
    {
        if (argc != 1 || !is_stringlike_type(args[0].type))
        {
            set_compile_error(line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_proc_call("DEL_FILE", argc);
        return 0;
    }
    if (strcasecmp(name, "LOAD_FILE") == 0)
    {
        if (argc != 1 || !is_stringlike_type(args[0].type))
        {
            set_compile_error(line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_proc_call("LOAD_FILE", argc);
        return 0;
    }
    if (strcasecmp(name, "SAVE_FILE") == 0)
    {
        if (argc != 0)
        {
            set_compile_error(line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_proc_call("SAVE_FILE", argc);
        return 0;
    }
    if (strcasecmp(name, "SET_INDENT_LEVEL") == 0)
    {
        if (argc != 0)
        {
            set_compile_error(line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_proc_call("SET_INDENT_LEVEL", argc);
        return 0;
    }
    if (strcasecmp(name, "REPLACE") == 0)
    {
        if (argc != 1 || !is_stringlike_type(args[0].type))
        {
            set_compile_error(line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_proc_call("REPLACE", argc);
        return 0;
    }
    if (strcasecmp(name, "TEXT") == 0)
    {
        if (argc != 1 || !is_stringlike_type(args[0].type))
        {
            set_compile_error(line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_proc_call("TEXT", argc);
        return 0;
    }
    if (strcasecmp(name, "PUT_LINE") == 0)
    {
        if (argc != 1 || !is_stringlike_type(args[0].type))
        {
            set_compile_error(line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_proc_call("PUT_LINE", argc);
        return 0;
    }
    if (strcasecmp(name, "DEL_CHARS") == 0)
    {
        if (argc != 1 || args[0].type != TYPE_INT)
        {
            set_compile_error(line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_proc_call("DEL_CHARS", argc);
        return 0;
    }
    if (strcasecmp(name, "GOTO_LINE") == 0)
    {
        if (argc != 1 || args[0].type != TYPE_INT)
        {
            set_compile_error(line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_proc_call("GOTO_LINE", argc);
        return 0;
    }
    if (strcasecmp(name, "GOTO_COL") == 0)
    {
        if (argc != 1 || args[0].type != TYPE_INT)
        {
            set_compile_error(line, "Type mismatch or syntax error.");
            return -1;
        }
        emit_proc_call("GOTO_COL", argc);
        return 0;
    }

    set_compile_error(line, "Syntax Error.");
    return -1;
}

static int parse_if_statement(Parser *ps)
{
    ExprInfo cond;
    int jz_patch;
    int else_patch;

    if (parser_expect(ps, TOK_IF, "Syntax Error.") != 0)
        return -1;
    if (parse_expression(ps, 1, &cond) != 0)
        return -1;
    if (cond.type != TYPE_INT)
    {
        set_compile_error(ps->tok.line, "IF expression must be integer.");
        return -1;
    }
    if (parser_expect(ps, TOK_THEN, "THEN expected.") != 0)
        return -1;

    emit_byte(OP_JZ);
    jz_patch = (int) emit_get_pos();
    emit_int(-1);

    if (parse_statement_list(ps, TOK_ELSE, TOK_END, TOK_EOF) != 0)
        return -1;

    if (parser_accept(ps, TOK_ELSE))
    {
        emit_byte(OP_GOTO);
        else_patch = (int) emit_get_pos();
        emit_int(-1);
        emit_patch_int(jz_patch, (int) emit_get_pos());
        if (parse_statement_list(ps, TOK_END, TOK_EOF, TOK_EOF) != 0)
            return -1;
        if (parser_expect(ps, TOK_END, "END statement not found.") != 0)
            return -1;
        emit_patch_int(else_patch, (int) emit_get_pos());
    }
    else
    {
        if (parser_expect(ps, TOK_END, "END statement not found.") != 0)
            return -1;
        emit_patch_int(jz_patch, (int) emit_get_pos());
    }

    return 0;
}

static int parse_while_statement(Parser *ps)
{
    ExprInfo cond;
    int loop_start;
    int jz_patch;

    if (parser_expect(ps, TOK_WHILE, "Syntax Error.") != 0)
        return -1;
    loop_start = (int) emit_get_pos();
    if (parse_expression(ps, 1, &cond) != 0)
        return -1;
    if (cond.type != TYPE_INT)
    {
        set_compile_error(ps->tok.line, "WHILE expression must be integer.");
        return -1;
    }
    if (parser_expect(ps, TOK_DO, "DO expected.") != 0)
        return -1;

    emit_byte(OP_JZ);
    jz_patch = (int) emit_get_pos();
    emit_int(-1);

    if (parse_statement_list(ps, TOK_END, TOK_EOF, TOK_EOF) != 0)
        return -1;
    if (parser_expect(ps, TOK_END, "END statement not found.") != 0)
        return -1;

    emit_byte(OP_GOTO);
    emit_int(loop_start);
    emit_patch_int(jz_patch, (int) emit_get_pos());
    return 0;
}

static int parse_statement(Parser *ps)
{
    char *name;
    int line;

    if (ps->tok.kind == TOK_DEF_INT)
    {
        parser_next(ps);
        if (parse_variable_declaration(ps, TYPE_INT) != 0) return -1;
        return parser_expect(ps, TOK_SEMICOLON, "; expected.");
    }
    if (ps->tok.kind == TOK_DEF_STR)
    {
        parser_next(ps);
        if (parse_variable_declaration(ps, TYPE_STR) != 0) return -1;
        return parser_expect(ps, TOK_SEMICOLON, "; expected.");
    }
    if (ps->tok.kind == TOK_DEF_CHAR)
    {
        parser_next(ps);
        if (parse_variable_declaration(ps, TYPE_CHAR) != 0) return -1;
        return parser_expect(ps, TOK_SEMICOLON, "; expected.");
    }
    if (ps->tok.kind == TOK_DEF_REAL)
    {
        parser_next(ps);
        if (parse_variable_declaration(ps, TYPE_REAL) != 0) return -1;
        return parser_expect(ps, TOK_SEMICOLON, "; expected.");
    }
    if (ps->tok.kind == TOK_GOTO)
    {
        if (parse_goto_statement(ps) != 0) return -1;
        return parser_expect(ps, TOK_SEMICOLON, "; expected.");
    }
    if (ps->tok.kind == TOK_CALL)
    {
        if (parse_call_statement(ps) != 0) return -1;
        return parser_expect(ps, TOK_SEMICOLON, "; expected.");
    }
    if (ps->tok.kind == TOK_TVCALL)
    {
        if (parse_tvcall_statement(ps) != 0) return -1;
        return parser_expect(ps, TOK_SEMICOLON, "; expected.");
    }
    if (ps->tok.kind == TOK_RET)
    {
        parser_next(ps);
        emit_byte(OP_RET);
        return parser_expect(ps, TOK_SEMICOLON, "; expected.");
    }
    if (ps->tok.kind == TOK_IF)
    {
        if (parse_if_statement(ps) != 0) return -1;
        return parser_expect(ps, TOK_SEMICOLON, "; expected.");
    }
    if (ps->tok.kind == TOK_WHILE)
    {
        if (parse_while_statement(ps) != 0) return -1;
        return parser_expect(ps, TOK_SEMICOLON, "; expected.");
    }
    if (ps->tok.kind == TOK_IDENTIFIER)
    {
        name = xstrdup(ps->tok.text);
        line = ps->tok.line;
        parser_next(ps);
        if (parser_accept(ps, TOK_COLON))
        {
            int rc = define_label(name, line);
            free(name);
            return rc;
        }
        if (ps->tok.kind == TOK_ASSIGN)
        {
            int rc = parse_assignment_after_name(ps, name, line);
            free(name);
            if (rc != 0) return -1;
            return parser_expect(ps, TOK_SEMICOLON, "; expected.");
        }
        if (ps->tok.kind == TOK_LPAREN)
        {
            int rc = parse_proc_statement_after_name(ps, name, line);
            free(name);
            if (rc != 0) return -1;
            return parser_expect(ps, TOK_SEMICOLON, "; expected.");
        }
        if (strcasecmp(name, "SAVE_FILE") == 0)
        {
            emit_proc_call("SAVE_FILE", 0);
            free(name);
            return parser_expect(ps, TOK_SEMICOLON, "; expected.");
        }
        if (strcasecmp(name, "CR") == 0)
        {
            emit_proc_call("CR", 0);
            free(name);
            return parser_expect(ps, TOK_SEMICOLON, "; expected.");
        }
        if (strcasecmp(name, "SET_INDENT_LEVEL") == 0)
        {
            emit_proc_call("SET_INDENT_LEVEL", 0);
            free(name);
            return parser_expect(ps, TOK_SEMICOLON, "; expected.");
        }
        if (strcasecmp(name, "DEL_CHAR") == 0)
        {
            emit_proc_call("DEL_CHAR", 0);
            free(name);
            return parser_expect(ps, TOK_SEMICOLON, "; expected.");
        }
        if (strcasecmp(name, "DEL_LINE") == 0)
        {
            emit_proc_call("DEL_LINE", 0);
            free(name);
            return parser_expect(ps, TOK_SEMICOLON, "; expected.");
        }
        if (strcasecmp(name, "LEFT") == 0 || strcasecmp(name, "RIGHT") == 0 ||
            strcasecmp(name, "UP") == 0 || strcasecmp(name, "DOWN") == 0 ||
            strcasecmp(name, "HOME") == 0 || strcasecmp(name, "EOL") == 0 ||
            strcasecmp(name, "TOF") == 0 || strcasecmp(name, "EOF") == 0 ||
            strcasecmp(name, "WORD_LEFT") == 0 || strcasecmp(name, "WORD_RIGHT") == 0 ||
            strcasecmp(name, "FIRST_WORD") == 0 || strcasecmp(name, "MARK_POS") == 0 ||
            strcasecmp(name, "GOTO_MARK") == 0 || strcasecmp(name, "POP_MARK") == 0 ||
            strcasecmp(name, "PAGE_UP") == 0 || strcasecmp(name, "PAGE_DOWN") == 0 ||
            strcasecmp(name, "NEXT_PAGE_BREAK") == 0 || strcasecmp(name, "LAST_PAGE_BREAK") == 0 ||
            strcasecmp(name, "TAB_RIGHT") == 0 || strcasecmp(name, "TAB_LEFT") == 0 ||
            strcasecmp(name, "INDENT") == 0 || strcasecmp(name, "UNDENT") == 0 ||
            strcasecmp(name, "BLOCK_BEGIN") == 0 || strcasecmp(name, "COL_BLOCK_BEGIN") == 0 ||
            strcasecmp(name, "STR_BLOCK_BEGIN") == 0 || strcasecmp(name, "BLOCK_END") == 0 ||
            strcasecmp(name, "BLOCK_OFF") == 0 || strcasecmp(name, "COPY_BLOCK") == 0 ||
            strcasecmp(name, "MOVE_BLOCK") == 0 || strcasecmp(name, "DELETE_BLOCK") == 0)
        {
            emit_proc_call(name, 0);
            free(name);
            return parser_expect(ps, TOK_SEMICOLON, "; expected.");
        }
        free(name);
        set_compile_error(line, "Syntax Error.");
        return -1;
    }

    set_compile_error(ps->tok.line, "Syntax Error.");
    return -1;
}

static int parse_statement_list(Parser *ps, TokenKind end1, TokenKind end2, TokenKind end3)
{
    while (ps->tok.kind != TOK_EOF && ps->tok.kind != end1 && ps->tok.kind != end2 && ps->tok.kind != end3)
    {
        if (parse_statement(ps) != 0)
            return -1;
    }
    return 0;
}

static int parse_macro_header(Parser *ps, unsigned *out_flags)
{
    unsigned flags = 0;

    while (ps->tok.kind != TOK_SEMICOLON && ps->tok.kind != TOK_EOF)
    {
        if (parser_accept(ps, TOK_TO))
        {
            if (ps->tok.kind != TOK_KEYSPEC)
            {
                set_compile_error(ps->tok.line, "Keycode expected.");
                return -1;
            }
            if (validate_keyspec(ps->tok.text, ps->tok.line) != 0)
                return -1;
            parser_next(ps);
        }
        else if (parser_accept(ps, TOK_FROM))
        {
            if (ps->tok.kind != TOK_IDENTIFIER)
            {
                set_compile_error(ps->tok.line, "Mode expected.");
                return -1;
            }
            if (validate_mode(ps->tok.text, ps->tok.line) != 0)
                return -1;
            parser_next(ps);
        }
        else if (ps->tok.kind == TOK_TRANS || ps->tok.kind == TOK_DUMP || ps->tok.kind == TOK_PERM)
        {
            if (ps->tok.kind == TOK_TRANS)
                flags |= MACRO_ATTR_TRANS;
            else if (ps->tok.kind == TOK_DUMP)
                flags |= MACRO_ATTR_DUMP;
            else if (ps->tok.kind == TOK_PERM)
                flags |= MACRO_ATTR_PERM;
            parser_next(ps);
        }
        else
        {
            set_compile_error(ps->tok.line, "Syntax Error.");
            return -1;
        }
    }
    if (out_flags != NULL)
        *out_flags = flags;
    return 0;
}

static int parse_macro_file_definition(Parser *ps)
{
    if (parser_expect(ps, TOK_MACRO_FILE, "Scommand expected.") != 0)
        return -1;
    if (ps->macro_file_seen)
    {
        set_compile_error(ps->tok.line, "$MACRO_FILE already defined.");
        return -1;
    }
    if (ps->tok.kind != TOK_IDENTIFIER)
    {
        set_compile_error(ps->tok.line, "Identifier expected.");
        return -1;
    }
    if (set_compiled_macro_file_name(ps->tok.text) != 0)
        return -1;
    ps->macro_file_seen = 1;
    parser_next(ps);
    return parser_expect(ps, TOK_SEMICOLON, "; expected.");
}

static int parse_macro_definition(Parser *ps)
{
    char *name;
    unsigned flags = 0;

    if (parser_expect(ps, TOK_MACRO, "Scommand expected.") != 0)
        return -1;
    if (ps->tok.kind != TOK_IDENTIFIER)
    {
        set_compile_error(ps->tok.line, "Macro name expected.");
        return -1;
    }

    name = xstrdup(ps->tok.text);
    if (name == NULL)
    {
        set_compile_error(ps->tok.line, "Out of memory.");
        return -1;
    }
    if (begin_macro(name) != 0)
    {
        free(name);
        return -1;
    }
    parser_next(ps);
    if (parse_macro_header(ps, &flags) != 0)
    {
        free(name);
        return -1;
    }

    if (add_compiled_macro_info(name, (int) emit_get_pos(), flags) != 0)
    {
        free(name);
        return -1;
    }

    if (parser_expect(ps, TOK_SEMICOLON, "; expected.") != 0)
    {
        free(name);
        return -1;
    }

    if (parse_statement_list(ps, TOK_END_MACRO, TOK_EOF, TOK_EOF) != 0)
    {
        free(name);
        return -1;
    }
    if (parser_expect(ps, TOK_END_MACRO, "Premature end of file.") != 0)
    {
        free(name);
        return -1;
    }
    if (parser_expect(ps, TOK_SEMICOLON, "; expected.") != 0)
    {
        free(name);
        return -1;
    }

    if (resolve_pending_refs() != 0)
    {
        free(name);
        return -1;
    }

    emit_byte(OP_HALT);
    free(name);
    return 0;
}

static int parse_program(Parser *ps)
{
    while (ps->tok.kind != TOK_EOF)
    {
        if (ps->tok.kind == TOK_MACRO_FILE)
        {
            if (parse_macro_file_definition(ps) != 0)
                return -1;
        }
        else if (ps->tok.kind == TOK_MACRO)
        {
            if (parse_macro_definition(ps) != 0)
                return -1;
        }
        else if (ps->tok.kind == TOK_ERROR)
        {
            set_compile_error(ps->tok.line, ps->tok.text ? ps->tok.text : "Syntax Error.");
            return -1;
        }
        else
        {
            set_compile_error(ps->tok.line, "Scommand expected.");
            return -1;
        }
    }
    return 0;
}

unsigned char *compile_macro_code(const char *source, size_t *out_size)
{
    Parser ps;
    unsigned char *result = NULL;

    if (out_size != NULL)
        *out_size = 0;

    reset_code_buffer();
    clear_symbols();
    reset_error_state();
    reset_macro_context();
    reset_compiled_macro_info();

    if (source == NULL)
    {
        set_compile_error(0, "No source provided.");
        return NULL;
    }

    parser_init(&ps, source);
    if (parse_program(&ps) != 0 && g_last_error[0] == '\0')
        set_compile_error(ps.tok.line, "Compilation failed.");
    token_free(&ps.tok);

    if (g_last_error[0] != '\0')
    {
        reset_code_buffer();
        clear_symbols();
        if (out_size != NULL)
            *out_size = 0;
        return NULL;
    }

    if (g_code_size == 0)
    {
        result = (unsigned char *) malloc(1);
        if (result == NULL)
        {
            set_compile_error(0, "Out of memory.");
            return NULL;
        }
        if (out_size != NULL)
            *out_size = 0;
        reset_code_buffer();
        clear_symbols();
        return result;
    }

    result = (unsigned char *) malloc(g_code_size);
    if (result == NULL)
    {
        set_compile_error(0, "Out of memory.");
        reset_code_buffer();
        clear_symbols();
        if (out_size != NULL)
            *out_size = 0;
        return NULL;
    }

    memcpy(result, g_code, g_code_size);
    if (out_size != NULL)
        *out_size = g_code_size;

    reset_code_buffer();
    clear_symbols();
    return result;
}


int get_compiled_macro_count(void)
{
    return g_compiled_macro_count;
}

const char *get_compiled_macro_name(int index)
{
    if (index < 0 || index >= g_compiled_macro_count)
        return NULL;
    return g_compiled_macros[index].name;
}

int get_compiled_macro_entry(int index)
{
    if (index < 0 || index >= g_compiled_macro_count)
        return -1;
    return g_compiled_macros[index].entry_pos;
}

int get_compiled_macro_flags(int index)
{
    if (index < 0 || index >= g_compiled_macro_count)
        return 0;
    return (int) g_compiled_macros[index].flags;
}

const char *get_compiled_macro_file_name(void)
{
    return g_compiled_macro_file_name;
}
