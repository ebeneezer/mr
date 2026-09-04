#include "MRFileEditor.hpp"

#include <algorithm>

void MRFileEditor::setFindMarkerRanges(const std::vector<std::pair<std::size_t, std::size_t>> &ranges) {
	std::vector<MRTextBufferModel::Range> normalized;
	const std::size_t length = mBufferModel.length();

	normalized.reserve(ranges.size());
	if (length != 0) {
		for (const auto &rangePair : ranges) {
			std::size_t start = std::min(rangePair.first, length);
			std::size_t end = std::min(rangePair.second, length);
			if (end < start) std::swap(start, end);
			if (end == start) {
				if (end < length) ++end;
				else if (start > 0)
					--start;
			}
			if (end > start) normalized.push_back(MRTextBufferModel::Range(start, end));
		}
	}
	normalizeRangeList(normalized);
	mFindMarkerRanges.swap(normalized);
	mMiniMapState.setFindRanges(mFindMarkerRanges);
	drawView();
}

void MRFileEditor::clearFindMarkerRanges() {
	if (mFindMarkerRanges.empty()) return;
	mFindMarkerRanges.clear();
	mMiniMapState.setFindRanges(mFindMarkerRanges);
	drawView();
}

void MRFileEditor::adoptCompilerDiagnosticRanges(const std::shared_ptr<const std::vector<MRTextBufferModel::Range>> &errorRanges,
	                                              const std::shared_ptr<const std::vector<MRTextBufferModel::Range>> &warningRanges) {
	mCompilerErrorRanges = errorRanges != nullptr ? errorRanges : std::make_shared<const std::vector<MRTextBufferModel::Range>>();
	mCompilerWarningRanges = warningRanges != nullptr ? warningRanges : std::make_shared<const std::vector<MRTextBufferModel::Range>>();
	mMiniMapState.adoptCompilerRanges(mCompilerErrorRanges, mCompilerWarningRanges);
	drawView();
}

void MRFileEditor::clearCompilerDiagnosticRanges() {
	if ((mCompilerErrorRanges == nullptr || mCompilerErrorRanges->empty()) &&
	    (mCompilerWarningRanges == nullptr || mCompilerWarningRanges->empty()))
		return;
	adoptCompilerDiagnosticRanges(std::make_shared<const std::vector<MRTextBufferModel::Range>>(),
	                              std::make_shared<const std::vector<MRTextBufferModel::Range>>());
}

void MRFileEditor::setDebuggerBreakpointRanges(const std::vector<std::pair<std::size_t, std::size_t>> &activeRanges, const std::vector<std::pair<std::size_t, std::size_t>> &inactiveRanges, const std::vector<std::pair<std::size_t, std::size_t>> &unboundRanges,
                                              const std::vector<std::size_t> &explicitUnboundLines) {
	std::vector<DebuggerBreakpointLineMarker> activeLines;
	std::vector<DebuggerBreakpointLineMarker> inactiveLines;
	std::vector<DebuggerBreakpointLineMarker> unboundLines;
	const std::size_t length = mBufferModel.length();
	auto appendLine = [this, length](std::size_t sourceOffset, std::vector<DebuggerBreakpointLineMarker> &lines) {
		const std::size_t offset = std::min(sourceOffset, length);
		const std::size_t lineIndex = mBufferModel.lineIndex(offset);
		const std::size_t lineStart = mBufferModel.lineStart(offset);
		const std::size_t lineEnd = mBufferModel.nextLine(lineStart);
		lines.push_back(DebuggerBreakpointLineMarker{lineIndex, lineStart, lineEnd});
	};
	auto appendRanges = [&appendLine](const std::vector<std::pair<std::size_t, std::size_t>> &source, std::vector<DebuggerBreakpointLineMarker> &lines) {
		lines.reserve(source.size());
		for (const std::pair<std::size_t, std::size_t> &rangePair : source) {
			appendLine(std::min(rangePair.first, rangePair.second), lines);
		}
	};
	auto normalizeLines = [](std::vector<DebuggerBreakpointLineMarker> &lines) {
		std::sort(lines.begin(), lines.end(), [](const DebuggerBreakpointLineMarker &left, const DebuggerBreakpointLineMarker &right) { return left.lineIndex < right.lineIndex; });
		lines.erase(std::unique(lines.begin(), lines.end(), [](const DebuggerBreakpointLineMarker &left, const DebuggerBreakpointLineMarker &right) { return left.lineIndex == right.lineIndex; }), lines.end());
	};

	appendRanges(activeRanges, activeLines);
	appendRanges(inactiveRanges, inactiveLines);
	appendRanges(unboundRanges, unboundLines);
	for (const std::size_t lineIndex : explicitUnboundLines) {
		const std::size_t lineStart = mBufferModel.lineStartByIndex(lineIndex);
		appendLine(lineStart, unboundLines);
	}
	normalizeLines(activeLines);
	mDebuggerBreakpointLines.swap(activeLines);
	normalizeLines(inactiveLines);
	mDebuggerBreakpointInactiveLines.swap(inactiveLines);
	normalizeLines(unboundLines);
	mDebuggerBreakpointUnboundLines.swap(unboundLines);
	drawView();
}

