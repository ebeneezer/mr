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
	drawView();
}

void MRFileEditor::clearFindMarkerRanges() {
	if (mFindMarkerRanges.empty()) return;
	mFindMarkerRanges.clear();
	drawView();
}

void MRFileEditor::setCompilerDiagnosticRanges(const std::vector<std::pair<std::size_t, std::size_t>> &errorRanges, const std::vector<std::pair<std::size_t, std::size_t>> &warningRanges) {
	std::vector<MRTextBufferModel::Range> normalizedErrors;
	std::vector<MRTextBufferModel::Range> normalizedWarnings;
	const std::size_t length = mBufferModel.length();
	auto appendRanges = [length](const std::vector<std::pair<std::size_t, std::size_t>> &source, std::vector<MRTextBufferModel::Range> &target) {
		target.reserve(source.size());
		if (length == 0) return;
		for (const auto &rangePair : source) {
			std::size_t start = std::min(rangePair.first, length);
			std::size_t end = std::min(rangePair.second, length);
			if (end < start) std::swap(start, end);
			if (end == start) {
				if (end < length) ++end;
				else if (start > 0)
					--start;
			}
			if (end > start) target.push_back(MRTextBufferModel::Range(start, end));
		}
	};

	appendRanges(errorRanges, normalizedErrors);
	appendRanges(warningRanges, normalizedWarnings);
	normalizeRangeList(normalizedErrors);
	normalizeRangeList(normalizedWarnings);
	mCompilerErrorRanges.swap(normalizedErrors);
	mCompilerWarningRanges.swap(normalizedWarnings);
	drawView();
}

void MRFileEditor::clearCompilerDiagnosticRanges() {
	if (mCompilerErrorRanges.empty() && mCompilerWarningRanges.empty()) return;
	mCompilerErrorRanges.clear();
	mCompilerWarningRanges.clear();
	drawView();
}

void MRFileEditor::setLspDiagnosticInformationRanges(const std::vector<std::pair<std::size_t, std::size_t>> &ranges) {
	std::vector<MRTextBufferModel::Range> normalized;
	const std::size_t length = mBufferModel.length();

	normalized.reserve(ranges.size());
	if (length != 0) {
		for (const auto &rangePair : ranges) {
			std::size_t start = std::min(rangePair.first, length);
			std::size_t end = std::min(rangePair.second, length);
			if (end < start) std::swap(start, end);
			if (end == start) {
				if (start > 0 && (start == length || mBufferModel.charAt(start) == '\n' || mBufferModel.charAt(start) == '\r'))
					--start;
				else if (end < length)
					++end;
			}
			if (end > start) normalized.push_back(MRTextBufferModel::Range(start, end));
		}
	}
	normalizeRangeList(normalized);
	if (mLspDiagnosticInformationRanges.size() == normalized.size()) {
		bool unchanged = true;

		for (std::size_t index = 0; index < normalized.size(); ++index) {
			if (mLspDiagnosticInformationRanges[index].start == normalized[index].start && mLspDiagnosticInformationRanges[index].end == normalized[index].end) continue;
			unchanged = false;
			break;
		}
		if (unchanged) return;
	}
	mLspDiagnosticInformationRanges.swap(normalized);
	drawView();
}

void MRFileEditor::clearLspDiagnosticInformationRanges() {
	if (mLspDiagnosticInformationRanges.empty()) return;
	mLspDiagnosticInformationRanges.clear();
	drawView();
}

void MRFileEditor::clearDirtyRanges() noexcept {
	mDirtyRanges.clear();
}

void MRFileEditor::normalizePairRangeList(std::vector<std::pair<std::size_t, std::size_t>> &ranges) {
	std::sort(ranges.begin(), ranges.end(), [](const std::pair<std::size_t, std::size_t> &a, const std::pair<std::size_t, std::size_t> &b) { return a.first < b.first || (a.first == b.first && a.second < b.second); });
	std::vector<std::pair<std::size_t, std::size_t>> merged;
	for (const auto &item : ranges) {
		if (item.second <= item.first) continue;
		if (merged.empty() || item.first > merged.back().second) merged.push_back(item);
		else if (item.second > merged.back().second)
			merged.back().second = item.second;
	}
	ranges.swap(merged);
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

void MRFileEditor::remapLspDiagnosticInformationRangesForAppliedChange(const MRTextBufferModel::DocumentChangeSet &change) {
	const std::size_t oldLength = change.oldLength;
	const std::size_t newLength = change.newLength;
	const MRTextBufferModel::Range touched = change.touchedRange.normalized();
	const long long delta = static_cast<long long>(newLength) - static_cast<long long>(oldLength);
	const std::size_t touchedLength = touched.length();
	const std::size_t editStart = std::min(touched.start, oldLength);
	std::size_t replacedOldLength = touchedLength;
	std::vector<MRTextBufferModel::Range> mapped;

	if (mLspDiagnosticInformationRanges.empty()) return;
	if (delta >= 0) {
		const std::size_t deltaUnsigned = static_cast<std::size_t>(delta);

		replacedOldLength = touchedLength > deltaUnsigned ? touchedLength - deltaUnsigned : 0;
	}
	if (replacedOldLength > oldLength - editStart) replacedOldLength = oldLength - editStart;
	const std::size_t oldEditEnd = editStart + replacedOldLength;

	mapped.reserve(mLspDiagnosticInformationRanges.size());
	for (std::size_t i = 0; i < mLspDiagnosticInformationRanges.size(); ++i) {
		MRTextBufferModel::Range range = mLspDiagnosticInformationRanges[i].clamped(oldLength).normalized();

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
		}
	}
	mLspDiagnosticInformationRanges.swap(mapped);
	normalizeRangeList(mLspDiagnosticInformationRanges);
}

void MRFileEditor::addDirtyRange(MRTextBufferModel::Range range) {
	if (mBufferModel.length() == 0) return;
	range = range.clamped(mBufferModel.length());
	range.normalize();
	if (range.empty()) {
		std::size_t point = std::min(range.start, mBufferModel.length() - 1);
		range = MRTextBufferModel::Range(point, point + 1);
	}
	mDirtyRanges.push_back(range);
	normalizeDirtyRanges();
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
