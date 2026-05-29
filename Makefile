# Makefile for the main editor (mr) with integrated RAM compilation (Debug mode)
# Minimal, conservative fix:
# - keep the original flat MR build
# - build TVision in ./tvision/build
# - link explicitly against ./tvision/build/libtvision.a
# - no variant/object-dir refactor

PKG_CONFIG ?= pkg-config
CXX = g++
CC = gcc
CMAKE ?= cmake
GIT ?= git
PATCH ?= patch
USE_CCACHE ?= auto
CCACHE ?= ccache
CCACHE_AVAILABLE := $(shell command -v $(CCACHE) 2>/dev/null)
ifeq ($(USE_CCACHE),auto)
export MR_USE_CCACHE := $(if $(CCACHE_AVAILABLE),1,0)
else ifeq ($(USE_CCACHE),1)
export MR_USE_CCACHE := 1
else ifeq ($(USE_CCACHE),yes)
export MR_USE_CCACHE := 1
else
export MR_USE_CCACHE := 0
endif
export MR_CCACHE := $(CCACHE)
NPROC ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
MAKEFLAGS += -j$(NPROC)
CLANG_TIDY ?= clang-tidy
BEAR ?= bear
LINT_FILE ?= mrmac/MRVM.cpp
MR_BUILD_EPOCH := $(shell date +%s)
TMP_BASE_DIR ?= /dev/shm
TMP_COMPILER_LAUNCHER := $(abspath ./misc/mr-compiler-temp.sh)
TMP_RUN = $(TMP_COMPILER_LAUNCHER)

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

PDF_EXPORT_CFLAGS := $(shell $(PKG_CONFIG) --cflags pangocairo cairo 2>/dev/null)
PDF_EXPORT_LIBS := $(shell $(PKG_CONFIG) --libs pangocairo cairo 2>/dev/null)

# Include paths
INCLUDES = -I$(TVISION_ACTIVE_SOURCE_DIR)/include -I./mrmac -I./piecetable -I./ui -I./coprocessor -I./app -I./app/commands -I./dialogs -I./config -I./keymap $(PDF_EXPORT_CFLAGS)

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
TVISION_TOOLCHAIN_SIGNATURE := $(TVISION_C_COMPILER)|$(TVISION_CXX_COMPILER)|$(TMP_COMPILER_LAUNCHER)
TVISION_CMAKE_FLAGS = \
	-DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_C_COMPILER=$(TVISION_C_COMPILER) \
	-DCMAKE_CXX_COMPILER=$(TVISION_CXX_COMPILER) \
	-DCMAKE_C_COMPILER_LAUNCHER=$(TMP_COMPILER_LAUNCHER) \
	-DCMAKE_CXX_COMPILER_LAUNCHER=$(TMP_COMPILER_LAUNCHER) \
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
LDFLAGS = $(PTHREAD_FLAGS) $(TVISION_LIB) $(PCRE2_LIB) $(NCURSESW_LIB) $(GPM_LIB) $(TINFO_LIB) $(PDF_EXPORT_LIBS)

