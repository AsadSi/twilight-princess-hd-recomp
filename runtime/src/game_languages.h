// The console languages TP HD shows its text in. The game (Zelda.rpx) picks /res/Msg<suffix>/ by its
// language (0x1014B6AF: 0 ja, 1 en, 2 fr, 3 de, 4 it, 5 es) and the console region (0x1014B6B0, 4
// Europe), 0x02A6B968. On the USA console the runtime reports (MCP_GetSysProdSettings) that is English
// (Msgus), French (Msgusfr) and Spanish (Msgussp); any other console language gets English. The disc
// also carries the European and Japanese text (Msguk, Msgfr, Msgde, Msgit, Msgsp, Msgjp), unused.
#pragma once
#include <vector>

namespace game_lang {

// Wii U console language codes (cafe.language): 0 ja, 1 en, 2 fr, 3 de, 4 it, 5 es, 6 zh, 7 ko,
// 8 nl, 9 pt, 10 ru, 11 zh-TW
constexpr int kLanguages = 12;
const char* name(int language);  // "English", ...; "?" out of range

const std::vector<int>& available();  // English, French, Spanish
bool is_available(int language);
int usable(int language);  // the language itself when available, else English

// the console language this start runs with, once the game has read it (-1 before)
int started();
void set_started(int language);

}  // namespace game_lang
