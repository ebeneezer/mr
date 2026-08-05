#include "MRPerformancePanel.hpp"

#include "../config/settings/MRSettingsRuntimeState.hpp"
#include "../coprocessor/MRCoprocessor.hpp"
#include "../mrmac/vm/MRVMHash.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kActivityMeterColumnCount = 3;
constexpr int kActivityMeterTextColumn = 5;
constexpr int kStatisticLabelWidth = 8;
constexpr int kStatisticValueColumn = kActivityMeterTextColumn + kStatisticLabelWidth + 2;
constexpr std::size_t kWorkerEventReadCount = 256;
constexpr std::chrono::seconds kActivityScaleDecreaseDelay(3);
constexpr TColorAttr kPanelColor = 0x70;
constexpr TColorAttr kLabelColor = 0x17;

std::size_t normalizedActivityScale(std::size_t value, std::size_t scaleStep) noexcept {
	if (value <= scaleStep) return scaleStep;
	return ((value - 1) / scaleStep + 1) * scaleStep;
}

const char *brailleBarGlyph(std::size_t filledRows) noexcept {
	static const char *glyphs[] = {" ", "\xE2\xA3\x80", "\xE2\xA3\xA4", "\xE2\xA3\xB6", "\xE2\xA3\xBF"};
	return glyphs[std::min<std::size_t>(filledRows, 4)];
}

std::string activityScaleText(std::size_t scale) {
	if (scale < 1000) return std::to_string(scale);
	if (scale < 100000) return std::to_string((scale + 999) / 1000) + "k";
	return "99+";
}

TColorAttr activityScaleColor(std::size_t activeCount, std::size_t queuedCount, std::size_t resultCount) noexcept {
	const std::size_t peak = std::max(activeCount, std::max(queuedCount, resultCount));
	if (peak == 0) return 0x71;
	const unsigned int peakOwners = static_cast<unsigned int>(activeCount == peak) + static_cast<unsigned int>(queuedCount == peak) + static_cast<unsigned int>(resultCount == peak);
	if (peakOwners != 1) return 0x71;
	if (activeCount == peak) return 0x74;
	if (queuedCount == peak) return 0x76;
	return 0x72;
}

std::uint64_t totalCounts(const std::array<std::uint64_t, mr::coprocessor::kTaskKindCount> &counts) noexcept {
	std::uint64_t total = 0;
	for (std::uint64_t count : counts) total += count;
	return total;
}

const char *laneName(mr::coprocessor::Lane lane) noexcept {
	switch (lane) {
		case mr::coprocessor::Lane::Io:
			return "IO";
		case mr::coprocessor::Lane::MiniMap:
			return "MAP";
		case mr::coprocessor::Lane::Macro:
			return "MAC";
		case mr::coprocessor::Lane::Extern:
			return "EXT";
		case mr::coprocessor::Lane::Compute:
		default:
			return "CPU";
	}
}

const char *taskName(mr::coprocessor::TaskKind kind) noexcept {
	switch (kind) {
		case mr::coprocessor::TaskKind::LineIndexWarmup:
			return "IDX";
		case mr::coprocessor::TaskKind::DisplayWidthWarmup:
			return "WIDTH";
		case mr::coprocessor::TaskKind::SyntaxWarmup:
			return "SYN";
		case mr::coprocessor::TaskKind::FoldWarmup:
			return "FOLD";
		case mr::coprocessor::TaskKind::MiniMapWarmup:
			return "MAP";
		case mr::coprocessor::TaskKind::FileCompare:
			return "DIFF";
		case mr::coprocessor::TaskKind::ExternalIo:
			return "EXIO";
		case mr::coprocessor::TaskKind::MacroJob:
			return "MACRO";
		case mr::coprocessor::TaskKind::HexPaneProjection:
			return "HEX";
		case mr::coprocessor::TaskKind::BentoDiagnosticsProjection:
			return "BDIAG";
		case mr::coprocessor::TaskKind::BentoOutlineProjection:
			return "BOUT";
		case mr::coprocessor::TaskKind::Custom:
		default:
			return "TASK";
	}
}

