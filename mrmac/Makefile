# Makefile für den mrmac Compiler

CC = clang
CFLAGS = -Wall -O2 -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--gc-sections	

# Standard-Ziel, das beim Aufruf von 'make' ausgeführt wird
all: mrmacc

# Bison generiert parser.tab.c und .h. Läuft nur, wenn parser.y oder mrmac.h geändert wurden.
parser.tab.c parser.tab.h: parser.y mrmac.h
	bison -d parser.y

# Flex generiert lex.yy.c. Läuft nur, wenn lexer.l oder parser.tab.h geändert wurden.
lex.yy.c: lexer.l parser.tab.h
	flex lexer.l

# Kompilierung des Compilers (mrmacc)
mrmacc: lex.yy.c parser.tab.c mrmac.c
	$(CC) $(CFLAGS) lex.yy.c parser.tab.c mrmac.c -o mrmacc

# Hilfsziel zum Aufräumen (Aufruf: make clean)
clean:
	rm -f parser.tab.c parser.tab.h lex.yy.c mrmacc test.mr
	