/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

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

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "mrmac/parser.y"

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
#define MAX_MACRO_NAME_LEN 8
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

    if (len == 0 || len > MAX_MACRO_NAME_LEN)
    {
        set_compile_error(line, "Macro name must be <= 8 characters long.");
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

#line 263 "mrmac/parser.tab.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "parser.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_IDENTIFIER = 3,                 /* IDENTIFIER  */
  YYSYMBOL_STRING_LITERAL = 4,             /* STRING_LITERAL  */
  YYSYMBOL_KEYSPEC = 5,                    /* KEYSPEC  */
  YYSYMBOL_INTEGER_LITERAL = 6,            /* INTEGER_LITERAL  */
  YYSYMBOL_SMACRO = 7,                     /* SMACRO  */
  YYSYMBOL_SMACRO_FILE = 8,                /* SMACRO_FILE  */
  YYSYMBOL_END_MACRO = 9,                  /* END_MACRO  */
  YYSYMBOL_DEF_INT = 10,                   /* DEF_INT  */
  YYSYMBOL_DEF_STR = 11,                   /* DEF_STR  */
  YYSYMBOL_DEF_CHAR = 12,                  /* DEF_CHAR  */
  YYSYMBOL_DEF_REAL = 13,                  /* DEF_REAL  */
  YYSYMBOL_IF = 14,                        /* IF  */
  YYSYMBOL_THEN = 15,                      /* THEN  */
  YYSYMBOL_ELSE = 16,                      /* ELSE  */
  YYSYMBOL_END = 17,                       /* END  */
  YYSYMBOL_WHILE = 18,                     /* WHILE  */
  YYSYMBOL_DO = 19,                        /* DO  */
  YYSYMBOL_TVCALL = 20,                    /* TVCALL  */
  YYSYMBOL_CALL = 21,                      /* CALL  */
  YYSYMBOL_RET = 22,                       /* RET  */
  YYSYMBOL_GOTO = 23,                      /* GOTO  */
  YYSYMBOL_TO = 24,                        /* TO  */
  YYSYMBOL_FROM = 25,                      /* FROM  */
  YYSYMBOL_TRANS = 26,                     /* TRANS  */
  YYSYMBOL_DUMP = 27,                      /* DUMP  */
  YYSYMBOL_PERM = 28,                      /* PERM  */
  YYSYMBOL_ASSIGN = 29,                    /* ASSIGN  */
  YYSYMBOL_EQ = 30,                        /* EQ  */
  YYSYMBOL_NE = 31,                        /* NE  */
  YYSYMBOL_LE = 32,                        /* LE  */
  YYSYMBOL_GE = 33,                        /* GE  */
  YYSYMBOL_LT = 34,                        /* LT  */
  YYSYMBOL_GT = 35,                        /* GT  */
  YYSYMBOL_PLUS = 36,                      /* PLUS  */
  YYSYMBOL_MINUS = 37,                     /* MINUS  */
  YYSYMBOL_MULT = 38,                      /* MULT  */
  YYSYMBOL_DIV = 39,                       /* DIV  */
  YYSYMBOL_AND = 40,                       /* AND  */
  YYSYMBOL_OR = 41,                        /* OR  */
  YYSYMBOL_NOT = 42,                       /* NOT  */
  YYSYMBOL_SHL = 43,                       /* SHL  */
  YYSYMBOL_SHR = 44,                       /* SHR  */
  YYSYMBOL_MOD = 45,                       /* MOD  */
  YYSYMBOL_LPAREN = 46,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 47,                    /* RPAREN  */
  YYSYMBOL_COMMA = 48,                     /* COMMA  */
  YYSYMBOL_SEMICOLON = 49,                 /* SEMICOLON  */
  YYSYMBOL_COLON = 50,                     /* COLON  */
  YYSYMBOL_UMINUS = 51,                    /* UMINUS  */
  YYSYMBOL_YYACCEPT = 52,                  /* $accept  */
  YYSYMBOL_program = 53,                   /* program  */
  YYSYMBOL_54_1 = 54,                      /* $@1  */
  YYSYMBOL_source_items = 55,              /* source_items  */
  YYSYMBOL_source_item = 56,               /* source_item  */
  YYSYMBOL_macro_file_definition = 57,     /* macro_file_definition  */
  YYSYMBOL_macro_definition = 58,          /* macro_definition  */
  YYSYMBOL_59_2 = 59,                      /* $@2  */
  YYSYMBOL_macro_header_opt = 60,          /* macro_header_opt  */
  YYSYMBOL_macro_header_items = 61,        /* macro_header_items  */
  YYSYMBOL_macro_header_item = 62,         /* macro_header_item  */
  YYSYMBOL_statement_list = 63,            /* statement_list  */
  YYSYMBOL_statement = 64,                 /* statement  */
  YYSYMBOL_variable_declaration = 65,      /* variable_declaration  */
  YYSYMBOL_66_3 = 66,                      /* $@3  */
  YYSYMBOL_67_4 = 67,                      /* $@4  */
  YYSYMBOL_68_5 = 68,                      /* $@5  */
  YYSYMBOL_69_6 = 69,                      /* $@6  */
  YYSYMBOL_var_list = 70,                  /* var_list  */
  YYSYMBOL_label_definition = 71,          /* label_definition  */
  YYSYMBOL_goto_statement = 72,            /* goto_statement  */
  YYSYMBOL_ret_statement = 73,             /* ret_statement  */
  YYSYMBOL_assignment_statement = 74,      /* assignment_statement  */
  YYSYMBOL_call_statement = 75,            /* call_statement  */
  YYSYMBOL_if_jz_mark = 76,                /* if_jz_mark  */
  YYSYMBOL_else_jump_mark = 77,            /* else_jump_mark  */
  YYSYMBOL_while_start_mark = 78,          /* while_start_mark  */
  YYSYMBOL_while_jz_mark = 79,             /* while_jz_mark  */
  YYSYMBOL_if_statement = 80,              /* if_statement  */
  YYSYMBOL_while_statement = 81,           /* while_statement  */
  YYSYMBOL_argument_list = 82,             /* argument_list  */
  YYSYMBOL_argument = 83,                  /* argument  */
  YYSYMBOL_expression = 84                 /* expression  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_uint8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  3
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   245

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  52
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  33
/* YYNRULES -- Number of rules.  */
#define YYNRULES  77
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  143

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   306


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   224,   224,   224,   233,   235,   239,   240,   244,   259,
     258,   276,   278,   282,   283,   287,   296,   305,   306,   307,
     310,   312,   316,   317,   318,   319,   320,   321,   322,   323,
     327,   327,   328,   328,   329,   329,   330,   330,   334,   343,
     355,   367,   386,   393,   411,   427,   439,   448,   457,   464,
     472,   476,   484,   492,   494,   495,   499,   503,   513,   518,
     533,   534,   538,   542,   546,   550,   554,   558,   562,   566,
     570,   574,   578,   582,   586,   590,   594,   598
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "IDENTIFIER",
  "STRING_LITERAL", "KEYSPEC", "INTEGER_LITERAL", "SMACRO", "SMACRO_FILE",
  "END_MACRO", "DEF_INT", "DEF_STR", "DEF_CHAR", "DEF_REAL", "IF", "THEN",
  "ELSE", "END", "WHILE", "DO", "TVCALL", "CALL", "RET", "GOTO", "TO",
  "FROM", "TRANS", "DUMP", "PERM", "ASSIGN", "EQ", "NE", "LE", "GE", "LT",
  "GT", "PLUS", "MINUS", "MULT", "DIV", "AND", "OR", "NOT", "SHL", "SHR",
  "MOD", "LPAREN", "RPAREN", "COMMA", "SEMICOLON", "COLON", "UMINUS",
  "$accept", "program", "$@1", "source_items", "source_item",
  "macro_file_definition", "macro_definition", "$@2", "macro_header_opt",
  "macro_header_items", "macro_header_item", "statement_list", "statement",
  "variable_declaration", "$@3", "$@4", "$@5", "$@6", "var_list",
  "label_definition", "goto_statement", "ret_statement",
  "assignment_statement", "call_statement", "if_jz_mark", "else_jump_mark",
  "while_start_mark", "while_jz_mark", "if_statement", "while_statement",
  "argument_list", "argument", "expression", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-93)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     -93,     5,   -93,   -93,    15,     4,     8,   -93,   -93,   -93,
     -93,   -32,     6,   -93,    31,    35,   -93,   -93,   -93,    -8,
       6,   -93,   -93,   -93,   -93,   -93,   126,   -21,    -4,   -93,
     -93,   -93,   -93,     0,   -93,    40,    95,   -93,    96,   -93,
      51,   -93,    68,    69,    70,    76,    85,    92,     0,   -93,
     -93,    99,   109,   118,   119,   -93,   -93,     0,     0,     0,
      48,     0,   125,   -93,   -93,   -93,   -93,   -93,   -93,   -93,
     -93,   -93,   168,   139,   139,   139,   139,   -93,   -93,   150,
     -93,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    71,    -2,   -93,   -34,
      27,    29,    47,   -93,   -93,    28,    28,    28,    28,    28,
      28,   -29,   -29,   -93,   -93,   200,   184,   -18,   -18,   -93,
     -93,   -93,    49,   -93,   168,   -93,   153,   -93,   -93,   -93,
     110,   -93,   -93,    -2,   -93,   -93,   -93,   140,   -93,   -93,
     -93,   156,   -93
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       2,     0,     4,     1,     3,     0,     0,     5,     6,     7,
       9,     0,    11,     8,     0,     0,    17,    18,    19,     0,
      12,    13,    15,    16,    20,    14,     0,     0,     0,    30,
      32,    34,    36,     0,    48,     0,     0,    42,     0,    21,
       0,    28,     0,     0,     0,     0,     0,     0,     0,    40,
      10,     0,     0,     0,     0,    59,    58,     0,     0,     0,
       0,     0,     0,    44,    41,    29,    23,    24,    25,    22,
      26,    27,    43,     0,     0,     0,     0,    61,    62,     0,
      46,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    53,    38,     0,
       0,     0,     0,    60,    20,    68,    69,    72,    73,    70,
      71,    63,    64,    65,    66,    74,    75,    76,    77,    67,
      49,    57,     0,    54,    56,    31,     0,    33,    35,    37,
       0,    20,    45,     0,    39,    47,    50,     0,    55,    20,
      52,     0,    51
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
     -93,   -93,   -93,   -93,   -93,   -93,   -93,   -93,   -93,   -93,
     152,   -92,   -93,   -93,   -93,   -93,   -93,   -93,    -6,   -93,
     -93,   -93,   -93,   -93,   -93,   -93,   -93,   -93,   -93,   -93,
     -93,    42,   -33
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,     1,     2,     4,     7,     8,     9,    12,    19,    20,
      21,    26,    39,    40,    51,    52,    53,    54,    99,    41,
      42,    43,    44,    45,   104,   139,    61,   131,    46,    47,
     122,   123,   124
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_uint8 yytable[] =
{
      60,    55,   121,    55,    56,     3,    56,    10,    48,    89,
      90,    11,   130,   125,   126,    72,    95,    13,    87,    88,
      89,    90,     5,     6,    77,    78,    79,    95,    96,    49,
      14,    15,    16,    17,    18,    57,    22,    57,    23,   137,
      58,    24,    58,    62,    59,    50,    59,   141,   105,   106,
     107,   108,   109,   110,   111,   112,   113,   114,   115,   116,
     117,   118,   119,    80,    87,    88,    89,    90,   100,   101,
     102,    93,    94,    95,   127,   126,   128,   126,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
     120,    93,    94,    95,   129,   126,   132,   133,    63,    64,
      65,    81,    82,    83,    84,    85,    86,    87,    88,    89,
      90,    91,    92,    27,    93,    94,    95,    66,    67,    68,
      29,    30,    31,    32,    33,    69,   135,   136,    34,    27,
      35,    36,    37,    38,    70,    28,    29,    30,    31,    32,
      33,    71,    98,    27,    34,    73,    35,    36,    37,    38,
      29,    30,    31,    32,    33,    74,   134,   140,    34,    27,
      35,    36,    37,    38,    75,    76,    29,    30,    31,    32,
      33,    97,    25,   142,    34,   138,    35,    36,    37,    38,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,     0,    93,    94,    95,     0,   103,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
       0,    93,    94,    95,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,     0,     0,    93,    94,    95,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
       0,     0,     0,    93,    94,    95
};

