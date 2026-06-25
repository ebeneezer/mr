#ifndef MRVM_KEYMAP_RUNTIME_HPP
#define MRVM_KEYMAP_RUNTIME_HPP

#define Uses_TEvent
#define Uses_TKeys
#include <tvision/tv.h>

#include "../MRVM.hpp"
#include "../mrmac.h"

#include <string>
#include <string_view>
#include <vector>

enum class MRVMExplicitBindingKind : unsigned char {
	MacroSpec,
	Command
};

struct MRVMExplicitKeyBinding {
	TKey key;
	int mode = MACRO_MODE_EDIT;
	MRVMExplicitBindingKind kind = MRVMExplicitBindingKind::MacroSpec;
	std::string macroSpec;
	int commandId = 0;
};

const char *mrvmKeymapActionIdForMacroCommand(const std::string &name) noexcept;
bool mrvmParseAssignedKeySpec(const std::string &spec, TKey &outKey);
bool mrvmParseIndexedBindingHeaders(const std::string &source, std::vector<TKey> &keys);
bool mrvmBindingKeysEqual(const TKey &lhs, const TKey &rhs) noexcept;
bool mrvmParseBindingKeyValue(const VirtualMachine::Value &value, TKey &key);
std::string mrvmNormalizeMenuKeySpec(std::string keySpec);
std::string mrvmMenuLabelFromBindingKey(const TKey &key);
bool mrvmParseBindingModeValue(int rawMode, int &mode) noexcept;
bool mrvmBindingModeMatches(int bindingMode, int currentMode) noexcept;
void mrvmRemoveExplicitBindingsForKey(std::vector<MRVMExplicitKeyBinding> &bindings, const TKey &key, int mode);
void mrvmLogCalculatorHotkeyState(const char *stage, const TKey &key, std::string_view detail = {});
bool mrvmReplayKeyInputSequence(const std::string &sequence);
bool mrvmKeyReplayActive() noexcept;
bool mrvmKeyPairToTKey(int key1, int key2, TKey &key, const char *&text, std::size_t &textLength, char &textByte);
bool mrvmPassMacroKeyPairToUi(int key1, int key2);

#endif
