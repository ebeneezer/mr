#include "MRVMRuntimeDebugger.hpp"

#include "MRVMHash.hpp"
#include "MRVMRuntimeCatalog.hpp"
#include "MRVMValue.hpp"

#include "../mrmac.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <limits>

namespace {
using Value = VirtualMachine::Value;

bool parseUint64Text(const std::string &text, std::uint64_t &value) {
	value = 0;
	if (text.empty()) return false;
	for (std::size_t index = 0; index < text.size(); ++index) {
		const unsigned char ch = static_cast<unsigned char>(text[index]);
		std::uint64_t digit;
		if (ch < static_cast<unsigned char>('0') || ch > static_cast<unsigned char>('9')) return false;
		digit = static_cast<std::uint64_t>(ch - static_cast<unsigned char>('0'));
		if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
		value = value * 10 + digit;
	}
	return true;
}

std::uint64_t valueAsUint64(const Value &value, std::uint64_t fallback) {
	std::uint64_t parsed = 0;

	if (value.type == TYPE_INT) return value.i > 0 ? static_cast<std::uint64_t>(value.i) : 0;
	if (value.type == TYPE_STR && parseUint64Text(value.s, parsed)) return parsed;
	return fallback;
}

Value ensureDebuggerHash(MRVMRuntimeKv &runtimeKv) {
	return runtimeKv.ensureRoot("MACRODEBUGGER");
}

bool findDebuggerHash(MRVMRuntimeKv &runtimeKv, Value &root) {
	return runtimeKv.findRoot("MACRODEBUGGER", root);
}

Value ensureDebuggerChildPath(MRVMRuntimeKv &runtimeKv, std::initializer_list<std::string> keys) {
	Value current = ensureDebuggerHash(runtimeKv);

	for (const std::string &key : keys)
		current = runtimeKv.ensureChild(current, key);
	return current;
}

bool findDebuggerChildPath(MRVMRuntimeKv &runtimeKv, std::initializer_list<std::string> keys, Value &child) {
	Value current;

	if (!findDebuggerHash(runtimeKv, current)) return false;
	for (const std::string &key : keys) {
		if (!runtimeKv.findChild(current, key, child)) return false;
		current = child;
	}
	return true;
}

void hashWriteInt(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, int value) {
	MRVMHashStore &store = runtimeKv.globalStore();

	mrvmHashWriteValue(store, store, hash, key, mrvmMakeInt(value));
}

void hashWriteUint(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, std::uint64_t value) {
	MRVMHashStore &store = runtimeKv.globalStore();

	mrvmHashWriteValue(store, store, hash, key, mrvmMakeString(std::to_string(value)));
}

void hashWriteString(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, const std::string &value) {
	MRVMHashStore &store = runtimeKv.globalStore();

	mrvmHashWriteValue(store, store, hash, key, mrvmMakeString(value));
}

int hashReadInt(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, int fallback) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, hash, key)) return fallback;
	return mrvmValueAsInt(mrvmHashReadValue(store, store, hash, key));
}

std::uint64_t hashReadUint(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, std::uint64_t fallback) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, hash, key)) return fallback;
	return valueAsUint64(mrvmHashReadValue(store, store, hash, key), fallback);
}

std::string hashReadString(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, hash, key)) return std::string();
	return mrvmValueAsString(mrvmHashReadValue(store, store, hash, key));
}

void writeBreakpointHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRMacroDebuggerBreakpoint &breakpoint) {
	hashWriteString(runtimeKv, hash, "macroKey", breakpoint.macroKey);
	hashWriteInt(runtimeKv, hash, "enabled", breakpoint.enabled ? 1 : 0);
	hashWriteInt(runtimeKv, hash, "line", breakpoint.line);
	hashWriteUint(runtimeKv, hash, "sourceStartOffset", breakpoint.sourceStartOffset);
	hashWriteUint(runtimeKv, hash, "sourceEndOffset", breakpoint.sourceEndOffset);
	hashWriteUint(runtimeKv, hash, "bytecodeOffset", breakpoint.bytecodeOffset);
	hashWriteInt(runtimeKv, hash, "debuggableKind", breakpoint.debuggableKind);
	hashWriteString(runtimeKv, hash, "conditionText", breakpoint.conditionText);
}

bool readBreakpointHash(MRVMRuntimeKv &runtimeKv, const Value &hash, MRMacroDebuggerBreakpoint &breakpoint) {
	breakpoint.macroKey = hashReadString(runtimeKv, hash, "macroKey");
	breakpoint.enabled = hashReadInt(runtimeKv, hash, "enabled", 0) != 0;
	breakpoint.line = hashReadInt(runtimeKv, hash, "line", 0);
	breakpoint.sourceStartOffset = static_cast<std::size_t>(hashReadUint(runtimeKv, hash, "sourceStartOffset", 0));
	breakpoint.sourceEndOffset = static_cast<std::size_t>(hashReadUint(runtimeKv, hash, "sourceEndOffset", breakpoint.sourceStartOffset));
	breakpoint.bytecodeOffset = static_cast<std::size_t>(hashReadUint(runtimeKv, hash, "bytecodeOffset", 0));
	breakpoint.debuggableKind = hashReadInt(runtimeKv, hash, "debuggableKind", 0);
	breakpoint.conditionText = hashReadString(runtimeKv, hash, "conditionText");
	return !breakpoint.macroKey.empty() && breakpoint.line > 0;
}

void writeWatchHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRMacroDebuggerWatch &watch) {
	hashWriteString(runtimeKv, hash, "macroKey", watch.macroKey);
	hashWriteString(runtimeKv, hash, "expression", watch.expression);
	hashWriteInt(runtimeKv, hash, "enabled", watch.enabled ? 1 : 0);
}

bool readWatchHash(MRVMRuntimeKv &runtimeKv, const Value &hash, MRMacroDebuggerWatch &watch) {
	watch.macroKey = hashReadString(runtimeKv, hash, "macroKey");
	watch.expression = hashReadString(runtimeKv, hash, "expression");
	watch.enabled = hashReadInt(runtimeKv, hash, "enabled", 0) != 0;
	return !watch.macroKey.empty() && !watch.expression.empty();
}

std::string lineKey(int line) {
	return std::to_string(line);
}

bool resolveLineBreakpointLocation(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, int line, MRMacroSourceMapEntry &span, std::string &actualMacroKey) {
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	actualMacroKey.clear();
	if (normalizedMacroKey.empty() || line <= 0) return false;
	if (!mrvmRuntimeCatalogFirstSourceMapSpanForLine(runtimeKv, normalizedMacroKey, line, span)) return false;
	actualMacroKey = mrvmUpperKey(span.macroName);
	return !actualMacroKey.empty();
}

} // namespace

MRMacroDebuggerBreakpoint::MRMacroDebuggerBreakpoint() : macroKey(), enabled(false), line(0), sourceStartOffset(0), sourceEndOffset(0), bytecodeOffset(0), debuggableKind(0), conditionText() {
}

MRMacroDebuggerWatch::MRMacroDebuggerWatch() : macroKey(), expression(), enabled(false) {
}

bool mrvmRuntimeDebuggerWriteLineBreakpoint(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, int line, bool enabled, const std::string &conditionText) {
	MRMacroSourceMapEntry span;
	MRMacroDebuggerBreakpoint breakpoint;
	std::string actualMacroKey;
	Value byLine;

	if (!resolveLineBreakpointLocation(runtimeKv, macroKey, line, span, actualMacroKey)) return false;

	breakpoint.macroKey = actualMacroKey;
	breakpoint.enabled = enabled;
	breakpoint.line = line;
	breakpoint.sourceStartOffset = span.sourceStartOffset;
	breakpoint.sourceEndOffset = span.sourceEndOffset;
	breakpoint.bytecodeOffset = span.bytecodeOffset;
	breakpoint.debuggableKind = span.debuggableKind;
	breakpoint.conditionText = conditionText;

	byLine = ensureDebuggerChildPath(runtimeKv, {"breakpoints", "byMacro", actualMacroKey, "byLine"});
	writeBreakpointHash(runtimeKv, runtimeKv.replaceChild(byLine, lineKey(line)), breakpoint);
	return true;
}

bool mrvmRuntimeDebuggerReadLineBreakpoint(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, int line, MRMacroDebuggerBreakpoint &breakpoint) {
	MRMacroSourceMapEntry span;
	Value byLine;
	Value breakpointHash;
	std::string actualMacroKey;

	if (!resolveLineBreakpointLocation(runtimeKv, macroKey, line, span, actualMacroKey)) return false;
	if (!findDebuggerChildPath(runtimeKv, {"breakpoints", "byMacro", actualMacroKey, "byLine"}, byLine)) return false;
	if (!runtimeKv.findChild(byLine, lineKey(line), breakpointHash)) return false;
	return readBreakpointHash(runtimeKv, breakpointHash, breakpoint);
}

bool mrvmRuntimeDebuggerEraseLineBreakpoint(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, int line) {
	MRMacroSourceMapEntry span;
	Value byLine;
	std::string actualMacroKey;

	if (!resolveLineBreakpointLocation(runtimeKv, macroKey, line, span, actualMacroKey)) return false;
	if (!findDebuggerChildPath(runtimeKv, {"breakpoints", "byMacro", actualMacroKey, "byLine"}, byLine)) return false;
	return runtimeKv.eraseChild(byLine, lineKey(line));
}

