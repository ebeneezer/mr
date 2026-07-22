#include "MRBentoBoxFileCompareProjection.hpp"

#include "../../diff/MRDiff.hpp"

#include <algorithm>
#include <utility>

namespace {

bool projectionCancelled(const std::atomic_bool *cancelFlag) noexcept {
	return cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire);
}

bool hunkCanExtend(const mr::diff::MRDiffHunk &hunk, mr::diff::MRDiffOp op,
	               std::size_t leftStart, std::size_t rightStart) noexcept {
	if (hunk.op != op) return false;
	switch (op) {
		case mr::diff::MRDiffOp::Equal:
			return leftStart == hunk.leftStart + hunk.count && rightStart == hunk.rightStart + hunk.count;
		case mr::diff::MRDiffOp::Delete:
			return leftStart == hunk.leftStart + hunk.count && rightStart == hunk.rightStart;
		case mr::diff::MRDiffOp::Insert:
			return leftStart == hunk.leftStart && rightStart == hunk.rightStart + hunk.count;
		default:
			break;
	}
	return false;
}

void appendHunk(std::vector<mr::diff::MRDiffHunk> &hunks, mr::diff::MRDiffOp op,
	            std::size_t leftStart, std::size_t rightStart, std::size_t count) {
	if (count == 0) return;
	if (!hunks.empty() && hunkCanExtend(hunks.back(), op, leftStart, rightStart)) {
		hunks.back().count += count;
		return;
	}
	hunks.push_back(mr::diff::MRDiffHunk(op, leftStart, rightStart, count));
}

void appendNormalizedChangeGroup(std::vector<mr::diff::MRDiffHunk> &hunks,
	                             const std::vector<std::string> &originalLines,
	                             const std::vector<std::string> &compareLines,
	                             std::size_t originalStart, std::size_t compareStart,
	                             std::size_t deletedLineCount, std::size_t insertedLineCount) {
	if (deletedLineCount == 0) {
		appendHunk(hunks, mr::diff::MRDiffOp::Insert, originalStart, compareStart, insertedLineCount);
		return;
	}
	if (insertedLineCount == 0) {
		appendHunk(hunks, mr::diff::MRDiffOp::Delete, originalStart, compareStart, deletedLineCount);
		return;
	}

	const std::size_t maxLineCount = std::max(deletedLineCount, insertedLineCount);
	std::size_t runStart = 0;
	std::size_t index = 0;
	while (index < maxLineCount) {
		const bool equalPair = index < deletedLineCount && index < insertedLineCount &&
		                       originalStart + index < originalLines.size() && compareStart + index < compareLines.size() &&
		                       originalLines[originalStart + index] == compareLines[compareStart + index];
		if (!equalPair) {
			++index;
			continue;
		}

		if (runStart < index) {
			const std::size_t deletedRunCount = runStart < deletedLineCount ? std::min(index, deletedLineCount) - runStart : 0;
			const std::size_t insertedRunCount = runStart < insertedLineCount ? std::min(index, insertedLineCount) - runStart : 0;
			appendHunk(hunks, mr::diff::MRDiffOp::Delete, originalStart + runStart,
			           compareStart + std::min(runStart, insertedLineCount), deletedRunCount);
			appendHunk(hunks, mr::diff::MRDiffOp::Insert,
			           originalStart + std::min(runStart + deletedRunCount, deletedLineCount),
			           compareStart + runStart, insertedRunCount);
		}
		appendHunk(hunks, mr::diff::MRDiffOp::Equal, originalStart + index, compareStart + index, 1);
		++index;
		runStart = index;
	}
	if (runStart < maxLineCount) {
		const std::size_t deletedRunCount = runStart < deletedLineCount ? deletedLineCount - runStart : 0;
		const std::size_t insertedRunCount = runStart < insertedLineCount ? insertedLineCount - runStart : 0;
		appendHunk(hunks, mr::diff::MRDiffOp::Delete, originalStart + runStart,
		           compareStart + std::min(runStart, insertedLineCount), deletedRunCount);
		appendHunk(hunks, mr::diff::MRDiffOp::Insert,
		           originalStart + std::min(runStart + deletedRunCount, deletedLineCount),
		           compareStart + runStart, insertedRunCount);
	}
}

