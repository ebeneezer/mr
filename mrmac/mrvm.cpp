#define Uses_MsgBox
#include <tvision/tv.h>

#include "mrvm.hpp"
#include "mrmac.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../ui/mrtheme.hpp"

namespace
{
    using Value = VirtualMachine::Value;

    static Value makeInt(int value)
    {
        Value v;
        v.type = TYPE_INT;
        v.i = value;
        return v;
    }

    static Value makeReal(double value)
    {
        Value v;
        v.type = TYPE_REAL;
        v.r = value;
        return v;
    }

    static Value makeString(const std::string &value)
    {
        Value v;
        v.type = TYPE_STR;
        v.s = value;
        return v;
    }

    static Value makeChar(unsigned char value)
    {
        Value v;
        v.type = TYPE_CHAR;
        v.c = value;
        return v;
    }

    static std::string charToString(unsigned char c)
    {
        if (c == 0)
            return std::string();
        return std::string(1, static_cast<char>(c));
    }

    static bool isStringLike(const Value &value)
    {
        return value.type == TYPE_STR || value.type == TYPE_CHAR;
    }

    static bool isNumeric(const Value &value)
    {
        return value.type == TYPE_INT || value.type == TYPE_REAL;
    }

    static std::string valueAsString(const Value &value)
    {
        char buf[128];

        switch (value.type)
        {
        case TYPE_STR:
            return value.s;
        case TYPE_CHAR:
            return charToString(value.c);
        case TYPE_INT:
            std::snprintf(buf, sizeof(buf), "%d", value.i);
            return std::string(buf);
        case TYPE_REAL:
            std::snprintf(buf, sizeof(buf), "%.11g", value.r);
            return std::string(buf);
        default:
            return std::string();
        }
    }

