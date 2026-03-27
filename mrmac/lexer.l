%{
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "parser.tab.h"
#include "mrmac.h"

extern void yyerror(const char *s);
int comment_depth = 0;

static char *dup_unescaped_string(const char *yytext)
{
    size_t src_len = strlen(yytext);
    char *out;
    size_t i;
    size_t j = 0;

    if (src_len < 2)
        return strdup("");

    out = (char *) malloc(src_len);
    if (out == NULL)
        return NULL;

    for (i = 1; i + 1 < src_len; ++i)
    {
        if (yytext[i] == '\'' && yytext[i + 1] == '\'')
        {
            out[j++] = '\'';
            ++i;
        }
        else
        {
            out[j++] = yytext[i];
        }
    }

    out[j] = '\0';
    return out;
}
%}

%option noyywrap
%option caseless
%option yylineno

%x COMMENT

DIGIT       [0-9]
LETTER      [a-zA-Z_]
ID          {LETTER}({LETTER}|{DIGIT})*
HEX         \$[0-9a-fA-F]+
KEYSPEC     <[^>\n]+>

%%

\xEF\xBB\xBF        { /* Ignore UTF-8 BOM */ }
[ \t\r\n]+          { /* Ignore whitespace */ }

"{"                 { comment_depth = 1; BEGIN(COMMENT); }
<COMMENT>"{"        { comment_depth++; }
<COMMENT>"}"        {
                        comment_depth--;
                        if (comment_depth == 0)
                            BEGIN(INITIAL);
                    }
<COMMENT>.|\n        { /* Ignore comment content */ }

SMACRO_FILE         { return SMACRO_FILE; }
\$MACRO             { return SMACRO; }
END_MACRO           { return END_MACRO; }
DEF_INT             { return DEF_INT; }
DEF_STR             { return DEF_STR; }
DEF_CHAR            { return DEF_CHAR; }
DEF_REAL            { return DEF_REAL; }
IF                  { return IF; }
THEN                { return THEN; }
ELSE                { return ELSE; }
END                 { return END; }
WHILE               { return WHILE; }
DO                  { return DO; }
TVCALL              { return TVCALL; }
CALL                { return CALL; }
RET                 { return RET; }
GOTO                { return GOTO; }
TO                  { return TO; }
FROM                { return FROM; }
TRANS               { return TRANS; }
DUMP                { return DUMP; }
PERM                { return PERM; }

":="                { return ASSIGN; }
"="                 { return EQ; }
"<>"                { return NE; }
"<="                { return LE; }
">="                { return GE; }
"<"                 { return LT; }
">"                 { return GT; }
"+"                 { return PLUS; }
"-"                 { return MINUS; }
"*"                 { return MULT; }
"/"                 { return DIV; }
AND                 { return AND; }
OR                  { return OR; }
NOT                 { return NOT; }
SHL                 { return SHL; }
SHR                 { return SHR; }
MOD                 { return MOD; }

","                 { return COMMA; }
";"                 { return SEMICOLON; }
":"                 { return COLON; }
"("                 { return LPAREN; }
")"                 { return RPAREN; }

{KEYSPEC}           {
                        yylval.sval = strdup(yytext);
                        return KEYSPEC;
                    }

\'([^\'\n]|\'\')*\' {
                        yylval.sval = dup_unescaped_string(yytext);
                        if (yylval.sval == NULL)
                        {
                            yyerror("Out of memory.");
                            return 0;
                        }
                        return STRING_LITERAL;
                    }

{ID}                {
                        yylval.sval = strdup(yytext);
                        return IDENTIFIER;
                    }

{DIGIT}+            {
                        yylval.ival = atoi(yytext);
                        return INTEGER_LITERAL;
                    }

{HEX}               {
                        yylval.ival = (int) strtol(yytext + 1, NULL, 16);
                        return INTEGER_LITERAL;
                    }

.                   {
                        char err_buf[64];
                        snprintf(err_buf, sizeof(err_buf), "Unexpected character: '%s'", yytext);
                        yyerror(err_buf);
                        return 0;
                    }

%%
