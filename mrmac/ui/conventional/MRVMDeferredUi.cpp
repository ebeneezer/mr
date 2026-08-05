#include "MRVMDeferredUi.hpp"
#include "../../vm/MRVMRuntimeState.hpp"
#include "../../vm/MRVMValue.hpp"
#include "../../../app/utils/MRFileIOUtils.hpp"
#include "../../../config/settings/MRSettingsRuntime.hpp"

#include "../../mrmac.h"

#define Uses_TText
#include <tvision/tv.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Value = VirtualMachine::Value;
}

bool currentExecutingMacroSpec(std::string &macroSpec);

namespace {
constexpr const char *kDeferredWorkingMessageText = "working...";

bool isStringLike(const Value &value) {
	return value.type == TYPE_STR || value.type == TYPE_CHAR;
}

bool isNumeric(const Value &value) {
	return value.type == TYPE_INT || value.type == TYPE_REAL || value.type == TYPE_CHAR;
}

std::string valueAsString(const Value &value) {
	switch (value.type) {
		case TYPE_STR:
			return value.s;
		case TYPE_CHAR:
			return std::string(1, static_cast<char>(value.i));
		case TYPE_INT:
			return std::to_string(value.i);
		case TYPE_REAL: {
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "%.15g", value.r);
			return std::string(buffer);
		}
		default:
			return std::string();
	}
}

int valueAsInt(const Value &value) {
	switch (value.type) {
		case TYPE_INT:
		case TYPE_CHAR:
			return value.i;
		case TYPE_REAL:
			return static_cast<int>(value.r);
		default:
			return 0;
	}
}

struct AnsiImage {
	int width = 0;
	int height = 0;
	std::string characters;
	std::string attributes;

	void applyDesktopMonochrome(unsigned char desktopAttribute) {
		static constexpr unsigned char ansiMonochromeCoverage[16] = {
		    0, 2, 6, 9, 4, 7, 10, 12, 3, 5, 11, 14, 8, 13, 15, 16};
		static constexpr unsigned char bayerThreshold[4][4] = {
		    {0, 8, 2, 10},
		    {12, 4, 14, 6},
		    {3, 11, 1, 9},
		    {15, 7, 13, 5}};
		static constexpr unsigned char shadeCharacters[4] = {0xB0, 0xB1, 0xB2, 0xDB};
		const unsigned char desktopForeground = desktopAttribute & 0x0F;
		const unsigned char desktopBackground = (desktopAttribute >> 4) & 0x0F;
		const unsigned char inverseDesktopAttribute = static_cast<unsigned char>((desktopForeground << 4) | desktopBackground);

		for (int row = 0; row < height; ++row)
			for (int column = 0; column < width; ++column) {
				const std::size_t index = static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(column);
				const unsigned char sourceCharacter = static_cast<unsigned char>(characters[index]);
				const unsigned char sourceAttribute = static_cast<unsigned char>(attributes[index]);
				const unsigned char foreground = sourceAttribute & 0x0F;
				const unsigned char background = (sourceAttribute >> 4) & 0x0F;
				const int foregroundCoverage = ansiMonochromeCoverage[foreground];
				const int backgroundCoverage = ansiMonochromeCoverage[background];
				int topCoverage = 0;
				int bottomCoverage = 0;

				switch (sourceCharacter) {
					case 0x20:
						topCoverage = backgroundCoverage;
						bottomCoverage = backgroundCoverage;
						break;
					case 0xB0:
						topCoverage = (foregroundCoverage + backgroundCoverage * 3 + 2) / 4;
						bottomCoverage = topCoverage;
						break;
					case 0xB1:
						topCoverage = (foregroundCoverage + backgroundCoverage + 1) / 2;
						bottomCoverage = topCoverage;
						break;
					case 0xB2:
						topCoverage = (foregroundCoverage * 3 + backgroundCoverage + 2) / 4;
						bottomCoverage = topCoverage;
						break;
					case 0xDB:
						topCoverage = foregroundCoverage;
						bottomCoverage = foregroundCoverage;
						break;
					case 0xDC:
						topCoverage = backgroundCoverage;
						bottomCoverage = foregroundCoverage;
						break;
					case 0xDF:
						topCoverage = foregroundCoverage;
						bottomCoverage = backgroundCoverage;
						break;
					default:
						attributes[index] = static_cast<char>(foregroundCoverage >= backgroundCoverage ? desktopAttribute : inverseDesktopAttribute);
						continue;
				}

				if (topCoverage != bottomCoverage) {
					const int threshold = bayerThreshold[row & 3][column & 3];
					const bool topVisible = topCoverage > threshold;
					const bool bottomVisible = bottomCoverage > threshold;

					if (topVisible && bottomVisible)
						characters[index] = static_cast<char>(0xDB);
					else if (topVisible)
						characters[index] = static_cast<char>(0xDF);
					else if (bottomVisible)
						characters[index] = static_cast<char>(0xDC);
					else
						characters[index] = static_cast<char>(0xB0);
					attributes[index] = static_cast<char>(desktopAttribute);
					continue;
				}

				const int shadeNumerator = (topCoverage + bottomCoverage) * 3;
				int shadeIndex = shadeNumerator / 32;
				const int shadeRemainder = shadeNumerator % 32;
				if (shadeIndex < 3 && shadeRemainder > bayerThreshold[row & 3][column & 3] * 2) ++shadeIndex;
				characters[index] = static_cast<char>(shadeCharacters[shadeIndex]);
				attributes[index] = static_cast<char>(desktopAttribute);
			}
	}
};

