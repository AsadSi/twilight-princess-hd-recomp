# Gyro aiming

The port has no GamePad, so it turns a **virtual GamePad**, whose motion `VPADRead` reports, with one of
these sources:

| Source | What it uses |
|---|---|
| Off (default) | the GamePad lies still: no gyro aiming, as before |
| Controller gyro | the gyro and accelerometer of a DualSense, DualShock 4, Switch Pro, Joy-Con, Steam Deck / Steam Controller and other controllers SDL3 reads (the macOS app uses GameController.framework: DualSense, DualShock 4, Switch Pro, Joy-Con) |
| Cemuhook (DSU) | a Cemuhook motion server over UDP: DS4Windows, BetterJoy, SteamDeckGyroDSU, phone apps. Default 127.0.0.1, port 26760, slot 1 |
| Mouse | mouse movement turns the GamePad while the game aims; made for Steam Input's "gyro to mouse" output, works with a plain mouse too |

This came with the Wind Waker HD fork. For Twilight Princess HD (2026-10-09):

- Not checked yet: when and how TP HD reads the GamePad's motion.
- The mouse source does nothing yet. It waits for the game to aim, and the aiming state came from a Wind
  Waker hook (`mods/camera.cpp`), which went with the fork. The macOS app also no longer passes mouse
  movement to it (that was `mods/mouse.mm`); the SDL host still does (`platform/mouse_sdl.cpp`).
- With a Pro Controller selected, nothing lets the game read the virtual GamePad's motion: Wind Waker's hook
  for that went with the fork (`motion::drives_gamepad()` is what a TP HD hook would check).

Settings overlay (F1) → **Controls** → **Gyro…**:

- **Source** (above). `WWHD_GYRO=off|controller|cemuhook|mouse` overrides it at start.
- **Turn left/right by** (controller and Cemuhook sources), the usual gyro aiming conventions (as in
  JoyShockMapper and Steam Input):
  - **Player space** (default): turning the controller left or right about the real vertical,
    however you hold it (flat, tilted towards you, upright). Rolling it does nothing.
  - **Yaw**: turning it about its own vertical axis, as if it lay flat.
  - **Roll**: tilting it to the side like a steering wheel.

  Tilting the controller's top up or down always turns the GamePad up or down.
- **Sensitivity left/right** and **up/down** (0.05x to 5x, default 0.5x), each with **Invert**.
  1.0x turns the virtual GamePad by the angle the controller turned.
- **Mouse: degrees per point**: how far one point of mouse movement turns the controller (then the
  sensitivity applies).
- **Cemuhook**: server, port and controller slot (1 to 4).
- **Recalibrate**: a controller input and/or a key, and a **Recalibrate now** button. If the view
  drifts while the controller rests, recalibrate and put it down for a second: the gyro's offset is
  learnt anew. (The binding is saved under its old name, `gyro.recenterPad` / `gyro.recenterKey`.)

Everything is saved with the other settings (`gyro.*` in `settings.ini`, or `display.plist` on
the macOS app).

Why off by default: the gyro moves the aim with every hand movement, which surprises players who never
asked for it, and a controller lying on a desk works fine either way. The Gyro window says when a
connected controller has a gyro.

## Why the virtual GamePad only turns

Wind Waker HD's first-person camera used only the change of the GamePad's direction matrix (`VPADStatus`
+0x6C, +0x78, +0x84: its X, Y and Z axes) from one frame to the next, in the GamePad's own frame. So the port
does not mirror the controller's pose: each source works out how far the player turned (left/right, up/down)
and the virtual GamePad turns by exactly that about its own axes. The view then follows the same way however
the controller is held, and nothing jumps when a controller connects or disconnects or the source changes.
Whether TP HD reads the matrix the same way is part of the check above.

## How the port does it

`runtime/src/motion/`:

- `fusion.cpp`: the axis mapping of the SDL and Cemuhook sources into one frame (the signs follow the
  GamePad conventions Cemu recorded from real hardware; Cemu is MPL-2.0, as this port), gyro bias
  estimation while the controller rests (no slow drift), a noise floor, the gravity direction (the
  accelerometer, carried along by the gyro), and the **aim**: how far the player turned left/right
  and up/down under the chosen axis mode (player space: the turn about gravity with JoyShockMapper's
  "relax factor", so a tilted hold still turns easily). The aim, after sensitivity and invert, turns the
  virtual GamePad about its own axes (yaw about its Y axis, pitch about its X axis).
- `dsu.cpp`: a small Cemuhook client: one background thread, 100 ms receive timeout, a data request
  per second (servers drop quiet clients), CRC-checked packets, never blocks the game.
- `motion.cpp`: the sources, settings, the recalibrate binding and what `VPADRead` gets. With several
  controllers, the one that moves drives the GamePad (another takes over when it turns while the
  active one rests). Sensor timestamps that do not advance (a driver without them, a clock that
  stalls) fall back to the arrival times, so the motion never stops because of them. The mouse source
  only takes movement while the game aims (`set_aiming`), and the pointer is captured then.
- Hosts: `platform/input_sdl.cpp` (SDL3 sensors, on only for the controller source; a watchdog turns a
  controller's sensors off and on again when they stay silent for 2 s while the game window has the
  focus), `gfx/input.mm` (GameController.framework, macOS 11+; sensors that the system switched off
  are switched on again), `platform/mouse_sdl.cpp` (mouse, SDL host).

## Troubleshooting

- **The gyro stops working after a while.** The log (`[gyro]` lines) says what happened: "no motion
  from controller N for 1 s" and "motion from ... again" (the samples stopped and came back; the
  window losing the focus does this too, SDL pauses controller events then), "turning its sensors off
  and on again" (the watchdog), "motion from controller N" (another controller took over).
  The Gyro window shows the same.
- **More detail:** `WWHD_GYRO_LOG=1` logs, twice a second and for every source, the raw gyro and
  accelerometer samples, the sample rate, the bias, the gravity direction and the aim, plus what the
  virtual GamePad gets; on the macOS app also the raw GameController values.

Debug: `WWHD_TEST_GYRO=from-to:yaw:pitch,...` turns the virtual GamePad by yaw / pitch degrees per
second during game-time seconds (any source, also off; no sensitivity; game time, so it does not
depend on how fast the machine runs).

## Testing with real hardware (wanted)

The axis modes are tested with simulated controllers (SDL and Cemuhook conventions, flat, tilted and
upright holds), not yet with physical controllers or with TP HD. Once the game's reading is checked:

1. **DualSense / DualShock 4 / Switch Pro** with **Controller gyro**: in **Player space**, turning the
   controller right turns the aim right and tilting its top up looks up, however you hold it; rolling does
   nothing. **Yaw** and **Roll** as described above. With the controller on a desk the aim must stay still
   after a second (bias calibration).
2. Same on the **macOS app** (GameController.framework, a different axis mapping:
   `WWHD_GYRO_LOG=1` logs the raw values).
3. **Steam Deck** (SDL) and **Joy-Con** pairs.
4. **Cemuhook**: DS4Windows or BetterJoy with the server on, source Cemuhook; the Gyro window shows
   "receiving motion". Stop the server: the game must keep running smoothly. Android needs network
   permission for this, which the app did not ask for.
5. Long sessions: the gyro keeps working (if not, the `[gyro]` log lines say why).