const char *stateName(mr::coprocessor::WorkerLifecycleState state) noexcept {
	switch (state) {
		case mr::coprocessor::WorkerLifecycleState::Created:
			return "created";
		case mr::coprocessor::WorkerLifecycleState::Assigned:
			return "assigned";
		case mr::coprocessor::WorkerLifecycleState::Idle:
			return "idle";
		case mr::coprocessor::WorkerLifecycleState::Queued:
			return "queued";
		case mr::coprocessor::WorkerLifecycleState::Running:
			return "running";
		case mr::coprocessor::WorkerLifecycleState::ResultReady:
			return "result-ready";
		case mr::coprocessor::WorkerLifecycleState::Accepted:
			return "accepted";
		case mr::coprocessor::WorkerLifecycleState::Adopted:
			return "adopted";
		case mr::coprocessor::WorkerLifecycleState::Discarded:
			return "discarded";
		case mr::coprocessor::WorkerLifecycleState::Stopping:
			return "stopping";
		case mr::coprocessor::WorkerLifecycleState::Finished:
		default:
			return "finished";
	}
}

const char *ownerName(mr::coprocessor::ExecutionOwnerKind kind) noexcept {
	switch (kind) {
		case mr::coprocessor::ExecutionOwnerKind::EditorWindow:
			return "ED";
		case mr::coprocessor::ExecutionOwnerKind::BentoPane:
			return "BE";
		case mr::coprocessor::ExecutionOwnerKind::HexPane:
			return "HX";
		case mr::coprocessor::ExecutionOwnerKind::ExternalSource:
			return "EX";
		case mr::coprocessor::ExecutionOwnerKind::MacroSession:
			return "MA";
		case mr::coprocessor::ExecutionOwnerKind::ProcessChannel:
			return "PC";
		case mr::coprocessor::ExecutionOwnerKind::Dialog:
			return "DL";
		case mr::coprocessor::ExecutionOwnerKind::Worker:
			return "WK";
		case mr::coprocessor::ExecutionOwnerKind::Unspecified:
		default:
			return "--";
	}
}

const char *directionName(mr::coprocessor::WorkDirection direction) noexcept {
	switch (direction) {
		case mr::coprocessor::WorkDirection::Bof:
			return "BOF";
		case mr::coprocessor::WorkDirection::Eof:
			return "EOF";
		case mr::coprocessor::WorkDirection::None:
		default:
			return "-";
	}
}

TColorAttr stateColor(mr::coprocessor::WorkerLifecycleState state, int affinityResult) noexcept {
	if (affinityResult > 0) return 0x74;
	switch (state) {
		case mr::coprocessor::WorkerLifecycleState::Running:
			return 0x72;
		case mr::coprocessor::WorkerLifecycleState::Queued:
		case mr::coprocessor::WorkerLifecycleState::ResultReady:
		case mr::coprocessor::WorkerLifecycleState::Accepted:
			return 0x76;
		case mr::coprocessor::WorkerLifecycleState::Adopted:
			return 0x72;
		case mr::coprocessor::WorkerLifecycleState::Discarded:
		case mr::coprocessor::WorkerLifecycleState::Stopping:
			return 0x74;
		case mr::coprocessor::WorkerLifecycleState::Created:
		case mr::coprocessor::WorkerLifecycleState::Assigned:
		case mr::coprocessor::WorkerLifecycleState::Idle:
		case mr::coprocessor::WorkerLifecycleState::Finished:
		default:
			return kPanelColor;
	}
}

std::string durationText(std::uint64_t micros) {
	char buffer[32];
	const double milliseconds = static_cast<double>(micros) / 1000.0;

	if (milliseconds < 1000.0) std::snprintf(buffer, sizeof(buffer), "%.1fms", milliseconds);
	else
		std::snprintf(buffer, sizeof(buffer), "%.2fs", milliseconds / 1000.0);
	return buffer;
}