TARGET = mr
MRFOLDTRAINER_TARGET = trainers/foldtrainer/mrfoldtrainer
MRFOLDTRAINER_SOURCE = trainers/foldtrainer/mrfoldtrainer.cpp
MRFOLDTRAINER_OBJECT = trainers/foldtrainer/mrfoldtrainer.o
MRINDENTTRAINER_TARGET = trainers/indenttrainer/mrindenttrainer
MRINDENTTRAINER_SOURCE = trainers/indenttrainer/mrindenttrainer.cpp
MRINDENTTRAINER_OBJECT = trainers/indenttrainer/mrindenttrainer.o
STAGE_PROFILE_PROBE_TARGET = regression/mr_stage_profile_probe
STAGE_PROFILE_PROBE_SOURCE = regression/mr_stage_profile_probe.cpp
STAGE_PROFILE_PROBE_OBJECT = regression/mr_stage_profile_probe.o
REGRESSION_PROBE_TARGET = regression/mr-regression-checks
REGRESSION_PROBE_SOURCE = regression/mr-regression-checks.cpp
REGRESSION_PROBE_OBJECT = regression/mr-regression-checks.o
PHASE1_REPRO_PROBE_TARGET = misc/mr_phase1_repro_probe
PHASE1_REPRO_PROBE_SOURCE = misc/mr_phase1_repro_probe.cpp
PHASE1_REPRO_PROBE_OBJECT = misc/mr_phase1_repro_probe.o
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
	app/export/MRPdfTextExporter.cpp \
	mr.cpp \
	app/MRAppState.cpp \
	app/MRCommandRouter.cpp \
	app/router/MRCommandRouterSearch.cpp \
	app/router/MRCommandRouterSearchCore.cpp \
	app/router/MRCommandRouterSearchMultiFile.cpp \
	app/router/MRCommandRouterSearchMultiFileCollect.cpp \
	app/router/MRCommandRouterSearchMultiFileDialog.cpp \
	app/router/MRCommandRouterSearchMultiFileSession.cpp \
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
	dialogs/MRAcquireDialog.cpp \
	dialogs/MRPdfExportDialog.cpp \
	dialogs/MRCompilerProfiles.cpp \
	dialogs/extensions/MRFileExtensionProfiles.cpp \
	dialogs/extensions/MRFileExtensionEditorSettings.cpp \
	dialogs/extensions/MRFileExtensionProfilesSupport.cpp \
	dialogs/setup/MRSetupCommon.cpp \
	dialogs/setup/MRSetup.cpp \
	dialogs/setup/MRSetupSections.cpp \
	dialogs/MRWindowList.cpp \
	mrmac/MRMacroRunner.cpp \
	app/commands/MRWindowCommands.cpp \
	config/settings/MRSettingsRuntimeState.cpp \
	config/settings/MRSettingsHistory.cpp \
	config/settings/MRSettingsThemesProfiles.cpp \
	config/settings/MRSettingsCompilerProfiles.cpp \
	config/settings/MRSettingsEditSetup.cpp \
	config/settings/MRSettingsAssignments.cpp \
	config/settings/MRSettingsSnapshotIO.cpp \
	config/settings/MRSettingsSourceModel.cpp \
	config/settings/MRSettingsNormalize.cpp \
	config/settings/MRSettingsRuntime.cpp \
	config/settings/MRSettingsStorage.cpp \
	app/commands/MRExternalCommand.cpp \
	app/commands/MRLogViewer.cpp \
	derivedstate/MRDerivedStateBase.cpp \
	derivedstate/MRFoldingDerivedState.cpp \
	derivedstate/MRMiniMapDerivedState.cpp \
	derivedstate/MRSyntaxDerivedState.cpp \
	coprocessor/MRPerformance.cpp \
	coprocessor/MRCoprocessorDispatch.cpp \
	mrmac/MRVM.cpp \
	mrmac/vm/MRVMProfile.cpp \
	mrmac/vm/MRVMDeferredUi.cpp \
	mrmac/vm/MRVMEditor.cpp \
	mrmac/vm/MRVMHash.cpp \
	mrmac/vm/MRVMSnippet.cpp \
	mrmac/vm/MRVMValue.cpp \
	mrmac/vm/MRVMSettings.cpp \
	mrmac/vm/MRVMScreen.cpp \
	ui/MRFrame.cpp \
	ui/MRBentoBox.cpp \
	ui/MRColumnListView.cpp \
	ui/MRDropList.cpp \
	ui/MRFileEditor/MRFileEditor.cpp \
	ui/MRFileEditor/MRFileEditorIndent.cpp \
	ui/MRFileEditor/MRFileEditorWarmup.cpp \
	ui/MRFileEditor/MRFileEditorViewport.cpp \
	ui/MRFileEditor/MRMiniMap.cpp \
	ui/MRFileEditor/MRTextFormatting.cpp \
	ui/MRFileEditor/MRTextViewport.cpp \
	ui/MRMenuBar.cpp \
	ui/MRMessageLineController.cpp \
	ui/MRPerformancePanel.cpp \
	ui/MRSidekickEditor.cpp \
	ui/MRScopedHistoryUI.cpp \
	ui/MRWindowManager.cpp \
	ui/MRNumericSlider.cpp \
	ui/MRPalette.cpp \
	ui/MRWindowSupport.cpp \
	ui/MRSyntax.cpp \
	ui/syntax/MRSyntaxClassification.cpp \
	ui/syntax/MRSyntaxMetadata.cpp \
	coprocessor/MRCoprocessor.cpp \
	piecetable/MRTextDocument.cpp \
	piecetable/MRTextDocumentLineIndex.cpp

