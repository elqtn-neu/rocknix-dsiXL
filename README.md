# DSi Menu for ROCKNIXDS (RG DS Plus)

A Nintendo DS and Game Boy Advance menu for the Anbernic RG DS Plus running ROCKNIX with ROCKNIXDS, made to look
and feel like the Nintendo DSi's own menu. Both screens are drawn at the DS's 256x192 and shown at exactly 4x on
the Plus's 1024x768 panels: no HD redraw, no filtering. Each pixel is drawn the way a DSi's LCD shows it (red, green
and blue stripes, a fine gap between rows and columns), so text and edges read as pixels rather than blur
(Options > Screen: DSi LCD or Plain pixels). Games can have the same look: Emulator settings > Screen > DSi LCD
(a GPU shader for ROCKNIXDS's DraStic, `device/shaders/dsi-lcd.frag`).

<p align="center">
  <img src="docs/img/boot.png" width="190" alt="Boot: the DS logo on the top screen, the health and safety screen on the bottom">
  <img src="docs/img/menu.png" width="190" alt="The menu: status bar and the game's logo on top; the cartridges, NDS/GBA tabs and scroll bar below">
  <img src="docs/img/launch-up.png" width="190" alt="Launch: the cartridge has risen into the top screen above the slot">
  <img src="docs/img/launch-slam.png" width="190" alt="Launch: the cartridge slammed into the slot at the bottom of the top screen">
</p>
<p align="center"><sub>Boot, the menu, and a game starting (games without art, as the menu draws them; with art the
cartridges carry their box art and the top screen the game's logo).</sub></p>

## What it does

- **Start-up, as the DSi XL's.** Both screens come up out of black together: the logo on the top screen, the health
  and safety screen on the bottom, the start-up chime. "Touch the Touch Screen to continue." fades in and breathes.
  Only a touch continues, as on the DSi (buttons too after 12 seconds, so a touchscreen that doesn't answer can't
  lock you out). The bottom screen goes white, then the top, and the menu slides in. **Hold A** through it to go
  straight on into the game you played last.
- **Top screen.**
  - The DSi's status bar: the speaker, the date, the time with its blinking colon, the battery.
  - The DSi's grey stripes with a faint drifting diamond pattern.
  - The selected game's **title logo**, floating, with a soft glow, a shadow on the floor, a glint and sparkles.
    With no logo, its title set as one.
  - **X** switches to the **photo view**: the game in a white photo frame, a little crooked, as the DSi showed
    your photos. A game with a logo gets a screenshot with the logo floating over it; a game without one (most DS
    games) gets its title screen alone, which carries the logo. X again goes back. The menu remembers which.
- **Bottom screen.**
  - White with the DSi's stripes and a slow pattern.
  - **3D cartridges** in a row: a DS game card or a GBA Game Pak, carrying the game's art.
  - The selected cartridge pops out in full colour in the DSi's selection frame. The frame travels with the
    cartridges (it lunges for the next one and they settle together), and a light passes over it now and then. The
    others float, smaller and greyer.
  - **L NDS / GBA R** tabs. The system's name as a watermark ("Nintendo DS", like "Nintendo DSi Menu").
  - The DSi's scroll bar along the bottom.
- **Starting a game.** The cartridge squashes, jumps off the bottom screen and comes up into the top one with a
  full turn. It hangs, then **slams down into the card slot** that has risen at the bottom of the top screen:
  the cartridge goes into the slot's slit with a thud, shake, dust, the power lamp. Then both screens fade to
  black and stay black until the game draws: no flash, no terminal, no desktop in between.
- **Back from a game.** Out of black, the cartridge pops out of the slot, drops through to the bottom screen and
  lands back in its place.
- **Brightness, the DSi's five steps.** Tap the sun in the bottom left corner. Both screens together; a DSi bar shows
  on the top screen. ROCKNIX's own Anbernic button + volume keeps working as it always does (installing with
  `--dsi-brightness` makes it step through the DSi's five levels instead).
- **Saving the battery.** After 30 seconds untouched the menu draws at 30 frames a second; after 2 minutes the
  screens dim, after 5 they go off (`dim_after`, `screen_off_after` in `config.ini`). The first touch or button
  only wakes them. Closing the lid sleeps (ROCKNIX's own suspend); opening it, the menu comes back out of black.
- **The battery.** A notice on the top screen at 15% and 5% (with a chime) and when the charger goes in. In a
  game, at 3% the game is saved and closed for you (the exit hotkey's way: DraStic's resume state, RetroArch's
  save), and the menu says so.
- **Your saves are backed up** after every game: a copy of the save in `roms/dsixl/backups/<nds|gba>/<game>/`
  when it changed, the last 5 kept.
- **When a game can't start**, the menu says why in a DSi-style window instead of quietly coming back.
- **Order and finding.** Options > Order: by name, recently played, most played. In name order, up/down jump to
  the next letter, a bubble with the letter over the scroll bar (dragging the scroll bar shows it too).
  Options > Hide this game takes one out of the rows (Options > Show hidden brings them back, faded).
- **New games** wear a red NEW tag until you first play them. Games copied onto the card while the menu is up
  show up by themselves.
- **Play time.** The photo view (X) says how long you've played a game and when you last did.
- **Resume points.** A green play mark on cartridges with a resume point. Continue shows the picture of where you
  left off when there is one (RetroArch's resume state picture).
- **Album** (Options): your screenshots (`roms/screenshots`) in a DSi Camera-style grid, big on the top screen, with
  a slideshow (A). A game's own screenshots also take turns in its photo frame.
- **Volume:** the volume keys show a DSi volume bar on the top screen.
- **The time of day** colours the top screen's light: a warm morning, a golden late afternoon, a violet dusk, a
  blue night.
- **Tactile navigation.** A press lands in about a tenth of a second, and the selection locks into its frame like a
  detent: a click, a squeeze of the frame, a short buzz of the vibration motor. Holding left/right glides along
  the row, faster and faster, and settles on the next cartridge when you let go. A flick coasts on, ticking past
  each cartridge, and slows to a stop. The ends of the list stretch like rubber and snap back. Holding A sinks the
  cartridge in; letting go starts it. L and R push their tabs in.
- **Alive.** Cartridges wobble like jelly when tapped, tilt as the row moves, and lean toward the selected one.
  After 15 seconds untouched, the selected one turns all the way round or a
  wave runs along the row. Taps leave a burst of stars in your colour. The bottom screen and the selection frame
  take on the selected game's own colour (from its box art), and the pattern slides slower than the row
  (parallax). The cursor tick rises in pitch along the list. On your birthday (`birthday = MM-DD` in
  `config.ini`): a cake by your name and confetti. In December: snow on the top screen.
- **Tactile everywhere.** Springs with a little overshoot, eases, a bump at the ends of the list, sounds on every
  move. Touch works throughout: drag or flick the row, tap a cartridge, tap the selected one to start it, tap the
  tabs, the scroll bar and its arrows.

## Controls

| | |
|---|---|
| Left / Right | the previous / next game (hold: a glide that speeds up) |
| Up / Down | the previous / next letter (in name order; else five games back / on) |
| L / R | the tabs: NDS, ★ Favorites (the middle one, where the menu opens), GBA |
| SELECT (or the gear, bottom right) | the emulator settings (for all DS games, all GBA games) |
| The sun, bottom left (or ROCKNIX's Anbernic button + volume) | brightness |
| A (or tap the selected cartridge) | start it (on release: the cartridge sinks in while held) |
| X | the top screen: the logo, or the photo view |
| Y | a favourite, or not (a gold star on the cartridge) |
| START | Options (below) |
| B | close a window |

**Options** (START): Emulator settings, Order, Hide this game, Show hidden, Album, Get art from the internet, Wi-Fi,
Time zone, Color, Clock, Sound effects, Music, Vibration, Controls in games, Look for new games, Exit to
EmulationStation, Power off; *Update to x.y.z* at the top when a newer release is on the card. Its last line shows
the battery (and the time it has left) and the games card's free space.
- **Wi-Fi:** the networks around (signal, padlock, the one you're on); a DSi-style touch keyboard for the
  password (keys on the bottom screen, what you type on the top one; the d-pad and A work too, B deletes, START is
  OK). It joins through ROCKNIX's own Wi-Fi tool, so EmulationStation sees the same network; the clock is set from
  the internet when it joins.
- **Time zone:** the part of the world, then the city.
- **Get art from the internet:** the art script with a progress window; each cartridge jumps when its art
  arrives. B lets it carry on in the background (a small mark turns in the top screen's corner).
- **Controls in games:** how to leave a game (L + SELECT + START), save and load. Also shown once before your
  first DS game and your first GBA game.

**Favorites** are a tab of their own, the middle one, and the menu opens on it:
- Its own look in the same DSi style: warm cream paper with little stars, a white lacquered shelf with a gold edge
  that the cartridges stand on, a gold selection frame, "★ Favorites" pressed in.
- DS cards and Game Paks side by side, in the order you added them.
- First, on a little shelf of its own: **Continue**, the game you played last, with a "Continue" balloon over it,
  "Continue playing: ..." in the title line and CONTINUE in its frame. Coming back from a game lands it there.
- Y on a favourite there takes it off the shelf (its neighbours close up); in the NDS and GBA tabs Y puts the star
  on or off, and favourites stay in their alphabetical places, marked in gold on the scroll bar.
- Kept in `/storage/.config/dsixl/favorites.txt` (a line per game, `NDS<tab>file name`, in the order added).

**Emulator settings** (SELECT, or tap the gear): the emulators' options for every game, a page for DS and one for
GBA (L/R), in a window on the bottom screen.
- Up/Down picks an option, Left/Right (or the arrows) changes it, the top screen says what the option does and
  what the choice does.
- The recommended choice of each option (the best balance of looks, speed and battery on this handheld) has a
  little gold star; **Recommended** puts the page back to them. **Done** (or B) keeps them.
- DS (ROCKNIXDS's DraStic): Screen (Crisp pixels, Pixel grid, Crisp with DS colors, Smooth, Pixel-perfect),
  Performance (Battery saver, Balanced, Max performance), 3D quality (2x, 3x), 3D textures, 3D engine, Resume.
- GBA (RetroArch): Emulator (mGBA, gpSP), Screen size (Pixel-perfect, Fill screen), Screen (Sharp, Smooth), Resume,
  Rewind, Response (run-ahead).
- They're written to ROCKNIX's `system.cfg` as the system's settings (`nds.shader=ds-crisp`), the same keys
  EmulationStation uses, so EmulationStation shows them too. (A game's own setting made in EmulationStation still
  wins for that game.)

No emulator is bundled: the menu starts the ones on the handheld (ROCKNIXDS's DraStic for DS, ROCKNIX's RetroArch
for GBA).

## Install (RG DS Plus, two microSD cards)

Card 1 (8 GB is plenty) holds the OS, card 2 the games. The menu changes nothing in ROCKNIX's system files: it puts
its own launcher in front of EmulationStation, and steps aside when asked or when it fails.

**1. The OS card: flash ROCKNIXDS's image** (ROCKNIX with ROCKNIXDS's DraStic, set up for the RG DS Plus).
- Download `rocknixds-...-plus-rocknix-....img.gz` from [ROCKNIXDS's releases](https://github.com/JorreFog/ROCKNIXDS/releases?q=plus)
  (about 1.6 GB, about 2 GB unpacked: it fits the 8 GB card; ROCKNIX grows its storage partition to the whole card on
  the first boot). Don't unzip it.
- Write it to the **8 GB card** with [balenaEtcher](https://etcher.balena.io/) (*Flash from file*, pick the card,
  *Flash!*). Double-check the target: everything on that card is erased.

**2. The games card** (16 GB, exFAT or FAT32, as it comes or formatted on your computer):
- Make a folder `roms` with `nds` and `gba` inside, and put your games there (`roms/nds/*.nds`, `roms/gba/*.gba`).
- Unpack `dist/dsixl-<version>.zip` (from this repository) onto the **root of this card**: it adds
  `roms/ports/Install DSi Menu.sh`, `roms/ports/Uninstall DSi Menu.sh` and a hidden `roms/ports/.dsixl-install`.

**3. First boot.** OS card in the slot the handheld boots from (the one marked as the system/TF1 slot), games card
in the other. Switch on: ROCKNIX sets up the card and restarts once by itself, ROCKNIXDS installs itself (about a
minute), and EmulationStation comes up. ROCKNIX finds the second card by itself and uses its `roms` folder for the
games (it's mounted at `/storage/games-external`, its `roms` shown at `/storage/roms`). Your games and *Ports* appear.

**4. Install the menu:** in EmulationStation, *Ports > Install DSi Menu*. It copies the menu to
`/storage/.config/dsixl` (on the OS card), checks the emulators, sets the recommended emulator settings where you
haven't set any (yours are never changed), makes the menu the start screen, and opens it. The log is
`/storage/dsixl-install.log`.

**5. Wi-Fi, time zone and art:** in the menu, *Options > Wi-Fi* (pick your network, type the password),
*Options > Time zone*, then *Options > Get art from the internet*. The art goes onto the games card
(`roms/dsixl/media`).

**Updating later:** no reflash, no ssh. Copy the new `dsixl-x.y.z.zip` itself (not unpacked) into the games card's
`roms/ports` folder (or its top folder). The menu says *Update x.y.z ready*; *Options > Update to x.y.z* installs
it and the menu starts again on the new version.

**Safe ways back:**
- *Options > Exit to EmulationStation*: EmulationStation until the next boot.
- If the menu fails to start twice in a row, EmulationStation comes up instead, with a message.
- *Ports > Uninstall DSi Menu* removes it; EmulationStation is the start screen again.
- From a computer: delete `/storage/.config/dsixl/enabled` (the OS card's storage partition) and EmulationStation is the
  start screen again; nothing else needs undoing.

Over ssh instead (root, password `rocknix`): copy the repository to the handheld and run
`sh device/install.sh --enable` (add `--with-rocknixds` on a plain ROCKNIX install to get ROCKNIXDS's DraStic as
well; needs Wi-Fi). `--disable` / `--uninstall` undo it.

**The emulators** come with the image, not with the menu: ROCKNIXDS's DraStic (DS, straight to both panels) and
ROCKNIX's RetroArch with mGBA (GBA). The installer checks they're there and says so if one isn't.

## Art

```sh
python3 /storage/.config/dsixl/dsixl-scrape.py      # on the handheld (Wi-Fi), or on a computer with --nds/--gba/--media
```

No account and no key needed. The **LaunchBox Games Database** comes first, for everything it has: its public
database (`Metadata.zip`, downloaded once a month into `media/.launchbox/` and read as a stream, so it fits in the
handheld's memory) lists each game's images, matched to your ROMs by name (alternate names too), your ROM's region
first:

| LaunchBox image | Used as |
|---|---|
| Cart - Front | the cartridge's whole face (a real scan) |
| Box - Front | the box (cropped to a label when there's no cartridge scan) |
| Clear Logo | the top screen's logo |
| Screenshot - Gameplay | the photo view, with the logo under it |
| Screenshot - Game Title | the photo view, for games without a logo |

What LaunchBox doesn't have comes from GameTDB (DS covers and cartridge scans, by the game code in the ROM) and
libretro-thumbnails (box art, title screens, screenshots, GBA logos). `--no-launchbox` skips LaunchBox;
`--refresh-launchbox` downloads its database again now.

Your own art always wins. Drop files in `/storage/roms/dsixl/media/<nds|gba>/<box|cart|label|logo|title|snap>/<ROM name
without extension>.png` (or `.jpg`): a `cart` is a scan of the real cartridge (used as its whole face), a `label`
fills the label area, a `box` is cropped to the label (its spine strip, rating and publisher strip cut off), a
`logo` should be a transparent PNG.
The menu also reads EmulationStation's scraped media beside the ROMs (`images/<name>-marquee.png` as the logo,
`-thumb`/`-image` as the box), so ScreenScraper art you already have shows up too.

## Sounds

Put the DSi's sounds (your own copies) in `/storage/roms/dsixl/sounds`: `boot.wav`, `cursor.wav`, `select.wav`,
`launch.wav`, `slam.wav`, ..., `music.mp3`. Every slot and the DSi sound that belongs in it:
[docs/sounds.md](docs/sounds.md). Until then, synthesized stand-ins play.

## Customising

- `config.ini`: your colour (`color`, the DS's 16 favourite colours: the selection frame, the
  tabs, the scroll bar, the title logos follow it), 12/24-hour clock, the boot screen (`boot_splash`, and
  `boot_wait = n` to continue by itself after n seconds), when the screens dim and go off (`dim_after`,
  `screen_off_after`, seconds; 0 never), the ROM, art and sound folders.
- `assets/boot_logo.png`: the boot logo is an approximation drawn by `tools/gen_assets.py`. Replace it with your
  own transparent PNG (about 216x32) to have the real one.
- The look lives in `src/widgets.c` (the DSi furniture, colours sampled from the DSi), `src/ui.c` (layout,
  animations, timings: the launch is the `L_*` timeline) and `src/cart.c` (the cartridges' shapes and faces).

## Building

```sh
sh build.sh            # this computer: build/dsixl (try it: ./build/dsixl --backend sdl, arrows/Z/X/Q/E/Enter, mouse = touch)
sh build.sh aarch64    # the handheld: build/dsixl-aarch64 (apt install gcc-aarch64-linux-gnu libdrm-dev libsdl2-dev)
```

C, no dependencies to link: libdrm, ALSA and SDL2 are opened at run time. `bin/dsixl-aarch64` is a prebuilt copy;
the installer uses `build/` first. Pictures without a display:
`./build/dsixl --backend headless --record out/ --seconds 8 --script "1:TOUCH@128x100,4:RIGHT"` writes every frame
as PNG.

Hardware notes and how the menu fits in 1 GB: [docs/hardware.md](docs/hardware.md).
Third-party code and fonts: [THIRD_PARTY.md](THIRD_PARTY.md).