std::string affinityText(int assignedCore, int affinityResult) {
	if (assignedCore < 0) return affinityResult > 0 ? "-!" + std::to_string(affinityResult) : "-";
	std::string text = std::to_string(assignedCore);
	if (affinityResult < 0) text += "?";
	else if (affinityResult > 0)
		text += "!" + std::to_string(affinityResult);
	return text;
}

std::string spanText(std::uint64_t start, std::uint64_t end) {
	return std::to_string(start) + ".." + std::to_string(end);
}

std::string workerEventTimestamp(std::uint64_t eventMicros, std::uint64_t originMicros) {
	char buffer[32];
	const std::uint64_t elapsedMicros = eventMicros >= originMicros ? eventMicros - originMicros : 0;
	const unsigned long long seconds = static_cast<unsigned long long>(elapsedMicros / 1000000);
	const unsigned long long milliseconds = static_cast<unsigned long long>((elapsedMicros % 1000000) / 1000);

	std::snprintf(buffer, sizeof(buffer), "T+%llu.%03llus", seconds, milliseconds);
	return buffer;
}

std::string workerEventLine(const mr::coprocessor::WorkerLifecycleEvent &event, std::uint64_t originMicros) {
	std::string line = workerEventTimestamp(event.monotonicMicros, originMicros);
	line += " W" + std::to_string(event.workerOrdinal);
	line += " owner:";
	line += ownerName(event.executionOwnerKind);
	line += ":" + std::to_string(event.executionOwnerLocalId);
	line += " cpu:" + affinityText(event.assignedCore, event.affinityResult);
	line += " " + std::string(laneName(event.lane));
	line += " " + std::string(stateName(event.state));
	if (event.taskId != 0) {
		const bool idle = event.state == mr::coprocessor::WorkerLifecycleState::Idle ||
		                  event.state == mr::coprocessor::WorkerLifecycleState::Assigned ||
		                  event.state == mr::coprocessor::WorkerLifecycleState::Finished;
		line += idle ? " last:" : " task:";
		line += std::string(taskName(event.taskKind)) + "#" + std::to_string(event.taskId);
		line += " doc:" + std::to_string(event.documentId) + "/v" + std::to_string(event.baseVersion);
	}
	line += " os-tid:" + std::to_string(event.osThreadId);
	line += " q:" + durationText(event.queueMicros) + " run:" + durationText(event.runMicros);
	if (event.acceptanceMicros != 0) line += " accept:" + durationText(event.acceptanceMicros);
	if (event.adoptionMicros != 0) line += " adopt:" + durationText(event.adoptionMicros);
	if (event.generation != 0) line += " gen:" + std::to_string(event.generation);
	if (event.direction != mr::coprocessor::WorkDirection::None) line += " dir:" + std::string(directionName(event.direction));
	if (event.hasPacketSpan) line += " span:" + spanText(event.packetStart, event.packetEnd);
	return line;
}

void appendRefreshValue(std::uint64_t &signature, std::uint64_t value) noexcept {
	signature ^= value;
	signature *= 1099511628211ULL;
}