class AnsiImageParser {
  public:
	explicit AnsiImageParser(const std::string &sourceText) noexcept : source(sourceText) {
	}

	bool parse(AnsiImage &image, std::string &errorText) {
		std::size_t offset = 0;

		error.clear();
		while (offset < source.size()) {
			const unsigned char byte = static_cast<unsigned char>(source[offset]);
			if (byte == 0x1B) {
				if (!parseEscape(offset)) return finishError(errorText);
				continue;
			}
			if (byte == '\r') {
				cursorColumn = 0;
				++offset;
				continue;
			}
			if (byte == '\n') {
				cursorColumn = 0;
				++cursorRow;
				++offset;
				continue;
			}
			if (byte == '\b') {
				cursorColumn = std::max(0, cursorColumn - 1);
				++offset;
				continue;
			}
			if (byte == '\t') {
				cursorColumn = ((cursorColumn / 8) + 1) * 8;
				++offset;
				continue;
			}
			if (byte == 0x1A) break;
			if (byte < 0x20 || byte == 0x7F) return fail("unsupported control character", offset, errorText);
			if (byte <= 0x7F) {
				if (!writeCharacter(static_cast<char>(byte), offset)) return finishError(errorText);
				++offset;
				continue;
			}

			const TStringView remaining(source.data() + offset, source.size() - offset);
			const std::size_t length = TText::next(remaining);
			if (length == 0 || length > remaining.size()) return fail("invalid UTF-8 character", offset, errorText);
			const char character = TText::toCodePage(remaining.substr(0, length));
			if (character == '\0') return fail("UTF-8 character is not representable in the active TVision code page", offset, errorText);
			if (!writeCharacter(character, offset)) return finishError(errorText);
			offset += length;
		}

		if (imageWidth <= 0 || imageHeight <= 0) return fail("ANSI image contains no drawable cells", offset, errorText);
		const std::size_t cellCount = static_cast<std::size_t>(imageWidth) * static_cast<std::size_t>(imageHeight);
		image.width = imageWidth;
		image.height = imageHeight;
		image.characters.assign(cellCount, ' ');
		image.attributes.assign(cellCount, static_cast<char>(0x07));
		for (std::size_t row = 0; row < rows.size() && row < static_cast<std::size_t>(imageHeight); ++row)
			for (std::size_t column = 0; column < rows[row].size() && column < static_cast<std::size_t>(imageWidth); ++column) {
				const std::size_t index = row * static_cast<std::size_t>(imageWidth) + column;
				image.characters[index] = rows[row][column].character;
				image.attributes[index] = static_cast<char>(rows[row][column].attribute);
			}
		errorText.clear();
		return true;
	}

  private:
	struct Cell {
		char character = ' ';
		unsigned char attribute = 0x07;
	};

	const std::string &source;
	std::vector<std::vector<Cell>> rows;
	std::string error;
	int cursorColumn = 0;
	int cursorRow = 0;
	int savedColumn = 0;
	int savedRow = 0;
	int foreground = 7;
	int background = 0;
	int imageWidth = 0;
	int imageHeight = 0;
	bool bold = false;
	bool brightForeground = false;
	bool inverse = false;

	bool finishError(std::string &errorText) {
		errorText = error;
		return false;
	}

	bool fail(const std::string &message, std::size_t offset, std::string &errorText) {
		setError(message, offset);
		return finishError(errorText);
	}

	void setError(const std::string &message, std::size_t offset) {
		if (error.empty()) error = message + " at byte " + std::to_string(offset);
	}

	static unsigned char biosColor(int ansiColor) noexcept {
		switch (ansiColor) {
			case 0:
				return 0;
			case 1:
				return 4;
			case 2:
				return 2;
			case 3:
				return 6;
			case 4:
				return 1;
			case 5:
				return 5;
			case 6:
				return 3;
			case 7:
				return 7;
			case 8:
				return 8;
			case 9:
				return 12;
			case 10:
				return 10;
			case 11:
				return 14;
			case 12:
				return 9;
			case 13:
				return 13;
			case 14:
				return 11;
			case 15:
				return 15;
			default:
				return 0;
		}
	}

	unsigned char currentAttribute() const noexcept {
		const int effectiveForeground = foreground + ((bold || brightForeground) ? 8 : 0);
		const int cellForeground = inverse ? background : effectiveForeground;
		const int cellBackground = inverse ? effectiveForeground : background;
		return static_cast<unsigned char>((biosColor(cellBackground) << 4) | biosColor(cellForeground));
	}

	bool writeCharacter(char character, std::size_t offset) {
		constexpr int maximumDimension = 4096;
		constexpr std::size_t maximumCellCount = 1024 * 1024;

		if (cursorColumn < 0 || cursorRow < 0 || cursorColumn >= maximumDimension || cursorRow >= maximumDimension) {
			setError("ANSI cursor position exceeds the supported image dimensions", offset);
			return false;
		}
		const int nextWidth = std::max(imageWidth, cursorColumn + 1);
		const int nextHeight = std::max(imageHeight, cursorRow + 1);
		if (static_cast<std::size_t>(nextWidth) > maximumCellCount / static_cast<std::size_t>(nextHeight)) {
			setError("ANSI image exceeds the supported cell count", offset);
			return false;
		}
		if (rows.size() <= static_cast<std::size_t>(cursorRow)) rows.resize(static_cast<std::size_t>(cursorRow) + 1);
		std::vector<Cell> &line = rows[static_cast<std::size_t>(cursorRow)];
		if (line.size() <= static_cast<std::size_t>(cursorColumn)) line.resize(static_cast<std::size_t>(cursorColumn) + 1);
		line[static_cast<std::size_t>(cursorColumn)].character = character;
		line[static_cast<std::size_t>(cursorColumn)].attribute = currentAttribute();
		imageWidth = nextWidth;
		imageHeight = nextHeight;
		++cursorColumn;
		return true;
	}