void normalizeHunks(const std::vector<std::string> &originalLines,
	                const std::vector<std::string> &compareLines,
	                std::vector<mr::diff::MRDiffHunk> &hunks) {
	std::vector<mr::diff::MRDiffHunk> normalized;
	bool groupOpen = false;
	std::size_t originalStart = 0;
	std::size_t compareStart = 0;
	std::size_t deletedLineCount = 0;
	std::size_t insertedLineCount = 0;

	normalized.reserve(hunks.size());
	for (const mr::diff::MRDiffHunk &hunk : hunks) {
		if (hunk.op == mr::diff::MRDiffOp::Equal && groupOpen) {
			appendNormalizedChangeGroup(normalized, originalLines, compareLines, originalStart, compareStart,
			                            deletedLineCount, insertedLineCount);
			groupOpen = false;
			deletedLineCount = 0;
			insertedLineCount = 0;
		}
		switch (hunk.op) {
			case mr::diff::MRDiffOp::Equal:
				appendHunk(normalized, hunk.op, hunk.leftStart, hunk.rightStart, hunk.count);
				break;
			case mr::diff::MRDiffOp::Delete:
			case mr::diff::MRDiffOp::Insert:
				if (!groupOpen) {
					groupOpen = true;
					originalStart = hunk.leftStart;
					compareStart = hunk.rightStart;
				}
				if (hunk.op == mr::diff::MRDiffOp::Delete)
					deletedLineCount += hunk.count;
				else
					insertedLineCount += hunk.count;
				break;
			default:
				break;
		}
	}
	if (groupOpen)
		appendNormalizedChangeGroup(normalized, originalLines, compareLines, originalStart, compareStart,
		                            deletedLineCount, insertedLineCount);
	hunks.swap(normalized);
}

std::shared_ptr<const std::vector<MRBentoFileCompareChangeGroup>> buildChangeGroups(
	const std::vector<mr::diff::MRDiffHunk> &hunks) {
	std::shared_ptr<std::vector<MRBentoFileCompareChangeGroup>> groups =
		std::make_shared<std::vector<MRBentoFileCompareChangeGroup>>();
	std::size_t displayLine = 0;
	bool groupOpen = false;

	for (const mr::diff::MRDiffHunk &hunk : hunks) {
		switch (hunk.op) {
			case mr::diff::MRDiffOp::Equal:
				displayLine += hunk.count;
				groupOpen = false;
				break;
			case mr::diff::MRDiffOp::Delete:
			case mr::diff::MRDiffOp::Insert:
				if (!groupOpen) {
					groups->push_back(MRBentoFileCompareChangeGroup());
					groups->back().displayStartLine = displayLine;
					groups->back().originalStartLine = hunk.leftStart;
					groups->back().compareStartLine = hunk.rightStart;
					groupOpen = true;
				}
				groups->back().displayLineCount += hunk.count;
				if (hunk.op == mr::diff::MRDiffOp::Delete)
					groups->back().deletedLineCount += hunk.count;
				else
					groups->back().insertedLineCount += hunk.count;
				displayLine += hunk.count;
				break;
			default:
				break;
		}
	}
	return groups;
}

void appendDisplayLine(std::string &text, std::vector<unsigned char> &lineKinds,
	                   const std::string &line, unsigned char lineKind) {
	if (!lineKinds.empty()) text.push_back('\n');
	text += line;
	lineKinds.push_back(lineKind);
}

void buildReadOnlyProjection(const MRBentoFileCompareDiffPayload &diff, bool original,
	                         const std::atomic_bool *cancelFlag, std::string &text,
	                         std::vector<unsigned char> &lineKinds) {
	const std::vector<std::string> &sourceLines = original ? *diff.originalLines : *diff.compareLines;

	for (const mr::diff::MRDiffHunk &hunk : diff.hunks) {
		if (projectionCancelled(cancelFlag)) return;
		switch (hunk.op) {
			case mr::diff::MRDiffOp::Equal:
			case mr::diff::MRDiffOp::Delete:
				if (!original && hunk.op == mr::diff::MRDiffOp::Delete) {
					for (std::size_t index = 0; index < hunk.count; ++index)
						appendDisplayLine(text, lineKinds, std::string(), mrfclkOffset);
					break;
				}
				for (std::size_t index = 0; index < hunk.count; ++index) {
					const std::size_t lineIndex = original ? hunk.leftStart + index : hunk.rightStart + index;
					if (lineIndex < sourceLines.size())
						appendDisplayLine(text, lineKinds, sourceLines[lineIndex],
						                  hunk.op == mr::diff::MRDiffOp::Equal ? mrfclkEqual : mrfclkMissing);
				}
				break;
			case mr::diff::MRDiffOp::Insert:
				if (original) {
					for (std::size_t index = 0; index < hunk.count; ++index)
						appendDisplayLine(text, lineKinds, std::string(), mrfclkOffset);
					break;
				}
				for (std::size_t index = 0; index < hunk.count; ++index) {
					const std::size_t lineIndex = hunk.rightStart + index;
					if (lineIndex < sourceLines.size())
						appendDisplayLine(text, lineKinds, sourceLines[lineIndex], mrfclkInsert);
				}
				break;
			default:
				break;
		}
	}
}