std::uint64_t panelRefreshSignature(const mr::coprocessor::Snapshot &taskSnapshot, const mr::coprocessor::WorkerTelemetrySnapshot &telemetry, const MRVMHashIoRateSnapshot &hashIoRate, const MRSettingsRuntimeIoRateSnapshot &settingsIoRate) noexcept {
	const mr::coprocessor::WorkerActivitySnapshot &activity = telemetry.recentActivity;
	std::uint64_t signature = 1469598103934665603ULL;
	bool running = false;

	appendRefreshValue(signature, telemetry.latestEventSequence);
	appendRefreshValue(signature, telemetry.createdCount);
	appendRefreshValue(signature, telemetry.finishedCount);
	appendRefreshValue(signature, telemetry.affinityFailureCount);
	for (std::uint64_t count : telemetry.oneShotCreatedByTaskKind) appendRefreshValue(signature, count);
	for (std::uint64_t count : telemetry.oneShotFinishedByTaskKind) appendRefreshValue(signature, count);
	appendRefreshValue(signature, taskSnapshot.pendingResults);
	appendRefreshValue(signature, activity.peakActiveCount);
	appendRefreshValue(signature, activity.peakQueuedCount);
	appendRefreshValue(signature, activity.peakResultCount);
	appendRefreshValue(signature, activity.createdCount);
	appendRefreshValue(signature, activity.finishedCount);
	appendRefreshValue(signature, activity.queuedCount);
	appendRefreshValue(signature, activity.completedCount);
	appendRefreshValue(signature, activity.cancelledCount);
	appendRefreshValue(signature, activity.failedCount);
	appendRefreshValue(signature, activity.acceptedCount);
	appendRefreshValue(signature, activity.adoptedCount);
	appendRefreshValue(signature, activity.discardedCount);
	appendRefreshValue(signature, activity.queueMicros);
	appendRefreshValue(signature, activity.runMicros);
	appendRefreshValue(signature, activity.acceptanceMicros);
	appendRefreshValue(signature, activity.adoptionMicros);
	appendRefreshValue(signature, hashIoRate.readsPerMinute);
	appendRefreshValue(signature, hashIoRate.writesPerMinute);
	appendRefreshValue(signature, settingsIoRate.readsPerMinute);
	appendRefreshValue(signature, settingsIoRate.writesPerMinute);
	for (const mr::coprocessor::WorkerSnapshot &worker : telemetry.workers) {
		appendRefreshValue(signature, worker.workerOrdinal);
		appendRefreshValue(signature, static_cast<std::uint64_t>(worker.lane));
		appendRefreshValue(signature, static_cast<std::uint64_t>(worker.state));
		appendRefreshValue(signature, worker.task.id);
		appendRefreshValue(signature, worker.queuedTaskCount);
		appendRefreshValue(signature, static_cast<std::uint64_t>(worker.assignedCore + 1));
		appendRefreshValue(signature, static_cast<std::uint64_t>(worker.affinityResult + 1));
		if (worker.state == mr::coprocessor::WorkerLifecycleState::Running) running = true;
	}
	for (const mr::coprocessor::ExternalSourceSnapshot &source : taskSnapshot.externalSources) {
		appendRefreshValue(signature, source.sourceId);
		appendRefreshValue(signature, source.running ? 1 : 0);
		appendRefreshValue(signature, source.activitySequence);
	}
	if (running) {
		const std::uint64_t refreshQuarterSecond = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 250);
		appendRefreshValue(signature, refreshQuarterSecond);
	}
	return signature;
}

} // namespace

MRPerformancePanel::WorkerEventSlot::WorkerEventSlot() noexcept
	: occupied(false), workerOrdinal(0), eventSequence(0), line(), color(kPanelColor) {
}

MRPerformancePanel::MRPerformancePanel(const TRect &bounds) noexcept
    : TView(bounds),
      mRefreshInitialized(false),
      mLastRefreshSignature(0),
      mActivityScale(1),
      mActivityScaleColor(0x71),
      mScaleDecreaseAt(std::chrono::steady_clock::time_point::max()),
      mLastWorkerEventSequence(0),
      mWorkerEventTimeOriginMicros(0),
      mWorkerEventSlots() {
	options |= ofBuffered;
	options &= ~ofSelectable;
	eventMask = 0;
}

void MRPerformancePanel::updateActivityScale(std::size_t peakValue, TColorAttr peakColor, std::chrono::steady_clock::time_point now) noexcept {
	const std::size_t scaleStep = static_cast<std::size_t>(std::max(1, size.y - 2)) * 4;
	const std::size_t requiredScale = normalizedActivityScale(peakValue, scaleStep);

	if (requiredScale > mActivityScale) {
		mActivityScale = requiredScale;
		mActivityScaleColor = peakColor;
		mScaleDecreaseAt = std::chrono::steady_clock::time_point::max();
		return;
	}
	if (requiredScale == mActivityScale) {
		mActivityScaleColor = peakColor;
		mScaleDecreaseAt = std::chrono::steady_clock::time_point::max();
		return;
	}
	if (mScaleDecreaseAt == std::chrono::steady_clock::time_point::max()) {
		mScaleDecreaseAt = now + kActivityScaleDecreaseDelay;
		return;
	}
	if (now < mScaleDecreaseAt) return;
	mActivityScale = requiredScale;
	mActivityScaleColor = peakColor;
	mScaleDecreaseAt = std::chrono::steady_clock::time_point::max();
}

