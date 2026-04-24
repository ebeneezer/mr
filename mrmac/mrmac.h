#ifndef MRMAC_H
#define MRMAC_H

#include <stddef.h>

/* Opcode set for the current MRMAC core */
#define OP_TVCALL 0x20
#define OP_JZ 0x21
#define OP_CALL 0x22
#define OP_RET 0x23
#define OP_INTRINSIC 0x24
#define OP_VAL 0x25
#define OP_RVAL 0x26
#define OP_FIRST_GLOBAL 0x27
#define OP_NEXT_GLOBAL 0x28
#define OP_PROC 0x29
#define OP_PROC_VAR 0x2A

#define OP_PUSH_I 0x30
#define OP_PUSH_S 0x31
#define OP_STORE_VAR 0x32
#define OP_LOAD_VAR 0x33
#define OP_GOTO 0x34
#define OP_DEF_VAR 0x35
#define OP_PUSH_R 0x36
#define OP_HALT 0xFF

/* Arithmetic */
#define OP_ADD 0x40
#define OP_SUB 0x41
#define OP_MUL 0x42
#define OP_DIV 0x43
#define OP_MOD 0x44
#define OP_NEG 0x45

/* Comparisons */
#define OP_CMP_EQ 0x50
#define OP_CMP_NE 0x51
#define OP_CMP_LT 0x52
#define OP_CMP_GT 0x53
#define OP_CMP_LE 0x54
#define OP_CMP_GE 0x55

/* Logic / bit operations */
#define OP_AND 0x60
#define OP_OR 0x61
#define OP_NOT 0x62
#define OP_SHL 0x63
#define OP_SHR 0x64
#define OP_BIT_AND 0x65
#define OP_BIT_OR 0x66
#define OP_BIT_XOR 0x67

/* Multi-Edit data types */
#define TYPE_INT 1
#define TYPE_STR 2
#define TYPE_CHAR 3
#define TYPE_REAL 4

/* Macro attribute flags */
#define MACRO_ATTR_TRANS 0x01
#define MACRO_ATTR_DUMP 0x02
#define MACRO_ATTR_PERM 0x04

/* Macro invocation modes used by $MACRO ... FROM ... */
#define MACRO_MODE_EDIT 0
#define MACRO_MODE_DOS_SHELL 1
#define MACRO_MODE_ALL 255

#ifdef __cplusplus
extern "C" {
#endif

void emit_byte(unsigned char byte);
void emit_int(int value);
void emit_double(double value);
void emit_string(const char *s);

/* Helper functions for backpatching */
size_t emit_get_pos(void);
void emit_patch_int(size_t pos, int value);

/* Error handling */
void set_compile_error(int line, const char *msg);
const char *get_last_compile_error(void);

/* Symbol table management */
void clear_symbols(void);
int add_symbol(const char *name, int type);
int lookup_symbol(const char *name, int *out_type);

/* Main function for in-memory compilation */
unsigned char *compile_macro_code(const char *source, size_t *out_size);

/* Information about the most recently compiled macro source. */
int get_compiled_macro_count(void);
const char *get_compiled_macro_name(int index);
int get_compiled_macro_entry(int index);
int get_compiled_macro_flags(int index);
const char *get_compiled_macro_keyspec(int index);
int get_compiled_macro_mode(int index);
const char *get_compiled_macro_file_name(void);

#ifdef __cplusplus
}
#endif

#endif
