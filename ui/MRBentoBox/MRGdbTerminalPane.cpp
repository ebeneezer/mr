#define Uses_TKeys
#include <tvision/tv.h>

#include "MRGdbTerminalPane.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

namespace {
constexpr std::size_t kMaximumScrollbackRows = 1024;

int xtermColorToAnsi(int color) noexcept {
	if (color < 0) return 7;
	if (color < 16) return color;
	if (color >= 232) return color >= 244 ? 7 : 8;
	const int cube = color - 16;
	const int red = cube / 36;
	const int green = (cube / 6) % 6;
	const int blue = cube % 6;
	const bool bright = std::max(red, std::max(green, blue)) >= 4;
	int result = 0;
	if (blue >= 3) result |= 1;
	if (green >= 3) result |= 2;
	if (red >= 3) result |= 4;
	if (bright) result |= 8;
	return result;
}
} // namespace

MRGdbTerminalPane::MRGdbTerminalPane(const TRect &bounds, const char *title, int number)
	: TWindowInit(&MRPaneEditWindow::initFrame), MRPaneEditWindow(bounds, title, number), screen(), primaryScreen(), scrollback(), columns(1), rows(1), cursorColumn(0), cursorRow(0), savedColumn(0), savedRow(0), primaryColumn(0), primaryRow(0), scrollTop(0), scrollBottom(0), foreground(7), background(0), bold(false), reverseVideo(false), wrapPending(false), cursorVisible(true), alternateScreen(false), escapeState(EscapeState::Text), escapeText(), scrollOffset(0) {
	if (getEditor() != nullptr) getEditor()->hide();
	if (horizontalEditorScrollBar() != nullptr) horizontalEditorScrollBar()->hide();
	if (verticalEditorScrollBar() != nullptr) verticalEditorScrollBar()->hide();
	if (editorIndicator() != nullptr) editorIndicator()->hide();
	if (frame != nullptr) frame->hide();
	resizeTerminal(std::max<int>(1, size.x), std::max<int>(1, size.y));
	appendTerminalOutput("No active inferior.\r\n");
}

void MRGdbTerminalPane::appendTerminalOutput(const std::string &text) {
	scrollOffset = 0;
	for (const unsigned char character : text) processCharacter(character);
	drawView();
}

void MRGdbTerminalPane::resetTerminal() {
	screen.assign(static_cast<std::size_t>(columns * rows), Cell());
	primaryScreen.clear();
	scrollback.clear();
	cursorColumn = 0;
	cursorRow = 0;
	savedColumn = 0;
	savedRow = 0;
	primaryColumn = 0;
	primaryRow = 0;
	scrollTop = 0;
	scrollBottom = rows - 1;
	foreground = 7;
	background = 0;
	bold = false;
	reverseVideo = false;
	wrapPending = false;
	cursorVisible = true;
	alternateScreen = false;
	escapeState = EscapeState::Text;
	escapeText.clear();
	scrollOffset = 0;
	drawView();
}

void MRGdbTerminalPane::changeBounds(const TRect &bounds) {
	MRPaneEditWindow::changeBounds(bounds);
	resizeTerminal(std::max<int>(1, size.x), std::max<int>(1, size.y));
	if (MRBentoBox *bento = dynamic_cast<MRBentoBox *>(owner)) bento->resizeGdbTerminal(columns, rows);
}