CXX_OBJECTS = $(CXX_SOURCES:.cpp=.o)
CORE_CXX_OBJECTS = $(filter-out mr.o,$(CXX_OBJECTS))

# C source files (In-Memory Macro Compiler)
C_SOURCES = \
	mrmac/mrmac.c

C_OBJECTS = $(C_SOURCES:.c=.o)

.PHONY: all clean clean-tvision rebuild-tvision tvision-build \
	tvision-upstream-init tvision-upstream-fetch tvision-subtree-pull tvision-apply-patches \
	tvision-sync-safe tvision-status \
	pcre2-check \
	mrfoldtrainer mrindenttrainer stage-profile-probe regression-probe regression-check regression-check-core regression-check-full mrmac-v1-check phase1-repro-probe \
	FORCE \
	compile-commands lint-file context-tar tar-archives

ifneq ($(filter clean,$(MAKECMDGOALS)),)
ifneq ($(filter all,$(MAKECMDGOALS)),)
.NOTPARALLEL:
all: clean
endif
endif

all: $(TARGET)
mrfoldtrainer: $(MRFOLDTRAINER_TARGET)
mrindenttrainer: $(MRINDENTTRAINER_TARGET)
stage-profile-probe: $(STAGE_PROFILE_PROBE_TARGET)
regression-probe: $(REGRESSION_PROBE_TARGET)
phase1-repro-probe: $(PHASE1_REPRO_PROBE_TARGET)
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

tvision-build: $(TVISION_SOURCE_DIR)/CMakeLists.txt $(TVISION_SOURCE_DIR)/source/CMakeLists.txt $(TVISION_LOCAL_PATCH_STAMP) $(TMP_COMPILER_LAUNCHER)
	@mkdir -p $(TVISION_SOURCE_DIR)/build
	@if [ ! -f $(TVISION_SOURCE_DIR)/build/CMakeCache.txt ] || \
		[ Makefile -nt $(TVISION_SOURCE_DIR)/build/CMakeCache.txt ] || \
		[ $(TVISION_SOURCE_DIR)/CMakeLists.txt -nt $(TVISION_SOURCE_DIR)/build/CMakeCache.txt ] || \
		[ $(TVISION_SOURCE_DIR)/source/CMakeLists.txt -nt $(TVISION_SOURCE_DIR)/build/CMakeCache.txt ] || \
		[ ! -f $(TVISION_TOOLCHAIN_STAMP) ] || \
		[ "$$(cat $(TVISION_TOOLCHAIN_STAMP) 2>/dev/null)" != "$(TVISION_TOOLCHAIN_SIGNATURE)" ]; then \
		$(CMAKE) -S $(TVISION_SOURCE_DIR) -B $(TVISION_SOURCE_DIR)/build $(TVISION_CMAKE_FLAGS); \
		printf '%s\n' "$(TVISION_TOOLCHAIN_SIGNATURE)" > $(TVISION_TOOLCHAIN_STAMP); \
	fi
	$(CMAKE) --build $(TVISION_SOURCE_DIR)/build --target tvision

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

tvision-sync-safe: tvision-subtree-pull
	$(MAKE) tvision-apply-patches

clean-tvision:
	rm -rf $(TVISION_SOURCE_DIR)/build $(TVISION_LOCAL_PATCH_STAMP)