void MRPerformancePanel::updateWorkerEventLog(const mr::coprocessor::WorkerTelemetrySnapshot &telemetry) {
	for (std::vector<mr::coprocessor::WorkerLifecycleEvent>::const_reverse_iterator eventIt = telemetry.recentEvents.rbegin(); eventIt != telemetry.recentEvents.rend(); ++eventIt) {
		const mr::coprocessor::WorkerLifecycleEvent &event = *eventIt;
		if (event.sequence <= mLastWorkerEventSequence) continue;
		mLastWorkerEventSequence = event.sequence;
		if (event.workerOrdinal == mr::coprocessor::kInvalidWorkerOrdinal) continue;

		std::size_t slotIndex = kWorkerEventSlotCount;
		for (std::size_t index = 0; index < mWorkerEventSlots.size(); ++index) {
			if (!mWorkerEventSlots[index].occupied || mWorkerEventSlots[index].workerOrdinal != event.workerOrdinal) continue;
			slotIndex = index;
			break;
		}
		if (slotIndex == mWorkerEventSlots.size()) {
			for (std::size_t index = 0; index < mWorkerEventSlots.size(); ++index) {
				if (mWorkerEventSlots[index].occupied) continue;
				slotIndex = index;
				break;
			}
		}
		if (slotIndex == mWorkerEventSlots.size()) {
			slotIndex = 0;
			for (std::size_t index = 1; index < mWorkerEventSlots.size(); ++index)
				if (mWorkerEventSlots[index].eventSequence < mWorkerEventSlots[slotIndex].eventSequence) slotIndex = index;
		}
		if (mWorkerEventTimeOriginMicros == 0) mWorkerEventTimeOriginMicros = event.monotonicMicros;
		WorkerEventSlot &slot = mWorkerEventSlots[slotIndex];
		slot.occupied = true;
		slot.workerOrdinal = event.workerOrdinal;
		slot.eventSequence = event.sequence;
		slot.line = workerEventLine(event, mWorkerEventTimeOriginMicros);
		slot.color = stateColor(event.state, event.affinityResult);
	}
}

void MRPerformancePanel::refresh() noexcept {
	const mr::coprocessor::Snapshot taskSnapshot = mr::coprocessor::globalCoprocessor().snapshot();
	const mr::coprocessor::WorkerTelemetrySnapshot telemetry = mr::coprocessor::globalCoprocessor().telemetrySnapshot(0);
	const MRVMHashIoRateSnapshot hashIoRate = mrvmHashIoRateSnapshot();
	const MRSettingsRuntimeIoRateSnapshot settingsIoRate = settingsRuntimeIoRateSnapshot();
	const mr::coprocessor::WorkerActivitySnapshot &activity = telemetry.recentActivity;
	const std::size_t activePeak = static_cast<std::size_t>(activity.peakActiveCount);
	const std::size_t queuedPeak = static_cast<std::size_t>(activity.peakQueuedCount);
	const std::size_t resultPeak = static_cast<std::size_t>(activity.peakResultCount);
	updateActivityScale(std::max(activePeak, std::max(queuedPeak, resultPeak)), activityScaleColor(activePeak, queuedPeak, resultPeak), std::chrono::steady_clock::now());
	std::uint64_t refreshSignature = panelRefreshSignature(taskSnapshot, telemetry, hashIoRate, settingsIoRate);
	appendRefreshValue(refreshSignature, mActivityScale);

	if (mRefreshInitialized && refreshSignature == mLastRefreshSignature) return;
	mRefreshInitialized = true;
	mLastRefreshSignature = refreshSignature;
	drawView();
}

