#include "MRDiff.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace {

struct MRElementEdit {
	mr::diff::MRDiffOp op;
	int leftIndex;
	int rightIndex;

	MRElementEdit() noexcept : op(mr::diff::MRDiffOp::Equal), leftIndex(0), rightIndex(0) {
	}

	MRElementEdit(mr::diff::MRDiffOp aOp, int aLeftIndex, int aRightIndex) noexcept : op(aOp), leftIndex(aLeftIndex), rightIndex(aRightIndex) {
	}
};

std::size_t vectorIndex(int k, int offset) noexcept {
	return static_cast<std::size_t>(k + offset);
}

void setError(std::string *errorText, const char *text) {
	if (errorText != nullptr) *errorText = text;
}

bool hunkCanExtend(const mr::diff::MRDiffHunk &hunk, const MRElementEdit &edit) noexcept {
	const std::size_t leftIndex = edit.leftIndex >= 0 ? static_cast<std::size_t>(edit.leftIndex) : 0;
	const std::size_t rightIndex = edit.rightIndex >= 0 ? static_cast<std::size_t>(edit.rightIndex) : 0;

	if (hunk.op != edit.op) return false;
	switch (edit.op) {
		case mr::diff::MRDiffOp::Equal:
			return leftIndex == hunk.leftStart + hunk.count && rightIndex == hunk.rightStart + hunk.count;
		case mr::diff::MRDiffOp::Delete:
			return leftIndex == hunk.leftStart + hunk.count && rightIndex == hunk.rightStart;
		case mr::diff::MRDiffOp::Insert:
			return leftIndex == hunk.leftStart && rightIndex == hunk.rightStart + hunk.count;
		default:
			break;
	}
	return false;
}

void appendElementEdit(std::vector<mr::diff::MRDiffHunk> &hunks, const MRElementEdit &edit) {
	const std::size_t leftIndex = edit.leftIndex >= 0 ? static_cast<std::size_t>(edit.leftIndex) : 0;
	const std::size_t rightIndex = edit.rightIndex >= 0 ? static_cast<std::size_t>(edit.rightIndex) : 0;

	if (!hunks.empty() && hunkCanExtend(hunks.back(), edit)) {
		hunks.back().count += 1;
		return;
	}
	hunks.push_back(mr::diff::MRDiffHunk(edit.op, leftIndex, rightIndex, 1));
}

bool findMiddleSplit(const std::vector<std::string> &leftLines, const std::vector<std::string> &rightLines,
	                 int leftStart, int leftEnd, int rightStart, int rightEnd,
	                 std::vector<int> &forward, std::vector<int> &reverse,
	                 int &splitLeft, int &splitRight, std::string *errorText,
	                 const std::atomic_bool *cancelFlag) {
	const int leftCount = leftEnd - leftStart;
	const int rightCount = rightEnd - rightStart;
	const int maxDistance = (leftCount + rightCount + 1) / 2;
	const int offset = maxDistance + 1;
	const std::size_t vectorSize = static_cast<std::size_t>(2) * static_cast<std::size_t>(maxDistance) + 3;
	if (forward.size() < vectorSize) {
		forward.resize(vectorSize);
		reverse.resize(vectorSize);
	}
	std::fill(forward.begin(), forward.begin() + static_cast<std::ptrdiff_t>(vectorSize), -1);
	std::fill(reverse.begin(), reverse.begin() + static_cast<std::ptrdiff_t>(vectorSize), -1);
	forward[vectorIndex(1, offset)] = 0;
	reverse[vectorIndex(1, offset)] = 0;

	const int delta = leftCount - rightCount;
	const bool oddDelta = (delta % 2) != 0;
	for (int distance = 0; distance <= maxDistance; ++distance) {
		if (cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire)) {
			setError(errorText, "Diff computation was cancelled.");
			return false;
		}

		for (int diagonal = -distance; diagonal <= distance; diagonal += 2) {
			const int down = forward[vectorIndex(diagonal + 1, offset)];
			const int right = forward[vectorIndex(diagonal - 1, offset)];
			int x = -1;
			if (diagonal == -distance || (diagonal != distance && (right < 0 || (down >= 0 && right < down))))
				x = down;
			else if (right >= 0)
				x = right + 1;
			if (x < 0) {
				forward[vectorIndex(diagonal, offset)] = -1;
				continue;
			}
			int y = x - diagonal;
			if (x > leftCount || y < 0 || y > rightCount) {
				forward[vectorIndex(diagonal, offset)] = -1;
				continue;
			}
			while (x < leftCount && y < rightCount &&
			       leftLines[static_cast<std::size_t>(leftStart + x)] == rightLines[static_cast<std::size_t>(rightStart + y)]) {
				++x;
				++y;
			}
			forward[vectorIndex(diagonal, offset)] = x;

			if (oddDelta && distance > 0) {
				const std::int64_t reverseDiagonal = static_cast<std::int64_t>(delta) - diagonal;
				if (reverseDiagonal >= 1 - distance && reverseDiagonal <= distance - 1 &&
				    reverse[vectorIndex(static_cast<int>(reverseDiagonal), offset)] >= 0 &&
				    x >= leftCount - reverse[vectorIndex(static_cast<int>(reverseDiagonal), offset)]) {
					splitLeft = leftStart + x;
					splitRight = rightStart + y;
					return true;
				}
			}
		}

		for (int diagonal = -distance; diagonal <= distance; diagonal += 2) {
			const int down = reverse[vectorIndex(diagonal + 1, offset)];
			const int right = reverse[vectorIndex(diagonal - 1, offset)];
			int x = -1;
			if (diagonal == -distance || (diagonal != distance && (right < 0 || (down >= 0 && right < down))))
				x = down;
			else if (right >= 0)
				x = right + 1;
			if (x < 0) {
				reverse[vectorIndex(diagonal, offset)] = -1;
				continue;
			}
			int y = x - diagonal;
			if (x > leftCount || y < 0 || y > rightCount) {
				reverse[vectorIndex(diagonal, offset)] = -1;
				continue;
			}
			while (x < leftCount && y < rightCount &&
			       leftLines[static_cast<std::size_t>(leftEnd - x - 1)] == rightLines[static_cast<std::size_t>(rightEnd - y - 1)]) {
				++x;
				++y;
			}
			reverse[vectorIndex(diagonal, offset)] = x;

			if (!oddDelta) {
				const std::int64_t forwardDiagonal = static_cast<std::int64_t>(delta) - diagonal;
				if (forwardDiagonal >= -distance && forwardDiagonal <= distance &&
				    forward[vectorIndex(static_cast<int>(forwardDiagonal), offset)] >= 0 &&
				    forward[vectorIndex(static_cast<int>(forwardDiagonal), offset)] >= leftCount - x) {
					splitLeft = leftStart + forward[vectorIndex(static_cast<int>(forwardDiagonal), offset)];
					splitRight = rightStart + splitLeft - leftStart - static_cast<int>(forwardDiagonal);
					return true;
				}
			}
		}
	}

	setError(errorText, "Diff computation did not reach an edit script.");
	return false;
}

