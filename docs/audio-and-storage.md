# Audio and storage

## Audio lifecycle

Cardputer ADV uses an ES8311 codec. Microphone and speaker operation are
mutually exclusive and are serialized by `AudioService`.

During capture, the firmware keeps M5Unified's two microphone slots occupied
and transfers completed blocks to a deeper application queue. A dedicated
low-priority task applies capture gain, updates the level snapshot, and writes
blocks to microSD.

Playback queues fixed-size mono 16-bit PCM blocks. A queued buffer must remain
unchanged until the speaker has consumed it.

## WAV finalization

Recording starts with a streaming WAV header. On normal stop, the writer seeks
back to repair the RIFF and data sizes, syncs the descriptor, and closes it.
An incomplete recording is discarded on capture or write failure.

The reader handles unknown RIFF chunks and does not require a fixed data
offset. Playback is limited to mono 16-bit PCM.

## microSD

Cardputer ADV microSD uses SCK 40, MISO 39, MOSI 14, and CS 12. The firmware
uses a conservative 10 MHz clock for reliable sustained writes. These values
belong in `StorageService` and should not be duplicated elsewhere.