void markLineRange(std::vector<unsigned char> &lineKinds, std::size_t startLine,
	               std::size_t lineCount, unsigned char lineKind) {
	for (std::size_t index = 0; index < lineCount && startLine + index < lineKinds.size(); ++index)
		lineKinds[startLine + index] = lineKind;
}

void markAnchorLine(std::vector<unsigned char> &lineKinds, std::size_t lineIndex,
	                unsigned char lineKind) {
	if (lineKinds.empty()) return;
	lineKinds[std::min(lineIndex, lineKinds.size() - 1)] = lineKind;
}

void appendFullSlice(std::vector<MRFileCompareMiniMapSlice> &slices,
	                 const std::vector<unsigned char> &lineKinds, std::size_t lineIndex,
	                 unsigned char lineKind) {
	if (lineIndex >= lineKinds.size()) return;
	slices.push_back(MRFileCompareMiniMapSlice{lineIndex, 0, 0, lineKind, true});
}

void appendChangedSlice(std::vector<MRFileCompareMiniMapSlice> &slices,
	                    const std::vector<unsigned char> &lineKinds, std::size_t lineIndex,
	                    const std::string &baseLine, const std::string &changedLine,
	                    unsigned char lineKind) {
	if (lineIndex >= lineKinds.size()) return;
	std::size_t prefix = 0;
	const std::size_t commonLimit = std::min(baseLine.size(), changedLine.size());
	while (prefix < commonLimit && baseLine[prefix] == changedLine[prefix]) ++prefix;
	std::size_t suffix = 0;
	while (suffix < commonLimit - prefix &&
	       baseLine[baseLine.size() - 1 - suffix] == changedLine[changedLine.size() - 1 - suffix])
		++suffix;
	std::size_t sliceStart = std::min(prefix, changedLine.size());
	std::size_t sliceEnd = changedLine.size() >= suffix ? changedLine.size() - suffix : changedLine.size();
	if (sliceEnd <= sliceStart && !changedLine.empty()) {
		sliceStart = sliceStart >= changedLine.size() ? changedLine.size() - 1 : sliceStart;
		sliceEnd = sliceStart + 1;
	}
	slices.push_back(MRFileCompareMiniMapSlice{lineIndex, sliceStart, sliceEnd, lineKind, changedLine.empty()});
}

