# AOT PGO

Ahead-of-time compilation in the current tree can consume runtime profile data from `.ap` files.

The current source of truth for the data model and loader/saver code is:

- `runtime/jit/profiling_data.h`
- `runtime/jit/profiling_loader.h`
- `runtime/jit/profiling_saver.cpp`
- `runtime/jit/libprofile/aot_profiling_data.h`
- `runtime/jit/libprofile/pgo_file_builder.cpp`
- `compiler/tools/paoc/paoc.cpp`

Use this document as the high-level map rather than as a binary-format specification dump. For the current `.ap`
on-disk layout, use `../../docs/bytecode_profiling.md` as the overview and `../../runtime/jit/libprofile/` as the
format owner.

## What Is Collected

Current profile data includes:

- inline caches for virtual call sites
- branch taken / not-taken counters
- throw counters

Relevant current runtime structures:

- `CallSiteInlineCache`
- `BranchData`
- `ThrowData`
- `ProfilingData`

Inline caches currently retain up to four observed receiver classes per call site and mark the site megamorphic once
the cache overflows.

## How Profiles Are Saved

The runtime can save `.ap` profile data to the path from `--profile-output`.

Important current options:

- `--profiler-enabled=true`
- `--profilesaver-enabled=true`
- `--incremental-profilesaver-enabled=true`
- `--profile-output=<file.ap>`
- `--compiler-profiling-threshold=<n>`
- `--profile-branches=true` when detailed branch data is needed

Current behavior:

- runtime profiling starts once methods cross the profiling threshold
- shutdown saving is performed from `Runtime::Destroy()` when profiling and profile saving are enabled
- incremental saving can also run through the profile saver worker
- current saving logic can split profiled panda files by their current profile path snapshot; it is no longer accurate to
  describe this only as "boot and initial app context" output

## How `ark_aot` Uses Profiles

`ark_aot` consumes AOT profiles through:

```bash
--paoc-use-profile:path=<profile.ap>[,force]
```

When reusing a profile, pass the application panda files to compile through `--paoc-panda-files`. `ark_aot` loads
those files before the profile is checked and attached, so they do not need to be duplicated in `--panda-files` only for
AOT-PGO profile attachment. Reserve `--panda-files` for additional runtime-context files that are not compilation
inputs.

Current loading flow in `compiler/tools/paoc/paoc.cpp`:

1. load runtime-context files from `--panda-files`
2. load compilation inputs from `--paoc-panda-files`
3. load `.ap`
4. validate class context against the current runtime/AOT context
5. attach per-method profiling data back to loaded methods
6. let optimization passes query that data through the runtime interface

If the class context does not match, the profile is rejected.

## Compilation Policies

Profile input and compilation policy are separate options. Loading a profile alone preserves the existing selection
behavior. Explicitly request partial compilation with:

```bash
./out/bin/ark_aot \
  --paoc-panda-files=app.abc \
  --paoc-output=app.an \
  --paoc-use-profile:path=workload.ap \
  --paoc-compilation-policy=profile-guided
```

Use the same runtime and boot-file context as the profiling run, as shown in the workflow below.

```text
Profile input                         Compilation policy
-------------                         ------------------
path=workload.ap                       default / profile-guided
       |                                         |
       v                                         v
Validate and attach data              Choose candidates and inline budget
       |                                         |
       +------------------+----------------------+
                          |
                          v
                 Compile with available profile data
```

`default` is the default value and preserves the existing selection and inline budgets. `profile-guided` selects
methods with successfully attached profile data, supplements them with allowlisted methods, and enables the
profile-based root inline budget. Neither policy bypasses existing compilation eligibility or time limits.

Let `U` be the input methods, `W` the allowlist, `D` the denylist and `P` the methods with successfully attached
profile data. The current CLI uses one list file in either allowlist or denylist mode, not both at once.

| List configuration | `default` (with or without profile input) | `profile-guided` |
| --- | --- | --- |
| No list | `U` | `P` |
| Allowlist | `W` | `W union P` |
| Denylist | `U - D` | `P - D` |

Regex and range restrictions apply to every candidate set in this table. The following is a semantic diagram;
the implementation observes range boundaries before applying other method filters.

```text
Whitelist:           A     C
Profile:                B  C
                       \ /
Candidates:          A  B  C
                          |
                          v
                Regex and range restrictions
                          |
                          v
                 Independent compilation roots
```

An allowlisted method does not need profile data. A profiled method need not be allowlisted. Methods excluded from
independent compilation may still be inlined into selected callers, and their profile data remains available for
applicable optimizations. An empty allowlist selects no methods under `default`, but still allows `P` under
`profile-guided`.

### Profile Requirements and Failure Behavior

