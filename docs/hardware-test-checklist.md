# Hardware test checklist

- Cold boot and record a short clip immediately.
- Stop and confirm the file appears in the library.
- Confirm the library shows SD free space and selected-file size/duration.
- Open help with `H`, page through it, and return with `Enter` or `Esc`.
- Confirm Help shows only the simplified Library, Manage, Recording,
  Playback, and System pages.
- Play it and verify intelligible audio.
- Pause and resume playback several times.
- Confirm `Seek Step` offers 5, 10, 20, and 60 seconds and defaults to
  5 seconds.
- Seek backward and forward during playback and verify the progress changes
  by the configured seek step.
- Adjust volume during playback.
- Use `[` and `]` during playback and verify speeds change live through
  0.75x, 1.0x, 1.25x, 1.5x, and 2.0x.
- Verify pause/resume, seek, and volume controls still work after changing
  playback speed.
- Verify playback speed starts at 1.0x each time playback begins.
- Verify Dimmed Standby shows the current playback speed.
- Verify the first wake key from Dimmed Standby or Black does not change
  playback speed.
- Copy the WAV to a computer and verify its duration and PCM format.
- Rename a recording and confirm `.WAV` is preserved.
- Try a rename that conflicts with an existing file and confirm it is blocked.
- Try unsupported rename characters and confirm they are not accepted.
- Lock a recording and confirm it shows the lock marker.
- Confirm a locked recording cannot be deleted.
- Unlock the recording and confirm deletion requires confirmation.
- Delete an unlocked recording.
- Remove `RECORDER.LCK` and confirm the library still opens.
- Open settings with a long `G0` press.
- Change brightness and confirm it is restored after reboot.
- Confirm `Low Battery Save` offers Off, 1%, 5%, and 10%, defaults to 10%,
  and is restored after reboot.
- Confirm no WIP settings are shown.
- Confirm `Reset to Default` asks for confirmation before resetting settings.
- Confirm settings shows the current firmware version.
- Enter `Screen Saver` settings and verify `When Home`, `While Recording`,
  `While Playing`, and `Triple-Press Wake` can be set independently.
- Verify `Dimmed Standby` and `Black` during recording do not interrupt
  recording.
- Verify `Dimmed Standby` and `Black` during playback do not interrupt
  playback.
- Verify the first key press from a dimmed or black screen wakes the display
  without stopping recording/playback, changing volume, or deleting a file.
- Verify the first key press from a dimmed or black screen does not pause,
  resume, seek, or stop playback.
- Turn on `Triple-Press Wake` and verify the same key must be pressed three
  times to wake from `Dimmed Standby` and `Black`.
- Verify a different key resets the triple-press wake count.
- Verify triple-press wake timeout requires starting the count again.
- With `Low Battery Save` enabled, simulate or test a low battery and confirm
  an active recording saves normally at the selected threshold.
- With `Low Battery Save` off, confirm low battery does not auto-stop a
  recording.
- Complete at least 20 record/play cycles.
- Record for at least ten minutes and verify stable memory and file growth.
- Test with no card and with less than 64 KiB free.
- Remove the card during recording and verify the app reaches an error screen
  without hanging.
- Repeat microphone-to-speaker transitions and listen for residual hiss.

Automated builds cannot verify acoustic quality, SD hot removal, speaker
routing, or battery calibration.