rebuild-tvision: clean-tvision $(TVISION_LIB)

$(ABOUT_QUOTES_GENERATED): README.md $(ABOUT_QUOTES_GENERATOR)
	@mkdir -p $(dir $@)
	bash $(ABOUT_QUOTES_GENERATOR) README.md $@

$(HELP_MARKDOWN_GENERATED): $(HELP_MARKDOWN_SOURCE) $(HELP_MARKDOWN_GENERATOR)
	@mkdir -p $(dir $@)
	bash $(HELP_MARKDOWN_GENERATOR) $(HELP_MARKDOWN_SOURCE) $@

# 1. Dependencies for C compilation
mrmac/mrmac.o: mrmac/mrmac.c mrmac/mrmac.h

# 2. Dependencies for C++ compilation
$(CXX_OBJECTS): | $(ABOUT_QUOTES_GENERATED) $(HELP_MARKDOWN_GENERATED)

mr.o: mr.cpp mrmac/MRVM.hpp app/MREditorApp.hpp ui/MRPalette.hpp $(HELP_MARKDOWN_GENERATED)
app/MRAppState.o: app/MRAppState.cpp app/MRAppState.hpp app/MRCommands.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp ui/MRBentoBox.hpp
app/MRCommandRouter.o: app/MRCommandRouter.cpp app/MRCommandRouter.hpp app/MRCommands.hpp dialogs/MRAbout.hpp dialogs/MRFileInformation.hpp dialogs/MRMacroFile.hpp dialogs/setup/MRSetup.hpp dialogs/MRWindowList.hpp mrmac/MRVM.hpp app/commands/MRExternalCommand.hpp app/commands/MRFileCommands.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp ui/MRFileEditor/MRFileEditor.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
app/MRMenuFactory.o: app/MRMenuFactory.cpp app/MRMenuFactory.hpp app/MRCommands.hpp ui/MRMenuBar.hpp
app/MRVersion.o: app/MRVersion.cpp app/MRVersion.hpp
app/MRVersion.o: CXXFLAGS += -DMR_BUILD_EPOCH=$(MR_BUILD_EPOCH)
app/MRVersion.o: FORCE
app/MREditorApp.o: app/MREditorApp.cpp app/MREditorApp.hpp app/MRAppState.hpp app/MRCommandRouter.hpp app/MRCommands.hpp app/MRMenuFactory.hpp coprocessor/MRCoprocessorDispatch.hpp coprocessor/MRPerformance.hpp app/commands/MRWindowCommands.hpp config/settings/MRSettingsRuntime.hpp config/settings/MRSettingsStorage.hpp ui/MRDeskTop.hpp ui/MRStatusLine.hpp ui/MRPalette.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
dialogs/MRAbout.o: dialogs/MRAbout.cpp dialogs/MRAbout.hpp app/MRVersion.hpp $(ABOUT_QUOTES_GENERATED)
dialogs/MRDirtyGating.o: dialogs/MRDirtyGating.cpp dialogs/MRDirtyGating.hpp dialogs/setup/MRSetupCommon.hpp
dialogs/MRColorSetup.o: dialogs/MRColorSetup.cpp dialogs/setup/MRSetup.hpp dialogs/setup/MRSetupCommon.hpp app/MRCommands.hpp
dialogs/MRFileInformation.o: dialogs/MRFileInformation.cpp dialogs/MRFileInformation.hpp app/MRCommands.hpp coprocessor/MRPerformance.hpp ui/MREditWindow.hpp ui/MRFileEditor/MRFileEditor.hpp ui/MRTextBuffer.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
dialogs/MRMacroFile.o: dialogs/MRMacroFile.cpp dialogs/MRMacroFile.hpp mrmac/MRMacroRunner.hpp
dialogs/MRAcquireDialog.o: dialogs/MRAcquireDialog.cpp dialogs/MRAcquireDialog.hpp app/commands/MRFileCommands.hpp app/commands/MRWindowCommands.hpp config/settings/MRSettingsRuntime.hpp dialogs/setup/MRSetupCommon.hpp ui/MRDropList.hpp
dialogs/extensions/MRFileExtensionEditorSettings.o: dialogs/extensions/MRFileExtensionEditorSettings.cpp dialogs/extensions/MRFileExtensionEditorSettingsInternal.hpp ui/MRNumericSlider.hpp dialogs/setup/MRSetupCommon.hpp
dialogs/extensions/MRFileExtensionProfilesSupport.o: dialogs/extensions/MRFileExtensionProfilesSupport.cpp dialogs/extensions/MRFileExtensionProfilesSupport.hpp dialogs/extensions/MRFileExtensionEditorSettingsInternal.hpp dialogs/setup/MRSetup.hpp config/settings/MRSettingsRuntime.hpp app/MREditorApp.hpp
dialogs/setup/MRSetupCommon.o: dialogs/setup/MRSetupCommon.cpp dialogs/setup/MRSetupCommon.hpp config/settings/MRSettingsRuntime.hpp ui/MRScopedHistoryUI.hpp ui/MRWindowSupport.hpp ui/MRFrame.hpp keymap/MRKeymapContext.hpp
dialogs/setup/MRSetup.o: dialogs/setup/MRSetup.cpp dialogs/setup/MRSetup.hpp dialogs/setup/MRSetupCommon.hpp app/MRCommands.hpp app/MREditorApp.hpp config/settings/MRSettingsRuntime.hpp ui/MRScopedHistoryUI.hpp ui/MRWindowSupport.hpp
dialogs/MRWindowList.o: dialogs/MRWindowList.cpp dialogs/MRWindowList.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp ui/MRWindowSupport.hpp
ui/MRFileEditor/MRFileEditor.o: ui/MRFileEditor/MRFileEditor.cpp ui/MRFileEditor/MRFileEditor.hpp ui/MRFileEditor/MRMiniMap.hpp ui/MRFileEditor/MRTextFormatting.hpp ui/MRFileEditor/MRTextViewport.hpp
ui/MRFileEditor/MRFileEditorIndent.o: ui/MRFileEditor/MRFileEditorIndent.cpp ui/MRFileEditor/MRFileEditor.hpp ui/MRFileEditor/MRMiniMap.hpp ui/MRFileEditor/MRTextFormatting.hpp ui/MRFileEditor/MRTextViewport.hpp
ui/MRFileEditor/MRFileEditorWarmup.o: ui/MRFileEditor/MRFileEditorWarmup.cpp ui/MRFileEditor/MRFileEditor.hpp ui/MRFileEditor/MRMiniMap.hpp ui/MRFileEditor/MRTextFormatting.hpp ui/MRFileEditor/MRTextViewport.hpp
ui/MRFileEditor/MRFileEditorViewport.o: ui/MRFileEditor/MRFileEditorViewport.cpp ui/MRFileEditor/MRFileEditor.hpp ui/MRFileEditor/MRMiniMap.hpp ui/MRFileEditor/MRTextFormatting.hpp ui/MRFileEditor/MRTextViewport.hpp
ui/MRFileEditor/MRMiniMap.o: ui/MRFileEditor/MRMiniMap.cpp ui/MRFileEditor/MRMiniMap.hpp piecetable/MRTextDocument.hpp config/settings/MRSettingsRuntime.hpp coprocessor/MRCoprocessor.hpp
ui/MRFileEditor/MRTextFormatting.o: ui/MRFileEditor/MRTextFormatting.cpp ui/MRFileEditor/MRTextFormatting.hpp config/settings/MRSettingsRuntime.hpp
ui/MRFileEditor/MRTextViewport.o: ui/MRFileEditor/MRTextViewport.cpp ui/MRFileEditor/MRTextViewport.hpp config/settings/MRSettingsRuntime.hpp
ui/MRScopedHistoryUI.o: ui/MRScopedHistoryUI.cpp ui/MRScopedHistoryUI.hpp dialogs/MRAcquireDialog.hpp config/settings/MRSettingsRuntime.hpp ui/MRFrame.hpp
ui/MRNumericSlider.o: ui/MRNumericSlider.cpp ui/MRNumericSlider.hpp
mrmac/MRMacroRunner.o: mrmac/MRMacroRunner.cpp mrmac/MRMacroRunner.hpp mrmac/mrmac.h mrmac/MRVM.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
app/commands/MRWindowCommands.o: app/commands/MRWindowCommands.cpp app/commands/MRWindowCommands.hpp app/commands/MRFileCommands.hpp config/settings/MRSettingsRuntime.hpp coprocessor/MRPerformance.hpp ui/MREditWindow.hpp ui/MRWindowSupport.hpp ui/MRMessageLineController.hpp
config/settings/MRSettingsRuntimeState.o: config/settings/MRSettingsRuntimeState.cpp config/settings/MRSettingsRuntimeState.hpp config/settings/MRSettingsHistory.hpp config/settings/MRSettingsRuntime.hpp
config/settings/MRSettingsHistory.o: config/settings/MRSettingsHistory.cpp config/settings/MRSettingsHistory.hpp config/settings/MRSettingsRuntimeState.hpp config/settings/MRSettingsRuntime.hpp
config/settings/MRSettingsThemesProfiles.o: config/settings/MRSettingsThemesProfiles.cpp config/settings/MRSettingsThemesProfiles.hpp config/settings/MRSettingsRuntime.hpp config/settings/MRSettingsRuntimeState.hpp config/settings/MRSettingsHistory.hpp
config/settings/MRSettingsEditSetup.o: config/settings/MRSettingsEditSetup.cpp config/settings/MRSettingsEditSetup.hpp config/settings/MRSettingsRuntime.hpp config/settings/MRSettingsRuntimeState.hpp config/settings/MRSettingsThemesProfiles.hpp
config/settings/MRSettingsAssignments.o: config/settings/MRSettingsAssignments.cpp config/settings/MRSettingsAssignments.hpp config/settings/MRSettingsRuntime.hpp config/settings/MRSettingsRuntimeState.hpp config/settings/MRSettingsHistory.hpp config/settings/MRSettingsEditSetup.hpp config/settings/MRSettingsSnapshotIO.hpp config/settings/MRSettingsThemesProfiles.hpp
config/settings/MRSettingsSnapshotIO.o: config/settings/MRSettingsSnapshotIO.cpp config/settings/MRSettingsSnapshotIO.hpp config/settings/MRSettingsRuntime.hpp config/settings/MRSettingsRuntimeState.hpp config/settings/MRSettingsHistory.hpp config/settings/MRSettingsThemesProfiles.hpp config/settings/MRSettingsEditSetup.hpp config/settings/MRSettingsStorage.hpp
config/settings/MRSettingsSourceModel.o: config/settings/MRSettingsSourceModel.cpp config/settings/MRSettingsSourceModel.hpp config/settings/MRSettingsRuntime.hpp
config/settings/MRSettingsNormalize.o: config/settings/MRSettingsNormalize.cpp config/settings/MRSettingsNormalize.hpp config/settings/MRSettingsAssignments.hpp config/settings/MRSettingsStorage.hpp config/settings/MRSettingsSnapshotIO.hpp config/settings/MRSettingsSourceModel.hpp
config/settings/MRSettingsRuntime.o: config/settings/MRSettingsRuntime.cpp config/settings/MRSettingsRuntime.hpp config/settings/MRSettingsRuntimeState.hpp config/settings/MRSettingsHistory.hpp config/settings/MRSettingsStorage.hpp config/settings/MRSettingsThemesProfiles.hpp config/settings/MRSettingsEditSetup.hpp config/settings/MRSettingsSnapshotIO.hpp config/settings/MRSettingsSourceModel.hpp config/settings/MRSettingsAssignments.hpp
config/settings/MRSettingsStorage.o: config/settings/MRSettingsStorage.cpp config/settings/MRSettingsStorage.hpp config/settings/MRSettingsRuntime.hpp
app/commands/MRExternalCommand.o: app/commands/MRExternalCommand.cpp app/commands/MRExternalCommand.hpp config/settings/MRSettingsRuntime.hpp coprocessor/MRCoprocessor.hpp
coprocessor/MRPerformance.o: coprocessor/MRPerformance.cpp coprocessor/MRPerformance.hpp coprocessor/MRCoprocessor.hpp
coprocessor/MRCoprocessorDispatch.o: coprocessor/MRCoprocessorDispatch.cpp coprocessor/MRCoprocessorDispatch.hpp coprocessor/MRPerformance.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp ui/MRBentoBox.hpp ui/MRIndicator.hpp ui/MRFileEditor/MRFileEditor.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
mrmac/MRVM.o: mrmac/MRVM.cpp mrmac/MRVM.hpp mrmac/vm/MRVMDeferredUi.hpp mrmac/vm/MRVMEditor.hpp mrmac/vm/MRVMHash.hpp mrmac/vm/MRVMSettings.hpp mrmac/vm/MRVMScreen.hpp mrmac/mrmac.h dialogs/MRWindowList.hpp ui/MRWindowSupport.hpp ui/MREditWindow.hpp ui/MRTextBuffer.hpp ui/MRFileEditor/MRFileEditor.hpp ui/MRTextBufferModel.hpp ui/MRSyntax.hpp piecetable/MRTextDocument.hpp
mrmac/vm/MRVMProfile.o: mrmac/vm/MRVMProfile.cpp mrmac/vm/MRVMProfile.hpp mrmac/mrmac.h
mrmac/vm/MRVMDeferredUi.o: mrmac/vm/MRVMDeferredUi.cpp mrmac/vm/MRVMDeferredUi.hpp mrmac/MRVM.hpp
mrmac/vm/MRVMEditor.o: mrmac/vm/MRVMEditor.cpp mrmac/vm/MRVMEditor.hpp mrmac/vm/MRVMScreen.hpp mrmac/MRVM.hpp ui/MREditWindow.hpp ui/MRFileEditor/MRFileEditor.hpp
mrmac/vm/MRVMHash.o: mrmac/vm/MRVMHash.cpp mrmac/vm/MRVMHash.hpp mrmac/MRVM.hpp
mrmac/vm/MRVMSettings.o: mrmac/vm/MRVMSettings.cpp mrmac/vm/MRVMSettings.hpp mrmac/MRVM.hpp config/settings/MRSettingsRuntime.hpp config/settings/MRSettingsStorage.hpp keymap/MRKeymapProfile.hpp
mrmac/vm/MRVMScreen.o: mrmac/vm/MRVMScreen.cpp mrmac/vm/MRVMScreen.hpp mrmac/MRVM.hpp ui/MRMenuBar.hpp ui/MRMessageLineController.hpp ui/MRWindowSupport.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp
ui/MRPalette.o: ui/MRPalette.cpp ui/MRPalette.hpp
ui/MRBentoBox.o: ui/MRBentoBox.cpp ui/MRBentoBox.hpp ui/MREditWindow.hpp ui/MRSidekickEditor.hpp
ui/MRWindowSupport.o: ui/MRWindowSupport.cpp ui/MRWindowSupport.hpp config/settings/MRSettingsRuntime.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp
ui/MRSyntax.o: ui/MRSyntax.cpp ui/MRSyntax.hpp
ui/MRSidekickEditor.o: ui/MRSidekickEditor.cpp ui/MRSidekickEditor.hpp ui/MREditWindow.hpp ui/MRFileEditor/MRFileEditor.hpp
coprocessor/MRCoprocessor.o: coprocessor/MRCoprocessor.cpp coprocessor/MRCoprocessor.hpp piecetable/MRTextDocument.hpp
piecetable/MRTextDocument.o: piecetable/MRTextDocument.cpp piecetable/MRTextDocument.hpp piecetable/MRTextDocumentLineIndex.hpp
piecetable/MRTextDocumentLineIndex.o: piecetable/MRTextDocumentLineIndex.cpp piecetable/MRTextDocumentLineIndex.hpp piecetable/MRTextDocument.hpp
$(MRFOLDTRAINER_OBJECT): $(MRFOLDTRAINER_SOURCE) ui/MRFileEditor/MRFileEditor.hpp ui/MRSyntax.hpp
$(MRINDENTTRAINER_OBJECT): $(MRINDENTTRAINER_SOURCE) config/settings/MRSettingsRuntime.hpp ui/MRFileEditor/MRFileEditor.hpp ui/MRSyntax.hpp

