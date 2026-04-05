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
CMAKE ?= cmake
GIT ?= git
PATCH ?= patch
NPROC ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
CLANG_TIDY ?= clang-tidy
BEAR ?= bear
LINT_FILE ?= mrmac/mrvm.cpp

TVISION_SOURCE_DIR = ./tvision
TVISION_PATCH_DIR ?= ./patches
TVISION_PATCHES := $(sort $(wildcard $(TVISION_PATCH_DIR)/*.patch))
TVISION_LOCAL_PATCH_STAMP ?= $(TVISION_SOURCE_DIR)/.mr-patches-applied
TVISION_UPSTREAM_URL ?= https://github.com/magiblot/tvision.git
TVISION_UPSTREAM_REF ?= master
TVISION_ACTIVE_SOURCE_DIR := $(TVISION_SOURCE_DIR)
TVISION_ACTIVE_BUILD_DIR := $(TVISION_SOURCE_DIR)/build

# Include paths
INCLUDES = -I$(TVISION_ACTIVE_SOURCE_DIR)/include -I./mrmac -I./piecetable -I./ui -I./coprocessor -I./app -I./app/commands -I./dialogs -I./config

# Language/runtime configuration.
CXXSTD ?= gnu++20
PTHREAD_FLAGS ?= -pthread

# Debug flags: -g for symbols, -O0 disables optimizations
CXXFLAGS = -Wall -g -O0 -std=$(CXXSTD) $(PTHREAD_FLAGS) $(INCLUDES)
CFLAGS = -Wall -g -O0 $(INCLUDES)

TVISION_BUILD_DIR = $(TVISION_ACTIVE_BUILD_DIR)
TVISION_LIB = $(TVISION_BUILD_DIR)/libtvision.a
TVISION_CMAKE_FLAGS = \
	-DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_C_COMPILER=$(CC) \
	-DCMAKE_CXX_COMPILER=$(CXX) \
	-DCMAKE_CXX_STANDARD=20 \
	-DCMAKE_CXX_STANDARD_REQUIRED=ON \
	-DCMAKE_CXX_EXTENSIONS=ON \
	-DTV_BUILD_EXAMPLES=OFF \
	-DTV_BUILD_TESTS=OFF \
	-DTV_BUILD_AVSCOLOR=OFF \
	-DTV_OPTIMIZE_BUILD=OFF

# Linker paths and libraries
NCURSESW_LIB ?= $(shell if [ -e /lib/x86_64-linux-gnu/libncursesw.so.6 ]; then echo -l:libncursesw.so.6; else echo -lncursesw; fi)
GPM_LIB ?= $(shell if [ -e /lib/x86_64-linux-gnu/libgpm.so.2 ]; then echo -l:libgpm.so.2; else echo -lgpm; fi)
TINFO_LIB ?= $(shell if [ -e /lib/x86_64-linux-gnu/libtinfo.so.6 ]; then echo -l:libtinfo.so.6; else echo -ltinfo; fi)
LDFLAGS = $(PTHREAD_FLAGS) $(TVISION_LIB) $(NCURSESW_LIB) $(GPM_LIB) $(TINFO_LIB)

TARGET = mr
STAGE_PROFILE_PROBE_TARGET = misc/mr_stage_profile_probe
STAGE_PROFILE_PROBE_SOURCE = misc/mr_stage_profile_probe.cpp
STAGE_PROFILE_PROBE_OBJECT = misc/mr_stage_profile_probe.o
REGRESSION_PROBE_TARGET = regression/mr-regression-checks
REGRESSION_PROBE_SOURCE = regression/mr-regression-checks.cpp
REGRESSION_PROBE_OBJECT = regression/mr-regression-checks.o
MRMAC_V1_SUITE_SCRIPT = misc/run_mrmac_v1_suite.sh
ABOUT_QUOTES_GENERATOR = misc/generate_about_quotes.sh
ABOUT_QUOTES_GENERATED = app/MRAboutQuotes.generated.hpp

# C++ source files (Editor and VM)
CXX_SOURCES = \
	mr.cpp \
	app/MRAppState.cpp \
	app/MRCommandRouter.cpp \
	app/MRMenuFactory.cpp \
	app/MRVersion.cpp \
	app/TMREditorApp.cpp \
	dialogs/MRAboutDialog.cpp \
	dialogs/MRColorSetupDialog.cpp \
	dialogs/MRFileInformationDialog.cpp \
	dialogs/MRInstallationAndSetupDialog.cpp \
	dialogs/MRMacroFileDialog.cpp \
	dialogs/MREditSettingsDialog.cpp \
	dialogs/MREditSettingsDialogSupport.cpp \
	dialogs/MRSetupDialogCommon.cpp \
	dialogs/MRSetupDialogs.cpp \
	dialogs/MRUnsavedChangesDialog.cpp \
	dialogs/MRWindowListDialog.cpp \
	mrmac/MRMacroRunner.cpp \
	app/commands/MRWindowCommands.cpp \
	config/MRDialogPaths.cpp \
	app/commands/MRFileCommands.cpp \
	app/commands/MRExternalCommand.cpp \
	coprocessor/MRPerformance.cpp \
	coprocessor/MRCoprocessorDispatch.cpp \
	mrmac/mrvm.cpp \
	ui/TMRFrame.cpp \
	ui/TMRMenuBar.cpp \
	ui/MRPalette.cpp \
	ui/MRWindowSupport.cpp \
	ui/TMRSyntax.cpp \
	coprocessor/MRCoprocessor.cpp \
	piecetable/MRTextDocument.cpp

CXX_OBJECTS = $(CXX_SOURCES:.cpp=.o)
CORE_CXX_OBJECTS = $(filter-out mr.o,$(CXX_OBJECTS))

# C source files (In-Memory Macro Compiler)
C_SOURCES = \
	mrmac/mrmac.c \
	mrmac/lex.yy.c \
	mrmac/parser.tab.c

C_OBJECTS = $(C_SOURCES:.c=.o)

.PHONY: all clean clean-tvision rebuild-tvision \
	tvision-upstream-init tvision-upstream-fetch tvision-subtree-pull tvision-apply-patches \
	tvision-sync-safe tvision-status \
	stage-profile-probe regression-probe regression-check regression-check-core regression-check-full mrmac-v1-check \
	compile-commands lint-file context-tar tar-archives

all: $(TARGET)
stage-profile-probe: $(STAGE_PROFILE_PROBE_TARGET)
regression-probe: $(REGRESSION_PROBE_TARGET)
regression-check: $(REGRESSION_PROBE_TARGET)
	./$(REGRESSION_PROBE_TARGET) --full
regression-check-core: $(REGRESSION_PROBE_TARGET)
	./$(REGRESSION_PROBE_TARGET)
regression-check-full: $(REGRESSION_PROBE_TARGET)
	./$(REGRESSION_PROBE_TARGET) --full
mrmac-v1-check: $(TARGET) $(STAGE_PROFILE_PROBE_TARGET) regression-probe
	$(MRMAC_V1_SUITE_SCRIPT)

CONTEXT_ARCHIVE ?= codebase-context.tar.bzip2
CONTEXT_GIT_INFO_NAME ?= CONTEXT_GIT_INFO.txt
CONTEXT_ARCHIVE_ITEMS = \
	.clang-format \
	.clang-tidy \
	.gitignore \
	mr.code-workspace \
	.vscode \
	Makefile \
	README.md \
	mr.cpp \
	mr.hlp \
	app \
	config \
	coprocessor \
	dialogs \
	documentation \
	misc \
	mrmac \
	patches \
	piecetable \
	regression \
	ui \
	tvision

context-tar tar-archives:
	@set -e; \
	rm -f $(CONTEXT_ARCHIVE); \
	tmpdir=$$(mktemp -d ./.context-archive.XXXXXX); \
	trap 'rm -rf "$$tmpdir"' EXIT INT TERM HUP; \
	git_info_file="$$tmpdir/$(CONTEXT_GIT_INFO_NAME)"; \
	{ \
		echo "MR / Multi-Edit Revisited Context Archive"; \
		echo; \
		if command -v $(GIT) >/dev/null 2>&1 && [ -d .git ]; then \
			echo "git branch: $$( $(GIT) rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown )"; \
			echo "git commit: $$( $(GIT) rev-parse HEAD 2>/dev/null || echo unknown )"; \
			echo "git describe: $$( $(GIT) describe --always --dirty --tags 2>/dev/null || echo unknown )"; \
			echo "git last commit: $$( $(GIT) log -1 --oneline 2>/dev/null || echo unknown )"; \
			echo; \
			echo "git status --short:"; \
			$(GIT) status --short 2>/dev/null || true; \
		else \
			echo "Git metadata unavailable in this working tree."; \
			echo "Expected source checkout with .git directory."; \
		fi; \
	} > "$$git_info_file"; \
	items=""; \
	for entry in $(CONTEXT_ARCHIVE_ITEMS); do \
		if [ -e "$$entry" ]; then \
			items="$$items $$entry"; \
		fi; \
	done; \
	if [ -z "$$items" ]; then \
		echo "No context archive inputs found." >&2; \
		exit 1; \
	fi; \
	tar -cjf $(CONTEXT_ARCHIVE) \
		--exclude-vcs \
		--exclude=.codex \
		--exclude=compile_commands.json \
		--exclude=mr \
		--exclude=misc \
		--exclude=mrmac/mrmac \
		--exclude=tvision/build \
		--exclude='*.o' \
		--exclude='*.a' \
		--exclude='*.so' \
		--exclude='*.tar' \
		--exclude='*.tar.gz' \
		--exclude='*.tar.bzip2' \
		$$items \
		-C "$$tmpdir" $(CONTEXT_GIT_INFO_NAME); \
	echo "Wrote $(CONTEXT_ARCHIVE)"
compile-commands:
	rm -f compile_commands.json
	@if command -v $(BEAR) >/dev/null 2>&1; then \
		$(BEAR) --output compile_commands.json -- make -B -j$(NPROC); \
	else \
		intercept-build --cdb compile_commands.json make -B -j$(NPROC); \
	fi
lint-file:
	@if [ ! -f compile_commands.json ]; then \
		echo "compile_commands.json fehlt. Erst 'make compile-commands' ausführen."; \
		exit 2; \
	fi
	$(CLANG_TIDY) -p . $(LINT_FILE)

# TVision: local subtree source + patch queue.
$(TVISION_LOCAL_PATCH_STAMP): $(TVISION_PATCHES) Makefile
	@set -e; \
	mkdir -p $(dir $(TVISION_LOCAL_PATCH_STAMP)); \
	if [ -n "$(strip $(TVISION_PATCHES))" ]; then \
		for p in $(TVISION_PATCHES); do \
			if $(PATCH) -d $(TVISION_SOURCE_DIR) -p1 -l -R --dry-run < "$$p" >/dev/null 2>&1; then \
				echo "Patch already applied $$p"; \
			elif $(PATCH) -d $(TVISION_SOURCE_DIR) -p1 -l --dry-run < "$$p" >/dev/null 2>&1; then \
				echo "Applying $$p"; \
				$(PATCH) -d $(TVISION_SOURCE_DIR) -p1 -l --forward < "$$p"; \
			else \
				echo "Unable to apply $$p cleanly to $(TVISION_SOURCE_DIR)." >&2; \
				exit 1; \
			fi; \
		done; \
	fi; \
	touch $(TVISION_LOCAL_PATCH_STAMP)

$(TVISION_SOURCE_DIR)/build/libtvision.a: $(TVISION_SOURCE_DIR)/CMakeLists.txt $(TVISION_SOURCE_DIR)/source/CMakeLists.txt $(TVISION_LOCAL_PATCH_STAMP)
	@mkdir -p $(TVISION_SOURCE_DIR)/build
	$(CMAKE) -S $(TVISION_SOURCE_DIR) -B $(TVISION_SOURCE_DIR)/build $(TVISION_CMAKE_FLAGS)
	$(CMAKE) --build $(TVISION_SOURCE_DIR)/build --target tvision -j$(NPROC)

tvision-upstream-init:
	@if ! $(GIT) remote | grep -qx 'tvision-upstream'; then \
		$(GIT) remote add tvision-upstream $(TVISION_UPSTREAM_URL); \
	fi

tvision-upstream-fetch: tvision-upstream-init
	$(GIT) fetch --prune tvision-upstream

tvision-subtree-pull: tvision-upstream-fetch
	$(GIT) subtree pull --prefix=tvision tvision-upstream $(TVISION_UPSTREAM_REF) --squash
	rm -f $(TVISION_LOCAL_PATCH_STAMP)

tvision-apply-patches: $(TVISION_LOCAL_PATCH_STAMP)

tvision-status:
	@echo "== TVision subtree status =="; \
	$(GIT) remote -v | grep tvision-upstream || true; \
	echo; \
	echo "Subtree HEAD:"; \
	$(GIT) log --oneline -n 1 -- tvision || true; \
	echo; \
	echo "Patch queue:"; \
	ls -1 $(TVISION_PATCH_DIR)/*.patch 2>/dev/null || echo "(none)"; \
	echo; \
	echo "Patch stamp:"; \
	if [ -f "$(TVISION_LOCAL_PATCH_STAMP)" ]; then echo "applied ($(TVISION_LOCAL_PATCH_STAMP))"; else echo "not applied"; fi

tvision-sync-safe:
	bash misc/tvision-sync-safe.sh

clean-tvision:
	rm -rf $(TVISION_SOURCE_DIR)/build $(TVISION_LOCAL_PATCH_STAMP)

rebuild-tvision: clean-tvision $(TVISION_LIB)

# 1. Flex and Bison generation
mrmac/parser.tab.c mrmac/parser.tab.h: mrmac/parser.y
	$(BISON) -d -o mrmac/parser.tab.c mrmac/parser.y

mrmac/lex.yy.c: mrmac/lexer.l mrmac/parser.tab.h
	$(FLEX) -o mrmac/lex.yy.c mrmac/lexer.l

$(ABOUT_QUOTES_GENERATED): README.md $(ABOUT_QUOTES_GENERATOR)
	bash $(ABOUT_QUOTES_GENERATOR) README.md $@

# 2. Dependencies for C compilation
mrmac/lex.yy.o: CFLAGS += -Wno-unused-function
mrmac/lex.yy.o: mrmac/lex.yy.c mrmac/parser.tab.h
mrmac/parser.tab.o: mrmac/parser.tab.c mrmac/parser.tab.h mrmac/mrmac.h
mrmac/mrmac.o: mrmac/mrmac.c mrmac/parser.tab.h mrmac/mrmac.h

# 3. Dependencies for C++ compilation
mr.o: mr.cpp mrmac/mrvm.hpp app/TMREditorApp.hpp ui/MRPalette.hpp
app/MRAppState.o: app/MRAppState.cpp app/MRAppState.hpp app/MRCommands.hpp app/commands/MRWindowCommands.hpp ui/TMREditWindow.hpp
app/MRCommandRouter.o: app/MRCommandRouter.cpp app/MRCommandRouter.hpp app/MRCommands.hpp dialogs/MRAboutDialog.hpp dialogs/MRFileInformationDialog.hpp dialogs/MRMacroFileDialog.hpp dialogs/MRSetupDialogs.hpp dialogs/MRWindowListDialog.hpp mrmac/mrvm.hpp app/commands/MRExternalCommand.hpp app/commands/MRFileCommands.hpp app/commands/MRWindowCommands.hpp ui/TMREditWindow.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
app/MRMenuFactory.o: app/MRMenuFactory.cpp app/MRMenuFactory.hpp app/MRCommands.hpp ui/TMRMenuBar.hpp
app/MRVersion.o: app/MRVersion.cpp app/MRVersion.hpp
app/TMREditorApp.o: app/TMREditorApp.cpp app/TMREditorApp.hpp app/MRAppState.hpp app/MRCommandRouter.hpp app/MRCommands.hpp app/MRMenuFactory.hpp coprocessor/MRCoprocessorDispatch.hpp coprocessor/MRPerformance.hpp app/commands/MRWindowCommands.hpp ui/TMRDeskTop.hpp ui/TMRStatusLine.hpp ui/MRPalette.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
dialogs/MRAboutDialog.o: dialogs/MRAboutDialog.cpp dialogs/MRAboutDialog.hpp app/MRVersion.hpp $(ABOUT_QUOTES_GENERATED)
dialogs/MRColorSetupDialog.o: dialogs/MRColorSetupDialog.cpp dialogs/MRSetupDialogs.hpp dialogs/MRSetupDialogCommon.hpp app/MRCommands.hpp
dialogs/MRFileInformationDialog.o: dialogs/MRFileInformationDialog.cpp dialogs/MRFileInformationDialog.hpp app/MRCommands.hpp coprocessor/MRPerformance.hpp ui/TMREditWindow.hpp ui/TMRFileEditor.hpp ui/TMRTextBuffer.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
dialogs/MRInstallationAndSetupDialog.o: dialogs/MRInstallationAndSetupDialog.cpp dialogs/MRSetupDialogs.hpp dialogs/MRSetupDialogCommon.hpp app/MRCommands.hpp
dialogs/MRMacroFileDialog.o: dialogs/MRMacroFileDialog.cpp dialogs/MRMacroFileDialog.hpp mrmac/MRMacroRunner.hpp
dialogs/MREditSettingsDialog.o: dialogs/MREditSettingsDialog.cpp dialogs/MREditSettingsDialogInternal.hpp dialogs/MRSetupDialogs.hpp dialogs/MRSetupDialogCommon.hpp
dialogs/MREditSettingsDialogSupport.o: dialogs/MREditSettingsDialogSupport.cpp dialogs/MREditSettingsDialogInternal.hpp dialogs/MRSetupDialogs.hpp config/MRDialogPaths.hpp app/TMREditorApp.hpp
dialogs/MRSetupDialogCommon.o: dialogs/MRSetupDialogCommon.cpp dialogs/MRSetupDialogCommon.hpp
dialogs/MRSetupDialogs.o: dialogs/MRSetupDialogs.cpp dialogs/MRSetupDialogs.hpp dialogs/MRSetupDialogCommon.hpp app/MRCommands.hpp app/TMREditorApp.hpp config/MRDialogPaths.hpp ui/MRWindowSupport.hpp
dialogs/MRWindowListDialog.o: dialogs/MRWindowListDialog.cpp dialogs/MRWindowListDialog.hpp app/commands/MRWindowCommands.hpp ui/TMREditWindow.hpp ui/MRWindowSupport.hpp
mrmac/MRMacroRunner.o: mrmac/MRMacroRunner.cpp mrmac/MRMacroRunner.hpp mrmac/mrmac.h mrmac/mrvm.hpp app/commands/MRWindowCommands.hpp ui/TMREditWindow.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
app/commands/MRWindowCommands.o: app/commands/MRWindowCommands.cpp app/commands/MRWindowCommands.hpp config/MRDialogPaths.hpp ui/TMREditWindow.hpp ui/MRWindowSupport.hpp
config/MRDialogPaths.o: config/MRDialogPaths.cpp config/MRDialogPaths.hpp
app/commands/MRFileCommands.o: app/commands/MRFileCommands.cpp app/commands/MRFileCommands.hpp config/MRDialogPaths.hpp coprocessor/MRPerformance.hpp app/commands/MRWindowCommands.hpp ui/TMREditWindow.hpp ui/MRWindowSupport.hpp
app/commands/MRExternalCommand.o: app/commands/MRExternalCommand.cpp app/commands/MRExternalCommand.hpp config/MRDialogPaths.hpp coprocessor/MRCoprocessor.hpp
coprocessor/MRPerformance.o: coprocessor/MRPerformance.cpp coprocessor/MRPerformance.hpp coprocessor/MRCoprocessor.hpp
coprocessor/MRCoprocessorDispatch.o: coprocessor/MRCoprocessorDispatch.cpp coprocessor/MRCoprocessorDispatch.hpp coprocessor/MRPerformance.hpp app/commands/MRWindowCommands.hpp ui/TMREditWindow.hpp ui/TMRIndicator.hpp ui/TMRFileEditor.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
mrmac/mrvm.o: mrmac/mrvm.cpp mrmac/mrvm.hpp mrmac/mrmac.h dialogs/MRWindowListDialog.hpp ui/MRWindowSupport.hpp ui/TMREditWindow.hpp ui/TMRTextBuffer.hpp ui/TMRFileEditor.hpp ui/TMRTextBufferModel.hpp ui/TMRSyntax.hpp piecetable/MRTextDocument.hpp
ui/MRPalette.o: ui/MRPalette.cpp ui/MRPalette.hpp
ui/MRWindowSupport.o: ui/MRWindowSupport.cpp ui/MRWindowSupport.hpp config/MRDialogPaths.hpp app/commands/MRWindowCommands.hpp ui/TMREditWindow.hpp
ui/TMRSyntax.o: ui/TMRSyntax.cpp ui/TMRSyntax.hpp
coprocessor/MRCoprocessor.o: coprocessor/MRCoprocessor.cpp coprocessor/MRCoprocessor.hpp piecetable/MRTextDocument.hpp
piecetable/MRTextDocument.o: piecetable/MRTextDocument.cpp piecetable/MRTextDocument.hpp

# 4. Linker call
$(TARGET): $(TVISION_LIB) $(CXX_OBJECTS) $(C_OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(STAGE_PROFILE_PROBE_TARGET): $(TVISION_LIB) $(CORE_CXX_OBJECTS) $(C_OBJECTS) $(STAGE_PROFILE_PROBE_OBJECT)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(REGRESSION_PROBE_TARGET): $(TVISION_LIB) $(CORE_CXX_OBJECTS) $(C_OBJECTS) $(REGRESSION_PROBE_OBJECT)
	$(CXX) -o $@ $^ $(LDFLAGS)

# C++ compilation
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# C compilation
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(CXX_OBJECTS) $(C_OBJECTS) $(TARGET) $(STAGE_PROFILE_PROBE_OBJECT) \
		$(STAGE_PROFILE_PROBE_TARGET) \
		$(REGRESSION_PROBE_OBJECT) $(REGRESSION_PROBE_TARGET) \
		misc/mr_keyin_probe.o misc/mr_tofrom_probe.o misc/mr_tofrom_dispatch_probe.o \
		misc/mr_staged_nav_probe misc/mr_staged_mark_page_probe \
		$(ABOUT_QUOTES_GENERATED) \
		mrmac/lex.yy.c mrmac/parser.tab.c mrmac/parser.tab.h
