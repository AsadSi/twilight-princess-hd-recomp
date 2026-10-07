// Language sources (game_languages.h, docs/language-packs.md): tells the USA game code the region and
// language of the European or Japanese pack the player chose.
//
// 0x025F9448 reads the console language into the system setting object (r3, the singleton at
// 0x101F4BAC): +0x10 region (the USA build always stores 2, or 1 when the setting can't be read),
// +0x14 language (only 1, 2 and 5 are kept, others become 1), and mirrors the language into the
// save's options (save + 0x12F0, byte +4: 0 English, 2 French, 3 Spanish). The region and language
// decide the 2D pack (0x02612BE0: region 4 -> Eu<Language> for languages 1..5, region 1 ->
// JpJapanese), the language suffix of layout panes (0x0270517C / 0x027051B0: _EuGe, _JpJa, ...), the
// software keyboard (0x026195C4: Japanese kana keyboard for region 1, QWERTZ/AZERTY... for region 4),
// the error viewer's region and language, and the time format of messages. All of these have their
// European and Japanese branches in the USA code; only this reader is limited to the USA.
//
// UNTESTED with real European or Japanese game files.
#include "game_languages.h"
#include "runtime.h"

extern "C" {
void f_025F9448_orig(Cpu* c);  // SysSetting::update
}

extern "C" void hook_025F9448(Cpu* c) {
    const uint32_t self = c->r[3];
    f_025F9448_orig(c);
    const game_lang::Start s = game_lang::current();
    if (!s.pack || !self) return;
    st32(self + 0x10, (uint32_t)s.region);
    st32(self + 0x14, (uint32_t)s.language);
    // the save's options, as the European game would keep them (only copied to 0x101EA6D2 by the
    // name scene in the USA code, never read there)
    const uint32_t save = ld32(0x101F84DC);
    if (save) st8(save + 0x12F0 + 4, (uint8_t)game_lang::options_language(s.language));
    static bool logged = false;
    if (!logged) {
        logged = true;
        LOG("[config] language source active: %s (%s), %s; the game is told region %d, language %d",
            game_lang::name(s.language), game_lang::region_name(s.region), s.pack->file.c_str(), s.region, s.language);
    }
}
