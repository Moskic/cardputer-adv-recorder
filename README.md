# Cardputer ADV Recorder

A voice recorder and WAV player for the M5Stack Cardputer ADV.

The firmware records 16 kHz, mono, 16-bit PCM WAV files directly to microSD.
Recording and playback are streamed through fixed-size buffers, so file length
is not limited by available RAM.

> Only Cardputer ADV is supported. The original Cardputer uses different audio
> hardware and is not compatible with this firmware.

## Features

- Stream recordings to microSD as `/REC0001.WAV` through `/REC9999.WAV`.
- Browse, play, and delete WAV files from the device.
- Adjust playback volume while listening.
- Show recording level, elapsed time, file size, and battery status.
- Recover from missing cards, low space, and storage errors.
- Finalize WAV headers and sync storage before presenting a recording as saved.

## Requirements

- M5Stack Cardputer ADV
- Writable FAT32 microSD card
- PlatformIO Core 6.1.19 or PlatformIO IDE

## Build and upload

```sh
platformio run
platformio run --target upload
platformio device monitor
```

The serial monitor runs at 115200 baud. To run host-side tests:

```sh
platformio test -e native-tests
```

## Controls

| Key | Action |
| --- | --- |
| `R` | Start recording |
| `Enter`, `Esc`, or `R` | Stop and save a recording |
| Up/Down key positions (`;` / `.`) | Select a recording |
| `Enter` | Play or stop the selected recording |
| Up/Down during playback | Adjust volume |
| `Delete` | Delete the selected recording |

## Audio format

New recordings use 16 kHz mono 16-bit PCM. Playback accepts mono 16-bit PCM
WAV files and walks RIFF chunks instead of requiring audio to begin at byte 44.

See [audio and storage notes](docs/audio-and-storage.md),
[troubleshooting](docs/troubleshooting.md), and the
[hardware test checklist](docs/hardware-test-checklist.md).

## License

[MIT](LICENSE)