void MRFileEditor::clearDebuggerBreakpointRanges() {
	if (mDebuggerBreakpointLines.empty() && mDebuggerBreakpointInactiveLines.empty() && mDebuggerBreakpointUnboundLines.empty()) return;
	mDebuggerBreakpointLines.clear();
	mDebuggerBreakpointInactiveLines.clear();
	mDebuggerBreakpointUnboundLines.clear();
	drawView();
}

std::vector<int> MRFileEditor::debuggerBreakpointLineNumbers() const {
	std::vector<int> lines;
	lines.reserve(mDebuggerBreakpointLines.size() + mDebuggerBreakpointInactiveLines.size() + mDebuggerBreakpointUnboundLines.size());
	for (const DebuggerBreakpointLineMarker &marker : mDebuggerBreakpointLines) lines.push_back(static_cast<int>(marker.lineIndex + 1));
	for (const DebuggerBreakpointLineMarker &marker : mDebuggerBreakpointInactiveLines) lines.push_back(static_cast<int>(marker.lineIndex + 1));
	for (const DebuggerBreakpointLineMarker &marker : mDebuggerBreakpointUnboundLines) lines.push_back(static_cast<int>(marker.lineIndex + 1));
	std::sort(lines.begin(), lines.end());
	lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
	return lines;
}

void MRFileEditor::setDebuggerWatchpointRanges(const std::vector<std::pair<std::size_t, std::size_t>> &activeRanges, const std::vector<std::pair<std::size_t, std::size_t>> &inactiveRanges, const std::vector<std::pair<std::size_t, std::size_t>> &errorRanges) {
	std::vector<MRTextBufferModel::Range> normalizedActive;
	std::vector<MRTextBufferModel::Range> normalizedInactive;
	std::vector<MRTextBufferModel::Range> normalizedErrors;
	const std::size_t length = mBufferModel.length();
	auto appendRanges = [length](const std::vector<std::pair<std::size_t, std::size_t>> &source, std::vector<MRTextBufferModel::Range> &target) {
		target.reserve(source.size());
		for (const std::pair<std::size_t, std::size_t> &rangePair : source) {
			std::size_t start = std::min(rangePair.first, length);
			std::size_t end = std::min(rangePair.second, length);

			if (end < start) std::swap(start, end);
			if (end == start && end < length) ++end;
			if (end > start) target.push_back(MRTextBufferModel::Range(start, end));
		}
	};

	appendRanges(activeRanges, normalizedActive);
	appendRanges(inactiveRanges, normalizedInactive);
	appendRanges(errorRanges, normalizedErrors);
	normalizeRangeList(normalizedActive);
	normalizeRangeList(normalizedInactive);
	normalizeRangeList(normalizedErrors);
	mDebuggerWatchpointActiveRanges.swap(normalizedActive);
	mDebuggerWatchpointInactiveRanges.swap(normalizedInactive);
	mDebuggerWatchpointErrorRanges.swap(normalizedErrors);
	drawView();
}

