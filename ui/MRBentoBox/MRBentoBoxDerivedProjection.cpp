#include "MRBentoBox.hpp"

#include "../MRMessageLineController.hpp"

#include "../../config/settings/MRSettingsRuntime.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <utility>

namespace {

struct DiagnosticSeverityMarker {
	const char *marker;
	const char *severity;
};

bool projectionCancelled(const std::atomic_bool *cancelFlag) noexcept {
	return cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire);
}

std::string pathBaseName(const std::string &path) {
	std::filesystem::path fsPath(path);
	const std::string base = fsPath.filename().string();

	return base.empty() ? path : base;
}

std::string normalizedDiagnosticPath(const std::string &path) {
	std::string normalized = path;

	for (char &ch : normalized)
		if (ch == '\\') ch = '/';
	normalized = std::filesystem::path(normalized).lexically_normal().generic_string();
	if (normalized.rfind("./", 0) == 0) normalized.erase(0, 2);
	return normalized;
}

bool pathSuffixMatches(const std::string &candidatePath, const std::string &sourcePath) {
	if (candidatePath.size() > sourcePath.size()) return false;
	if (candidatePath == sourcePath) return true;
	if (sourcePath.size() <= candidatePath.size()) return false;
	if (sourcePath.compare(sourcePath.size() - candidatePath.size(), candidatePath.size(), candidatePath) != 0) return false;
	return sourcePath[sourcePath.size() - candidatePath.size() - 1] == '/';
}

bool compilerDiagnosticPathMatches(const std::string &candidatePath, const std::string &sourcePath) {
	if (candidatePath.empty() || sourcePath.empty()) return false;
	const std::string candidate = normalizedDiagnosticPath(candidatePath);
	const std::string source = normalizedDiagnosticPath(sourcePath);

	if (candidate == source || pathSuffixMatches(candidate, source)) return true;
	return pathBaseName(candidate) == pathBaseName(source);
}

bool parseUnsignedField(const std::string &text, std::size_t start, std::size_t end, std::size_t &value) {
	std::size_t parsed = 0;

	if (start >= end || std::isdigit(static_cast<unsigned char>(text[start])) == 0) return false;
	value = 0;
	while (start + parsed < end && std::isdigit(static_cast<unsigned char>(text[start + parsed])) != 0) {
		value = value * 10 + static_cast<std::size_t>(text[start + parsed] - '0');
		++parsed;
	}
	return parsed != 0 && start + parsed == end;
}

bool parseCompilerDiagnosticLine(const std::string &line, const std::string &sourcePath, std::size_t outputOffset, MRCompilerDiagnostic &diagnostic) {
	static const DiagnosticSeverityMarker markers[] = {{": fatal error:", "fatal error"}, {": error:", "error"}, {": warning:", "warning"}, {": note:", "note"}};
	std::size_t markerPos = std::string::npos;
	const DiagnosticSeverityMarker *matchedMarker = nullptr;

	for (const DiagnosticSeverityMarker &marker : markers) {
		const std::size_t pos = line.find(marker.marker);
		if (pos != std::string::npos && (matchedMarker == nullptr || pos < markerPos)) {
			markerPos = pos;
			matchedMarker = &marker;
		}
	}
	if (matchedMarker == nullptr) return false;

	const std::string location = line.substr(0, markerPos);
	const std::size_t lastColon = location.rfind(':');
	std::size_t lineColon = std::string::npos;
	std::size_t sourceLine = 0;
	std::size_t sourceColumn = 1;

	if (lastColon == std::string::npos) return false;
	if (parseUnsignedField(location, lastColon + 1, location.size(), sourceColumn)) {
		lineColon = location.rfind(':', lastColon - 1);
		if (lineColon == std::string::npos) return false;
		if (!parseUnsignedField(location, lineColon + 1, lastColon, sourceLine)) return false;
	} else {
		lineColon = lastColon;
		if (!parseUnsignedField(location, lineColon + 1, location.size(), sourceLine)) return false;
		sourceColumn = 1;
	}
	if (sourceLine == 0 || sourceColumn == 0) return false;
	const std::string diagnosticPath = location.substr(0, lineColon);

	diagnostic.sourcePath = diagnosticPath;
	diagnostic.sourceLine = sourceLine;
	diagnostic.sourceColumn = sourceColumn;
	diagnostic.severity = matchedMarker->severity;
	diagnostic.text = line.substr(markerPos + std::strlen(matchedMarker->marker));
	while (!diagnostic.text.empty() && diagnostic.text.front() == ' ')
		diagnostic.text.erase(diagnostic.text.begin());
	diagnostic.sourceOffset = 0;
	diagnostic.outputOffset = outputOffset;
	diagnostic.problemOffset = 0;
	diagnostic.sourceAvailable = compilerDiagnosticPathMatches(diagnosticPath, sourcePath);
	return true;
}

bool parseLatexSourceDiagnosticLine(const std::string &line, const std::string &sourcePath, std::size_t outputOffset, MRCompilerDiagnostic &diagnostic) {
	std::size_t lineColon = std::string::npos;
	std::size_t textColon = std::string::npos;
	std::size_t sourceLine = 0;

	for (std::size_t colon = line.find(':'); colon != std::string::npos; colon = line.find(':', colon + 1)) {
		const std::size_t nextColon = line.find(':', colon + 1);

		if (nextColon == std::string::npos) return false;
		if (!parseUnsignedField(line, colon + 1, nextColon, sourceLine)) continue;
		lineColon = colon;
		textColon = nextColon;
		break;
	}
	if (lineColon == std::string::npos || textColon == std::string::npos || sourceLine == 0) return false;

	const std::string diagnosticPath = line.substr(0, lineColon);
	if (!compilerDiagnosticPathMatches(diagnosticPath, sourcePath)) return false;

	diagnostic.sourcePath = diagnosticPath;
	diagnostic.sourceLine = sourceLine;
	diagnostic.sourceColumn = 1;
	diagnostic.text = line.substr(textColon + 1);
	while (!diagnostic.text.empty() && diagnostic.text.front() == ' ')
		diagnostic.text.erase(diagnostic.text.begin());
	if (diagnostic.text.empty()) return false;
	diagnostic.severity = diagnostic.text.find("Warning:") != std::string::npos ? "warning" : "error";
	diagnostic.sourceOffset = 0;
	diagnostic.outputOffset = outputOffset;
	diagnostic.problemOffset = 0;
	diagnostic.sourceAvailable = true;
	return true;
}