# 3. Linker call
$(TARGET): $(TVISION_LIB) $(CXX_OBJECTS) $(C_OBJECTS) | pcre2-check
	$(TMP_RUN) $(CXX) -o $@ $^ $(LDFLAGS) || { paplay --volume=25000 /usr/share/sounds/ocean/stereo/battery-caution.oga; exit 1; }
	killall mr 2> /dev/null || true
	paplay --volume=25000 /usr/share/sounds/freedesktop/stereo/service-login.oga || true

$(MRFOLDTRAINER_TARGET): $(TVISION_LIB) $(CORE_CXX_OBJECTS) $(C_OBJECTS) $(MRFOLDTRAINER_OBJECT) | pcre2-check
	$(TMP_RUN) $(CXX) -o $@ $^ $(LDFLAGS)

$(MRINDENTTRAINER_TARGET): $(TVISION_LIB) $(CORE_CXX_OBJECTS) $(C_OBJECTS) $(MRINDENTTRAINER_OBJECT) | pcre2-check
	$(TMP_RUN) $(CXX) -o $@ $^ $(LDFLAGS)

$(STAGE_PROFILE_PROBE_TARGET): $(TVISION_LIB) $(CORE_CXX_OBJECTS) $(C_OBJECTS) $(STAGE_PROFILE_PROBE_OBJECT) | pcre2-check
	$(TMP_RUN) $(CXX) -o $@ $^ $(LDFLAGS)

