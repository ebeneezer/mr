%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "mrmac.h"

extern int yylex(void);
extern int yylineno;
void yyerror(const char *s);

int param_count = 0;
int current_def_type = 0;

#define MAX_LABELS 500
#define MAX_PENDING_REFS 1000
#define MAX_IDENTIFIER_LEN 20

typedef struct
{
    char name[MAX_IDENTIFIER_LEN + 1];
    int target_pos;
} LabelDef;

typedef enum
{
    REF_GOTO = 1,
    REF_CALL = 2
} PendingRefKind;

typedef struct
{
    char name[MAX_IDENTIFIER_LEN + 1];
    size_t patch_pos;
    int line;
    PendingRefKind kind;
} PendingRef;

static LabelDef label_defs[MAX_LABELS];
static int label_count = 0;

static PendingRef pending_refs[MAX_PENDING_REFS];
static int pending_ref_count = 0;

static int smacro_file_seen = 0;

static void reset_macro_context(void)
{
    label_count = 0;
    pending_ref_count = 0;
    clear_symbols();
}

static int begin_macro(const char *name, int line)
{
    size_t len = strlen(name);

    if (len == 0)
    {
        set_compile_error(line, "Macro name expected.");
        return -1;
    }

    reset_macro_context();
    return 0;
}