	bool parseEscape(std::size_t &offset) {
		if (offset + 1 >= source.size()) {
			setError("truncated ANSI escape sequence", offset);
			return false;
		}
		const char next = source[offset + 1];
		if (next == '7') {
			savedColumn = cursorColumn;
			savedRow = cursorRow;
			offset += 2;
			return true;
		}
		if (next == '8') {
			cursorColumn = savedColumn;
			cursorRow = savedRow;
			offset += 2;
			return true;
		}
		if (next != '[') {
			setError("unsupported ANSI escape sequence", offset);
			return false;
		}
		return parseCsi(offset);
	}

	bool parseCsi(std::size_t &offset) {
		const std::size_t sequenceOffset = offset;
		std::size_t finalOffset = offset + 2;

		while (finalOffset < source.size()) {
			const unsigned char byte = static_cast<unsigned char>(source[finalOffset]);
			if (byte >= 0x40 && byte <= 0x7E) break;
			if (byte < 0x20 || byte > 0x3F) {
				setError("invalid ANSI CSI sequence", sequenceOffset);
				return false;
			}
			++finalOffset;
		}
		if (finalOffset >= source.size()) {
			setError("truncated ANSI CSI sequence", sequenceOffset);
			return false;
		}

		std::string parametersText = source.substr(offset + 2, finalOffset - offset - 2);
		bool privateMode = false;
		if (!parametersText.empty() && parametersText.front() == '?') {
			privateMode = true;
			parametersText.erase(parametersText.begin());
		}
		std::vector<int> parameters;
		if (!parseParameters(parametersText, parameters, sequenceOffset)) return false;
		if (!applyCsi(source[finalOffset], parameters, privateMode, sequenceOffset)) return false;
		offset = finalOffset + 1;
		return true;
	}

	bool parseParameters(const std::string &text, std::vector<int> &parameters, std::size_t offset) {
		if (text.empty()) return true;
		std::size_t start = 0;
		for (;;) {
			const std::size_t end = text.find(';', start);
			const std::size_t length = (end == std::string::npos ? text.size() : end) - start;
			int value = 0;
			for (std::size_t index = 0; index < length; ++index) {
				const char digit = text[start + index];
				if (digit < '0' || digit > '9' || value > (std::numeric_limits<int>::max() - (digit - '0')) / 10) {
					setError("invalid ANSI CSI parameter", offset);
					return false;
				}
				value = value * 10 + (digit - '0');
			}
			parameters.push_back(value);
			if (end == std::string::npos) return true;
			start = end + 1;
		}
	}

	static int parameter(const std::vector<int> &parameters, std::size_t index, int fallback) noexcept {
		if (index >= parameters.size() || parameters[index] == 0) return fallback;
		return parameters[index];
	}

	bool setCursor(std::int64_t column, std::int64_t row, std::size_t offset) {
		constexpr std::int64_t maximumDimension = 4096;

		if (column < 0 || row < 0 || column >= maximumDimension || row >= maximumDimension) {
			setError("ANSI cursor position exceeds the supported image dimensions", offset);
			return false;
		}
		cursorColumn = static_cast<int>(column);
		cursorRow = static_cast<int>(row);
		return true;
	}

	bool applyCsi(char command, const std::vector<int> &parameters, bool privateMode, std::size_t offset) {
		if (privateMode) {
			if ((command == 'h' || command == 'l') && parameters.size() == 1 && parameters[0] == 25) return true;
			setError("unsupported private ANSI CSI sequence", offset);
			return false;
		}
		switch (command) {
			case 'm':
				return applySgr(parameters, offset);
			case 'H':
			case 'f':
				return setCursor(static_cast<std::int64_t>(parameter(parameters, 1, 1)) - 1, static_cast<std::int64_t>(parameter(parameters, 0, 1)) - 1, offset);
			case 'A':
				return setCursor(cursorColumn, std::max<std::int64_t>(0, static_cast<std::int64_t>(cursorRow) - parameter(parameters, 0, 1)), offset);
			case 'B':
				return setCursor(cursorColumn, static_cast<std::int64_t>(cursorRow) + parameter(parameters, 0, 1), offset);
			case 'C':
				return setCursor(static_cast<std::int64_t>(cursorColumn) + parameter(parameters, 0, 1), cursorRow, offset);
			case 'D':
				return setCursor(std::max<std::int64_t>(0, static_cast<std::int64_t>(cursorColumn) - parameter(parameters, 0, 1)), cursorRow, offset);
			case 'G':
				return setCursor(static_cast<std::int64_t>(parameter(parameters, 0, 1)) - 1, cursorRow, offset);
			case 'd':
				return setCursor(cursorColumn, static_cast<std::int64_t>(parameter(parameters, 0, 1)) - 1, offset);
			case 's':
				savedColumn = cursorColumn;
				savedRow = cursorRow;
				return true;
			case 'u':
				cursorColumn = savedColumn;
				cursorRow = savedRow;
				return true;
			case 'J':
				if (parameters.size() <= 1 && parameter(parameters, 0, 0) == 2) {
					rows.clear();
					imageWidth = 0;
					imageHeight = 0;
					return true;
				}
				break;
			default:
				break;
		}
		setError("unsupported ANSI CSI command", offset);
		return false;
	}

