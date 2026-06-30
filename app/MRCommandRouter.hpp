#ifndef MRCOMMANDROUTER_HPP
#define MRCOMMANDROUTER_HPP

#include <tvision/tv.h>

#include "../config/settings/MRSettingsRuntime.hpp"

#include <string>
#include <string_view>

class MREditWindow;

struct MRLspRuntimeSettings {
	bool spawnDaemon;
	MRLanguageServerSidekickPlacement sidekickPlacement;
	int hoverDwellMs;
	int documentSyncDelayMs;
	int signatureQuietMs;
	MRLanguageServerChannelSettings channels;

	MRLspRuntimeSettings() noexcept
	    : spawnDaemon(true), sidekickPlacement(MRLanguageServerSidekickPlacement::RightMargin), hoverDwellMs(kLanguageServerHoverDwellMsDefault), documentSyncDelayMs(kLanguageServerDocumentSyncDelayMsDefault), signatureQuietMs(kLanguageServerSignatureQuietMsDefault), channels() {
	}
};

[[nodiscard]] bool handleMRCommand(ushort command, void *commandInfo = nullptr);
[[nodiscard]] bool dispatchMRKeymapAction(std::string_view actionId, std::string_view sequenceText = {}, MREditWindow *targetWindow = nullptr);
[[nodiscard]] bool dispatchMRKeymapMacro(std::string_view macroSpec);
[[nodiscard]] bool showMRLspContextMenu(MREditWindow *targetWindow, TPoint where);
void notifyMRLspMouseActivity(TPoint where) noexcept;
void notifyMRLspBlockMouseActivity() noexcept;
void notifyMRLspKeyboardActivity() noexcept;
[[nodiscard]] MRLspRuntimeSettings configuredMRLspRuntimeSettings();
void mrRefreshLspRuntimeSettingsSnapshot(const MRLspRuntimeSettings &settings);
void mrRefreshLspRuntimeSettingsSnapshot();
void mrApplyLspSupportSettingsChange();
void mrApplyLspSupportSettingsChange(bool spawnDaemonEnabled);
void pumpMRLspService();
void pumpMRLspService(bool spawnDaemonEnabled);
void pumpMRLspService(const MRLspRuntimeSettings &settings);
void clearTransientSearchSelectionOnUserInput(const TEvent &event);
bool mrLspCompletionTargetSelfTestForRegression(std::string &failureReason);
bool mrLspSnippetMiddlewareExpansionSelfTestForRegression(std::string &failureReason);

#endif
