# To do

## Improvements
- [ ] 1. Options > Get art: download art from the menu (Wi-Fi), DSi window with a progress bar, carts pop in as art lands
- [ ] 2. Update from the games card: a newer `dsixl-*.zip` in `roms/ports` -> "Update available" -> installs itself
- [ ] 3. Lid close = sleep (fade out, DSi close sound, wake to the same state); low-battery blink + chime; charging animation
- [ ] 4. Resume badges on carts with a resume point; "Continue playing" shows the in-game frame where you stopped
- [ ] 5. Volume overlay (DSi bars on the top screen)
- [ ] 6. Jump by letter: drag the scroll bar -> letter bubble; hold L/R skips letters
- [ ] 7. Sort (name / recent / most played) and hide games
- [ ] 8. Play time per game in the X photo view ("Played 12h 40m, last played yesterday")
- [ ] 9. Time-of-day mood: the top screen's tint shifts morning -> evening -> night
- [ ] 10. Album: DraStic/RetroArch screenshots, DSi Camera style, slideshow in the photo frame
- [ ] 11. Load art in the background: instant start with 300+ games

## Others
- [ ] O1. Brightness: hold the Anbernic button + volume up/down, both screens, 5 steps like the DSi, menu and games
      (a small helper service), DSi bar in the menu, remembered across reboots
- [ ] O2. Power: redraw only when something moves, lowest clocks in the menu, dim after 2 min then screens off,
      Wi-Fi off unless needed, suspend on lid close, GBA bottom backlight off, battery-drain readout in Options
- [ ] O3. Fade to black into games (stay black until the emulator draws) and fade back into the menu
- [ ] O4. DSi XL startup: white -> "Nintendo DSi" logo + startup chime -> Health and Safety, touch to continue
      (every time, like the DSi) -> white wipe -> menu; ROCKNIX's boot splash white if possible without touching the
      system partition

## Quality of life
- [ ] Q1. Automatic save backups after every game (`roms/dsixl/backups`, last 5 per game)
- [ ] Q2. Low-battery rescue: at ~3% in a game, save a resume point and exit
- [ ] Q3. Friendly launch errors: DSi-style "This software couldn't be started" with the reason
- [ ] Q4. Wi-Fi in Options with a DSi-style touch keyboard
- [ ] Q5. Clock over Wi-Fi + time zone in Options
- [ ] Q6. Controls reminder on the top screen the first time each emulator launches (and in Options)
- [ ] Q7. New games found at startup on their own, "New" badge until first played
- [ ] Q8. Free space on the games card in Options
- [ ] Q9. Hold A during the startup -> straight into "Continue playing"
