# Hardware test checklist

- Cold boot and record a short clip immediately.
- Stop and confirm the file appears in the library.
- Confirm the library shows SD free space and selected-file size/duration.
- Play it and verify intelligible audio.
- Adjust volume during playback.
- Copy the WAV to a computer and verify its duration and PCM format.
- Delete a recording.
- Open settings with a long `G0` press.
- Change brightness and confirm it is restored after reboot.
- Enter `Screen Saver` settings and verify `When Home`, `While Recording`,
  and `While Playing` can be set independently.
- Verify `Dimmed Standby` and `Black` during recording do not interrupt
  recording.
- Verify `Dimmed Standby` and `Black` during playback do not interrupt
  playback.
- Verify the first key press from a dimmed or black screen wakes the display
  without stopping recording/playback, changing volume, or deleting a file.
- Complete at least 20 record/play cycles.
- Record for at least ten minutes and verify stable memory and file growth.
- Test with no card and with less than 64 KiB free.
- Remove the card during recording and verify the app reaches an error screen
  without hanging.
- Repeat microphone-to-speaker transitions and listen for residual hiss.

Automated builds cannot verify acoustic quality, SD hot removal, speaker
routing, or battery calibration.
