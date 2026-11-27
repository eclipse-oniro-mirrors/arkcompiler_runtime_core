# bytecode_explorer (Ark Bytecode Explorer) — Agent Guide

> When an agent adds or changes code structure, features, or architectural boundaries in this scope, it MUST update this guide in the same change.

This document is the **full working guide** for **`arkcompiler/runtime_core/bytecode_explorer`**.

---

## What is bytecode_explorer?

**Ark Bytecode Explorer** is an **offline, read-only, browser-based viewer for Ark bytecode (`.abc`) files**. A single local binary serves an embedded web UI from a loopback-only HTTP server (bound to `127.0.0.1`), hosting a workbench of six coordinated views:

- **Structure tree** — semantic entities (File / Module / Script / Class / Function / Field / …) with live filtering
- **Disassembly** — method instructions with a per-instruction inspector (operands, resolved IDs, immediates)
- **CFG** — interactive control-flow graphs with real basic blocks and layout
- **Metadata** — catalog browsing with cross-references
- **Hex** — raw file bytes linked to entity ranges
- **Statistics** — scope-level code bytes / instruction totals with an honest completeness model

Documents can be **opened and switched in the page**: an empty landing page with a native file dialog, a server-side recent-files list, and in-place switching where a failed switch keeps the previous document open. Appearance offers three themes (dark / light / high-contrast) with server-side persistence.

The tool is **strictly read-only** toward ABC files and **strictly local** toward the network.

---

## High-level architecture and data flow

1. **Entry** (`app/`): `command_line.cpp` parses arguments; `document_loader.cpp` loads the initial document (or starts empty).
2. **Core pipeline** (`core/`): `reader/physical_reader` provides bounded raw file access; `reader/structure_reader` extracts entities from the panda file; `index/structure_index` classifies them into the semantic model; `method_index`, `metadata/` build lookup services.
3. **On-demand analysis**: function disassembly, CFG build/analyze/layout, hex windows, metadata pages and scope statistics are computed lazily per entity, with cancellation support and budget limits.
4. **Web layer** (`web/`): `host/loopback_host` serves the embedded assets and the JSON API; `request_router` dispatches routes; `api/web_api` (+ `statistics_api`, `recent_files`, `preferences`) serialize results; `web_session` holds session/token state. The frontend (plain ES modules, no framework) is **embedded into the binary at build time** via `web/tools/embed_web_assets.py`.
5. **Platform** (`platform/`): `browser_launcher` opens the local URL; `file_dialog` hosts the native open-file dialog used by the in-page open flow.

**Key takeaway: frontend assets live inside the binary** — every frontend change requires a rebuild before it is visible.

---

## Repository layout

### `app/` — entry and document lifecycle
`command_line.{h,cpp}` (options: `--help`, `--version`, `--no-open`, `--dump-structure`, `--show-internals`); `document_loader.{h,cpp}`.

### `core/` — read-only analysis pipeline
- `reader/` — `physical_reader` (bounded reads), `structure_reader` (panda file → entities)
- `model/` — entity model (`EntityKind` / `EntityRole` / `EntityOrigin`, semantic vs physical entities, backing arrays)
- `index/` — `structure_index` (classification and names), `file_range_locator`, `method_index`
- `metadata/` — catalog, index service, model, reference indexer
- `cfg/` — `cfg_builder`, `cfg_analysis`, `cfg_layout`, `control_flow_graph`
- `statistics/` — `scope_statistics` (background scan with method/byte budgets)
- `function_disassembler` + `format/` — disassembly and structure formatters
- `common/` — `bounded_memory_resource`, `result`, `cancellation`, `task_runner`
- `document/` — `abc_document`

### `web/` — server and frontend
- `host/` — `loopback_host` (127.0.0.1 only), `http_request` (parser with method whitelist), `request_router` (route table), `pick_controller` (in-page open flow)
- `api/` — `web_api` (structure / disassembly / cfg / hex / metadata / document routes), `statistics_api`, `web_session`, `recent_files`, `preferences` (theme persistence)
- `assets/` + `tools/embed_web_assets.py` — asset embedding
- `frontend/` — `src/` (ES modules: `main.js`, per-view modules, `theme.js`, `tooltip.js`, `document-switching.js`, …), `tests/` (node), `index.html`, `styles.css`

### `platform/`
`browser_launcher` (opens the local URL), `file_dialog` (native open dialog).

### `tests/`
C++ unit tests, one file per core area plus `web_http_test`, `web_session_test`, `pick_controller_test`, `preferences_test`, `recent_files_test`; test data under `tests/data/`. Frontend tests: `web/frontend/tests/*.test.mjs`.

---

## Key conventions

