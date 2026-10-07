# Language sources (experimental)

The port is built from the USA game (title 00050000-10143500, version 0), which has English,
French and Spanish. A **language source** adds the languages of a European or Japanese copy of
the game that you also own: German, Italian, British English, European French and Spanish, or
Japanese. The game is still built from and runs the USA code; only the text, the fonts and the
localised 2D layouts are taken from the second game.

> **Status: untested with real European or Japanese files.** Everything here was written and
> tested without such a dump, from the USA game code and synthetic test files. Expect problems;
> please report what you see (see [the test plan](#test-plan-for-a-real-dump)).

## Use

You need the USA game (installed as usual) **and** your own dump of the European
(00050000-10143600) or Japanese (00050000-10143400) game: a `.wux`/`.wud` image with its disc
key, a Cemu `.wua` archive or an extracted folder.

```
setup.py --language-source "Wind Waker HD (EUR).wux" --language-disc-key eur.key --common-key common.key
setup.py --language-source wwhd-eur.wua
setup.py --language-source /path/to/extracted/eur-game
setup.py --remove-language-source EU
```

(The interactive setup also offers it when the game is installed; the graphical installer's
protocol has `add_language_source`, see `tools/installer/README.md`.)

Setup checks the title id (the USA game, updates, DLC and other games are refused) and takes only
`content/Common/Pack/permanent_2d_*.pack` (about 12 MB per language) and `meta/meta.xml` into
`data/game-lang/EU` or `data/game-lang/JP`, with a `language-source.json` listing the packs and
their SHA-256. Nothing else of the second game is extracted or used, and nothing goes into the
repository or the release.

In the game, open the settings (F1) > Language: the source's languages appear under "From your
European game" / "From your Japanese game". The choice applies on the next start. Without the
overlay: `WWHD_LANGUAGE=3 WWHD_LANGUAGE_REGION=eu` (German from the European source),
`WWHD_LANGUAGE=0 WWHD_LANGUAGE_REGION=jp` (Japanese). `WWHD_LANG_DIR` points the game at another
folder of language sources (default: `game-lang` next to the game folder, else in the current
folder).

## How it works

### How the game picks its language (USA code)

* At boot (`SystemTask::prepare`, 0x0203FD18) the system setting object (singleton 0x101F4BAC)
  reads the console language with `UCReadSysConfig("cafe.language")` (0x025F9448). It keeps
  `+0x10` = region in Wii U region bits (1 Japan, 2 USA, 4 Europe) and `+0x14` = console language
  (0 ja, 1 en, 2 fr, 3 de, 4 it, 5 es, ...). **The USA build always stores region 2 and keeps only
  languages 1, 2 and 5** (anything else becomes English); it also mirrors the language into the
  save's options (byte +4 at save + 0x12F0: 0 English, 2 French, 3 Spanish — the GameCube PAL
  order 0 en, 1 de, 2 fr, 3 es, 4 it; the USA code only copies it to 0x101EA6D2, never reads it).
* Every other user of the setting has the European and Japanese branches compiled in:
  * the 2D pack, 0x02612BE0: region 1 → `Pack/permanent_2d_JpJapanese.pack`; region 2 →
    `UsFrench`/`UsSpanish`/`UsEnglish`; region 4 → `Eu<English|French|German|Italian|Spanish>` for
    languages 1..5 (table 0x100E2408). All nine names are in the USA executable (0x1048DD4C).
  * the pack holds everything language-dependent of the 2D side: all MSBT message archives
    (`message*_msbt.szs`, one `*_msbt.szs` per layout, `unitString`, `rubyString`), the MSBT project
    `CKing_msbp.szs`, the six fonts (`CKingMain`, `CKingMainL`, `CKingMsg`, `CKingPic`, `CKingRuby`,
    `CKingZelda`) and all in-game layouts (221 files). In the three USA packs only the 58 MSBT
    archives differ; layouts and fonts are byte-identical.
  * the message manager (0x025F4A60, 0x025F416C) loads the project and its message sets from the
    loaded pack by name (fallback path `Cafe/US/Message/Us<Language>/<name>.szs`, unused);
    messages are looked up by label.
  * layout panes with a language suffix (0x0270517C region index 0 JP / 1 US / 2 EU, 0x027051B0
    language index, suffix table `_JpJa _UsEn _UsFr _UsPo _UsSp _EuEn _EuDu _EuGe _EuFr _EuPo
    _EuRu _EuSp _EuIt`): even the USA layouts contain `_JpJa` panes (message windows, menus, the
    treasure map; `N_TitleLogo_00_JpJa` in `Common/Layout/Title_00.szs`).
  * the software keyboard for the name (0x026195C4): region 1 → Japanese keyboard, region 4 →
    per-language European layouts (QWERTZ for German...).
  * the error viewer's region and language (0x02033118, 0x0203315C), and the time format of
    messages (0x025FE38C: English and German put the minutes first).
* Region-independent: the audio (`Cafe/US/AudioRes/JAudioRes`, fixed path in the USA code), the
  3D packs (`szs_permanent*`, `permanent_3d`, `first_szs_permanent`), stages, objects and
  `Common/Layout` (which already has the Japanese title logo pane).

### What the port does

* **Setup** takes the packs of the second game (`wwhd-extract --only`).
* **Runtime** (`runtime/src/game_languages.{h,cpp}`) finds the packs in
  `data/game-lang/*/content/Common/Pack` (names without case, `SARC` header, packs the USA game
  has itself are skipped) and offers them in the Language tab.
* **Console language** (`hle/coreinit_misc.cpp`): with a source language chosen, the game gets
  that language code (3 for German, ...).
* **Region** (`language_region.cpp`, hook on 0x025F9448 in `tools/recomp/hooks_language.txt`):
  after the USA reader, the source's region (4 or 1) and language are written to the setting object
  and the options byte as the European game would keep them. The game then asks for
  `Pack/permanent_2d_EuGerman.pack`.
* **File system** (`hle/fs.cpp`): a read of the active source's pack name (any case, any prefix) is
  served from the language source; a content mod's file of the same name still comes first;
  writers are never redirected. Nothing else is redirected.
* The name prompt reads its allowed characters from the active pack's `CKingMsg` font
  (`overlay/game_font.cpp`).

## Known risks

* **The European/Japanese builds may differ in code**, not only in the region constant. The USA
  executable contains the European and Japanese branches listed above (and the build path
  `ProductUS` suggests one code base per region), but a region-specific difference elsewhere (for
  example in the message tag handling or the text layout) would not be seen until the real files
  are played.
* **Message labels**: the USA code asks for messages by label. A label that exists only in the USA
  text (or a European-only label the USA code never asks for) would show empty text or fail a
  lookup.
* **Memory**: the pack index and its files are loaded into heaps sized by the USA build. Longer
  German or French text, or a larger Japanese font, could exceed a heap.
* **Text that does not fit**: the European layouts come from the European pack, so boxes sized
  for German should come with it; code-side widths (USA) could still clip.
* **Japanese**: the Japanese pack uses ruby (furigana, `CKingRuby`, `rubyString`) and the `_JpJa`
  panes; the USA code has both, but only the Japanese build may enable everything (for example a
  ruby option). The JP keyboard and name characters depend on the font (`CKingMsg`).
* **Save files** are shared by all languages; the name is stored as text, so a Japanese name shows
  in other languages only where the fonts have those characters (the USA `CKingMsg` has kana and
  1,235 kanji, the menu font `CKingMain` kana and 75 kanji).
* **Save states** are tied to the language they were made in (the loaded text is in the state).
* Region-specific files outside the packs (the EU disc's `Common/Layout`, `Jpeg`,
  `ProgramTexture`, `Cafe/EU/AudioRes`) are not taken; if they differ, the USA ones are shown.

## Test plan for a real dump

0. **Before any European dump** (checks the region path with the USA files only): make a folder
   `X/EU/content/Common/Pack` with a **symlink** `permanent_2d_EuFrench.pack` → your USA
   `permanent_2d_UsFrench.pack`, start with `WWHD_LANG_DIR=X WWHD_LANGUAGE=2
   WWHD_LANGUAGE_REGION=eu`. The log must say "language source active ... region 4"; the game
   must ask for `permanent_2d_EuFrench.pack` and show French; the name keyboard is AZERTY.
1. `wwhd-extract list` on the European/Japanese image: compare the file list and sizes with the
   USA game. Every file outside `content/Common/Pack/permanent_2d_*` that differs is a candidate
   for the language source (note especially `Common/Layout`, `Jpeg`, `ProgramTexture`,
   `Cafe/EU`).
2. `setup.py --language-source ...` with the image, the `.wua` and a folder; check
   `data/game-lang/EU/language-source.json` (five packs), and that nothing else was extracted.
3. Per language (German, Italian, British English, French, Spanish; Japanese): boot, title screen
   (logo, "Press Start"), file select, new game, name entry (keyboard layout, umlauts, ß, accents,
   kana/kanji; the name in dialogs and on the file select), the opening (Grandma, Aryll, the
   telescope), a full dialogue scene, shops (numbers, rupees, plurals), the auction (timers), the
   Pictograph and the Pictobox text, the sea chart and treasure maps, the item menu (long item
   names), the quest status, the options menu, the game over and save screens, the boat race and
   other timers (`putTimeF`), the Tingle Bottle and Miiverse texts, the credits.
4. Long German strings: item descriptions, the Hero's Charm and Tingle Bottle texts, the options
   (Kamerasteuerung...), the save messages: nothing clipped or overflowing the box; text speed and
   line breaks as on the European console.
5. Japanese: kana and kanji in dialogues, ruby (furigana) above kanji, the title logo
   (`N_TitleLogo_00_JpJa`), `_JpJa` panes in the message windows, the treasure map and the item
   menu, the Japanese name keyboard.
6. Fonts: no missing glyph boxes (European accents in the menu font `CKingMain`, the GamePad
   screen).
7. Saves: make a save in German, load it in English and Japanese and back; the name and the
   progress must survive; a USA save loads in every language.
8. Memory: play several scene changes and the menus in German and Japanese with the heap
   statistics; no allocation failures in the log.
9. Compare against the European console or Cemu running the European game for the same scenes
   (screenshots), to separate port problems from the original's.