void MRPerformancePanel::draw() {
	const mr::coprocessor::Snapshot taskSnapshot = mr::coprocessor::globalCoprocessor().snapshot();
	const mr::coprocessor::WorkerTelemetrySnapshot telemetry = mr::coprocessor::globalCoprocessor().telemetrySnapshot(kWorkerEventReadCount);
	const MRVMHashIoRateSnapshot hashIoRate = mrvmHashIoRateSnapshot();
	const MRSettingsRuntimeIoRateSnapshot settingsIoRate = settingsRuntimeIoRateSnapshot();
	const mr::coprocessor::WorkerActivitySnapshot &activity = telemetry.recentActivity;
	std::size_t idleCount = 0;
	std::size_t runningExternalCount = 0;
	std::size_t ioWorkerCount = 0;
	std::size_t computeWorkerCount = 0;
	std::size_t miniMapWorkerCount = 0;
	std::size_t macroWorkerCount = 0;
	std::size_t externalWorkerCount = 0;
	std::size_t editorOwnerCount = 0;
	std::size_t bentoOwnerCount = 0;
	std::size_t hexOwnerCount = 0;
	std::size_t externalOwnerCount = 0;
	std::size_t macroOwnerCount = 0;
	std::size_t processOwnerCount = 0;
	std::size_t dialogOwnerCount = 0;
	std::vector<std::pair<mr::coprocessor::ExecutionOwnerKind, std::size_t>> distinctOwners;
	int y = 0;

	updateWorkerEventLog(telemetry);
	distinctOwners.reserve(telemetry.workers.size());
	for (const mr::coprocessor::WorkerSnapshot &worker : telemetry.workers) {
		if (worker.state == mr::coprocessor::WorkerLifecycleState::Idle || worker.state == mr::coprocessor::WorkerLifecycleState::Assigned) ++idleCount;
		switch (worker.lane) {
			case mr::coprocessor::Lane::Io:
				++ioWorkerCount;
				break;
			case mr::coprocessor::Lane::MiniMap:
				++miniMapWorkerCount;
				break;
			case mr::coprocessor::Lane::Macro:
				++macroWorkerCount;
				break;
			case mr::coprocessor::Lane::Extern:
				++externalWorkerCount;
				break;
			case mr::coprocessor::Lane::Compute:
			default:
				++computeWorkerCount;
				break;
		}
		bool ownerAlreadyCounted = false;
		for (const std::pair<mr::coprocessor::ExecutionOwnerKind, std::size_t> &owner : distinctOwners) {
			if (owner.first != worker.executionOwnerKind || owner.second != worker.executionOwnerLocalId) continue;
			ownerAlreadyCounted = true;
			break;
		}
		if (ownerAlreadyCounted) continue;
		distinctOwners.push_back(std::make_pair(worker.executionOwnerKind, worker.executionOwnerLocalId));
		switch (worker.executionOwnerKind) {
			case mr::coprocessor::ExecutionOwnerKind::EditorWindow:
				++editorOwnerCount;
				break;
			case mr::coprocessor::ExecutionOwnerKind::BentoPane:
				++bentoOwnerCount;
				break;
			case mr::coprocessor::ExecutionOwnerKind::HexPane:
				++hexOwnerCount;
				break;
			case mr::coprocessor::ExecutionOwnerKind::ExternalSource:
				++externalOwnerCount;
				break;
			case mr::coprocessor::ExecutionOwnerKind::MacroSession:
				++macroOwnerCount;
				break;
			case mr::coprocessor::ExecutionOwnerKind::ProcessChannel:
				++processOwnerCount;
				break;
			case mr::coprocessor::ExecutionOwnerKind::Dialog:
				++dialogOwnerCount;
				break;
			case mr::coprocessor::ExecutionOwnerKind::Unspecified:
			case mr::coprocessor::ExecutionOwnerKind::Worker:
			default:
				break;
		}
	}
	for (const mr::coprocessor::ExternalSourceSnapshot &source : taskSnapshot.externalSources)
		if (source.running) ++runningExternalCount;
	const std::size_t activePeak = static_cast<std::size_t>(activity.peakActiveCount);
	const std::size_t queuedPeak = static_cast<std::size_t>(activity.peakQueuedCount);
	const std::size_t resultPeak = static_cast<std::size_t>(activity.peakResultCount);
	updateActivityScale(std::max(activePeak, std::max(queuedPeak, resultPeak)), activityScaleColor(activePeak, queuedPeak, resultPeak), std::chrono::steady_clock::now());

	std::string line = "live:" + std::to_string(telemetry.workers.size()) + "  idle:" + std::to_string(idleCount) + "  external:" + std::to_string(runningExternalCount);
	line += "  created:" + std::to_string(telemetry.createdCount) + "  finished:" + std::to_string(telemetry.finishedCount);
	line += "  one-shot:" + std::to_string(totalCounts(telemetry.oneShotCreatedByTaskKind)) + "/" + std::to_string(totalCounts(telemetry.oneShotFinishedByTaskKind));
	line += "  affinity-errors:" + std::to_string(telemetry.affinityFailureCount);
	writePanelLine(y++, "COPRO", line, telemetry.affinityFailureCount == 0 ? kPanelColor : static_cast<TColorAttr>(0x74));

	line = "1s  workers:+" + std::to_string(activity.createdCount) + "/-" + std::to_string(activity.finishedCount);
	line += "  done:" + std::to_string(activity.completedCount) + "  accepted:" + std::to_string(activity.acceptedCount) + "  adopted:" + std::to_string(activity.adoptedCount) + "  discarded:" + std::to_string(activity.discardedCount);
	line += "  cancelled:" + std::to_string(activity.cancelledCount) + "  failed:" + std::to_string(activity.failedCount);
	line += "  q/run/accept/adopt:" + durationText(activity.queueMicros) + "/" + durationText(activity.runMicros) + "/" + durationText(activity.acceptanceMicros) + "/" + durationText(activity.adoptionMicros);
	writePanelLine(y++, "ACTIVITY", line, kPanelColor);

	line = "IO:" + std::to_string(ioWorkerCount) + "  CPU:" + std::to_string(computeWorkerCount);
	line += "  MAP:" + std::to_string(miniMapWorkerCount) + "  MAC:" + std::to_string(macroWorkerCount);
	line += "  EXT:" + std::to_string(externalWorkerCount);
	line += "  OWNERS ED:" + std::to_string(editorOwnerCount) + " BE:" + std::to_string(bentoOwnerCount);
	line += " HX:" + std::to_string(hexOwnerCount) + " EX:" + std::to_string(externalOwnerCount);
	line += " MA:" + std::to_string(macroOwnerCount) + " PC:" + std::to_string(processOwnerCount) + " DL:" + std::to_string(dialogOwnerCount);
	line += "  IO/min VM-hash:" + std::to_string(hashIoRate.readsPerMinute) + "/" + std::to_string(hashIoRate.writesPerMinute);
	line += "  settings:" + std::to_string(settingsIoRate.readsPerMinute) + "/" + std::to_string(settingsIoRate.writesPerMinute);
	writePanelLine(y++, "LANES", line, kPanelColor);

	for (std::size_t row = 0; row < mWorkerEventSlots.size() && y < size.y; ++row) {
		line.clear();
		TColorAttr color = kPanelColor;
		if (mWorkerEventSlots[row].occupied) {
			line = mWorkerEventSlots[row].line;
			color = mWorkerEventSlots[row].color;
		}
		writePanelLine(y++, row == 0 ? "EVENTS" : "", line, color);
	}
	while (y < size.y)
		writePanelLine(y++, nullptr, std::string(), kPanelColor);
	drawActivityMeter(activePeak, queuedPeak, resultPeak);
}

