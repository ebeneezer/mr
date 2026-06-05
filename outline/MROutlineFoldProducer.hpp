#ifndef MROUTLINEFOLDPRODUCER_HPP
#define MROUTLINEFOLDPRODUCER_HPP

#include "MROutlineModel.hpp"

#include "../derivedstate/MRFoldingDerivedState.hpp"
#include "../ui/MRSyntax.hpp"
#include "../ui/MRTextBufferModel.hpp"

#include <cstddef>
#include <string>
#include <vector>

bool mrBuildFoldOutlineSnapshotFromFoldState(MRSyntaxLanguage language, std::size_t documentId, std::size_t version, std::size_t topLine, std::size_t bottomLine, bool complete,
                                             const std::vector<std::string> &lineTexts, const std::vector<MRFoldSpan> &spans, const MRTextBufferModel::ReadSnapshot &readSnapshot,
                                             const MROutlineRequest &request, MROutlineSnapshot &snapshot);

std::string mrBuildOutlineTrainingAsciiForFoldSpans(const std::vector<std::string> &lineTexts, const std::vector<MRFoldSpan> &spans, MRSyntaxLanguage language);

#endif
