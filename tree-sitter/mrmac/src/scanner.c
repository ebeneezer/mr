#include <stdbool.h>
#include <stdint.h>
#include "tree_sitter/parser.h"

enum TokenType {
	COMMENT,
};

void *tree_sitter_mrmac_external_scanner_create(void) {
	return NULL;
}

void tree_sitter_mrmac_external_scanner_destroy(void *payload) {
	(void) payload;
}

unsigned tree_sitter_mrmac_external_scanner_serialize(void *payload, char *buffer) {
	(void) payload;
	(void) buffer;
	return 0;
}

void tree_sitter_mrmac_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
	(void) payload;
	(void) buffer;
	(void) length;
}

bool tree_sitter_mrmac_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
	unsigned depth = 0;

	(void) payload;
	if (!valid_symbols[COMMENT]) return false;
	if (lexer->lookahead != '{') return false;

	lexer->result_symbol = COMMENT;
	while (true) {
		if (lexer->lookahead == 0) return depth > 0;
		if (lexer->lookahead == '{') {
			++depth;
			lexer->advance(lexer, false);
			continue;
		}
		if (lexer->lookahead == '}') {
			lexer->advance(lexer, false);
			if (depth == 0) return false;
			--depth;
			if (depth == 0) return true;
			continue;
		}
		lexer->advance(lexer, false);
	}
}
