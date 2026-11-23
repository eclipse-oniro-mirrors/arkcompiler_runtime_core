# Ark Bytecode Explorer

[简体中文](README_zh.md)

Ark Bytecode Explorer is an **offline, browser-based viewer for Ark bytecode (`.abc`) files**. One local binary opens an interactive workbench in your browser: browse the semantic structure of the file, read disassembled methods with per-instruction detail, walk interactive control-flow graphs, inspect metadata cross-references and raw hex bytes — with zero setup and no data leaving your machine.

## Highlights

- **Single binary, fully offline** — the web UI is embedded in the executable and served by a loopback-only HTTP server bound to `127.0.0.1`; no external network access.
- **Six coordinated views** — structure tree, disassembly with an instruction inspector, interactive CFG, metadata browser with cross-references, hex viewer and scope statistics; selections stay in sync across views.
- **Open and switch documents in the page** — start without a file argument into a landing page, open ABC files through a native file dialog, reopen recent documents from a server-side list, and switch documents in place (a failed switch keeps the previous document open).
- **Read-only and defensive** — the tool never writes ABC files; malformed entity data is reported as diagnostics instead of terminating the process.

## How to download and build

### Download:

```sh
repo init -u https://gitee.com/ark-standalone-build/manifest.git -b master
repo sync -c -j8
repo forall -c 'git lfs pull'
./prebuilts_download.sh
```

### Build:

```sh
# Linux Version (build on Linux)
./ark.py x64.release ark_bytecode_explorer --gn-args="is_standard_system=true enable_cmc_gc=false"
# Windows Version (build on Linux)
./ark.py mingw_x86_64.release ark_bytecode_explorer --gn-args="is_standard_system=true enable_cmc_gc=false"
# macOS Version (build on macOS)
./ark.py mac_arm64.release arkcompiler/runtime_core/ark_bytecode_explorer --gn-args="is_standard_system=true enable_cmc_gc=false"
```

The binary is placed at `out/<target>/arkcompiler/runtime_core/ark_bytecode_explorer` (`.exe` on Windows).

## How to run

```sh
BEX=out/x64.release/arkcompiler/runtime_core/ark_bytecode_explorer

$BEX file.abc    # open the workbench for file.abc (a browser tab opens automatically)
$BEX             # start empty: open files from the landing page and switch in place
$BEX --no-open   # print the local URL without opening a browser
```

Options:

- `-h`, `--help` — show help without opening the ABC
- `--version` — show version without opening the ABC
- `--no-open` — print the local URL without opening a browser
- `--dump-structure` — headless mode: print the semantic structure tree and exit
- `--show-internals` — include compiler-generated and ABC-internal nodes in a structure dump

## Tests

```sh
# C++ unit tests
ninja -C out/x64.release ark_bytecode_explorer_tests
./out/x64.release/tests/unittest/runtime_core/runtime_core/ark_bytecode_explorer_tests

# Frontend unit tests (plain node, no browser needed)
cd arkcompiler/runtime_core/bytecode_explorer/web/frontend
node --test tests/*.test.mjs
```
