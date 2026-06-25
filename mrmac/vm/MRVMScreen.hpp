#ifndef MRVM_SCREEN_HPP
#define MRVM_SCREEN_HPP

#include <string>
#include <vector>

#include "../MRVM.hpp"

bool returnWithMacroScreenMutation(bool ok) noexcept;
bool returnWithDirectScreenMutation(bool ok) noexcept;
bool applyMarqueeProc(const std::string &name, const std::vector<VirtualMachine::Value> &args);
bool applyMakeMessageProc(const std::vector<VirtualMachine::Value> &args);
bool applyBrainProc(const std::string &name, const std::vector<VirtualMachine::Value> &args);
bool applyPutBoxProc(const std::string &name, const std::vector<VirtualMachine::Value> &args);
bool applyWriteProc(const std::string &name, const std::vector<VirtualMachine::Value> &args);
bool applyClrLineProc(const std::string &name, const std::vector<VirtualMachine::Value> &args);
bool applyGotoxyProc(const std::string &name, const std::vector<VirtualMachine::Value> &args);
bool applyPutLineColNumberProc(const std::string &name, const std::vector<VirtualMachine::Value> &args);
bool applyScrollBoxProc(const std::string &name, const std::vector<VirtualMachine::Value> &args, bool down);
bool applyClearScreenProc(const std::string &name, const std::vector<VirtualMachine::Value> &args);
bool applyKillBoxProc(const std::string &name, const std::vector<VirtualMachine::Value> &args);
bool mrvmUiScreenRenderDeferredCommand(const MRMacroDeferredUiCommand &command);

#endif
