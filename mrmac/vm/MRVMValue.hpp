#ifndef MRVM_VALUE_HPP
#define MRVM_VALUE_HPP

#include "../MRVM.hpp"

class MRVMHashStore;

bool mrvmValueIsArrayType(int type);
int mrvmArrayTypeForElementType(int elementType);
int mrvmArrayElementTypeForArrayType(int arrayType);
VirtualMachine::Value mrvmMakeArrayValue(int elementType);
VirtualMachine::Value mrvmDefaultValueForType(int type);
VirtualMachine::Value mrvmCoerceForStore(const VirtualMachine::Value &value, int targetType);
VirtualMachine::Value mrvmArrayReadValue(const VirtualMachine::Value &arrayValue, int index);
void mrvmArrayWriteValue(VirtualMachine::Value &arrayValue, int index, const VirtualMachine::Value &value, MRVMHashStore &localStore, MRVMHashStore &globalStore);

#endif