bool parseCompilerDriverDiagnosticLine(const std::string &line, std::size_t outputOffset, MRCompilerDiagnostic &diagnostic) {
	static const DiagnosticSeverityMarker markers[] = {{"warning:", "warning"}, {"note:", "note"}};
	std::size_t markerPos = std::string::npos;
	const DiagnosticSeverityMarker *matchedMarker = nullptr;

	for (const DiagnosticSeverityMarker &marker : markers) {
		const std::size_t pos = line.find(marker.marker);
		if (pos != std::string::npos && (matchedMarker == nullptr || pos < markerPos)) {
			markerPos = pos;
			matchedMarker = &marker;
		}
	}
	if (matchedMarker == nullptr || markerPos == 0) return false;

	diagnostic.sourcePath = line.substr(0, markerPos);
	while (!diagnostic.sourcePath.empty() && diagnostic.sourcePath.back() == ' ')
		diagnostic.sourcePath.pop_back();
	if (!diagnostic.sourcePath.empty() && diagnostic.sourcePath.back() == ':') diagnostic.sourcePath.pop_back();
	while (!diagnostic.sourcePath.empty() && diagnostic.sourcePath.back() == ' ')
		diagnostic.sourcePath.pop_back();
	diagnostic.sourceLine = 0;
	diagnostic.sourceColumn = 0;
	diagnostic.severity = matchedMarker->severity;
	diagnostic.text = line.substr(markerPos + std::strlen(matchedMarker->marker));
	while (!diagnostic.text.empty() && diagnostic.text.front() == ' ')
		diagnostic.text.erase(diagnostic.text.begin());
	diagnostic.sourceOffset = 0;
	diagnostic.outputOffset = outputOffset;
	diagnostic.problemOffset = 0;
	diagnostic.sourceAvailable = false;
	return true;
}

bool parseLatexBangDiagnosticLine(const std::string &line, std::size_t outputOffset, MRCompilerDiagnostic &diagnostic) {
	if (line.rfind("! ", 0) != 0) return false;
	std::string text = line.substr(2);

	while (!text.empty() && text.front() == ' ')
		text.erase(text.begin());
	if (text.empty() || text.rfind("==>", 0) == 0 || text == "Emergency stop.") return false;

	diagnostic.sourcePath = "LaTeX";
	diagnostic.sourceLine = 0;
	diagnostic.sourceColumn = 0;
	diagnostic.severity = "error";
	diagnostic.text = text;
	diagnostic.sourceOffset = 0;
	diagnostic.outputOffset = outputOffset;
	diagnostic.problemOffset = 0;
	diagnostic.sourceAvailable = false;
	return true;
}

bool parseLatexBuildWarningLine(const std::string &line, const std::string &sourcePath, std::size_t outputOffset, MRCompilerDiagnostic &diagnostic) {
	static const DiagnosticSeverityMarker markers[] = {{"LaTeX Warning:", "LaTeX"}, {"Package ", "Package"}, {"Class ", "Class"}};
	const DiagnosticSeverityMarker *matchedMarker = nullptr;
	std::size_t markerPos = std::string::npos;
	std::size_t warningPos = std::string::npos;

	for (const DiagnosticSeverityMarker &marker : markers) {
		if (line.rfind(marker.marker, 0) != 0) continue;
		matchedMarker = &marker;
		markerPos = std::strlen(marker.marker);
		break;
	}
	if (matchedMarker == nullptr) return false;
	if (std::strcmp(matchedMarker->marker, "LaTeX Warning:") == 0) {
		diagnostic.sourcePath = matchedMarker->severity;
		diagnostic.text = line.substr(markerPos);
	} else {
		warningPos = line.find(" Warning:", markerPos);
		if (warningPos == std::string::npos) return false;
		diagnostic.sourcePath = line.substr(0, warningPos);
		diagnostic.text = line.substr(warningPos + std::strlen(" Warning:"));
	}
	while (!diagnostic.text.empty() && diagnostic.text.front() == ' ')
		diagnostic.text.erase(diagnostic.text.begin());
	if (diagnostic.text.empty()) return false;
	diagnostic.sourceLine = 0;
	diagnostic.sourceColumn = 0;
	const std::string inputLineMarker = " on input line ";
	const std::size_t inputLine = diagnostic.text.find(inputLineMarker);
	if (inputLine != std::string::npos) {
		const std::size_t lineStart = inputLine + inputLineMarker.size();
		std::size_t lineEnd = lineStart;
		while (lineEnd < diagnostic.text.size() && std::isdigit(static_cast<unsigned char>(diagnostic.text[lineEnd])) != 0)
			++lineEnd;
		std::size_t sourceLine = 0;
		if (parseUnsignedField(diagnostic.text, lineStart, lineEnd, sourceLine) && sourceLine > 0) {
			diagnostic.sourceLine = sourceLine;
			diagnostic.sourceColumn = 1;
		}
	}
	diagnostic.severity = "warning";
	diagnostic.sourceOffset = 0;
	diagnostic.outputOffset = outputOffset;
	diagnostic.problemOffset = 0;
	diagnostic.sourceAvailable = diagnostic.sourceLine > 0 && !sourcePath.empty();
	if (diagnostic.sourceAvailable) diagnostic.sourcePath = sourcePath;
	return true;
}

bool diagnosticSeverityEnabled(const MRCompilerDiagnostic &diagnostic, bool trackWarnings, bool trackNotes) noexcept {
	if (diagnostic.severity == "error" || diagnostic.severity == "fatal error") return true;
	if (diagnostic.severity == "warning") return trackWarnings;
	if (diagnostic.severity == "note") return trackNotes;
	return false;
}

std::size_t nextUtf8Offset(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t offset, std::size_t limit) noexcept {
	if (offset >= limit) return limit;
	const unsigned char lead = static_cast<unsigned char>(snapshot.charAt(offset));
	std::size_t count = 1;

	if ((lead & 0x80U) == 0U) return offset + 1;
	if ((lead & 0xE0U) == 0xC0U) count = 2;
	else if ((lead & 0xF0U) == 0xE0U)
		count = 3;
	else if ((lead & 0xF8U) == 0xF0U)
		count = 4;
	else
		return offset + 1;
	if (count > limit - offset) return offset + 1;
	for (std::size_t i = 1; i < count; ++i)
		if ((static_cast<unsigned char>(snapshot.charAt(offset + i)) & 0xC0U) != 0x80U) return offset + 1;
	return offset + count;
}

std::size_t sourceOffsetForCompilerColumn(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t lineStart, std::size_t lineEnd, std::size_t column) noexcept {
	std::size_t offset = lineStart;

	for (std::size_t currentColumn = 1; currentColumn < column && offset < lineEnd; ++currentColumn)
		offset = nextUtf8Offset(snapshot, offset, lineEnd);
	return offset;
}

std::size_t sourceColumnForOffset(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t lineStart, std::size_t offset) noexcept {
	std::size_t column = 1;
	std::size_t current = lineStart;

	while (current < offset) {
		const std::size_t next = nextUtf8Offset(snapshot, current, offset);
		if (next <= current) break;
		current = next;
		++column;
	}
	return column;
}

bool editTouchesDiagnosticLine(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t diagnosticOffset,
	                            std::size_t editStart, std::size_t editEnd) noexcept {
	const std::size_t lineStart = snapshot.lineStart(diagnosticOffset);
	const std::size_t lineEnd = snapshot.lineEnd(diagnosticOffset);

	if (editEnd > editStart) return editStart < lineEnd && editEnd > lineStart;
	return editStart >= lineStart && editStart <= lineEnd;
}

