#ifndef MRVM_RUNTIME_INTERNAL_HPP
#define MRVM_RUNTIME_INTERNAL_HPP

#include "../MRVM.hpp"
#include "MRVMRuntimeCatalog.hpp"
#include "MRVMRuntimeState.hpp"
#include "../../piecetable/MRTextDocument.hpp"

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

class MRFileEditor;
class MREditWindow;
struct MREditSetupSettings;

namespace mrvm_runtime {

using Value = VirtualMachine::Value;

struct SearchMatchSnapshot {
	bool valid;
	std::string fileName;
	std::string foundText;
	int foundX;
	int foundY;

	SearchMatchSnapshot() : valid(false), fileName(), foundText(), foundX(0), foundY(0) {
	}
};

MREditWindow *activeMacroEditWindow();
MRFileEditor *currentEditor();
BackgroundEditSession *currentBackgroundEditSession() noexcept;
ExecutionState *currentExecutionState() noexcept;
MRMacroExecutionSessionId currentExecutionSessionId() noexcept;
std::string &runtimeParameterString() noexcept;
int &runtimeReturnInt() noexcept;
std::string &runtimeReturnStr() noexcept;
int &runtimeErrorLevel() noexcept;
bool backgroundMacroCancelRequested() noexcept;
bool currentRuntimeIgnoreCase() noexcept;
int currentRegexStatusValue();
bool setCurrentRegexStatus(bool enabled);
bool currentRuntimeTabExpand() noexcept;
SearchMatchSnapshot currentSearchMatchSnapshot();
Value loadCurrentFileState(const std::string &key);
std::string getEnvironmentValue(const std::string &entryName);
int findFirstFileMatch(const std::string &pattern);
int findNextFileMatch();
std::size_t searchLimitForward(const std::string &text, std::size_t start, int numLines);
std::size_t searchLimitBackward(const std::string &text, std::size_t start, int numLines);
bool searchEditorForward(MRFileEditor *editor, const std::string &needle, int numLines, bool ignoreCase, std::size_t &matchStart, std::size_t &matchEnd);
bool searchEditorBackward(MRFileEditor *editor, const std::string &needle, int numLines, bool ignoreCase, std::size_t &matchStart, std::size_t &matchEnd);
bool replaceLastSearch(MRFileEditor *editor, const std::string &replacement);
bool replaceLastSearchBackground(const std::string &replacement);
bool backgroundReplaceRange(const mr::editor::Range &range, const std::string &text, std::size_t cursorPos);
bool backgroundSetCursor(std::size_t target);
std::size_t backgroundCharPtrOffset(std::size_t lineStart, int column);
std::size_t backgroundLineMoveOffset(std::size_t offset, int delta);
std::size_t backgroundPrevWordOffset(std::size_t offset);
std::size_t backgroundNextWordOffset(std::size_t offset);
std::string snapshotEditorText(MRFileEditor *editor);

Value currentEditorCharValue();
bool isVirtualChar(char c);
int nextResolvedTabDisplayColumn(const MREditSetupSettings &settings, int col);
std::string expandTabsString(const std::string &value, bool toVirtuals);
std::string tabsToSpacesString(const std::string &value);
int expandedTabsAdjustedIndex(const std::string &value, int index);
int currentEditorIndentLevel();
bool setCurrentEditorIndentLevel(int level);
bool currentEditorInsertMode();
bool setCurrentEditorInsertMode(bool on);
std::string currentEditorLineText(MRFileEditor *editor);
std::string currentEditorWord(MRFileEditor *editor, const std::string &delimiters);
bool insertEditorText(MRFileEditor *editor, const std::string &text);
bool replaceEditorLine(MRFileEditor *editor, const std::string &text);
bool wordWrapEditorLine(MRFileEditor *editor);
bool backspaceEditor(MRFileEditor *editor);
bool deleteEditorChars(MRFileEditor *editor, int count);
bool deleteEditorLine(MRFileEditor *editor);
int currentEditorColumn(MRFileEditor *editor);
bool currentUiCursorPosition(int &x, int &y);
int currentEditorLineNumber(MRFileEditor *editor);
std::size_t currentEditorCursorOffset(MRFileEditor *editor);
bool setEditorCursor(MRFileEditor *editor, unsigned int target, int requestedVisualColumn = -1);
bool moveEditorLeft(MRFileEditor *editor);
bool moveEditorRight(MRFileEditor *editor);
bool moveEditorUp(MRFileEditor *editor);
bool moveEditorDown(MRFileEditor *editor);
bool moveEditorHome(MRFileEditor *editor);
bool moveEditorEol(MRFileEditor *editor);
bool moveEditorTof(MRFileEditor *editor);
bool moveEditorEof(MRFileEditor *editor);
bool moveEditorWordLeft(MRFileEditor *editor);
bool moveEditorWordRight(MRFileEditor *editor);
bool moveEditorFirstWord(MRFileEditor *editor);
bool gotoEditorLine(MRFileEditor *editor, int lineNum);
bool gotoEditorCol(MRFileEditor *editor, int colNum);
bool currentEditorAtEof(MRFileEditor *editor);
bool currentEditorAtEol(MRFileEditor *editor);
int currentEditorRow(MRFileEditor *editor);
int currentEditorPage(MRFileEditor *editor);
int currentEditorPageLine(MRFileEditor *editor);
bool markEditorPosition(MREditWindow *win, MRFileEditor *editor);
bool gotoEditorMark(MREditWindow *win, MRFileEditor *editor);
bool popEditorMark(MREditWindow *win);
bool setEditorRandomAccessMark(MREditWindow *win, MRFileEditor *editor, int index);
bool gotoEditorRandomAccessMark(MREditWindow *win, MRFileEditor *editor, int index);
bool moveEditorPageUp(MRFileEditor *editor);
bool moveEditorPageDown(MRFileEditor *editor);
bool moveEditorNextPageBreak(MRFileEditor *editor);
bool moveEditorLastPageBreak(MRFileEditor *editor);
bool replaceEditorBuffer(MRFileEditor *editor, const std::string &text, std::size_t cursorPos);
int lineIndexForPtr(MRFileEditor *editor, unsigned int ptr);

int blockStatusValue(MREditWindow *win);
bool blockMarkingValue(MREditWindow *win);
int blockLine1Value(MREditWindow *win, MRFileEditor *editor);
int blockLine2Value(MREditWindow *win, MRFileEditor *editor);
int blockCol1Value(MREditWindow *win, MRFileEditor *editor);
int blockCol2Value(MREditWindow *win, MRFileEditor *editor);
bool beginCurrentBlockMode(int mode);
bool endCurrentBlockMode();
bool clearCurrentBlockMode();

std::vector<MREditWindow *> allEditWindows();
MREditWindow *editWindowByIndex(int index);
void cleanupWindowLinkGroups();
int windowLinkGroupOf(MREditWindow *win);
bool isWindowLinked(MREditWindow *win);
int countEditWindows();
int currentEditWindowIndex();
int currentLinkStatus();
bool currentWindowGeometry(int &x1, int &y1, int &x2, int &y2);
bool prepareWindowLink(MREditWindow *current, MREditWindow *target, MREditWindow *&source, MREditWindow *&dest);
bool linkCurrentEditWindow();
bool unlinkCurrentEditWindow();
void syncLinkedWindowsFrom(MREditWindow *source);
bool redrawCurrentEditWindow();
bool redrawEntireScreen();
bool zoomCurrentEditWindow();
bool createEditWindow();
bool switchEditWindow(int index);
bool sizeCurrentEditWindow(int x1, int y1, int x2, int y2);
bool deleteCurrentEditWindow();
bool eraseCurrentEditWindow();
bool modifyCurrentEditWindow();
bool moveEditorTabRight(MRFileEditor *editor);
bool moveEditorTabLeft(MRFileEditor *editor);
bool indentEditor(MRFileEditor *editor);
bool undentEditor(MRFileEditor *editor);
bool carriageReturnEditor(MRFileEditor *editor);

bool readLoadedMacroByKey(const std::string &macroKey, MacroRef &macroRef);
bool loadedMacroExists(const std::string &macroKey);
bool loadMacroFileIntoRegistry(const std::string &spec, std::string *loadedFileKey = nullptr);
bool unloadMacroFromRegistry(const std::string &macroName);
bool ensureLoadedFileResident(const std::string &fileKey);
bool evictTransientFileImage(const std::string &fileKey);
bool currentBackgroundChildMacroAllowed(const LoadedMacroFile &file) noexcept;
bool resolveDebugMacroSpec(const std::string &spec, std::string &macroKey, std::string &parameterString, LoadedMacroFile &file, std::string &errorMessage);
bool prepareDebugMacroByKey(const std::string &macroKey, bool stopAtEntry, MacroRef &macroRef, LoadedMacroFile &file, std::vector<std::size_t> &breakpointOffsets, bool &firstRun, std::string &errorMessage);
bool tryLoadIndexedMacroForKey(const TKey &pressed);
std::vector<std::string> macroCatalogMacroOrder();
std::size_t macroCatalogMacroEnumIndex();
void setMacroCatalogMacroEnumIndex(std::size_t index);
bool readMacroKeyPair(bool blocking, int &key1, int &key2);
bool pushQueuedKeyPair(int key1, int key2) noexcept;
MacroFunctionLabelFrame &currentFunctionLabelFrame();

std::string parseNamedValue(const std::string &needle, const std::string &source);
void appendUniqueString(std::vector<std::string> &values, const std::string &value);
std::vector<int> currentGlobalHashRoots();
bool readRuntimeGlobalValueDirect(const std::string &name, GlobalEntry &entry);
std::string runtimeGlobalStringValue(const std::string &name);
int runtimeGlobalIntValue(const std::string &name);
std::vector<std::string> macroGlobalOrderValues();
std::size_t macroGlobalEnumIndex();
void setMacroGlobalEnumIndex(std::size_t index);
void setGlobalValue(const std::string &name, int type, const Value &value);
void setGlobalValueFromStore(const std::string &name, int type, const Value &value, MRVMHashStore &localStore);
bool readGlobalValue(const std::string &name, GlobalEntry &entry);
MRMacroDebugVariableScope macroDebugVariableScope(const std::string &name, const std::set<std::string> &closureVariableNames, const std::set<std::string> &sessionVariableNames);
std::string macroDebugValueText(const Value &value, const MRVMHashStore &localStore, const MRVMHashStore &globalStore);
void appendMacroDebugVariableSnapshots(MRMacroDebugRunResult &result, const std::map<std::string, Value> &variableStore, const std::set<std::string> &closureVariableNames, const std::set<std::string> &sessionVariableNames, const MRVMHashStore &localStore, const MRVMHashStore &globalStore);
void appendMacroDebugAppGlobalSnapshots(MRMacroDebugRunResult &result, const MRVMHashStore &globalStore);

bool readLoadedMacroFileByKey(const std::string &fileKey, LoadedMacroFile &file);
bool loadedMacroFileExists(const std::string &fileKey);
void writeLoadedMacroFileByKey(const LoadedMacroFile &file);
void writeLoadedMacroByKey(const std::string &macroKey, const MacroRef &macroRef);
void appendMacroCatalogMacroOrder(const std::string &macroKey);
std::size_t macroCatalogLoadedMacroCount();
std::vector<IndexedBoundMacroEntry> macroCatalogIndexedBindings();
bool markMacroCatalogIndexedWarmupAttempted(const std::string &fileKey);
std::size_t macroCatalogIndexedBindingCount();
std::string resolveLoadedFileKeyForSpec(const std::string &fileSpec);
bool macroIsRunning(const std::string &macroKey);
bool removeMacroFromRegistryByKey(const std::string &macroKey);

void applyFunctionLabelState();
void showMacroModelessDialog(const std::vector<Value> &args);
void updateMacroModelessDialog(const std::vector<Value> &args);
void updateMacroModelessDisplayLine(const std::vector<Value> &args);
void closeMacroModelessDialog(const std::vector<Value> &args);
void listExecSessionClosures(const std::vector<Value> &args);
void stopExecSessionClosure(const std::vector<Value> &args);
int currentUiMacroMode();
bool executeLoadedMacro(const std::string &macroKey, const std::string &paramPart, std::vector<std::string> *logSink);
bool executeLoadedMacroWithConfiguredKeymapBatch(const std::string &macroKey, const std::string &paramPart, std::vector<std::string> *logSink);
bool macroAllowsUiMode(const MacroRef &macroRef, int mode) noexcept;
void clearRegisteredBindingsForKey(const TKey *key, int mode, bool clearAllModes);
bool executeRuntimeMacroSpec(const std::string &spec, std::vector<std::string> *logLines);
bool currentExecutingMacroSpecFromRuntimeStack(std::string &macroSpec);
bool composeLoadedMacroSpec(const MacroRef &macroRef, std::string &macroSpec);
bool macroSpecTargetsLoadedMacro(const std::string &spec, const std::string &targetFileKey, const std::string &targetMacroKey);
bool dispatchApplicationCommandEvent(unsigned short command);
bool executeExplicitKeyBinding(const TKey &pressed, int mode, std::vector<std::string> *logLines);
bool projectRuntimeMenuKeyLabelsFromExplicitBindings(std::string *errorMessage);
bool fileContainsOnlyTransientMacros(const LoadedMacroFile &file);

} // namespace mrvm_runtime

#endif
