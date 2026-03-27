#include "mrmac.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern int yyparse(void);
extern int yylineno;

typedef struct yy_buffer_state *YY_BUFFER_STATE;
extern YY_BUFFER_STATE yy_scan_string(const char *yystr);
extern void yy_delete_buffer(YY_BUFFER_STATE buffer);
extern int yylex_destroy(void);

#define INITIAL_CODE_CAPACITY 256
#define MAX_SYMBOLS 200
#define MAX_SYMBOL_NAME 20

typedef struct
{
	char name[MAX_SYMBOL_NAME + 1];
	int type;
} SymbolEntry;

static unsigned char *g_code = NULL;
static size_t g_code_size = 0;
static size_t g_code_capacity = 0;

static SymbolEntry g_symbols[MAX_SYMBOLS];
static int g_symbol_count = 0;

static char g_last_error[512];
static int g_last_error_line = 0;

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

	new_buf = (unsigned char *)realloc(g_code, new_capacity);
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

	if (name == NULL || *name == '\0')
	{
		set_compile_error(yylineno, "Variable name expected.");
		return -1;
	}

	len = strlen(name);
	if (len > MAX_SYMBOL_NAME)
	{
		set_compile_error(yylineno, "Variable name too long.");
		return -1;
	}

	for (int i = 0; i < g_symbol_count; i++)
	{
		if (strcasecmp(g_symbols[i].name, name) == 0)
		{
			set_compile_error(yylineno, "Duplicate variable.");
			return -1;
		}
	}

	if (g_symbol_count >= MAX_SYMBOLS)
	{
		set_compile_error(yylineno, "Out of variables.");
		return -1;
	}

	strcpy(g_symbols[g_symbol_count].name, name);
	g_symbols[g_symbol_count].type = type;
	g_symbol_count++;
	return 0;
}

int lookup_symbol(const char *name, int *out_type)
{
	if (name == NULL || *name == '\0')
		return -1;

	for (int i = 0; i < g_symbol_count; i++)
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

unsigned char *compile_macro_code(const char *source, size_t *out_size)
{
	YY_BUFFER_STATE buffer = NULL;
	unsigned char *result = NULL;

	if (out_size != NULL)
		*out_size = 0;

	reset_code_buffer();
	clear_symbols();
	reset_error_state();

	if (source == NULL)
	{
		set_compile_error(0, "No source provided.");
		return NULL;
	}

	yylineno = 1;

	buffer = yy_scan_string(source);
	if (buffer == NULL)
	{
		set_compile_error(0, "Failed to initialize lexer input.");
		return NULL;
	}

	if (yyparse() != 0 && g_last_error[0] == '\0')
		set_compile_error(yylineno, "Compilation failed.");

	yy_delete_buffer(buffer);
	yylex_destroy();

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
		result = (unsigned char *)malloc(1);
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

	result = (unsigned char *)malloc(g_code_size);
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