bool remapDiagnosticsForSourceChanges(std::vector<MRCompilerDiagnostic> &diagnostics,
	                                  const std::shared_ptr<const MRBentoDiagnosticSourceChange> &sourceChanges,
	                                  const MRTextBufferModel::ReadSnapshot &currentSnapshot,
	                                  const std::atomic_bool *cancelFlag) {
	std::vector<const MRBentoDiagnosticSourceChange *> orderedChanges;
	for (const MRBentoDiagnosticSourceChange *change = sourceChanges.get(); change != nullptr; change = change->previous.get())
		orderedChanges.push_back(change);
	std::reverse(orderedChanges.begin(), orderedChanges.end());
	for (std::size_t changeIndex = 0; changeIndex < orderedChanges.size(); ++changeIndex) {
		if (projectionCancelled(cancelFlag)) return false;
		const MRBentoDiagnosticSourceChange &sourceChange = *orderedChanges[changeIndex];
		const MRTextBufferModel::DocumentChangeSet &changeSet = sourceChange.changeSet;
		const MRTextBufferModel::ReadSnapshot &oldSnapshot = sourceChange.oldSnapshot;
		const MRTextBufferModel::ReadSnapshot &newSnapshot = sourceChange.newSnapshot;

		if (!changeSet.changed || changeSet.oldVersion != oldSnapshot.version() || changeSet.newVersion != newSnapshot.version() ||
		    oldSnapshot.documentId() != newSnapshot.documentId())
			return false;
		if (changeIndex != 0) {
			const MRTextBufferModel::ReadSnapshot &previous = orderedChanges[changeIndex - 1]->newSnapshot;
			if (previous.documentId() != oldSnapshot.documentId() || previous.version() != oldSnapshot.version()) return false;
		}
		const std::size_t editStart = changeSet.touchedRange.start;
		std::size_t oldEditEnd = std::min(changeSet.touchedRange.end, changeSet.oldLength);
		const long long delta = static_cast<long long>(changeSet.newLength) - static_cast<long long>(changeSet.oldLength);
		if (delta > 0 && changeSet.touchedRange.end - editStart == static_cast<std::size_t>(delta)) oldEditEnd = editStart;

		std::vector<MRCompilerDiagnostic> remapped;
		remapped.reserve(diagnostics.size());
		for (MRCompilerDiagnostic diagnostic : diagnostics) {
			if (projectionCancelled(cancelFlag)) return false;
			if (!diagnostic.sourceAvailable) {
				remapped.push_back(std::move(diagnostic));
				continue;
			}
			if (editTouchesDiagnosticLine(oldSnapshot, diagnostic.sourceOffset, editStart, oldEditEnd)) continue;
			if (diagnostic.sourceOffset >= oldEditEnd) {
				const long long shifted = static_cast<long long>(diagnostic.sourceOffset) + delta;
				if (shifted < 0 || static_cast<std::size_t>(shifted) > newSnapshot.length()) continue;
				diagnostic.sourceOffset = static_cast<std::size_t>(shifted);
			}
			const std::size_t lineIndex = newSnapshot.lineIndex(diagnostic.sourceOffset);
			const std::size_t lineStart = newSnapshot.lineStartByIndex(lineIndex);
			diagnostic.sourceLine = lineIndex + 1;
			diagnostic.sourceColumn = sourceColumnForOffset(newSnapshot, lineStart, diagnostic.sourceOffset);
			remapped.push_back(std::move(diagnostic));
		}
		diagnostics = std::move(remapped);
	}
	if (sourceChanges != nullptr) {
		const MRTextBufferModel::ReadSnapshot &lastSnapshot = sourceChanges->newSnapshot;
		if (lastSnapshot.documentId() != currentSnapshot.documentId() || lastSnapshot.version() != currentSnapshot.version()) return false;
	}
	return true;
}

void normalizeProjectionRanges(std::vector<MRTextBufferModel::Range> &ranges) {
	std::sort(ranges.begin(), ranges.end(), [](const MRTextBufferModel::Range &first, const MRTextBufferModel::Range &second) {
		return first.start < second.start || (first.start == second.start && first.end < second.end);
	});
	std::vector<MRTextBufferModel::Range> merged;
	merged.reserve(ranges.size());
	for (const MRTextBufferModel::Range &range : ranges) {
		if (range.end <= range.start) continue;
		if (merged.empty() || range.start > merged.back().end)
			merged.push_back(range);
		else if (range.end > merged.back().end)
			merged.back().end = range.end;
	}
	ranges = std::move(merged);
}

std::vector<MRCompilerDiagnostic> parseCompilerDiagnostics(const std::string &text, const std::string &sourcePath, const std::atomic_bool *cancelFlag) {
	std::vector<MRCompilerDiagnostic> diagnostics;
	const std::size_t textLength = text.size();
	std::size_t lineStart = 0;

	while (lineStart < textLength) {
		if (projectionCancelled(cancelFlag)) return std::vector<MRCompilerDiagnostic>();
		std::size_t lineEnd = text.find('\n', lineStart);
		MRCompilerDiagnostic diagnostic;

		if (lineEnd == std::string::npos) lineEnd = textLength;
		const std::string line = text.substr(lineStart, lineEnd - lineStart);
		if (parseCompilerDiagnosticLine(line, sourcePath, lineStart, diagnostic) || parseLatexSourceDiagnosticLine(line, sourcePath, lineStart, diagnostic) ||
		    parseCompilerDriverDiagnosticLine(line, lineStart, diagnostic) || parseLatexBangDiagnosticLine(line, lineStart, diagnostic) ||
		    parseLatexBuildWarningLine(line, sourcePath, lineStart, diagnostic))
			diagnostics.push_back(std::move(diagnostic));
		if (lineEnd == textLength) break;
		lineStart = lineEnd + 1;
	}
	return diagnostics;
}

std::string diagnosticProblemLine(const MRCompilerDiagnostic &diagnostic) {
	std::ostringstream line;

	line << diagnostic.severity << " ";
	if (diagnostic.sourceLine == 0)
		line << (diagnostic.sourcePath.empty() ? "build" : diagnostic.sourcePath);
	else
		line << pathBaseName(diagnostic.sourcePath) << ":" << diagnostic.sourceLine << ":" << diagnostic.sourceColumn;
	if (!diagnostic.text.empty()) line << " " << diagnostic.text;
	return line.str();
}

const char *outlineKindLabel(MROutlineKind kind) noexcept {
	switch (kind) {
		case mrokModule:
			return "mod";
		case mrokNamespace:
			return "ns";
		case mrokClass:
			return "type";
		case mrokMethod:
			return "meth";
		case mrokFunction:
			return "func";
		case mrokSection:
			return "sec";
		case mrokMacro:
			return "macro";
		case mrokTarget:
			return "target";
		case mrokBlock:
			return "block";
		default:
			return "sym";
	}
}

std::uint64_t projectionTextHash(const std::string &text) noexcept {
	std::uint64_t hash = 1469598103934665603ULL;

	for (char ch : text) {
		hash ^= static_cast<unsigned char>(ch);
		hash *= 1099511628211ULL;
	}
	return hash;
}

