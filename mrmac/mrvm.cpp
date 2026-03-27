#define Uses_MsgBox
#include <tvision/tv.h>

#include "mrvm.hpp"
#include "mrmac.h"
#include <cstring>
#include <string>
#include <cstdio>
#include <vector>
#include <map>

#include "../ui/mrtheme.hpp"

VirtualMachine::VirtualMachine()
{
	sp = 0;
}

void VirtualMachine::push(int value)
{
	if (sp < 256)
	{
		stack[sp++] = value;
	}
	else
	{
		log.push_back("VM Error: Stack overflow.");
	}
}

int VirtualMachine::pop()
{
	if (sp > 0)
		return stack[--sp];

	log.push_back("VM Error: Stack underflow.");
	return 0;
}

void VirtualMachine::execute(const unsigned char *bytecode, size_t length)
{
	size_t ip = 0;
	std::vector<std::string> string_pool;
	std::vector<size_t> call_stack;
	variables.clear();
	log.clear();

	while (ip < length)
	{
		unsigned char opcode = bytecode[ip++];

		if (opcode == OP_PUSH_I)
		{
			int val;
			std::memcpy(&val, &bytecode[ip], sizeof(int));
			ip += sizeof(int);
			push(val);
			log.push_back("Push integer: " + std::to_string(val));
		}
		else if (opcode == OP_PUSH_S)
		{
			const char *str = reinterpret_cast<const char *>(&bytecode[ip]);
			ip += std::strlen(str) + 1;

			string_pool.push_back(std::string(str));
			push(static_cast<int>(string_pool.size() - 1));
			log.push_back("Push string: " + std::string(str));
		}
		else if (opcode == OP_LOAD_VAR)
		{
			const char *var_name = reinterpret_cast<const char *>(&bytecode[ip]);
			ip += std::strlen(var_name) + 1;

			std::map<std::string, int>::const_iterator it = variables.find(std::string(var_name));
			int value = 0;

			if (it != variables.end())
				value = it->second;

			push(value);
			log.push_back("Load variable: " + std::string(var_name) + " = " + std::to_string(value));
		}
		else if (opcode == OP_STORE_VAR)
		{
			const char *var_name = reinterpret_cast<const char *>(&bytecode[ip]);
			ip += std::strlen(var_name) + 1;

			int value = pop();
			variables[std::string(var_name)] = value;
			log.push_back("Store variable: " + std::string(var_name) + " = " + std::to_string(value));
		}
		else if (opcode == OP_GOTO)
		{
			int target;
			std::memcpy(&target, &bytecode[ip], sizeof(int));
			ip += sizeof(int);

			if (target < 0 || static_cast<size_t>(target) >= length)
			{
				log.push_back("VM Error: Invalid jump target in GOTO.");
				break;
			}

			log.push_back("GOTO -> " + std::to_string(target));
			ip = static_cast<size_t>(target);
		}
		else if (opcode == OP_CALL)
		{
			int target;
			std::memcpy(&target, &bytecode[ip], sizeof(int));
			ip += sizeof(int);

			if (target < 0 || static_cast<size_t>(target) >= length)
			{
				log.push_back("VM Error: Invalid jump target in CALL.");
				break;
			}

			call_stack.push_back(ip);
			log.push_back("CALL -> " + std::to_string(target));
			ip = static_cast<size_t>(target);
		}
		else if (opcode == OP_RET)
		{
			if (call_stack.empty())
			{
				log.push_back("VM Error: RET without matching CALL.");
				break;
			}

			ip = call_stack.back();
			call_stack.pop_back();
			log.push_back("RET -> " + std::to_string(ip));
		}
		else if (opcode == OP_JZ)
		{
			int target;
			std::memcpy(&target, &bytecode[ip], sizeof(int));
			ip += sizeof(int);

			int cond = pop();
			if (target < 0 || static_cast<size_t>(target) >= length)
			{
				log.push_back("VM Error: Invalid jump target in JZ.");
				break;
			}

			if (cond == 0)
			{
				log.push_back("JZ -> " + std::to_string(target));
				ip = static_cast<size_t>(target);
			}
			else
			{
				log.push_back("JZ not taken");
			}
		}
		else if (opcode == OP_ADD)
		{
			int b = pop();
			int a = pop();
			push(a + b);
			log.push_back("Add: " + std::to_string(a) + " + " + std::to_string(b));
		}
		else if (opcode == OP_SUB)
		{
			int b = pop();
			int a = pop();
			push(a - b);
			log.push_back("Sub: " + std::to_string(a) + " - " + std::to_string(b));
		}
		else if (opcode == OP_MUL)
		{
			int b = pop();
			int a = pop();
			push(a * b);
			log.push_back("Mul: " + std::to_string(a) + " * " + std::to_string(b));
		}
		else if (opcode == OP_DIV)
		{
			int b = pop();
			int a = pop();
			if (b == 0)
			{
				log.push_back("VM Error: Division by zero.");
				break;
			}

			push(a / b);
			log.push_back("Div: " + std::to_string(a) + " / " + std::to_string(b));
		}
		else if (opcode == OP_MOD)
		{
			int b = pop();
			int a = pop();
			if (b == 0)
			{
				log.push_back("VM Error: Modulo by zero.");
				break;
			}

			push(a % b);
			log.push_back("Mod: " + std::to_string(a) + " % " + std::to_string(b));
		}
		else if (opcode == OP_NEG)
		{
			int a = pop();
			push(-a);
			log.push_back("Neg: " + std::to_string(a));
		}
		else if (opcode == OP_CMP_EQ)
		{
			int b = pop();
			int a = pop();
			push(a == b ? 1 : 0);
			log.push_back("Cmp ==");
		}
		else if (opcode == OP_CMP_NE)
		{
			int b = pop();
			int a = pop();
			push(a != b ? 1 : 0);
			log.push_back("Cmp !=");
		}
		else if (opcode == OP_CMP_LT)
		{
			int b = pop();
			int a = pop();
			push(a < b ? 1 : 0);
			log.push_back("Cmp <");
		}
		else if (opcode == OP_CMP_GT)
		{
			int b = pop();
			int a = pop();
			push(a > b ? 1 : 0);
			log.push_back("Cmp >");
		}
		else if (opcode == OP_CMP_LE)
		{
			int b = pop();
			int a = pop();
			push(a <= b ? 1 : 0);
			log.push_back("Cmp <=");
		}
		else if (opcode == OP_CMP_GE)
		{
			int b = pop();
			int a = pop();
			push(a >= b ? 1 : 0);
			log.push_back("Cmp >=");
		}
		else if (opcode == OP_AND)
		{
			int b = pop();
			int a = pop();
			push(a & b);
			log.push_back("And");
		}
		else if (opcode == OP_OR)
		{
			int b = pop();
			int a = pop();
			push(a | b);
			log.push_back("Or");
		}
		else if (opcode == OP_NOT)
		{
			int a = pop();
			push(~a);
			log.push_back("Not");
		}
		else if (opcode == OP_SHL)
		{
			int b = pop();
			int a = pop();
			push(a << b);
			log.push_back("Shl");
		}
		else if (opcode == OP_SHR)
		{
			int b = pop();
			int a = pop();
			push(a >> b);
			log.push_back("Shr");
		}
		else if (opcode == OP_TVCALL)
		{
			const char *func_name = reinterpret_cast<const char *>(&bytecode[ip]);
			ip += std::strlen(func_name) + 1;
			unsigned char param_count = bytecode[ip++];

			log.push_back("TVCALL: " + std::string(func_name) + " (" + std::to_string(param_count) + " params)");

			if (std::strcmp(func_name, "MessageBox") == 0)
			{
				if (param_count == 2)
				{
					int str_idx = pop();
					int value = pop();

					if (str_idx >= 0 && static_cast<size_t>(str_idx) < string_pool.size())
						messageBox(mfInformation | mfOKButton, "%s %d", string_pool[str_idx].c_str(), value);
				}
				else if (param_count == 1)
				{
					int str_idx = pop();
					if (str_idx >= 0 && static_cast<size_t>(str_idx) < string_pool.size())
						messageBox(mfInformation | mfOKButton, "%s", string_pool[str_idx].c_str());
				}
			}
			else if (std::strcmp(func_name, "RegisterTheme") == 0)
			{
				if (param_count == 17)
				{
					int str_idx = pop();
					MRTheme newTheme;

					for (int i = 15; i >= 0; i--)
						newTheme.paletteData[i] = static_cast<char>(pop());
					newTheme.paletteData[16] = '\0';

					if (str_idx >= 0 && static_cast<size_t>(str_idx) < string_pool.size())
					{
						std::string targetClass = string_pool[str_idx];
						MRThemeRegistry::instance().registerTheme(targetClass, newTheme);
					}
				}
			}
		}
		else if (opcode == OP_HALT)
		{
			log.push_back("Program end reached.");
			break;
		}
		else
		{
			char hexOp[10];
			std::snprintf(hexOp, sizeof(hexOp), "0x%02X", opcode);
			log.push_back("VM Error: Unknown opcode " + std::string(hexOp));
			break;
		}
	}
}
