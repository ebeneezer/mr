# Makefile for the main editor (mr) with integrated RAM compilation (Debug mode)
# Minimal, conservative fix:
# - keep the original flat MR build
# - build TVision in ./tvision/build
# - link explicitly against ./tvision/build/libtvision.a
# - no variant/object-dir refactor

PKG_CONFIG ?= pkg-config
CXX = clang++
CC = clang
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
BOLT_WORKFLOW ?= ./misc/mr-bolt-workflow.sh
BOLT_BUILD_DIR ?= build/bolt
BOLT_CXX ?= clang++
BOLT_RUN_ARGS ?=
LLVM_BOLT ?= llvm-bolt
PERF2BOLT ?= perf2bolt
MERGE_FDATA ?= merge-fdata
PERF ?= perf
STRIP ?= strip
BSDTAR ?= bsdtar
INSTALL ?= install
SHA256SUM ?= sha256sum
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
INCLUDES = -I$(TVISION_ACTIVE_SOURCE_DIR)/include -I./mrmac -I./piecetable -I./ui -I./coprocessor -I./diff -I./app -I./app/commands -I./dialogs -I./config -I./keymap $(PDF_EXPORT_CFLAGS)

# Language/runtime configuration.
CXXSTD ?= gnu++20
PTHREAD_FLAGS ?= -pthread

# Optimized normal-build flags
CXXFLAGS = -Wall -O3 -std=$(CXXSTD) $(PTHREAD_FLAGS) $(INCLUDES)
CFLAGS = -Wall -O3 $(INCLUDES)

TVISION_BUILD_DIR = $(TVISION_ACTIVE_BUILD_DIR)
TVISION_LIB = $(TVISION_BUILD_DIR)/libtvision.a
TVISION_TOOLCHAIN_STAMP = $(TVISION_BUILD_DIR)/.mr-toolchain
TVISION_C_COMPILER := $(shell command -v $(CC) 2>/dev/null || echo $(CC))
TVISION_CXX_COMPILER := $(shell command -v $(CXX) 2>/dev/null || echo $(CXX))
TVISION_TOOLCHAIN_SIGNATURE := $(TVISION_C_COMPILER)|$(TVISION_CXX_COMPILER)|$(TMP_COMPILER_LAUNCHER)
TVISION_CMAKE_FLAGS = \
	-DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_C_FLAGS_DEBUG="-g -O3" \
	-DCMAKE_CXX_FLAGS_DEBUG="-g -O3" \
	-DCMAKE_C_COMPILER=$(TVISION_C_COMPILER) \
	-DCMAKE_CXX_COMPILER=$(TVISION_CXX_COMPILER) \
	-DCMAKE_C_COMPILER_LAUNCHER=$(TMP_COMPILER_LAUNCHER) \
	-DCMAKE_CXX_COMPILER_LAUNCHER=$(TMP_COMPILER_LAUNCHER) \
	-DCMAKE_CXX_STANDARD=20 \
	-DCMAKE_CXX_STANDARD_REQUIRED=ON \
	-DCMAKE_CXX_EXTENSIONS=ON \
	-DTV_BUILD_EXAMPLES=ON \
	-DTV_BUILD_TESTS=OFF \
	-DTV_BUILD_AVSCOLOR=OFF \
	-DTV_OPTIMIZE_BUILD=OFF

# Linker paths and libraries
NCURSESW_LIB ?= $(shell if [ -e /lib/x86_64-linux-gnu/libncursesw.so.6 ]; then echo -l:libncursesw.so.6; else echo -lncursesw; fi)
GPM_LIB ?= $(shell if [ -e /lib/x86_64-linux-gnu/libgpm.so.2 ]; then echo -l:libgpm.so.2; else echo -lgpm; fi)
TINFO_LIB ?= $(shell if [ -e /lib/x86_64-linux-gnu/libtinfo.so.6 ]; then echo -l:libtinfo.so.6; else echo -ltinfo; fi)
LDFLAGS = $(PTHREAD_FLAGS) $(TVISION_LIB) $(PCRE2_LIB) $(NCURSESW_LIB) $(GPM_LIB) $(TINFO_LIB) $(PDF_EXPORT_LIBS) -Wl,--strip-debug

TARGET = mr
MRFOLDTRAINER_TARGET = trainers/foldtrainer/mrfoldtrainer
MRFOLDTRAINER_SOURCE = trainers/foldtrainer/mrfoldtrainer.cpp
MRFOLDTRAINER_OBJECT = trainers/foldtrainer/mrfoldtrainer.o
MRINDENTTRAINER_TARGET = trainers/indenttrainer/mrindenttrainer
MRINDENTTRAINER_SOURCE = trainers/indenttrainer/mrindenttrainer.cpp
MRINDENTTRAINER_OBJECT = trainers/indenttrainer/mrindenttrainer.o
MROUTLINETRAINER_TARGET = trainers/outlinetrainer/mroutlinetrainer
MROUTLINETRAINER_SOURCE = trainers/outlinetrainer/mroutlinetrainer.cpp
MROUTLINETRAINER_OBJECT = trainers/outlinetrainer/mroutlinetrainer.o
STAGE_PROFILE_PROBE_TARGET = regression/mr_stage_profile_probe
STAGE_PROFILE_PROBE_SOURCE = regression/mr_stage_profile_probe.cpp
STAGE_PROFILE_PROBE_OBJECT = regression/mr_stage_profile_probe.o
REGRESSION_PROBE_TARGET = regression/mr-regression-checks
REGRESSION_PROBE_SOURCE = regression/mr-regression-checks.cpp
REGRESSION_PROBE_OBJECT = regression/mr-regression-checks.o
MACRO_DEBUGGER_CROSS_SECTION_PROBE_SOURCE = regression/MRMacroDebuggerCrossSectionProbe.cpp
MACRO_DEBUGGER_CROSS_SECTION_PROBE_OBJECT = regression/MRMacroDebuggerCrossSectionProbe.o
BASIC_LANGUAGE_PROBE_TARGET = regression/mr_basic_language_probe
BASIC_LANGUAGE_PROBE_SOURCE = regression/MRBasicLanguageProbe.cpp
BASIC_LANGUAGE_PROBE_OBJECT = regression/MRBasicLanguageProbe.o
PHASE1_REPRO_PROBE_TARGET = misc/mr_phase1_repro_probe
PHASE1_REPRO_PROBE_SOURCE = misc/mr_phase1_repro_probe.cpp
PHASE1_REPRO_PROBE_OBJECT = misc/mr_phase1_repro_probe.o
MR_WORKSPACE_SERVICE_CONTEXT_PROBE_TARGET = regression/mr_workspace_service_context_probe
MR_WORKSPACE_SERVICE_CONTEXT_PROBE_SOURCE = regression/mr_workspace_service_context_probe.cpp
MR_WORKSPACE_SERVICE_CONTEXT_PROBE_OBJECT = regression/mr_workspace_service_context_probe.o
MRMAC_V1_SUITE_SCRIPT = misc/run_mrmac_v1_suite.sh
ABOUT_QUOTES_GENERATOR = ./generate_about_quotes.sh
ABOUT_QUOTES_GENERATED = app/MRAboutQuotes.generated.hpp
HELP_MARKDOWN_GENERATOR = ./generate_help_markdown.sh
HELP_MARKDOWN_SOURCE = app/mrhelp.md
HELP_MARKDOWN_GENERATED = app/MRHelp.generated.hpp
HELP_HYPERTEXT_GENERATOR = ./generate_tvision_help.sh
HELP_HYPERTEXT_SOURCE = documentation/help/mrhelp.txt
HELP_HYPERTEXT_COMPILED = mr.hlp
HELP_TOPICS_GENERATED = app/MRHelpTopics.generated.hpp
TVHC_TOOL = $(TVISION_BUILD_DIR)/tvhc
TVHC_BUILD_STAMP = $(TVISION_BUILD_DIR)/.mr-tvhc
HELP_CONTEXT_OBJECTS = \
	app/MRCommandRouter.o \
	app/MREditorApp.o \
	app/MREditorAppMacroRecording.o \
	app/MRMenuFactory.o \
	app/commands/MRLogViewer.o \
	app/router/MRCommandRouterSearch.o \
	app/router/MRCommandRouterSearchMultiFileDialog.o \
	app/router/MRCommandRouterText.o \
	dialogs/MRAbout.o \
	dialogs/MRAcquireDialog.o \
	dialogs/MRColorSetup.o \
	dialogs/MRCompilerProfiles.o \
	dialogs/MRDirtyGating.o \
	dialogs/MRFileInformation.o \
	dialogs/MRKeymapManager.o \
	dialogs/MRMacroFile.o \
	dialogs/MRPdfExportDialog.o \
	dialogs/MRWindowList.o \
	dialogs/extensions/MRFileExtensionProfiles.o \
	dialogs/setup/MRSetupSections.o \
	mrmac/ui/conventional/MRVMMacroDialogRuntime.o \
	ui/MRHelpSystem.o \
	ui/MRMenuBar.o \
	ui/MRSidekickEditor.o \
	ui/MRWindowSupport.o \
	ui/widgets/MRScopedHistoryUI.o
MANUAL_DIRECTORY = documentation/manuals
PDFLATEX ?= pdflatex
MAKEINDEX ?= makeindex
RSVG_CONVERT ?= rsvg-convert
MANUAL_SVG_ASSETS = \
	$(MANUAL_DIRECTORY)/assets/mr-coprocessor-lanes.svg \
	$(MANUAL_DIRECTORY)/assets/mr-deferred-scan-windows.svg \
	$(MANUAL_DIRECTORY)/assets/mr-minimap-function-flow.svg \
	$(MANUAL_DIRECTORY)/assets/mr-piece-table-snapshots.svg \
	$(MANUAL_DIRECTORY)/assets/mr-settings-bootstrap-flow.svg \
	$(MANUAL_DIRECTORY)/assets/mrmac-exec-session-scheduler-routes.svg \
	$(MANUAL_DIRECTORY)/assets/mrmac-vm-execution-flow.svg
