# MuOS Minus Media Recovery Audit

Generated on 2026-05-21 from the current `mufrog-commit-recover` branch.

## Evidence

The primary source is the read-only Codex session history under
`~/.codex/sessions`. Recovered repositories under
`/root/host-frogdev/universal/temp` were used only as secondary evidence when
their commit titles and affected files matched JSONL captures.

Important JSONL anchors:

| Evidence | Date | JSONL position | Notes |
| --- | --- | --- | --- |
| `muos_frontend` at `5121dfa` | 2026-05-12 | `2026/05/12/rollout-2026-05-12T05-06-38...jsonl:1` | Branch started near `v0.4.4`. |
| `muos_adapter` at `803e3ad` | 2026-05-15 | `2026/05/15/rollout-2026-05-15T18-18-08...jsonl:1` and `:24284` | Captures the native frontend/theme chain through `8fd24b4`. |
| Theme/rendering chain | 2026-05-16 | `2026/05/16/rollout-2026-05-16T19-17-51...jsonl:1902` | Shows the late themed chrome/submenu/storage-hardening commits. |
| `muos_adapter_minus_media` at `5b54ff4` | 2026-05-18 | `2026/05/18/rollout-2026-05-18T21-47-41...jsonl:1` | Minus-media branch exists. |
| `muos_adapter_minus_media` at `dd4451f` | 2026-05-19 | `2026/05/19/rollout-2026-05-19T05-05-11...jsonl:1` | User-cited session family. |
| Recovery/no-MMC frontend chain | 2026-05-20 | `2026/05/20/rollout-2026-05-20T07-21-55...jsonl:4461` | Captures later frontend recovery titles. |
| Branch tip evidence `e92c2f2` | 2026-05-20 | `2026/05/20/rollout-2026-05-20T07-21-55...jsonl:4540` | `Fix ROM browsing false launches`. |
| Target chain through `e05e173` | 2026-05-21 | `2026/05/21/rollout-2026-05-21T04-49-16...jsonl:2942` | Storage/loading/crash diagnostic commits. |

## Commit Ledger

The branch is not recoverable with identical object IDs from the live repo, but
the session logs preserve the intended sequence. Hashes below are the recovered
or captured short hashes, not a claim that the current branch will reproduce
those exact IDs.

Native frontend and MuOS adapter path:

| Hash | Message | Evidence |
| --- | --- | --- |
| `571637e` / `75b3cde` | Add native frontend path | JSONL `2026-05-15...:24284`, recovered log |
| `fb1dd22` / `e6220ce` | Fix native quick menu and MuOS deps | JSONL `2026-05-15...:24284`, recovered log |
| `9c3f21d` / `f0c4230` | Add MuOS frontend adapter mode | JSONL `2026-05-15...:24284`, recovered log |
| `074b402` / `386a854` | Move MuOS dynamic lists to LVGL | JSONL `2026-05-15...:24284`, recovered log |
| `63e17b0` / `a38707c` | Latch quick menu entry input | JSONL `2026-05-15...:24284`, recovered log |
| `5080ee3` / `60d1789` | Expand MuOS frontend settings | JSONL `2026-05-15...:24284`, recovered log |
| `00f8b5d` / `d4ecfc0` | Guard invalid libretro content | JSONL `2026-05-15...:24284`, recovered log |
| `4839dec` / `84ce564` | Expand MuOS quick menu | JSONL `2026-05-15...:24284`, recovered log |
| `3bc829a` / `cccd7b9` | Refine MuOS pause menu state flow | JSONL `2026-05-15...:24284`, recovered log |
| `41c80bb` / `1b9a1fb` | Add MuOS core options and fix states | JSONL `2026-05-15...:24284`, recovered log |

Theme, language, and native rendering:

| Hash | Message | Evidence |
| --- | --- | --- |
| `8654e4c` / `e7561fe` | Make MuOS theme installs testable | JSONL `2026-05-15...:24284`, recovered log |
| `4b200f7` | Add MuOS theme and language selection | recovered log, current repo feature match |
| `1c8444a` | Install MuOS theme archives | recovered log, current repo feature match |
| `fb937c8` | Generalize MuOS theme loading | recovered log, current repo feature match |
| `166afa8` / `721c156` | Restore themed submenu wallpaper rendering | JSONL `2026-05-16...:1902`, recovered log |
| `5662a88` / `cd1fb31` | Remove transparent submenu text halos | JSONL `2026-05-16...:1902`, recovered log |
| `8a2e8bb` / `510cbf7` | Clean transparent list text and C64 launch layout | JSONL `2026-05-16...:1902`, recovered log |
| `90465ee` / `448ca75` | Refine themed submenu rendering | JSONL `2026-05-16...:1902`, recovered log |
| `7300cd2` / `32a41f5` | Stabilize themed text and installs | JSONL `2026-05-16...:1902`, recovered log |
| `c34052d` / `93ba278` | Stabilize heavy theme handling | JSONL `2026-05-16...:1902`, recovered log |
| `9a4b773` / `f435531` | Improve themed shell artwork | JSONL `2026-05-16...:1902`, recovered log |
| `66a617f` / `95396c5` | Improve themed artwork support | JSONL `2026-05-15...:24284`, JSONL `2026-05-20...:4461` |
| `803e3ad` / `4b072a7` | Expand MuOS theme asset support | JSONL `2026-05-15...:24284`, JSONL `2026-05-20...:4461` |
| `fd03441` / `d48b115` | Improve themed frontend chrome | JSONL `2026-05-15...:24284`, JSONL `2026-05-20...:4461` |
| `8fd24b4` / `9e4967a` | Tighten themed chrome and sound diagnostics | JSONL `2026-05-15...:24284`, JSONL `2026-05-20...:4461` |
| `3e65e2a` | Add theme font selection | JSONL `2026-05-20...:4461` |
| `bfad82d` | Show loading messages for theme changes | JSONL `2026-05-20...:4461` |
| `3f0ada6` | Support theme TTF fonts and broader media | JSONL `2026-05-20...:4461` |