void MRFileEditor::clearDebuggerWatchpointRanges() {
	if (mDebuggerWatchpointActiveRanges.empty() && mDebuggerWatchpointInactiveRanges.empty() && mDebuggerWatchpointErrorRanges.empty()) return;
	mDebuggerWatchpointActiveRanges.clear();
	mDebuggerWatchpointInactiveRanges.clear();
	mDebuggerWatchpointErrorRanges.clear();
	drawView();
}

void MRFileEditor::setDebuggerVariableChangedRanges(const std::vector<std::pair<std::size_t, std::size_t>> &ranges) {
	std::vector<MRTextBufferModel::Range> normalized;
	const std::size_t length = mBufferModel.length();

	normalized.reserve(ranges.size());
	for (const std::pair<std::size_t, std::size_t> &rangePair : ranges) {
		std::size_t start = std::min(rangePair.first, length);
		std::size_t end = std::min(rangePair.second, length);

		if (end < start) std::swap(start, end);
		if (end == start && end < length) ++end;
		if (end > start) normalized.push_back(MRTextBufferModel::Range(start, end));
	}
	normalizeRangeList(normalized);
	mDebuggerVariableChangedRanges.swap(normalized);
	drawView();
}

void MRFileEditor::clearDebuggerVariableChangedRanges() {
	if (mDebuggerVariableChangedRanges.empty()) return;
	mDebuggerVariableChangedRanges.clear();
	drawView();
}

void MRFileEditor::setDebuggerInstructionLine(std::size_t lineIndex) {
	if (mDebuggerInstructionLineValid && mDebuggerInstructionLine == lineIndex) return;
	mDebuggerInstructionLine = lineIndex;
	mDebuggerInstructionLineValid = true;
	drawView();
}

void MRFileEditor::clearDebuggerInstructionLine() {
	if (!mDebuggerInstructionLineValid) return;
	mDebuggerInstructionLineValid = false;
	mDebuggerInstructionLine = 0;
	drawView();
}

void MRFileEditor::clearDirtyRanges() {
	mDirtyRanges.clear();
	mMiniMapState.setDirtyRanges(mDirtyRanges);
}

void MRFileEditor::normalizeRangeList(std::vector<MRTextBufferModel::Range> &ranges) {
	std::sort(ranges.begin(), ranges.end(), [](const MRTextBufferModel::Range &a, const MRTextBufferModel::Range &b) { return a.start < b.start || (a.start == b.start && a.end < b.end); });
	std::vector<MRTextBufferModel::Range> merged;
	for (const MRTextBufferModel::Range &item : ranges) {
		if (item.end <= item.start) continue;
		if (merged.empty() || item.start > merged.back().end) merged.push_back(item);
		else if (item.end > merged.back().end)
			merged.back().end = item.end;
	}
	ranges.swap(merged);
}

void MRFileEditor::normalizeDirtyRanges() {
	normalizeRangeList(mDirtyRanges);
}

