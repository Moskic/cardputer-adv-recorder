# Troubleshooting

## The firmware reports that Cardputer ADV is required

This project intentionally rejects the original Cardputer. Confirm the device
is an M5Stack Cardputer ADV.

## The microSD card does not mount

- Format the card as FAT32.
- Reinsert it and press `Enter` on the error screen.
- Try a different card if mounting or sustained writes remain unreliable.

## Recording does not start

- Cold boot the device and retry.
- Confirm at least 64 KiB is free.
- Check the 115200 baud serial log for errors `E4` through `E9`.

## A recording does not play

Playback supports mono 16-bit PCM WAV files. Other WAV encodings are rejected.
Recordings made by this firmware use the supported format.

## Build problems

Use PlatformIO Core 6.1.19 and keep the dependency versions from
`platformio.ini`. For a clean rebuild:

```sh
platformio run -e cardputer-adv-recorder --target clean
platformio run -e cardputer-adv-recorder
```