void buildEditableProjection(const MRBentoFileCompareDiffPayload &diff, bool original,
	                         const std::atomic_bool *cancelFlag,
	                         std::vector<unsigned char> &lineKinds,
	                         std::vector<MRFileCompareMiniMapSlice> &slices) {
	const std::vector<std::string> &originalLines = *diff.originalLines;
	const std::vector<std::string> &compareLines = *diff.compareLines;
	lineKinds.assign(original ? originalLines.size() : compareLines.size(), mrfclkEqual);

	for (const MRBentoFileCompareChangeGroup &group : *diff.changeGroups) {
		if (projectionCancelled(cancelFlag)) return;
		const bool replaceGroup = group.deletedLineCount > 0 && group.insertedLineCount > 0;
		if (original) {
			if (group.deletedLineCount > 0) {
				markLineRange(lineKinds, group.originalStartLine, group.deletedLineCount, mrfclkMissing);
				for (std::size_t index = 0; index < group.deletedLineCount && group.originalStartLine + index < lineKinds.size(); ++index) {
					const std::size_t originalIndex = group.originalStartLine + index;
					if (replaceGroup && index < group.insertedLineCount && originalIndex < originalLines.size() &&
					    group.compareStartLine + index < compareLines.size())
						appendChangedSlice(slices, lineKinds, originalIndex, compareLines[group.compareStartLine + index],
						                   originalLines[originalIndex], mrfclkMissing);
					else
						appendFullSlice(slices, lineKinds, originalIndex, mrfclkMissing);
				}
			} else if (group.insertedLineCount > 0) {
				markAnchorLine(lineKinds, group.originalStartLine, mrfclkInsert);
				appendFullSlice(slices, lineKinds,
				                std::min(group.originalStartLine, lineKinds.empty() ? 0 : lineKinds.size() - 1), mrfclkInsert);
			}
			continue;
		}

		if (replaceGroup) {
			for (std::size_t index = 0; index < group.insertedLineCount && group.compareStartLine + index < lineKinds.size(); ++index) {
				const std::size_t originalLength = group.originalStartLine + index < originalLines.size() ? originalLines[group.originalStartLine + index].size() : 0;
				const std::size_t compareLength = group.compareStartLine + index < compareLines.size() ? compareLines[group.compareStartLine + index].size() : 0;
				const unsigned char lineKind = compareLength < originalLength ? mrfclkMissing : mrfclkInsert;
				lineKinds[group.compareStartLine + index] = lineKind;
				if (index < group.deletedLineCount && group.originalStartLine + index < originalLines.size() &&
				    group.compareStartLine + index < compareLines.size())
					appendChangedSlice(slices, lineKinds, group.compareStartLine + index,
					                   originalLines[group.originalStartLine + index], compareLines[group.compareStartLine + index], lineKind);
				else
					appendFullSlice(slices, lineKinds, group.compareStartLine + index, lineKind);
			}
			if (group.deletedLineCount > group.insertedLineCount) {
				const std::size_t anchorLine = std::min(group.compareStartLine + group.insertedLineCount,
				                                        lineKinds.empty() ? 0 : lineKinds.size() - 1);
				markAnchorLine(lineKinds, group.compareStartLine + group.insertedLineCount, mrfclkMissing);
				appendFullSlice(slices, lineKinds, anchorLine, mrfclkMissing);
			}
		} else if (group.insertedLineCount > 0) {
			markLineRange(lineKinds, group.compareStartLine, group.insertedLineCount, mrfclkInsert);
			for (std::size_t index = 0; index < group.insertedLineCount && group.compareStartLine + index < lineKinds.size(); ++index)
				appendFullSlice(slices, lineKinds, group.compareStartLine + index, mrfclkInsert);
		} else if (group.deletedLineCount > 0) {
			markAnchorLine(lineKinds, group.compareStartLine, mrfclkMissing);
			appendFullSlice(slices, lineKinds,
			                std::min(group.compareStartLine, lineKinds.empty() ? 0 : lineKinds.size() - 1), mrfclkMissing);
		}
	}
}

} // namespace

MRBentoFileCompareChangeGroup::MRBentoFileCompareChangeGroup() noexcept
	: displayStartLine(0), originalStartLine(0), compareStartLine(0), displayLineCount(0),
	  deletedLineCount(0), insertedLineCount(0) {
}

MRBentoFileCompareAcquisitionPayload::MRBentoFileCompareAcquisitionPayload() noexcept
	: generation(0), original(false), documentId(0), version(0),
	  lines(std::make_shared<const std::vector<std::string>>()) {
}

MRBentoFileCompareDiffPayload::MRBentoFileCompareDiffPayload() noexcept
	: mr::coprocessor::FileComparePayload(), generation(0),
	  originalLines(std::make_shared<const std::vector<std::string>>()),
	  compareLines(std::make_shared<const std::vector<std::string>>()),
	  changeGroups(std::make_shared<const std::vector<MRBentoFileCompareChangeGroup>>()) {
}

MRBentoFileComparePaneProjectionPayload::MRBentoFileComparePaneProjectionPayload() noexcept
	: generation(0), original(false), editable(false), sourceDocumentId(0), sourceVersion(0),
	  text(std::make_shared<const std::string>()),
	  lineKinds(std::make_shared<const std::vector<unsigned char>>()),
	  miniMapSlices(std::make_shared<const std::vector<MRFileCompareMiniMapSlice>>()) {
}

MRBentoFileComparePipelineState::MRBentoFileComparePipelineState() noexcept
	: generationCounter(1), activeGeneration(0), originalAcquisitionTaskId(0), compareAcquisitionTaskId(0),
	  diffTaskId(0), originalProjectionTaskId(0), compareProjectionTaskId(0), originalAcquisition(),
	  compareAcquisition(), diff(), originalProjection(), compareProjection(), originalTargetDocumentId(0),
	  originalTargetVersion(0), compareTargetDocumentId(0), compareTargetVersion(0) {
}

