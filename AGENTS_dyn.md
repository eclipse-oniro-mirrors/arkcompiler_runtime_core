# ArkTS-Dyn runtime guidance

> When an agent adds or changes code structure, features, or architectural boundaries in this scope, it MUST update this guide in the same change.

Read the [repository guide](AGENTS.md) for shared invariants, approval boundaries and the full directory index.
Use this page for code outside `static_core/`. For static VM work, start at the [static runtime guide](static_core/AGENTS.md).

## 1. Route by task, directory or term

| Task or search term | Owning path and first read | What to check |
|---|---|---|
| Assembly, pandaasm | [assembler/AGENTS.md](assembler/AGENTS.md) | Preserve operand encoding and round-trip through the disassembler. |
| ABC decoding, ark_disasm | [disassembler/AGENTS.md](disassembler/AGENTS.md) | Use the reader for the dynamic file format. |
| ABC header, index or loader | [libpandafile/AGENTS.md](libpandafile/AGENTS.md) | Check offsets and compatibility with existing files. |
| Bytecode optimization | [bytecode_optimizer/AGENTS.md](bytecode_optimizer/AGENTS.md) | Verify transformed methods and exception ranges. |
| JIT, AOT or optimizer pass | [compiler/optimizer](compiler/optimizer) and [compiler/tests](compiler/tests) | Inspect the affected pass and its compiler tests. |
| Opcode, immediate or instruction format | [isa/AGENTS.md](isa/AGENTS.md) | Update the instruction definition before regenerating consumers. |
| Bytecode verification | [verifier/AGENTS.md](verifier/AGENTS.md) | Exercise both accepted and rejected inputs. |
| Base library, allocation or synchronization | [libpandabase/AGENTS.md](libpandabase/AGENTS.md) | Check ownership and platform dependencies. |
| Platform interface or XGC | [arkplatform/AGENTS.md](arkplatform/AGENTS.md) | Preserve the VM boundary and lifetimes. |
| Archive or HAP loading | [libziparchive/AGENTS.md](libziparchive/AGENTS.md) | Validate lengths before reading entries. |
| Editing ABC through a public API | [libabckit/AGENTS.md](libabckit/AGENTS.md) | Check public API and format compatibility. |

## 2. Constraints and validation

Use the dynamic instruction and file definitions when changing a bytecode producer or consumer.
Consult the root guide before changing opcode layouts, object layouts, thread semantics or external interfaces.

For an assembler change, build the component and its host unit-test target from the full OpenHarmony checkout root:

```sh
./build.sh --product-name rk3568 --build-target arkcompiler/runtime_core/assembler:libarkassembler
./build.sh --product-name rk3568 --build-target arkcompiler/runtime_core/assembler/tests:host_unittest
```

For other components, use the owning guide's targets. Record build and test execution separately.
Check the [repository completion criteria](AGENTS.md#43-done-definition) before reporting results.