void MRPerformancePanel::drawActivityMeter(std::size_t activeCount, std::size_t queuedCount, std::size_t resultCount) {
	const std::size_t values[kActivityMeterColumnCount] = {activeCount, queuedCount, resultCount};
	const TColorAttr colors[kActivityMeterColumnCount] = {static_cast<TColorAttr>(0x74), static_cast<TColorAttr>(0x76), static_cast<TColorAttr>(0x72)};
	const char labels[kActivityMeterColumnCount] = {'A', 'Q', 'R'};
	const int meterRows = std::max(0, size.y - 2);
	const std::size_t dotCapacity = static_cast<std::size_t>(meterRows) * 4;
	if (size.y > 0) {
		TDrawBuffer buffer;
		std::string scale = activityScaleText(mActivityScale);
		if (scale.size() > static_cast<std::size_t>(kActivityMeterColumnCount)) scale.resize(kActivityMeterColumnCount);
		const int scaleX = kActivityMeterColumnCount - static_cast<int>(scale.size());

		buffer.moveChar(0, ' ', kPanelColor, kActivityMeterColumnCount);
		buffer.moveStr(static_cast<ushort>(scaleX), scale.c_str(), mActivityScaleColor, static_cast<ushort>(scale.size()));
		writeBuf(0, 0, std::min(kActivityMeterColumnCount, static_cast<int>(size.x)), 1, buffer);
	}

	for (int column = 0; column < kActivityMeterColumnCount && column < size.x; ++column) {
		const std::size_t clampedValue = std::min(values[column], mActivityScale);
		const std::size_t filledDots = clampedValue == 0 || dotCapacity == 0 ? 0 : (clampedValue * dotCapacity + mActivityScale - 1) / mActivityScale;
		for (int meterY = 0; meterY < meterRows; ++meterY) {
			const std::size_t rowFromBottom = static_cast<std::size_t>(meterRows - meterY - 1);
			const std::size_t firstDot = rowFromBottom * 4;
			const std::size_t filledRows = filledDots <= firstDot ? 0 : std::min<std::size_t>(4, filledDots - firstDot);
			TDrawBuffer buffer;

			buffer.moveChar(0, ' ', kPanelColor, 1);
			if (filledRows != 0) buffer.moveStr(0, brailleBarGlyph(filledRows), colors[column], 1);
			writeBuf(column, meterY + 1, 1, 1, buffer);
		}
		if (size.y > 1) {
			TDrawBuffer buffer;

			buffer.moveChar(0, labels[column], colors[column], 1);
			writeBuf(column, size.y - 1, 1, 1, buffer);
		}
	}
}

