// The languages TP HD shows its text in. The game (Zelda.rpx) picks /res/Msg<suffix>/ by its language
// (0x1014B6AF: 0 ja, 1 en, 2 fr, 3 de, 4 it, 5 es) and the console's region (0x1014B6B0), 0x02A6B968, and every
// variant is on the disc: Msgus, Msgusfr, Msgussp (USA), Msguk, Msgfr, Msgsp, Msgde, Msgit (Europe), Msgjp. The
// runtime reports the console language (UCReadSysConfig cafe.language) and the region (MCP_GetSysProdSettings)
// of the chosen variant. A language the game lacks (Chinese, Korean, Dutch, Portuguese, Russian) gets English.
#pragma once
#include <string>
#include <vector>

namespace game_lang {

// Wii U console language codes (cafe.language): 0 ja, 1 en, 2 fr, 3 de, 4 it, 5 es, 6 zh, 7 ko,
// 8 nl, 9 pt, 10 ru, 11 zh-TW
constexpr int kLanguages = 12;

// Wii U region bits (MCPSysProdSettings product area and game region)
enum Region : int { kJapan = 1, kUsa = 2, kEurope = 4 };
const char* region_code(int region);            // "jp", "us", "eu"
int region_from_code(const std::string& code);  // "eu", "EU", "Europe" -> kEurope ...; 0 if unknown

struct Variant {
    int language;
    int region;
    const char* name;  // "English (North America)", ...
};
const std::vector<Variant>& variants();
// The variant for a console language and a region code ("" or unknown: the language's own region)
const Variant& choose(int language, const std::string& region);

// The variant this start runs with: WWHD_LANGUAGE (and WWHD_LANGUAGE_REGION), else the settings overlay's
// saved choice (hostui "language", "language_region"), else English (North America). Decided once.
const Variant& current();

}  // namespace game_lang