std::string outlineCoverageStatus(const MROutlineSnapshot &snapshot, std::size_t itemCount) {
	char rangeText[64];
	std::string status = std::to_string(itemCount) + (itemCount == 1 ? " item" : " items");

	if (snapshot.complete) return status + ", full";
	if (snapshot.bottomLine <= snapshot.topLine) return status;
	std::snprintf(rangeText, sizeof(rangeText), "lines %zu-%zu", snapshot.topLine + 1, snapshot.bottomLine);
	return status + ", " + rangeText;
}

bool diagnosticChangeChainStartsAt(const std::shared_ptr<const MRBentoDiagnosticSourceChange> &changes,
	                                std::size_t documentId, std::size_t version,
	                                std::size_t currentDocumentId, std::size_t currentVersion) noexcept {
	const MRBentoDiagnosticSourceChange *oldest = changes.get();

	if (oldest == nullptr || changes->newSnapshot.documentId() != currentDocumentId || changes->newSnapshot.version() != currentVersion) return false;
	while (oldest->previous != nullptr)
		oldest = oldest->previous.get();
	return oldest->oldSnapshot.documentId() == documentId && oldest->oldSnapshot.version() == version;
}

bool rebuildDiagnosticChangesAfter(const std::shared_ptr<const MRBentoDiagnosticSourceChange> &current,
	                                const std::shared_ptr<const MRBentoDiagnosticSourceChange> &processed,
	                                std::shared_ptr<const MRBentoDiagnosticSourceChange> &remaining) {
	std::vector<const MRBentoDiagnosticSourceChange *> newer;
	const MRBentoDiagnosticSourceChange *change = current.get();

	remaining.reset();
	while (change != nullptr && change != processed.get()) {
		newer.push_back(change);
		change = change->previous.get();
	}
	if (change != processed.get()) return false;
	for (auto it = newer.rbegin(); it != newer.rend(); ++it)
		remaining = std::make_shared<const MRBentoDiagnosticSourceChange>((*it)->oldSnapshot, (*it)->newSnapshot, (*it)->changeSet, remaining);
	return true;
}

void postCompilerNavigationUnavailable() {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "No compiler diagnostic location found.",
	                               mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
}

} // namespace

MRBentoDiagnosticsProjectionPayload::MRBentoDiagnosticsProjectionPayload() noexcept
	: generation(0), sourceDocumentId(0), sourceVersion(0), outputDocumentId(0), outputVersion(0), targetDocumentId(0), targetVersion(0), sourcePath(),
	  trackWarnings(false), trackNotes(false), projectionText(), diagnostics(), sourceErrorRanges(), sourceWarningRanges(), status(), textHash(0) {
}

MRBentoOutlineProjectionPayload::MRBentoOutlineProjectionPayload() noexcept
	: generation(0), functions(false), inputLanguage(MRSyntaxLanguage::PlainText), sourceDocumentId(0), sourceVersion(0), inputRevision(0),
	  targetDocumentId(0), targetVersion(0), projectionText(), entries(), status(), textHash(0), complete(false) {
}

MRBentoDiagnosticSourceChange::MRBentoDiagnosticSourceChange() noexcept : oldSnapshot(), newSnapshot(), changeSet(), depth(0), previous() {
}

MRBentoDiagnosticSourceChange::MRBentoDiagnosticSourceChange(const MRTextBufferModel::ReadSnapshot &anOldSnapshot,
	                                                         const MRTextBufferModel::ReadSnapshot &aNewSnapshot,
	                                                         const MRTextBufferModel::DocumentChangeSet &aChangeSet,
	                                                         std::shared_ptr<const MRBentoDiagnosticSourceChange> aPrevious)
	: oldSnapshot(anOldSnapshot), newSnapshot(aNewSnapshot), changeSet(aChangeSet),
	  depth(aPrevious != nullptr ? aPrevious->depth + 1 : 1), previous(std::move(aPrevious)) {
}

std::shared_ptr<const MRBentoDiagnosticsProjectionPayload> mrBuildBentoDiagnosticsProjection(
	const MRTextBufferModel::ReadSnapshot &sourceSnapshot, const MRTextBufferModel::ReadSnapshot &diagnosticSourceSnapshot,
	const MRTextBufferModel::ReadSnapshot &outputSnapshot,
	std::size_t targetDocumentId, std::size_t targetVersion, std::uint64_t generation, const std::string &sourcePath,
	bool trackWarnings, bool trackNotes, bool parseOutput, std::shared_ptr<const std::vector<MRCompilerDiagnostic>> diagnostics,
	std::shared_ptr<const MRBentoDiagnosticSourceChange> sourceChanges,
	const std::atomic_bool *cancelFlag, std::string *errorMessage) {
	std::shared_ptr<MRBentoDiagnosticsProjectionPayload> projection = std::make_shared<MRBentoDiagnosticsProjectionPayload>();
	std::vector<MRCompilerDiagnostic> projectedDiagnostics;
	std::vector<MRTextBufferModel::Range> sourceErrorRanges;
	std::vector<MRTextBufferModel::Range> sourceWarningRanges;
	std::string problemText;
	int errorCount = 0;
	int warningCount = 0;
	int noteCount = 0;

	projection->generation = generation;
	projection->sourceDocumentId = sourceSnapshot.documentId();
	projection->sourceVersion = sourceSnapshot.version();
	projection->outputDocumentId = outputSnapshot.documentId();
	projection->outputVersion = outputSnapshot.version();
	projection->targetDocumentId = targetDocumentId;
	projection->targetVersion = targetVersion;
	projection->sourcePath = sourcePath;
	projection->trackWarnings = trackWarnings;
	projection->trackNotes = trackNotes;
	if (projectionCancelled(cancelFlag)) return std::shared_ptr<const MRBentoDiagnosticsProjectionPayload>();
	if (parseOutput)
		projectedDiagnostics = parseCompilerDiagnostics(outputSnapshot.text(), sourcePath, cancelFlag);
	else if (diagnostics != nullptr)
		projectedDiagnostics = *diagnostics;
	if (parseOutput)
		for (MRCompilerDiagnostic &diagnostic : projectedDiagnostics)
			if (diagnostic.sourceAvailable) {
				const std::size_t lineStart = diagnosticSourceSnapshot.lineStartByIndex(diagnostic.sourceLine > 0 ? diagnostic.sourceLine - 1 : 0);
				const std::size_t lineEnd = diagnosticSourceSnapshot.lineEnd(lineStart);
				diagnostic.sourceOffset = sourceOffsetForCompilerColumn(diagnosticSourceSnapshot, lineStart, lineEnd, diagnostic.sourceColumn);
			}
	if (!remapDiagnosticsForSourceChanges(projectedDiagnostics, sourceChanges, sourceSnapshot, cancelFlag)) {
		if (!projectionCancelled(cancelFlag) && errorMessage != nullptr)
			*errorMessage = "diagnostic source-change chain is inconsistent";
		return std::shared_ptr<const MRBentoDiagnosticsProjectionPayload>();
	}
	if (projectionCancelled(cancelFlag)) return std::shared_ptr<const MRBentoDiagnosticsProjectionPayload>();

	std::vector<MRCompilerDiagnostic> filteredDiagnostics;
	filteredDiagnostics.reserve(projectedDiagnostics.size());
	for (MRCompilerDiagnostic &diagnostic : projectedDiagnostics) {
		if (projectionCancelled(cancelFlag)) return std::shared_ptr<const MRBentoDiagnosticsProjectionPayload>();
		if (!diagnosticSeverityEnabled(diagnostic, trackWarnings, trackNotes)) continue;
		diagnostic.problemOffset = problemText.size();
		const std::string line = diagnosticProblemLine(diagnostic);
		problemText.append(line);
		problemText.push_back('\n');

		if (diagnostic.severity == "error" || diagnostic.severity == "fatal error")
			++errorCount;
		else if (diagnostic.severity == "warning")
			++warningCount;
		else if (diagnostic.severity == "note")
			++noteCount;
		if (diagnostic.sourceAvailable) {
			const std::size_t rangeStart = sourceSnapshot.clampOffset(diagnostic.sourceOffset);
			const std::size_t rangeEnd = nextUtf8Offset(sourceSnapshot, rangeStart, sourceSnapshot.length());

			if (diagnostic.severity == "warning" || diagnostic.severity == "note")
				sourceWarningRanges.push_back(MRTextBufferModel::Range(rangeStart, rangeEnd));
			else
				sourceErrorRanges.push_back(MRTextBufferModel::Range(rangeStart, rangeEnd));
		}
		filteredDiagnostics.push_back(std::move(diagnostic));
	}

	std::ostringstream status;
	if (errorCount > 0) status << errorCount << " error" << (errorCount == 1 ? "" : "s");
	if (warningCount > 0) {
		if (status.tellp() > 0) status << ", ";
		status << warningCount << " warning" << (warningCount == 1 ? "" : "s");
	}
	if (noteCount > 0) {
		if (status.tellp() > 0) status << ", ";
		status << noteCount << " note" << (noteCount == 1 ? "" : "s");
	}
	projection->status = status.str();
	normalizeProjectionRanges(sourceErrorRanges);
	normalizeProjectionRanges(sourceWarningRanges);
	projection->diagnostics = std::make_shared<const std::vector<MRCompilerDiagnostic>>(std::move(filteredDiagnostics));
	projection->sourceErrorRanges = std::make_shared<const std::vector<MRTextBufferModel::Range>>(std::move(sourceErrorRanges));
	projection->sourceWarningRanges = std::make_shared<const std::vector<MRTextBufferModel::Range>>(std::move(sourceWarningRanges));
	projection->textHash = projectionTextHash(problemText);
	projection->projectionText = std::make_shared<const std::string>(std::move(problemText));
	return projection;
}