	bool applySgr(const std::vector<int> &parameters, std::size_t offset) {
		if (parameters.empty()) return applySgrCode(0, offset);
		for (int code : parameters)
			if (!applySgrCode(code, offset)) return false;
		return true;
	}

	bool applySgrCode(int code, std::size_t offset) {
		if (code == 0) {
			foreground = 7;
			background = 0;
			bold = false;
			brightForeground = false;
			inverse = false;
			return true;
		}
		if (code == 1) {
			bold = true;
			return true;
		}
		if (code == 22) {
			bold = false;
			return true;
		}
		if (code == 7) {
			inverse = true;
			return true;
		}
		if (code == 27) {
			inverse = false;
			return true;
		}
		if (code >= 30 && code <= 37) {
			foreground = code - 30;
			brightForeground = false;
			return true;
		}
		if (code == 39) {
			foreground = 7;
			brightForeground = false;
			return true;
		}
		if (code >= 40 && code <= 47) {
			background = code - 40;
			return true;
		}
		if (code == 49) {
			background = 0;
			return true;
		}
		if (code >= 90 && code <= 97) {
			foreground = code - 90;
			brightForeground = true;
			return true;
		}
		if (code >= 100 && code <= 107) {
			background = code - 100 + 8;
			return true;
		}
		setError("unsupported ANSI SGR parameter " + std::to_string(code), offset);
		return false;
	}
};

enum class DeferredVisualUiProc {
	Unknown,
	MakeMessage,
	MarqueeInfo,
	MarqueeWarning,
	MarqueeError,
	MessageBox,
	Working,
	Brain,
	DesktopSetColor,
	DesktopPutChar,
	DesktopPutString,
	DesktopBlit,
	DesktopClear,
	PutBox,
	Write,
	ClrLine,
	Gotoxy,
	PutLineNum,
	PutColNum,
	ScrollBoxUp,
	ScrollBoxDn,
	ClearScreen,
	KillBox
};

DeferredVisualUiProc classifyDeferredVisualUiProc(const std::string &name) noexcept {
	if (name == "MAKE_MESSAGE") return DeferredVisualUiProc::MakeMessage;
	if (name == "MARQUEE") return DeferredVisualUiProc::MarqueeInfo;
	if (name == "MARQUEE_WARNING") return DeferredVisualUiProc::MarqueeWarning;
	if (name == "MARQUEE_ERROR") return DeferredVisualUiProc::MarqueeError;
	if (name == "UI_MESSAGEBOX") return DeferredVisualUiProc::MessageBox;
	if (name == "WORKING") return DeferredVisualUiProc::Working;
	if (name == "BRAIN") return DeferredVisualUiProc::Brain;
	if (name == "DESKTOP_SET_COLOR") return DeferredVisualUiProc::DesktopSetColor;
	if (name == "DESKTOP_PUT_CHAR") return DeferredVisualUiProc::DesktopPutChar;
	if (name == "DESKTOP_PUT_STRING") return DeferredVisualUiProc::DesktopPutString;
	if (name == "DESKTOP_BLIT") return DeferredVisualUiProc::DesktopBlit;
	if (name == "DESKTOP_CLEAR") return DeferredVisualUiProc::DesktopClear;
	if (name == "PUT_BOX") return DeferredVisualUiProc::PutBox;
	if (name == "WRITE") return DeferredVisualUiProc::Write;
	if (name == "CLR_LINE") return DeferredVisualUiProc::ClrLine;
	if (name == "GOTOXY") return DeferredVisualUiProc::Gotoxy;
	if (name == "PUT_LINE_NUM") return DeferredVisualUiProc::PutLineNum;
	if (name == "PUT_COL_NUM") return DeferredVisualUiProc::PutColNum;
	if (name == "SCROLL_BOX_UP") return DeferredVisualUiProc::ScrollBoxUp;
	if (name == "SCROLL_BOX_DN") return DeferredVisualUiProc::ScrollBoxDn;
	if (name == "CLEAR_SCREEN") return DeferredVisualUiProc::ClearScreen;
	if (name == "KILL_BOX") return DeferredVisualUiProc::KillBox;
	return DeferredVisualUiProc::Unknown;
}

