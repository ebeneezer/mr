#ifndef MRDIFF_HPP
#define MRDIFF_HPP

#include <cstddef>
#include <atomic>
#include <string>
#include <string_view>
#include <vector>

namespace mr {
namespace diff {

enum class MRDiffOp : unsigned char {
	Equal,
	Delete,
	Insert
};

struct MRDiffHunk {
	MRDiffOp op;
	std::size_t leftStart;
	std::size_t rightStart;
	std::size_t count;

	MRDiffHunk() noexcept : op(MRDiffOp::Equal), leftStart(0), rightStart(0), count(0) {
	}

	MRDiffHunk(MRDiffOp aOp, std::size_t aLeftStart, std::size_t aRightStart, std::size_t aCount) noexcept : op(aOp), leftStart(aLeftStart), rightStart(aRightStart), count(aCount) {
	}
};

bool mrComputeMyersDiff(const std::vector<std::string> &leftLines, const std::vector<std::string> &rightLines, std::vector<MRDiffHunk> &hunks, std::string *errorText = nullptr, const std::atomic_bool *cancelFlag = nullptr);
void mrSplitTextLinesForDiff(std::string_view text, std::vector<std::string> &lines);

} // namespace diff
} // namespace mr

#endif
