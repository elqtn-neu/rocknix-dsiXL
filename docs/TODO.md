# To do

## Improvements
- [x] 1. Options > Get art: download art from the menu (Wi-Fi), DSi window with a progress bar, carts pop in as art lands
- [x] 2. Update from the games card: a newer `dsixl-*.zip` in `roms/ports` -> "Update available" -> installs itself
- [x] 3. Lid close = sleep (wake to the same state, out of black, open sound); low-battery notices + chime; charging notice
      (not done: a close sound: ROCKNIX's suspend mutes and freezes the menu the moment the lid closes)
- [x] 4. Resume badges on carts with a resume point; "Continue playing" shows the in-game frame where you stopped
- [x] 5. Volume overlay (DSi bars on the top screen)
- [x] 6. Jump by letter: drag the scroll bar -> letter bubble; up/down skip letters (L/R stay the tabs)
- [x] 7. Sort (name / recent / most played) and hide games
- [x] 8. Play time per game in the X photo view ("Played 12h 40m, last played yesterday")
- [x] 9. Time-of-day mood: the top screen's tint shifts morning -> evening -> night
- [x] 10. Album: DraStic/RetroArch screenshots, DSi Camera style, slideshow in the photo frame
- [x] 11. Load art in the background: instant start with 300+ games

## Others
- [x] O1. Brightness: hold the Anbernic button + volume up/down, both screens, 5 steps like the DSi, menu and games
      (a small helper service), DSi bar in the menu, remembered across reboots
- [x] O2. Power: redraw only when something moves, lowest clocks in the menu, dim after 2 min then screens off,
      Wi-Fi off unless needed, suspend on lid close, GBA bottom backlight off, battery-drain readout in Options
      (done: 30 fps when idle, dim, screens off, lid, GBA bottom off, battery time left. Not done: forcing clocks down
      and Wi-Fi off by itself; to be measured on the handheld first)
- [x] O3. Fade to black into games (stay black until the emulator draws) and fade back into the menu
- [x] O4. DSi XL startup: white -> "Nintendo DSi" logo + startup chime -> Health and Safety, touch to continue
      (every time, like the DSi) -> white wipe -> menu (ROCKNIX's boot logo stays: it's in the system image)

## Quality of life
- [x] Q1. Automatic save backups after every game (`roms/dsixl/backups`, last 5 per game)
- [x] Q2. Low-battery rescue: at ~3% in a game, save a resume point and exit
- [x] Q3. Friendly launch errors: DSi-style "This software couldn't be started" with the reason
- [x] Q4. Wi-Fi in Options with a DSi-style touch keyboard
- [x] Q5. Clock over Wi-Fi + time zone in Options
- [x] Q6. Controls reminder on the top screen the first time each emulator launches (and in Options)
- [x] Q7. New games found at startup on their own, "New" badge until first played
- [x] Q8. Free space on the games card in Options
- [x] Q9. Hold A during the startup -> straight into "Continue playing"