void MRFileEditor::remapFindMarkerRangesForAppliedChange(const MRTextBufferModel::DocumentChangeSet &change) {
	const std::size_t oldLength = change.oldLength;
	const std::size_t newLength = change.newLength;
	const MRTextBufferModel::Range touched = change.touchedRange.normalized();
	const long long delta = static_cast<long long>(newLength) - static_cast<long long>(oldLength);
	const std::size_t editStart = std::min(touched.start, oldLength);
	std::size_t replacedOldLength = touched.length();

	if (mFindMarkerRanges.empty()) return;
	if (delta >= 0) {
		const std::size_t addedLength = static_cast<std::size_t>(delta);
		replacedOldLength = replacedOldLength > addedLength ? replacedOldLength - addedLength : 0;
	}
	if (replacedOldLength > oldLength - editStart) replacedOldLength = oldLength - editStart;
	const std::size_t oldEditEnd = editStart + replacedOldLength;
	std::vector<MRTextBufferModel::Range> mapped;
	mapped.reserve(mFindMarkerRanges.size());
	for (const MRTextBufferModel::Range &marker : mFindMarkerRanges) {
		const MRTextBufferModel::Range range = marker.clamped(oldLength).normalized();

		if (range.end <= range.start) continue;
		if (range.end <= editStart) {
			mapped.push_back(range);
			continue;
		}
		if (range.start >= oldEditEnd) {
			const long long shiftedStart = static_cast<long long>(range.start) + delta;
			const long long shiftedEnd = static_cast<long long>(range.end) + delta;
			if (shiftedEnd <= 0) continue;
			mapped.push_back(MRTextBufferModel::Range(static_cast<std::size_t>(std::max<long long>(0, shiftedStart)), std::min(static_cast<std::size_t>(shiftedEnd), newLength)));
		}
	}
	normalizeRangeList(mapped);
	mFindMarkerRanges.swap(mapped);
	mMiniMapState.setFindRanges(mFindMarkerRanges);
}

void MRFileEditor::remapDebuggerBreakpointLinesForAppliedChange(const MRTextBufferModel::DocumentChangeSet &change) {
	const std::size_t oldLength = change.oldLength;
	const std::size_t newLength = change.newLength;
	const MRTextBufferModel::Range touched = change.touchedRange.normalized();
	const long long delta = static_cast<long long>(newLength) - static_cast<long long>(oldLength);
	const std::size_t editStart = std::min(touched.start, oldLength);
	std::size_t replacedOldLength = touched.length();

	if (delta >= 0) {
		const std::size_t addedLength = static_cast<std::size_t>(delta);
		replacedOldLength = replacedOldLength > addedLength ? replacedOldLength - addedLength : 0;
	}
	if (replacedOldLength > oldLength - editStart) replacedOldLength = oldLength - editStart;
	const std::size_t oldEditEnd = editStart + replacedOldLength;
	auto remapLines = [this, newLength, editStart, oldEditEnd, replacedOldLength, delta](std::vector<DebuggerBreakpointLineMarker> &markers) {
		std::vector<DebuggerBreakpointLineMarker> mapped;

		mapped.reserve(markers.size());
		for (const DebuggerBreakpointLineMarker &marker : markers) {
			const bool sourceLineDeleted = replacedOldLength != 0 && editStart <= marker.lineStart && oldEditEnd >= marker.lineEnd && oldEditEnd > marker.lineStart;
			if (sourceLineDeleted) continue;
			long long mappedOffset = static_cast<long long>(marker.lineStart);
			if (marker.lineEnd <= editStart) {
				mappedOffset = static_cast<long long>(marker.lineStart);
			} else if (marker.lineStart >= oldEditEnd) {
				mappedOffset += delta;
			} else {
				mappedOffset = static_cast<long long>(std::min(marker.lineStart, editStart));
			}
			const std::size_t offset = static_cast<std::size_t>(std::max<long long>(0, std::min<long long>(mappedOffset, static_cast<long long>(newLength))));
			const std::size_t lineIndex = mBufferModel.lineIndex(offset);
			const std::size_t lineStart = mBufferModel.lineStart(offset);
			mapped.push_back(DebuggerBreakpointLineMarker{lineIndex, lineStart, mBufferModel.nextLine(lineStart)});
		}
		std::sort(mapped.begin(), mapped.end(), [](const DebuggerBreakpointLineMarker &left, const DebuggerBreakpointLineMarker &right) { return left.lineIndex < right.lineIndex; });
		mapped.erase(std::unique(mapped.begin(), mapped.end(), [](const DebuggerBreakpointLineMarker &left, const DebuggerBreakpointLineMarker &right) { return left.lineIndex == right.lineIndex; }), mapped.end());
		markers.swap(mapped);
	};

	remapLines(mDebuggerBreakpointLines);
	remapLines(mDebuggerBreakpointInactiveLines);
	remapLines(mDebuggerBreakpointUnboundLines);
}

