#ifndef MRVM_HPP
#define MRVM_HPP

#include <cstddef>
#include <string>
#include <vector>
#include <map>

class VirtualMachine
{
private:
	int stack[256];
	int sp;

	std::map<std::string, int> variables;

	void push(int value);
	int pop();

public:
	std::vector<std::string> log;

	VirtualMachine();
	void execute(const unsigned char *bytecode, size_t length);
};

#endif