void MRGdbTerminalPane::draw() {
	const std::size_t historyRows = alternateScreen ? 0 : scrollback.size();
	const std::size_t start = historyRows >= scrollOffset ? historyRows - scrollOffset : 0;
	for (int row = 0; row < rows; ++row) {
		TDrawBuffer buffer;
		const std::size_t sourceRow = start + static_cast<std::size_t>(row);
		for (int column = 0; column < columns; ++column) {
			Cell cell;
			if (!alternateScreen && sourceRow < historyRows) {
				const std::vector<Cell> &history = scrollback[sourceRow];
				if (column < static_cast<int>(history.size())) cell = history[static_cast<std::size_t>(column)];
			} else {
				const std::size_t screenRow = sourceRow >= historyRows ? sourceRow - historyRows : 0;
				if (screenRow < static_cast<std::size_t>(rows)) cell = screen[screenRow * static_cast<std::size_t>(columns) + static_cast<std::size_t>(column)];
			}
			std::uint8_t attribute = cell.attribute;
			if (scrollOffset == 0 && cursorVisible && row == cursorRow && column == cursorColumn) attribute = static_cast<std::uint8_t>(((attribute & 0x0F) << 4) | ((attribute & 0xF0) >> 4));
			buffer.moveChar(static_cast<ushort>(column), cell.character, TAttrPair(TColorAttr(attribute)), 1);
		}
		writeLine(0, static_cast<short>(row), static_cast<short>(columns), 1, buffer);
	}
}

void MRGdbTerminalPane::handleEvent(TEvent &event) {
	if (event.what == evMouseWheel) {
		if (!alternateScreen) {
			const int delta = event.mouse.wheel > 0 ? 3 : -3;
			if (delta > 0) scrollOffset = std::min<std::size_t>(scrollback.size(), scrollOffset + static_cast<std::size_t>(delta));
			else scrollOffset = scrollOffset > static_cast<std::size_t>(-delta) ? scrollOffset - static_cast<std::size_t>(-delta) : 0;
			drawView();
		}
		clearEvent(event);
		return;
	}
	if (event.what == evKeyDown) {
		sendKeyEvent(event);
		return;
	}
	MRPaneEditWindow::handleEvent(event);
}

bool MRGdbTerminalPane::usesNativeEditorChrome() const noexcept { return false; }
bool MRGdbTerminalPane::ownsPaneWheelEvents() const noexcept { return true; }
bool MRGdbTerminalPane::projectsPaneContentLocally() const noexcept { return true; }

void MRGdbTerminalPane::resizeTerminal(int newColumns, int newRows) {
	newColumns = std::max(1, newColumns);
	newRows = std::max(1, newRows);
	if (newColumns == columns && newRows == rows && !screen.empty()) return;
	std::vector<Cell> resized(static_cast<std::size_t>(newColumns * newRows));
	const int copyColumns = std::min(columns, newColumns);
	const int copyRows = screen.empty() ? 0 : std::min(rows, newRows);
	for (int row = 0; row < copyRows; ++row)
		for (int column = 0; column < copyColumns; ++column)
			resized[static_cast<std::size_t>(row * newColumns + column)] = screen[static_cast<std::size_t>(row * columns + column)];
	screen.swap(resized);
	columns = newColumns;
	rows = newRows;
	cursorColumn = std::min(cursorColumn, columns - 1);
	cursorRow = std::min(cursorRow, rows - 1);
	wrapPending = false;
	scrollTop = 0;
	scrollBottom = rows - 1;
}

void MRGdbTerminalPane::processCharacter(unsigned char character) {
	switch (escapeState) {
		case EscapeState::Text:
			if (character == 0x1B) escapeState = EscapeState::Escape;
			else if (character == '\r') { cursorColumn = 0; wrapPending = false; }
			else if (character == '\n' || character == '\v' || character == '\f') { wrapPending = false; lineFeed(); }
			else if (character == '\b') { cursorColumn = std::max(0, cursorColumn - 1); wrapPending = false; }
			else if (character == '\t') { cursorColumn = std::min(columns - 1, (cursorColumn + 8) & ~7); wrapPending = false; }
			else if (character >= 32 && character != 127) putCharacter(static_cast<char>(character));
			break;
		case EscapeState::Escape:
			processEscape(character);
			break;
		case EscapeState::Csi:
			if (character >= 0x40 && character <= 0x7E) {
				processCsi(character);
				escapeState = EscapeState::Text;
				escapeText.clear();
			} else if (escapeText.size() < 64) escapeText += static_cast<char>(character);
			break;
		case EscapeState::Osc:
			if (character == 0x07) escapeState = EscapeState::Text;
			else if (character == 0x1B) escapeState = EscapeState::OscEscape;
			break;
		case EscapeState::OscEscape:
			escapeState = character == '\\' ? EscapeState::Text : EscapeState::Osc;
			break;
	}
}