static int find_label_index(const char *name)
{
    for (int i = 0; i < label_count; i++)
    {
        if (strcasecmp(label_defs[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int define_label(const char *name, int line)
{
    size_t len = strlen(name);

    if (len == 0 || len > MAX_IDENTIFIER_LEN)
    {
        set_compile_error(line, "Label too long.");
        return -1;
    }

    if (label_count >= MAX_LABELS)
    {
        set_compile_error(line, "Too many labels.");
        return -1;
    }

    if (find_label_index(name) >= 0)
    {
        set_compile_error(line, "Duplicate label.");
        return -1;
    }

    strncpy(label_defs[label_count].name, name, MAX_IDENTIFIER_LEN);
    label_defs[label_count].name[MAX_IDENTIFIER_LEN] = '\0';
    label_defs[label_count].target_pos = (int) emit_get_pos();
    label_count++;
    return 0;
}

static int add_pending_ref(const char *name, size_t patch_pos, int line, PendingRefKind kind)
{
    size_t len = strlen(name);

    if (len == 0 || len > MAX_IDENTIFIER_LEN)
    {
        set_compile_error(line, "Label too long.");
        return -1;
    }

    if (pending_ref_count >= MAX_PENDING_REFS)
    {
        set_compile_error(line, "Too many labels.");
        return -1;
    }

    strncpy(pending_refs[pending_ref_count].name, name, MAX_IDENTIFIER_LEN);
    pending_refs[pending_ref_count].name[MAX_IDENTIFIER_LEN] = '\0';
    pending_refs[pending_ref_count].patch_pos = patch_pos;
    pending_refs[pending_ref_count].line = line;
    pending_refs[pending_ref_count].kind = kind;
    pending_ref_count++;
    return 0;
}

static int resolve_pending_refs(void)
{
    char err_buf[256];

    for (int i = 0; i < pending_ref_count; i++)
    {
        int idx = find_label_index(pending_refs[i].name);
        if (idx < 0)
        {
            snprintf(err_buf, sizeof(err_buf), "Label %s not found.", pending_refs[i].name);
            set_compile_error(pending_refs[i].line, err_buf);
            return -1;
        }

        emit_patch_int(pending_refs[i].patch_pos, label_defs[idx].target_pos);
    }

    return 0;
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

    if (strcasecmp(text, "EDIT") == 0)
        return 0;
    if (strcasecmp(text, "DOS_SHELL") == 0)
        return 0;
    if (strcasecmp(text, "ALL") == 0)
        return 0;

    set_compile_error(line, "Mode expected.");
    return -1;
}
%}

%union {
    int ival;
    char *sval;
}

%token <sval> IDENTIFIER STRING_LITERAL KEYSPEC
%token <ival> INTEGER_LITERAL

%token SMACRO SMACRO_FILE END_MACRO
%token DEF_INT DEF_STR DEF_CHAR DEF_REAL
%token IF THEN ELSE END WHILE DO TVCALL CALL RET GOTO
%token TO FROM TRANS DUMP PERM

%token ASSIGN EQ NE LE GE LT GT PLUS MINUS MULT DIV
%token AND OR NOT SHL SHR MOD
%token LPAREN RPAREN COMMA SEMICOLON COLON

%type <ival> if_jz_mark else_jump_mark while_start_mark while_jz_mark

%left OR
%left AND
%left EQ NE LT GT LE GE
%left SHL SHR
%left PLUS MINUS
%left MULT DIV MOD
%right NOT
%right UMINUS

%%

program:
      {
          smacro_file_seen = 0;
          param_count = 0;
          current_def_type = 0;
          reset_macro_context();
      }
      source_items
    ;

source_items:
      /* empty */
    | source_items source_item
    ;

source_item:
      macro_file_definition
    | macro_definition
    ;

macro_file_definition:
      SMACRO_FILE IDENTIFIER SEMICOLON
        {
            if (smacro_file_seen)
            {
                set_compile_error(yylineno, "$MACRO_FILE already defined.");
                free($2);
                YYERROR;
            }
            smacro_file_seen = 1;
            free($2);
        }
    ;

macro_definition:
      SMACRO IDENTIFIER
        {
            if (begin_macro($2, yylineno) != 0)
            {
                free($2);
                YYERROR;
            }
            free($2);
        }
      macro_header_opt SEMICOLON statement_list END_MACRO SEMICOLON
        {
            if (resolve_pending_refs() != 0)
                YYERROR;

            emit_byte(OP_HALT);
        }
    ;

macro_header_opt:
      /* empty */
    | macro_header_items
    ;

macro_header_items:
      macro_header_item
    | macro_header_items macro_header_item
    ;

macro_header_item:
      TO KEYSPEC
        {
            if (validate_keyspec($2, yylineno) != 0)
            {
                free($2);
                YYERROR;
            }
            free($2);
        }
    | FROM IDENTIFIER
        {
            if (validate_mode($2, yylineno) != 0)
            {
                free($2);
                YYERROR;
            }
            free($2);
        }
    | TRANS
    | DUMP
    | PERM
    ;

statement_list:
      /* empty */
    | statement_list statement
    ;

statement:
      call_statement SEMICOLON
    | goto_statement SEMICOLON
    | ret_statement SEMICOLON
    | assignment_statement SEMICOLON
    | if_statement SEMICOLON
    | while_statement SEMICOLON
    | label_definition
    | variable_declaration SEMICOLON
    ;

variable_declaration:
      DEF_INT  { current_def_type = TYPE_INT;  } LPAREN var_list RPAREN
    | DEF_STR  { current_def_type = TYPE_STR;  } LPAREN var_list RPAREN
    | DEF_CHAR { current_def_type = TYPE_CHAR; } LPAREN var_list RPAREN
    | DEF_REAL { current_def_type = TYPE_REAL; } LPAREN var_list RPAREN
    ;

var_list:
      IDENTIFIER
        {
            if (add_symbol($1, current_def_type) == -1)
            {
                free($1);
                YYERROR;
            }
            free($1);
        }
    | var_list COMMA IDENTIFIER
        {
            if (add_symbol($3, current_def_type) == -1)
            {
                free($3);
                YYERROR;
            }
            free($3);
        }
    ;

label_definition:
      IDENTIFIER COLON
        {
            if (define_label($1, yylineno) != 0)
            {
                free($1);
                YYERROR;
            }
            free($1);
        }
    ;

goto_statement:
      GOTO IDENTIFIER
        {
            size_t patch_pos;

            emit_byte(OP_GOTO);
            patch_pos = emit_get_pos();
            emit_int(-1);

            if (add_pending_ref($2, patch_pos, yylineno, REF_GOTO) != 0)
            {
                free($2);
                YYERROR;
            }

            free($2);
        }
    ;

ret_statement:
      RET
        {
            emit_byte(OP_RET);
        }
    ;

assignment_statement:
      IDENTIFIER ASSIGN expression
        {
            int type = 0;

            if (lookup_symbol($1, &type) < 0)
            {
                free($1);
                yyerror("Variable expected.");
                YYERROR;
            }

            emit_byte(OP_STORE_VAR);
            emit_string($1);
            free($1);
        }
    ;

call_statement:
      CALL IDENTIFIER
        {
            size_t patch_pos;

            emit_byte(OP_CALL);
            patch_pos = emit_get_pos();
            emit_int(-1);

            if (add_pending_ref($2, patch_pos, yylineno, REF_CALL) != 0)
            {
                free($2);
                YYERROR;
            }

            free($2);
        }
    | TVCALL IDENTIFIER LPAREN argument_list RPAREN
        {
            emit_byte(OP_TVCALL);
            emit_string($2);
            emit_byte((unsigned char) param_count);
            free($2);
            param_count = 0;
        }
    ;

if_jz_mark:
      /* empty */
        {
            emit_byte(OP_JZ);
            $$ = (int) emit_get_pos();
            emit_int(-1);
        }
    ;

else_jump_mark:
      /* empty */
        {
            emit_byte(OP_GOTO);
            $$ = (int) emit_get_pos();
            emit_int(-1);
        }
    ;

while_start_mark:
      /* empty */
        {
            $$ = (int) emit_get_pos();
        }
    ;

while_jz_mark:
      /* empty */
        {
            emit_byte(OP_JZ);
            $$ = (int) emit_get_pos();
            emit_int(-1);
        }
    ;

if_statement:
      IF expression THEN if_jz_mark statement_list END
        {
            emit_patch_int($4, (int) emit_get_pos());
        }
    | IF expression THEN if_jz_mark statement_list ELSE else_jump_mark statement_list END
        {
            emit_patch_int($4, $7 + (int) sizeof(int));
            emit_patch_int($7, (int) emit_get_pos());
        }
    ;

while_statement:
      WHILE while_start_mark expression DO while_jz_mark statement_list END
        {
            emit_byte(OP_GOTO);
            emit_int($2);
            emit_patch_int($5, (int) emit_get_pos());
        }
    ;

argument_list:
      /* empty */
    | argument
    | argument_list COMMA argument
    ;

argument:
      expression
        {
            param_count++;
        }
    | STRING_LITERAL
        {
            emit_byte(OP_PUSH_S);
            emit_string($1);
            free($1);
            param_count++;
        }
    ;

expression:
      INTEGER_LITERAL
        {
            emit_byte(OP_PUSH_I);
            emit_int($1);
        }
    | IDENTIFIER
        {
            int type = 0;

            if (lookup_symbol($1, &type) < 0)
            {
                free($1);
                yyerror("Variable expected.");
                YYERROR;
            }

            emit_byte(OP_LOAD_VAR);
            emit_string($1);
            free($1);
        }
    | LPAREN expression RPAREN
    | MINUS expression %prec UMINUS
        {
            emit_byte(OP_NEG);
        }
    | NOT expression
        {
            emit_byte(OP_NOT);
        }
    | expression PLUS expression
        {
            emit_byte(OP_ADD);
        }
    | expression MINUS expression
        {
            emit_byte(OP_SUB);
        }
    | expression MULT expression
        {
            emit_byte(OP_MUL);
        }
    | expression DIV expression
        {
            emit_byte(OP_DIV);
        }
    | expression MOD expression
        {
            emit_byte(OP_MOD);
        }
    | expression EQ expression
        {
            emit_byte(OP_CMP_EQ);
        }
    | expression NE expression
        {
            emit_byte(OP_CMP_NE);
        }
    | expression LT expression
        {
            emit_byte(OP_CMP_LT);
        }
    | expression GT expression
        {
            emit_byte(OP_CMP_GT);
        }
    | expression LE expression
        {
            emit_byte(OP_CMP_LE);
        }
    | expression GE expression
        {
            emit_byte(OP_CMP_GE);
        }
    | expression AND expression
        {
            emit_byte(OP_AND);
        }
    | expression OR expression
        {
            emit_byte(OP_OR);
        }
    | expression SHL expression
        {
            emit_byte(OP_SHL);
        }
    | expression SHR expression
        {
            emit_byte(OP_SHR);
        }
    ;

%%

void yyerror(const char *s)
{
    if (strcmp(s, "syntax error") == 0)
        set_compile_error(yylineno, "Syntax Error.");
    else
        set_compile_error(yylineno, s);
}
