#ifndef MRGDBTERMINALPANE_HPP
#define MRGDBTERMINALPANE_HPP

#include "MRBentoBox.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class MRGdbTerminalPane final : public MRPaneEditWindow {
  public:
	MRGdbTerminalPane(const TRect &bounds, const char *title, int number);
	void appendTerminalOutput(const std::string &text);
	void resetTerminal();

  protected:
	void changeBounds(const TRect &bounds) override;
	void draw() override;
	void handleEvent(TEvent &event) override;
	[[nodiscard]] bool usesNativeEditorChrome() const noexcept override;
	[[nodiscard]] bool ownsPaneWheelEvents() const noexcept override;
	[[nodiscard]] bool projectsPaneContentLocally() const noexcept override;

  private:
	struct Cell {
		Cell() noexcept : character(' '), attribute(0x07) {}
		Cell(char aCharacter, std::uint8_t aAttribute) noexcept : character(aCharacter), attribute(aAttribute) {}
		char character;
		std::uint8_t attribute;
	};

	enum class EscapeState : unsigned char { Text, Escape, Csi, Osc, OscEscape };

	void resizeTerminal(int columns, int rows);
	void processCharacter(unsigned char character);
	void processEscape(unsigned char character);
	void processCsi(unsigned char finalCharacter);
	void putCharacter(char character);
	void lineFeed();
	void reverseLineFeed();
	void scrollUp(int count);
	void scrollDown(int count);
	void eraseDisplay(int mode);
	void eraseLine(int mode);
	void applySgr(const std::vector<int> &parameters);
	void useAlternateScreen(bool enabled);
	void sendKeyEvent(TEvent &event);
	[[nodiscard]] std::vector<int> csiParameters(bool &privateMode) const;
	[[nodiscard]] std::uint8_t currentAttribute() const noexcept;

	std::vector<Cell> screen;
	std::vector<Cell> primaryScreen;
	std::deque<std::vector<Cell>> scrollback;
	int columns;
	int rows;
	int cursorColumn;
	int cursorRow;
	int savedColumn;
	int savedRow;
	int primaryColumn;
	int primaryRow;
	int scrollTop;
	int scrollBottom;
	int foreground;
	int background;
	bool bold;
	bool reverseVideo;
	bool wrapPending;
	bool cursorVisible;
	bool alternateScreen;
	EscapeState escapeState;
	std::string escapeText;
	std::size_t scrollOffset;
};

#endif