void MRGdbTerminalPane::processEscape(unsigned char character) {
	escapeState = EscapeState::Text;
	switch (character) {
		case '[': escapeState = EscapeState::Csi; escapeText.clear(); break;
		case ']': escapeState = EscapeState::Osc; break;
		case '7': savedColumn = cursorColumn; savedRow = cursorRow; break;
		case '8': cursorColumn = savedColumn; cursorRow = savedRow; wrapPending = false; break;
		case 'D': wrapPending = false; lineFeed(); break;
		case 'M': wrapPending = false; reverseLineFeed(); break;
		case 'E': cursorColumn = 0; wrapPending = false; lineFeed(); break;
		case 'c': resetTerminal(); break;
		default: break;
	}
}

std::vector<int> MRGdbTerminalPane::csiParameters(bool &privateMode) const {
	std::vector<int> parameters;
	std::size_t start = 0;
	privateMode = !escapeText.empty() && escapeText[0] == '?';
	if (privateMode) start = 1;
	while (start <= escapeText.size()) {
		const std::size_t separator = escapeText.find(';', start);
		const std::string value = escapeText.substr(start, separator == std::string::npos ? std::string::npos : separator - start);
		parameters.push_back(value.empty() ? 0 : std::atoi(value.c_str()));
		if (separator == std::string::npos) break;
		start = separator + 1;
	}
	if (parameters.empty()) parameters.push_back(0);
	return parameters;
}

void MRGdbTerminalPane::processCsi(unsigned char finalCharacter) {
	bool privateMode = false;
	const std::vector<int> parameters = csiParameters(privateMode);
	const int first = parameters.empty() || parameters[0] == 0 ? 1 : parameters[0];
	if (finalCharacter != 'm') wrapPending = false;
	switch (finalCharacter) {
		case 'A': cursorRow = std::max(scrollTop, cursorRow - first); break;
		case 'B': cursorRow = std::min(scrollBottom, cursorRow + first); break;
		case 'C': cursorColumn = std::min(columns - 1, cursorColumn + first); break;
		case 'D': cursorColumn = std::max(0, cursorColumn - first); break;
		case 'E': cursorRow = std::min(scrollBottom, cursorRow + first); cursorColumn = 0; break;
		case 'F': cursorRow = std::max(scrollTop, cursorRow - first); cursorColumn = 0; break;
		case 'G': cursorColumn = std::min(columns - 1, first - 1); break;
		case 'd': cursorRow = std::min(rows - 1, first - 1); break;
		case 'H':
		case 'f':
			cursorRow = std::min(rows - 1, std::max(0, first - 1));
			cursorColumn = parameters.size() > 1 ? std::min(columns - 1, std::max(0, parameters[1] - 1)) : 0;
			break;
		case 'J': eraseDisplay(parameters[0]); break;
		case 'K': eraseLine(parameters[0]); break;
		case 'm': applySgr(parameters); break;
		case 'r':
			scrollTop = std::min(rows - 1, std::max(0, first - 1));
			scrollBottom = parameters.size() > 1 && parameters[1] > 0 ? std::min(rows - 1, parameters[1] - 1) : rows - 1;
			if (scrollBottom < scrollTop) { scrollTop = 0; scrollBottom = rows - 1; }
			cursorColumn = 0; cursorRow = scrollTop;
			break;
		case 's': savedColumn = cursorColumn; savedRow = cursorRow; break;
		case 'u': cursorColumn = savedColumn; cursorRow = savedRow; break;
		case 'h':
		case 'l':
			if (privateMode) {
				const bool enabled = finalCharacter == 'h';
				for (const int mode : parameters) {
					if (mode == 25) cursorVisible = enabled;
					if (mode == 1047 || mode == 1049) useAlternateScreen(enabled);
				}
			}
			break;
		default: break;
	}
}

