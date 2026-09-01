#include "MRGdbMi.hpp"

#include <cctype>
#include <cstdlib>

namespace {

std::string decodeMiString(const std::string &text, std::size_t quotePosition, std::size_t *endPosition = nullptr) {
	std::string decoded;
	if (quotePosition >= text.size() || text[quotePosition] != '"') return decoded;
	for (std::size_t index = quotePosition + 1; index < text.size(); ++index) {
		const char current = text[index];
		if (current == '"') {
			if (endPosition != nullptr) *endPosition = index + 1;
			return decoded;
		}
		if (current != '\\' || index + 1 >= text.size()) {
			decoded += current;
			continue;
		}
		const char escaped = text[++index];
		switch (escaped) {
			case 'n': decoded += '\n'; break;
			case 'r': decoded += '\r'; break;
			case 't': decoded += '\t'; break;
			case 'b': decoded += '\b'; break;
			case 'f': decoded += '\f'; break;
			case 'v': decoded += '\v'; break;
			case '\\':
			case '"': decoded += escaped; break;
			default:
				if (escaped >= '0' && escaped <= '7') {
					int value = escaped - '0';
					int count = 1;
					while (count < 3 && index + 1 < text.size() && text[index + 1] >= '0' && text[index + 1] <= '7') {
						value = value * 8 + text[++index] - '0';
						++count;
					}
					decoded += static_cast<char>(value);
				} else decoded += escaped;
				break;
		}
	}
	if (endPosition != nullptr) *endPosition = text.size();
	return decoded;
}

MRGdbMiRecord parseMiRecord(const std::string &line) {
	MRGdbMiRecord record;
	std::size_t position = 0;
	record.raw = line;
	while (position < line.size() && std::isdigit(static_cast<unsigned char>(line[position]))) {
		record.token = record.token * 10U + static_cast<unsigned>(line[position] - '0');
		++position;
	}
	if (position >= line.size()) return record;
	const char prefix = line[position++];
	switch (prefix) {
		case '^': record.kind = MRGdbMiRecordKind::Result; break;
		case '*': record.kind = MRGdbMiRecordKind::Exec; break;
		case '+': record.kind = MRGdbMiRecordKind::Status; break;
		case '=': record.kind = MRGdbMiRecordKind::Notify; break;
		case '~': record.kind = MRGdbMiRecordKind::Console; record.text = decodeMiString(line, position); return record;
		case '@': record.kind = MRGdbMiRecordKind::Target; record.text = decodeMiString(line, position); return record;
		case '&': record.kind = MRGdbMiRecordKind::Log; record.text = decodeMiString(line, position); return record;
		default: if (line == "(gdb)") record.kind = MRGdbMiRecordKind::Prompt; return record;
	}
	const std::size_t comma = line.find(',', position);
	record.resultClass = line.substr(position, comma == std::string::npos ? std::string::npos : comma - position);
	return record;
}

std::size_t fieldPosition(const std::string &record, const char *name, std::size_t start = 0) {
	const std::string marker = std::string(name) + "=";
	std::size_t position = start;
	while ((position = record.find(marker, position)) != std::string::npos) {
		if (position == 0 || record[position - 1] == ',' || record[position - 1] == '{' || record[position - 1] == '[') return position + marker.size();
		position += marker.size();
	}
	return std::string::npos;
}

std::string fieldAfter(const std::string &record, const char *name, std::size_t start) {
	const std::size_t valuePosition = fieldPosition(record, name, start);
	if (valuePosition == std::string::npos) return std::string();
	if (valuePosition < record.size() && record[valuePosition] == '"') return decodeMiString(record, valuePosition);
	const std::size_t end = record.find_first_of(",}]", valuePosition);
	return record.substr(valuePosition, end == std::string::npos ? std::string::npos : end - valuePosition);
}

} // namespace

MRGdbMiRecord::MRGdbMiRecord() noexcept : kind(MRGdbMiRecordKind::Unknown), token(0), resultClass(), text(), raw() {}

