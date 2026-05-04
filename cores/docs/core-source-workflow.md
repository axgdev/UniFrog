# Core Source Workflow

Core source inputs are listed in `cores/manifest.mk`.

Each manifest entry has:

```text
name|checkout-directory|source-branch
```

`make -C cores init` clones or updates those branches into `.deps/cores/`.
The app repository does not commit those source trees and does not use Git
submodules for them.

Useful commands:

```sh
make -C cores core-status
make -C cores log-core CORE=gpsp
make -C cores diff-core CORE=gpsp
make -C cores update-core CORE=gpsp
```

UniFrog-specific source edits currently live on the manifest branches. Prefer
committed overlays for new integration files when possible, and keep direct
upstream source edits focused enough to review when rebasing onto newer upstream
code.