void MRGdbTerminalPane::putCharacter(char character) {
	if (wrapPending) { cursorColumn = 0; lineFeed(); wrapPending = false; }
	screen[static_cast<std::size_t>(cursorRow * columns + cursorColumn)] = Cell(character, currentAttribute());
	if (cursorColumn + 1 >= columns) wrapPending = true;
	else ++cursorColumn;
}

void MRGdbTerminalPane::lineFeed() {
	if (cursorRow < scrollBottom) { ++cursorRow; return; }
	scrollUp(1);
}

void MRGdbTerminalPane::reverseLineFeed() {
	if (cursorRow > scrollTop) { --cursorRow; return; }
	scrollDown(1);
}

void MRGdbTerminalPane::scrollUp(int count) {
	for (int iteration = 0; iteration < count; ++iteration) {
		if (scrollTop == 0 && scrollBottom == rows - 1 && !alternateScreen) {
			scrollback.emplace_back(screen.begin(), screen.begin() + columns);
			while (scrollback.size() > kMaximumScrollbackRows) scrollback.pop_front();
		}
		for (int row = scrollTop; row < scrollBottom; ++row)
			std::copy(screen.begin() + static_cast<std::ptrdiff_t>((row + 1) * columns), screen.begin() + static_cast<std::ptrdiff_t>((row + 2) * columns), screen.begin() + static_cast<std::ptrdiff_t>(row * columns));
		std::fill(screen.begin() + static_cast<std::ptrdiff_t>(scrollBottom * columns), screen.begin() + static_cast<std::ptrdiff_t>((scrollBottom + 1) * columns), Cell(' ', currentAttribute()));
	}
}

void MRGdbTerminalPane::scrollDown(int count) {
	for (int iteration = 0; iteration < count; ++iteration) {
		for (int row = scrollBottom; row > scrollTop; --row)
			std::copy(screen.begin() + static_cast<std::ptrdiff_t>((row - 1) * columns), screen.begin() + static_cast<std::ptrdiff_t>(row * columns), screen.begin() + static_cast<std::ptrdiff_t>(row * columns));
		std::fill(screen.begin() + static_cast<std::ptrdiff_t>(scrollTop * columns), screen.begin() + static_cast<std::ptrdiff_t>((scrollTop + 1) * columns), Cell(' ', currentAttribute()));
	}
}

void MRGdbTerminalPane::eraseDisplay(int mode) {
	const Cell blank(' ', currentAttribute());
	const std::size_t cursor = static_cast<std::size_t>(cursorRow * columns + cursorColumn);
	if (mode == 2 || mode == 3) std::fill(screen.begin(), screen.end(), blank);
	else if (mode == 1) std::fill(screen.begin(), screen.begin() + static_cast<std::ptrdiff_t>(std::min(screen.size(), cursor + 1)), blank);
	else std::fill(screen.begin() + static_cast<std::ptrdiff_t>(std::min(screen.size(), cursor)), screen.end(), blank);
	if (mode == 3) scrollback.clear();
}

void MRGdbTerminalPane::eraseLine(int mode) {
	const Cell blank(' ', currentAttribute());
	const std::size_t start = static_cast<std::size_t>(cursorRow * columns);
	if (mode == 2) std::fill(screen.begin() + static_cast<std::ptrdiff_t>(start), screen.begin() + static_cast<std::ptrdiff_t>(start + columns), blank);
	else if (mode == 1) std::fill(screen.begin() + static_cast<std::ptrdiff_t>(start), screen.begin() + static_cast<std::ptrdiff_t>(start + cursorColumn + 1), blank);
	else std::fill(screen.begin() + static_cast<std::ptrdiff_t>(start + cursorColumn), screen.begin() + static_cast<std::ptrdiff_t>(start + columns), blank);
}

