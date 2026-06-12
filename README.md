# Velocity 🧭

This branch is prepared for Codex exploration of the Velocity GVSOC target.
The goal is simple: build the hardware model, build the example software, and
run the simulator from a fresh checkout with minimal guessing.

## Quick Start 🚀

Use `bash`. From the repository root:

```bash
eval "$(/usr/local/anaconda3/bin/conda shell.bash hook)"
export CCACHE_DIR="$PWD/.ccache"
source init.sh
make hw sw run
```

A successful run ends with the GVSOC progress bar reaching `100%`.

## First Run In The Codex Sandbox 🧰

The Codex sandbox usually needs a few extra steps on the first run because the
README command fetches submodules and toolchains from GitHub, and the default
home directory cache can be read-only.

### 1. Clone The Branch 🌱

```bash
git clone --branch codex_velocity https://github.com/husterZC/gvsoc.git
cd gvsoc
```

If GitHub access fails with `Could not resolve host: github.com`, rerun the
clone with network access enabled.

### 2. Enable Conda For Bash 🐍

`init.sh` starts with:

```bash
conda activate py312
```

In non-interactive Codex shells, `conda activate` may fail unless the conda
bash hook is loaded first:

```bash
eval "$(/usr/local/anaconda3/bin/conda shell.bash hook)"
```

The expected environment is `py312`, and the build expects Python `>= 3.12`.

### 3. Put ccache In The Workspace 📦

The sandbox may make `/home/.../.ccache` read-only. Use a cache directory inside
the repo:

```bash
export CCACHE_DIR="$PWD/.ccache"
```

This avoids errors like:

```text
ccache: error: Failed to create temporary file for /home/.../.ccache/tmp/...: Read-only file system
```

### 4. Fetch Submodules And Toolchains 🛠️

`source init.sh` updates Git submodules and prepares the RISC-V toolchain when
needed:

```bash
source init.sh
```

If an earlier network-blocked attempt created an empty `third_party/toolchain`
directory, `init.sh` may incorrectly skip the download. Force the toolchain
target once:

```bash
make -B third_party/toolchain
```

This downloads and extracts the RISC-V toolchains used by the software build.
Network access is required for this step.

### 5. Build And Run ✨

```bash
make hw sw run
```

This performs:

- `make hw`: configures Velocity and builds/installs the GVSOC model.
- `make sw`: builds `sw_build/velocity.elf` and `sw_build/velocity.dump`.
- `make run`: runs `sw_build/velocity.elf` on `pulp.chips.velocity.velocity_target`.

## Known Sandbox Notes 📝

These messages can appear in the cluster/Codex environment and are usually not
the real failure:

```text
yp_bind_client_create_v3: RPC: Remote system error - Operation not permitted
/usr/bin/id: cannot find name for user ID ...
logger: socket /dev/log: Operation not permitted
```

Treat them as environment noise unless the command exits non-zero for another
reason.

## Common Failures And Fixes 🧯

### `conda activate` Is Not Configured

Symptom:

```text
CommandNotFoundError: Your shell has not been properly configured to use 'conda activate'.
```

Fix:

```bash
eval "$(/usr/local/anaconda3/bin/conda shell.bash hook)"
source init.sh
```

### Python 3.6 Parses GVSOC Python Files

Symptom:

```text
SyntaxError: future feature annotations is not defined
```

Cause: the build is using system Python 3.6 instead of the `py312` conda
environment.

Fix:

```bash
eval "$(/usr/local/anaconda3/bin/conda shell.bash hook)"
source init.sh
```

Confirm that `init.sh` reports:

```text
python >= 3.12.0 found at <python>
```

### RISC-V Assembler Rejects `zfh`

Symptom:

```text
Fatal error: -march=rv32imafdv_zfh: unsupported ISA subset `z'
```

Cause: the downloaded `third_party/toolchain/install/bin` compiler is missing
from `PATH`, often because `third_party/toolchain` exists but is incomplete.

Fix:

```bash
make -B third_party/toolchain
source init.sh
make sw
```

### GitHub Downloads Fail

Symptom:

```text
Could not resolve host: github.com
wget: unable to resolve host address 'github.com'
```

Fix: rerun the same command with network access enabled in Codex. The affected
steps are usually:

```bash
git clone --branch codex_velocity https://github.com/husterZC/gvsoc.git
source init.sh
make -B third_party/toolchain
```

## Pushing From Codex 🔐

Use SSH for pushes. HTTPS may try to open an interactive GitHub askpass prompt,
which does not work reliably in the Codex sandbox.

Set the remote to SSH:

```bash
git remote set-url origin git@github.com:husterZC/gvsoc.git
```

Check the remote:

```bash
git remote -v
```

Expected push URL:

```text
origin  git@github.com:husterZC/gvsoc.git (push)
```

Commit and push the current branch:

```bash
git add README.md
git commit -m "Document Codex simulator setup"
GIT_SSH_COMMAND="ssh -o BatchMode=yes" git push origin codex_velocity
```

`BatchMode=yes` makes SSH fail cleanly instead of hanging on an interactive
passphrase or password prompt. The sandbox may print this harmless warning:

```text
X11 forwarding request failed on channel 0
```

## Clean Rebuild Recipes 🧹

Rebuild only the software:

```bash
eval "$(/usr/local/anaconda3/bin/conda shell.bash hook)"
export CCACHE_DIR="$PWD/.ccache"
source init.sh
make clean_sw
make sw run
```

Rebuild hardware and software:

```bash
eval "$(/usr/local/anaconda3/bin/conda shell.bash hook)"
export CCACHE_DIR="$PWD/.ccache"
source init.sh
make hw sw run
```

Force toolchain download again:

```bash
make -B third_party/toolchain
```

## Useful Paths 🗺️

- `velocity/velocity.mk`: Velocity `hw`, `sw`, `run`, and `rund` targets.
- `velocity/hw/velocity_arch.py`: default Velocity architecture config.
- `velocity/sw/`: runtime and example software.
- `sw_build/velocity.elf`: software binary passed to GVSOC.
- `sw_build/velocity.dump`: generated disassembly.
- `install/bin/gvsoc`: installed simulator launcher.
- `.ccache/`: workspace-local compiler cache for sandbox runs.

## Verified Codex Command ✅

The following command sequence was verified in the Codex sandbox:

```bash
eval "$(/usr/local/anaconda3/bin/conda shell.bash hook)"
export CCACHE_DIR="$PWD/.ccache"
source init.sh
make -B third_party/toolchain
make hw sw run
```

Final observed result:

```text
[SystemInfo]: num_cluster = 128
[====================================================================================================] 100%
```
