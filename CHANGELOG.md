# Changelog

## 1.3.0 - Playback controls and file management

- Add pause and resume during playback.
- Add 10-second seek backward and forward during playback.
- Keep volume controls on Up/Down and use `Esc` to stop playback.
- Show `PAUSED` in the playback UI and dimmed playback screen.
- Keep wake-first-key behavior for dimmed or black screen playback.
- Add library file rename.
- Add recording lock state with delete protection.
- Add delete confirmation for unlocked recordings.
- Sort recordings by filename descending.
- Add a paged help screen opened with `H` from the library.

## 1.2.0 - App structure and release packaging

- Split the recorder application implementation into focused app modules for
  recording flow, playback flow, capture buffering, UI, settings, screen saver,
  and file browsing.
- Keep `RecorderApp` as the public application entry point while reducing the
  main implementation file size.
- Simplify release packaging to publish one complete flash image plus
  `SHA256SUMS.txt`.
- Document complete release image flashing at offset `0x0000`.

## 1.1.0 - Display settings and screen saver

- Change the header label to `RECORDER`.
- Show SD free space and selected-file size/duration in the library.
- Add persistent settings with brightness control.
- Add screen saver settings for home, recording, and playback states.
- Add Dimmed Standby and Black screen saver modes that keep recording and
  playback running.
- Add wake-first-key handling so the first input from a dimmed or black screen
  only restores the display.
- Mark stored but not-yet-implemented settings as WIP in the menu.

## 1.0.0 - Initial release

- Initial standalone release of Cardputer ADV Recorder.
- Stream 16 kHz mono PCM recordings to microSD.
- Browse, play, adjust volume, and delete recordings on the device.
- Add native WAV parsing and recording-name tests.
- Add automated firmware builds, tests, and release artifacts.