std::shared_ptr<const MRBentoFileCompareAcquisitionPayload> mrBuildBentoFileCompareAcquisition(
	const MRTextBufferModel::ReadSnapshot &snapshot, std::uint64_t generation, bool original,
	const std::atomic_bool *cancelFlag) {
	if (projectionCancelled(cancelFlag)) return std::shared_ptr<const MRBentoFileCompareAcquisitionPayload>();
	std::string text = snapshot.text();
	if (projectionCancelled(cancelFlag)) return std::shared_ptr<const MRBentoFileCompareAcquisitionPayload>();
	std::shared_ptr<std::vector<std::string>> lines = std::make_shared<std::vector<std::string>>();
	mr::diff::mrSplitTextLinesForDiff(text, *lines);
	if (projectionCancelled(cancelFlag)) return std::shared_ptr<const MRBentoFileCompareAcquisitionPayload>();

	std::shared_ptr<MRBentoFileCompareAcquisitionPayload> payload =
		std::make_shared<MRBentoFileCompareAcquisitionPayload>();
	payload->generation = generation;
	payload->original = original;
	payload->documentId = snapshot.documentId();
	payload->version = snapshot.version();
	payload->lines = lines;
	return payload;
}

std::shared_ptr<const MRBentoFileCompareDiffPayload> mrBuildBentoFileCompareDiff(
	const std::shared_ptr<const MRBentoFileCompareAcquisitionPayload> &original,
	const std::shared_ptr<const MRBentoFileCompareAcquisitionPayload> &compare,
	const std::atomic_bool *cancelFlag, std::string &errorText) {
	if (original == nullptr || compare == nullptr || original->lines == nullptr || compare->lines == nullptr) {
		errorText = "File compare acquisition is incomplete.";
		return std::shared_ptr<const MRBentoFileCompareDiffPayload>();
	}
	if (projectionCancelled(cancelFlag)) return std::shared_ptr<const MRBentoFileCompareDiffPayload>();

	std::vector<mr::diff::MRDiffHunk> hunks;
	if (!mr::diff::mrComputeMyersDiff(*original->lines, *compare->lines, hunks, &errorText, cancelFlag))
		return std::shared_ptr<const MRBentoFileCompareDiffPayload>();
	if (projectionCancelled(cancelFlag)) return std::shared_ptr<const MRBentoFileCompareDiffPayload>();
	normalizeHunks(*original->lines, *compare->lines, hunks);
	if (projectionCancelled(cancelFlag)) return std::shared_ptr<const MRBentoFileCompareDiffPayload>();

	std::shared_ptr<MRBentoFileCompareDiffPayload> payload = std::make_shared<MRBentoFileCompareDiffPayload>();
	payload->generation = original->generation;
	payload->originalDocumentId = original->documentId;
	payload->originalBaseVersion = original->version;
	payload->compareDocumentId = compare->documentId;
	payload->compareBaseVersion = compare->version;
	payload->originalLineCount = original->lines->size();
	payload->compareLineCount = compare->lines->size();
	payload->hunks = std::move(hunks);
	payload->originalLines = original->lines;
	payload->compareLines = compare->lines;
	payload->changeGroups = buildChangeGroups(payload->hunks);
	return payload;
}

std::shared_ptr<const MRBentoFileComparePaneProjectionPayload> mrBuildBentoFileComparePaneProjection(
	const std::shared_ptr<const MRBentoFileCompareDiffPayload> &diff, bool original, bool editable,
	const std::atomic_bool *cancelFlag) {
	if (diff == nullptr || diff->originalLines == nullptr || diff->compareLines == nullptr || diff->changeGroups == nullptr)
		return std::shared_ptr<const MRBentoFileComparePaneProjectionPayload>();
	if (projectionCancelled(cancelFlag)) return std::shared_ptr<const MRBentoFileComparePaneProjectionPayload>();

	std::shared_ptr<std::string> text = std::make_shared<std::string>();
	std::shared_ptr<std::vector<unsigned char>> lineKinds = std::make_shared<std::vector<unsigned char>>();
	std::shared_ptr<std::vector<MRFileCompareMiniMapSlice>> slices =
		std::make_shared<std::vector<MRFileCompareMiniMapSlice>>();
	if (editable)
		buildEditableProjection(*diff, original, cancelFlag, *lineKinds, *slices);
	else
		buildReadOnlyProjection(*diff, original, cancelFlag, *text, *lineKinds);
	if (projectionCancelled(cancelFlag)) return std::shared_ptr<const MRBentoFileComparePaneProjectionPayload>();

	std::shared_ptr<MRBentoFileComparePaneProjectionPayload> payload =
		std::make_shared<MRBentoFileComparePaneProjectionPayload>();
	payload->generation = diff->generation;
	payload->original = original;
	payload->editable = editable;
	payload->sourceDocumentId = original ? diff->originalDocumentId : diff->compareDocumentId;
	payload->sourceVersion = original ? diff->originalBaseVersion : diff->compareBaseVersion;
	payload->text = text;
	payload->lineKinds = lineKinds;
	payload->miniMapSlices = slices;
	return payload;
}
