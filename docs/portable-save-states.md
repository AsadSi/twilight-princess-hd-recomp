# Portable save states

Save states come in two kinds that share the five slots:

| | Portable (default) | Full (debugging) |
|---|---|---|
| File | `slotN.wwstate` | `slotN.bin` |
| Size | a few KB (refused above 64 KB) | about 270–300 MB |
| Holds | the save data of the current Quest Log, Link's place, a header | all guest memory (game code, decompressed assets), threads, HLE state |
| Shareable | yes, attach it to bug reports | **never**: it contains game code and data |
| Loads into | any session of the same game, any build | only the same build, exactly where it was saved |
| Exact | no (see below) | yes |

Both live in the states folder (its path is shown in the Saves tab; releases keep it in `data/user`, builds from source
use `~/Library/Application Support/wwhd/states/` on macOS and the configuration folder elsewhere; `WWHD_STATE_DIR` overrides it). Loading a slot loads whichever kind it holds (the newer file if it
holds both). Crash Recovery's automatic states are always full states.

## Choosing the kind

- **Save** (Saves tab of the settings overlay, Shift+F1–F5, the macOS Save States menu) makes a
  portable state.
- *Full save states (large, contain game data, don't share) – for debugging* in the Saves tab (or
  the Save States menu on macOS) switches Save to full states; the choice is kept in
  `states/full_save_states.cfg`.
- `WWHD_FULL_SAVE_STATES=1` / `=0` decides for one start. The scripted-test variables
  `WWHD_STATE_SAVE_AT`, `WWHD_STATE_LOAD_AT`, `WWHD_TEST_SAVE` and `WWHD_TEST_LOAD` keep using full
  states (unless `WWHD_FULL_SAVE_STATES=0`).

## Bug reports

The Saves tab's **Copy save for bug report** copies the paths of the newest portable state and of
`cking.sav`. The issue template asks for both. A developer loads a received state by copying it
into the states folder as `slotN.wwstate` and loading slot N, or with
`WWHD_PORTABLE_LOAD=<file>` (applied as soon as a Quest Log is being played). `tools/savegame/wwstate.py info <file>`
shows what a state holds (place, hearts, items, songs, ...) and `wwstate.py to-sav <file> -o <dir>`
turns it into a `cking.sav`.

## What a portable state holds

A UTF-8 text file of `key = value` lines (`runtime/src/portable_state.h`):

- header: `format` (1), `title_id` and `title_version` (from `meta/meta.xml`), `game_hash` (a hash
  of `cking.rpx`, to tell executables apart; not its contents), `runtime` (version and commit),
  `created`, `file_slot` (Quest Log 0–2), `player_name`;
- place: `stage`, `start_point`, `start_room`, `layer` (how the stage was entered), `room` (Link's
  room), `link_pos`, `link_angle_y` (shape angle), `link_proc` and `on_ship`, `time_of_day` and
  `date` (day counter; day of week = date % 7);
- `savedata`: the Quest Log's block of `cking.sav` (0xA94 bytes: 0x768 bytes of save data, zeros, the
  game's byte sum and complement sum), made by the game's own save functions: `dSv_info_c::putSave`
  for the current stage and `dComIfGs_setGameStartStage` as the in-game save does before writing
  (both undone afterwards, so making a state changes nothing in the game), then
  `dSv_info_c::memory_to_card` (025BA9FC). Inventory, flags, progress, dungeon memory, time of day;
- `hd_player`, `hd_status`, `hd_event`, `hd_map`: the HD per-file sections of `cking.sav` (16, 4, 20
  and 220 bytes), the stored ones with the live data copied over by the SaveMgr's own copy functions;
- `checksum`: CRC-32 of everything before it.

Weather is not recorded: it follows from the progress, the place and the time of day.

**Guard**: the writer and the reader accept only these fields, the binary ones at exactly these
sizes, text values up to 128 characters and files up to 64 KB; a file that breaks any of this is
not written, and not read. A state can therefore never carry a memory dump.
`runtime/tools/portable_state_test.cpp` tests the format (round trip, version mismatch, both
checksums, the size guard, unknown or repeated fields).

## Loading

At the frame boundary on the game's main thread, once a Quest Log is being played (the title
screen and file select wait):

1. `dSv_info_c::card_to_memory` (025BA7B0) puts the save data into the game;
   `dSv_info_c::getSave` of the current stage makes the stage memory the loaded one (the stage
   change puts it back), the dungeon bits (`dSv_danBit_c::init(-1)`) and temporary flags start
   fresh, the HD sections are copied in, and the SaveMgr's refresh (02721880) sets the item buttons
   and equipment from the loaded data;
2. Link's room, position and angle go into `dSv_restart_c` (as when the game restarts a room after a
   fall), and the next stage is the recorded one with spawn point -1, the recorded room and layer:
   `dStage_playerInit` creates Link at that position.

The log then says `[savestate] portable load: arrived in <stage> room <n> at x y z (distance d from
the saved position)`.

**Not restored** (it is not a snapshot): enemies, items lying around, moving platforms and other
actor state start as the stage starts them; a running cutscene, dialogue or minigame is not resumed;
Link starts standing (a state made on the boat at sea starts swimming, without the boat next to
him); the camera starts behind Link; the file is loaded into the current Quest Log of the session
(saving in game afterwards writes it there; `file_slot` is informational).

## Scenario test

`runtime/tools/portable_state_scenario.py <wwhd> <game> <save> <workdir>` (headless): from a copy of
a save, warps into Link's house, walks, saves a portable state; cold boot, changes the rupees, loads
the state, saves again once Link has arrived, and compares stage, room, position, angle, the save
data field by field (`tools/savegame/wwsave.py`) and the HD sections; then checks that full save
states still save and load.
