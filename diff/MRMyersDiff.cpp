#include "MRDiff.hpp"

#include <algorithm>
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

void appendLeadingEqualEdits(std::vector<MRElementEdit> &edits, int &x, int &y) {
	while (x > 0 && y > 0) {
		--x;
		--y;
		edits.push_back(MRElementEdit(mr::diff::MRDiffOp::Equal, x, y));
	}
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

	if (leftLines.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) || rightLines.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
		setError(errorText, "Diff input is too large for the Myers index range.");
		return false;
	}

	const int leftCount = static_cast<int>(leftLines.size());
	const int rightCount = static_cast<int>(rightLines.size());
	const int maxDistance = leftCount + rightCount;
	const int offset = maxDistance + 1;
	const std::size_t vectorSize = static_cast<std::size_t>(2 * maxDistance + 3);

	std::vector<int> v(vectorSize, 0);
	std::vector<std::vector<int>> trace;
	trace.reserve(static_cast<std::size_t>(maxDistance + 1));
	v[vectorIndex(1, offset)] = 0;

	for (int distance = 0; distance <= maxDistance; ++distance) {
		if (cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire)) {
			setError(errorText, "Diff computation was cancelled.");
			return false;
		}

		for (int diagonal = -distance; diagonal <= distance; diagonal += 2) {
			int x = 0;

			if (diagonal == -distance || (diagonal != distance && v[vectorIndex(diagonal - 1, offset)] < v[vectorIndex(diagonal + 1, offset)])) {
				x = v[vectorIndex(diagonal + 1, offset)];
			} else {
				x = v[vectorIndex(diagonal - 1, offset)] + 1;
			}

			int y = x - diagonal;
			while (x < leftCount && y < rightCount && leftLines[static_cast<std::size_t>(x)] == rightLines[static_cast<std::size_t>(y)]) {
				++x;
				++y;
			}
			v[vectorIndex(diagonal, offset)] = x;

			if (x >= leftCount && y >= rightCount) {
				std::vector<MRElementEdit> edits;
				trace.push_back(v);

				int backX = leftCount;
				int backY = rightCount;
				for (int backDistance = static_cast<int>(trace.size()) - 1; backDistance > 0; --backDistance) {
					const std::vector<int> &previous = trace[static_cast<std::size_t>(backDistance - 1)];
					const int backDiagonal = backX - backY;
					int previousDiagonal = 0;

					if (backDiagonal == -backDistance || (backDiagonal != backDistance && previous[vectorIndex(backDiagonal - 1, offset)] < previous[vectorIndex(backDiagonal + 1, offset)])) {
						previousDiagonal = backDiagonal + 1;
					} else {
						previousDiagonal = backDiagonal - 1;
					}

					const int previousX = previous[vectorIndex(previousDiagonal, offset)];
					const int previousY = previousX - previousDiagonal;

					while (backX > previousX && backY > previousY) {
						--backX;
						--backY;
						edits.push_back(MRElementEdit(MRDiffOp::Equal, backX, backY));
					}

					if (backX == previousX) {
						--backY;
						edits.push_back(MRElementEdit(MRDiffOp::Insert, backX, backY));
					} else {
						--backX;
						edits.push_back(MRElementEdit(MRDiffOp::Delete, backX, backY));
					}
				}

				appendLeadingEqualEdits(edits, backX, backY);
				std::reverse(edits.begin(), edits.end());
				hunks.reserve(edits.size());
				for (std::size_t i = 0; i < edits.size(); ++i)
					appendElementEdit(hunks, edits[i]);
				return true;
			}
		}
		trace.push_back(v);
	}

	setError(errorText, "Diff computation did not reach an edit script.");
	return false;
}

} // namespace diff
} // namespace mr
