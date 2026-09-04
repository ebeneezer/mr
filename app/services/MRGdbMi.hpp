#ifndef MRGDBMI_HPP
#define MRGDBMI_HPP

#include <cstddef>
#include <string>
#include <vector>

enum class MRGdbMiRecordKind : unsigned char { Result, Exec, Status, Notify, Console, Target, Log, Prompt, Unknown };

struct MRGdbMiRecord {
	MRGdbMiRecord() noexcept;
	MRGdbMiRecordKind kind;
	unsigned token;
	std::string resultClass;
	std::string text;
	std::string raw;
};

struct MRGdbMiVariable {
	std::string name;
	std::string value;
	std::string type;
	std::string objectName;
	std::string parentObjectName;
	int depth = 0;
	int childCount = 0;
};

struct MRGdbMiBreakpoint {
	std::string number;
	std::string file;
	int line;
};

class MRGdbMiStream {
  public:
	void append(const char *data, std::size_t size, std::vector<MRGdbMiRecord> &records);
	void finish(std::vector<MRGdbMiRecord> &records);

  private:
	std::string pending;
};

[[nodiscard]] std::string mrGdbMiQuote(const std::string &value);
[[nodiscard]] std::string mrGdbMiField(const std::string &record, const char *name);
[[nodiscard]] int mrGdbMiIntField(const std::string &record, const char *name, int fallback = 0);
void mrGdbMiVariables(const std::string &record, std::vector<MRGdbMiVariable> &variables);
void mrGdbMiChildren(const std::string &record, const std::string &parentObjectName, int depth, std::vector<MRGdbMiVariable> &variables);
void mrGdbMiChanges(const std::string &record, std::vector<MRGdbMiVariable> &variables);
void mrGdbMiBreakpoints(const std::string &record, std::vector<MRGdbMiBreakpoint> &breakpoints);

#endif
