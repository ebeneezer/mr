#ifndef MRVM_VALUE_HPP
#define MRVM_VALUE_HPP

#include "../MRVM.hpp"

#include <cstdint>
#include <string>

class MRVMHashStore;

std::string mrvmUpperKey(const std::string &value);
VirtualMachine::Value mrvmMakeInt(int value);
VirtualMachine::Value mrvmMakeReal(double value);
VirtualMachine::Value mrvmMakeString(const std::string &value);
VirtualMachine::Value mrvmMakeChar(unsigned char value);
VirtualMachine::Value mrvmMakeHash(int handle, bool globalStorage = false);
std::string mrvmCharToString(unsigned char c);
bool mrvmIsStringLike(const VirtualMachine::Value &value);
bool mrvmIsNumeric(const VirtualMachine::Value &value);
std::string mrvmValueAsString(const VirtualMachine::Value &value);
double mrvmValueAsReal(const VirtualMachine::Value &value);
int mrvmValueAsInt(const VirtualMachine::Value &value);
bool mrvmValueHasContent(const VirtualMachine::Value &value);
int mrvmCompareValues(const VirtualMachine::Value &a, const VirtualMachine::Value &b);
std::string mrvmRemoveSpaceAscii(const std::string &value);
std::string mrvmGetExtensionPart(const std::string &value);
std::string mrvmGetPathPart(const std::string &value);
std::string mrvmTruncateExtensionPart(const std::string &value);
std::string mrvmTruncatePathPart(const std::string &value);
void mrvmEnforceStringLength(const std::string &value);
std::string mrvmUtf8FromCodepoint(std::uint32_t codepoint);
int mrvmCheckedStringIndex(int pos);
int mrvmCheckedInsertIndex(int pos);
int mrvmFindValErrorPosition(const std::string &text);
int mrvmFindRValErrorPosition(const std::string &text);
bool mrvmValueIsArrayType(int type);
int mrvmArrayTypeForElementType(int elementType);
int mrvmArrayElementTypeForArrayType(int arrayType);
VirtualMachine::Value mrvmMakeArrayValue(int elementType);
VirtualMachine::Value mrvmDefaultValueForType(int type);
VirtualMachine::Value mrvmCoerceForStore(const VirtualMachine::Value &value, int targetType);
VirtualMachine::Value mrvmArrayReadValue(const VirtualMachine::Value &arrayValue, int index);
void mrvmArrayWriteValue(VirtualMachine::Value &arrayValue, int index, const VirtualMachine::Value &value, MRVMHashStore &localStore, MRVMHashStore &globalStore);

#endif