bool buildDeferredVisualUiProcedureCommand(const std::string &name, const std::vector<Value> &args, MRMacroDeferredUiCommand &command) {
	switch (classifyDeferredVisualUiProc(name)) {
		case DeferredVisualUiProc::MakeMessage:
			if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("MAKE_MESSAGE expects one string argument.");
			command = MRMacroDeferredUiCommand(mrducMakeMessage, 0, 0, 0, 0, 0, 0, 0, 0, valueAsString(args[0]));
			return true;
		case DeferredVisualUiProc::MarqueeInfo:
		case DeferredVisualUiProc::MarqueeWarning:
		case DeferredVisualUiProc::MarqueeError:
			if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error(name + " expects one string argument.");
			command = MRMacroDeferredUiCommand(name == "MARQUEE" ? mrducMarqueeInfo : (name == "MARQUEE_WARNING" ? mrducMarqueeWarning : mrducMarqueeError), 0, 0, 0, 0, 0, 0, 0, 0, valueAsString(args[0]));
			return true;
		case DeferredVisualUiProc::MessageBox:
			if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("UI_MESSAGEBOX expects one string argument.");
			command = MRMacroDeferredUiCommand(mrducMessageBox, 0, 0, 0, 0, 0, 0, 0, 0, valueAsString(args[0]));
			return true;
		case DeferredVisualUiProc::Working:
			if (!args.empty()) throw std::runtime_error("WORKING expects no arguments.");
			command = MRMacroDeferredUiCommand(mrducMarqueeWarning, 0, 0, 0, 0, 0, 0, 0, 0, kDeferredWorkingMessageText);
			return true;
		case DeferredVisualUiProc::Brain:
			if (args.size() != 1 || !isNumeric(args[0])) throw std::runtime_error("BRAIN expects one integer argument.");
			command = MRMacroDeferredUiCommand(mrducBrain, valueAsInt(args[0]) != 0 ? 1 : 0);
			return true;
		case DeferredVisualUiProc::DesktopClear:
			if (!args.empty()) throw std::runtime_error("DESKTOP_CLEAR expects no arguments.");
			command = MRMacroDeferredUiCommand(mrducDesktopClear);
			return true;
		case DeferredVisualUiProc::DesktopSetColor:
			if (args.size() != 1 || args[0].type != TYPE_INT || args[0].i < 0 || args[0].i > 0xFF) throw std::runtime_error("DESKTOP_SET_COLOR expects one attribute in the range 0..255.");
			command = MRMacroDeferredUiCommand(mrducDesktopSetColor, args[0].i);
			return true;
		case DeferredVisualUiProc::DesktopPutChar: {
			std::string character;
			if (args.size() != 3 || !isStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("DESKTOP_PUT_CHAR expects (char, int, int).");
			if (args[0].type == TYPE_CHAR) character.push_back(static_cast<char>(args[0].c));
			else
				character = args[0].s;
			if (character.size() != 1) throw std::runtime_error("DESKTOP_PUT_CHAR expects exactly one character.");
			command = MRMacroDeferredUiCommand(mrducDesktopPutChar, args[1].i, args[2].i, 0, 0, 0, 0, 0, 0, character);
			return true;
		}
		case DeferredVisualUiProc::DesktopPutString: {
			std::string text;
			if (args.size() != 3 || !isStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("DESKTOP_PUT_STRING expects (string, int, int).");
			if (args[0].type == TYPE_CHAR) text.push_back(static_cast<char>(args[0].c));
			else
				text = args[0].s;
			command = MRMacroDeferredUiCommand(mrducDesktopPutString, args[1].i, args[2].i, 0, 0, 0, 0, 0, 0, text);
			return true;
		}
		case DeferredVisualUiProc::DesktopBlit: {
			std::string source;
			std::string ioError;
			std::string parseError;
			AnsiImage image;
			if (args.size() != 4 || !isStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT) throw std::runtime_error("DESKTOP_BLIT expects (string, int, int, int).");
			const std::string path = valueAsString(args[0]);
			const int mode = args[3].i;
			if (path.empty()) throw std::runtime_error("DESKTOP_BLIT requires a non-empty ANSI file path.");
			if (mode != 0 && mode != 1) throw std::runtime_error("DESKTOP_BLIT mode must be 0 (ANSI colour) or 1 (desktop monochrome).");
			if (!readTextFile(path, source, ioError)) throw std::runtime_error("DESKTOP_BLIT: " + ioError);
			if (source.size() > 16 * 1024 * 1024) throw std::runtime_error("DESKTOP_BLIT: ANSI input exceeds 16 MiB.");
			AnsiImageParser parser(source);
			if (!parser.parse(image, parseError)) throw std::runtime_error("DESKTOP_BLIT: " + parseError + " in " + path);
			if (mode == 1) {
				unsigned char desktopAttribute = 0x90;
				static_cast<void>(configuredColorSlotOverride(kMrPaletteDesktop, desktopAttribute));
				image.applyDesktopMonochrome(desktopAttribute);
			}
			command = MRMacroDeferredUiCommand(mrducDesktopBlit, args[1].i, args[2].i, image.width, image.height, 0, 0, 0, 0, image.characters);
			command.text2 = image.attributes;
			return true;
		}
		case DeferredVisualUiProc::PutBox:
			if (args.size() != 8 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT || args[5].type != TYPE_INT || !isStringLike(args[6]) || args[7].type != TYPE_INT) throw std::runtime_error(name + " expects (int, int, int, int, int, int, string, int).");
			command = MRMacroDeferredUiCommand(mrducPutBox, valueAsInt(args[0]), valueAsInt(args[1]), valueAsInt(args[2]), valueAsInt(args[3]), valueAsInt(args[4]), valueAsInt(args[5]), valueAsInt(args[7]), 0, valueAsString(args[6]));
			return true;
		case DeferredVisualUiProc::Write:
			if (args.size() != 5 || !isStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT) throw std::runtime_error(name + " expects (string, int, int, int, int).");
			command = MRMacroDeferredUiCommand(mrducWrite, valueAsInt(args[1]), valueAsInt(args[2]), valueAsInt(args[3]), valueAsInt(args[4]), 0, 0, 0, 0, valueAsString(args[0]));
			return true;
		case DeferredVisualUiProc::ClrLine:
			if (!args.empty() && (args.size() != 3 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT)) throw std::runtime_error(name + " expects no arguments or (int, int, int).");
			command = args.empty() ? MRMacroDeferredUiCommand(mrducClrLine) : MRMacroDeferredUiCommand(mrducClrLine, valueAsInt(args[0]), valueAsInt(args[1]), valueAsInt(args[2]));
			return true;
		case DeferredVisualUiProc::Gotoxy:
			if (args.size() != 2 || args[0].type != TYPE_INT || args[1].type != TYPE_INT) throw std::runtime_error(name + " expects (int, int).");
			command = MRMacroDeferredUiCommand(mrducGotoxy, valueAsInt(args[0]), valueAsInt(args[1]));
			return true;
		case DeferredVisualUiProc::PutLineNum:
		case DeferredVisualUiProc::PutColNum:
			if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error(name + " expects one integer argument.");
			command = MRMacroDeferredUiCommand(classifyDeferredVisualUiProc(name) == DeferredVisualUiProc::PutLineNum ? mrducPutLineNum : mrducPutColNum, valueAsInt(args[0]));
			return true;
		case DeferredVisualUiProc::ScrollBoxUp:
		case DeferredVisualUiProc::ScrollBoxDn:
			if (args.size() != 5 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT) throw std::runtime_error(name + " expects (int, int, int, int, int).");
			command = MRMacroDeferredUiCommand(classifyDeferredVisualUiProc(name) == DeferredVisualUiProc::ScrollBoxUp ? mrducScrollBoxUp : mrducScrollBoxDn, valueAsInt(args[0]), valueAsInt(args[1]), valueAsInt(args[2]), valueAsInt(args[3]), valueAsInt(args[4]), 0, 0, 0);
			return true;
		case DeferredVisualUiProc::ClearScreen:
			if (!args.empty() && (args.size() != 1 || args[0].type != TYPE_INT)) throw std::runtime_error(name + " expects no arguments or one integer argument.");
			command = MRMacroDeferredUiCommand(mrducClearScreen, args.empty() ? 0x07 : valueAsInt(args[0]));
			return true;
		case DeferredVisualUiProc::KillBox:
			if (!args.empty()) throw std::runtime_error(name + " expects no arguments.");
			command = MRMacroDeferredUiCommand(mrducKillBox);
			return true;
		case DeferredVisualUiProc::Unknown:
			return false;
	}
	return false;
}

enum class DeferredMenuUiProc {
	Unknown,
	RegisterMenuItem,
	RemoveMenuItem
};

DeferredMenuUiProc classifyDeferredMenuUiProc(const std::string &name) noexcept {
	if (name == "REGISTER_MENU_ITEM") return DeferredMenuUiProc::RegisterMenuItem;
	if (name == "REMOVE_MENU_ITEM") return DeferredMenuUiProc::RemoveMenuItem;
	return DeferredMenuUiProc::Unknown;
}

bool buildDeferredMenuUiProcedureCommand(const std::string &name, const std::vector<Value> &args, MRMacroDeferredUiCommand &command) {
	std::string macroSpec;

	if (!currentExecutingMacroSpec(macroSpec)) throw std::runtime_error(name + " requires an active macro context.");

	switch (classifyDeferredMenuUiProc(name)) {
		case DeferredMenuUiProc::RegisterMenuItem:
			if ((args.size() != 2 && args.size() != 3) || !isStringLike(args[0]) || !isStringLike(args[1]) || (args.size() == 3 && !isStringLike(args[2]))) throw std::runtime_error("REGISTER_MENU_ITEM expects (string, string[, string]).");
			command.type = mrducRegisterMenuItem;
			command.text = valueAsString(args[0]);
			command.text2 = valueAsString(args[1]);
			command.text3 = args.size() == 3 ? valueAsString(args[2]) : macroSpec;
			command.text4 = macroSpec;
			return true;
		case DeferredMenuUiProc::RemoveMenuItem:
			if (args.size() != 2 || !isStringLike(args[0]) || !isStringLike(args[1])) throw std::runtime_error("REMOVE_MENU_ITEM expects (string, string).");
			command.type = mrducRemoveMenuItem;
			command.text = valueAsString(args[0]);
			command.text2 = valueAsString(args[1]);
			command.text3 = macroSpec;
			return true;
		case DeferredMenuUiProc::Unknown:
			return false;
	}
	return false;
}

bool applyDeferredMenuUiProcedureCommand(const MRMacroDeferredUiCommand &command) {
	std::string errorText;

	switch (command.type) {
		case mrducRegisterMenuItem:
			if (!mrvmUiRegisterMenuItem(command.text, command.text2, command.text3, command.text4, &errorText)) throw std::runtime_error("REGISTER_MENU_ITEM failed: " + (errorText.empty() ? std::string("unable to register menu item.") : errorText));
			return true;
		case mrducRemoveMenuItem:
			if (!mrvmUiRemoveMenuItem(command.text, command.text2, command.text3, &errorText)) throw std::runtime_error("REMOVE_MENU_ITEM failed: " + (errorText.empty() ? std::string("unable to remove menu item.") : errorText));
			return true;
		default:
			return false;
	}
}

} // namespace

bool queueDeferredUiProcedure(const std::string &name, const std::vector<Value> &args, int &errorCode) {
	BackgroundEditSession *session = g_backgroundEditSession;

	errorCode = 0;
	if (session == nullptr) return false;

	if (name == "MARQUEE" || name == "MARQUEE_WARNING" || name == "MARQUEE_ERROR") {
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error(name + " expects one string argument.");
		if (name == "MARQUEE") session->deferredUiCommands.emplace_back(mrducMarqueeInfo, 0, 0, 0, 0, 0, 0, 0, 0, mrvmValueAsString(args[0]));
		else if (name == "MARQUEE_WARNING")
			session->deferredUiCommands.emplace_back(mrducMarqueeWarning, 0, 0, 0, 0, 0, 0, 0, 0, mrvmValueAsString(args[0]));
		else
			session->deferredUiCommands.emplace_back(mrducMarqueeError, 0, 0, 0, 0, 0, 0, 0, 0, mrvmValueAsString(args[0]));
		return true;
	}
	if (name == "MAKE_MESSAGE") {
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("MAKE_MESSAGE expects one string argument.");
		session->deferredUiCommands.emplace_back(mrducMakeMessage, 0, 0, 0, 0, 0, 0, 0, 0, mrvmValueAsString(args[0]));
		return true;
	}
	if (name == "UI_MESSAGEBOX") {
		if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("UI_MESSAGEBOX expects one string argument.");
		session->deferredUiCommands.emplace_back(mrducMessageBox, 0, 0, 0, 0, 0, 0, 0, 0, mrvmValueAsString(args[0]));
		return true;
	}
	if (name == "WORKING") {
		if (!args.empty()) throw std::runtime_error("WORKING expects no arguments.");
		session->deferredUiCommands.emplace_back(mrducMarqueeWarning, 0, 0, 0, 0, 0, 0, 0, 0, kDeferredWorkingMessageText);
		return true;
	}
	if (name == "BRAIN") {
		if (args.size() != 1 || !mrvmIsNumeric(args[0])) throw std::runtime_error("BRAIN expects one integer argument.");
		session->deferredUiCommands.emplace_back(mrducBrain, mrvmValueAsInt(args[0]) != 0 ? 1 : 0);
		return true;
	}
	if (name == "DESKTOP_BLIT") throw std::runtime_error("DESKTOP_BLIT is not allowed during staged background execution.");
	if (name == "DESKTOP_SET_COLOR" || name == "DESKTOP_PUT_CHAR" || name == "DESKTOP_PUT_STRING" || name == "DESKTOP_CLEAR") {
		MRMacroDeferredUiCommand command;
		if (!buildDeferredVisualUiProcedureCommand(name, args, command)) return false;
		session->deferredUiCommands.push_back(command);
		return true;
	}
	if (name == "REGISTER_MENU_ITEM" || name == "REMOVE_MENU_ITEM") throw std::runtime_error(name + " is not allowed during staged background execution.");

	if (name == "PUT_BOX") {
		if (args.size() != 8 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT || args[5].type != TYPE_INT || !mrvmIsStringLike(args[6]) || args[7].type != TYPE_INT) throw std::runtime_error(name + " expects (int, int, int, int, int, int, string, int).");
		session->deferredUiCommands.emplace_back(mrducPutBox, mrvmValueAsInt(args[0]), mrvmValueAsInt(args[1]), mrvmValueAsInt(args[2]), mrvmValueAsInt(args[3]), mrvmValueAsInt(args[4]), mrvmValueAsInt(args[5]), mrvmValueAsInt(args[7]), 0, mrvmValueAsString(args[6]));
		return true;
	}
	if (name == "WRITE") {
		if (args.size() != 5 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT) throw std::runtime_error(name + " expects (string, int, int, int, int).");
		session->deferredUiCommands.emplace_back(mrducWrite, mrvmValueAsInt(args[1]), mrvmValueAsInt(args[2]), mrvmValueAsInt(args[3]), mrvmValueAsInt(args[4]), 0, 0, 0, 0, mrvmValueAsString(args[0]));
		return true;
	}
	if (name == "CLR_LINE") {
		if (args.empty()) {
			session->deferredUiCommands.emplace_back(mrducClrLine);
			return true;
		}
		if (args.size() != 3 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("CLR_LINE expects no arguments or (int, int, int).");
		session->deferredUiCommands.emplace_back(mrducClrLine, mrvmValueAsInt(args[0]), mrvmValueAsInt(args[1]), mrvmValueAsInt(args[2]));
		return true;
	}
	if (name == "GOTOXY") {
		int x;
		int y;
		if (args.size() != 2 || args[0].type != TYPE_INT || args[1].type != TYPE_INT) throw std::runtime_error("GOTOXY expects (int, int).");
		x = mrvmValueAsInt(args[0]);
		y = mrvmValueAsInt(args[1]);
		if (session->screenWidth > 0) x = std::max(1, std::min(x, session->screenWidth));
		if (session->screenHeight > 0) y = std::max(1, std::min(y, session->screenHeight));
		session->screenCursorX = x;
		session->screenCursorY = y;
		session->deferredUiCommands.emplace_back(mrducGotoxy, x, y);
		return true;
	}
	if (name == "PUT_LINE_NUM") {
		if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("PUT_LINE_NUM expects one integer argument.");
		session->deferredUiCommands.emplace_back(mrducPutLineNum, mrvmValueAsInt(args[0]));
		return true;
	}
	if (name == "PUT_COL_NUM") {
		if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("PUT_COL_NUM expects one integer argument.");
		session->deferredUiCommands.emplace_back(mrducPutColNum, mrvmValueAsInt(args[0]));
		return true;
	}
	if (name == "SCROLL_BOX_UP" || name == "SCROLL_BOX_DN") {
		if (args.size() != 5 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT) throw std::runtime_error(name + " expects (int, int, int, int, int).");
		session->deferredUiCommands.emplace_back(name == "SCROLL_BOX_UP" ? mrducScrollBoxUp : mrducScrollBoxDn, mrvmValueAsInt(args[0]), mrvmValueAsInt(args[1]), mrvmValueAsInt(args[2]), mrvmValueAsInt(args[3]), mrvmValueAsInt(args[4]), 0, 0, 0);
		return true;
	}
	if (name == "CLEAR_SCREEN") {
		if (!args.empty() && (args.size() != 1 || args[0].type != TYPE_INT)) throw std::runtime_error("CLEAR_SCREEN expects no arguments or one integer argument.");
		session->screenCursorX = 1;
		session->screenCursorY = 1;
		session->deferredUiCommands.emplace_back(mrducClearScreen, args.empty() ? 0x07 : mrvmValueAsInt(args[0]));
		return true;
	}
	if (name == "KILL_BOX") {
		if (!args.empty()) throw std::runtime_error("KILL_BOX expects no arguments.");
		session->deferredUiCommands.emplace_back(mrducKillBox);
		return true;
	}

	if (name == "CREATE_WINDOW") {
		session->deferredUiCommands.emplace_back(mrducCreateWindow);
		if (session->windowCount < std::numeric_limits<int>::max()) ++session->windowCount;
		session->currentWindow = std::max(1, session->windowCount);
		session->linkStatus = 0;
		return true;
	}
	if (name == "DELETE_WINDOW") {
		session->deferredUiCommands.emplace_back(mrducDeleteWindow);
		if (session->windowCount > 0) --session->windowCount;
		if (session->windowCount <= 0) {
			session->windowCount = 0;
			session->currentWindow = 0;
			session->linkStatus = 0;
		} else if (session->currentWindow > session->windowCount)
			session->currentWindow = session->windowCount;
		return true;
	}
	if (name == "MODIFY_WINDOW") {
		session->deferredUiCommands.emplace_back(mrducModifyWindow);
		return true;
	}
	if (name == "LINK_WINDOW") {
		session->deferredUiCommands.emplace_back(mrducLinkWindow);
		session->linkStatus = 1;
		return true;
	}
	if (name == "UNLINK_WINDOW") {
		session->deferredUiCommands.emplace_back(mrducUnlinkWindow);
		session->linkStatus = 0;
		return true;
	}
	if (name == "ZOOM") {
		session->deferredUiCommands.emplace_back(mrducZoom);
		return true;
	}
	if (name == "REDRAW") {
		session->deferredUiCommands.emplace_back(mrducRedraw);
		return true;
	}
	if (name == "NEW_SCREEN") {
		session->deferredUiCommands.emplace_back(mrducNewScreen);
		return true;
	}
	if (name == "SWITCH_WINDOW") {
		int index;
		int count;
		if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("SWITCH_WINDOW expects one integer argument.");
		index = mrvmValueAsInt(args[0]);
		count = session->windowCount;
		if (count > 0) {
			if (index <= 0) index = 1;
			if (index > count) index = ((index - 1) % count) + 1;
			session->currentWindow = index;
		}
		session->deferredUiCommands.emplace_back(mrducSwitchWindow, index);
		return true;
	}
	if (name == "SIZE_WINDOW") {
		int x1;
		int y1;
		int x2;
		int y2;
		if (args.size() != 4 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT) throw std::runtime_error("SIZE_WINDOW expects four integer arguments.");
		x1 = mrvmValueAsInt(args[0]);
		y1 = mrvmValueAsInt(args[1]);
		x2 = mrvmValueAsInt(args[2]);
		y2 = mrvmValueAsInt(args[3]);
		session->windowGeometryValid = true;
		session->windowX1 = x1;
		session->windowY1 = y1;
		session->windowX2 = x2;
		session->windowY2 = y2;
		session->deferredUiCommands.emplace_back(mrducSizeWindow, x1, y1, x2, y2);
		return true;
	}
	return false;
}

bool dispatchDeferredVisualUiProcedure(const std::string &name, const std::vector<Value> &args, int &errorCode) {
	MRMacroDeferredUiCommand command;

	errorCode = 0;
	if (queueDeferredUiProcedure(name, args, errorCode)) return true;
	if (!buildDeferredVisualUiProcedureCommand(name, args, command)) return false;
	mrvmUiRenderFacadeRenderDeferredCommand(command);
	return true;
}

bool dispatchDeferredMenuUiProcedure(const std::string &name, const std::vector<Value> &args, int &errorCode) {
	MRMacroDeferredUiCommand command;

	errorCode = 0;
	if (queueDeferredUiProcedure(name, args, errorCode)) return true;
	if (!buildDeferredMenuUiProcedureCommand(name, args, command)) return false;
	return applyDeferredMenuUiProcedureCommand(command);
}
