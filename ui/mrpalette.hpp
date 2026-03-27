#ifndef MRPALETTE_HPP
#define MRPALETTE_HPP

struct MRPalette
{
	unsigned char desktop;
	char menuBar[7];
	char statusLine[7];
	char menuPopup[7];
};

extern MRPalette currentPalette;
void loadDefaultMultiEditPalette();

#endif