| Policy / input | Behavior |
| --- | --- |
| `default`, no profile option | Compile using existing selection and optimization behavior |
| `default`, valid profile | Preserve selection and use the available data for existing PGO optimizations |
| `default`, profile load failure, no `force` | Report the failure and retain existing best-effort behavior |
| `default`, profile load failure, `force` | Fail |
| `profile-guided`, omitted or empty profile path | Reject arguments before creating the runtime |
| `profile-guided`, missing, invalid or incompatible profile | Fail, even without `force` or with an allowlist |
| `profile-guided`, valid profile with no selected methods | `P` is empty; only allowlist entries can supplement it |

```text
Missing / invalid profile             Valid profile, P is empty
-------------------------             -------------------------
Cannot determine P                    P = {}
          |                                |
          v                                v
profile-guided fails                  Apply normal set rules
                                      No fallback to all methods
```

An empty selected set does not guarantee a successful empty artifact: existing no-compilation checks still apply.
The effective policy is reported in the compiler INFO log after successful profile loading, or after an allowed
best-effort load failure under `default`.

### Inline Budget

Under `profile-guided`, every independent compilation root uses the maximum of `compiler-inlining-max-insts` and
`compiler-inlining-max-insts-hot`, whether it was selected from profile data or from an allowlist. Nested inlining
retains the outermost root's budget. Under `default`, the additional profile-based budget is inactive even if a
profile is loaded.

```text
profile-guided (any root)              default policy
        |                                    |
        v                                    v
max(regular limit, profile limit)        regular limit
        |                                    |
        +--> inherited by nested inlining <--+
```

Profile membership is not a method invocation count or a guarantee that every executed method is represented.
Methods without relevant profiling points may have no entry. The `hot` option name refers to the profile-based
budget here, not to an independently measured method hotness threshold.

The earlier PR sub-option `--paoc-use-profile:select-methods` is replaced by
`--paoc-compilation-policy=profile-guided`; it is not retained as an alias. Existing commands that only load a profile,
or use lists, regexes and ranges, retain their default behavior.

## What The Compiler Uses Today

Current profile-guided decisions include:

- inline-cache driven monomorphic or polymorphic inlining
- branch-counter driven block ordering and if-conversion heuristics
- throw-counter driven try/catch and throw-heavy path heuristics

Relevant current code paths:

- `compiler/optimizer/optimizations/inlining.cpp`
- `compiler/optimizer/analysis/linear_order.cpp`
- `compiler/optimizer/analysis/hotness_propagation.h`
- `compiler/optimizer/optimizations/if_conversion.cpp`
- `compiler/optimizer/optimizations/try_catch_resolving.cpp`

## Current Limitations

AOT PGO has important limits:

- AOT mode does not use CHA-based guarded devirtualization in the same way JIT does; `Inlining::ResolveTarget`
  explicitly restricts the CHA path to non-AOT graphs
- external inline-cache driven inlining still follows the normal AOT external-inlining rules; it is gated by
  `--compiler-inline-external-methods-aot` plus the usual AOT availability constraints
- branch data is collected by runtime profiling before `ark_aot` consumes the `.ap`; high-frequency branch collection
  can add startup cost on loop-heavy workloads. See
  [branch-profiling-cost.md](../../runtime/jit/docs/knowledge/branch-profiling-cost.md)
- profile data is useful, but it does not remove the usual AOT constraints around unavailable classes, external methods,
  or mismatched load context

## Minimal Current Workflow

1. Run the workload with profiling enabled and profile saving turned on.
2. Inspect the saved `.ap` with `ark_aptool dump`.
3. Re-run `ark_aot` with `--paoc-use-profile:path=<profile.ap>` and matching `--paoc-panda-files` inputs.
4. Use `--compiler-regex`, `--compiler-dump`, and `--compiler-disasm-dump:single-file` to verify which methods and
   optimizations changed.

Example shape:

```bash
./out/bin/ark \
  --load-runtimes=ets \
  --boot-panda-files=./out/plugins/ets/etsstdlib.abc \
  --profile-output=workload.ap \
  --profilesaver-enabled=true \
  app.abc app.ETSGLOBAL::main

./out/bin/ark_aptool dump --ap-path workload.ap --output workload.yaml --abc-path app.abc

./out/bin/ark_aot \
  --load-runtimes=ets \
  --boot-panda-files=./out/plugins/ets/etsstdlib.abc \
  --paoc-panda-files=app.abc \
  --paoc-output=app.an \
  --paoc-use-profile:path=workload.ap
```

## Related Documents

- `../../docs/aptool.md`
- `../../docs/bytecode_profiling.md`
- `../../runtime/jit/libprofile/`
- `performance_workflows.md`
- `../../runtime/jit/AGENTS.md`
