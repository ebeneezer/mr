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
NCURSESW_LIB ?= $(shell if [ -e /lib/x86_64-linux-gnu/libncursesw.so.6 ]; then echo -l:libncursesw.so.6; else echo -lncursesw; fi)
GPM_LIB ?= $(shell if [ -e /lib/x86_64-linux-gnu/libgpm.so.2 ]; then echo -l:libgpm.so.2; else echo -lgpm; fi)
TINFO_LIB ?= $(shell if [ -e /lib/x86_64-linux-gnu/libtinfo.so.6 ]; then echo -l:libtinfo.so.6; else echo -ltinfo; fi)
LDFLAGS = -L./tvision/build -ltvision $(NCURSESW_LIB) $(GPM_LIB) $(TINFO_LIB)

TARGET = mr
CHAT_CONTEXT_ARCHIVE = mr-chat-context.tar.bz2

# Files relevant for ChatGPT sync / review
CHAT_CONTEXT_FILES = \
	Makefile \
	filelist.txt \
	mr.cpp \
	mrmac/mrmac.h \
	mrmac/mrmac.c \
	mrmac/parser.y \
	mrmac/lexer.l \
	mrmac/parser.tab.h \
	mrmac/parser.tab.c \
	mrmac/lex.yy.c \
	mrmac/mrvm.cpp \
	mrmac/mrvm.hpp \
	ui/mrmacrotest.cpp \
	ui/mrmacrotest.hpp \
	ui/mrpalette.cpp \
	ui/mrpalette.hpp \
	ui/mrwindowlist.cpp \
	ui/mrwindowlist.hpp \
	mr.hlp \
	ui/TMRMenuBar.hpp \
	ui/TMRMenuBox.hpp \
	ui/TMRFrame.hpp \
	ui/TMRStatusLine.hpp \
	ui/TMRDeskTop.hpp \
	ui/TMREditWindow.hpp

CHAT_CONTEXT_DIRS = \
	mrmac/macros

# C++ source files (Editor and VM)
CXX_SOURCES = \
	mr.cpp \
	mrmac/mrvm.cpp \
	ui/mrpalette.cpp \
	ui/mrmacrotest.cpp \
	ui/mrwindowlist.cpp

CXX_OBJECTS = $(CXX_SOURCES:.cpp=.o)

# C source files (In-Memory Macro Compiler)
C_SOURCES = \
	mrmac/mrmac.c \
	mrmac/lex.yy.c \
	mrmac/parser.tab.c

C_OBJECTS = $(C_SOURCES:.c=.o)

.PHONY: all clean context-tar

CHAT_CONTEXT_GENERATED = mrmac/parser.tab.h mrmac/parser.tab.c mrmac/lex.yy.c

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
mr.o: mr.cpp mrmac/mrmac.h mrmac/mrvm.hpp ui/mrpalette.hpp ui/mrmacrotest.hpp ui/mrwindowlist.hpp
mrmac/mrvm.o: mrmac/mrvm.cpp mrmac/mrvm.hpp mrmac/mrmac.h ui/mrwindowlist.hpp
ui/mrpalette.o: ui/mrpalette.cpp ui/mrpalette.hpp
ui/mrmacrotest.o: ui/mrmacrotest.cpp ui/mrmacrotest.hpp mrmac/mrmac.h mrmac/mrvm.hpp
ui/mrwindowlist.o: ui/mrwindowlist.cpp ui/mrwindowlist.hpp ui/TMREditWindow.hpp

# 4. Linker call
$(TARGET): $(CXX_OBJECTS) $(C_OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# 5. Create a tar archive with the files relevant for ChatGPT sync / review
context-tar: $(CHAT_CONTEXT_GENERATED)
	@set -e; \
	files=""; \
	for f in $(CHAT_CONTEXT_FILES); do \
		if [ -f "$$f" ]; then \
			files="$$files $$f"; \
		else \
			echo "Skipping missing file: $$f"; \
		fi; \
	done; \
	for d in $(CHAT_CONTEXT_DIRS); do \
		if [ -d "$$d" ]; then \
			files="$$files $$d"; \
		else \
			echo "Skipping missing directory: $$d"; \
		fi; \
	done; \
	if [ -z "$$files" ]; then \
		echo "No context files found for archive."; \
		exit 1; \
	fi; \
	$(TAR) -cjf $(CHAT_CONTEXT_ARCHIVE) $$files; \
	echo "Created $(CHAT_CONTEXT_ARCHIVE)"

# C++ compilation
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# C compilation
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(CXX_OBJECTS) $(C_OBJECTS) $(TARGET) $(CHAT_CONTEXT_ARCHIVE) mrmac/lex.yy.c mrmac/parser.tab.c mrmac/parser.tab.h
