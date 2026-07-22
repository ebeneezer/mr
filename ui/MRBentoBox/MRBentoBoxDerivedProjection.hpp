#ifndef MRBENTOBOXDERIVEDPROJECTION_HPP
#define MRBENTOBOXDERIVEDPROJECTION_HPP

#include "../../coprocessor/MRCoprocessor.hpp"
#include "../../outline/MROutlineModel.hpp"
#include "../MRTextBufferModel.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct MRCompilerDiagnostic {
	MRCompilerDiagnostic() noexcept;

	std::string sourcePath;
	std::size_t sourceLine;
	std::size_t sourceColumn;
	std::string severity;
	std::string text;
	std::size_t sourceOffset;
	std::size_t outputOffset;
	std::size_t problemOffset;
	bool sourceAvailable;
};

struct MRBentoOutlineEntry {
	MRBentoOutlineEntry() noexcept;

	std::size_t paneOffset;
	std::size_t sourceOffset;
	std::size_t sourceSelectionEnd;
};

struct MRBentoDiagnosticSourceChange {
	MRTextBufferModel::ReadSnapshot oldSnapshot;
	MRTextBufferModel::ReadSnapshot newSnapshot;
	MRTextBufferModel::DocumentChangeSet changeSet;
	std::size_t depth;
	std::shared_ptr<const MRBentoDiagnosticSourceChange> previous;

	MRBentoDiagnosticSourceChange() noexcept;
	MRBentoDiagnosticSourceChange(const MRTextBufferModel::ReadSnapshot &anOldSnapshot,
	                              const MRTextBufferModel::ReadSnapshot &aNewSnapshot,
	                              const MRTextBufferModel::DocumentChangeSet &aChangeSet,
	                              std::shared_ptr<const MRBentoDiagnosticSourceChange> aPrevious);
};

struct MRBentoDiagnosticsProjectionPayload final : mr::coprocessor::Payload {
	std::uint64_t generation;
	std::size_t sourceDocumentId;
	std::size_t sourceVersion;
	std::size_t outputDocumentId;
	std::size_t outputVersion;
	std::size_t targetDocumentId;
	std::size_t targetVersion;
	std::string sourcePath;
	bool trackWarnings;
	bool trackNotes;
	std::shared_ptr<const std::string> projectionText;
	std::shared_ptr<const std::vector<MRCompilerDiagnostic>> diagnostics;
	std::shared_ptr<const std::vector<MRTextBufferModel::Range>> sourceErrorRanges;
	std::shared_ptr<const std::vector<MRTextBufferModel::Range>> sourceWarningRanges;
	std::string status;
	std::uint64_t textHash;

	MRBentoDiagnosticsProjectionPayload() noexcept;
};

struct MRBentoOutlineProjectionPayload final : mr::coprocessor::Payload {
	std::uint64_t generation;
	bool functions;
	MRSyntaxLanguage inputLanguage;
	std::size_t sourceDocumentId;
	std::size_t sourceVersion;
	std::uint64_t inputRevision;
	std::size_t targetDocumentId;
	std::size_t targetVersion;
	std::shared_ptr<const std::string> projectionText;
	std::shared_ptr<const std::vector<MRBentoOutlineEntry>> entries;
	std::string status;
	std::uint64_t textHash;
	bool complete;

	MRBentoOutlineProjectionPayload() noexcept;
};

[[nodiscard]] std::shared_ptr<const MRBentoDiagnosticsProjectionPayload> mrBuildBentoDiagnosticsProjection(
	const MRTextBufferModel::ReadSnapshot &sourceSnapshot, const MRTextBufferModel::ReadSnapshot &diagnosticSourceSnapshot,
	const MRTextBufferModel::ReadSnapshot &outputSnapshot,
	std::size_t targetDocumentId, std::size_t targetVersion, std::uint64_t generation, const std::string &sourcePath,
	bool trackWarnings, bool trackNotes, bool parseOutput, std::shared_ptr<const std::vector<MRCompilerDiagnostic>> diagnostics,
	std::shared_ptr<const MRBentoDiagnosticSourceChange> sourceChanges,
	const std::atomic_bool *cancelFlag, std::string *errorMessage);

[[nodiscard]] std::shared_ptr<const MRBentoOutlineProjectionPayload> mrBuildBentoOutlineProjection(
	const MRTextBufferModel::ReadSnapshot &sourceSnapshot, const MROutlineSnapshot &outlineSnapshot, bool snapshotReady,
	MRSyntaxLanguage inputLanguage, bool completeWarmupRequested, bool functions, std::size_t targetDocumentId, std::size_t targetVersion,
	std::uint64_t generation, std::uint64_t inputRevision, const std::atomic_bool *cancelFlag);

#endif
