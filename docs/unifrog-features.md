# Unifrog Features

Unifrog is the launcher and system shell for the SF2000. It is designed to be
easy to understand, easy to repair, and mostly controlled by files on the SD
card.

## How It Is Organized

- `unifrog/` contains shipped system files and packaged assets.
- `unifrog_data/` contains user settings, caches, saves, history, favorites,
  themes, languages, scripts, and update inputs.
- `ROMS/` contains games and related content.

If you copy or remove files in those locations, Unifrog updates with them.

## Main Features

### Game browsing

- Browse game folders directly from the SD card.
- Launch content from a simple, configurable library layout.
- Set the content root in `unifrog_data/unifrog.ini` with `rom_root`.
- Use system folders and file names to keep content organized.

### History and favorites

- Open recent games quickly.
- Mark games as favorites.
- Keep both lists as plain files, so they are easy to back up.

### Themes and languages

- Change the visual theme.
- Select a font bundled with the active theme when the theme provides one.
- Switch launcher language packs.
- Store theme previews and caches on the SD card.

### Box art and descriptions

- Show cover art when it is available.
- Show preview art and text descriptions when they exist.
- Fall back to simple labels when artwork is missing.
- Support MuOS-style `assign` and `catalogue` folders when they are present.

### Storage tools

- Check and recover SD storage behavior.
- Switch between supported storage profiles.
- View storage status from the launcher.
- Expose `/bios` as the libretro system directory so emulator BIOS files can
  live on the SD card with the rest of the system files.

### Firmware and updates

- Browse firmware boot files.
- Select update packages from the SD card.
- Keep firmware files separate from user games.

### Apps and scripts

- Open built-in tools and helper screens.
- Browse and play videos, music, and images from Apps -> Media Player.
- Run optional local scripts stored on the SD card.
- Keep scripted actions separate from core launcher content.

### Launch defaults

- Choose whether boot lands on the launcher, resumes the last game, or opens a
  specific launcher section.
- Toggle save-state auto-load and auto-save from the launcher.
- Pick the state slot used by the in-game quick menu and launch defaults.
- Remap the launcher shortcuts for resume, log flush, and screenshot capture.

### System info

- View battery, device, and runtime information.
- Open diagnostic screens when needed.
- Keep the system recoverable from the device itself.

## What Unifrog Does Not Try To Be

- It is not a Linux desktop.
- It is not a network service manager.
- It is not a remote admin console.
- It does not depend on background daemons to browse games or launch content.

## Inspiration

Several layout and library ideas were inspired by MuOS. The goal in Unifrog is
to keep the useful file-based parts, simplify the rest, and make the behavior
fit the SF2000.
