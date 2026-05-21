# Unifrog Goal

This document captures the content and launcher direction for Unifrog, based on
the MuOS frontend investigation. MuOS is the inspiration for several of the
ideas below, but the implementation and product wording should stay
Unifrog-first.

## End Goal

Unifrog should provide a clear, reliable, file-driven frontend for the SF2000:

- Users browse games, apps, tools, themes, languages, updates, and firmware
  from the SD card.
- Content discovery follows a configurable folder layout instead of one fixed
  ROM directory rule.
- The layout works well for simple SF2000 libraries and remains compatible with
  MuOS-style organized trees.
- Mutable state stays in `unifrog_data`, while immutable system assets and
  packaged runtime files stay in `unifrog`.
- The frontend remains fast, understandable, and recoverable on a non-Linux
  device.

## What We Are Taking As Inspiration

The goal is not to copy MuOS feature-for-feature. The useful parts are the ones
that can be expressed as files, folders, metadata, and launcher state.

### 1. Organized content library

MuOS showed a useful pattern for storing content in a structured tree with
system-specific metadata. For Unifrog, that becomes:

- Configurable content roots.
- System folders under a chosen root, such as `ROMS/gba`.
- Optional per-system metadata files.
- Optional per-game metadata sidecars.
- A predictable fallback path when metadata is missing.

Success looks like this:

- A library can be laid out in a simple folder structure.
- Advanced users can add richer metadata without breaking the basic browser.
- Existing MuOS-style trees can be recognized without requiring the full MuOS
  runtime.

### 2. Browse, launch, and resume

MuOS separates launch history, collections, and content browsing. Unifrog
should keep the same user value, but with a simpler storage model:

- Recent games are stored in `unifrog_data/history.txt`.
- Favorites are stored in `unifrog_data/favorites.txt`.
- Launch state is saved so the frontend can return to the same place.
- The last launched item should be easy to relaunch.

Success looks like this:

- The user can jump into a system, launch a game, and return to the same list.
- Recently used titles are easy to find.
- Favorites are a file-backed list, not a database.

### 3. File-backed metadata and artwork

MuOS uses assign/catalogue files to attach artwork and descriptions to content.
Unifrog should support the same concept in a simpler and more explicit way:

- System metadata may live beside the library or under `unifrog/info`.
- Box art and preview art should be resolved by folder name and file stem.
- Descriptions should be plain text files where available.
- Missing art should fail gracefully with a default icon or label.

Success looks like this:

- A library can gain cover art without changing the launcher code.
- Users can organize art by system or catalogue.
- The browser still works when no art exists.

### 4. Lightweight collection features

MuOS provides collection and history workflows that are mostly file operations.
Unifrog should keep that spirit, but avoid complexity that does not help on the
SF2000:

- Favorites / collection management.
- Recent play history.
- Optional local tags or notes.
- Optional per-game control or shader files when they are stored as sidecars.

Success looks like this:

- The user can mark content without leaving the device.
- The feature set stays understandable from the filesystem alone.

### 5. Theme and language management

MuOS treats themes and languages as installed content. That fits Unifrog well:

- Themes live in `unifrog_data/themes`.
- Languages live in `unifrog_data/languages`.
- Theme previews are cached in `unifrog_data/cache`.
- Switching a theme or language updates a simple config file.

Success looks like this:

- Themes can be added or removed by copying files.
- The active theme and language are easy to inspect and recover.

### 6. Storage and firmware tools

MuOS has a lot of storage and system tools. On SF2000 we should keep only the
parts that are useful without a Linux service layer:

- Storage profile selection.
- Storage recovery.
- Firmware boot selection from `.asd` files.
- Update package selection from `unifrog/updates`.
- A visible summary of what is currently active.

Success looks like this:

- Users can recover SD issues without guessing.
- Updates are file-based and easy to validate.
- Firmware and updates remain separate from user content.

## What We Are Not Taking

These MuOS areas are not a good fit for SF2000 and should stay out of the
main product scope:

- Network profile management.
- SSH, SFTP, Tailscale, Syncthing, or similar services.
- Web server or remote admin features.
- Background daemon orchestration.
- Features that assume a Linux userspace and shell-first workflow.

## Unifrog Storage Model

Keep the split simple:

- `unifrog/` is for immutable packaged system files, launcher assets, and
  shipped runtime content.
- `unifrog_data/` is for user content, settings, caches, saves, histories,
  favorites, themes, languages, scripts, and update inputs.
- Game libraries remain under `ROMS` unless a configured path says otherwise.

Suggested user-facing explanation:

- `unifrog` is what the system ships with.
- `unifrog_data` is what the user owns and changes.
- `ROMS` is where games live.

## Concrete Discovery Goal

The current discovery model should evolve from one fixed ROM rule into a
configurable content map:

- One or more content roots can be defined.
- Each root can contain systems as first-level folders.
- Each system can optionally define its own metadata and art lookup rules.
- File suffixes remain a fallback, not the only discovery mechanism.
- MuOS-style layout conventions should work when they are present.

That means the browser should be able to answer:

- "Where are the content roots?"
- "Which folder is the system?"
- "Which core or launcher should this content use?"
- "Where are the art and description files?"

without hard-coding the answer to a single folder layout.

### Discovery contract

Use a simple precedence order so the behavior stays predictable:

1. Read the configured content roots.
2. Treat the first directory under each root as the system key.
3. Look for per-system metadata before falling back to global defaults.
4. Resolve artwork and descriptions from the system, catalogue, or file stem.
5. Fall back to suffix-based core detection only when folder structure is not
   enough.
6. Keep the current safe fallback behavior so legacy libraries still launch.

A compatible library should be understandable from its folders alone. A
practical example is:

```text
ROMS/
  gba/
    Game Name.gba
    Game Name.cfg
  snes/
    Game Name.sfc
unifrog_data/
  history.txt
  favorites.txt
  settings.ini
unifrog/
  info/
    assign/
      gba/global.ini
    catalogue/
      gba/
        box/
        preview/
        text/
```

The goal is that a user can copy in this tree and immediately understand how
content is organized, where metadata lives, and where the system should look
for art or descriptions.

### Implementation milestones

The goal should be delivered in small, visible steps:

- Phase 1: configurable content roots, while preserving the current `/ROMS`
  behavior as the default.
- Phase 2: system metadata and artwork lookup that can use MuOS-style folders
  when present.
- Phase 3: file-backed history, favorites, and launch resume behavior that
  stays inside `unifrog_data`.
- Phase 4: theme, language, storage, and firmware tools that remain file-driven
  and do not depend on Linux services.

Each phase should be useful on its own and should not require the later phases
to be complete before it can ship.

## Target Outcome

When this work is complete, Unifrog should feel like:

- A simple handheld launcher for everyday use.
- A structured file browser for advanced users.
- A MuOS-inspired library layout that does not require Linux services.
- A system that is easy to back up, inspect, and repair by copying files.

## Definition Of Done

This goal is complete when all of the following are true:

- The content browser supports configurable roots and a documented folder
  layout.
- The launcher can still handle existing libraries without extra user steps.
- History, favorites, settings, themes, languages, and caches live in
  `unifrog_data`.
- Packaged assets and system files live in `unifrog`.
- The user-facing docs explain the layout in plain language.
- The implementation remains a good fit for the SF2000 and does not depend on
  Linux services.

