#ifndef MRTHEME_HPP
#define MRTHEME_HPP

#include <string>
#include <unordered_map>
#include <cstdint>

enum MRTokenType
{
	tkNormal = 1,
	tkKeyword = 2,
	tkString = 3,
	tkComment = 4,
	tkError = 5
};

struct MRTheme
{
	char paletteData[17];
};

class MRThemeRegistry
{
private:
	std::unordered_map<std::string, MRTheme> themes;
	MRThemeRegistry()
	{
		// Authentisches Multi-Edit Farbschema für MS-DOS
		// Indizes 0-7: Rahmen (Classic Cyan/Blue Style)
		// Indizes 8-15: Inhalt (Normaler Text, Keywords, Strings, etc)
		MRTheme multiEditTheme = {{0x13, 0x13, 0x1B, 0x13, 0x13, 0x13, 0x13, 0x13, // Rahmen (Cyan auf Blau)
											0x1F, 0x1E, 0x1A, 0x1B, 0x1C, 0x30, 0x1F, 0x1F, // Inhalt (Weiß/Gelb auf Blau, 0x30 für Selektion)
											'\0'}};
		themes["Default"] = multiEditTheme;
	}

public:
	static MRThemeRegistry &instance()
	{
		static MRThemeRegistry instance;
		return instance;
	}

	void registerTheme(const std::string &classIdentifier, const MRTheme &theme)
	{
		themes[classIdentifier] = theme;
	}

	MRTheme getTheme(const std::string &classIdentifier)
	{
		auto it = themes.find(classIdentifier);
		if (it != themes.end())
		{
			return it->second;
		}
		else
		{
			return themes["Default"];
		}
	}
};

#endif