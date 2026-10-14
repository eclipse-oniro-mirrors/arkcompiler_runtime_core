# memmem

Memory benchmark runner for OpenHarmony and Andr0id. It collects memory evidence from configured apps and creates CSV and SVG views from that evidence.

## Authors

- Nazarov Konstantin
- Petrov Igor

## Prerequisites

- Python 3.11
- HDC available on the host for OpenHarmony, or ABD available on the host for Andr0id
- A connected rooted OpenHarmony or Andr0id device

Create `.env`:

```bash
HDC_PATH=/path/to/hdc
ABD_PATH=/path/to/abd
```

## Setup

```bash
make venv
source .venv/bin/activate
```

## Commands

```bash
python execute_flow.py --flow flow.json --backend hdc
python create_view.py memmem-out-.../iteration_0/evidence --out-dir views
python run.py --flow flow.json --backend hdc --repeats 3
```

`execute_flow.py` collects evidence. `create_view.py` creates a view from one or more existing evidence directories and does not require a device or `.env` file. `run.py` orchestrates `execute_flow.py` and `create_view.py` into a single CLI entrypoint: it collects evidence for every iteration, then creates a view for each iteration and a combined view.

`execute_flow.py` and `run.py` accept:

- `--flow PATH` (required): canonical or macro-bearing flow JSON.
- `--backend {hdc,abd}` (default: `hdc`): only the selected backend executable is required in `.env`.
- `--out-dir PATH`: output parent; defaults to a timestamped `memmem-out-*` directory.
- `--repeats N` (default: `1`): number of evidence collections.
- `--reboot` or `--no-reboot` (default: no reboot).
- `--device-log` or `--no-device-log` (default: device logging enabled).

`create_view.py` and `run.py` accept `--smaps-filter REGEX` (default: `.*`) to select smaps mappings by normalized tag. `create_view.py` requires one or more evidence directories and `--out-dir PATH` for the new view.

All three commands accept:

- `--memmem-log-level {info,warn,err}` (default: `err`).
- `--memmem-log-file PATH`.

Path arguments (`--flow`, `--out-dir`, `--memmem-log-file`, `HDC_PATH`, and `ABD_PATH`) are resolved against the directory the CLI was invoked from unless the path is absolute.

## Record (record_hdc.py)

```bash
python record_hdc.py --timeout 30
```

`record_hdc.py` records UI input from an OpenHarmony device with `uitest uiRecord` and writes `flow-YYYYMMDD_HHMMSS_microseconds.json`. Andr0id recording is not supported.

- optional `--timeout`: recording duration in seconds; if omitted, recording continues until you type `y` and press Enter
- script will prompt user for input when ready
- generated flows contain UI input commands only; add waits, snapshots, and screenshots manually
- generated `AppFlow` entries preserve recorded bundle/ability identity order and always use `terminate: false`
- if a recorder event has empty bundle or ability metadata, the generated field remains `""` and the script prints a warning; edit those fields manually

## Compare result directories (`compare.py`)

Compare one concrete baseline view with one or more concrete change views without rerunning benchmarks:

```bash
python compare.py \
  --base baseline/view/view-20261027_120000_000000 \
  --change change-a/view/view-20261027_120000_000000 \
  --change change-b/view/view-20261027_120000_000000 \
  --out-dir memmem-comparison
```

Inputs must be validated concrete views with identical filter text/effective flags and ordered breakdown declarations. This is a caller assertion that matching labels represent comparable apps and flows; comparison does not reopen source evidence, so generated views remain comparable after their evidence has moved or been removed. The new `--out-dir` must not exist. The output contains comparative `summary.csv` and `breakdowns/<snapshot_label>/<app_label>.csv` files with one base value and one value/percentage pair for every `--change` argument in command-line order. Breakdown rows include the union of tags from all inputs; unavailable values and percentage deltas against a zero baseline are empty. Invalid or incompatible inputs fail before the comparison destination is created; generation failures retain partial newly created output.

## Flow schema

Flow JSON has two forms. The **canonical form** is what execution uses and what CLI provenance stores; it is also the output of preprocessing. The **unprocessed form** additionally accepts macros. The execution CLIs accept either form and canonicalize it before collection.

### Canonical form

```json
{
  "$desc": "Measures memory after launch and one tap.",
  "flow": [
    {
      "label": "APP_A",
      "bundle": "com.example.app",
      "ability": "EntryAbility",
      "terminate": true,
      "commands": [
        { "action": "wait", "payload": 2.1 },
        { "action": "tap", "payload": { "x_pct": 50, "y_pct": 80 } },
        { "action": "screenshot", "payload": "after_tap" },
        { "action": "snapshot", "payload": "after_tap" },
        { "action": "key", "payload": { "key": "Home" } }
      ]
    }
  ]
}
```

