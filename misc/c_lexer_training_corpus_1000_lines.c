/* c_lexer_training_corpus_1000_lines.c - synthetic C lexer corpus, not production code */
#pragma once
#pragma message("C lexer corpus")
#warning "intentional lexer-training warning"
#include <assert.h>
#include <complex.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <iso646.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <threads.h>
#include <time.h>
#include <uchar.h>
#include <wchar.h>
#include <wctype.h>
#ifndef C_LEXER_CORPUS_H
#define C_LEXER_CORPUS_H 1
#define CAT_INNER(a,b) a ## b
#define CAT(a,b) CAT_INNER(a,b)
#define STR_INNER(x) #x
#define STR(x) STR_INNER(x)
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define SWAP(type,a,b) do { type tmp = (a); (a) = (b); (b) = tmp; } while (0)
#define LOG(fmt, ...) fprintf(stderr, "[log] " fmt "\n", __VA_ARGS__)
#define EMPTY()
#define MULTI(x) do { int macro_local = (x); macro_local += 1; } while (0)
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(int) >= 2, "int width");
#endif
#if defined(__GNUC__)
#define ATTR_PACKED __attribute__((packed))
#define ATTR_NORETURN __attribute__((noreturn))
#else
#define ATTR_PACKED
#define ATTR_NORETURN
#endif
typedef signed char i8;
typedef unsigned char u8;
typedef short i16;
typedef unsigned short u16;
typedef int i32;
typedef unsigned int u32;
typedef long long i64;
typedef unsigned long long u64;
typedef float f32;
typedef double f64;
typedef long double f80;
typedef enum TokenKind { TK_NONE, TK_IDENT, TK_NUMBER, TK_STRING, TK_CHAR, TK_OP, TK_COMMENT, TK_PP, TK_EOF = 255 } TokenKind;
enum Flags { FLAG_NONE = 0, FLAG_A = 1 << 0, FLAG_B = 1 << 1, FLAG_C = 1 << 2, FLAG_ALL = FLAG_A | FLAG_B | FLAG_C };
struct Position { unsigned line; unsigned column; };
struct Token { TokenKind kind; struct Position pos; union { long i; double f; const char *s; void *p; } value; unsigned flags:3; unsigned escaped:1; unsigned reserved:28; };
union NumberPun { uint64_t u64; int64_t i64; double f64; unsigned char bytes[8]; };
struct ATTR_PACKED PackedRecord { uint8_t tag; uint32_t length; char payload[7]; };
typedef struct Node Node;
struct Node { const char *name; Node *next; Node *children[4]; int (*visit)(Node *, void *); };
static int global_static = 1;
extern int global_extern;
volatile sig_atomic_t global_signal_flag = 0;
_Thread_local int thread_local_counter = 0;
alignas(64) static unsigned char aligned_buffer[256];
static _Atomic int atomic_counter = ATOMIC_VAR_INIT(0);
static const char *s1 = "hello\nworld\t\"quote\"\\slash";
static const char *s2 = "alpha" "beta" "gamma";
static const char *s3 = "\x41\x42\x43";
static const char *s4 = "\u00E4\U0001F600";
static const wchar_t *ws = L"wide string";
static const char16_t u16s[] = u"utf16";
static const char32_t u32s[] = U"utf32";
static const char *comment_like = "/* not comment */ // not comment";
static const char char_literals[] = { 'a', '\n', '\t', '\0', '\x41', '\101' };
static int ints[] = { 0, 1, -1, 0123, 0x7f, 0XDEAD, 1u, 2U, 3l, 4L, 5ul, 6UL, 7ll, 8LL, 9ull, 10ULL };
static double floats[] = { 0.0, 1., .5, 1e10, 1E-10, 0x1.8p+2, 0Xf.fp-1, 1.0f, 2.0F, 3.0l, 4.0L };
static double complex z = 1.0 + 2.0 * I;
noreturn void fatal(const char *message);
ATTR_NORETURN void attr_fatal(const char *message);
static inline int inline_add(int a, int b) { return a + b; }
static int optional_args(int first, ...);
#define TYPE_NAME(x) _Generic((x), int: "int", long: "long", float: "float", double: "double", char *: "char*", const char *: "const char*", default: "other")
static int optional_args(int first, ...) { va_list ap; va_start(ap, first); int sum = first; for (int i = 0; i < 3; ++i) sum += va_arg(ap, int); va_end(ap); return sum; }
noreturn void fatal(const char *message) { fprintf(stderr, "%s\n", message ? message : "fatal"); abort(); }
ATTR_NORETURN void attr_fatal(const char *message) { fatal(message); }
static int compare_ints(const void *lhs, const void *rhs) { const int *a = lhs; const int *b = rhs; return (*a > *b) - (*a < *b); }
static void pointer_examples(void) { int value = 42; int *p = &value; int **pp = &p; int a[5] = { [0] = 1, [2] = 3, [4] = 5 }; int (*ap)[5] = &a; void *opaque = a; const int *pc = &value; int * const cp = &value; volatile int vv = *p; restrict int *rp = p; **pp += (*ap)[0] + (int)(uintptr_t)opaque + *pc + *cp + vv + *rp; }
static int control_flow(int input) { int result = 0; label_start: for (int i = 0; i < input; ++i) { for (int j = input; j > 0; --j) { if (i == j) continue; else if (i * j > 32) break; else result += i ^ j; } } while (result < input * 10) { result++; if (result & 1) continue; if (result > 100) break; } do { result--; } while (result > 50); switch (input) { case 0: result = 0; break; case 1: case 2: result += 2; break; default: result += input; goto label_end; } if (result < 0) goto label_start; label_end: return result; }
static void struct_union_examples(void) { struct Token t = { .kind = TK_IDENT, .pos = { .line = 1, .column = 2 }, .value.s = "identifier", .flags = FLAG_A | FLAG_B, .escaped = 0 }; union NumberPun p = { .f64 = 3.5 }; struct PackedRecord r = { .tag = 1, .length = 7, .payload = "payload" }; Node root = { .name = "root", .next = NULL, .children = { NULL }, .visit = NULL }; t.value.i = (long)p.u64 + r.tag + (root.name != NULL); (void)t; }
static int recursion(int n) { return n <= 1 ? 1 : n * recursion(n - 1); }
static void ops(void) { unsigned x = 0x0f; x <<= 1; x >>= 1; x &= 0xff; x |= 0x10; x ^= 0x01; x = ~x; x = (x && 1) || 0; x = (x == 1) ? x : x + 1; x += 1; x -= 1; x *= 2; x /= 2; x %= 3; x++; --x; }
static void library_examples(void) { char b[128]; snprintf(b, sizeof b, "%s:%d:%f", "text", 42, 3.14); size_t n = strlen(b); char *copy = malloc(n + 1); if (copy) { memcpy(copy, b, n + 1); qsort(ints, COUNT(ints), sizeof ints[0], compare_ints); free(copy); } }
static jmp_buf jb;
static void setjmp_example(void) { if (setjmp(jb) == 0) longjmp(jb, 1); }
static int thread_fn(void *arg) { atomic_fetch_add(&atomic_counter, 1); thread_local_counter += arg != NULL; return 0; }
static void atomic_thread_examples(void) { thrd_t t; mtx_t m; cnd_t c; mtx_init(&m, mtx_plain); cnd_init(&c); thrd_create(&t, thread_fn, NULL); thrd_join(t, NULL); cnd_destroy(&c); mtx_destroy(&m); }
static void pp_examples(void) { const char *name = STR(CAT(alpha, beta)); (void)name; }
/* GENERATED_BLOCK_001: C macros, structs, loops, expressions */
#define GENERATED_MACRO_001(x) (((x) + 1) * (1 + 1))
typedef struct GeneratedStruct001 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct001;
static GeneratedStruct001 generated_value_001 = {
    .id = 1,
    .name = "GeneratedStruct001",
    .payload.i = 1 * 1,
    .bit_a = 1 % 2,
    .bit_b = (1 + 1) % 2,
    .rest = 1
};
static int generated_function_001(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 1) % 2) == 0) {
                total += GENERATED_MACRO_001(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 1: total += generated_value_001.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_002: C macros, structs, loops, expressions */
#define GENERATED_MACRO_002(x) (((x) + 2) * (2 + 1))
typedef struct GeneratedStruct002 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct002;
static GeneratedStruct002 generated_value_002 = {
    .id = 2,
    .name = "GeneratedStruct002",
    .payload.i = 2 * 2,
    .bit_a = 2 % 2,
    .bit_b = (2 + 1) % 2,
    .rest = 2
};
static int generated_function_002(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 2) % 2) == 0) {
                total += GENERATED_MACRO_002(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 2: total += generated_value_002.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_003: C macros, structs, loops, expressions */
#define GENERATED_MACRO_003(x) (((x) + 3) * (3 + 1))
typedef struct GeneratedStruct003 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct003;
static GeneratedStruct003 generated_value_003 = {
    .id = 3,
    .name = "GeneratedStruct003",
    .payload.i = 3 * 3,
    .bit_a = 3 % 2,
    .bit_b = (3 + 1) % 2,
    .rest = 3
};
static int generated_function_003(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 3) % 2) == 0) {
                total += GENERATED_MACRO_003(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 3: total += generated_value_003.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_004: C macros, structs, loops, expressions */
#define GENERATED_MACRO_004(x) (((x) + 4) * (4 + 1))
typedef struct GeneratedStruct004 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct004;
static GeneratedStruct004 generated_value_004 = {
    .id = 4,
    .name = "GeneratedStruct004",
    .payload.i = 4 * 4,
    .bit_a = 4 % 2,
    .bit_b = (4 + 1) % 2,
    .rest = 4
};
static int generated_function_004(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 4) % 2) == 0) {
                total += GENERATED_MACRO_004(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 4: total += generated_value_004.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_005: C macros, structs, loops, expressions */
#define GENERATED_MACRO_005(x) (((x) + 5) * (5 + 1))
typedef struct GeneratedStruct005 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct005;
static GeneratedStruct005 generated_value_005 = {
    .id = 5,
    .name = "GeneratedStruct005",
    .payload.i = 5 * 5,
    .bit_a = 5 % 2,
    .bit_b = (5 + 1) % 2,
    .rest = 5
};
static int generated_function_005(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 5) % 2) == 0) {
                total += GENERATED_MACRO_005(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 5: total += generated_value_005.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_006: C macros, structs, loops, expressions */
#define GENERATED_MACRO_006(x) (((x) + 6) * (6 + 1))
typedef struct GeneratedStruct006 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct006;
static GeneratedStruct006 generated_value_006 = {
    .id = 6,
    .name = "GeneratedStruct006",
    .payload.i = 6 * 6,
    .bit_a = 6 % 2,
    .bit_b = (6 + 1) % 2,
    .rest = 6
};
static int generated_function_006(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 6) % 2) == 0) {
                total += GENERATED_MACRO_006(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 6: total += generated_value_006.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_007: C macros, structs, loops, expressions */
#define GENERATED_MACRO_007(x) (((x) + 7) * (7 + 1))
typedef struct GeneratedStruct007 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct007;
static GeneratedStruct007 generated_value_007 = {
    .id = 7,
    .name = "GeneratedStruct007",
    .payload.i = 7 * 7,
    .bit_a = 7 % 2,
    .bit_b = (7 + 1) % 2,
    .rest = 7
};
static int generated_function_007(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 7) % 2) == 0) {
                total += GENERATED_MACRO_007(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 7: total += generated_value_007.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_008: C macros, structs, loops, expressions */
#define GENERATED_MACRO_008(x) (((x) + 8) * (8 + 1))
typedef struct GeneratedStruct008 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct008;
static GeneratedStruct008 generated_value_008 = {
    .id = 8,
    .name = "GeneratedStruct008",
    .payload.i = 8 * 8,
    .bit_a = 8 % 2,
    .bit_b = (8 + 1) % 2,
    .rest = 8
};
static int generated_function_008(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 8) % 2) == 0) {
                total += GENERATED_MACRO_008(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 8: total += generated_value_008.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_009: C macros, structs, loops, expressions */
#define GENERATED_MACRO_009(x) (((x) + 9) * (9 + 1))
typedef struct GeneratedStruct009 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct009;
static GeneratedStruct009 generated_value_009 = {
    .id = 9,
    .name = "GeneratedStruct009",
    .payload.i = 9 * 9,
    .bit_a = 9 % 2,
    .bit_b = (9 + 1) % 2,
    .rest = 9
};
static int generated_function_009(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 9) % 2) == 0) {
                total += GENERATED_MACRO_009(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 9: total += generated_value_009.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_010: C macros, structs, loops, expressions */
#define GENERATED_MACRO_010(x) (((x) + 10) * (10 + 1))
typedef struct GeneratedStruct010 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct010;
static GeneratedStruct010 generated_value_010 = {
    .id = 10,
    .name = "GeneratedStruct010",
    .payload.i = 10 * 10,
    .bit_a = 10 % 2,
    .bit_b = (10 + 1) % 2,
    .rest = 10
};
static int generated_function_010(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 10) % 2) == 0) {
                total += GENERATED_MACRO_010(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 10: total += generated_value_010.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_011: C macros, structs, loops, expressions */
#define GENERATED_MACRO_011(x) (((x) + 11) * (11 + 1))
typedef struct GeneratedStruct011 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct011;
static GeneratedStruct011 generated_value_011 = {
    .id = 11,
    .name = "GeneratedStruct011",
    .payload.i = 11 * 11,
    .bit_a = 11 % 2,
    .bit_b = (11 + 1) % 2,
    .rest = 11
};
static int generated_function_011(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 11) % 2) == 0) {
                total += GENERATED_MACRO_011(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 11: total += generated_value_011.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_012: C macros, structs, loops, expressions */
#define GENERATED_MACRO_012(x) (((x) + 12) * (12 + 1))
typedef struct GeneratedStruct012 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct012;
static GeneratedStruct012 generated_value_012 = {
    .id = 12,
    .name = "GeneratedStruct012",
    .payload.i = 12 * 12,
    .bit_a = 12 % 2,
    .bit_b = (12 + 1) % 2,
    .rest = 12
};
static int generated_function_012(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 12) % 2) == 0) {
                total += GENERATED_MACRO_012(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 12: total += generated_value_012.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_013: C macros, structs, loops, expressions */
#define GENERATED_MACRO_013(x) (((x) + 13) * (13 + 1))
typedef struct GeneratedStruct013 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct013;
static GeneratedStruct013 generated_value_013 = {
    .id = 13,
    .name = "GeneratedStruct013",
    .payload.i = 13 * 13,
    .bit_a = 13 % 2,
    .bit_b = (13 + 1) % 2,
    .rest = 13
};
static int generated_function_013(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 13) % 2) == 0) {
                total += GENERATED_MACRO_013(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 13: total += generated_value_013.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_014: C macros, structs, loops, expressions */
#define GENERATED_MACRO_014(x) (((x) + 14) * (14 + 1))
typedef struct GeneratedStruct014 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct014;
static GeneratedStruct014 generated_value_014 = {
    .id = 14,
    .name = "GeneratedStruct014",
    .payload.i = 14 * 14,
    .bit_a = 14 % 2,
    .bit_b = (14 + 1) % 2,
    .rest = 14
};
static int generated_function_014(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 14) % 2) == 0) {
                total += GENERATED_MACRO_014(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 14: total += generated_value_014.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_015: C macros, structs, loops, expressions */
#define GENERATED_MACRO_015(x) (((x) + 15) * (15 + 1))
typedef struct GeneratedStruct015 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct015;
static GeneratedStruct015 generated_value_015 = {
    .id = 15,
    .name = "GeneratedStruct015",
    .payload.i = 15 * 15,
    .bit_a = 15 % 2,
    .bit_b = (15 + 1) % 2,
    .rest = 15
};
static int generated_function_015(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 15) % 2) == 0) {
                total += GENERATED_MACRO_015(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 15: total += generated_value_015.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_016: C macros, structs, loops, expressions */
#define GENERATED_MACRO_016(x) (((x) + 16) * (16 + 1))
typedef struct GeneratedStruct016 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct016;
static GeneratedStruct016 generated_value_016 = {
    .id = 16,
    .name = "GeneratedStruct016",
    .payload.i = 16 * 16,
    .bit_a = 16 % 2,
    .bit_b = (16 + 1) % 2,
    .rest = 16
};
static int generated_function_016(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 16) % 2) == 0) {
                total += GENERATED_MACRO_016(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 16: total += generated_value_016.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_017: C macros, structs, loops, expressions */
#define GENERATED_MACRO_017(x) (((x) + 17) * (17 + 1))
typedef struct GeneratedStruct017 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct017;
static GeneratedStruct017 generated_value_017 = {
    .id = 17,
    .name = "GeneratedStruct017",
    .payload.i = 17 * 17,
    .bit_a = 17 % 2,
    .bit_b = (17 + 1) % 2,
    .rest = 17
};
static int generated_function_017(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 17) % 2) == 0) {
                total += GENERATED_MACRO_017(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 17: total += generated_value_017.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_018: C macros, structs, loops, expressions */
#define GENERATED_MACRO_018(x) (((x) + 18) * (18 + 1))
typedef struct GeneratedStruct018 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct018;
static GeneratedStruct018 generated_value_018 = {
    .id = 18,
    .name = "GeneratedStruct018",
    .payload.i = 18 * 18,
    .bit_a = 18 % 2,
    .bit_b = (18 + 1) % 2,
    .rest = 18
};
static int generated_function_018(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 18) % 2) == 0) {
                total += GENERATED_MACRO_018(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 18: total += generated_value_018.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_019: C macros, structs, loops, expressions */
#define GENERATED_MACRO_019(x) (((x) + 19) * (19 + 1))
typedef struct GeneratedStruct019 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct019;
static GeneratedStruct019 generated_value_019 = {
    .id = 19,
    .name = "GeneratedStruct019",
    .payload.i = 19 * 19,
    .bit_a = 19 % 2,
    .bit_b = (19 + 1) % 2,
    .rest = 19
};
static int generated_function_019(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 19) % 2) == 0) {
                total += GENERATED_MACRO_019(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 19: total += generated_value_019.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_020: C macros, structs, loops, expressions */
#define GENERATED_MACRO_020(x) (((x) + 20) * (20 + 1))
typedef struct GeneratedStruct020 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct020;
static GeneratedStruct020 generated_value_020 = {
    .id = 20,
    .name = "GeneratedStruct020",
    .payload.i = 20 * 20,
    .bit_a = 20 % 2,
    .bit_b = (20 + 1) % 2,
    .rest = 20
};
static int generated_function_020(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 20) % 2) == 0) {
                total += GENERATED_MACRO_020(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 20: total += generated_value_020.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_021: C macros, structs, loops, expressions */
#define GENERATED_MACRO_021(x) (((x) + 21) * (21 + 1))
typedef struct GeneratedStruct021 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct021;
static GeneratedStruct021 generated_value_021 = {
    .id = 21,
    .name = "GeneratedStruct021",
    .payload.i = 21 * 21,
    .bit_a = 21 % 2,
    .bit_b = (21 + 1) % 2,
    .rest = 21
};
static int generated_function_021(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 21) % 2) == 0) {
                total += GENERATED_MACRO_021(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 21: total += generated_value_021.id; break;
        default: total += input; break;
    }
    return total;
}
/* GENERATED_BLOCK_022: C macros, structs, loops, expressions */
#define GENERATED_MACRO_022(x) (((x) + 22) * (22 + 1))
typedef struct GeneratedStruct022 {
    int id;
    char name[32];
    union { int i; double f; void *p; } payload;
    unsigned bit_a : 1;
    unsigned bit_b : 1;
    unsigned rest : 30;
} GeneratedStruct022;
static GeneratedStruct022 generated_value_022 = {
    .id = 22,
    .name = "GeneratedStruct022",
    .payload.i = 22 * 22,
    .bit_a = 22 % 2,
    .bit_b = (22 + 1) % 2,
    .rest = 22
};
static int generated_function_022(int input, int (*callback)(int)) {
    int total = 0;
    for (int outer = 0; outer < 3; ++outer) {
        for (int inner = 0; inner < 3; ++inner) {
            if (((outer + inner + input + 22) % 2) == 0) {
                total += GENERATED_MACRO_022(outer + inner);
            } else if (callback != NULL) {
                total += callback(inner);
            } else {
                total -= outer - inner;
            }
        }
    }
    switch (input) {
        case 22: total += generated_value_022.id; break;
        default: total += input; break;
    }
    return total;
}
static void final_corpus_section(void) {
    int matrix[3][3] = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 } };
    int (*row_pointer)[3] = matrix;
    int value = row_pointer[1][2];
    int compound = ((int[]){1, 2, 3, 4})[2];
    struct Position pos = (struct Position){ .line = 10, .column = 20 };
    const char *type_name = TYPE_NAME(value);
    _Atomic int local_atomic = 0;
    atomic_store(&local_atomic, value + compound + (int)pos.line);
    LOG("%s:%d", type_name, atomic_load(&local_atomic));
}
#if 0
inactive_preprocessor_code();
#else
static int active_preprocessor_code = 1;
#endif
static int c_filler_0943(int x) { return (x + 943) ^ (943 << 1); }
static int c_filler_0944(int x) { return (x + 944) ^ (944 << 1); }
static int c_filler_0945(int x) { return (x + 945) ^ (945 << 1); }
static int c_filler_0946(int x) { return (x + 946) ^ (946 << 1); }
static int c_filler_0947(int x) { return (x + 947) ^ (947 << 1); }
static int c_filler_0948(int x) { return (x + 948) ^ (948 << 1); }
static int c_filler_0949(int x) { return (x + 949) ^ (949 << 1); }
static int c_filler_0950(int x) { return (x + 950) ^ (950 << 1); }
static int c_filler_0951(int x) { return (x + 951) ^ (951 << 1); }
static int c_filler_0952(int x) { return (x + 952) ^ (952 << 1); }
static int c_filler_0953(int x) { return (x + 953) ^ (953 << 1); }
static int c_filler_0954(int x) { return (x + 954) ^ (954 << 1); }
static int c_filler_0955(int x) { return (x + 955) ^ (955 << 1); }
static int c_filler_0956(int x) { return (x + 956) ^ (956 << 1); }
static int c_filler_0957(int x) { return (x + 957) ^ (957 << 1); }
static int c_filler_0958(int x) { return (x + 958) ^ (958 << 1); }
static int c_filler_0959(int x) { return (x + 959) ^ (959 << 1); }
static int c_filler_0960(int x) { return (x + 960) ^ (960 << 1); }
static int c_filler_0961(int x) { return (x + 961) ^ (961 << 1); }
static int c_filler_0962(int x) { return (x + 962) ^ (962 << 1); }
static int c_filler_0963(int x) { return (x + 963) ^ (963 << 1); }
static int c_filler_0964(int x) { return (x + 964) ^ (964 << 1); }
static int c_filler_0965(int x) { return (x + 965) ^ (965 << 1); }
static int c_filler_0966(int x) { return (x + 966) ^ (966 << 1); }
static int c_filler_0967(int x) { return (x + 967) ^ (967 << 1); }
static int c_filler_0968(int x) { return (x + 968) ^ (968 << 1); }
static int c_filler_0969(int x) { return (x + 969) ^ (969 << 1); }
static int c_filler_0970(int x) { return (x + 970) ^ (970 << 1); }
static int c_filler_0971(int x) { return (x + 971) ^ (971 << 1); }
static int c_filler_0972(int x) { return (x + 972) ^ (972 << 1); }
static int c_filler_0973(int x) { return (x + 973) ^ (973 << 1); }
static int c_filler_0974(int x) { return (x + 974) ^ (974 << 1); }
static int c_filler_0975(int x) { return (x + 975) ^ (975 << 1); }
static int c_filler_0976(int x) { return (x + 976) ^ (976 << 1); }
static int c_filler_0977(int x) { return (x + 977) ^ (977 << 1); }
static int c_filler_0978(int x) { return (x + 978) ^ (978 << 1); }
static int c_filler_0979(int x) { return (x + 979) ^ (979 << 1); }
static int c_filler_0980(int x) { return (x + 980) ^ (980 << 1); }
static int c_filler_0981(int x) { return (x + 981) ^ (981 << 1); }
static int c_filler_0982(int x) { return (x + 982) ^ (982 << 1); }
static int c_filler_0983(int x) { return (x + 983) ^ (983 << 1); }
static int c_filler_0984(int x) { return (x + 984) ^ (984 << 1); }
static int c_filler_0985(int x) { return (x + 985) ^ (985 << 1); }
static int c_filler_0986(int x) { return (x + 986) ^ (986 << 1); }
static int c_filler_0987(int x) { return (x + 987) ^ (987 << 1); }
static int c_filler_0988(int x) { return (x + 988) ^ (988 << 1); }
static int c_filler_0989(int x) { return (x + 989) ^ (989 << 1); }
static int c_filler_0990(int x) { return (x + 990) ^ (990 << 1); }
static int c_filler_0991(int x) { return (x + 991) ^ (991 << 1); }
static int c_filler_0992(int x) { return (x + 992) ^ (992 << 1); }
static int c_filler_0993(int x) { return (x + 993) ^ (993 << 1); }
static int c_filler_0994(int x) { return (x + 994) ^ (994 << 1); }
static int c_filler_0995(int x) { return (x + 995) ^ (995 << 1); }
static int c_filler_0996(int x) { return (x + 996) ^ (996 << 1); }
static int c_filler_0997(int x) { return (x + 997) ^ (997 << 1); }
static int c_filler_0998(int x) { return (x + 998) ^ (998 << 1); }
static int c_filler_0999(int x) { return (x + 999) ^ (999 << 1); }
static int c_filler_1000(int x) { return (x + 1000) ^ (1000 << 1); }
#endif /* C_LEXER_CORPUS_H */