bool mrvmRuntimeDebuggerLineBreakpointsForMacro(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, std::vector<MRMacroDebuggerBreakpoint> &breakpoints) {
	Value byLine;
	std::vector<std::string> keys;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	breakpoints.clear();
	if (normalizedMacroKey.empty()) return false;
	if (!findDebuggerChildPath(runtimeKv, {"breakpoints", "byMacro", normalizedMacroKey, "byLine"}, byLine)) return false;

	keys = runtimeKv.globalStore().keys(byLine.hashHandle);
	for (const std::string &key : keys) {
		Value breakpointHash;
		MRMacroDebuggerBreakpoint breakpoint;

		if (!runtimeKv.findChild(byLine, key, breakpointHash)) continue;
		if (!readBreakpointHash(runtimeKv, breakpointHash, breakpoint)) continue;
		breakpoints.push_back(breakpoint);
	}
	std::sort(breakpoints.begin(), breakpoints.end(), [](const MRMacroDebuggerBreakpoint &left, const MRMacroDebuggerBreakpoint &right) {
		if (left.line != right.line) return left.line < right.line;
		return left.bytecodeOffset < right.bytecodeOffset;
	});
	return true;
}

bool mrvmRuntimeDebuggerSetLineBreakpointsEnabledForMacro(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, bool enabled) {
	Value byLine;
	std::vector<std::string> keys;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	if (normalizedMacroKey.empty()) return false;
	if (!findDebuggerChildPath(runtimeKv, {"breakpoints", "byMacro", normalizedMacroKey, "byLine"}, byLine)) return true;
	keys = runtimeKv.globalStore().keys(byLine.hashHandle);
	for (const std::string &key : keys) {
		Value breakpointHash;
		MRMacroDebuggerBreakpoint breakpoint;

		if (!runtimeKv.findChild(byLine, key, breakpointHash)) continue;
		if (!readBreakpointHash(runtimeKv, breakpointHash, breakpoint)) continue;
		breakpoint.enabled = enabled;
		writeBreakpointHash(runtimeKv, breakpointHash, breakpoint);
	}
	return true;
}

bool mrvmRuntimeDebuggerEraseLineBreakpointsForMacro(MRVMRuntimeKv &runtimeKv, const std::string &macroKey) {
	Value byMacro;
	Value byLine;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	if (normalizedMacroKey.empty()) return false;
	if (!findDebuggerChildPath(runtimeKv, {"breakpoints", "byMacro"}, byMacro)) return true;
	if (!runtimeKv.findChild(byMacro, normalizedMacroKey, byLine)) return true;
	return runtimeKv.eraseChild(byMacro, normalizedMacroKey);
}

bool mrvmRuntimeDebuggerEnabledBreakpointOffsetsForMacro(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, std::vector<std::size_t> &bytecodeOffsets) {
	std::vector<MRMacroDebuggerBreakpoint> breakpoints;

	bytecodeOffsets.clear();
	if (!mrvmRuntimeDebuggerLineBreakpointsForMacro(runtimeKv, macroKey, breakpoints)) return false;
	for (const MRMacroDebuggerBreakpoint &breakpoint : breakpoints)
		if (breakpoint.enabled) bytecodeOffsets.push_back(breakpoint.bytecodeOffset);
	std::sort(bytecodeOffsets.begin(), bytecodeOffsets.end());
	bytecodeOffsets.erase(std::unique(bytecodeOffsets.begin(), bytecodeOffsets.end()), bytecodeOffsets.end());
	return true;
}

bool mrvmRuntimeDebuggerWriteWatch(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, const std::string &expression, bool enabled) {
	MRMacroDebuggerWatch watch;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);
	Value byExpression;

	if (normalizedMacroKey.empty() || expression.empty()) return false;
	watch.macroKey = normalizedMacroKey;
	watch.expression = expression;
	watch.enabled = enabled;
	byExpression = ensureDebuggerChildPath(runtimeKv, {"watches", "byMacro", normalizedMacroKey, "byExpression"});
	writeWatchHash(runtimeKv, runtimeKv.replaceChild(byExpression, expression), watch);
	return true;
}

bool mrvmRuntimeDebuggerEraseWatch(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, const std::string &expression) {
	Value byExpression;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	if (normalizedMacroKey.empty() || expression.empty()) return false;
	if (!findDebuggerChildPath(runtimeKv, {"watches", "byMacro", normalizedMacroKey, "byExpression"}, byExpression)) return false;
	return runtimeKv.eraseChild(byExpression, expression);
}

bool mrvmRuntimeDebuggerWatchesForMacro(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, std::vector<MRMacroDebuggerWatch> &watches) {
	Value byExpression;
	std::vector<std::string> keys;
	const std::string normalizedMacroKey = mrvmUpperKey(macroKey);

	watches.clear();
	if (normalizedMacroKey.empty()) return false;
	if (!findDebuggerChildPath(runtimeKv, {"watches", "byMacro", normalizedMacroKey, "byExpression"}, byExpression)) return false;
	keys = runtimeKv.globalStore().keys(byExpression.hashHandle);
	for (const std::string &key : keys) {
		Value watchHash;
		MRMacroDebuggerWatch watch;

		if (!runtimeKv.findChild(byExpression, key, watchHash)) continue;
		if (!readWatchHash(runtimeKv, watchHash, watch)) continue;
		watches.push_back(watch);
	}
	std::sort(watches.begin(), watches.end(), [](const MRMacroDebuggerWatch &left, const MRMacroDebuggerWatch &right) {
		return left.expression < right.expression;
	});
	return true;
}