std::shared_ptr<const MRBentoOutlineProjectionPayload> mrBuildBentoOutlineProjection(
	const MRTextBufferModel::ReadSnapshot &sourceSnapshot, const MROutlineSnapshot &outlineSnapshot, bool snapshotReady,
	MRSyntaxLanguage inputLanguage, bool completeWarmupRequested, bool functions, std::size_t targetDocumentId, std::size_t targetVersion,
	std::uint64_t generation, std::uint64_t inputRevision, const std::atomic_bool *cancelFlag) {
	std::shared_ptr<MRBentoOutlineProjectionPayload> projection = std::make_shared<MRBentoOutlineProjectionPayload>();
	std::vector<MRBentoOutlineEntry> entries;
	std::string text;

	projection->generation = generation;
	projection->functions = functions;
	projection->inputLanguage = inputLanguage;
	projection->sourceDocumentId = sourceSnapshot.documentId();
	projection->sourceVersion = sourceSnapshot.version();
	projection->inputRevision = inputRevision;
	projection->targetDocumentId = targetDocumentId;
	projection->targetVersion = targetVersion;
	if (projectionCancelled(cancelFlag)) return std::shared_ptr<const MRBentoOutlineProjectionPayload>();

	if (!snapshotReady) {
		text = functions ? "Functions warming...\n" : "Structure warming...\n";
		projection->status = completeWarmupRequested ? "warming full" : "warming";
		projection->complete = false;
	} else if (outlineSnapshot.nodes.empty()) {
		text = functions ? "No functions available from MR outline.\n" : "No structure available from MR outline.\n";
		projection->status = outlineCoverageStatus(outlineSnapshot, 0);
		projection->complete = outlineSnapshot.complete;
	} else {
		entries.reserve(outlineSnapshot.nodes.size());
		for (std::size_t i = 0; i < outlineSnapshot.nodes.size(); ++i) {
			if (projectionCancelled(cancelFlag)) return std::shared_ptr<const MRBentoOutlineProjectionPayload>();
			const MROutlineNode &node = outlineSnapshot.nodes[i];
			int depth = 0;
			std::uint32_t parent = node.parent;
			char lineNumber[32];
			MRBentoOutlineEntry entry;

			while (parent != MROutlineNode::npos && parent < outlineSnapshot.nodes.size() && depth < 20) {
				++depth;
				parent = outlineSnapshot.nodes[parent].parent;
			}
			entry.paneOffset = text.size();
			entry.sourceOffset = sourceSnapshot.clampOffset(node.selectionRange.start.offset);
			entry.sourceSelectionEnd = sourceSnapshot.lineEnd(entry.sourceOffset);
			if (entry.sourceSelectionEnd < entry.sourceOffset) entry.sourceSelectionEnd = entry.sourceOffset;
			entries.push_back(entry);

			std::snprintf(lineNumber, sizeof(lineNumber), "%6zu  ", node.selectionRange.start.line + 1);
			text.append(lineNumber);
			for (int level = 0; level < depth; ++level)
				text.append("  ");
			text.append(outlineKindLabel(node.kind));
			text.append("  ");
			if (node.nameOffset < outlineSnapshot.textPool.size())
				text.append(outlineSnapshot.textPool.data() + node.nameOffset,
				            std::min<std::size_t>(node.nameLength, outlineSnapshot.textPool.size() - node.nameOffset));
			if (node.confidence == mrocHeuristic) text.append("  ?");
			text.push_back('\n');
		}
		projection->status = outlineCoverageStatus(outlineSnapshot, entries.size());
		projection->complete = outlineSnapshot.complete;
	}
	projection->textHash = projectionTextHash(text);
	projection->entries = std::make_shared<const std::vector<MRBentoOutlineEntry>>(std::move(entries));
	projection->projectionText = std::make_shared<const std::string>(std::move(text));
	return projection;
}

void MRBentoBox::cancelBentoProjectionTask(BentoProjectionTaskState &state) noexcept {
	if (state.taskId != 0) {
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(state.taskId));
		releaseCoprocessorTask(state.taskId);
	}
	state.taskId = 0;
	state.activeGeneration = 0;
	state.pending = false;
	state.pendingForce = false;
	state.pendingDiagnosticsRequest = bdprNone;
	state.diagnosticSourceChanges.reset();
	state.diagnosticBaseDocumentId = 0;
	state.diagnosticBaseVersion = 0;
	state.completeCoverageRequested = false;
	state.inputBufferId = 0;
	state.inputRevision = 0;
	state.inputComplete = false;
	state.projectionCurrent = false;
	state.retryBlocked = true;
	state.targetBufferId = 0;
	state.requestedAt = std::chrono::steady_clock::time_point();
}

