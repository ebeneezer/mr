#ifndef MRDERIVEDSTATEBASE_HPP
#define MRDERIVEDSTATEBASE_HPP

#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

class MRDerivedStateBase {
  public:
	using LineRange = std::pair<std::size_t, std::size_t>;

	MRDerivedStateBase() noexcept;
	virtual ~MRDerivedStateBase();

	void resetBaseState() noexcept;
	void setBaseDocument(std::size_t documentId, std::size_t version) noexcept;

	std::size_t baseDocumentId() const noexcept;
	std::size_t baseVersion() const noexcept;

	void clearValidRanges() noexcept;
	void clearInvalidRanges() noexcept;
	void clearAllRanges() noexcept;

	void rememberValidRange(std::size_t startLine, std::size_t endLine) noexcept;
	void rememberInvalidRange(std::size_t startLine, std::size_t endLine) noexcept;
	void invalidateValidRangesFrom(std::size_t lineIndex) noexcept;
	bool validRangeCovered(std::size_t startLine, std::size_t endLine) const noexcept;

	const std::vector<LineRange> &validRanges() const noexcept;
	const std::vector<LineRange> &invalidRanges() const noexcept;

  protected:
	static void normalizeRanges(std::vector<LineRange> &ranges);

  private:
	std::size_t mDocumentId;
	std::size_t mVersion;
	std::vector<LineRange> mValidRanges;
	std::vector<LineRange> mInvalidRanges;
};

#endif
