#ifndef MRVM_HPP
#define MRVM_HPP

#include <cstddef>
#include <atomic>
#include <map>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

#include "MRTextDocument.hpp"

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
	bool verboseLogging;
	bool logTruncated;

	void appendLogLine(const std::string &line, bool important = false);

	void push(const Value &value);
	Value pop();

 public:
	std::vector<std::string> log;
	bool cancelledExecution;

	VirtualMachine();
	void setVerboseLogging(bool enable) noexcept {
		verboseLogging = enable;
	}
	void execute(const unsigned char *bytecode, size_t length);
	void executeAt(const unsigned char *bytecode, size_t length, size_t entryOffset,
	               const std::string &parameterString, const std::string &macroName,
	               bool resetState, bool firstRun);
	bool wasCancelled() const noexcept {
		return cancelledExecution;
	}
};

	enum MRMacroExecutionFlags {
		mrefBackgroundSafe = 1u << 0,
		mrefStagedWrite = 1u << 1,
		mrefUiAffinity = 1u << 2,
		mrefExternalIo = 1u << 3
	};

	struct MRMacroExecutionProfile {
		unsigned flags;
		std::size_t opcodeCount;
		std::size_t intrinsicCount;
		std::size_t procCount;
		std::size_t procVarCount;
		std::size_t tvCallCount;
		std::vector<std::string> stagedWriteSymbols;
		std::vector<std::string> uiAffinitySymbols;
		std::vector<std::string> externalIoSymbols;

		MRMacroExecutionProfile() noexcept
		    : flags(0), opcodeCount(0), intrinsicCount(0), procCount(0), procVarCount(0),
		      tvCallCount(0), stagedWriteSymbols(), uiAffinitySymbols(), externalIoSymbols() {
		}

		bool has(unsigned mask) const noexcept {
			return (flags & mask) != 0;
		}
	};

void mrvmSetProcessContext(int argc, char **argv);
MRMacroExecutionProfile mrvmAnalyzeBytecode(const unsigned char *bytecode, std::size_t length);
std::string mrvmDescribeExecutionProfile(const MRMacroExecutionProfile &profile);
bool mrvmCanRunInBackground(const MRMacroExecutionProfile &profile) noexcept;
bool mrvmCanRunStagedInBackground(const MRMacroExecutionProfile &profile) noexcept;

struct MRMacroJobResult {
	std::vector<std::string> logLines;
	bool hadError;
	bool cancelled;

	MRMacroJobResult() noexcept : logLines(), hadError(false), cancelled(false) {
	}
};

MRMacroJobResult mrvmRunBytecodeBackground(const unsigned char *bytecode, std::size_t length,
                                           std::stop_token stopToken = std::stop_token(),
                                           std::shared_ptr<std::atomic_bool> cancelFlag = nullptr);

struct MRMacroStagedExecutionInput {
	mr::editor::TextDocument document;
	std::size_t baseVersion;
	std::size_t cursorOffset;
	std::size_t selectionStart;
	std::size_t selectionEnd;
	bool insertMode;
	int indentLevel;
	int pageLines;
	std::string fileName;
	bool fileChanged;

	MRMacroStagedExecutionInput() noexcept
	    : document(), baseVersion(0), cursorOffset(0), selectionStart(0), selectionEnd(0),
	      insertMode(true), indentLevel(1), pageLines(20), fileName(), fileChanged(false) {
	}
};

struct MRMacroStagedJobResult {
	std::vector<std::string> logLines;
	bool hadError;
	bool cancelled;
	mr::editor::StagedEditTransaction transaction;
	std::size_t cursorOffset;
	std::size_t selectionStart;
	std::size_t selectionEnd;
	bool insertMode;
	int indentLevel;
	std::string fileName;
	bool fileChanged;

	MRMacroStagedJobResult() noexcept
	    : logLines(), hadError(false), cancelled(false), transaction(), cursorOffset(0), selectionStart(0),
	      selectionEnd(0), insertMode(true), indentLevel(1), fileName(), fileChanged(false) {
	}
};

MRMacroStagedJobResult mrvmRunBytecodeStagedBackground(const unsigned char *bytecode,
                                                       std::size_t length,
                                                       const MRMacroStagedExecutionInput &input,
                                                       std::stop_token stopToken = std::stop_token(),
                                                       std::shared_ptr<std::atomic_bool> cancelFlag = nullptr);

bool mrvmUiLinkCurrentWindow();
bool mrvmUiUnlinkCurrentWindow();
bool mrvmUiZoomCurrentWindow();
bool mrvmUiRedrawCurrentWindow();
bool mrvmUiNewScreen();

#endif