void MRBentoBox::cancelAllBentoProjectionTasks() noexcept {
	cancelBentoProjectionTask(diagnosticsProjectionTask);
	pendingCompilerProblemNavigation = 0;
	cancelBentoProjectionTask(structureProjectionTask);
	cancelBentoProjectionTask(functionsProjectionTask);
}

void MRBentoBox::cancelBentoProjectionForPane(int paneBufferId) {
	if (paneBufferId <= 0) return;
	const bool diagnosticsInputClosed = diagnosticsProjectionTask.inputBufferId == paneBufferId || compilerDiagnosticsOutputBufferId == paneBufferId;
	const bool diagnosticsTargetClosed = diagnosticsProjectionTask.targetBufferId == paneBufferId || compilerProblemsTargetBufferId == paneBufferId;
	if (diagnosticsInputClosed || diagnosticsTargetClosed) {
		if (diagnosticsInputClosed) clearCompilerDiagnostics();
		cancelBentoProjectionTask(diagnosticsProjectionTask);
		pendingCompilerProblemNavigation = 0;
		if (diagnosticsInputClosed) {
			compilerDiagnosticSourceChanges.reset();
			compilerDiagnosticsParseSourceSnapshot.reset();
			compilerDiagnostics.reset();
			compilerDiagnosticsDocumentId = 0;
			compilerDiagnosticsVersion = 0;
			compilerDiagnosticsOutputDocumentId = 0;
			compilerDiagnosticsOutputVersion = 0;
			compilerDiagnosticsOutputBufferId = 0;
			compilerDiagnosticsParseRequired = true;
			compilerDiagnosticsSourceInvalidated = false;
			compilerProblemsTargetDocumentId = 0;
			compilerProblemsTargetVersion = 0;
			compilerProblemsTargetBufferId = 0;
			compilerOutputStatus.clear();
			compilerProblemsStatus.clear();
		}
	}
	if (structureProjectionTask.targetBufferId == paneBufferId) cancelBentoProjectionTask(structureProjectionTask);
	if (functionsProjectionTask.targetBufferId == paneBufferId) cancelBentoProjectionTask(functionsProjectionTask);
}

bool MRBentoBox::adoptBentoProjectionText(MREditWindow *targetWindow, const std::shared_ptr<const std::string> &text,
	                                      std::size_t expectedDocumentId, std::size_t expectedVersion, const char *title) {
	if (targetWindow == nullptr || text == nullptr) return false;
	const bool adoptionWasActive = bentoProjectionAdoptionActive;

	bentoProjectionAdoptionActive = true;
	const bool adopted = targetWindow->adoptReadOnlyProjectionText(text, expectedDocumentId, expectedVersion, title);
	bentoProjectionAdoptionActive = adoptionWasActive;
	return adopted;
}

void MRBentoBox::resumePendingBentoProjection(BentoProjectionTaskState &state, MRBentoPaneRole role) {
	const bool pending = state.pending;
	const bool force = state.pendingForce;
	const BentoDiagnosticsProjectionRequest diagnosticsRequest = state.pendingDiagnosticsRequest;

	state.pending = false;
	state.pendingForce = false;
	state.pendingDiagnosticsRequest = bdprNone;
	if (!pending) return;
	if (role == bprProblems) {
		if (diagnosticsRequest != bdprNone) static_cast<void>(submitCompilerDiagnosticsProjection(diagnosticsRequest));
		return;
	}
	if (role == bprStructure)
		structureOutlineState.lastRefresh = std::chrono::steady_clock::time_point();
	else if (role == bprFunctions)
		functionsOutlineState.lastRefresh = std::chrono::steady_clock::time_point();
	static_cast<void>(submitOutlineProjection(role, force));
}