void MRGdbTerminalPane::applySgr(const std::vector<int> &parameters) {
	for (std::size_t index = 0; index < parameters.size(); ++index) {
		const int parameter = parameters[index];
		if (parameter == 0) { foreground = 7; background = 0; bold = false; reverseVideo = false; }
		else if (parameter == 1) bold = true;
		else if (parameter == 22) bold = false;
		else if (parameter == 7) reverseVideo = true;
		else if (parameter == 27) reverseVideo = false;
		else if (parameter >= 30 && parameter <= 37) foreground = parameter - 30;
		else if (parameter >= 40 && parameter <= 47) background = parameter - 40;
		else if (parameter >= 90 && parameter <= 97) foreground = parameter - 90 + 8;
		else if (parameter >= 100 && parameter <= 107) background = parameter - 100 + 8;
		else if (parameter == 39) foreground = 7;
		else if (parameter == 49) background = 0;
		else if ((parameter == 38 || parameter == 48) && index + 2 < parameters.size() && parameters[index + 1] == 5) {
			const int color = xtermColorToAnsi(parameters[index + 2]);
			if (parameter == 38) foreground = color; else background = color;
			index += 2;
		} else if ((parameter == 38 || parameter == 48) && index + 4 < parameters.size() && parameters[index + 1] == 2) {
			const int red = parameters[index + 2];
			const int green = parameters[index + 3];
			const int blue = parameters[index + 4];
			int color = (blue >= 128 ? 1 : 0) | (green >= 128 ? 2 : 0) | (red >= 128 ? 4 : 0);
			if (std::max(red, std::max(green, blue)) >= 192) color |= 8;
			if (parameter == 38) foreground = color; else background = color;
			index += 4;
		}
	}
}

void MRGdbTerminalPane::useAlternateScreen(bool enabled) {
	if (enabled == alternateScreen) return;
	if (enabled) {
		primaryScreen = screen;
		primaryColumn = cursorColumn;
		primaryRow = cursorRow;
		screen.assign(static_cast<std::size_t>(columns * rows), Cell(' ', currentAttribute()));
		cursorColumn = 0;
		cursorRow = 0;
		wrapPending = false;
	} else {
		if (primaryScreen.size() == screen.size()) screen.swap(primaryScreen);
		primaryScreen.clear();
		cursorColumn = std::min(columns - 1, primaryColumn);
		cursorRow = std::min(rows - 1, primaryRow);
		wrapPending = false;
	}
	alternateScreen = enabled;
	scrollOffset = 0;
}

void MRGdbTerminalPane::sendKeyEvent(TEvent &event) {
	MRBentoBox *bento = dynamic_cast<MRBentoBox *>(owner);
	if (bento == nullptr) return;
	std::string input;
	const ushort key = ctrlToArrow(event.keyDown.keyCode);
	if ((event.keyDown.controlKeyState & kbPaste) != 0) {
		std::array<char, 512> buffer{};
		size_t length = 0;
		while (textEvent(event, TSpan<char>(buffer.data(), buffer.size()), length)) input.append(buffer.data(), length);
	} else if (key == kbEnter) input = "\r";
	else if (key == kbBack) input = "\x7F";
	else if (key == kbTab || key == kbCtrlI) input = "\t";
	else if (key == kbEsc) input = "\x1B";
	else if (key == kbUp) input = "\x1B[A";
	else if (key == kbDown) input = "\x1B[B";
	else if (key == kbRight) input = "\x1B[C";
	else if (key == kbLeft) input = "\x1B[D";
	else if (key == kbHome) input = "\x1B[H";
	else if (key == kbEnd) input = "\x1B[F";
	else if (key == kbIns) input = "\x1B[2~";
	else if (key == kbDel) input = "\x1B[3~";
	else if (key == kbPgUp) input = "\x1B[5~";
	else if (key == kbPgDn) input = "\x1B[6~";
	else if (event.keyDown.charScan.charCode != 0) input.assign(1, event.keyDown.charScan.charCode);
	if (!input.empty()) static_cast<void>(bento->sendGdbTerminalInput(input));
	clearEvent(event);
}

std::uint8_t MRGdbTerminalPane::currentAttribute() const noexcept {
	int selectedForeground = foreground;
	int selectedBackground = background;
	if (bold && selectedForeground < 8) selectedForeground += 8;
	if (reverseVideo) std::swap(selectedForeground, selectedBackground);
	return static_cast<std::uint8_t>(((selectedBackground & 0x0F) << 4) | (selectedForeground & 0x0F));
}