$(REGRESSION_PROBE_TARGET): $(TVISION_LIB) $(CORE_CXX_OBJECTS) $(C_OBJECTS) $(REGRESSION_PROBE_OBJECT) | pcre2-check
	$(TMP_RUN) $(CXX) -o $@ $^ $(LDFLAGS)

$(PHASE1_REPRO_PROBE_TARGET): $(TVISION_LIB) $(CORE_CXX_OBJECTS) $(C_OBJECTS) $(PHASE1_REPRO_PROBE_OBJECT) | pcre2-check
	$(TMP_RUN) $(CXX) -o $@ $^ $(LDFLAGS)


# C++ compilations
%.o: %.cpp
	$(TMP_RUN) $(CXX) $(CXXFLAGS) -c $< -o $@ || { paplay --volume=25000 /usr/share/sounds/ocean/stereo/battery-caution.oga; exit 1; }

# C compilation
%.o: %.c
	$(TMP_RUN) $(CC) $(CFLAGS) -c $< -o $@ || { paplay --volume=25000 /usr/share/sounds/ocean/stereo/battery-caution.oga; exit 1; }

clean:
	rm -f $(CXX_OBJECTS) $(C_OBJECTS) $(TARGET) $(STAGE_PROFILE_PROBE_OBJECT) \
		$(MRFOLDTRAINER_OBJECT) $(MRFOLDTRAINER_TARGET) \
		$(MRINDENTTRAINER_OBJECT) $(MRINDENTTRAINER_TARGET) \
		$(STAGE_PROFILE_PROBE_TARGET) \
		$(REGRESSION_PROBE_OBJECT) \
		$(PHASE1_REPRO_PROBE_OBJECT) $(PHASE1_REPRO_PROBE_TARGET) \
		config/MRDialogPaths.o config/MRSettingsLoader.o \
		misc/mr_keyin_probe.o misc/mr_tofrom_probe.o misc/mr_tofrom_dispatch_probe.o \
		misc/mr_staged_nav_probe misc/mr_staged_mark_page_probe
