# Sounds: making the menu sound like a DSi

The menu plays a sound file for each of these moments, from its sound folder
(`/storage/roms/dsixl/sounds`, on the network share as `roms/dsixl/sounds`). A missing file falls back to a built-in
synthesized sound. Put the DSi's own sounds here and the menu sounds like the DSi.

**Without a pack**, the menu makes its own (`src/synth.c`): soft glassy bells, a mellow electric piano, wooden
ticks with a little snap under them for the tactile feel, all ringing in a small room. Its music is an original
tune in the spirit of the DSi menu (a relaxed bossa in F, about 80 seconds going round), not Nintendo's theme. It is
played by a small sequencer as it goes: no file, almost no memory, a few percent of one CPU core. A `music.mp3`
in the pack replaces it.

The DSi's sounds aren't included: they are Nintendo's recordings. Use your own copies. For example, rip them from a
DSi you own, or extract them from the DSi system menu's sound archive (an `.sdat` file) with an SDAT tool such as
VGMTrans or Tinke, then export WAV files.

| File | When it plays | The DSi sound to use |
|---|---|---|
| `boot.wav` | the logo appears at power-on | the chime of the health and safety screen |
| `continue.wav` | touching to leave the health and safety screen | the "touch to continue" confirmation |
| `cursor.wav` | moving from one game to the next | the icon-to-icon "tick" of the menu |
| `select.wav` | A on a game, toggling an option | the "pon" of selecting an icon |
| `launch.wav` | the cartridge flies up | the start-a-title sound |
| `slam.wav` | the cartridge lands in its slot | a card-insert "clack" works well |
| `back.wav` | B, closing the options | the cancel sound |
| `bump.wav` | pushing past the end of the list | the "can't go further" bump |
| `tab.wav` | NDS ↔ GBA (L / R) | the camera tab (L/R) sound |
| `whoosh.wav` | the menu sliding in | the menu's icon slide |
| `open.wav` | the options window opening | a window/balloon open sound |
| `scroll.wav` | dragging along the scroll bar | the scroll bar's tick |
| `error.wav` | nothing to start | the error buzz |
| `notch.wav` | the selection locking into the frame (a detent) | a short, firm click (the DSi's icon-landing click) |
| `key.wav` | a key of the touch keyboard (Wi-Fi password) | the DSi keyboard's key click |
| `toggle.wav` | an option switched on (played lower for off) | a settings toggle |
| `close.wav` | a window closing | the window-close sound |
| `star.wav` | a game made a favourite | a sparkle |
| `done.wav` | something finished well (connected, art found) | a success chime |
| `page.wav` | a window's page, the album's pages | the page-turn sound |
| `wake.wav` | the lid opened, the screens back on | the open-lid sound |
| `bright.wav` | a brightness step | the brightness change click |
| `iris_in.wav` | the screens closing onto the slot, into a game (about half a second) | a falling run, a tick at the end |
| `sleep.wav` | the power button or the lid: the screens going out (`wake.wav` when they come back) | a soft chord settling down, a tick |
| `iris_out.wav` | the screens opening out of the slot, back from a game | a rising run, blooming |
| `music.mp3` (or `music.wav`) | the menu's music, looped | the DSi Menu theme |

- **Formats:** WAV (8, 16 or 24-bit PCM, or 32-bit float; mono or stereo; any sample rate) or MP3, under the same
  name (`cursor.mp3`). Everything is resampled to 48 kHz.
- **Trim the silence** at the start of each effect. The menu plays them the moment they happen, and leading
  silence makes the menu feel slow.
- **Music** streams from the MP3 as it plays (no memory spike), and fades out as a game starts.
- Options > **Sound effects** and **Music** turn each off. The menu reads the folder when it starts. Close it with
  the exit hotkey or start a game, and the new files play.
