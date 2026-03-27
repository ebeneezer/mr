# Makefile for the main editor (mr) with integrated RAM compilation (Debug mode)

CXX = g++
CC = gcc
FLEX = flex
BISON = bison
TAR = tar

# Include paths
INCLUDES = -I./tvision/include -I./mrmac -I./piecetable -I./ui

# Debug flags: -g for symbols, -O0 disables optimizations
CXXFLAGS = -Wall -g -O0 $(INCLUDES)
CFLAGS = -Wall -g -O0 $(INCLUDES)

# Linker paths and libraries
LDFLAGS = -L./tvision/build -ltvision -lncursesw -lgpm

TARGET = mr
CHAT_CONTEXT_ARCHIVE = mr-chat-context.tar

# Files relevant for ChatGPT sync / review
CHAT_CONTEXT_FILES = \
	Makefile \
	mr.cpp \
	mrmac/mrmac.h \
	mrmac/mrmac.c \
	mrmac/parser.y \
	mrmac/lexer.l \
	mrmac/mrvm.cpp \
	mrmac/mrvm.hpp \
	ui/mrmacrotest.cpp \
	ui/mrmacrotest.hpp \
	ui/theme.mrmac \
	ui/mrpalette.cpp \
	ui/mrpalette.hpp \
	ui/mrtheme.hpp \
	ui/mrtheme.cpp \
	ui/TMRMenuBar.hpp \
	ui/TMRMenuBox.hpp \
	ui/TMRFrame.hpp \
	ui/TMRStatusLine.hpp \
	ui/TMRDeskTop.hpp \
	ui/TMREditWindow.hpp

# C++ source files (Editor and VM)
CXX_SOURCES = \
	mr.cpp \
	mrmac/mrvm.cpp \
	ui/mrpalette.cpp \
	ui/mrmacrotest.cpp

CXX_OBJECTS = $(CXX_SOURCES:.cpp=.o)

# C source files (In-Memory Macro Compiler)
C_SOURCES = \
	mrmac/mrmac.c \
	mrmac/lex.yy.c \
	mrmac/parser.tab.c

C_OBJECTS = $(C_SOURCES:.c=.o)

.PHONY: all clean context-tar

all: $(TARGET) context-tar

# 1. Flex and Bison generation
mrmac/parser.tab.c mrmac/parser.tab.h: mrmac/parser.y
	$(BISON) -d -o mrmac/parser.tab.c mrmac/parser.y

mrmac/lex.yy.c: mrmac/lexer.l mrmac/parser.tab.h
	$(FLEX) -o mrmac/lex.yy.c mrmac/lexer.l

# 2. Dependencies for C compilation
mrmac/lex.yy.o: CFLAGS += -Wno-unused-function
mrmac/lex.yy.o: mrmac/lex.yy.c mrmac/parser.tab.h
mrmac/parser.tab.o: mrmac/parser.tab.c mrmac/parser.tab.h mrmac/mrmac.h
mrmac/mrmac.o: mrmac/mrmac.c mrmac/parser.tab.h mrmac/mrmac.h

# 3. Dependencies for C++ compilation
mr.o: mr.cpp mrmac/mrmac.h mrmac/mrvm.hpp ui/mrpalette.hpp ui/mrmacrotest.hpp
mrmac/mrvm.o: mrmac/mrvm.cpp mrmac/mrvm.hpp mrmac/mrmac.h
ui/mrpalette.o: ui/mrpalette.cpp ui/mrpalette.hpp
ui/mrmacrotest.o: ui/mrmacrotest.cpp ui/mrmacrotest.hpp mrmac/mrmac.h mrmac/mrvm.hpp

# 4. Linker call
$(TARGET): $(CXX_OBJECTS) $(C_OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# 5. Create a tar archive with the files relevant for ChatGPT sync / review
context-tar:
	@set -e; \
	files=""; \
	for f in $(CHAT_CONTEXT_FILES); do \
		if [ -f "$$f" ]; then \
			files="$$files $$f"; \
		else \
			echo "Skipping missing file: $$f"; \
		fi; \
	done; \
	if [ -z "$$files" ]; then \
		echo "No context files found for archive."; \
		exit 1; \
	fi; \
	$(TAR) -cf $(CHAT_CONTEXT_ARCHIVE) $$files; \
	echo "Created $(CHAT_CONTEXT_ARCHIVE)"

# C++ compilation
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# C compilation
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(CXX_OBJECTS) $(C_OBJECTS) $(TARGET) $(CHAT_CONTEXT_ARCHIVE) mrmac/lex.yy.c mrmac/parser.tab.c mrmac/parser.tab.h