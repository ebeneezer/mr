#ifndef MROUTLINEFOLDPRODUCER_HPP
#define MROUTLINEFOLDPRODUCER_HPP

#include "MROutlineModel.hpp"

#include "../derivedstate/MRFoldingDerivedState.hpp"
#include "../ui/MRSyntax.hpp"
#include "../ui/MRTextBufferModel.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct MRFoldOutlineInputSnapshot {
	MRSyntaxLanguage language = MRSyntaxLanguage::PlainText;
	std::size_t documentId = 0;
	std::size_t version = 0;
	std::size_t topLine = 0;
	std::size_t bottomLine = 0;
	std::uint64_t visibleRevision = 0;
	bool complete = false;
	std::shared_ptr<const std::vector<std::string>> lineTexts;
	std::shared_ptr<const std::vector<MRFoldSpan>> spans;
	std::shared_ptr<const MRTextBufferModel::ReadSnapshot> readSnapshot;
	MROutlineRequest request;
};

bool mrBuildFoldOutlineSnapshotFromFoldState(MRSyntaxLanguage language, std::size_t documentId, std::size_t version, std::size_t topLine, std::size_t bottomLine, bool complete,
                                             const std::vector<std::string> &lineTexts, const std::vector<MRFoldSpan> &spans, const MRTextBufferModel::ReadSnapshot &readSnapshot,
                                             const MROutlineRequest &request, MROutlineSnapshot &snapshot, const std::atomic_bool *cancelFlag = nullptr);

bool mrBuildFoldOutlineSnapshot(const MRFoldOutlineInputSnapshot &input, MROutlineSnapshot &snapshot, const std::atomic_bool *cancelFlag = nullptr);

std::string mrBuildOutlineTrainingAsciiForFoldSpans(const std::vector<std::string> &lineTexts, const std::vector<MRFoldSpan> &spans, MRSyntaxLanguage language);

#endif