TPalette &MRPerformancePanel::getPalette() const {
	static const TColorAttr paletteData[] = {0x70, 0x70, 0x70, 0x70};
	static TPalette palette(paletteData, 4);
	return palette;
}

void MRPerformancePanel::writePanelLine(int y, const char *label, const std::string &line, TColorAttr color) {
	TDrawBuffer buffer;
	std::string text = line;
	const int textWidth = std::max(0, size.x - kStatisticValueColumn);

	if (y < 0 || y >= size.y) return;
	for (char &ch : text)
		if (static_cast<unsigned char>(ch) < 32) ch = ' ';
	if (text.size() > static_cast<std::size_t>(textWidth)) text.resize(static_cast<std::size_t>(textWidth));
	buffer.moveChar(0, ' ', kPanelColor, size.x);
	if (label != nullptr && kActivityMeterTextColumn < size.x) {
		const int labelAreaWidth = std::min(kStatisticLabelWidth + 1, static_cast<int>(size.x) - kActivityMeterTextColumn);
		buffer.moveChar(kActivityMeterTextColumn, ' ', kLabelColor, static_cast<ushort>(labelAreaWidth));
		if (label[0] != '\0') {
			std::string labelText = label;
			if (labelText.size() > static_cast<std::size_t>(kStatisticLabelWidth)) labelText.resize(kStatisticLabelWidth);
			labelText.insert(0, static_cast<std::size_t>(kStatisticLabelWidth) - labelText.size(), ' ');
			labelText += ':';
			buffer.moveStr(kActivityMeterTextColumn, labelText.c_str(), kLabelColor, static_cast<ushort>(labelAreaWidth));
		}
	}
	if (!text.empty() && textWidth > 0) buffer.moveStr(kStatisticValueColumn, text.c_str(), color, static_cast<ushort>(textWidth));
	writeBuf(0, y, size.x, 1, buffer);
}