Storage, logging, loading, and crash diagnostics:

| Hash | Message | Evidence |
| --- | --- | --- |
| `9f31677` | Split distribution and user data roots | recovered log, current repo feature match |
| `8d5d480` / `94e06a3` | Defer log writes during core sessions | JSONL `2026-05-15...:24284`, recovered log |
| `9ef9328` / `e184752` | Defer core-session SD logging | JSONL `2026-05-15...:24284`, recovered log |
| `7eace25` | Back off fast SD after read timeouts | recovered log |
| `b9c351b` / `27aacce` | Harden frontend storage stalls | JSONL `2026-05-16...:1902`, JSONL `2026-05-20...:4461` |
| `0975ad8` / `8f70ddf` | Harden frontend SD recovery | JSONL `2026-05-16...:1902`, JSONL `2026-05-20...:4461` |
| `16e8da4` | Recover compressed save states after storage loss | recovered log |
| `4d1b977` | Add state autosave and autoload | recovered log |
| `80e670d` | Expose BIOS directory to libretro cores | recovered log |
| `5e3df11` / `ee83a81` | Restyle pause menu like submenus | recovered log, JSONL `2026-05-20...:4461` |
| `7f4d6b6` / `56625b3` | Fix frontend return paths and core input maps | recovered log, JSONL `2026-05-20...:4461` |
| `f67ab91` | Fix script navigation and loading screens | recovered log |
| `31c095e` / `e05e173` | Reduce SD contention during ZIP loads | recovered log, JSONL `2026-05-21...:2942` |
| `dc9871c` / `b95550e` | Add retained crash phase diagnostics | recovered log, JSONL `2026-05-21...:2942` |
| `c983f6e` / `c358be0` | Retain/full exception register context | recovered log, JSONL `2026-05-21...:2942` |
| `57da8c7` / `3ee6383` | Stabilize loading display path | recovered log, JSONL `2026-05-21...:2942` |
| `85c2b11` / `c643018` | Force safe SD transfers through PIO | recovered log, JSONL `2026-05-21...:2942` |
| `e92c2f2` | Fix ROM browsing false launches | JSONL `2026-05-20...:4540`, recovered log |

## Reverted or Excluded

These should not be ported into the minus-media end state:

- `cd53436`, `17ce6f5`, `b192f62`, `6c88a4f`, `4f9f8c8`, `76a6889`:
  HCRTOS/AVI image probe experiments. The branch later reverted them, and the
  final SD package should not contain `hcrtos-image*`.
- The matching probe reverts `3072fa1`, `36d635e`, `d8ac245`, `1478c32`,
  `03bffd7`, and `7147931` are treated as net-zero.
- Media-player replacement commits after the adapter split are outside the
  `muos_adapter_minus_media` target unless a later minus-media commit retained a
  specific compatibility fallback.
- `d651f7f Revert "Preserve crash logs across storage faults"` means the
  reverted crash-log preservation behavior should not be reconstructed as-is.

## Current Repository Comparison

Present in the current repository:

- Immutable `unifrog` plus mutable `unifrog_data`, including
  `unifrog_data/logs/reports`.
- Native frontend is the active path; JavaScript frontend packaging is no longer
  the main UI.
- Theme archive loading, language `.ini` files, theme selection, and language
  selection exist.
- Open-with/core/media choice flow exists.
- Native loading feedback exists for theme changes, scripts, launch, storage,
  firmware, and update actions.
- The SD output no longer contains the reverted `hcrtos-image*` probe files.

Partially present or still mismatched:

- The recovered themed renderer chain is only partially represented. The
  current renderer has grid launch support and wallpaper/static image loading,
  but it does not carry the complete historical LVGL object model. The most
  obvious bug found here was that alpha `0` values from themes were converted to
  opaque panels, contradicting the transparent submenu and wallpaper-rendering
  commits.
- Theme TTF font selection is documented and `stb_truetype` exists, but the
  current native frontend still mostly uses the compact built-in text path.
- Some late automated frontend driver and retained diagnostic workflow commits
  are present in spirit but not identical to the recovered branch.

Applied in this recovery pass:

- Preserve theme alpha `0` for header, footer, list rows, and launch tiles so
  transparent submenu themes composite over the freshly drawn wallpaper instead
  of becoming opaque blocks.
- Honor header/footer/list text alpha where the current renderer has a matching
  field, instead of drawing hidden theme text unconditionally.