- **Checked access**: the component builds with the libpandafile `arkfile_checked_config` — a read-only tool must **report malformed ABC as diagnostics, never fatal-terminate** the process.
- **HTTP method whitelist**: the request parser (`web/host/http_request.cpp`) accepts GET/POST/PUT only. A new method needs both parser and router changes. Route tests that construct `HttpRequest` directly **bypass the parser** — the parser layer is the real gate.
- **Asset double registration**: a new frontend file must be registered in `BUILD.gn` **in the sources array AND as an `--asset` route**. Missing either yields a 404 and "Explorer JavaScript could not be loaded".
- **ELEMENT_IDS**: DOM access in `main.js` goes through the explicit `ELEMENT_IDS` list; new interactive elements must be registered there.
- **Themes**: all colors live in the three CSS token blocks (`:root`, `:root[data-theme='light']`, `:root[data-theme='high-contrast']`) — no JS color values. The C++ `IsKnownTheme` whitelist and the frontend `THEMES` list must stay in sync.
- **Persistence**: `~/.cache/ark_bytecode_explorer/` holds `recent.json` (recent documents) and `preferences.json` (theme); both are strict single-key JSON, and the write side is gated by loopback-host checks.
- **Read-only**: the tool never writes ABC files.

---

## Build and test

From the ark root:

```bash
./ark.py x64.release ark_bytecode_explorer --gn-args="is_standard_system=true enable_cmc_gc=false"
```

After `repo sync` or large baseline jumps, always go through `ark.py` (gn regeneration) instead of bare incremental ninja.

```bash
ninja -C out/x64.release ark_bytecode_explorer_tests
./out/x64.release/tests/unittest/runtime_core/runtime_core/ark_bytecode_explorer_tests
```

Frontend tests (no browser needed; run from `bytecode_explorer/web/frontend`):

```bash
node --test tests/*.test.mjs
```

**Rebuild the binary after frontend changes** (assets are embedded; sanity-check served content via `/assets/*.js`).

---

## How to add a new feature (checklist)

### A) New HTTP API
1. Implement the handler in `web/api/` (or extend `web_api.cpp`).
2. Register the route in `web/host/request_router.cpp`.
3. New HTTP method? Extend the parser whitelist in `http_request.cpp` first.
4. Wire session state in `web_session` if the route needs it.
5. Add tests: `web_http_test` / `web_session_test` / a dedicated api test file.

### B) New frontend module
1. Add `src/<module>.js` and `tests/<module>.test.mjs`.
2. Register in `BUILD.gn`: sources array **and** `--asset` route (both!).
3. Register DOM ids in `ELEMENT_IDS` (`main.js`) if the module touches the DOM.
4. Run `node --test`, then rebuild the binary.

### C) New API data field
1. Serialize in `web_api.cpp` (e.g. the node JSON builder).
2. Consume in the frontend with strict field validation.
3. Update the frontend tests that pin the wire format.

### D) New persisted preference
1. `web/api/preferences.cpp`: whitelist + validation (`IsKnownXxx` style).
2. Frontend: consume and save; keep the C++ whitelist and the frontend list in sync.
3. GET/PUT round-trip tests, including invalid-value → 400.

---

## Critical pitfalls / "do not do this"

- **Do not** add network access beyond the loopback server.
- **Do not** write to ABC files — the tool is read-only by design.
- **Do not** skip `ark.py` after `repo sync` / baseline jumps — bare ninja can fail on regenerated code.
- **Do not** add a frontend file without **both** `BUILD.gn` registrations.
- **Do not** test method handling by constructing `HttpRequest` directly — the parser whitelist is the gate the production path goes through.
- **Do not** duplicate theme colors in JS — CSS tokens only, defined in all three theme blocks.
- **Remember**: statistics budgets (max methods / max code bytes) are correctness semantics, not just performance knobs — the `Limited` counters report incompleteness honestly; keep that contract when touching `scope_statistics`.

---

## Where to look (quick reference)

| Goal | Location |
|------|----------|
| Panels and UX wiring | `web/frontend/src/main.js` + per-view modules (`tree-model`, `cfg-view`, `hex-view`, `metadata-view`, `statistics-view`) |
| Entity model and kinds | `core/model/entity.h`, `core/index/structure_index.cpp` |
| HTTP route table | `web/host/request_router.cpp` |
| Session and token | `web/api/web_session.cpp` |
| In-page open / switch flow | `web/host/pick_controller.cpp`, `platform/file_dialog.cpp`, `web/frontend/src/document-switching.js` |
| Recent files / theme persistence | `web/api/recent_files.cpp`, `web/api/preferences.cpp` |
| Asset embedding | `web/tools/embed_web_assets.py`, `BUILD.gn` |
| Tests | `tests/` (C++), `web/frontend/tests/` (node) |
