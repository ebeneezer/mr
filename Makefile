# Makefile for the main editor (mr) with integrated RAM compilation (Debug mode)
# Minimal, conservative fix:
# - keep the original flat MR build
# - build TVision in ./tvision/build
# - link explicitly against ./tvision/build/libtvision.a
# - no variant/object-dir refactor

CXX = g++
CC = gcc
FLEX = flex
BISON = bison
TAR = tar
CMAKE ?= cmake
GIT ?= git
PATCH ?= patch
NPROC ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)

TVISION_SOURCE_DIR = ./tvision
TVISION_PATCH_DIR ?= ./patches
TVISION_PATCHES := $(sort $(wildcard $(TVISION_PATCH_DIR)/*.patch))
TVISION_UPSTREAM_URL ?= https://github.com/magiblot/tvision.git
TVISION_UPSTREAM_REF ?= master
TVISION_AUTO_SYNC ?= 0
TVISION_CACHE_DIR ?= ./.vendor-cache/tvision.git
TVISION_VENDOR_ROOT ?= ./build/vendor
TVISION_VENDOR_SRC ?= $(TVISION_VENDOR_ROOT)/tvision-src
TVISION_VENDOR_BUILD ?= $(TVISION_VENDOR_ROOT)/tvision-build

ifeq ($(TVISION_AUTO_SYNC),1)
TVISION_ACTIVE_SOURCE_DIR := $(TVISION_VENDOR_SRC)
TVISION_ACTIVE_BUILD_DIR := $(TVISION_VENDOR_BUILD)
else
TVISION_ACTIVE_SOURCE_DIR := $(TVISION_SOURCE_DIR)
TVISION_ACTIVE_BUILD_DIR := $(TVISION_SOURCE_DIR)/build
endif

# Include paths
INCLUDES = -I$(TVISION_ACTIVE_SOURCE_DIR)/include -I./mrmac -I./piecetable -I./ui

# Debug flags: -g for symbols, -O0 disables optimizations
CXXFLAGS = -Wall -g -O0 $(INCLUDES)
CFLAGS = -Wall -g -O0 $(INCLUDES)

TVISION_BUILD_DIR = $(TVISION_ACTIVE_BUILD_DIR)
TVISION_LIB = $(TVISION_BUILD_DIR)/libtvision.a
TVISION_CMAKE_FLAGS = \
	-DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_C_COMPILER=$(CC) \
	-DCMAKE_CXX_COMPILER=$(CXX) \
	-DTV_BUILD_EXAMPLES=OFF \
	-DTV_BUILD_TESTS=OFF \
	-DTV_BUILD_AVSCOLOR=OFF \
	-DTV_OPTIMIZE_BUILD=OFF

# Linker paths and libraries
NCURSESW_LIB ?= $(shell if [ -e /lib/x86_64-linux-gnu/libncursesw.so.6 ]; then echo -l:libncursesw.so.6; else echo -lncursesw; fi)
GPM_LIB ?= $(shell if [ -e /lib/x86_64-linux-gnu/libgpm.so.2 ]; then echo -l:libgpm.so.2; else echo -lgpm; fi)
TINFO_LIB ?= $(shell if [ -e /lib/x86_64-linux-gnu/libtinfo.so.6 ]; then echo -l:libtinfo.so.6; else echo -ltinfo; fi)
LDFLAGS = $(TVISION_LIB) $(NCURSESW_LIB) $(GPM_LIB) $(TINFO_LIB)

TARGET = mr
CHAT_CONTEXT_ARCHIVE = mr-chat-context.tar.bz2
TVISION_ARCHIVE = tvision.tar.bz2
BISON_ARCHIVE = bison-system.tar.bz2
FLEX_ARCHIVE = flex-system.tar.bz2

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
	ui/TMREditWindow.hpp \
	ui/TMRIndicator.hpp \
	ui/TMRFileEditor.hpp \
	ui/TMRSyntax.hpp \
	ui/TMRSyntax.cpp \
	tvision/include/tvision/internal/findfrst.h \
	tvision/source/platform/findfrst.cpp \
	tvision/source/platform/dir.cpp

CHAT_CONTEXT_DIRS = \
	mrmac/macros

# C++ source files (Editor and VM)
CXX_SOURCES = \
	mr.cpp \
	mrmac/mrvm.cpp \
	ui/mrpalette.cpp \
	ui/mrmacrotest.cpp \
	ui/mrwindowlist.cpp \
	ui/TMRSyntax.cpp

CXX_OBJECTS = $(CXX_SOURCES:.cpp=.o)

# C source files (In-Memory Macro Compiler)
C_SOURCES = \
	mrmac/mrmac.c \
	mrmac/lex.yy.c \
	mrmac/parser.tab.c

C_OBJECTS = $(C_SOURCES:.c=.o)

.PHONY: all clean clean-root-bz2 clean-tvision clean-tvision-cache rebuild-tvision \
	tvision-upstream-init tvision-upstream-fetch tvision-vendor-clean tvision-vendor-export \
	tvision-vendor-normalize tvision-vendor-patch tvision-vendor-prepare Synchronisation \
	context-tar tvision-tar bison-tar flex-tar

CHAT_CONTEXT_GENERATED = mrmac/parser.tab.h mrmac/parser.tab.c mrmac/lex.yy.c

all: clean-root-bz2 $(TARGET)

Synchronisation: clean-root-bz2 context-tar tvision-tar bison-tar flex-tar

clean-root-bz2:
	rm -f ./*.bz2

# TVision: default local build or optional auto-synced vendor build.
$(TVISION_SOURCE_DIR)/build/libtvision.a: $(TVISION_SOURCE_DIR)/CMakeLists.txt $(TVISION_SOURCE_DIR)/source/CMakeLists.txt
	@mkdir -p $(TVISION_SOURCE_DIR)/build
	$(CMAKE) -S $(TVISION_SOURCE_DIR) -B $(TVISION_SOURCE_DIR)/build $(TVISION_CMAKE_FLAGS)
	$(CMAKE) --build $(TVISION_SOURCE_DIR)/build --target tvision -j$(NPROC)

tvision-upstream-init:
	@mkdir -p $(dir $(TVISION_CACHE_DIR))
	@if [ ! -d $(TVISION_CACHE_DIR) ]; then \
		$(GIT) clone --mirror $(TVISION_UPSTREAM_URL) $(TVISION_CACHE_DIR); \
	fi

tvision-upstream-fetch: tvision-upstream-init
	$(GIT) --git-dir=$(TVISION_CACHE_DIR) fetch --prune origin

tvision-vendor-clean:
	rm -rf $(TVISION_VENDOR_SRC) $(TVISION_VENDOR_BUILD)

tvision-vendor-export: tvision-upstream-fetch
	rm -rf $(TVISION_VENDOR_SRC)
	@mkdir -p $(TVISION_VENDOR_SRC)
	$(GIT) --git-dir=$(TVISION_CACHE_DIR) archive --format=tar $(TVISION_UPSTREAM_REF) | $(TAR) -xf - -C $(TVISION_VENDOR_SRC)

tvision-vendor-normalize: tvision-vendor-export
	find $(TVISION_VENDOR_SRC) -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.cmake' -o -name 'CMakeLists.txt' \) -exec sed -i 's/\r$$//' {} +

tvision-vendor-patch: tvision-vendor-normalize
	@set -e; \
	if [ -n "$(strip $(TVISION_PATCHES))" ]; then \
		for p in $(TVISION_PATCHES); do \
			echo "Applying $$p"; \
			$(PATCH) -d $(TVISION_VENDOR_SRC) -p1 --forward < "$$p"; \
		done; \
	fi

$(TVISION_VENDOR_BUILD)/libtvision.a: tvision-vendor-patch
	@mkdir -p $(TVISION_VENDOR_BUILD)
	$(CMAKE) -S $(TVISION_VENDOR_SRC) -B $(TVISION_VENDOR_BUILD) $(TVISION_CMAKE_FLAGS)
	$(CMAKE) --build $(TVISION_VENDOR_BUILD) --target tvision -j$(NPROC)

tvision-vendor-prepare: $(TVISION_VENDOR_BUILD)/libtvision.a

ifeq ($(TVISION_AUTO_SYNC),1)
$(CXX_OBJECTS) $(C_OBJECTS): $(TVISION_LIB)
endif

clean-tvision:
	rm -rf $(TVISION_SOURCE_DIR)/build $(TVISION_VENDOR_ROOT)

clean-tvision-cache:
	rm -rf $(TVISION_CACHE_DIR)

rebuild-tvision: clean-tvision $(TVISION_LIB)

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
mr.o: mr.cpp mrmac/mrmac.h mrmac/mrvm.hpp ui/mrpalette.hpp ui/mrmacrotest.hpp ui/mrwindowlist.hpp ui/TMREditWindow.hpp ui/TMRIndicator.hpp ui/TMRFileEditor.hpp ui/TMRTextBuffer.hpp ui/TMRTextBufferModel.hpp ui/TMRSyntax.hpp
mrmac/mrvm.o: mrmac/mrvm.cpp mrmac/mrvm.hpp mrmac/mrmac.h ui/mrwindowlist.hpp ui/TMREditWindow.hpp ui/TMRTextBuffer.hpp ui/TMRFileEditor.hpp ui/TMRTextBufferModel.hpp ui/TMRSyntax.hpp
ui/mrpalette.o: ui/mrpalette.cpp ui/mrpalette.hpp
ui/mrmacrotest.o: ui/mrmacrotest.cpp ui/mrmacrotest.hpp mrmac/mrmac.h mrmac/mrvm.hpp
ui/mrwindowlist.o: ui/mrwindowlist.cpp ui/mrwindowlist.hpp ui/TMREditWindow.hpp ui/TMRIndicator.hpp ui/TMRFileEditor.hpp ui/TMRTextBuffer.hpp ui/TMRTextBufferModel.hpp ui/TMRSyntax.hpp
ui/TMRSyntax.o: ui/TMRSyntax.cpp ui/TMRSyntax.hpp

# 4. Linker call
$(TARGET): $(TVISION_LIB) $(CXX_OBJECTS) $(C_OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# 5. Create a tar archive with the files relevant for ChatGPT sync / review
context-tar: clean-root-bz2 $(CHAT_CONTEXT_GENERATED)
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

# 6. Create a tar archive with the complete tvision tree, including binaries
tvision-tar: clean-root-bz2
	@set -e; \
	if [ ! -d tvision ]; then \
		echo "Directory tvision not found."; \
		exit 1; \
	fi; \
	$(TAR) -cjf $(TVISION_ARCHIVE) tvision; \
	echo "Created $(TVISION_ARCHIVE)"

# 7. Create a bzip2 tar archive with the system bison binary and support files
bison-tar: clean-root-bz2
	@set -e; \
	if [ ! -x /usr/bin/bison ]; then \
		echo "System binary /usr/bin/bison not found."; \
		exit 1; \
	fi; \
	if [ ! -d /usr/share/bison ]; then \
		echo "Directory /usr/share/bison not found."; \
		exit 1; \
	fi; \
	$(TAR) -cjf $(BISON_ARCHIVE) /usr/bin/bison /usr/share/bison; \
	echo "Created $(BISON_ARCHIVE)"

# 8. Create a bzip2 tar archive with the system flex binary
flex-tar: clean-root-bz2
	@set -e; \
	if [ ! -x /usr/bin/flex ]; then \
		echo "System binary /usr/bin/flex not found."; \
		exit 1; \
	fi; \
	$(TAR) -cjf $(FLEX_ARCHIVE) /usr/bin/flex; \
	echo "Created $(FLEX_ARCHIVE)"

# C++ compilation
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# C compilation
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(CXX_OBJECTS) $(C_OBJECTS) $(TARGET) $(CHAT_CONTEXT_ARCHIVE) $(TVISION_ARCHIVE) $(BISON_ARCHIVE) $(FLEX_ARCHIVE) mrmac/lex.yy.c mrmac/parser.tab.c mrmac/parser.tab.h
	rm -rf $(TVISION_VENDOR_ROOT)
