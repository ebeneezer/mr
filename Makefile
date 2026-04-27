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
MAKEFLAGS += -j$(NPROC)
CLANG_TIDY ?= clang-tidy
BEAR ?= bear
LINT_FILE ?= mrmac/MRVM.cpp
MR_BUILD_EPOCH := $(shell date +%s)

TVISION_SOURCE_DIR = ./tvision
TVISION_PATCH_DIR ?= ./patches
TVISION_PATCHES := $(sort $(wildcard $(TVISION_PATCH_DIR)/*.patch))
TVISION_LOCAL_PATCH_STAMP ?= $(TVISION_SOURCE_DIR)/.mr-patches-applied
TVISION_UPSTREAM_URL ?= https://github.com/magiblot/tvision.git
TVISION_UPSTREAM_REF ?= master
TVISION_ACTIVE_SOURCE_DIR := $(TVISION_SOURCE_DIR)
TVISION_ACTIVE_BUILD_DIR := $(TVISION_SOURCE_DIR)/build

PCRE2_LIB ?= /usr/lib/libpcre2-8.so
PCRE2_HEADER ?= /usr/include/pcre2.h

# Include paths
INCLUDES = -I$(TVISION_ACTIVE_SOURCE_DIR)/include -I./mrmac -I./piecetable -I./ui -I./coprocessor -I./app -I./app/commands -I./dialogs -I./config -I./keymap

# Language/runtime configuration.
CXXSTD ?= gnu++20
PTHREAD_FLAGS ?= -pthread

# Debug flags: -g for symbols, -O0 disables optimizations
CXXFLAGS = -Wall -g -O0 -std=$(CXXSTD) $(PTHREAD_FLAGS) $(INCLUDES)
CFLAGS = -Wall -g -O0 $(INCLUDES)

TVISION_BUILD_DIR = $(TVISION_ACTIVE_BUILD_DIR)
TVISION_LIB = $(TVISION_BUILD_DIR)/libtvision.a
TVISION_TOOLCHAIN_STAMP = $(TVISION_BUILD_DIR)/.mr-toolchain
TVISION_C_COMPILER := $(shell command -v $(CC) 2>/dev/null || echo $(CC))
TVISION_CXX_COMPILER := $(shell command -v $(CXX) 2>/dev/null || echo $(CXX))
TVISION_CMAKE_FLAGS = \
	-DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_C_COMPILER=$(TVISION_C_COMPILER) \
	-DCMAKE_CXX_COMPILER=$(TVISION_CXX_COMPILER) \
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
LDFLAGS = $(PTHREAD_FLAGS) $(TVISION_LIB) $(PCRE2_LIB) $(NCURSESW_LIB) $(GPM_LIB) $(TINFO_LIB)

TARGET = mr
STAGE_PROFILE_PROBE_TARGET = regression/mr_stage_profile_probe
STAGE_PROFILE_PROBE_SOURCE = regression/mr_stage_profile_probe.cpp
STAGE_PROFILE_PROBE_OBJECT = regression/mr_stage_profile_probe.o
REGRESSION_PROBE_TARGET = regression/mr-regression-checks
REGRESSION_PROBE_SOURCE = regression/mr-regression-checks.cpp
REGRESSION_PROBE_OBJECT = regression/mr-regression-checks.o
MRMAC_V1_SUITE_SCRIPT = misc/run_mrmac_v1_suite.sh
ABOUT_QUOTES_GENERATOR = ./generate_about_quotes.sh
ABOUT_QUOTES_GENERATED = app/MRAboutQuotes.generated.hpp
HELP_MARKDOWN_GENERATOR = ./generate_help_markdown.sh
HELP_MARKDOWN_SOURCE = app/mrhelp.md
HELP_MARKDOWN_GENERATED = app/MRHelp.generated.hpp

# C++ source files (Editor and VM)
CXX_SOURCES = \
	app/utils/MRStringUtils.cpp \
	app/utils/MRFileIOUtils.cpp \
	mr.cpp \
	app/MRAppState.cpp \
	app/MRCommandRouter.cpp \
	app/MRMenuFactory.cpp \
	app/MRVersion.cpp \
	app/MREditorApp.cpp \
	keymap/MRKeymapActionCatalog.cpp \
	keymap/MRKeymapContext.cpp \
	keymap/MRKeymapProfile.cpp \
	keymap/MRKeymapResolver.cpp \
	keymap/MRKeymapToken.cpp \
	keymap/MRKeymapTrie.cpp \
	keymap/MRKeymapSequence.cpp \
	dialogs/MRAbout.cpp \
	dialogs/MRColorSetup.cpp \
	dialogs/MRDirtyGating.cpp \
	dialogs/MRFileInformation.cpp \
	dialogs/MRKeymapManager.cpp \
	dialogs/MRMacroFile.cpp \
	dialogs/MRFileExtensionProfiles.cpp \
	dialogs/MRFileExtensionEditorSettings.cpp \
	dialogs/MRFileExtensionProfilesSupport.cpp \
	dialogs/MRSetup.cpp \
	dialogs/MRWindowList.cpp \
	mrmac/MRMacroRunner.cpp \
	app/commands/MRWindowCommands.cpp \
	config/MRDialogPaths.cpp \
	config/MRSettingsLoader.cpp \
	app/commands/MRExternalCommand.cpp \
	coprocessor/MRPerformance.cpp \
	coprocessor/MRCoprocessorDispatch.cpp \
	mrmac/MRVM.cpp \
	ui/MRFrame.cpp \
	ui/MRColumnListView.cpp \
	ui/MRMenuBar.cpp \
	ui/MRMessageLineController.cpp \
	ui/MRWindowManager.cpp \
	ui/MRNumericSlider.cpp \
	ui/MRPalette.cpp \
	ui/MRWindowSupport.cpp \
	ui/MRSyntax.cpp \
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

.PHONY: all clean clean-tvision rebuild-tvision tvision-build \
	tvision-upstream-init tvision-upstream-fetch tvision-subtree-pull tvision-apply-patches \
	tvision-sync-safe tvision-status \
	pcre2-check \
	stage-profile-probe regression-probe regression-check regression-check-core regression-check-full mrmac-v1-check \
	FORCE \
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
	generate_about_quotes.sh \
	mr.cpp \
	mr.hlp \
	app \
	config \
	coprocessor \
	dialogs \
	documentation \
	mrmac \
	patches \
	piecetable \
	keymap \
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

tvision-build: $(TVISION_SOURCE_DIR)/CMakeLists.txt $(TVISION_SOURCE_DIR)/source/CMakeLists.txt $(TVISION_LOCAL_PATCH_STAMP)
	@mkdir -p $(TVISION_SOURCE_DIR)/build
	@if [ ! -f $(TVISION_SOURCE_DIR)/build/CMakeCache.txt ] || \
		[ $(TVISION_SOURCE_DIR)/CMakeLists.txt -nt $(TVISION_SOURCE_DIR)/build/CMakeCache.txt ] || \
		[ $(TVISION_SOURCE_DIR)/source/CMakeLists.txt -nt $(TVISION_SOURCE_DIR)/build/CMakeCache.txt ] || \
		[ ! -f $(TVISION_TOOLCHAIN_STAMP) ] || \
		[ "$$(cat $(TVISION_TOOLCHAIN_STAMP) 2>/dev/null)" != "$(TVISION_C_COMPILER)|$(TVISION_CXX_COMPILER)" ]; then \
		$(CMAKE) -S $(TVISION_SOURCE_DIR) -B $(TVISION_SOURCE_DIR)/build $(TVISION_CMAKE_FLAGS); \
		printf '%s\n' "$(TVISION_C_COMPILER)|$(TVISION_CXX_COMPILER)" > $(TVISION_TOOLCHAIN_STAMP); \
	fi
	$(CMAKE) --build $(TVISION_SOURCE_DIR)/build --target tvision -j$(NPROC)

$(TVISION_LIB): tvision-build
	@test -f $(TVISION_LIB)

pcre2-check:
	@test -f $(PCRE2_LIB)
	@test -f $(PCRE2_HEADER)
	@echo "Using system PCRE2: $(PCRE2_LIB) / $(PCRE2_HEADER)"

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
mrmac/parser.tab.c mrmac/parser.tab.h &: mrmac/parser.y
	$(BISON) -d -o mrmac/parser.tab.c mrmac/parser.y

mrmac/lex.yy.c: mrmac/lexer.l mrmac/parser.tab.h
	$(FLEX) -o mrmac/lex.yy.c mrmac/lexer.l

$(ABOUT_QUOTES_GENERATED): README.md $(ABOUT_QUOTES_GENERATOR)
	@mkdir -p $(dir $@)
	bash $(ABOUT_QUOTES_GENERATOR) README.md $@

$(HELP_MARKDOWN_GENERATED): $(HELP_MARKDOWN_SOURCE) $(HELP_MARKDOWN_GENERATOR)
	@mkdir -p $(dir $@)
	bash $(HELP_MARKDOWN_GENERATOR) $(HELP_MARKDOWN_SOURCE) $@

# 2. Dependencies for C compilation
mrmac/lex.yy.o: CFLAGS += -Wno-unused-function
mrmac/lex.yy.o: mrmac/lex.yy.c mrmac/parser.tab.h
mrmac/parser.tab.o: mrmac/parser.tab.c mrmac/parser.tab.h mrmac/mrmac.h
mrmac/mrmac.o: mrmac/mrmac.c mrmac/parser.tab.h mrmac/mrmac.h

# 3. Dependencies for C++ compilation
$(CXX_OBJECTS): | $(ABOUT_QUOTES_GENERATED) $(HELP_MARKDOWN_GENERATED)

mr.o: mr.cpp mrmac/MRVM.hpp app/MREditorApp.hpp ui/MRPalette.hpp $(HELP_MARKDOWN_GENERATED)
app/MRAppState.o: app/MRAppState.cpp app/MRAppState.hpp app/MRCommands.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp
app/MRCommandRouter.o: app/MRCommandRouter.cpp app/MRCommandRouter.hpp app/MRCommands.hpp dialogs/MRAbout.hpp dialogs/MRFileInformation.hpp dialogs/MRMacroFile.hpp dialogs/MRSetup.hpp dialogs/MRWindowList.hpp mrmac/MRVM.hpp app/commands/MRExternalCommand.hpp app/commands/MRFileCommands.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp ui/MRFileEditor.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
app/MRMenuFactory.o: app/MRMenuFactory.cpp app/MRMenuFactory.hpp app/MRCommands.hpp ui/MRMenuBar.hpp
app/MRVersion.o: app/MRVersion.cpp app/MRVersion.hpp
app/MRVersion.o: CXXFLAGS += -DMR_BUILD_EPOCH=$(MR_BUILD_EPOCH)
app/MRVersion.o: FORCE
app/MREditorApp.o: app/MREditorApp.cpp app/MREditorApp.hpp app/MRAppState.hpp app/MRCommandRouter.hpp app/MRCommands.hpp app/MRMenuFactory.hpp coprocessor/MRCoprocessorDispatch.hpp coprocessor/MRPerformance.hpp app/commands/MRWindowCommands.hpp config/MRDialogPaths.hpp config/MRSettingsLoader.hpp ui/MRDeskTop.hpp ui/MRStatusLine.hpp ui/MRPalette.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
dialogs/MRAbout.o: dialogs/MRAbout.cpp dialogs/MRAbout.hpp app/MRVersion.hpp $(ABOUT_QUOTES_GENERATED)
dialogs/MRDirtyGating.o: dialogs/MRDirtyGating.cpp dialogs/MRDirtyGating.hpp dialogs/MRSetupCommon.hpp
dialogs/MRColorSetup.o: dialogs/MRColorSetup.cpp dialogs/MRSetup.hpp dialogs/MRSetupCommon.hpp app/MRCommands.hpp
dialogs/MRFileInformation.o: dialogs/MRFileInformation.cpp dialogs/MRFileInformation.hpp app/MRCommands.hpp coprocessor/MRPerformance.hpp ui/MREditWindow.hpp ui/MRFileEditor.hpp ui/MRTextBuffer.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
dialogs/MRMacroFile.o: dialogs/MRMacroFile.cpp dialogs/MRMacroFile.hpp mrmac/MRMacroRunner.hpp
dialogs/MRFileExtensionEditorSettings.o: dialogs/MRFileExtensionEditorSettings.cpp dialogs/MRFileExtensionEditorSettingsInternal.hpp ui/MRNumericSlider.hpp dialogs/MRSetupCommon.hpp
dialogs/MRFileExtensionProfilesSupport.o: dialogs/MRFileExtensionProfilesSupport.cpp dialogs/MRFileExtensionProfilesSupport.hpp dialogs/MRFileExtensionEditorSettingsInternal.hpp dialogs/MRSetup.hpp config/MRDialogPaths.hpp app/MREditorApp.hpp
dialogs/MRSetup.o: dialogs/MRSetup.cpp dialogs/MRSetup.hpp dialogs/MRSetupCommon.hpp app/MRCommands.hpp app/MREditorApp.hpp config/MRDialogPaths.hpp ui/MRWindowSupport.hpp
dialogs/MRWindowList.o: dialogs/MRWindowList.cpp dialogs/MRWindowList.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp ui/MRWindowSupport.hpp
ui/MRNumericSlider.o: ui/MRNumericSlider.cpp ui/MRNumericSlider.hpp
mrmac/MRMacroRunner.o: mrmac/MRMacroRunner.cpp mrmac/MRMacroRunner.hpp mrmac/mrmac.h mrmac/MRVM.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
app/commands/MRWindowCommands.o: app/commands/MRWindowCommands.cpp app/commands/MRWindowCommands.hpp app/commands/MRFileCommands.hpp config/MRDialogPaths.hpp coprocessor/MRPerformance.hpp ui/MREditWindow.hpp ui/MRWindowSupport.hpp ui/MRMessageLineController.hpp
config/MRDialogPaths.o: config/MRDialogPaths.cpp config/MRDialogPaths.hpp
config/MRSettingsLoader.o: config/MRSettingsLoader.cpp config/MRSettingsLoader.hpp config/MRDialogPaths.hpp
app/commands/MRExternalCommand.o: app/commands/MRExternalCommand.cpp app/commands/MRExternalCommand.hpp config/MRDialogPaths.hpp coprocessor/MRCoprocessor.hpp
coprocessor/MRPerformance.o: coprocessor/MRPerformance.cpp coprocessor/MRPerformance.hpp coprocessor/MRCoprocessor.hpp
coprocessor/MRCoprocessorDispatch.o: coprocessor/MRCoprocessorDispatch.cpp coprocessor/MRCoprocessorDispatch.hpp coprocessor/MRPerformance.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp ui/MRIndicator.hpp ui/MRFileEditor.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
mrmac/MRVM.o: mrmac/MRVM.cpp mrmac/MRVM.hpp mrmac/mrmac.h dialogs/MRWindowList.hpp ui/MRWindowSupport.hpp ui/MREditWindow.hpp ui/MRTextBuffer.hpp ui/MRFileEditor.hpp ui/MRTextBufferModel.hpp ui/MRSyntax.hpp piecetable/MRTextDocument.hpp
ui/MRPalette.o: ui/MRPalette.cpp ui/MRPalette.hpp
ui/MRWindowSupport.o: ui/MRWindowSupport.cpp ui/MRWindowSupport.hpp config/MRDialogPaths.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp
ui/MRSyntax.o: ui/MRSyntax.cpp ui/MRSyntax.hpp
coprocessor/MRCoprocessor.o: coprocessor/MRCoprocessor.cpp coprocessor/MRCoprocessor.hpp piecetable/MRTextDocument.hpp
piecetable/MRTextDocument.o: piecetable/MRTextDocument.cpp piecetable/MRTextDocument.hpp

# 4. Linker call
$(TARGET): $(TVISION_LIB) $(CXX_OBJECTS) $(C_OBJECTS) | pcre2-check
	$(CXX) -o $@ $^ $(LDFLAGS) || { paplay --volume=25000 /usr/share/sounds/ocean/stereo/battery-caution.oga; exit 1; }
	killall mr 2> /dev/null || true
	paplay --volume=25000 /usr/share/sounds/freedesktop/stereo/service-login.oga || true

$(STAGE_PROFILE_PROBE_TARGET): $(TVISION_LIB) $(CORE_CXX_OBJECTS) $(C_OBJECTS) $(STAGE_PROFILE_PROBE_OBJECT) | pcre2-check
	$(CXX) -o $@ $^ $(LDFLAGS)

$(REGRESSION_PROBE_TARGET): $(TVISION_LIB) $(CORE_CXX_OBJECTS) $(C_OBJECTS) $(REGRESSION_PROBE_OBJECT) | pcre2-check
	$(CXX) -o $@ $^ $(LDFLAGS)


# C++ compilations
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@ || { paplay --volume=25000 /usr/share/sounds/ocean/stereo/battery-caution.oga; exit 1; }

# C compilation
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ || { paplay --volume=25000 /usr/share/sounds/ocean/stereo/battery-caution.oga; exit 1; }

clean:
	rm -f $(CXX_OBJECTS) $(C_OBJECTS) $(TARGET) $(STAGE_PROFILE_PROBE_OBJECT) \
		$(STAGE_PROFILE_PROBE_TARGET) \
		$(REGRESSION_PROBE_OBJECT) $(REGRESSION_PROBE_TARGET) \
		misc/mr_keyin_probe.o misc/mr_tofrom_probe.o misc/mr_tofrom_dispatch_probe.o \
		misc/mr_staged_nav_probe misc/mr_staged_mark_page_probe \
		$(ABOUT_QUOTES_GENERATED) \
		$(HELP_MARKDOWN_GENERATED) \
		mrmac/lex.yy.c mrmac/parser.tab.c mrmac/parser.tab.h