void MRFileEditor::pushMappedDirtyRange(std::vector<MRTextBufferModel::Range> &mapped, std::size_t start, std::size_t end, std::size_t maxLength) {
	start = std::min(start, maxLength);
	end = std::min(end, maxLength);
	if (end <= start) return;
	mapped.push_back(MRTextBufferModel::Range(start, end));
}

void MRFileEditor::remapDirtyRangesForAppliedChange(const MRTextBufferModel::DocumentChangeSet &change) {
	const std::size_t oldLength = change.oldLength;
	const std::size_t newLength = change.newLength;
	const MRTextBufferModel::Range touched = change.touchedRange.normalized();
	const long long delta = static_cast<long long>(newLength) - static_cast<long long>(oldLength);
	const std::size_t touchedLength = touched.length();
	const std::size_t editStart = std::min(touched.start, oldLength);
	std::size_t replacedOldLength = touchedLength;

	if (mDirtyRanges.empty()) return;
	if (delta >= 0) {
		const std::size_t deltaUnsigned = static_cast<std::size_t>(delta);
		replacedOldLength = touchedLength > deltaUnsigned ? touchedLength - deltaUnsigned : 0;
	}
	if (replacedOldLength > oldLength - editStart) replacedOldLength = oldLength - editStart;
	const std::size_t oldEditEnd = editStart + replacedOldLength;

	std::vector<MRTextBufferModel::Range> mapped;
	mapped.reserve(mDirtyRanges.size() + 2);

	for (std::size_t i = 0; i < mDirtyRanges.size(); ++i) {
		MRTextBufferModel::Range range = mDirtyRanges[i].clamped(oldLength).normalized();

		if (range.end <= range.start) continue;
		if (range.end <= editStart) {
			pushMappedDirtyRange(mapped, range.start, range.end, newLength);
			continue;
		}
		if (range.start >= oldEditEnd) {
			const long long shiftedStart = static_cast<long long>(range.start) + delta;
			const long long shiftedEnd = static_cast<long long>(range.end) + delta;
			if (shiftedEnd <= 0) continue;
			pushMappedDirtyRange(mapped, static_cast<std::size_t>(std::max<long long>(0, shiftedStart)), static_cast<std::size_t>(std::max<long long>(0, shiftedEnd)), newLength);
			continue;
		}

		if (range.start < editStart) pushMappedDirtyRange(mapped, range.start, editStart, newLength);
		if (range.end > oldEditEnd) {
			const long long shiftedStart = static_cast<long long>(oldEditEnd) + delta;
			const long long shiftedEnd = static_cast<long long>(range.end) + delta;
			if (shiftedEnd > 0) pushMappedDirtyRange(mapped, static_cast<std::size_t>(std::max<long long>(0, shiftedStart)), static_cast<std::size_t>(std::max<long long>(0, shiftedEnd)), newLength);
		}
	}

	mDirtyRanges.swap(mapped);
	normalizeDirtyRanges();
}

void MRFileEditor::addDirtyRange(MRTextBufferModel::Range range) {
	if (mBufferModel.length() == 0) {
		mMiniMapState.setDirtyRanges(mDirtyRanges);
		return;
	}
	range = range.clamped(mBufferModel.length());
	range.normalize();
	if (range.empty()) {
		std::size_t point = std::min(range.start, mBufferModel.length() - 1);
		range = MRTextBufferModel::Range(point, point + 1);
	}
	mDirtyRanges.push_back(range);
	normalizeDirtyRanges();
	mMiniMapState.setDirtyRanges(mDirtyRanges);
}

bool MRFileEditor::isDirtyOffset(std::size_t pos) const noexcept {
	if (mDirtyRanges.empty() || mBufferModel.length() == 0) return false;
	if (pos >= mBufferModel.length()) return false;
	for (const MRTextBufferModel::Range &item : mDirtyRanges) {
		if (item.end <= pos) continue;
		if (item.start > pos) break;
		return pos < item.end;
	}
	return false;
}