`$desc` is optional top-level metadata field and does not affect execution.

When `terminate` is `true`, the runner terminates that bundle after commands.

App labels may contain only letters, digits, `_`, and `-`. App labels must be unique among app labels.

### Multi-touch input

`multi_touch_input` replays two or three concurrent finger paths on an OpenHarmony HDC device. It is not supported by the ABD backend. `finger_list` contains normalized start and end coordinates for each finger, and `duration_ms` is an integer from `1` through `15000`.

```json
{
  "flow": [
    {
      "label": "zoom",
      "bundle": "com.example.app",
      "ability": "EntryAbility",
      "terminate": false,
      "commands": [
        {
          "action": "multi_touch_input",
          "payload": {
            "finger_list": [
              { "x1_pct": 35, "y1_pct": 50, "x2_pct": 20, "y2_pct": 50 },
              { "x1_pct": 65, "y1_pct": 50, "x2_pct": 80, "y2_pct": 50 }
            ],
            "duration_ms": 300
          }
        }
      ]
    }
  ]
}
```

### Repeat macros

Use repeat macros to avoid copying repeated command patterns.

```json
{
  "flow": [
    {
      "label": "APP_A",
      "bundle": "com.example.app",
      "ability": "EntryAbility",
      "terminate": true,
      "commands": [
        {
          "macro": "repeat",
          "payload": {
            "iter_var": "i",
            "n_iter": 3,
            "commands": [
              { "action": "wait", "payload": 2 },
              { "action": "snapshot", "payload": "after_wait_{i}" }
            ]
          }
        }
      ]
    }
  ]
}
```

`iter_var` names the replacement variable and must match `^[A-Za-z_][A-Za-z0-9_]*$`. `n_iter` is the non-negative iteration count. During expansion, every `{<iter_var>}` occurrence inside string typed payloads is replaced with the iteration number in the range `[0, n_iter-1]`. The macro is replaced in place by the expanded commands in the same order.

Labels are validated only after expansion: `"after_tap_{i}"` expands to `"after_tap_0"`, `"after_tap_1"`, ... and each resulting label must satisfy the canonical label rules.

### Programmatic API (lib.py)

Use `lib.py` to build flows and reusable Python command helpers when a JSON flow becomes unwieldy.

```python
import pathlib

import lib


def scroll_samples(prefix: str, count: int, wait_s: float) -> list[lib.Command]:
    commands: list[lib.Command] = []
    for i in range(1, count + 1):
        commands.extend([
            lib.wait(wait_s),
            lib.directional_fling("down", velocity=20000, step_length=10),
            lib.snapshot(f"{prefix}{i}"),
            lib.screenshot(f"{prefix}{i}"),
        ])
    return commands


def zoom_out() -> lib.MultiTouchInputCommand:
    return lib.multi_touch_input([
        {"x1_pct": 35, "y1_pct": 50, "x2_pct": 20, "y2_pct": 50},
        {"x1_pct": 65, "y1_pct": 50, "x2_pct": 80, "y2_pct": 50},
    ], duration_ms=300)


scenario = lib.flow(
    [
        lib.app_flow(
            label="app_label",
            bundle="com.app.bundle",
            ability="Ability",
            terminate=True,
            commands=[
                lib.snapshot("on_startup"),
                zoom_out(),
                *scroll_samples("after_fling", count=10, wait_s=5),
            ],
        )
    ],
    desc="Measures memory across repeated scroll samples.",
)

device = lib.get_hdc_device(lib.get_hdc(pathlib.Path("/path/to/hdc")))
evidence_dir = pathlib.Path("evidence")
view_dir = pathlib.Path("view")

lib.configure_logger("info")
lib.execute_flow(scenario, device, evidence_dir, reboot=False, device_log=True)
lib.create_view([evidence_dir], view_dir)
```

`lib.execute_flow(flow, device, dst, *, reboot=False, device_log=True)` performs one collection and returns the absolute evidence destination. `lib.create_view(dirs, dst, smaps_filter=re.compile(".*"))` creates a view from a nonempty ordered evidence list and returns the absolute view destination. Both APIs raise on failure and require a new destination.

Python users can also author macros programmatically. `lib.unprocessed_flow`, `lib.unprocessed_app_flow`, `lib.unprocessed_snapshot`, `lib.unprocessed_screenshot`, and `lib.repeat(n_iter, iter_var, commands)` build an `UnprocessedFlow`, and `lib.preprocess_flow(flow)` expands it into a canonical `lib.Flow` for `lib.execute_flow`. This is the same transformation execution CLIs apply to macro-bearing flow JSON:

```python
scenario = lib.unprocessed_flow([
    lib.unprocessed_app_flow(
        label="app_label",
        bundle="com.app.bundle",
        ability="Ability",
        terminate=True,
        commands=[
            lib.unprocessed_snapshot("on_startup"),
            lib.repeat(
                n_iter=10,
                iter_var="i",
                commands=[
                    lib.wait(5),
                    lib.directional_fling("down", velocity=20000, step_length=10),
                    lib.unprocessed_snapshot("after_fling_{i}"),
                    lib.unprocessed_screenshot("after_fling_{i}"),
                ],
            ),
        ],
    )
], desc="Measures memory across repeated scroll samples.")

canonical = lib.preprocess_flow(scenario)
lib.execute_flow(canonical, device, evidence_dir, reboot=False, device_log=True)
```

For programmatic flows, prefer ordinary Python composition over `lib.unprocessed_*` APIs unless macro expansion is needed.

Configure optional application logging with `lib.configure_logger(level, log_file=None)`, access it with `lib.get_logger()`, and reset it with `lib.reset_logger()`. `level` is `"info"`, `"warn"`, or `"err"`.

## Commands

### Existing commands

```json
{ "action": "wait", "payload": 1.0 }
{ "action": "snapshot", "payload": "label" }
{ "action": "screenshot", "payload": "label" }
{ "action": "key", "payload": { "key": "Home" } }
```

`wait` blocks execution for the given amount of seconds.

`snapshot` captures smaps for every active app. Snapshot labels may contain only letters, digits, `_`, and `-`, and must be unique.

`screenshot` captures the current screen as PNG evidence. Screenshot labels may contain only letters, digits, `_`, and `-`, and must be unique.

`key` accepts only `Home`, `Back`, or `Power` and sends corresponding input to the device.

### UI commands

Coordinates are integer percentages from `0` to `100`:

```json
{ "action": "tap", "payload": { "x_pct": 50, "y_pct": 50 } }
{ "action": "double_tap", "payload": { "x_pct": 50, "y_pct": 50 } }
{ "action": "long_tap", "payload": { "x_pct": 50, "y_pct": 50 } }
{ "action": "input_text", "payload": { "x_pct": 50, "y_pct": 40, "text": "hello" } }
{ "action": "text", "payload": "hello" }
```

Swipe-like commands require `velocity` in `200..40000`:

```json
{
  "action": "swipe",
  "payload": {
    "x1_pct": 50,
    "y1_pct": 80,
    "x2_pct": 50,
    "y2_pct": 20,
    "velocity": 800
  }
}
```

`drag` has the same payload as `swipe`.

`fling` also requires positive `step_length`:

```json
{
  "action": "fling",
  "payload": {
    "x1_pct": 50,
    "y1_pct": 80,
    "x2_pct": 50,
    "y2_pct": 20,
    "velocity": 1200,
    "step_length": 20
  }
}
```

Directional fling:

```json
{
  "action": "directional_fling",
  "payload": { "direction": "up", "velocity": 1200, "step_length": 20 }
}
```

`direction` is one of `left`, `right`, `up`, `down`.

## Andr0id Backend Notes

With `--backend abd`, `bundle` and `ability` are interpreted as the Andr0id package and activity, for example `com.example.app` and `.MainActivity`. Andr0id devices must be rooted so the shell can capture app smaps. Device logs are collected with `logcat` at `logs/log.log`. Andr0id `input` does not expose all UITest gesture semantics, so double taps, long taps, drags, flings, velocity, and step length are replayed on a best-effort basis.

## Output

### Evidence

Each execution creates an exact evidence directory:

```text
evidence/
  apps.json
  snapshots/
    metadata.json
    <snapshot_label>/<app_label>.smaps
  screenshots/
    metadata.json
    <screenshot_label>.png
  logs/log.log                  # only when device logging is enabled
```

`apps.json` records launched applications. Snapshot and screenshot metadata describe the evidence available to view creation.

`run.py` uses this container layout, including when `--repeats 1`:

```text
<out-dir>/
  flow.json
  iteration_0/evidence/
  iteration_0/view/view-<timestamp>/
  iteration_1/evidence/
  iteration_1/view/view-<timestamp>/
  view/view-<timestamp>/
```

### Views

Every view has one aggregate layout, whether it uses one or many evidence sources:

```text
<view>/
  metadata.json
  summary.csv
  outliers.csv
  breakdowns/
    metadata.json
    <snapshot_label>/<app_label>.csv
  plots/<app_label>/<app_label>-<metric>.svg
```

`summary.csv` and breakdown CSVs contain aggregate statistics: mean, geometric mean, median, standard deviation, minimum, and maximum. `outliers.csv` lists evidence sources excluded from aggregation.

## Testing

```bash
source .venv/bin/activate
make tests_full
```

Useful individual checks:

```bash
make test
make mypy
make pylint
```

## Update requirements

```bash
pip freeze > requirements.txt
```
