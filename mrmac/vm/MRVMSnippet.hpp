#ifndef MRVM_SNIPPET_HPP
#define MRVM_SNIPPET_HPP

#include <string>

#include "../MRVM.hpp"

class MREditWindow;
class MRVMHashStore;

bool mrvmSnippetOpenSidekickFromActiveEditor(MREditWindow *win, MRVMHashStore &localStore, MRVMHashStore &globalStore, const VirtualMachine::Value &snippetRootHash);
void mrvmSnippetUnloadLanguage(MRVMHashStore &localStore, MRVMHashStore &globalStore, const VirtualMachine::Value &snippetRootHash, const std::string &language);

#endif