bool MRBentoBox::applyBentoProjectionResult(const mr::coprocessor::Result &result) {
	BentoProjectionTaskState *taskState = nullptr;
	MRBentoPaneRole role = bprProblems;

	switch (result.task.kind) {
		case mr::coprocessor::TaskKind::BentoDiagnosticsProjection:
			taskState = &diagnosticsProjectionTask;
			role = bprProblems;
			break;
		case mr::coprocessor::TaskKind::BentoOutlineProjection: {
			if (functionsProjectionTask.taskId == result.task.id) {
				taskState = &functionsProjectionTask;
				role = bprFunctions;
			} else if (structureProjectionTask.taskId == result.task.id) {
				taskState = &structureProjectionTask;
				role = bprStructure;
			} else
				return false;
			break;
		}
		default:
			return false;
	}
	if (taskState == nullptr || taskState->taskId == 0 || taskState->taskId != result.task.id) return false;
	const bool adoptionWasActive = bentoProjectionAdoptionActive;
	bentoProjectionAdoptionActive = true;
	bool pending = taskState->pending;
	bool pendingForce = taskState->pendingForce;
	const bool completeCoverageRequested = taskState->completeCoverageRequested;
	BentoDiagnosticsProjectionRequest pendingDiagnosticsRequest = taskState->pendingDiagnosticsRequest;
	const BentoDiagnosticsProjectionRequest completedDiagnosticsRequest = taskState->diagnosticsRequest;
	const std::shared_ptr<const MRBentoDiagnosticSourceChange> submittedDiagnosticChanges = taskState->diagnosticSourceChanges;
	const std::size_t submittedDiagnosticBaseDocumentId = taskState->diagnosticBaseDocumentId;
	const std::size_t submittedDiagnosticBaseVersion = taskState->diagnosticBaseVersion;

	releaseCoprocessorTask(result.task.id);
	taskState->taskId = 0;
	taskState->pending = false;
	taskState->pendingForce = false;
	taskState->pendingDiagnosticsRequest = bdprNone;
	taskState->projectionCurrent = false;
	bool adopted = false;
	bool baselinePromoted = false;
	bool projectionChanged = false;

	if (result.completed() && result.task.executionOwnerKind == mr::coprocessor::ExecutionOwnerKind::BentoPane &&
	    result.task.generation == taskState->activeGeneration && result.task.documentId == taskState->sourceDocumentId &&
	    result.task.baseVersion == taskState->sourceVersion) {
		if (role == bprProblems) {
			const MRBentoDiagnosticsProjectionPayload *payload = dynamic_cast<const MRBentoDiagnosticsProjectionPayload *>(result.payload.get());
			MREditWindow *targetWindow = problemsPane();
			MREditWindow *outputWindow = buildOutputPane();
			MRFileEditor *sourceEditor = getEditor();
			MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;

			const bool payloadMatchesTask = payload != nullptr && payload->projectionText != nullptr && payload->diagnostics != nullptr &&
			                          payload->sourceErrorRanges != nullptr && payload->sourceWarningRanges != nullptr &&
			                          targetWindow != nullptr && targetEditor != nullptr &&
			                          outputWindow != nullptr && sourceEditor != nullptr && targetWindow->bufferId() == taskState->targetBufferId &&
			                          outputWindow->bufferId() == taskState->inputBufferId &&
			                          static_cast<std::size_t>(targetWindow->bufferId()) == result.task.executionOwnerLocalId &&
			                          payload->generation == taskState->activeGeneration && payload->sourceDocumentId == taskState->sourceDocumentId &&
			                          payload->sourceVersion == taskState->sourceVersion && payload->outputDocumentId == taskState->inputDocumentId &&
			                          payload->outputVersion == taskState->inputVersion && payload->targetDocumentId == taskState->targetDocumentId &&
			                          payload->targetVersion == taskState->targetVersion && sourceEditor->documentId() == payload->sourceDocumentId &&
			                          outputWindow->documentId() == payload->outputDocumentId &&
			                          outputWindow->documentVersion() == payload->outputVersion && targetEditor->documentId() == payload->targetDocumentId &&
			                          targetEditor->documentVersion() == payload->targetVersion && payload->sourcePath == taskState->sourcePath &&
			                          payload->trackWarnings == taskState->trackWarnings && payload->trackNotes == taskState->trackNotes &&
			                          payload->sourcePath == currentFileName() && payload->trackWarnings == configuredTrackCompilerWarnings() &&
			                          payload->trackNotes == configuredTrackCompilerNotes();
			const bool routeMatches = payloadMatchesTask && sourceEditor->documentVersion() == payload->sourceVersion;
			if (payloadMatchesTask && !routeMatches && sourceEditor->documentVersion() > payload->sourceVersion) {
				if (completedDiagnosticsRequest == bdprParseOutput) {
					std::shared_ptr<const MRBentoDiagnosticSourceChange> remainingChanges;
					bool chainMatches = false;

					if (submittedDiagnosticChanges != nullptr) {
						chainMatches = submittedDiagnosticChanges->newSnapshot.documentId() == payload->sourceDocumentId &&
						               submittedDiagnosticChanges->newSnapshot.version() == payload->sourceVersion &&
						               rebuildDiagnosticChangesAfter(compilerDiagnosticSourceChanges, submittedDiagnosticChanges, remainingChanges);
					} else if (diagnosticChangeChainStartsAt(compilerDiagnosticSourceChanges, payload->sourceDocumentId, payload->sourceVersion,
					                                              sourceEditor->documentId(), sourceEditor->documentVersion())) {
						remainingChanges = compilerDiagnosticSourceChanges;
						chainMatches = true;
					}
					if (chainMatches) {
						compilerDiagnostics = payload->diagnostics;
						compilerDiagnosticsDocumentId = payload->sourceDocumentId;
						compilerDiagnosticsVersion = payload->sourceVersion;
						compilerDiagnosticsOutputDocumentId = payload->outputDocumentId;
						compilerDiagnosticsOutputVersion = payload->outputVersion;
						compilerDiagnosticsOutputBufferId = outputWindow->bufferId();
						compilerDiagnosticSourceChanges = remainingChanges;
						compilerDiagnosticsParseRequired = false;
						compilerDiagnosticsSourceInvalidated = false;
						compilerDiagnosticsParseSourceSnapshot.reset();
						pending = remainingChanges != nullptr;
						if (pending) pendingDiagnosticsRequest = bdprRemapExisting;
						baselinePromoted = true;
					}
				} else if (completedDiagnosticsRequest == bdprRemapExisting && submittedDiagnosticChanges != nullptr &&
				           compilerDiagnosticsDocumentId == submittedDiagnosticBaseDocumentId &&
				           compilerDiagnosticsVersion == submittedDiagnosticBaseVersion &&
				           submittedDiagnosticChanges->newSnapshot.documentId() == payload->sourceDocumentId &&
				           submittedDiagnosticChanges->newSnapshot.version() == payload->sourceVersion) {
					std::shared_ptr<const MRBentoDiagnosticSourceChange> remainingChanges;

					if (rebuildDiagnosticChangesAfter(compilerDiagnosticSourceChanges, submittedDiagnosticChanges, remainingChanges)) {
						compilerDiagnostics = payload->diagnostics;
						compilerDiagnosticsDocumentId = payload->sourceDocumentId;
						compilerDiagnosticsVersion = payload->sourceVersion;
						compilerDiagnosticSourceChanges = remainingChanges;
						pending = remainingChanges != nullptr;
						if (pending && pendingDiagnosticsRequest < bdprRemapExisting) pendingDiagnosticsRequest = bdprRemapExisting;
						baselinePromoted = true;
					}
				}
			}
				const bool cachedTargetMatches = routeMatches && compilerProblemsTargetDocumentId == targetEditor->documentId() &&
				                                 compilerProblemsTargetVersion == targetEditor->documentVersion() &&
				                                 compilerProblemsTargetBufferId == targetWindow->bufferId();
				const bool textAlreadyCurrent = cachedTargetMatches && compilerProblemsTextHash == payload->textHash &&
				                                compilerProblemsTextLength == payload->projectionText->size();
				const bool statusChanged = routeMatches && compilerProblemsStatus != payload->status;
				const bool textReady = textAlreadyCurrent ||
				                       (routeMatches && adoptBentoProjectionText(targetWindow, payload->projectionText,
				                                                                  payload->targetDocumentId, payload->targetVersion, "Problems"));
				if (textReady) {
					compilerDiagnostics = payload->diagnostics;
					compilerDiagnosticSourceChanges.reset();
					compilerDiagnosticsDocumentId = payload->sourceDocumentId;
					compilerDiagnosticsVersion = payload->sourceVersion;
					compilerDiagnosticsOutputDocumentId = payload->outputDocumentId;
					compilerDiagnosticsOutputVersion = payload->outputVersion;
					compilerDiagnosticsOutputBufferId = outputWindow->bufferId();
					compilerProblemsTargetDocumentId = targetEditor->documentId();
					compilerProblemsTargetVersion = targetEditor->documentVersion();
					compilerProblemsTargetBufferId = targetWindow->bufferId();
					compilerProblemsTextLength = payload->projectionText->size();
					compilerProblemsTextHash = payload->textHash;
					compilerProblemsStatus = payload->status;
				clearTrackedCompilerSidekick(true);
				targetEditor->clearFindMarkerRanges();
				sourceEditor->adoptCompilerDiagnosticRanges(payload->sourceErrorRanges, payload->sourceWarningRanges);
				targetWindow->setFileChanged(false);
				taskState->sourceDocumentId = payload->sourceDocumentId;
				taskState->sourceVersion = payload->sourceVersion;
					taskState->inputDocumentId = payload->outputDocumentId;
					taskState->inputVersion = payload->outputVersion;
					taskState->inputBufferId = outputWindow->bufferId();
				taskState->targetDocumentId = targetEditor->documentId();
				taskState->targetVersion = targetEditor->documentVersion();
				taskState->targetBufferId = targetWindow->bufferId();
				taskState->sourcePath = payload->sourcePath;
				taskState->trackWarnings = payload->trackWarnings;
				taskState->trackNotes = payload->trackNotes;
					taskState->diagnosticsRequest = completedDiagnosticsRequest;
					if (completedDiagnosticsRequest == bdprParseOutput) {
						compilerDiagnosticsParseRequired = false;
						compilerDiagnosticsSourceInvalidated = false;
						compilerDiagnosticsParseSourceSnapshot.reset();
					}
					taskState->projectionCurrent = true;
					adopted = true;
					projectionChanged = !textAlreadyCurrent || statusChanged;
				}
		} else {
			const MRBentoOutlineProjectionPayload *payload = dynamic_cast<const MRBentoOutlineProjectionPayload *>(result.payload.get());
			MREditWindow *targetWindow = role == bprFunctions ? functionsPane() : structurePane();
			MRFileEditor *sourceEditor = getEditor();
			MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
			std::shared_ptr<const std::vector<MRBentoOutlineEntry>> *entries = role == bprFunctions ? &functionsOutlineEntries : &structureOutlineEntries;
			MRBentoOutlinePaneState *outlineState = role == bprFunctions ? &functionsOutlineState : &structureOutlineState;
			std::string *status = role == bprFunctions ? &functionsOutlineStatus : &structureOutlineStatus;
			const bool functions = role == bprFunctions;
			const std::uint64_t currentInputRevision = sourceEditor != nullptr ? sourceEditor->foldOutlineInputRevision() : 0;
			const MRSyntaxLanguage currentInputLanguage = sourceEditor != nullptr ? sourceEditor->syntaxLanguage() : MRSyntaxLanguage::PlainText;
			const bool completeCoverageSuperset = payload != nullptr && payload->complete && payload->inputLanguage == taskState->inputLanguage &&
			                                      payload->inputLanguage == currentInputLanguage;
			if (currentInputRevision != taskState->inputRevision && !completeCoverageSuperset) {
				pending = true;
					if (completeCoverageRequested) pendingForce = true;
			}

			const bool routeMatches = payload != nullptr && payload->projectionText != nullptr && payload->entries != nullptr && payload->functions == functions &&
			                          targetWindow != nullptr && targetEditor != nullptr && sourceEditor != nullptr &&
			                          targetWindow->bufferId() == taskState->targetBufferId &&
			                          static_cast<std::size_t>(targetWindow->bufferId()) == result.task.executionOwnerLocalId &&
			                          payload->generation == taskState->activeGeneration && payload->sourceDocumentId == taskState->sourceDocumentId &&
			                          payload->sourceVersion == taskState->sourceVersion && payload->inputRevision == taskState->inputRevision &&
			                          payload->inputLanguage == taskState->inputLanguage && payload->inputLanguage == currentInputLanguage &&
			                          (currentInputRevision == payload->inputRevision || completeCoverageSuperset) &&
			                          payload->targetDocumentId == taskState->targetDocumentId &&
			                          payload->targetVersion == taskState->targetVersion && sourceEditor->documentId() == payload->sourceDocumentId &&
			                          sourceEditor->documentVersion() == payload->sourceVersion && targetEditor->documentId() == payload->targetDocumentId &&
			                          targetEditor->documentVersion() == payload->targetVersion;
			const char *title = functions ? "Functions" : "Structure";
			const bool keepCompleteProjection = routeMatches && !payload->complete && outlineState->complete &&
			                                    outlineState->documentId == payload->sourceDocumentId && outlineState->version == payload->sourceVersion &&
			                                    outlineState->language == payload->inputLanguage && outlineState->targetDocumentId == payload->targetDocumentId &&
			                                    outlineState->targetVersion == payload->targetVersion && outlineState->targetBufferId == targetWindow->bufferId();
			const bool textAlreadyCurrent = routeMatches && outlineState->targetDocumentId == payload->targetDocumentId &&
			                                outlineState->targetVersion == payload->targetVersion &&
			                                outlineState->targetBufferId == targetWindow->bufferId() &&
			                                outlineState->textHash == payload->textHash &&
			                                outlineState->textLength == payload->projectionText->size();
			const bool statusChanged = routeMatches && (*status != payload->status || outlineState->complete != payload->complete);
			const bool textReady = keepCompleteProjection || textAlreadyCurrent ||
			                       (routeMatches && adoptBentoProjectionText(targetWindow, payload->projectionText, payload->targetDocumentId,
			                                                                  payload->targetVersion, title));
			if (textReady) {
				if (completeCoverageSuperset && !pendingForce) pending = false;
				if (!keepCompleteProjection) {
					*entries = payload->entries;
					*status = payload->status;
					outlineState->documentId = payload->sourceDocumentId;
					outlineState->version = payload->sourceVersion;
					outlineState->targetDocumentId = targetEditor->documentId();
					outlineState->targetVersion = targetEditor->documentVersion();
					outlineState->targetBufferId = targetWindow->bufferId();
					outlineState->textLength = payload->projectionText->size();
					outlineState->textHash = payload->textHash;
					outlineState->language = payload->inputLanguage;
					outlineState->complete = payload->complete;
					outlineState->lastRefresh = std::chrono::steady_clock::now();
				}
				targetWindow->setFileChanged(false);
				taskState->sourceDocumentId = payload->sourceDocumentId;
				taskState->sourceVersion = payload->sourceVersion;
				taskState->inputRevision = currentInputRevision;
				taskState->inputLanguage = payload->inputLanguage;
				taskState->inputComplete = payload->complete;
				taskState->targetDocumentId = targetEditor->documentId();
				taskState->targetVersion = targetEditor->documentVersion();
				taskState->targetBufferId = targetWindow->bufferId();
					taskState->completeCoverageRequested = completeCoverageRequested || keepCompleteProjection || payload->complete;
				taskState->projectionCurrent = true;
				adopted = true;
				projectionChanged = !keepCompleteProjection && (!textAlreadyCurrent || statusChanged);
			}
		}
	}

	if (adopted || baselinePromoted) {
		taskState->retryBlocked = false;
		if (adopted && projectionChanged) {
			updateActivePaneFrame();
			if (hasPaneSplit()) {
				bentoProjectionDirty |= bpdLayout;
				flushBentoProjection();
			}
		}
	}
	else if (!pending) {
		taskState->retryBlocked = result.failed();
		if (role == bprProblems && pendingCompilerProblemNavigation != 0) {
			pendingCompilerProblemNavigation = 0;
			postCompilerNavigationUnavailable();
		}
	}
	taskState->pending = pending;
	taskState->pendingForce = pendingForce;
	taskState->pendingDiagnosticsRequest = pendingDiagnosticsRequest;
	if (role == bprProblems && adopted && !pending && pendingDiagnosticsRequest == bdprNone && pendingCompilerProblemNavigation != 0 &&
	    compilerDiagnosticsCurrent()) {
		const int navigation = pendingCompilerProblemNavigation;
		pendingCompilerProblemNavigation = 0;
		const bool navigated = navigation > 0 ? jumpToNextProblem() : jumpToPreviousProblem();
		if (!navigated) postCompilerNavigationUnavailable();
	}
	resumePendingBentoProjection(*taskState, role);
	bentoProjectionAdoptionActive = adoptionWasActive;
	return adopted;
}