bool appendDiffRange(const std::vector<std::string> &leftLines, const std::vector<std::string> &rightLines,
	                 int leftStart, int leftEnd, int rightStart, int rightEnd,
	                 std::vector<int> &forward, std::vector<int> &reverse,
	                 std::vector<mr::diff::MRDiffHunk> &hunks, std::string *errorText,
	                 const std::atomic_bool *cancelFlag) {
	if (cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire)) {
		setError(errorText, "Diff computation was cancelled.");
		return false;
	}

	while (leftStart < leftEnd && rightStart < rightEnd &&
	       leftLines[static_cast<std::size_t>(leftStart)] == rightLines[static_cast<std::size_t>(rightStart)]) {
		appendElementEdit(hunks, MRElementEdit(mr::diff::MRDiffOp::Equal, leftStart, rightStart));
		++leftStart;
		++rightStart;
	}

	int suffixCount = 0;
	while (leftStart < leftEnd - suffixCount && rightStart < rightEnd - suffixCount &&
	       leftLines[static_cast<std::size_t>(leftEnd - suffixCount - 1)] ==
	           rightLines[static_cast<std::size_t>(rightEnd - suffixCount - 1)])
		++suffixCount;
	leftEnd -= suffixCount;
	rightEnd -= suffixCount;

	if (leftStart == leftEnd) {
		for (int rightIndex = rightStart; rightIndex < rightEnd; ++rightIndex)
			appendElementEdit(hunks, MRElementEdit(mr::diff::MRDiffOp::Insert, leftStart, rightIndex));
	} else if (rightStart == rightEnd) {
		for (int leftIndex = leftStart; leftIndex < leftEnd; ++leftIndex)
			appendElementEdit(hunks, MRElementEdit(mr::diff::MRDiffOp::Delete, leftIndex, rightStart));
	} else if (leftEnd - leftStart == 1) {
		int match = rightEnd;
		for (int rightIndex = rightStart; rightIndex < rightEnd; ++rightIndex) {
			if (leftLines[static_cast<std::size_t>(leftStart)] == rightLines[static_cast<std::size_t>(rightIndex)]) {
				match = rightIndex;
				break;
			}
		}
		if (match == rightEnd) {
			appendElementEdit(hunks, MRElementEdit(mr::diff::MRDiffOp::Delete, leftStart, rightStart));
			for (int rightIndex = rightStart; rightIndex < rightEnd; ++rightIndex)
				appendElementEdit(hunks, MRElementEdit(mr::diff::MRDiffOp::Insert, leftEnd, rightIndex));
		} else {
			for (int rightIndex = rightStart; rightIndex < match; ++rightIndex)
				appendElementEdit(hunks, MRElementEdit(mr::diff::MRDiffOp::Insert, leftStart, rightIndex));
			appendElementEdit(hunks, MRElementEdit(mr::diff::MRDiffOp::Equal, leftStart, match));
			for (int rightIndex = match + 1; rightIndex < rightEnd; ++rightIndex)
				appendElementEdit(hunks, MRElementEdit(mr::diff::MRDiffOp::Insert, leftEnd, rightIndex));
		}
	} else if (rightEnd - rightStart == 1) {
		int match = leftEnd;
		for (int leftIndex = leftStart; leftIndex < leftEnd; ++leftIndex) {
			if (leftLines[static_cast<std::size_t>(leftIndex)] == rightLines[static_cast<std::size_t>(rightStart)]) {
				match = leftIndex;
				break;
			}
		}
		if (match == leftEnd) {
			for (int leftIndex = leftStart; leftIndex < leftEnd; ++leftIndex)
				appendElementEdit(hunks, MRElementEdit(mr::diff::MRDiffOp::Delete, leftIndex, rightStart));
			appendElementEdit(hunks, MRElementEdit(mr::diff::MRDiffOp::Insert, leftEnd, rightStart));
		} else {
			for (int leftIndex = leftStart; leftIndex < match; ++leftIndex)
				appendElementEdit(hunks, MRElementEdit(mr::diff::MRDiffOp::Delete, leftIndex, rightStart));
			appendElementEdit(hunks, MRElementEdit(mr::diff::MRDiffOp::Equal, match, rightStart));
			for (int leftIndex = match + 1; leftIndex < leftEnd; ++leftIndex)
				appendElementEdit(hunks, MRElementEdit(mr::diff::MRDiffOp::Delete, leftIndex, rightEnd));
		}
	} else {
		int splitLeft = 0;
		int splitRight = 0;
		if (!findMiddleSplit(leftLines, rightLines, leftStart, leftEnd, rightStart, rightEnd,
		                     forward, reverse, splitLeft, splitRight, errorText, cancelFlag)) return false;
		if ((splitLeft == leftStart && splitRight == rightStart) ||
		    (splitLeft == leftEnd && splitRight == rightEnd)) {
			setError(errorText, "Diff computation did not make progress.");
			return false;
		}
		if (!appendDiffRange(leftLines, rightLines, leftStart, splitLeft, rightStart, splitRight,
		                     forward, reverse, hunks, errorText, cancelFlag)) return false;
		if (!appendDiffRange(leftLines, rightLines, splitLeft, leftEnd, splitRight, rightEnd,
		                     forward, reverse, hunks, errorText, cancelFlag)) return false;
	}

	for (int index = 0; index < suffixCount; ++index)
		appendElementEdit(hunks, MRElementEdit(mr::diff::MRDiffOp::Equal, leftEnd + index, rightEnd + index));
	return true;
}

} // namespace

