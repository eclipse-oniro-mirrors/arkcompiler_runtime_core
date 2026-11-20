/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Hotreload smoke test, version 1, incremental-patch profile (patch_partial carries only
 * Greeter: the applied branch expects the omitted classes to keep their old bodies).
 *
 * The file basename is the family stem: es2panda derives the module name from the source
 * basename, so every version of one family MUST share it, which is what makes the abc files
 * comparable.
 *
 * argv: <targetPath> <patchPath>
 */

class Greeter {
    public n: int = 0;

    public bump(): int {
        this.n = this.n + 1;
        return this.n;
    }

    // the patch changes the returned literal -> proves a method body was really swapped
    public tag(): string {
        return 'V1';
    }
}

class Calc {
    public static base: int = 100;

    // the patch changes the arithmetic -> proves static dispatch also goes to the new body
    public static calc(): int {
        return 1;
    }
}

/*
 * `Lazy` is reachable ONLY through `LazyHolder.get()`, i.e. two levels away from `main`.
 *
 * That distance matters: with the default `--verification-mode=ahead-of-time`, verifying a class
 * loads the classes its own methods reference, but only ONE level deep. So `LazyHolder` is already
 * loaded when the reload happens (and is swapped like any other class), while `Lazy` is not loaded
 * yet. Loading it afterwards therefore goes through the lazy path
 * (`EtsClassLinkerContext::EnumeratePandaFilesImpl` -> `EtsAbcFile::GetPandaFile()`), which only
 * yields the patch if the transaction published the new file into the `AbcRuntimeLinker` slot.
 */
class LazyHolder {
    public get(): string {
        return new Lazy().tag();
    }
}

class Lazy {
    public tag(): string {
        return 'L1';
    }
}

function doReload(target: string, patch: string): int {
    return HotReloadBridge.hotReloadNative(target, patch);
}

/*
 * The native side is bound by the smoke driver (tests/ani/hotreload_smoke): bare paths,
 * the target resolved across the live panda files.
 *
 * The assertions are split by the reload's outcome: applied (rc 0) proves the new bodies through
 * instance dispatch, static dispatch and the lazy load; refused proves the transaction modified
 * nothing. Which of the two the case expects is pinned by the driver (HR_EXPECT_RC).
 */
class HotReloadBridge {
    public static native hotReloadNative(target: string, patch: string): int;

    public run(a0: string, a1: string, a2: string): int {
    let g = new Greeter();
    g.bump();
    g.bump();
    // mutate a static field so that "state survives" is not just the cctor default
    Calc.base = 777;

    arktest.assertEQ(g.tag(), 'V1', 'before: instance dispatch');
    arktest.assertEQ(g.n, 2, 'before: instance state');
    arktest.assertEQ(Calc.calc(), 1, 'before: static dispatch');
    arktest.assertEQ(Calc.base, 777, 'before: static field');

    let rc = doReload(a0, a1);

    // the transaction's own guarantees, whichever way it went
    arktest.assertEQ(g.n, 2, 'instance state preserved');
    arktest.assertEQ(Calc.base, 777, 'static field preserved');

    if (rc == 0) {
        arktest.assertEQ(g.tag(), 'V2', 'applied: method body swapped');
        arktest.assertEQ(Calc.calc(), 1, 'applied: omitted class keeps its old body');
        // first use of `LazyHolder.get()` in the whole program, so `Lazy` is loaded only now
        arktest.assertEQ(new LazyHolder().get(), 'L1', 'applied: omitted class still loads from the original');
    } else {
        arktest.assertEQ(g.tag(), 'V1', 'refused: old body still active');
        arktest.assertEQ(Calc.calc(), 1, 'refused: static dispatch untouched');
        arktest.assertEQ(new LazyHolder().get(), 'L1', 'refused: lazy load from the original');
    }
    return 0;
}
}