    static std::string uppercaseAscii(std::string value)
    {
        for (std::string::size_type i = 0; i < value.size(); ++i)
            value[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[i])));
        return value;
    }

    static double valueAsReal(const Value &value)
    {
        if (value.type == TYPE_REAL)
            return value.r;
        if (value.type == TYPE_INT)
            return static_cast<double>(value.i);
        throw std::runtime_error("numeric value expected");
    }

    static int valueAsInt(const Value &value)
    {
        if (value.type == TYPE_INT)
            return value.i;
        throw std::runtime_error("integer value expected");
    }

    static int compareValues(const Value &a, const Value &b)
    {
        if (isStringLike(a) && isStringLike(b))
        {
            std::string as = valueAsString(a);
            std::string bs = valueAsString(b);
            if (as < bs)
                return -1;
            if (as > bs)
                return 1;
            return 0;
        }

        if (isNumeric(a) && isNumeric(b))
        {
            double av = valueAsReal(a);
            double bv = valueAsReal(b);
            if (av < bv)
                return -1;
            if (av > bv)
                return 1;
            return 0;
        }

        throw std::runtime_error("type mismatch");
    }

    static Value defaultValueForType(int type)
    {
        switch (type)
        {
        case TYPE_INT:
            return makeInt(0);
        case TYPE_REAL:
            return makeReal(0.0);
        case TYPE_CHAR:
            return makeChar(0);
        case TYPE_STR:
        default:
            return makeString("");
        }
    }

    static Value coerceForStore(const Value &value, int targetType)
    {
        switch (targetType)
        {
        case TYPE_INT:
            if (value.type == TYPE_INT)
                return value;
            throw std::runtime_error("type mismatch");

        case TYPE_REAL:
            if (value.type == TYPE_REAL)
                return value;
            if (value.type == TYPE_INT)
                return makeReal(static_cast<double>(value.i));
            throw std::runtime_error("type mismatch");

        case TYPE_STR:
            if (value.type == TYPE_STR)
                return value;
            if (value.type == TYPE_CHAR)
                return makeString(charToString(value.c));
            throw std::runtime_error("type mismatch");

        case TYPE_CHAR:
            if (value.type == TYPE_CHAR)
                return value;
            if (value.type == TYPE_STR)
            {
                if (value.s.empty())
                    return makeChar(0);
                return makeChar(static_cast<unsigned char>(value.s[0]));
            }
            throw std::runtime_error("type mismatch");

        default:
            throw std::runtime_error("unknown variable type");
        }
    }

    static void enforceStringLength(const std::string &s)
    {
        if (s.size() > 254)
            throw std::runtime_error("String length error.");
    }

    static int checkedStringIndex(int pos)
    {
        if (pos < 1 || pos > 254)
            throw std::runtime_error("Invalid string index on string copy operation.");
        return pos;
    }

    static int checkedInsertIndex(int pos)
    {
        if (pos < 0 || pos > 254)
            throw std::runtime_error("Invalid string index on string copy operation.");
        return pos;
    }

    static int findValErrorPosition(const std::string &text)
    {
        std::size_t i = 0;
        const std::size_t n = text.size();

        while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i == n)
            return 1;

        if (text[i] == '+' || text[i] == '-')
            ++i;

        {
            const std::size_t firstDigit = i;
            while (i < n && std::isdigit(static_cast<unsigned char>(text[i])))
                ++i;
            if (i == firstDigit)
                return static_cast<int>(firstDigit + 1);
        }

        while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i != n)
            return static_cast<int>(i + 1);
        return 0;
    }

    static int findRValErrorPosition(const std::string &text)
    {
        std::size_t i = 0;
        const std::size_t n = text.size();

        while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i == n)
            return 1;

        if (text[i] == '+' || text[i] == '-')
            ++i;

        {
            bool seenDigits = false;
            while (i < n && std::isdigit(static_cast<unsigned char>(text[i])))
            {
                seenDigits = true;
                ++i;
            }
            if (i < n && text[i] == '.')
            {
                ++i;
                while (i < n && std::isdigit(static_cast<unsigned char>(text[i])))
                {
                    seenDigits = true;
                    ++i;
                }
            }
            if (!seenDigits)
                return static_cast<int>(i + 1);
        }

        if (i < n && (text[i] == 'e' || text[i] == 'E'))
        {
            const std::size_t expPos = i;
            ++i;
            if (i < n && (text[i] == '+' || text[i] == '-'))
                ++i;
            {
                const std::size_t firstExpDigit = i;
                while (i < n && std::isdigit(static_cast<unsigned char>(text[i])))
                    ++i;
                if (i == firstExpDigit)
                    return static_cast<int>(expPos + 1);
            }
        }

        while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i != n)
            return static_cast<int>(i + 1);
        return 0;
    }

    static Value applyIntrinsic(const std::string &name, const std::vector<Value> &args)
    {
        if (name == "STR")
        {
            if (args.size() != 1 || args[0].type != TYPE_INT)
                throw std::runtime_error("STR expects one integer argument.");
            return makeString(valueAsString(args[0]));
        }
        if (name == "CHAR")
        {
            if (args.size() != 1 || args[0].type != TYPE_INT)
                throw std::runtime_error("CHAR expects one integer argument.");
            return makeChar(static_cast<unsigned char>(args[0].i & 0xFF));
        }
        if (name == "ASCII")
        {
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("ASCII expects one string argument.");
            std::string s = valueAsString(args[0]);
            return makeInt(s.empty() ? 0 : static_cast<unsigned char>(s[0]));
        }
        if (name == "CAPS")
        {
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("CAPS expects one string argument.");
            return makeString(uppercaseAscii(valueAsString(args[0])));
        }
        if (name == "COPY")
        {
            std::string s;
            int pos;
            int count;
            std::size_t start;
            if (args.size() != 3 || !isStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT)
                throw std::runtime_error("COPY expects (string, int, int).");
            s = valueAsString(args[0]);
            pos = checkedStringIndex(args[1].i);
            count = args[2].i;
            if (count < 0)
                throw std::runtime_error("Invalid string index on string copy operation.");
            if (static_cast<std::size_t>(pos) > s.size())
                return makeString("");
            start = static_cast<std::size_t>(pos - 1);
            return makeString(s.substr(start, static_cast<std::size_t>(count)));
        }
        if (name == "LENGTH")
        {
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("LENGTH expects one string argument.");
            return makeInt(static_cast<int>(valueAsString(args[0]).size()));
        }
        if (name == "POS")
        {
            std::string needle;
            std::string haystack;
            std::size_t pos;
            if (args.size() != 2 || !isStringLike(args[0]) || !isStringLike(args[1]))
                throw std::runtime_error("POS expects (substring, string).");
            needle = valueAsString(args[0]);
            haystack = valueAsString(args[1]);
            if (needle.empty())
                return makeInt(1);
            pos = haystack.find(needle);
            return makeInt(pos == std::string::npos ? 0 : static_cast<int>(pos + 1));
        }
        if (name == "XPOS")
        {
            std::string needle;
            std::string haystack;
            int startPos;
            std::size_t pos;
            if (args.size() != 3 || !isStringLike(args[0]) || !isStringLike(args[1]) || args[2].type != TYPE_INT)
                throw std::runtime_error("XPOS expects (substring, string, int).");
            needle = valueAsString(args[0]);
            haystack = valueAsString(args[1]);
            startPos = checkedStringIndex(args[2].i);
            if (needle.empty())
                return makeInt(startPos <= static_cast<int>(haystack.size()) + 1 ? startPos : 0);
            if (static_cast<std::size_t>(startPos) > haystack.size())
                return makeInt(0);
            pos = haystack.find(needle, static_cast<std::size_t>(startPos - 1));
            return makeInt(pos == std::string::npos ? 0 : static_cast<int>(pos + 1));
        }
        if (name == "STR_DEL")
        {
            std::string s;
            int pos;
            int count;
            std::size_t start;
            if (args.size() != 3 || !isStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT)
                throw std::runtime_error("STR_DEL expects (string, int, int).");
            s = valueAsString(args[0]);
            pos = checkedStringIndex(args[1].i);
            count = args[2].i;
            if (count < 0)
                throw std::runtime_error("Invalid string index on string copy operation.");
            if (static_cast<std::size_t>(pos) > s.size())
                return makeString(s);
            start = static_cast<std::size_t>(pos - 1);
            s.erase(start, static_cast<std::size_t>(count));
            return makeString(s);
        }
        if (name == "STR_INS")
        {
            std::string target;
            std::string dest;
            int location;
            std::size_t insertPos;
            if (args.size() != 3 || !isStringLike(args[0]) || !isStringLike(args[1]) || args[2].type != TYPE_INT)
                throw std::runtime_error("STR_INS expects (string, string, int).");
            target = valueAsString(args[0]);
            dest = valueAsString(args[1]);
            location = checkedInsertIndex(args[2].i);
            insertPos = static_cast<std::size_t>(location);
            if (insertPos > dest.size())
                insertPos = dest.size();
            dest.insert(insertPos, target);
            enforceStringLength(dest);
            return makeString(dest);
        }
        if (name == "REAL_I")
        {
            if (args.size() != 1 || args[0].type != TYPE_INT)
                throw std::runtime_error("REAL_I expects one integer argument.");
            return makeReal(static_cast<double>(args[0].i));
        }
        if (name == "INT_R")
        {
            if (args.size() != 1 || args[0].type != TYPE_REAL)
                throw std::runtime_error("INT_R expects one real argument.");
            if (args[0].r < static_cast<double>(std::numeric_limits<int>::min()) ||
                args[0].r > static_cast<double>(std::numeric_limits<int>::max()))
                throw std::runtime_error("Real to Integer conversion out of range.");
            return makeInt(static_cast<int>(args[0].r));
        }
        if (name == "RSTR")
        {
            char fmt[32];
            char buf[256];
            int width;
            int precision;

            if (args.size() != 3 || args[0].type != TYPE_REAL || args[1].type != TYPE_INT || args[2].type != TYPE_INT)
                throw std::runtime_error("RSTR expects (real, int, int).");

            width = args[1].i;
            precision = args[2].i;
            if (width < 0)
                width = 0;
            if (precision < 0)
                precision = 0;
            if (precision > 20)
                precision = 20;

            std::snprintf(fmt, sizeof(fmt), "%%%d.%df", width, precision);
            std::snprintf(buf, sizeof(buf), fmt, args[0].r);
            enforceStringLength(buf);
            return makeString(buf);
        }

        throw std::runtime_error("Unknown intrinsic: " + name);
    }
}