static const yytype_int16 yycheck[] =
{
      33,     3,     4,     3,     6,     0,     6,     3,    29,    38,
      39,     3,   104,    47,    48,    48,    45,    49,    36,    37,
      38,    39,     7,     8,    57,    58,    59,    45,    61,    50,
      24,    25,    26,    27,    28,    37,     5,    37,     3,   131,
      42,    49,    42,     3,    46,    49,    46,   139,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    15,    36,    37,    38,    39,    74,    75,
      76,    43,    44,    45,    47,    48,    47,    48,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      19,    43,    44,    45,    47,    48,    47,    48,     3,     3,
      49,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,     3,    43,    44,    45,    49,    49,    49,
      10,    11,    12,    13,    14,    49,    16,    17,    18,     3,
      20,    21,    22,    23,    49,     9,    10,    11,    12,    13,
      14,    49,     3,     3,    18,    46,    20,    21,    22,    23,
      10,    11,    12,    13,    14,    46,     3,    17,    18,     3,
      20,    21,    22,    23,    46,    46,    10,    11,    12,    13,
      14,    46,    20,    17,    18,   133,    20,    21,    22,    23,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    -1,    43,    44,    45,    -1,    47,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      -1,    43,    44,    45,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    -1,    -1,    43,    44,    45,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      -1,    -1,    -1,    43,    44,    45
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,    53,    54,     0,    55,     7,     8,    56,    57,    58,
       3,     3,    59,    49,    24,    25,    26,    27,    28,    60,
      61,    62,     5,     3,    49,    62,    63,     3,     9,    10,
      11,    12,    13,    14,    18,    20,    21,    22,    23,    64,
      65,    71,    72,    73,    74,    75,    80,    81,    29,    50,
      49,    66,    67,    68,    69,     3,     6,    37,    42,    46,
      84,    78,     3,     3,     3,    49,    49,    49,    49,    49,
      49,    49,    84,    46,    46,    46,    46,    84,    84,    84,
      15,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    43,    44,    45,    84,    46,     3,    70,
      70,    70,    70,    47,    76,    84,    84,    84,    84,    84,
      84,    84,    84,    84,    84,    84,    84,    84,    84,    84,
      19,     4,    82,    83,    84,    47,    48,    47,    47,    47,
      63,    79,    47,    48,     3,    16,    17,    63,    83,    77,
      17,    63,    17
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    52,    54,    53,    55,    55,    56,    56,    57,    59,
      58,    60,    60,    61,    61,    62,    62,    62,    62,    62,
      63,    63,    64,    64,    64,    64,    64,    64,    64,    64,
      66,    65,    67,    65,    68,    65,    69,    65,    70,    70,
      71,    72,    73,    74,    75,    75,    76,    77,    78,    79,
      80,    80,    81,    82,    82,    82,    83,    83,    84,    84,
      84,    84,    84,    84,    84,    84,    84,    84,    84,    84,
      84,    84,    84,    84,    84,    84,    84,    84
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     0,     2,     0,     2,     1,     1,     3,     0,
       8,     0,     1,     1,     2,     2,     2,     1,     1,     1,
       0,     2,     2,     2,     2,     2,     2,     2,     1,     2,
       0,     5,     0,     5,     0,     5,     0,     5,     1,     3,
       2,     2,     1,     3,     2,     5,     0,     0,     0,     0,
       6,     9,     7,     0,     1,     3,     1,     1,     1,     1,
       3,     2,     2,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* $@1: %empty  */
#line 224 "mrmac/parser.y"
      {
          smacro_file_seen = 0;
          param_count = 0;
          current_def_type = 0;
          reset_macro_context();
      }
#line 1460 "mrmac/parser.tab.c"
    break;

  case 8: /* macro_file_definition: SMACRO_FILE IDENTIFIER SEMICOLON  */
#line 245 "mrmac/parser.y"
        {
            if (smacro_file_seen)
            {
                set_compile_error(yylineno, "SMACRO_FILE already defined.");
                free((yyvsp[-1].sval));
                YYERROR;
            }
            smacro_file_seen = 1;
            free((yyvsp[-1].sval));
        }
#line 1475 "mrmac/parser.tab.c"
    break;

  case 9: /* $@2: %empty  */
#line 259 "mrmac/parser.y"
        {
            if (begin_macro((yyvsp[0].sval), yylineno) != 0)
            {
                free((yyvsp[0].sval));
                YYERROR;
            }
            free((yyvsp[0].sval));
        }
#line 1488 "mrmac/parser.tab.c"
    break;

  case 10: /* macro_definition: SMACRO IDENTIFIER $@2 macro_header_opt SEMICOLON statement_list END_MACRO SEMICOLON  */
#line 268 "mrmac/parser.y"
        {
            if (resolve_pending_refs() != 0)
                YYERROR;

            emit_byte(OP_HALT);
        }
#line 1499 "mrmac/parser.tab.c"
    break;

  case 15: /* macro_header_item: TO KEYSPEC  */
#line 288 "mrmac/parser.y"
        {
            if (validate_keyspec((yyvsp[0].sval), yylineno) != 0)
            {
                free((yyvsp[0].sval));
                YYERROR;
            }
            free((yyvsp[0].sval));
        }
#line 1512 "mrmac/parser.tab.c"
    break;

  case 16: /* macro_header_item: FROM IDENTIFIER  */
#line 297 "mrmac/parser.y"
        {
            if (validate_mode((yyvsp[0].sval), yylineno) != 0)
            {
                free((yyvsp[0].sval));
                YYERROR;
            }
            free((yyvsp[0].sval));
        }
#line 1525 "mrmac/parser.tab.c"
    break;

  case 30: /* $@3: %empty  */
#line 327 "mrmac/parser.y"
               { current_def_type = TYPE_INT;  }
#line 1531 "mrmac/parser.tab.c"
    break;

  case 32: /* $@4: %empty  */
#line 328 "mrmac/parser.y"
               { current_def_type = TYPE_STR;  }
#line 1537 "mrmac/parser.tab.c"
    break;

  case 34: /* $@5: %empty  */
#line 329 "mrmac/parser.y"
               { current_def_type = TYPE_CHAR; }
#line 1543 "mrmac/parser.tab.c"
    break;

  case 36: /* $@6: %empty  */
#line 330 "mrmac/parser.y"
               { current_def_type = TYPE_REAL; }
#line 1549 "mrmac/parser.tab.c"
    break;

  case 38: /* var_list: IDENTIFIER  */
#line 335 "mrmac/parser.y"
        {
            if (add_symbol((yyvsp[0].sval), current_def_type) == -1)
            {
                free((yyvsp[0].sval));
                YYERROR;
            }
            free((yyvsp[0].sval));
        }
#line 1562 "mrmac/parser.tab.c"
    break;

  case 39: /* var_list: var_list COMMA IDENTIFIER  */
#line 344 "mrmac/parser.y"
        {
            if (add_symbol((yyvsp[0].sval), current_def_type) == -1)
            {
                free((yyvsp[0].sval));
                YYERROR;
            }
            free((yyvsp[0].sval));
        }
#line 1575 "mrmac/parser.tab.c"
    break;

  case 40: /* label_definition: IDENTIFIER COLON  */
#line 356 "mrmac/parser.y"
        {
            if (define_label((yyvsp[-1].sval), yylineno) != 0)
            {
                free((yyvsp[-1].sval));
                YYERROR;
            }
            free((yyvsp[-1].sval));
        }
#line 1588 "mrmac/parser.tab.c"
    break;

  case 41: /* goto_statement: GOTO IDENTIFIER  */
#line 368 "mrmac/parser.y"
        {
            size_t patch_pos;

            emit_byte(OP_GOTO);
            patch_pos = emit_get_pos();
            emit_int(-1);

            if (add_pending_ref((yyvsp[0].sval), patch_pos, yylineno, REF_GOTO) != 0)
            {
                free((yyvsp[0].sval));
                YYERROR;
            }

            free((yyvsp[0].sval));
        }
#line 1608 "mrmac/parser.tab.c"
    break;

  case 42: /* ret_statement: RET  */
#line 387 "mrmac/parser.y"
        {
            emit_byte(OP_RET);
        }
#line 1616 "mrmac/parser.tab.c"
    break;

  case 43: /* assignment_statement: IDENTIFIER ASSIGN expression  */
#line 394 "mrmac/parser.y"
        {
            int type = 0;

            if (lookup_symbol((yyvsp[-2].sval), &type) < 0)
            {
                free((yyvsp[-2].sval));
                yyerror("Variable expected.");
                YYERROR;
            }

            emit_byte(OP_STORE_VAR);
            emit_string((yyvsp[-2].sval));
            free((yyvsp[-2].sval));
        }
#line 1635 "mrmac/parser.tab.c"
    break;

  case 44: /* call_statement: CALL IDENTIFIER  */
#line 412 "mrmac/parser.y"
        {
            size_t patch_pos;

            emit_byte(OP_CALL);
            patch_pos = emit_get_pos();
            emit_int(-1);

            if (add_pending_ref((yyvsp[0].sval), patch_pos, yylineno, REF_CALL) != 0)
            {
                free((yyvsp[0].sval));
                YYERROR;
            }

            free((yyvsp[0].sval));
        }
#line 1655 "mrmac/parser.tab.c"
    break;

  case 45: /* call_statement: TVCALL IDENTIFIER LPAREN argument_list RPAREN  */
#line 428 "mrmac/parser.y"
        {
            emit_byte(OP_TVCALL);
            emit_string((yyvsp[-3].sval));
            emit_byte((unsigned char) param_count);
            free((yyvsp[-3].sval));
            param_count = 0;
        }
#line 1667 "mrmac/parser.tab.c"
    break;

  case 46: /* if_jz_mark: %empty  */
#line 439 "mrmac/parser.y"
        {
            emit_byte(OP_JZ);
            (yyval.ival) = (int) emit_get_pos();
            emit_int(-1);
        }
#line 1677 "mrmac/parser.tab.c"
    break;

  case 47: /* else_jump_mark: %empty  */
#line 448 "mrmac/parser.y"
        {
            emit_byte(OP_GOTO);
            (yyval.ival) = (int) emit_get_pos();
            emit_int(-1);
        }
#line 1687 "mrmac/parser.tab.c"
    break;

  case 48: /* while_start_mark: %empty  */
#line 457 "mrmac/parser.y"
        {
            (yyval.ival) = (int) emit_get_pos();
        }
#line 1695 "mrmac/parser.tab.c"
    break;

  case 49: /* while_jz_mark: %empty  */
#line 464 "mrmac/parser.y"
        {
            emit_byte(OP_JZ);
            (yyval.ival) = (int) emit_get_pos();
            emit_int(-1);
        }
#line 1705 "mrmac/parser.tab.c"
    break;

  case 50: /* if_statement: IF expression THEN if_jz_mark statement_list END  */
#line 473 "mrmac/parser.y"
        {
            emit_patch_int((yyvsp[-2].ival), (int) emit_get_pos());
        }
#line 1713 "mrmac/parser.tab.c"
    break;

  case 51: /* if_statement: IF expression THEN if_jz_mark statement_list ELSE else_jump_mark statement_list END  */
#line 477 "mrmac/parser.y"
        {
            emit_patch_int((yyvsp[-5].ival), (yyvsp[-2].ival) + (int) sizeof(int));
            emit_patch_int((yyvsp[-2].ival), (int) emit_get_pos());
        }
#line 1722 "mrmac/parser.tab.c"
    break;

  case 52: /* while_statement: WHILE while_start_mark expression DO while_jz_mark statement_list END  */
#line 485 "mrmac/parser.y"
        {
            emit_byte(OP_GOTO);
            emit_int((yyvsp[-5].ival));
            emit_patch_int((yyvsp[-2].ival), (int) emit_get_pos());
        }
#line 1732 "mrmac/parser.tab.c"
    break;

  case 56: /* argument: expression  */
#line 500 "mrmac/parser.y"
        {
            param_count++;
        }
#line 1740 "mrmac/parser.tab.c"
    break;

  case 57: /* argument: STRING_LITERAL  */
#line 504 "mrmac/parser.y"
        {
            emit_byte(OP_PUSH_S);
            emit_string((yyvsp[0].sval));
            free((yyvsp[0].sval));
            param_count++;
        }
#line 1751 "mrmac/parser.tab.c"
    break;

  case 58: /* expression: INTEGER_LITERAL  */
#line 514 "mrmac/parser.y"
        {
            emit_byte(OP_PUSH_I);
            emit_int((yyvsp[0].ival));
        }
#line 1760 "mrmac/parser.tab.c"
    break;

  case 59: /* expression: IDENTIFIER  */
#line 519 "mrmac/parser.y"
        {
            int type = 0;

            if (lookup_symbol((yyvsp[0].sval), &type) < 0)
            {
                free((yyvsp[0].sval));
                yyerror("Variable expected.");
                YYERROR;
            }

            emit_byte(OP_LOAD_VAR);
            emit_string((yyvsp[0].sval));
            free((yyvsp[0].sval));
        }
#line 1779 "mrmac/parser.tab.c"
    break;

  case 61: /* expression: MINUS expression  */
#line 535 "mrmac/parser.y"
        {
            emit_byte(OP_NEG);
        }
#line 1787 "mrmac/parser.tab.c"
    break;

  case 62: /* expression: NOT expression  */
#line 539 "mrmac/parser.y"
        {
            emit_byte(OP_NOT);
        }
#line 1795 "mrmac/parser.tab.c"
    break;

  case 63: /* expression: expression PLUS expression  */
#line 543 "mrmac/parser.y"
        {
            emit_byte(OP_ADD);
        }
#line 1803 "mrmac/parser.tab.c"
    break;

  case 64: /* expression: expression MINUS expression  */
#line 547 "mrmac/parser.y"
        {
            emit_byte(OP_SUB);
        }
#line 1811 "mrmac/parser.tab.c"
    break;

  case 65: /* expression: expression MULT expression  */
#line 551 "mrmac/parser.y"
        {
            emit_byte(OP_MUL);
        }
#line 1819 "mrmac/parser.tab.c"
    break;

  case 66: /* expression: expression DIV expression  */
#line 555 "mrmac/parser.y"
        {
            emit_byte(OP_DIV);
        }
#line 1827 "mrmac/parser.tab.c"
    break;

  case 67: /* expression: expression MOD expression  */
#line 559 "mrmac/parser.y"
        {
            emit_byte(OP_MOD);
        }
#line 1835 "mrmac/parser.tab.c"
    break;

  case 68: /* expression: expression EQ expression  */
#line 563 "mrmac/parser.y"
        {
            emit_byte(OP_CMP_EQ);
        }
#line 1843 "mrmac/parser.tab.c"
    break;

  case 69: /* expression: expression NE expression  */
#line 567 "mrmac/parser.y"
        {
            emit_byte(OP_CMP_NE);
        }
#line 1851 "mrmac/parser.tab.c"
    break;

  case 70: /* expression: expression LT expression  */
#line 571 "mrmac/parser.y"
        {
            emit_byte(OP_CMP_LT);
        }
#line 1859 "mrmac/parser.tab.c"
    break;

  case 71: /* expression: expression GT expression  */
#line 575 "mrmac/parser.y"
        {
            emit_byte(OP_CMP_GT);
        }
#line 1867 "mrmac/parser.tab.c"
    break;

  case 72: /* expression: expression LE expression  */
#line 579 "mrmac/parser.y"
        {
            emit_byte(OP_CMP_LE);
        }
#line 1875 "mrmac/parser.tab.c"
    break;

  case 73: /* expression: expression GE expression  */
#line 583 "mrmac/parser.y"
        {
            emit_byte(OP_CMP_GE);
        }
#line 1883 "mrmac/parser.tab.c"
    break;

  case 74: /* expression: expression AND expression  */
#line 587 "mrmac/parser.y"
        {
            emit_byte(OP_AND);
        }
#line 1891 "mrmac/parser.tab.c"
    break;

  case 75: /* expression: expression OR expression  */
#line 591 "mrmac/parser.y"
        {
            emit_byte(OP_OR);
        }
#line 1899 "mrmac/parser.tab.c"
    break;

  case 76: /* expression: expression SHL expression  */
#line 595 "mrmac/parser.y"
        {
            emit_byte(OP_SHL);
        }
#line 1907 "mrmac/parser.tab.c"
    break;

  case 77: /* expression: expression SHR expression  */
#line 599 "mrmac/parser.y"
        {
            emit_byte(OP_SHR);
        }
#line 1915 "mrmac/parser.tab.c"
    break;


#line 1919 "mrmac/parser.tab.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 604 "mrmac/parser.y"


void yyerror(const char *s)
{
    if (strcmp(s, "syntax error") == 0)
        set_compile_error(yylineno, "Syntax Error.");
    else
        set_compile_error(yylineno, s);
}