void MRGdbMiStream::append(const char *data, std::size_t size, std::vector<MRGdbMiRecord> &records) {
	pending.append(data, size);
	for (;;) {
		const std::size_t newline = pending.find('\n');
		if (newline == std::string::npos) break;
		std::string line = pending.substr(0, newline);
		pending.erase(0, newline + 1);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (!line.empty()) records.push_back(parseMiRecord(line));
	}
}

void MRGdbMiStream::finish(std::vector<MRGdbMiRecord> &records) {
	if (pending.empty()) return;
	if (pending.back() == '\r') pending.pop_back();
	if (!pending.empty()) records.push_back(parseMiRecord(pending));
	pending.clear();
}

std::string mrGdbMiQuote(const std::string &value) {
	std::string quoted("\"");
	for (const char character : value) {
		switch (character) {
			case '\\': quoted += "\\\\"; break;
			case '"': quoted += "\\\""; break;
			case '\n': quoted += "\\n"; break;
			case '\r': quoted += "\\r"; break;
			case '\t': quoted += "\\t"; break;
			default: quoted += character; break;
		}
	}
	quoted += '"';
	return quoted;
}

std::string mrGdbMiField(const std::string &record, const char *name) { return fieldAfter(record, name, 0); }

int mrGdbMiIntField(const std::string &record, const char *name, int fallback) {
	const std::string value = mrGdbMiField(record, name);
	char *end = nullptr;
	const long parsed = std::strtol(value.c_str(), &end, 10);
	return !value.empty() && end != nullptr && *end == '\0' ? static_cast<int>(parsed) : fallback;
}

void mrGdbMiVariables(const std::string &record, std::vector<MRGdbMiVariable> &variables) {
	std::size_t position = record.find("variables=[");
	variables.clear();
	if (position == std::string::npos) return;
	while ((position = record.find("{name=", position)) != std::string::npos) {
		const std::size_t end = record.find('}', position);
		const std::string item = record.substr(position, end == std::string::npos ? std::string::npos : end - position + 1);
		MRGdbMiVariable variable;
		variable.name = mrGdbMiField(item, "name");
		variable.value = mrGdbMiField(item, "value");
		variable.type = mrGdbMiField(item, "type");
		if (!variable.name.empty()) variables.push_back(variable);
		if (end == std::string::npos) break;
		position = end + 1;
	}
}

void mrGdbMiChanges(const std::string &record, std::vector<MRGdbMiVariable> &variables) {
	std::size_t position = record.find("changelist=[");
	variables.clear();
	if (position == std::string::npos) return;
	while ((position = record.find("{name=", position)) != std::string::npos) {
		const std::size_t end = record.find('}', position);
		const std::string item = record.substr(position, end == std::string::npos ? std::string::npos : end - position + 1);
		MRGdbMiVariable variable;
		variable.name = mrGdbMiField(item, "name");
		variable.value = mrGdbMiField(item, "value");
		variable.type = mrGdbMiField(item, "in_scope");
		if (!variable.name.empty()) variables.push_back(variable);
		if (end == std::string::npos) break;
		position = end + 1;
	}
}

void mrGdbMiBreakpoints(const std::string &record, std::vector<MRGdbMiBreakpoint> &breakpoints) {
	std::size_t position = record.find("body=[");
	breakpoints.clear();
	if (position == std::string::npos) position = record.find("bkpt={");
	if (position == std::string::npos) return;
	while ((position = record.find("number=", position)) != std::string::npos) {
		const std::size_t end = record.find('}', position);
		const std::string item = "{" + record.substr(position, end == std::string::npos ? std::string::npos : end - position + 1);
		MRGdbMiBreakpoint breakpoint;
		breakpoint.number = mrGdbMiField(item, "number");
		breakpoint.file = mrGdbMiField(item, "fullname");
		if (breakpoint.file.empty()) breakpoint.file = mrGdbMiField(item, "file");
		breakpoint.line = mrGdbMiIntField(item, "line", 0);
		if (!breakpoint.number.empty()) breakpoints.push_back(breakpoint);
		if (end == std::string::npos) break;
		position = end + 1;
	}
}
