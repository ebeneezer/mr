#ifndef MRVM_HPP
#define MRVM_HPP

#include <cstddef>
#include <map>
#include <string>
#include <vector>

class VirtualMachine {
  public:
	struct Value {
		int type;
		int i;
		double r;
		std::string s;
		unsigned char c;

		Value();
	};

  private:
	std::vector<Value> stack;
	std::map<std::string, Value> variables;

	void push(const Value &value);
	Value pop();

  public:
	std::vector<std::string> log;

	VirtualMachine();
	void execute(const unsigned char *bytecode, size_t length);
	void executeAt(const unsigned char *bytecode, size_t length, size_t entryOffset,
	               const std::string &parameterString, const std::string &macroName,
	               bool resetState, bool firstRun);
};

#endif

void mrvmSetProcessContext(int argc, char **argv);
