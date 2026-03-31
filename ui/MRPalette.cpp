#include "MRPalette.hpp"

MRPalette currentPalette;

void loadDefaultMultiEditPalette() {
	currentPalette.desktop = 0x90; // Heller Blau-Desktop mit schwarzen Punkten

	// Menü (Blau auf Grau, Selektion Magenta)
	currentPalette.menuBar[0] = 0x71;
	currentPalette.menuBar[1] = 0x78;
	currentPalette.menuBar[2] = 0x70;
	currentPalette.menuBar[3] = 0x5F; // Selektion: Weiß auf Magenta
	currentPalette.menuBar[4] = 0x58;
	currentPalette.menuBar[5] = 0x5F;

	// Statuszeile (Schwarz/Weiß auf Cyan)
	currentPalette.statusLine[0] = 0x30;
	currentPalette.statusLine[1] = 0x38;
	currentPalette.statusLine[2] = 0x3F;
	currentPalette.statusLine[3] = 0x30;

	// Der Editor (Weiß auf Blau)
	// Wir nutzen hier direkt VGA 0x1F
}
