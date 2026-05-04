# Core Source Workflow

Core source inputs are listed in `cores/manifest.mk`.

Each manifest entry has:

```text
name|checkout-directory|upstream-url|upstream-commit
```

Shared support entries add sparse paths:

```text
name|checkout-directory|upstream-url|upstream-commit|sparse-paths
```

`make -C cores init` fetches those exact upstream commits into `.deps/cores/`
and applies the matching patch series from `cores/patches/<name>/`. It also
fetches shared support sources into `.deps/support/`. The app repository does
not commit those source trees and does not use Git submodules for them.

Useful commands:

```sh
make -C cores core-status
make -C cores log-core CORE=gpsp
make -C cores diff-core CORE=gpsp
make -C cores update-core CORE=gpsp
```

UniFrog-specific source edits live in committed patch files. To inspect the
current integration for one core, run:

```sh
make -C cores diff-core CORE=gpsp
make -C cores log-core CORE=gpsp
```

To update a core, change its pinned upstream commit in `cores/manifest.mk`, run
`make -C cores update-core CORE=<core>`, and refresh only the patches that no
longer apply cleanly. Prefer committed overlays for new integration files when
possible, and keep direct upstream source edits focused enough to review when
rebasing onto newer upstream code.
