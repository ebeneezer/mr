#ifndef MRBENTOBOXFILECOMPAREPROJECTION_HPP
#define MRBENTOBOXFILECOMPAREPROJECTION_HPP

#include "../../coprocessor/MRCoprocessor.hpp"
#include "../MRTextBufferModel.hpp"
#include "../MRFileEditor/MRMiniMap.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct MRBentoFileCompareChangeGroup {
	MRBentoFileCompareChangeGroup() noexcept;

	std::size_t displayStartLine;
	std::size_t originalStartLine;
	std::size_t compareStartLine;
	std::size_t displayLineCount;
	std::size_t deletedLineCount;
	std::size_t insertedLineCount;
};

struct MRBentoFileCompareAcquisitionPayload final : mr::coprocessor::Payload {
	MRBentoFileCompareAcquisitionPayload() noexcept;

	std::uint64_t generation;
	bool original;
	std::size_t documentId;
	std::size_t version;
	std::shared_ptr<const std::vector<std::string>> lines;
};

struct MRBentoFileCompareDiffPayload final : mr::coprocessor::FileComparePayload {
	MRBentoFileCompareDiffPayload() noexcept;

	std::uint64_t generation;
	std::shared_ptr<const std::vector<std::string>> originalLines;
	std::shared_ptr<const std::vector<std::string>> compareLines;
	std::shared_ptr<const std::vector<MRBentoFileCompareChangeGroup>> changeGroups;
};

struct MRBentoFileComparePaneProjectionPayload final : mr::coprocessor::Payload {
	MRBentoFileComparePaneProjectionPayload() noexcept;

	std::uint64_t generation;
	bool original;
	bool editable;
	std::size_t sourceDocumentId;
	std::size_t sourceVersion;
	std::shared_ptr<const std::string> text;
	std::shared_ptr<const std::vector<unsigned char>> lineKinds;
	std::shared_ptr<const std::vector<MRFileCompareMiniMapSlice>> miniMapSlices;
};

struct MRBentoFileComparePipelineState {
	MRBentoFileComparePipelineState() noexcept;

	std::uint64_t generationCounter;
	std::uint64_t activeGeneration;
	std::uint64_t originalAcquisitionTaskId;
	std::uint64_t compareAcquisitionTaskId;
	std::uint64_t diffTaskId;
	std::uint64_t originalProjectionTaskId;
	std::uint64_t compareProjectionTaskId;
	std::shared_ptr<const MRBentoFileCompareAcquisitionPayload> originalAcquisition;
	std::shared_ptr<const MRBentoFileCompareAcquisitionPayload> compareAcquisition;
	std::shared_ptr<const MRBentoFileCompareDiffPayload> diff;
	std::shared_ptr<const MRBentoFileComparePaneProjectionPayload> originalProjection;
	std::shared_ptr<const MRBentoFileComparePaneProjectionPayload> compareProjection;
	std::size_t originalTargetDocumentId;
	std::size_t originalTargetVersion;
	std::size_t compareTargetDocumentId;
	std::size_t compareTargetVersion;
};

std::shared_ptr<const MRBentoFileCompareAcquisitionPayload> mrBuildBentoFileCompareAcquisition(
	const MRTextBufferModel::ReadSnapshot &snapshot, std::uint64_t generation, bool original,
	const std::atomic_bool *cancelFlag);

std::shared_ptr<const MRBentoFileCompareDiffPayload> mrBuildBentoFileCompareDiff(
	const std::shared_ptr<const MRBentoFileCompareAcquisitionPayload> &original,
	const std::shared_ptr<const MRBentoFileCompareAcquisitionPayload> &compare,
	const std::atomic_bool *cancelFlag, std::string &errorText);

std::shared_ptr<const MRBentoFileComparePaneProjectionPayload> mrBuildBentoFileComparePaneProjection(
	const std::shared_ptr<const MRBentoFileCompareDiffPayload> &diff, bool original, bool editable,
	const std::atomic_bool *cancelFlag);

#endif