namespace mr {
namespace diff {

void mrSplitTextLinesForDiff(std::string_view text, std::vector<std::string> &lines) {
	lines.clear();
	std::size_t start = 0;

	while (start < text.size()) {
		std::size_t end = text.find('\n', start);
		if (end == std::string_view::npos) end = text.size();
		std::size_t lineEnd = end;
		if (lineEnd > start && text[lineEnd - 1] == '\r') --lineEnd;
		lines.push_back(std::string(text.substr(start, lineEnd - start)));
		start = end < text.size() ? end + 1 : text.size();
	}
	if (text.empty()) return;
	if (!text.empty() && text[text.size() - 1] == '\n') lines.push_back(std::string());
}

bool mrComputeMyersDiff(const std::vector<std::string> &leftLines, const std::vector<std::string> &rightLines, std::vector<MRDiffHunk> &hunks, std::string *errorText, const std::atomic_bool *cancelFlag) {
	hunks.clear();
	if (errorText != nullptr) errorText->clear();

	const std::size_t maxInputLines = static_cast<std::size_t>(std::numeric_limits<int>::max() - 3);
	if (leftLines.size() > maxInputLines || rightLines.size() > maxInputLines - leftLines.size()) {
		setError(errorText, "Diff input is too large for the Myers index range.");
		return false;
	}

	std::vector<int> forward;
	std::vector<int> reverse;
	const bool success = appendDiffRange(leftLines, rightLines, 0, static_cast<int>(leftLines.size()),
	                                    0, static_cast<int>(rightLines.size()), forward, reverse,
	                                    hunks, errorText, cancelFlag);
	if (!success) hunks.clear();
	return success;
}

} // namespace diff
} // namespace mr
