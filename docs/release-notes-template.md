## What's new

- (List changes here)

## Files

| File | What it is |
|---|---|
| `snake_os.<version>.pi0.img` | SD card image for Raspberry Pi Zero / Zero W |
| `snake_os.<version>.sha256.txt` | SHA-256 checksum |
| `snake_os.<version>.sha256.txt.sig` | GPG signature of the checksum file |
| `snake_os.<version>.legal-info.tar.gz` | Licenses and source code for the image's contents |

## Install

1. Verify the download:
   `shasum -a 256 -c snake_os.<version>.sha256.txt` (macOS) or
   `sha256sum -c snake_os.<version>.sha256.txt` (Linux)
2. Flash the `.img` to a microSD card with balenaEtcher or Raspberry Pi Imager.
3. Insert the card and power on.

Using a 320x240 screen? Press KEY3 on the menu to switch screen type.

## Reproducible build

SHA-256: `<paste hash here>`

Rebuild it yourself with the instructions in docs/BUILDING.md
("Verifying a release by rebuilding it").
