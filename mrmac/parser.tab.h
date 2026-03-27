/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_MRMAC_PARSER_TAB_H_INCLUDED
# define YY_YY_MRMAC_PARSER_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    IDENTIFIER = 258,              /* IDENTIFIER  */
    STRING_LITERAL = 259,          /* STRING_LITERAL  */
    KEYSPEC = 260,                 /* KEYSPEC  */
    INTEGER_LITERAL = 261,         /* INTEGER_LITERAL  */
    SMACRO = 262,                  /* SMACRO  */
    SMACRO_FILE = 263,             /* SMACRO_FILE  */
    END_MACRO = 264,               /* END_MACRO  */
    DEF_INT = 265,                 /* DEF_INT  */
    DEF_STR = 266,                 /* DEF_STR  */
    DEF_CHAR = 267,                /* DEF_CHAR  */
    DEF_REAL = 268,                /* DEF_REAL  */
    IF = 269,                      /* IF  */
    THEN = 270,                    /* THEN  */
    ELSE = 271,                    /* ELSE  */
    END = 272,                     /* END  */
    WHILE = 273,                   /* WHILE  */
    DO = 274,                      /* DO  */
    TVCALL = 275,                  /* TVCALL  */
    CALL = 276,                    /* CALL  */
    RET = 277,                     /* RET  */
    GOTO = 278,                    /* GOTO  */
    TO = 279,                      /* TO  */
    FROM = 280,                    /* FROM  */
    TRANS = 281,                   /* TRANS  */
    DUMP = 282,                    /* DUMP  */
    PERM = 283,                    /* PERM  */
    ASSIGN = 284,                  /* ASSIGN  */
    EQ = 285,                      /* EQ  */
    NE = 286,                      /* NE  */
    LE = 287,                      /* LE  */
    GE = 288,                      /* GE  */
    LT = 289,                      /* LT  */
    GT = 290,                      /* GT  */
    PLUS = 291,                    /* PLUS  */
    MINUS = 292,                   /* MINUS  */
    MULT = 293,                    /* MULT  */
    DIV = 294,                     /* DIV  */
    AND = 295,                     /* AND  */
    OR = 296,                      /* OR  */
    NOT = 297,                     /* NOT  */
    SHL = 298,                     /* SHL  */
    SHR = 299,                     /* SHR  */
    MOD = 300,                     /* MOD  */
    LPAREN = 301,                  /* LPAREN  */
    RPAREN = 302,                  /* RPAREN  */
    COMMA = 303,                   /* COMMA  */
    SEMICOLON = 304,               /* SEMICOLON  */
    COLON = 305,                   /* COLON  */
    UMINUS = 306                   /* UMINUS  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 193 "mrmac/parser.y"

    int ival;
    char *sval;

#line 120 "mrmac/parser.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_MRMAC_PARSER_TAB_H_INCLUDED  */