VirtualMachine::Value::Value() : type(TYPE_INT), i(0), r(0.0), s(), c(0) {}

VirtualMachine::VirtualMachine()
{
}

void VirtualMachine::push(const Value &value)
{
    stack.push_back(value);
}

VirtualMachine::Value VirtualMachine::pop()
{
    if (!stack.empty())
    {
        Value value = stack.back();
        stack.pop_back();
        return value;
    }

    log.push_back("VM Error: Stack underflow.");
    return makeInt(0);
}

void VirtualMachine::execute(const unsigned char *bytecode, size_t length)
{
    size_t ip = 0;
    std::vector<size_t> call_stack;

    variables.clear();
    stack.clear();
    log.clear();

    auto readInt = [&](int &value) {
        std::memcpy(&value, &bytecode[ip], sizeof(int));
        ip += sizeof(int);
    };

    auto readDouble = [&](double &value) {
        std::memcpy(&value, &bytecode[ip], sizeof(double));
        ip += sizeof(double);
    };

    auto readCString = [&](std::string &value) {
        const char *text = reinterpret_cast<const char *>(&bytecode[ip]);
        value = text;
        ip += value.size() + 1;
    };

    auto popArgs = [&](unsigned char count) {
        std::vector<Value> args;
        args.reserve(count);
        for (unsigned char i = 0; i < count; ++i)
            args.push_back(pop());
        std::reverse(args.begin(), args.end());
        return args;
    };

    try
    {
        while (ip < length)
        {
            unsigned char opcode = bytecode[ip++];

            if (opcode == OP_PUSH_I)
            {
                int val;
                readInt(val);
                push(makeInt(val));
                log.push_back("Push integer: " + std::to_string(val));
            }
            else if (opcode == OP_PUSH_R)
            {
                double val;
                readDouble(val);
                push(makeReal(val));
                log.push_back("Push real: " + valueAsString(makeReal(val)));
            }
            else if (opcode == OP_PUSH_S)
            {
                std::string str;
                readCString(str);
                enforceStringLength(str);
                push(makeString(str));
                log.push_back("Push string: " + str);
            }
            else if (opcode == OP_DEF_VAR)
            {
                std::string varName;
                int varType = static_cast<int>(bytecode[ip++]);
                readCString(varName);
                variables[varName] = defaultValueForType(varType);
                log.push_back("Define variable: " + varName);
            }
            else if (opcode == OP_LOAD_VAR)
            {
                std::string varName;
                readCString(varName);

                std::map<std::string, Value>::const_iterator it = variables.find(varName);
                if (it == variables.end())
                    variables[varName] = makeInt(0);

                push(variables[varName]);
                log.push_back("Load variable: " + varName);
            }
            else if (opcode == OP_STORE_VAR)
            {
                std::string varName;
                int targetType = static_cast<int>(bytecode[ip++]);
                readCString(varName);
                Value value = coerceForStore(pop(), targetType);
                if (value.type == TYPE_STR)
                    enforceStringLength(value.s);
                variables[varName] = value;
                log.push_back("Store variable: " + varName);
            }
            else if (opcode == OP_GOTO)
            {
                int target;
                readInt(target);
                if (target < 0 || static_cast<size_t>(target) >= length)
                    throw std::runtime_error("Invalid jump target in GOTO.");
                ip = static_cast<size_t>(target);
            }
            else if (opcode == OP_CALL)
            {
                int target;
                readInt(target);
                if (target < 0 || static_cast<size_t>(target) >= length)
                    throw std::runtime_error("Invalid jump target in CALL.");
                call_stack.push_back(ip);
                ip = static_cast<size_t>(target);
            }
            else if (opcode == OP_RET)
            {
                if (call_stack.empty())
                    throw std::runtime_error("RET without matching CALL.");
                ip = call_stack.back();
                call_stack.pop_back();
            }
            else if (opcode == OP_JZ)
            {
                int target;
                Value cond;
                readInt(target);
                cond = pop();
                if (cond.type != TYPE_INT)
                    throw std::runtime_error("IF/WHILE expression must be integer.");
                if (target < 0 || static_cast<size_t>(target) >= length)
                    throw std::runtime_error("Invalid jump target in JZ.");
                if (cond.i == 0)
                    ip = static_cast<size_t>(target);
            }
            else if (opcode == OP_ADD)
            {
                Value b = pop();
                Value a = pop();
                if (isStringLike(a) && isStringLike(b))
                {
                    std::string s = valueAsString(a) + valueAsString(b);
                    enforceStringLength(s);
                    push(makeString(s));
                }
                else if (isNumeric(a) && isNumeric(b))
                {
                    if (a.type == TYPE_REAL || b.type == TYPE_REAL)
                        push(makeReal(valueAsReal(a) + valueAsReal(b)));
                    else
                        push(makeInt(a.i + b.i));
                }
                else
                    throw std::runtime_error("Type mismatch or syntax error.");
            }
            else if (opcode == OP_SUB)
            {
                Value b = pop();
                Value a = pop();
                if (!isNumeric(a) || !isNumeric(b))
                    throw std::runtime_error("Type mismatch or syntax error.");
                if (a.type == TYPE_REAL || b.type == TYPE_REAL)
                    push(makeReal(valueAsReal(a) - valueAsReal(b)));
                else
                    push(makeInt(a.i - b.i));
            }
            else if (opcode == OP_MUL)
            {
                Value b = pop();
                Value a = pop();
                if (!isNumeric(a) || !isNumeric(b))
                    throw std::runtime_error("Type mismatch or syntax error.");
                if (a.type == TYPE_REAL || b.type == TYPE_REAL)
                    push(makeReal(valueAsReal(a) * valueAsReal(b)));
                else
                    push(makeInt(a.i * b.i));
            }
            else if (opcode == OP_DIV)
            {
                Value b = pop();
                Value a = pop();
                if (!isNumeric(a) || !isNumeric(b))
                    throw std::runtime_error("Type mismatch or syntax error.");
                if ((b.type == TYPE_REAL && b.r == 0.0) || (b.type == TYPE_INT && b.i == 0))
                    throw std::runtime_error("Division by zero.");
                if (a.type == TYPE_REAL || b.type == TYPE_REAL)
                    push(makeReal(valueAsReal(a) / valueAsReal(b)));
                else
                    push(makeInt(a.i / b.i));
            }
            else if (opcode == OP_MOD)
            {
                Value b = pop();
                Value a = pop();
                if (a.type != TYPE_INT || b.type != TYPE_INT)
                    throw std::runtime_error("Type mismatch or syntax error.");
                if (b.i == 0)
                    throw std::runtime_error("Modulo by zero.");
                push(makeInt(a.i % b.i));
            }
            else if (opcode == OP_NEG)
            {
                Value a = pop();
                if (!isNumeric(a))
                    throw std::runtime_error("Type mismatch or syntax error.");
                if (a.type == TYPE_REAL)
                    push(makeReal(-a.r));
                else
                    push(makeInt(-a.i));
            }
            else if (opcode == OP_CMP_EQ || opcode == OP_CMP_NE || opcode == OP_CMP_LT ||
                     opcode == OP_CMP_GT || opcode == OP_CMP_LE || opcode == OP_CMP_GE)
            {
                Value b = pop();
                Value a = pop();
                int cmp = compareValues(a, b);
                int result = 0;
                switch (opcode)
                {
                case OP_CMP_EQ: result = (cmp == 0); break;
                case OP_CMP_NE: result = (cmp != 0); break;
                case OP_CMP_LT: result = (cmp < 0); break;
                case OP_CMP_GT: result = (cmp > 0); break;
                case OP_CMP_LE: result = (cmp <= 0); break;
                case OP_CMP_GE: result = (cmp >= 0); break;
                }
                push(makeInt(result));
            }
            else if (opcode == OP_AND)
            {
                Value b = pop();
                Value a = pop();
                push(makeInt((valueAsInt(a) != 0 && valueAsInt(b) != 0) ? 1 : 0));
            }
            else if (opcode == OP_OR)
            {
                Value b = pop();
                Value a = pop();
                push(makeInt((valueAsInt(a) != 0 || valueAsInt(b) != 0) ? 1 : 0));
            }
            else if (opcode == OP_NOT)
            {
                Value a = pop();
                push(makeInt(valueAsInt(a) == 0 ? 1 : 0));
            }
            else if (opcode == OP_SHL)
            {
                Value b = pop();
                Value a = pop();
                push(makeInt(valueAsInt(a) << valueAsInt(b)));
            }
            else if (opcode == OP_SHR)
            {
                Value b = pop();
                Value a = pop();
                push(makeInt(valueAsInt(a) >> valueAsInt(b)));
            }
            else if (opcode == OP_INTRINSIC)
            {
                std::string name;
                readCString(name);
                unsigned char argc = bytecode[ip++];
                std::vector<Value> args = popArgs(argc);
                push(applyIntrinsic(name, args));
            }
            else if (opcode == OP_VAL || opcode == OP_RVAL)
            {
                std::string varName;
                Value source;
                int resultCode = 0;
                readCString(varName);
                source = pop();
                if (!isStringLike(source))
                    throw std::runtime_error("Type mismatch or syntax error.");

                std::string textValue = valueAsString(source);
                if (opcode == OP_VAL)
                {
                    int errorPos = findValErrorPosition(textValue);
                    if (errorPos == 0)
                    {
                        long long parsed = std::strtoll(textValue.c_str(), nullptr, 10);
                        if (parsed < static_cast<long long>(std::numeric_limits<int>::min()) ||
                            parsed > static_cast<long long>(std::numeric_limits<int>::max()))
                            throw std::runtime_error("Real to Integer conversion out of range.");
                        variables[varName] = makeInt(static_cast<int>(parsed));
                    }
                    else
                        resultCode = errorPos;
                }
                else
                {
                    int errorPos = findRValErrorPosition(textValue);
                    if (errorPos == 0)
                    {
                        char *endPtr = nullptr;
                        double parsed = std::strtod(textValue.c_str(), &endPtr);
                        (void) endPtr;
                        variables[varName] = makeReal(parsed);
                    }
                    else
                        resultCode = errorPos;
                }
                push(makeInt(resultCode));
            }
            else if (opcode == OP_TVCALL)
            {
                std::string funcName;
                readCString(funcName);
                unsigned char argc = bytecode[ip++];
                std::vector<Value> args = popArgs(argc);

                log.push_back("TVCALL: " + funcName + " (" + std::to_string(argc) + " params)");

                if (funcName == "MessageBox")
                {
                    if (args.empty())
                    {
                        messageBox(mfInformation | mfOKButton, "%s", "");
                    }
                    else
                    {
                        std::string text;
                        for (size_t i = 0; i < args.size(); ++i)
                        {
                            if (i != 0)
                                text += ' ';
                            text += valueAsString(args[i]);
                        }
                        messageBox(mfInformation | mfOKButton, "%s", text.c_str());
                    }
                }
                else if (funcName == "RegisterTheme")
                {
                    if (args.size() == 17 && isStringLike(args[16]))
                    {
                        MRTheme newTheme;
                        std::string targetClass = valueAsString(args[16]);
                        for (int i = 0; i < 16; ++i)
                        {
                            if (args[i].type != TYPE_INT)
                                throw std::runtime_error("RegisterTheme palette values must be integers.");
                            newTheme.paletteData[i] = static_cast<char>(args[i].i & 0xFF);
                        }
                        newTheme.paletteData[16] = '\0';
                        MRThemeRegistry::instance().registerTheme(targetClass, newTheme);
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
                throw std::runtime_error(std::string("Unknown opcode ") + hexOp);
            }
        }
    }
    catch (const std::exception &ex)
    {
        log.push_back(std::string("VM Error: ") + ex.what());
    }
}
