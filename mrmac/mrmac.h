#ifndef MRMAC_H
#define MRMAC_H

#include <stddef.h>

/* Opcode set for the current MRMAC core */
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
#define OP_HASH_LOAD 0x37
#define OP_HASH_STORE 0x38
#define OP_HASH_LOAD_VALUE 0x39
#define OP_HASH_STORE_VALUE 0x3A
#define OP_ARRAY_LOAD 0x3B
#define OP_ARRAY_STORE 0x3C
#define OP_ARRAY_LOAD_VALUE 0x3D
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
#define TYPE_HASH 5
#define TYPE_INT_ARRAY 6
#define TYPE_STR_ARRAY 7
#define TYPE_CHAR_ARRAY 8
#define TYPE_REAL_ARRAY 9
#define TYPE_HASH_ARRAY 10

/* Macro attribute flags */
#define MACRO_ATTR_TRANS 0x01
#define MACRO_ATTR_DUMP 0x02
#define MACRO_ATTR_PERM 0x04

/* Macro invocation modes used by $MACRO ... FROM ... */
#define MACRO_MODE_EDIT 0
#define MACRO_MODE_DOS_SHELL 1
#define MACRO_MODE_ALL 255

/* Compiled MRMac unit kinds. */
#define MRMAC_UNIT_MACRO 0
#define MRMAC_UNIT_CLOSURE 1

/* Source-map entry kinds. Values are runtime metadata, not opcodes. */
#define MRMAC_SOURCE_MAP_MACRO_ENTRY 1
#define MRMAC_SOURCE_MAP_STATEMENT 2
#define MRMAC_SOURCE_MAP_EXPRESSION 3
#define MRMAC_SOURCE_MAP_CALL 4
#define MRMAC_SOURCE_MAP_BRANCH 5
#define MRMAC_SOURCE_MAP_LABEL 6

typedef struct {
	size_t bytecodeOffset;
	size_t sourceStartOffset;
	size_t sourceEndOffset;
	int line;
	int column;
	const char *macroName;
	int debuggableKind;
} MRMacSourceMapEntry;

typedef void (*MRMacSourceMapSink)(void *context, const MRMacSourceMapEntry *entry);

typedef struct {
	const char *name;
	int type;
} MRMacWatchSymbol;

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
unsigned char *compile_macro_code_with_source_map(const char *source, size_t *out_size, MRMacSourceMapSink source_map_sink, void *source_map_context);
unsigned char *compile_macro_watch_expression(const char *expression, const MRMacWatchSymbol *symbols, size_t symbol_count, size_t *out_size, int *out_type);
int validate_macro_watch_expression(const char *expression);

/* Information about the most recently compiled macro source. */
int get_compiled_macro_count(void);
const char *get_compiled_macro_name(int index);
int get_compiled_macro_entry(int index);
int get_compiled_macro_flags(int index);
const char *get_compiled_macro_keyspec(int index);
int get_compiled_macro_mode(int index);
int get_compiled_macro_unit_kind(int index);
int get_compiled_macro_tick_ms(int index);
const char *get_compiled_macro_file_name(void);

#ifdef __cplusplus
}
#endif

#endif