MANUAL_PDF_ASSETS = $(wildcard $(MANUAL_DIRECTORY)/assets/*.pdf) $(MANUAL_SVG_ASSETS:.svg=.pdf)
MANUAL_AUXILIARIES = \
	$(MANUAL_DIRECTORY)/mr-macro-reference.aux \
	$(MANUAL_DIRECTORY)/mr-macro-reference.cb \
	$(MANUAL_DIRECTORY)/mr-macro-reference.cb2 \
	$(MANUAL_DIRECTORY)/mr-macro-reference.idx \
	$(MANUAL_DIRECTORY)/mr-macro-reference.ilg \
	$(MANUAL_DIRECTORY)/mr-macro-reference.ind \
	$(MANUAL_DIRECTORY)/mr-macro-reference.log \
	$(MANUAL_DIRECTORY)/mr-macro-reference.out \
	$(MANUAL_DIRECTORY)/mr-macro-reference.toc \
	$(MANUAL_DIRECTORY)/mr-technical-manual.aux \
	$(MANUAL_DIRECTORY)/mr-technical-manual.log \
	$(MANUAL_DIRECTORY)/mr-technical-manual.out \
	$(MANUAL_DIRECTORY)/mr-technical-manual.toc \
	$(MANUAL_DIRECTORY)/mr-users-manual.aux \
	$(MANUAL_DIRECTORY)/mr-users-manual.idx \
	$(MANUAL_DIRECTORY)/mr-users-manual.ilg \
	$(MANUAL_DIRECTORY)/mr-users-manual.ind \
	$(MANUAL_DIRECTORY)/mr-users-manual.log \
	$(MANUAL_DIRECTORY)/mr-users-manual.out \
	$(MANUAL_DIRECTORY)/mr-users-manual.toc
MANUAL_BUILD_ARTIFACTS = $(MANUAL_AUXILIARIES) $(MANUAL_PDF_ASSETS)

MR_RELEASE_VERSION ?= 0.2.0
MR_RELEASE_EPOCH ?= $(MR_BUILD_EPOCH)
MR_RELEASE_PLATFORM ?= linux-x86_64
MR_RELEASE_OUTPUT_DIR ?= release
MR_RELEASE_INSTALLER = install.sh
MR_RELEASE_MANUALS = \
	$(MANUAL_DIRECTORY)/mr-users-manual.pdf \
	$(MANUAL_DIRECTORY)/mr-macro-reference.pdf \
	$(MANUAL_DIRECTORY)/mr-technical-manual.pdf
MR_RELEASE_MACROS = \
	mrmac/macros/MRComfortExtensions.mrmac \
	mrmac/macros/colorthemes/idocs.mrmac \
	mrmac/macros/compilersupport/MRCompilerMiddleware.mrmac \
	mrmac/macros/keymaps/MRDefaultKeymaps.mrmac \
	mrmac/macros/keymaps/emacs.mrmac \
	mrmac/macros/keymaps/nano.mrmac \
	mrmac/macros/keymaps/wordstar-extensions.mrmac \
	mrmac/macros/keymaps/wordstar.mrmac \
	mrmac/macros/utils/analogclocktick.mrmac \
	mrmac/macros/utils/desktoputils.mrmac

# C++ source files (Editor and VM)
CXX_SOURCES = \
	app/utils/MRStringUtils.cpp \
	app/utils/MRFileIOUtils.cpp \
	app/export/MRPdfTextExporter.cpp \
	app/MRPrivilegedFileBroker.cpp \
	mr.cpp \
	app/MRAppState.cpp \
	app/MRCommandRouter.cpp \
	app/router/MRCommandRouterGit.cpp \
	app/router/MRCommandRouterPdf.cpp \
	app/router/MRCommandRouterSearch.cpp \
	app/router/MRCommandRouterSearchState.cpp \
	app/router/MRCommandRouterSearchDialogs.cpp \
	app/router/MRCommandRouterSearchCore.cpp \
	app/router/MRCommandRouterSearchMultiFile.cpp \
	app/router/MRCommandRouterSearchMultiFileCollect.cpp \
	app/router/MRCommandRouterSearchMultiFileDialog.cpp \
	app/router/MRCommandRouterSearchMultiFileReplaceAllDialog.cpp \
	app/router/MRCommandRouterSearchMultiFileSession.cpp \
	app/router/MRCommandRouterText.cpp \
	app/MRFunctionKeyBindings.cpp \
	app/MRMenuFactory.cpp \
	app/MRMacroDebuggerCommandRoute.cpp \
	app/MRVersion.cpp \
	app/MRRuntimeScheduler.cpp \
	app/MRRuntimeTimerSource.cpp \
	app/MREditorApp.cpp \
	app/MREditorAppMacroRecording.cpp \
	app/MREditorAppPresentation.cpp \
	app/MREditorAppStartup.cpp \
	app/services/MRWorkspaceServiceContext.cpp \
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
	dialogs/extensions/MRFileExtensionProfileDrafts.cpp \
	dialogs/extensions/MRFileExtensionProfileSelection.cpp \
	dialogs/setup/MRSetupCommon.cpp \
	dialogs/setup/MRSetup.cpp \
	dialogs/setup/MRSetupSections.cpp \
	dialogs/MRWindowList.cpp \
	mrmac/ui/modeless/MRMacroModelessUi.cpp \
	mrmac/ui/modeless/MRMacroModelessCanvas.cpp \
	mrmac/ui/modeless/MRMacroModelessControls.cpp \
	mrmac/ui/modeless/MRMacroUiCollections.cpp \
	mrmac/MRMacroExecutionSession.cpp \
	mrmac/MRMacroRunner.cpp \
	app/commands/MRBentoWorkspaceCodec.cpp \
	app/commands/MRVirtualDesktopCommands.cpp \
	app/commands/MRWindowCommands.cpp \
	app/commands/MRWindowFileOperations.cpp \
	app/commands/MRWindowRuntimeState.cpp \
	app/commands/MRWorkspaceCommands.cpp \
	app/commands/MRWorkspaceRuntime.cpp \
	config/settings/MRSettingsAssignmentParsing.cpp \
	config/settings/MRSettingsAutoexec.cpp \
	config/settings/MRSettingsEditFormat.cpp \
	config/settings/MRSettingsEditProfiles.cpp \
	config/settings/MRSettingsKeymapProfiles.cpp \
	config/settings/MRSettingsPaths.cpp \
	config/settings/MRSettingsRuntimeState.cpp \
	config/settings/MRSettingsSnapshotAssignments.cpp \
	config/settings/MRSettingsStructuredStorage.cpp \
	config/settings/MRSettingsThemeFiles.cpp \
	config/settings/MRSettingsWindowColorParsing.cpp \
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
	derivedstate/MRFoldingDerivedState.cpp \
	derivedstate/MRMiniMapDerivedState.cpp \
	derivedstate/MRSyntaxDerivedState.cpp \
	diff/MRMyersDiff.cpp \
	outline/MROutlineFoldProducer.cpp \
	coprocessor/MRPerformance.cpp \
	coprocessor/MRCoprocessorDeferredPlayback.cpp \
	coprocessor/MRCoprocessorDispatch.cpp \
	coprocessor/MRCoprocessorBentoDispatch.cpp \
	mrmac/MRVM.cpp \
	mrmac/vm/MRVMDebugExecution.cpp \
	mrmac/vm/procedures/MRVMConfigurationProcedures.cpp \
	mrmac/vm/procedures/MRVMEditorProcedures.cpp \
	mrmac/vm/procedures/MRVMProcedureExecution.cpp \
	mrmac/vm/procedures/MRVMMacroProcedures.cpp \
	mrmac/vm/procedures/MRVMRuntimeProcedures.cpp \
	mrmac/MRVMDebugSession.cpp \
	mrmac/MRVMDebugValues.cpp \
	mrmac/vm/MRVMProfile.cpp \
	mrmac/ui/conventional/MRVMDeferredUi.cpp \
	mrmac/ui/conventional/MRVMEditorOperations.cpp \
	mrmac/ui/conventional/MRVMEditor.cpp \
	mrmac/ui/conventional/MRVMEditorState.cpp \
	mrmac/vm/MRVMBytecodeExecution.cpp \
	mrmac/vm/MRVMExecutionRuntime.cpp \
	mrmac/vm/MRVMDelayRuntime.cpp \
	mrmac/vm/MRVMExecSessions.cpp \
	mrmac/vm/MRVMHash.cpp \
	mrmac/vm/MRVMIntrinsics.cpp \
	mrmac/vm/MRVMKeymapRuntime.cpp \
	mrmac/ui/conventional/MRVMMacroDialogRuntime.cpp \
	mrmac/vm/MRVMMacroLoading.cpp \
	mrmac/vm/MRVMMacroRuntime.cpp \
	mrmac/ui/modeless/MRVMMacroModelessProcedures.cpp \
	mrmac/vm/MRVMMacroSpecRuntime.cpp \
	mrmac/ui/modeless/MRVMModelessUiStorage.cpp \
	mrmac/ui/modeless/MRVMModelessUiRuntime.cpp \
	mrmac/ui/modeless/MRVMModelessWindowRuntime.cpp \
	mrmac/vm/MRVMProcessRuntime.cpp \
	mrmac/vm/MRVMProcedureCatalog.cpp \
	mrmac/vm/MRVMRuntimeCatalog.cpp \
	mrmac/vm/MRVMRuntimeDebugger.cpp \
	mrmac/vm/MRVMRuntimeGlobals.cpp \
	mrmac/vm/MRVMRuntimeKv.cpp \
	mrmac/vm/MRVMRuntimeState.cpp \
	mrmac/ui/conventional/MRVMUiStateRuntime.cpp \
	mrmac/vm/MRVMSystemVariables.cpp \
	mrmac/vm/MRVMSystemVariableCatalog.cpp \
	mrmac/vm/MRVMValue.cpp \
	mrmac/vm/MRVMSettings.cpp \
	mrmac/ui/conventional/MRVMScreen.cpp \
	mrmac/ui/conventional/MRVMScreenState.cpp \
	ui/MRFrame.cpp \
	ui/MRBentoBox/MRBentoBox.cpp \
	ui/MRBentoBox/MRBentoBoxDerivedProjection.cpp \
	ui/MRBentoBox/MRBentoBoxDiagnostics.cpp \
	ui/MRBentoBox/MRBentoBoxDebugger.cpp \
	ui/MRBentoBox/MRBentoBoxDebuggerStatus.cpp \
	ui/MRBentoBox/MRBentoBoxFileCompare.cpp \
	ui/MRBentoBox/MRBentoBoxFileCompareProjection.cpp \
	ui/MRBentoBox/MRBentoBoxFileCompareView.cpp \
	ui/MRBentoBox/MRBentoBoxOutline.cpp \
	ui/MRBentoBox/MRBentoPaneFrameView.cpp \
	ui/MRBentoBox/MRBentoBoxPaneWindow.cpp \
	ui/MRBentoBox/MRBentoBoxEvents.cpp \
	ui/MRBentoBox/MRBentoBoxLayout.cpp \
	ui/MRBentoBox/MRBentoBoxChrome.cpp \
	ui/MRBentoBox/MRBentoBoxProjection.cpp \
	ui/MRBentoBox/MRBentoBoxRoleSupport.cpp \
	ui/MRBentoHexEditor/MRBentoHexEditor.cpp \
	ui/MRBentoHexEditor/MRHexInspector.cpp \
	ui/MRBentoHexEditor/MRHexStrings.cpp \
	ui/MRBentoHexEditor/MRHexUtf8.cpp \
	ui/MRBentoHexEditor/panes/MRHexPaneWindow.cpp \
	ui/MRBentoHexEditor/panes/MRHexPaneProjection.cpp \
	ui/MRBentoHexEditor/panes/MRHexPaneView.cpp \
	ui/widgets/MRColumnListView.cpp \
	ui/widgets/MRDropList.cpp \
	ui/MRFileEditor/MRFileEditor.cpp \
	ui/MRFileEditor/MRFileEditorClipboard.cpp \
	ui/MRFileEditor/MRFileEditorCommitSync.cpp \
	ui/MRFileEditor/MRFileEditorSave.cpp \
	ui/MRFileEditor/MRFileEditorMarkers.cpp \
	ui/MRFileEditor/MRFileEditorFoldWarmup.cpp \
	ui/MRFileEditor/MRFileEditorFoldCanonicalContext.cpp \
	ui/MRFileEditor/MRFileEditorFoldResultAdoption.cpp \
	ui/MRFileEditor/MRFileEditorFoldLevelOperation.cpp \
	ui/MRFileEditor/MRFileEditorFoldWorker.cpp \
	ui/MRFileEditor/MRFileEditorLineWarmup.cpp \
	ui/MRFileEditor/MRFileEditorNavigation.cpp \
	ui/MRFileEditor/MRFileEditorFormatting.cpp \
	ui/MRFileEditor/MRFileEditorTextEditing.cpp \
	ui/MRFileEditor/MRFileEditorEvents.cpp \
	ui/MRFileEditor/MRFileEditorViewState.cpp \
	ui/MRFileEditor/MRFEBlockOps.cpp \
	ui/MRFileEditor/MRFileEditorIndent.cpp \
	ui/MRFileEditor/MRFileEditorSyntaxWarmup.cpp \
	ui/MRFileEditor/MRFileEditorWarmup.cpp \
	ui/MRFileEditor/MRFileEditorWidthWarmup.cpp \
	ui/MRFileEditor/MRFileEditorViewport.cpp \
	ui/MRFileEditor/MRMiniMap.cpp \
	ui/MRFileEditor/MRMiniMapOverlay.cpp \
	ui/MRFileEditor/MRTextFormatting.cpp \
	ui/MRFileEditor/MRTextViewport.cpp \
	ui/MRMenuBar.cpp \
	ui/MRMenuBarDrawing.cpp \
	ui/MRMessageLineController.cpp \
	ui/MRPerformancePanel.cpp \
	ui/MRSidekickEditor.cpp \
	ui/MRSidekickEditing.cpp \
	ui/MRSidekickLayout.cpp \
	ui/MRHelpSystem.cpp \
	ui/widgets/MRScopedHistoryUI.cpp \
	ui/MRWindowLayout.cpp \
	ui/widgets/MRNumericSlider.cpp \
	ui/widgets/MRSpinner.cpp \
	ui/MRWindowSupport.cpp \
	ui/MRSyntax.cpp \
	ui/MRSyntaxBasic.cpp \
	ui/MRSyntaxBasicBlocks.cpp \
	ui/syntax/MRSyntaxClassification.cpp \
	ui/syntax/MRSyntaxXmlNucleus.cpp \
	ui/syntax/MRSyntaxMetadata.cpp \
	coprocessor/MRCoprocessor.cpp \
	coprocessor/MRCoprocessorWorkerLifecycle.cpp \
	coprocessor/MRCoprocessorTelemetry.cpp \
	piecetable/MRTextDocument.cpp \
	piecetable/MRTextDocumentDirectLineIndex.cpp \
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
	mrfoldtrainer mrindenttrainer mroutlinetrainer stage-profile-probe regression-probe regression-check regression-check-core regression-check-full basic-language-probe mrmac-v1-check phase1-repro-probe workspace-service-context-probe \
	bolt-seed bolt-seed-gcc bolt-seed-clang bolt-record bolt-optimize bolt-clean \
	compile-manuals \
	release-zip \
	FORCE \
	compile-commands lint-file context-tar tar-archives

ifneq ($(filter clean,$(MAKECMDGOALS)),)
ifneq ($(filter all,$(MAKECMDGOALS)),)
all: clean
$(CXX_OBJECTS) $(C_OBJECTS) tvision-build: clean
endif
endif

all: $(TARGET)
mrfoldtrainer: $(MRFOLDTRAINER_TARGET)
mrindenttrainer: $(MRINDENTTRAINER_TARGET)
mroutlinetrainer: $(MROUTLINETRAINER_TARGET)
stage-profile-probe: $(STAGE_PROFILE_PROBE_TARGET)
regression-probe: $(REGRESSION_PROBE_TARGET)
basic-language-probe: $(BASIC_LANGUAGE_PROBE_TARGET)
phase1-repro-probe: $(PHASE1_REPRO_PROBE_TARGET)
workspace-service-context-probe: $(MR_WORKSPACE_SERVICE_CONTEXT_PROBE_TARGET)
bolt-seed:
	@MR_BOLT_BUILD_DIR="$(BOLT_BUILD_DIR)" MR_BOLT_CXX="$(BOLT_CXX)" $(BOLT_WORKFLOW) seed
bolt-seed-gcc:
	@MR_BOLT_BUILD_DIR="$(BOLT_BUILD_DIR)" MR_BOLT_CXX="g++" $(BOLT_WORKFLOW) seed
bolt-seed-clang:
	@MR_BOLT_BUILD_DIR="$(BOLT_BUILD_DIR)" MR_BOLT_CXX="clang++" $(BOLT_WORKFLOW) seed
bolt-record:
	@MR_BOLT_BUILD_DIR="$(BOLT_BUILD_DIR)" MR_BOLT_PERF="$(PERF)" $(BOLT_WORKFLOW) record -- $(BOLT_RUN_ARGS)
bolt-optimize:
	@MR_BOLT_BUILD_DIR="$(BOLT_BUILD_DIR)" MR_BOLT_PERF2BOLT="$(PERF2BOLT)" MR_BOLT_MERGE_FDATA="$(MERGE_FDATA)" MR_BOLT_LLVM_BOLT="$(LLVM_BOLT)" MR_BOLT_STRIP="$(STRIP)" $(BOLT_WORKFLOW) optimize
bolt-clean:
	@MR_BOLT_BUILD_DIR="$(BOLT_BUILD_DIR)" $(BOLT_WORKFLOW) clean
regression-check: $(REGRESSION_PROBE_TARGET)
	./$(REGRESSION_PROBE_TARGET) --full
regression-check-core: $(REGRESSION_PROBE_TARGET)
	./$(REGRESSION_PROBE_TARGET)
regression-check-full: $(REGRESSION_PROBE_TARGET)
	./$(REGRESSION_PROBE_TARGET) --full
mrmac-v1-check: $(TARGET) $(STAGE_PROFILE_PROBE_TARGET) regression-probe
	$(MRMAC_V1_SUITE_SCRIPT)

compile-manuals:
	@set -e; \
	cleanup() { rm -f $(MANUAL_BUILD_ARTIFACTS); }; \
	trap cleanup EXIT INT TERM HUP; \
	for svg in $(MANUAL_SVG_ASSETS); do \
		pdf=$${svg%.svg}.pdf; \
		$(RSVG_CONVERT) -f pdf -o "$$pdf" "$$svg"; \
	done; \
	(cd $(MANUAL_DIRECTORY) && $(PDFLATEX) -halt-on-error -interaction=nonstopmode mr-macro-reference.tex); \
	(cd $(MANUAL_DIRECTORY) && $(MAKEINDEX) mr-macro-reference); \
	(cd $(MANUAL_DIRECTORY) && $(PDFLATEX) -halt-on-error -interaction=nonstopmode mr-macro-reference.tex); \
	(cd $(MANUAL_DIRECTORY) && $(PDFLATEX) -halt-on-error -interaction=nonstopmode mr-macro-reference.tex); \
	(cd $(MANUAL_DIRECTORY) && $(PDFLATEX) -halt-on-error -shell-escape -interaction=nonstopmode mr-technical-manual.tex); \
	(cd $(MANUAL_DIRECTORY) && $(PDFLATEX) -halt-on-error -shell-escape -interaction=nonstopmode mr-technical-manual.tex); \
	(cd $(MANUAL_DIRECTORY) && $(PDFLATEX) -halt-on-error -interaction=nonstopmode mr-users-manual.tex); \
	(cd $(MANUAL_DIRECTORY) && $(MAKEINDEX) mr-users-manual); \
	(cd $(MANUAL_DIRECTORY) && $(PDFLATEX) -halt-on-error -interaction=nonstopmode mr-users-manual.tex); \
	(cd $(MANUAL_DIRECTORY) && $(PDFLATEX) -halt-on-error -interaction=nonstopmode mr-users-manual.tex)

release-zip:
	@set -eu; \
	epoch="$(MR_RELEASE_EPOCH)"; \
	case "$$epoch" in \
		''|*[!0-9]*) echo "MR_RELEASE_EPOCH must contain decimal digits only." >&2; exit 2 ;; \
	esac; \
	command -v $(BSDTAR) >/dev/null 2>&1; \
	command -v $(INSTALL) >/dev/null 2>&1; \
	command -v $(SHA256SUM) >/dev/null 2>&1; \
	$(MAKE) clean all CXX=clang++ MR_BUILD_EPOCH="$$epoch"; \
	name="mr-$(MR_RELEASE_VERSION)-build-$$epoch-$(MR_RELEASE_PLATFORM)"; \
	mkdir -p "$(MR_RELEASE_OUTPUT_DIR)"; \
	output_directory=$$(cd "$(MR_RELEASE_OUTPUT_DIR)" && pwd); \
	staging_directory=$$(mktemp -d "$(MR_RELEASE_OUTPUT_DIR)/.mr-release.XXXXXX"); \
	trap 'rm -rf "$$staging_directory"' EXIT INT TERM HUP; \
	release_root="$$staging_directory/$$name"; \
	$(INSTALL) -d -m 0755 \
		"$$release_root/bin" \
		"$$release_root/share/doc/mr" \
		"$$release_root/share/licenses/mr" \
		"$$release_root/share/mr/macros"; \
	$(INSTALL) -m 0755 "$(TARGET)" "$$release_root/bin/mr"; \
	$(INSTALL) -m 0644 "$(HELP_HYPERTEXT_COMPILED)" "$$release_root/bin/mr.hlp"; \
	for manual_file in $(MR_RELEASE_MANUALS); do \
		$(INSTALL) -m 0644 "$$manual_file" "$$release_root/share/doc/mr/"; \
	done; \
	$(INSTALL) -m 0644 tvision/COPYRIGHT "$$release_root/share/licenses/mr/TVISION-COPYRIGHT"; \
	for macro_file in $(MR_RELEASE_MACROS); do \
		relative_path=$${macro_file#mrmac/macros/}; \
		$(INSTALL) -d -m 0755 "$$release_root/share/mr/macros/$$(dirname "$$relative_path")"; \
		$(INSTALL) -m 0644 "$$macro_file" "$$release_root/share/mr/macros/$$relative_path"; \
	done; \
	sed "s/@MR_RELEASE_EPOCH@/$$epoch/g" "$(MR_RELEASE_INSTALLER)" > "$$release_root/install.sh"; \
	chmod 0755 "$$release_root/install.sh"; \
	rm -f "$$output_directory/$$name.zip" "$$output_directory/$$name.zip.sha256"; \
	$(BSDTAR) -a -cf "$$output_directory/$$name.zip" -C "$$staging_directory" "$$name"; \
	(cd "$$output_directory" && $(SHA256SUM) "$$name.zip" > "$$name.zip.sha256"); \
	echo "Wrote $$output_directory/$$name.zip"; \
	echo "Wrote $$output_directory/$$name.zip.sha256"

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
	generate_tvision_help.sh \
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
	+$(CMAKE) --build $(TVISION_SOURCE_DIR)/build --target tvision

$(TVISION_LIB): tvision-build
	@test -f $(TVISION_LIB)

$(TVHC_BUILD_STAMP): Makefile $(TVISION_SOURCE_DIR)/CMakeLists.txt $(TVISION_SOURCE_DIR)/source/CMakeLists.txt $(TVISION_LOCAL_PATCH_STAMP) $(TMP_COMPILER_LAUNCHER) | $(TVISION_LIB)
	+$(CMAKE) --build $(TVISION_BUILD_DIR) --target tvhc
	@test -x $(TVHC_TOOL)
	@touch $@

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

$(HELP_HYPERTEXT_COMPILED) $(HELP_TOPICS_GENERATED) &: $(HELP_HYPERTEXT_SOURCE) $(HELP_HYPERTEXT_GENERATOR) $(TVHC_BUILD_STAMP)
	bash $(HELP_HYPERTEXT_GENERATOR) $(TVHC_TOOL) $(HELP_HYPERTEXT_SOURCE) $(HELP_HYPERTEXT_COMPILED) $(HELP_TOPICS_GENERATED)

# 1. Dependencies for C compilation
mrmac/mrmac.o: mrmac/mrmac.c mrmac/mrmac.h

# 2. Dependencies for C++ compilation
$(CXX_OBJECTS): | $(ABOUT_QUOTES_GENERATED) $(HELP_MARKDOWN_GENERATED)
$(HELP_CONTEXT_OBJECTS): $(HELP_TOPICS_GENERATED)

mr.o: mr.cpp mrmac/MRVM.hpp app/MREditorApp.hpp app/MRPrivilegedFileBroker.hpp $(HELP_MARKDOWN_GENERATED)
app/MRPrivilegedFileBroker.o: app/MRPrivilegedFileBroker.cpp app/MRPrivilegedFileBroker.hpp
app/MRAppState.o: app/MRAppState.cpp app/MRAppState.hpp app/MRCommands.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp ui/MRBentoBox/MRBentoBox.hpp
app/MRCommandRouter.o: app/MRCommandRouter.cpp app/MRCommandRouter.hpp app/MRCommands.hpp app/router/MRCommandRouterGit.hpp app/router/MRCommandRouterPdf.hpp app/router/MRCommandRouterText.hpp dialogs/MRAbout.hpp dialogs/MRFileInformation.hpp dialogs/MRMacroFile.hpp dialogs/setup/MRSetup.hpp dialogs/MRWindowList.hpp mrmac/MRVM.hpp mrmac/mrmac.h mrmac/vm/MRVMHash.hpp mrmac/vm/MRVMRuntimeKv.hpp app/commands/MRExternalCommand.hpp app/commands/MRFileCommands.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp ui/MRFileEditor/MRFileEditor.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
app/MRFunctionKeyBindings.o: app/MRFunctionKeyBindings.cpp app/MRFunctionKeyBindings.hpp app/MRCommandRouter.hpp app/MRCommands.hpp ui/MRBentoBox/MRBentoBox.hpp ui/MREditWindow.hpp ui/MRStatusLine.hpp ui/MRWindowSupport.hpp
app/router/MRCommandRouterGit.o: app/router/MRCommandRouterGit.cpp app/router/MRCommandRouterGit.hpp app/commands/MRExternalCommand.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
app/router/MRCommandRouterPdf.o: app/router/MRCommandRouterPdf.cpp app/router/MRCommandRouterPdf.hpp app/export/MRPdfTextExporter.hpp app/utils/MRStringUtils.hpp config/settings/MRSettingsRuntime.hpp config/settings/MRSettingsStorage.hpp dialogs/MRPdfExportDialog.hpp dialogs/setup/MRSetupCommon.hpp ui/MREditWindow.hpp ui/MRFileEditor/MRFileEditor.hpp ui/MRMessageLineController.hpp ui/MRWindowSupport.hpp
app/router/MRCommandRouterText.o: app/router/MRCommandRouterText.cpp app/router/MRCommandRouterText.hpp app/commands/MRWindowCommands.hpp app/utils/MRStringUtils.hpp config/settings/MRSettingsRuntime.hpp config/settings/MRSettingsStorage.hpp dialogs/setup/MRSetupCommon.hpp ui/MREditWindow.hpp ui/MRFileEditor/MRFileEditor.hpp ui/MRFrame.hpp ui/MRMessageLineController.hpp ui/MRWindowSupport.hpp app/MREditorApp.hpp
app/MRMenuFactory.o: app/MRMenuFactory.cpp app/MRMenuFactory.hpp app/MRCommands.hpp ui/MRMenuBar.hpp
app/MRVersion.o: app/MRVersion.cpp app/MRVersion.hpp
app/MRVersion.o: CXXFLAGS += -DMR_BUILD_EPOCH=$(MR_BUILD_EPOCH)
app/MRVersion.o: FORCE
app/MRRuntimeScheduler.o: app/MRRuntimeScheduler.cpp app/MRRuntimeScheduler.hpp mrmac/MRMacroExecutionSession.hpp mrmac/MRMacroRunner.hpp mrmac/MRVM.hpp ui/MRWindowSupport.hpp
app/MRRuntimeTimerSource.o: app/MRRuntimeTimerSource.cpp app/MRRuntimeTimerSource.hpp app/MRRuntimeScheduler.hpp
app/MREditorApp.o: app/MREditorApp.cpp app/MREditorApp.hpp app/MRCommandRouter.hpp app/MRCommands.hpp app/MRFunctionKeyBindings.hpp app/MRMacroDebuggerCommandRoute.hpp app/MRMenuFactory.hpp app/commands/MRFileCommands.hpp app/commands/MRWindowCommands.hpp config/settings/MRSettingsRuntime.hpp config/settings/MRSettingsStorage.hpp coprocessor/MRCoprocessor.hpp dialogs/setup/MRSetupCommon.hpp mrmac/MRMacroRunner.hpp mrmac/MRVM.hpp ui/MRBentoBox/MRBentoBox.hpp ui/MRDeskTop.hpp ui/MREditWindow.hpp ui/MRFrame.hpp ui/MRMenuBar.hpp ui/MRMessageLineController.hpp ui/MRPerformancePanel.hpp ui/MRSidekickEditor.hpp ui/MRStatusLine.hpp ui/MRWindowLayout.hpp ui/MRWindowSupport.hpp
app/MREditorAppMacroRecording.o: app/MREditorAppMacroRecording.cpp app/MREditorApp.hpp app/MRCommandRouter.hpp app/MRCommands.hpp app/MRFunctionKeyBindings.hpp app/MRMacroDebuggerCommandRoute.hpp app/MRMenuFactory.hpp app/commands/MRFileCommands.hpp app/commands/MRWindowCommands.hpp app/utils/MRFileIOUtils.hpp config/settings/MRSettingsRuntime.hpp config/settings/MRSettingsRuntimeState.hpp dialogs/MRDirtyGating.hpp dialogs/setup/MRSetupCommon.hpp mrmac/MRMacroRunner.hpp mrmac/MRVM.hpp ui/MRDeskTop.hpp ui/MREditWindow.hpp ui/MRFrame.hpp ui/MRHelpSystem.hpp ui/MRMessageLineController.hpp ui/MRSidekickEditor.hpp ui/MRStatusLine.hpp ui/MRWindowLayout.hpp ui/MRWindowSupport.hpp
app/MREditorAppPresentation.o: app/MREditorAppPresentation.cpp app/MREditorApp.hpp app/MRAppState.hpp app/MRCommandRouter.hpp app/MRFunctionKeyBindings.hpp app/MRMacroDebuggerCommandRoute.hpp app/MRMenuFactory.hpp app/MRRuntimeTimerSource.hpp app/commands/MRFileCommands.hpp app/commands/MRWindowCommands.hpp config/settings/MRSettingsRuntime.hpp coprocessor/MRCoprocessor.hpp coprocessor/MRCoprocessorDispatch.hpp dialogs/setup/MRSetupCommon.hpp mrmac/MRMacroRunner.hpp mrmac/MRVM.hpp ui/MRBentoBox/MRBentoBox.hpp ui/MRDeskTop.hpp ui/MREditWindow.hpp ui/MRFrame.hpp ui/MRMenuBar.hpp ui/MRMessageLineController.hpp ui/MRPerformancePanel.hpp ui/MRSidekickEditor.hpp ui/MRStatusLine.hpp ui/MRWindowLayout.hpp ui/MRWindowSupport.hpp
app/MREditorAppStartup.o: app/MREditorAppStartup.cpp app/MREditorApp.hpp app/MRAppState.hpp app/MRCommandRouter.hpp app/MRFunctionKeyBindings.hpp app/MRMenuFactory.hpp app/MRPrivilegedFileBroker.hpp app/MRRuntimeScheduler.hpp app/commands/MRFileCommands.hpp app/commands/MRWindowCommands.hpp app/utils/MRFileIOUtils.hpp config/settings/MRSettingsAssignments.hpp config/settings/MRSettingsRuntime.hpp config/settings/MRSettingsRuntimeState.hpp config/settings/MRSettingsStorage.hpp coprocessor/MRCoprocessor.hpp coprocessor/MRCoprocessorDispatch.hpp dialogs/MRDirtyGating.hpp dialogs/setup/MRSetupCommon.hpp mrmac/MRMacroRunner.hpp mrmac/MRVM.hpp ui/MRBentoHexEditor/MRBentoHexEditor.hpp ui/MRDeskTop.hpp ui/MREditWindow.hpp ui/MRFileEditor/MRFileEditor.hpp ui/MRFrame.hpp ui/MRMenuBar.hpp ui/MRMessageLineController.hpp ui/MRSidekickEditor.hpp ui/MRWindowLayout.hpp ui/MRWindowSupport.hpp
app/MRMacroDebuggerCommandRoute.o: app/MRMacroDebuggerCommandRoute.cpp app/MRMacroDebuggerCommandRoute.hpp app/MRCommands.hpp ui/MRBentoBox/MRBentoBox.hpp ui/MRWindowSupport.hpp
dialogs/MRAbout.o: dialogs/MRAbout.cpp dialogs/MRAbout.hpp app/MRVersion.hpp $(ABOUT_QUOTES_GENERATED)
dialogs/MRDirtyGating.o: dialogs/MRDirtyGating.cpp dialogs/MRDirtyGating.hpp dialogs/setup/MRSetupCommon.hpp
dialogs/MRColorSetup.o: dialogs/MRColorSetup.cpp dialogs/setup/MRSetup.hpp dialogs/setup/MRSetupCommon.hpp app/MRCommands.hpp
dialogs/MRFileInformation.o: dialogs/MRFileInformation.cpp dialogs/MRFileInformation.hpp app/MRCommands.hpp coprocessor/MRPerformance.hpp ui/MREditWindow.hpp ui/MRFileEditor/MRFileEditor.hpp ui/MRTextBuffer.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
dialogs/MRMacroFile.o: dialogs/MRMacroFile.cpp dialogs/MRMacroFile.hpp mrmac/MRMacroRunner.hpp
dialogs/MRAcquireDialog.o: dialogs/MRAcquireDialog.cpp dialogs/MRAcquireDialog.hpp app/commands/MRFileCommands.hpp app/commands/MRWindowCommands.hpp config/settings/MRSettingsRuntime.hpp dialogs/setup/MRSetupCommon.hpp ui/widgets/MRDropList.hpp
dialogs/extensions/MRFileExtensionEditorSettings.o: dialogs/extensions/MRFileExtensionEditorSettings.cpp dialogs/extensions/MRFileExtensionEditorSettingsInternal.hpp ui/widgets/MRNumericSlider.hpp dialogs/setup/MRSetupCommon.hpp
dialogs/extensions/MRFileExtensionProfileDrafts.o: dialogs/extensions/MRFileExtensionProfileDrafts.cpp dialogs/extensions/MRFileExtensionProfileDrafts.hpp dialogs/extensions/MRFileExtensionEditorSettingsInternal.hpp dialogs/setup/MRSetup.hpp config/settings/MRSettingsRuntime.hpp app/MREditorApp.hpp
dialogs/extensions/MRFileExtensionProfileSelection.o: dialogs/extensions/MRFileExtensionProfileSelection.cpp dialogs/extensions/MRFileExtensionProfileDrafts.hpp app/commands/MRWindowCommands.hpp app/utils/MRStringUtils.hpp ui/MREditWindow.hpp
dialogs/setup/MRSetupCommon.o: dialogs/setup/MRSetupCommon.cpp dialogs/setup/MRSetupCommon.hpp config/settings/MRSettingsRuntime.hpp ui/widgets/MRScopedHistoryUI.hpp ui/MRWindowSupport.hpp ui/MRFrame.hpp keymap/MRKeymapContext.hpp
dialogs/setup/MRSetup.o: dialogs/setup/MRSetup.cpp dialogs/setup/MRSetup.hpp dialogs/setup/MRSetupCommon.hpp app/MRCommands.hpp app/MREditorApp.hpp config/settings/MRSettingsRuntime.hpp ui/widgets/MRScopedHistoryUI.hpp ui/MRWindowSupport.hpp
dialogs/MRWindowList.o: dialogs/MRWindowList.cpp dialogs/MRWindowList.hpp app/commands/MRWindowCommands.hpp ui/MRDesktopWindow.hpp ui/MREditWindow.hpp ui/MRWindowSupport.hpp
ui/MRWindowLayout.o: ui/MRWindowLayout.cpp ui/MRWindowLayout.hpp ui/MRDesktopWindow.hpp ui/MREditWindow.hpp app/commands/MRWindowCommands.hpp
ui/MRFileEditor/MRFileEditor.o: ui/MRFileEditor/MRFileEditor.cpp ui/MRFileEditor/MRFileEditor.hpp ui/MRFileEditor/MRFEBlockOps.hpp ui/MRFileEditor/MRMiniMap.hpp ui/MRFileEditor/MRTextFormatting.hpp ui/MRFileEditor/MRTextViewport.hpp outline/MROutlineFoldProducer.hpp
ui/MRFileEditor/MRFileEditorClipboard.o: ui/MRFileEditor/MRFileEditorClipboard.cpp ui/MRFileEditor/MRFileEditor.hpp
ui/MRFileEditor/MRFileEditorSave.o: ui/MRFileEditor/MRFileEditorSave.cpp ui/MRFileEditor/MRFileEditor.hpp app/MRPrivilegedFileBroker.hpp config/settings/MRSettingsStorage.hpp
ui/MRFileEditor/MRFileEditorMarkers.o: ui/MRFileEditor/MRFileEditorMarkers.cpp ui/MRFileEditor/MRFileEditor.hpp
ui/MRFileEditor/MRFileEditorFoldWarmup.o: ui/MRFileEditor/MRFileEditorFoldWarmup.cpp ui/MRFileEditor/MRFoldWarmupPayload.hpp ui/MRFileEditor/MRFileEditor.hpp outline/MROutlineFoldProducer.hpp ui/MRSyntaxBasic.hpp
ui/MRFileEditor/MRFileEditorFoldCanonicalContext.o: ui/MRFileEditor/MRFileEditorFoldCanonicalContext.cpp ui/MRFileEditor/MRFoldWarmupPayload.hpp ui/MRFileEditor/MRFileEditor.hpp
ui/MRFileEditor/MRFileEditorFoldResultAdoption.o: ui/MRFileEditor/MRFileEditorFoldResultAdoption.cpp ui/MRFileEditor/MRFoldWarmupPayload.hpp ui/MRFileEditor/MRFileEditor.hpp
ui/MRFileEditor/MRFileEditorFoldLevelOperation.o: ui/MRFileEditor/MRFileEditorFoldLevelOperation.cpp ui/MRFileEditor/MRFileEditor.hpp
ui/MRFileEditor/MRFileEditorFoldWorker.o: ui/MRFileEditor/MRFileEditorFoldWorker.cpp ui/MRFileEditor/MRFileEditor.hpp
ui/MRFileEditor/MRFileEditorLineWarmup.o: ui/MRFileEditor/MRFileEditorLineWarmup.cpp ui/MRFileEditor/MRFileEditor.hpp coprocessor/MRCoprocessor.hpp piecetable/MRTextDocument.hpp
ui/MRFileEditor/MRFileEditorNavigation.o: ui/MRFileEditor/MRFileEditorNavigation.cpp ui/MRFileEditor/MRFileEditor.hpp
ui/MRFileEditor/MRFileEditorFormatting.o: ui/MRFileEditor/MRFileEditorFormatting.cpp ui/MRFileEditor/MRFileEditor.hpp config/settings/MRSettingsStorage.hpp
ui/MRFileEditor/MRFileEditorTextEditing.o: ui/MRFileEditor/MRFileEditorTextEditing.cpp ui/MRFileEditor/MRFileEditor.hpp
ui/MRFileEditor/MRFileEditorEvents.o: ui/MRFileEditor/MRFileEditorEvents.cpp ui/MRFileEditor/MRFileEditor.hpp ui/MREditWindow.hpp app/MREditorApp.hpp
ui/MRFileEditor/MRFileEditorViewState.o: ui/MRFileEditor/MRFileEditorViewState.cpp ui/MRFileEditor/MRFileEditor.hpp ui/MREditWindow.hpp
ui/MRFileEditor/MRFEBlockOps.o: ui/MRFileEditor/MRFEBlockOps.cpp ui/MRFileEditor/MRFEBlockOps.hpp ui/MRFileEditor/MRFileEditor.hpp
ui/MRFileEditor/MRFileEditorIndent.o: ui/MRFileEditor/MRFileEditorIndent.cpp ui/MRFileEditor/MRFileEditor.hpp ui/MRFileEditor/MRMiniMap.hpp ui/MRFileEditor/MRTextFormatting.hpp ui/MRFileEditor/MRTextViewport.hpp ui/MRSyntaxBasic.hpp
ui/MRFileEditor/MRFileEditorCommitSync.o: ui/MRFileEditor/MRFileEditorCommitSync.cpp ui/MRFileEditor/MRFileEditor.hpp ui/MRFileEditor/MRMiniMap.hpp app/MRCommands.hpp
ui/MRFileEditor/MRFileEditorSyntaxWarmup.o: ui/MRFileEditor/MRFileEditorSyntaxWarmup.cpp ui/MRFileEditor/MRFileEditor.hpp coprocessor/MRCoprocessor.hpp derivedstate/MRSyntaxDerivedState.hpp piecetable/MRTextDocument.hpp
ui/MRFileEditor/MRFileEditorWarmup.o: ui/MRFileEditor/MRFileEditorWarmup.cpp ui/MRFileEditor/MRFileEditor.hpp ui/MRFileEditor/MRMiniMap.hpp ui/MRFileEditor/MRTextFormatting.hpp ui/MRFileEditor/MRTextViewport.hpp
ui/MRFileEditor/MRFileEditorWidthWarmup.o: ui/MRFileEditor/MRFileEditorWidthWarmup.cpp ui/MRFileEditor/MRFileEditor.hpp coprocessor/MRCoprocessor.hpp piecetable/MRTextDocument.hpp config/settings/MRSettingsRuntime.hpp
ui/MRFileEditor/MRFileEditorViewport.o: ui/MRFileEditor/MRFileEditorViewport.cpp ui/MRFileEditor/MRFileEditor.hpp ui/MRFileEditor/MRMiniMap.hpp ui/MRFileEditor/MRTextFormatting.hpp ui/MRFileEditor/MRTextViewport.hpp
ui/MRFileEditor/MRMiniMap.o: ui/MRFileEditor/MRMiniMap.cpp ui/MRFileEditor/MRMiniMap.hpp piecetable/MRTextDocument.hpp config/settings/MRSettingsRuntime.hpp coprocessor/MRCoprocessor.hpp
ui/MRFileEditor/MRMiniMapOverlay.o: ui/MRFileEditor/MRMiniMapOverlay.cpp ui/MRFileEditor/MRMiniMap.hpp piecetable/MRTextDocument.hpp config/settings/MRSettingsRuntime.hpp coprocessor/MRCoprocessor.hpp
ui/MRFileEditor/MRTextFormatting.o: ui/MRFileEditor/MRTextFormatting.cpp ui/MRFileEditor/MRTextFormatting.hpp config/settings/MRSettingsRuntime.hpp
ui/MRFileEditor/MRTextViewport.o: ui/MRFileEditor/MRTextViewport.cpp ui/MRFileEditor/MRTextViewport.hpp config/settings/MRSettingsRuntime.hpp
ui/MRMenuBar.o: ui/MRMenuBar.cpp ui/MRMenuBar.hpp ui/MRMenuBarDrawingInternal.hpp ui/MRMessageLineController.hpp
ui/MRMenuBarDrawing.o: ui/MRMenuBarDrawing.cpp ui/MRMenuBar.hpp ui/MRMenuBarDrawingInternal.hpp ui/MRMessageLineController.hpp ui/widgets/MRNumericSlider.hpp
ui/MRMessageLineController.o: ui/MRMessageLineController.cpp ui/MRMessageLineController.hpp ui/MRMenuBar.hpp ui/MRStatusLine.hpp config/settings/MRSettingsRuntime.hpp mrmac/vm/MRVMRuntimeKv.hpp mrmac/vm/MRVMValue.hpp
ui/widgets/MRScopedHistoryUI.o: ui/widgets/MRScopedHistoryUI.cpp ui/widgets/MRScopedHistoryUI.hpp dialogs/MRAcquireDialog.hpp config/settings/MRSettingsRuntime.hpp ui/MRFrame.hpp ui/widgets/MRDropList.hpp
ui/widgets/MRNumericSlider.o: ui/widgets/MRNumericSlider.cpp ui/widgets/MRNumericSlider.hpp config/settings/MRSettingsRuntime.hpp
ui/widgets/MRSpinner.o: ui/widgets/MRSpinner.cpp ui/widgets/MRSpinner.hpp config/settings/MRSettingsRuntime.hpp
mrmac/ui/modeless/MRMacroModelessUi.o: mrmac/ui/modeless/MRMacroModelessUi.cpp mrmac/ui/modeless/MRMacroModelessUi.hpp mrmac/ui/modeless/MRMacroModelessCanvas.hpp mrmac/MRVM.hpp mrmac/ui/modeless/MRVMModelessUiRuntime.hpp dialogs/MRWindowList.hpp ui/MRDesktopWindow.hpp
mrmac/ui/modeless/MRMacroModelessCanvas.o: mrmac/ui/modeless/MRMacroModelessCanvas.cpp mrmac/ui/modeless/MRMacroModelessCanvas.hpp mrmac/ui/modeless/MRMacroModelessUi.hpp mrmac/MRVM.hpp
mrmac/MRMacroExecutionSession.o: mrmac/MRMacroExecutionSession.cpp mrmac/MRMacroExecutionSession.hpp ui/MRWindowSupport.hpp
mrmac/MRMacroRunner.o: mrmac/MRMacroRunner.cpp mrmac/MRMacroRunner.hpp mrmac/MRMacroExecutionSession.hpp mrmac/mrmac.h mrmac/MRVM.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp
app/commands/MRBentoWorkspaceCodec.o: app/commands/MRBentoWorkspaceCodec.cpp app/commands/MRBentoWorkspaceCodec.hpp ui/MRBentoBox/MRBentoBox.hpp ui/MRBentoBox/MRBentoBoxRoleSupport.hpp
app/commands/MRWindowCommands.o: app/commands/MRWindowCommands.cpp app/commands/MRWindowCommands.hpp app/commands/MRBentoWorkspaceCodec.hpp app/commands/MRFileCommands.hpp config/settings/MRSettingsRuntime.hpp coprocessor/MRPerformance.hpp ui/MRDesktopWindow.hpp ui/MREditWindow.hpp ui/MRWindowSupport.hpp ui/MRMessageLineController.hpp
config/settings/MRSettingsRuntimeState.o: config/settings/MRSettingsRuntimeState.cpp config/settings/MRSettingsRuntimeState.hpp config/settings/MRSettingsHistory.hpp config/settings/MRSettingsRuntime.hpp
config/settings/MRSettingsStructuredStorage.o: config/settings/MRSettingsStructuredStorage.cpp config/settings/MRSettingsRuntimeState.hpp config/settings/MRSettingsEditSetup.hpp config/settings/MRSettingsRuntime.hpp keymap/MRKeymapProfile.hpp
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
coprocessor/MRCoprocessorDispatch.o: coprocessor/MRCoprocessorDispatch.cpp coprocessor/MRCoprocessorDispatch.hpp coprocessor/MRCoprocessorBentoDispatch.hpp coprocessor/MRPerformance.hpp app/commands/MRWindowCommands.hpp app/router/MRCommandRouterGit.hpp ui/MREditWindow.hpp ui/MRBentoBox/MRBentoBox.hpp ui/MRBentoHexEditor/panes/MRHexPaneWindow.hpp ui/MRIndicator.hpp ui/MRFileEditor/MRFileEditor.hpp ui/MRWindowSupport.hpp coprocessor/MRCoprocessor.hpp mrmac/MRMacroExecutionSession.hpp
coprocessor/MRCoprocessorBentoDispatch.o: coprocessor/MRCoprocessorBentoDispatch.cpp coprocessor/MRCoprocessorBentoDispatch.hpp coprocessor/MRCoprocessor.hpp coprocessor/MRPerformance.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp ui/MRBentoBox/MRBentoBox.hpp ui/MRWindowSupport.hpp
mrmac/MRVM.o: mrmac/MRVM.cpp mrmac/MRVM.hpp mrmac/MRVMDebugSession.hpp mrmac/vm/MRVMExecSessions.hpp mrmac/ui/conventional/MRVMDeferredUi.hpp mrmac/ui/conventional/MRVMEditor.hpp mrmac/vm/MRVMHash.hpp mrmac/ui/conventional/MRVMMacroDialogRuntime.hpp mrmac/ui/modeless/MRVMMacroModelessProcedures.hpp mrmac/vm/MRVMMacroSpecRuntime.hpp mrmac/ui/modeless/MRVMModelessUiRuntime.hpp mrmac/vm/MRVMRuntimeCatalog.hpp mrmac/vm/MRVMRuntimeGlobals.hpp mrmac/vm/MRVMRuntimeKv.hpp mrmac/vm/MRVMRuntimeState.hpp mrmac/vm/MRVMSettings.hpp mrmac/ui/conventional/MRVMScreen.hpp mrmac/mrmac.h dialogs/MRWindowList.hpp ui/MRWindowSupport.hpp ui/MREditWindow.hpp ui/MRTextBuffer.hpp ui/MRFileEditor/MRFileEditor.hpp ui/MRTextBufferModel.hpp ui/MRSyntax.hpp piecetable/MRTextDocument.hpp
mrmac/vm/MRVMDebugExecution.o: mrmac/vm/MRVMDebugExecution.cpp mrmac/MRVM.hpp mrmac/MRVMDebugSession.hpp mrmac/vm/MRVMDebugExecution.hpp mrmac/vm/MRVMExecSessions.hpp mrmac/vm/MRVMMacroSpecRuntime.hpp mrmac/vm/MRVMRuntimeCatalog.hpp mrmac/vm/MRVMRuntimeDebugger.hpp mrmac/vm/MRVMRuntimeInternal.hpp mrmac/vm/MRVMRuntimeKv.hpp mrmac/vm/MRVMRuntimeState.hpp mrmac/vm/MRVMValue.hpp mrmac/mrmac.h
mrmac/vm/MRVMRuntimeState.o: mrmac/vm/MRVMRuntimeState.cpp mrmac/vm/MRVMRuntimeState.hpp mrmac/vm/MRVMRuntimeKv.hpp mrmac/vm/MRVMValue.hpp mrmac/mrmac.h
mrmac/vm/procedures/MRVMConfigurationProcedures.o: mrmac/vm/procedures/MRVMConfigurationProcedures.cpp mrmac/MRVM.hpp mrmac/vm/MRVMProcedureExecution.hpp mrmac/vm/MRVMExecSessions.hpp mrmac/ui/conventional/MRVMDeferredUi.hpp mrmac/ui/conventional/MRVMEditor.hpp mrmac/ui/conventional/MRVMMacroDialogRuntime.hpp mrmac/ui/modeless/MRVMMacroModelessProcedures.hpp mrmac/ui/modeless/MRVMModelessUiRuntime.hpp mrmac/vm/MRVMProcedureCatalog.hpp mrmac/vm/MRVMRuntimeInternal.hpp mrmac/vm/MRVMSettings.hpp mrmac/vm/MRVMValue.hpp mrmac/mrmac.h
mrmac/vm/procedures/MRVMEditorProcedures.o: mrmac/vm/procedures/MRVMEditorProcedures.cpp mrmac/MRVM.hpp mrmac/vm/MRVMProcedureExecution.hpp mrmac/ui/conventional/MRVMDeferredUi.hpp mrmac/ui/conventional/MRVMEditor.hpp mrmac/vm/MRVMProcedureCatalog.hpp mrmac/vm/MRVMRuntimeInternal.hpp mrmac/vm/MRVMValue.hpp mrmac/mrmac.h
mrmac/vm/procedures/MRVMProcedureExecution.o: mrmac/vm/procedures/MRVMProcedureExecution.cpp mrmac/MRVM.hpp mrmac/vm/MRVMBytecodeExecution.hpp mrmac/vm/MRVMProcedureCatalog.hpp mrmac/vm/MRVMProcedureExecution.hpp
mrmac/vm/procedures/MRVMMacroProcedures.o: mrmac/vm/procedures/MRVMMacroProcedures.cpp mrmac/MRVM.hpp mrmac/vm/MRVMBytecodeExecution.hpp mrmac/vm/MRVMDebugExecution.hpp mrmac/vm/MRVMProcedureExecution.hpp mrmac/vm/MRVMMacroSpecRuntime.hpp mrmac/vm/MRVMProcedureCatalog.hpp mrmac/vm/MRVMRuntimeInternal.hpp mrmac/vm/MRVMRuntimeState.hpp mrmac/vm/MRVMValue.hpp mrmac/mrmac.h
mrmac/vm/procedures/MRVMRuntimeProcedures.o: mrmac/vm/procedures/MRVMRuntimeProcedures.cpp mrmac/MRVM.hpp mrmac/vm/MRVMDelayRuntime.hpp mrmac/vm/MRVMProcedureExecution.hpp mrmac/vm/MRVMKeymapRuntime.hpp mrmac/vm/MRVMProcedureCatalog.hpp mrmac/vm/MRVMRuntimeInternal.hpp mrmac/vm/MRVMRuntimeState.hpp mrmac/vm/MRVMValue.hpp mrmac/mrmac.h
mrmac/vm/MRVMDelayRuntime.o: mrmac/vm/MRVMDelayRuntime.cpp mrmac/vm/MRVMDelayRuntime.hpp mrmac/vm/MRVMRuntimeInternal.hpp mrmac/MRVM.hpp mrmac/vm/MRVMRuntimeState.hpp
mrmac/MRVMDebugSession.o: mrmac/MRVMDebugSession.cpp mrmac/MRVMDebugSession.hpp mrmac/MRVM.hpp mrmac/MRMacroExecutionSession.hpp mrmac/vm/MRVMDebugExecution.hpp mrmac/vm/MRVMRuntimeCatalog.hpp mrmac/vm/MRVMRuntimeDebugger.hpp mrmac/vm/MRVMRuntimeState.hpp mrmac/vm/MRVMValue.hpp
mrmac/vm/MRVMProfile.o: mrmac/vm/MRVMProfile.cpp mrmac/vm/MRVMProfile.hpp mrmac/mrmac.h
mrmac/ui/conventional/MRVMDeferredUi.o: mrmac/ui/conventional/MRVMDeferredUi.cpp mrmac/ui/conventional/MRVMDeferredUi.hpp mrmac/vm/MRVMRuntimeState.hpp mrmac/vm/MRVMValue.hpp mrmac/MRVM.hpp
mrmac/ui/conventional/MRVMEditorOperations.o: mrmac/ui/conventional/MRVMEditorOperations.cpp mrmac/ui/conventional/MRVMEditor.hpp mrmac/ui/conventional/MRVMScreen.hpp mrmac/vm/MRVMRuntimeInternal.hpp mrmac/vm/MRVMRuntimeState.hpp mrmac/vm/MRVMValue.hpp mrmac/MRVM.hpp app/commands/MRWindowCommands.hpp config/settings/MRSettingsRuntime.hpp ui/MREditWindow.hpp ui/MRFileEditor/MRFileEditor.hpp ui/MRWindowSupport.hpp
mrmac/ui/conventional/MRVMEditor.o: mrmac/ui/conventional/MRVMEditor.cpp mrmac/ui/conventional/MRVMEditor.hpp mrmac/ui/conventional/MRVMScreen.hpp mrmac/vm/MRVMProcessRuntime.hpp mrmac/vm/MRVMRuntimeInternal.hpp mrmac/MRVM.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp ui/MRFileEditor/MRFileEditor.hpp
mrmac/ui/conventional/MRVMEditorState.o: mrmac/ui/conventional/MRVMEditorState.cpp mrmac/ui/conventional/MRVMEditor.hpp mrmac/ui/conventional/MRVMScreen.hpp mrmac/vm/MRVMRuntimeInternal.hpp mrmac/vm/MRVMRuntimeState.hpp mrmac/vm/MRVMValue.hpp mrmac/MRVM.hpp app/commands/MRWindowCommands.hpp config/settings/MRSettingsRuntime.hpp ui/MREditWindow.hpp ui/MRFileEditor/MRFileEditor.hpp ui/MRWindowSupport.hpp
mrmac/vm/MRVMBytecodeExecution.o: mrmac/vm/MRVMBytecodeExecution.cpp mrmac/vm/MRVMBytecodeExecution.hpp mrmac/vm/MRVMDelayRuntime.hpp mrmac/vm/MRVMExecSessions.hpp mrmac/vm/MRVMHash.hpp mrmac/vm/MRVMIntrinsics.hpp mrmac/vm/MRVMProcedureCatalog.hpp mrmac/vm/MRVMProcedureExecution.hpp mrmac/vm/MRVMRuntimeInternal.hpp mrmac/vm/MRVMRuntimeKv.hpp mrmac/vm/MRVMSystemVariables.hpp mrmac/vm/MRVMValue.hpp mrmac/MRVM.hpp mrmac/mrmac.h app/utils/MRConstants.hpp
mrmac/vm/MRVMExecutionRuntime.o: mrmac/vm/MRVMExecutionRuntime.cpp mrmac/vm/MRVMRuntimeState.hpp mrmac/vm/MRVMValue.hpp mrmac/MRVM.hpp mrmac/mrmac.h
mrmac/vm/MRVMExecSessions.o: mrmac/vm/MRVMExecSessions.cpp mrmac/vm/MRVMExecSessions.hpp mrmac/vm/MRVMRuntimeKv.hpp mrmac/vm/MRVMHash.hpp mrmac/vm/MRVMValue.hpp mrmac/MRVM.hpp mrmac/MRMacroExecutionSession.hpp mrmac/mrmac.h
mrmac/vm/MRVMHash.o: mrmac/vm/MRVMHash.cpp mrmac/vm/MRVMHash.hpp mrmac/MRVM.hpp
mrmac/vm/MRVMIntrinsics.o: mrmac/vm/MRVMIntrinsics.cpp mrmac/vm/MRVMIntrinsics.hpp mrmac/vm/MRVMRuntimeInternal.hpp mrmac/vm/MRVMRuntimeState.hpp mrmac/vm/MRVMRuntimeCatalog.hpp mrmac/vm/MRVMHash.hpp mrmac/vm/MRVMProcessRuntime.hpp mrmac/vm/MRVMValue.hpp mrmac/MRVM.hpp mrmac/ui/conventional/MRVMMacroDialogRuntime.hpp mrmac/ui/modeless/MRVMMacroModelessProcedures.hpp mrmac/ui/modeless/MRVMModelessUiRuntime.hpp ui/MREditWindow.hpp ui/MRWindowSupport.hpp
mrmac/vm/MRVMKeymapRuntime.o: mrmac/vm/MRVMKeymapRuntime.cpp mrmac/vm/MRVMKeymapRuntime.hpp mrmac/vm/MRVMValue.hpp mrmac/MRVM.hpp mrmac/mrmac.h
mrmac/ui/conventional/MRVMMacroDialogRuntime.o: mrmac/ui/conventional/MRVMMacroDialogRuntime.cpp mrmac/ui/conventional/MRVMMacroDialogRuntime.hpp mrmac/vm/MRVMRuntimeKv.hpp mrmac/ui/modeless/MRVMModelessUiRuntime.hpp mrmac/ui/conventional/MRVMScreen.hpp mrmac/vm/MRVMValue.hpp mrmac/MRVM.hpp dialogs/setup/MRSetupCommon.hpp ui/MRWindowSupport.hpp
mrmac/vm/MRVMMacroLoading.o: mrmac/vm/MRVMMacroLoading.cpp mrmac/vm/MRVMRuntimeInternal.hpp mrmac/vm/MRVMExecSessions.hpp mrmac/vm/MRVMKeymapRuntime.hpp mrmac/vm/MRVMProfile.hpp mrmac/vm/MRVMRuntimeDebugger.hpp mrmac/vm/MRVMRuntimeState.hpp mrmac/MRVM.hpp mrmac/mrmac.h app/MRRuntimeScheduler.hpp app/utils/MRFileIOUtils.hpp
mrmac/vm/MRVMMacroRuntime.o: mrmac/vm/MRVMMacroRuntime.cpp mrmac/vm/MRVMRuntimeInternal.hpp mrmac/vm/MRVMExecSessions.hpp mrmac/vm/MRVMHash.hpp mrmac/vm/MRVMKeymapRuntime.hpp mrmac/vm/MRVMMacroSpecRuntime.hpp mrmac/vm/MRVMProcessRuntime.hpp mrmac/vm/MRVMRuntimeCatalog.hpp mrmac/vm/MRVMRuntimeGlobals.hpp mrmac/vm/MRVMRuntimeKv.hpp mrmac/vm/MRVMRuntimeState.hpp mrmac/vm/MRVMValue.hpp mrmac/MRVM.hpp mrmac/MRMacroRunner.hpp mrmac/ui/modeless/MRMacroModelessUi.hpp mrmac/ui/modeless/MRVMModelessUiRuntime.hpp app/MRCommandRouter.hpp app/MRRuntimeScheduler.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp ui/MRWindowSupport.hpp
mrmac/ui/modeless/MRVMMacroModelessProcedures.o: mrmac/ui/modeless/MRVMMacroModelessProcedures.cpp mrmac/ui/modeless/MRVMMacroModelessProcedures.hpp mrmac/ui/conventional/MRVMMacroDialogRuntime.hpp mrmac/ui/modeless/MRVMModelessUiRuntime.hpp mrmac/vm/MRVMValue.hpp mrmac/ui/modeless/MRMacroModelessUi.hpp mrmac/MRVM.hpp
mrmac/vm/MRVMMacroSpecRuntime.o: mrmac/vm/MRVMMacroSpecRuntime.cpp mrmac/vm/MRVMMacroSpecRuntime.hpp mrmac/vm/MRVMValue.hpp app/utils/MRStringUtils.hpp
mrmac/ui/modeless/MRVMModelessUiStorage.o: mrmac/ui/modeless/MRVMModelessUiStorage.cpp mrmac/ui/modeless/MRVMModelessUiStorage.hpp mrmac/ui/modeless/MRVMModelessUiRuntime.hpp mrmac/vm/MRVMRuntimeKv.hpp mrmac/vm/MRVMHash.hpp mrmac/MRVM.hpp mrmac/ui/modeless/MRMacroModelessUi.hpp mrmac/mrmac.h
mrmac/ui/modeless/MRVMModelessUiRuntime.o: mrmac/ui/modeless/MRVMModelessUiRuntime.cpp mrmac/ui/modeless/MRVMModelessUiStorage.hpp mrmac/ui/modeless/MRVMModelessUiRuntime.hpp mrmac/MRVM.hpp
mrmac/ui/modeless/MRVMModelessWindowRuntime.o: mrmac/ui/modeless/MRVMModelessWindowRuntime.cpp mrmac/ui/modeless/MRVMModelessUiStorage.hpp mrmac/ui/modeless/MRVMModelessUiRuntime.hpp mrmac/MRVM.hpp
mrmac/ui/modeless/MRMacroModelessControls.o: mrmac/ui/modeless/MRMacroModelessControls.cpp mrmac/ui/modeless/MRMacroModelessControls.hpp mrmac/ui/modeless/MRVMModelessUiRuntime.hpp ui/widgets/MRNumericSlider.hpp
mrmac/ui/modeless/MRMacroUiCollections.o: mrmac/ui/modeless/MRMacroUiCollections.cpp mrmac/ui/modeless/MRMacroModelessControls.hpp mrmac/ui/modeless/MRVMModelessUiRuntime.hpp
mrmac/vm/MRVMProcessRuntime.o: mrmac/vm/MRVMProcessRuntime.cpp mrmac/vm/MRVMProcessRuntime.hpp mrmac/vm/MRVMRuntimeState.hpp mrmac/vm/MRVMValue.hpp app/utils/MRStringUtils.hpp
mrmac/vm/MRVMProcedureCatalog.o: mrmac/vm/MRVMProcedureCatalog.cpp mrmac/vm/MRVMProcedureCatalog.hpp
mrmac/vm/MRVMRuntimeCatalog.o: mrmac/vm/MRVMRuntimeCatalog.cpp mrmac/vm/MRVMRuntimeCatalog.hpp mrmac/vm/MRVMRuntimeKv.hpp mrmac/vm/MRVMHash.hpp mrmac/vm/MRVMProfile.hpp mrmac/MRVM.hpp app/MRRuntimeScheduler.hpp mrmac/mrmac.h
mrmac/vm/MRVMRuntimeGlobals.o: mrmac/vm/MRVMRuntimeGlobals.cpp mrmac/vm/MRVMRuntimeGlobals.hpp mrmac/vm/MRVMRuntimeKv.hpp mrmac/vm/MRVMHash.hpp mrmac/MRVM.hpp mrmac/mrmac.h
mrmac/vm/MRVMRuntimeKv.o: mrmac/vm/MRVMRuntimeKv.cpp mrmac/vm/MRVMRuntimeKv.hpp mrmac/vm/MRVMHash.hpp mrmac/MRVM.hpp mrmac/mrmac.h
mrmac/ui/conventional/MRVMUiStateRuntime.o: mrmac/ui/conventional/MRVMUiStateRuntime.cpp mrmac/vm/MRVMRuntimeState.hpp mrmac/vm/MRVMRuntimeGlobals.hpp mrmac/vm/MRVMRuntimeCatalog.hpp mrmac/vm/MRVMValue.hpp mrmac/MRVM.hpp mrmac/mrmac.h
mrmac/vm/MRVMSystemVariables.o: mrmac/vm/MRVMSystemVariables.cpp mrmac/vm/MRVMSystemVariables.hpp mrmac/vm/MRVMRuntimeInternal.hpp mrmac/vm/MRVMRuntimeCatalog.hpp mrmac/vm/MRVMRuntimeState.hpp mrmac/vm/MRVMValue.hpp mrmac/MRVM.hpp config/settings/MRSettingsRuntime.hpp ui/MREditWindow.hpp ui/MRWindowSupport.hpp
mrmac/vm/MRVMSettings.o: mrmac/vm/MRVMSettings.cpp mrmac/vm/MRVMSettings.hpp mrmac/MRVM.hpp config/settings/MRSettingsRuntime.hpp config/settings/MRSettingsStorage.hpp keymap/MRKeymapProfile.hpp
mrmac/ui/conventional/MRVMScreen.o: mrmac/ui/conventional/MRVMScreen.cpp mrmac/ui/conventional/MRVMScreen.hpp mrmac/MRVM.hpp ui/MRMenuBar.hpp ui/MRMessageLineController.hpp ui/MRWindowSupport.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp
ui/MRBentoBox/MRBentoBox.o: ui/MRBentoBox/MRBentoBox.cpp ui/MRBentoBox/MRBentoBox.hpp ui/MREditWindow.hpp ui/widgets/MRDropList.hpp
ui/MRBentoBox/MRBentoBoxDerivedProjection.o: ui/MRBentoBox/MRBentoBoxDerivedProjection.cpp ui/MRBentoBox/MRBentoBoxDerivedProjection.hpp coprocessor/MRCoprocessor.hpp outline/MROutlineModel.hpp ui/MRTextBufferModel.hpp
ui/MRBentoBox/MRBentoBoxDiagnostics.o: ui/MRBentoBox/MRBentoBoxDiagnostics.cpp ui/MRBentoBox/MRBentoBox.hpp ui/MREditWindow.hpp ui/MRSidekickEditor.hpp config/settings/MRSettingsRuntime.hpp
ui/MRBentoBox/MRBentoBoxDebugger.o: ui/MRBentoBox/MRBentoBoxDebugger.cpp ui/MRBentoBox/MRBentoBox.hpp ui/MRBentoBox/MRBentoBoxDebuggerStatus.hpp ui/MREditWindow.hpp ui/MRFrame.hpp mrmac/MRVM.hpp mrmac/mrmac.h mrmac/vm/MRVMRuntimeDebugger.hpp app/commands/MRWindowCommands.hpp
ui/MRBentoBox/MRBentoBoxDebuggerStatus.o: ui/MRBentoBox/MRBentoBoxDebuggerStatus.cpp ui/MRBentoBox/MRBentoBoxDebuggerStatus.hpp mrmac/MRVM.hpp mrmac/vm/MRVMRuntimeDebugger.hpp
ui/MRBentoBox/MRBentoBoxOutline.o: ui/MRBentoBox/MRBentoBoxOutline.cpp ui/MRBentoBox/MRBentoBox.hpp ui/MREditWindow.hpp outline/MROutlineFoldProducer.hpp
ui/MRBentoBox/MRBentoBoxPaneWindow.o: ui/MRBentoBox/MRBentoBoxPaneWindow.cpp ui/MRBentoBox/MRBentoBox.hpp ui/MREditWindow.hpp ui/MRFrame.hpp config/settings/MRSettingsRuntime.hpp
ui/MRBentoBox/MRBentoBoxProjection.o: ui/MRBentoBox/MRBentoBoxProjection.cpp ui/MRBentoBox/MRBentoBox.hpp ui/MREditWindow.hpp ui/MRFrame.hpp ui/MRSidekickEditor.hpp ui/MRWindowSupport.hpp config/settings/MRSettingsRuntime.hpp ui/widgets/MRDropList.hpp
ui/MRBentoBox/MRBentoBoxLayout.o: ui/MRBentoBox/MRBentoBoxLayout.cpp ui/MRBentoBox/MRBentoBox.hpp ui/MRBentoBox/MRBentoPaneFrameView.hpp
ui/MRBentoHexEditor/MRBentoHexEditor.o: ui/MRBentoHexEditor/MRBentoHexEditor.cpp ui/MRBentoHexEditor/MRBentoHexEditor.hpp ui/MRBentoHexEditor/panes/MRHexPaneWindow.hpp ui/MRBentoHexEditor/panes/MRHexPaneProjection.hpp ui/MRBentoBox/MRBentoBox.hpp app/MRCommands.hpp
ui/MRBentoHexEditor/MRHexStrings.o: ui/MRBentoHexEditor/MRHexStrings.cpp ui/MRBentoHexEditor/MRHexStrings.hpp ui/MRBentoHexEditor/MRHexUtf8.hpp
ui/MRBentoHexEditor/panes/MRHexPaneWindow.o: ui/MRBentoHexEditor/panes/MRHexPaneWindow.cpp ui/MRBentoHexEditor/panes/MRHexPaneWindow.hpp ui/MRBentoHexEditor/panes/MRHexPaneView.hpp app/MRCommands.hpp
ui/MRBentoHexEditor/panes/MRHexPaneProjection.o: ui/MRBentoHexEditor/panes/MRHexPaneProjection.cpp ui/MRBentoHexEditor/panes/MRHexPaneProjection.hpp ui/MRBentoHexEditor/MRHexInspector.hpp ui/MRBentoHexEditor/MRHexStrings.hpp coprocessor/MRCoprocessor.hpp
ui/MRBentoHexEditor/panes/MRHexPaneView.o: ui/MRBentoHexEditor/panes/MRHexPaneView.cpp ui/MRBentoHexEditor/panes/MRHexPaneView.hpp ui/MRBentoHexEditor/panes/MRHexPaneProjection.hpp ui/MRBentoHexEditor/MRBentoHexEditor.hpp
ui/widgets/MRColumnListView.o: ui/widgets/MRColumnListView.cpp ui/widgets/MRColumnListView.hpp config/settings/MRSettingsRuntime.hpp
ui/widgets/MRDropList.o: ui/widgets/MRDropList.cpp ui/widgets/MRDropList.hpp ui/widgets/MRColumnListView.hpp dialogs/setup/MRSetupCommon.hpp
outline/MROutlineFoldProducer.o: outline/MROutlineFoldProducer.cpp outline/MROutlineFoldProducer.hpp outline/MROutlineModel.hpp derivedstate/MRFoldingDerivedState.hpp ui/MRSyntax.hpp ui/MRSyntaxBasic.hpp ui/MRTextBufferModel.hpp app/utils/MRStringUtils.hpp
ui/MRWindowSupport.o: ui/MRWindowSupport.cpp ui/MRWindowSupport.hpp app/MRPrivilegedFileBroker.hpp config/settings/MRSettingsRuntime.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp
ui/MRSyntax.o: ui/MRSyntax.cpp ui/MRSyntax.hpp
ui/MRSyntaxBasic.o: ui/MRSyntaxBasic.cpp ui/MRSyntax.hpp
ui/MRSyntaxBasicBlocks.o: ui/MRSyntaxBasicBlocks.cpp ui/MRSyntaxBasic.hpp
ui/MRSidekickEditor.o: ui/MRSidekickEditor.cpp ui/MRSidekickEditor.hpp ui/MREditWindow.hpp ui/MRFileEditor/MRFileEditor.hpp
coprocessor/MRCoprocessor.o: coprocessor/MRCoprocessor.cpp coprocessor/MRCoprocessor.hpp piecetable/MRTextDocument.hpp diff/MRDiff.hpp
coprocessor/MRCoprocessorWorkerLifecycle.o: coprocessor/MRCoprocessorWorkerLifecycle.cpp coprocessor/MRCoprocessor.hpp piecetable/MRTextDocument.hpp diff/MRDiff.hpp
coprocessor/MRCoprocessorTelemetry.o: coprocessor/MRCoprocessorTelemetry.cpp coprocessor/MRCoprocessor.hpp piecetable/MRTextDocument.hpp diff/MRDiff.hpp
diff/MRMyersDiff.o: diff/MRMyersDiff.cpp diff/MRDiff.hpp
piecetable/MRTextDocument.o: piecetable/MRTextDocument.cpp piecetable/MRTextDocument.hpp piecetable/MRTextDocumentLineIndex.hpp
piecetable/MRTextDocumentLineIndex.o: piecetable/MRTextDocumentLineIndex.cpp piecetable/MRTextDocumentLineIndex.hpp piecetable/MRTextDocument.hpp
piecetable/MRTextDocumentDirectLineIndex.o: piecetable/MRTextDocumentDirectLineIndex.cpp piecetable/MRTextDocumentLineIndex.hpp piecetable/MRTextDocument.hpp
$(MRFOLDTRAINER_OBJECT): $(MRFOLDTRAINER_SOURCE) ui/MRFileEditor/MRFileEditor.hpp ui/MRSyntax.hpp
$(MRINDENTTRAINER_OBJECT): $(MRINDENTTRAINER_SOURCE) config/settings/MRSettingsRuntime.hpp ui/MRFileEditor/MRFileEditor.hpp ui/MRSyntax.hpp ui/MRSyntaxBasic.hpp
$(MROUTLINETRAINER_OBJECT): $(MROUTLINETRAINER_SOURCE) ui/MRFileEditor/MRFileEditor.hpp ui/MRSyntax.hpp
$(REGRESSION_PROBE_OBJECT): $(REGRESSION_PROBE_SOURCE) app/MRRuntimeScheduler.hpp app/MRRuntimeTimerSource.hpp coprocessor/MRCoprocessor.hpp mrmac/MRMacroExecutionSession.hpp mrmac/MRVM.hpp piecetable/MRTextDocument.hpp
$(MACRO_DEBUGGER_CROSS_SECTION_PROBE_OBJECT): $(MACRO_DEBUGGER_CROSS_SECTION_PROBE_SOURCE) mrmac/MRMacroExecutionSession.hpp mrmac/MRVM.hpp mrmac/mrmac.h mrmac/vm/MRVMRuntimeDebugger.hpp
$(BASIC_LANGUAGE_PROBE_OBJECT): $(BASIC_LANGUAGE_PROBE_SOURCE) app/commands/MRExternalCommand.hpp config/settings/MRSettingsCompilerProfiles.hpp config/settings/MRSettingsRuntime.hpp ui/MRSyntax.hpp ui/MRSyntaxBasic.hpp
app/services/MRWorkspaceServiceContext.o: app/services/MRWorkspaceServiceContext.cpp app/services/MRWorkspaceServiceContext.hpp app/commands/MRWindowCommands.hpp ui/MREditWindow.hpp
$(MR_WORKSPACE_SERVICE_CONTEXT_PROBE_OBJECT): $(MR_WORKSPACE_SERVICE_CONTEXT_PROBE_SOURCE) app/services/MRWorkspaceServiceContext.hpp
# 3. Linker call
$(TARGET): $(TVISION_LIB) $(CXX_OBJECTS) $(C_OBJECTS) | pcre2-check $(HELP_HYPERTEXT_COMPILED)
	$(TMP_RUN) $(CXX) -o $@ $^ $(LDFLAGS) || { paplay --volume=25000 /usr/share/sounds/ocean/stereo/battery-caution.oga; exit 1; }
	killall mr 2> /dev/null || true
	paplay --volume=25000 /usr/share/sounds/freedesktop/stereo/service-login.oga || true

$(MRFOLDTRAINER_TARGET): $(TVISION_LIB) $(CORE_CXX_OBJECTS) $(C_OBJECTS) $(MRFOLDTRAINER_OBJECT) | pcre2-check
	$(TMP_RUN) $(CXX) -o $@ $^ $(LDFLAGS)

$(MRINDENTTRAINER_TARGET): $(TVISION_LIB) $(CORE_CXX_OBJECTS) $(C_OBJECTS) $(MRINDENTTRAINER_OBJECT) | pcre2-check
	$(TMP_RUN) $(CXX) -o $@ $^ $(LDFLAGS)

$(MROUTLINETRAINER_TARGET): $(TVISION_LIB) $(CORE_CXX_OBJECTS) $(C_OBJECTS) $(MROUTLINETRAINER_OBJECT) | pcre2-check
	$(TMP_RUN) $(CXX) -o $@ $^ $(LDFLAGS)

$(STAGE_PROFILE_PROBE_TARGET): $(TVISION_LIB) $(CORE_CXX_OBJECTS) $(C_OBJECTS) $(STAGE_PROFILE_PROBE_OBJECT) | pcre2-check
	$(TMP_RUN) $(CXX) -o $@ $^ $(LDFLAGS)

$(REGRESSION_PROBE_TARGET): $(TVISION_LIB) $(CORE_CXX_OBJECTS) $(C_OBJECTS) $(REGRESSION_PROBE_OBJECT) $(MACRO_DEBUGGER_CROSS_SECTION_PROBE_OBJECT) | pcre2-check
	$(TMP_RUN) $(CXX) -o $@ $^ $(LDFLAGS)

$(BASIC_LANGUAGE_PROBE_TARGET): $(TVISION_LIB) $(CORE_CXX_OBJECTS) $(C_OBJECTS) $(BASIC_LANGUAGE_PROBE_OBJECT) | pcre2-check
	$(TMP_RUN) $(CXX) -o $@ $^ $(LDFLAGS)

$(PHASE1_REPRO_PROBE_TARGET): $(TVISION_LIB) $(CORE_CXX_OBJECTS) $(C_OBJECTS) $(PHASE1_REPRO_PROBE_OBJECT) | pcre2-check
	$(TMP_RUN) $(CXX) -o $@ $^ $(LDFLAGS)

$(MR_WORKSPACE_SERVICE_CONTEXT_PROBE_TARGET): $(TVISION_LIB) $(CORE_CXX_OBJECTS) $(C_OBJECTS) $(MR_WORKSPACE_SERVICE_CONTEXT_PROBE_OBJECT) | pcre2-check
	$(TMP_RUN) $(CXX) -o $@ $^ $(LDFLAGS)

# C++ compilations
%.o: %.cpp
	$(TMP_RUN) $(CXX) $(CXXFLAGS) -c $< -o $@ || { paplay --volume=25000 /usr/share/sounds/ocean/stereo/battery-caution.oga; exit 1; }

# C compilation
%.o: %.c
	$(TMP_RUN) $(CC) $(CFLAGS) -c $< -o $@ || { paplay --volume=25000 /usr/share/sounds/ocean/stereo/battery-caution.oga; exit 1; }

clean:
	find . -type f -name '*.o' -delete
	rm -f $(CXX_OBJECTS) $(C_OBJECTS) $(TARGET) $(STAGE_PROFILE_PROBE_OBJECT) \
		$(MRFOLDTRAINER_OBJECT) $(MRFOLDTRAINER_TARGET) \
		$(MRINDENTTRAINER_OBJECT) $(MRINDENTTRAINER_TARGET) \
		$(MROUTLINETRAINER_OBJECT) $(MROUTLINETRAINER_TARGET) \
		$(STAGE_PROFILE_PROBE_TARGET) \
		$(REGRESSION_PROBE_OBJECT) $(MACRO_DEBUGGER_CROSS_SECTION_PROBE_OBJECT) \
		$(BASIC_LANGUAGE_PROBE_OBJECT) $(BASIC_LANGUAGE_PROBE_TARGET) \
		$(PHASE1_REPRO_PROBE_OBJECT) $(PHASE1_REPRO_PROBE_TARGET) \
		$(MR_WORKSPACE_SERVICE_CONTEXT_PROBE_OBJECT) $(MR_WORKSPACE_SERVICE_CONTEXT_PROBE_TARGET) \
		config/MRDialogPaths.o config/MRSettingsLoader.o \
		misc/mr_keyin_probe.o misc/mr_tofrom_probe.o misc/mr_tofrom_dispatch_probe.o \
		misc/mr_staged_nav_probe misc/mr_staged_mark_page_probe
