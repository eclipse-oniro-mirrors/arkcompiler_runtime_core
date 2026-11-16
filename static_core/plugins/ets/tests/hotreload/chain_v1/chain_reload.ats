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
 * Chain of INCREMENTAL reloads, generation 1 (running when the first reload happens).
 *
 * Both patches carry ONLY `Counter` (+ the module globals); `LateHolder` and `Late` are in
 * no patch. `Late` sits TWO levels away from `main` on purpose: the ahead-of-time
 * verification of `main` loads what `main` itself references (`LateHolder`) but only one
 * level deep, so `Late` is still unloaded when the reloads happen, and its first use after
 * them goes through the real lookup path. That lookup must walk the publication chain
 * [patch3, patch2, original] and fall through both patches to the original file.
 *
 * argv: <targetPath> <patch2Path> <patch3Path>
 */

class Counter {
    public n: int = 0;

    public bump(): int {
        this.n = this.n + 1;
        return this.n;
    }

    public tag(): string {
        return 'V1';
    }
}

class LateHolder {
    public get(): string {
        return new Late().tag();
    }
}

class Late {
    public tag(): string {
        return 'LATE1';
    }
}

/*
 * The native side is bound by the smoke driver (tests/ani/hotreload_smoke): bare paths,
 * the target resolved across the live panda files.
 */
class HotReloadBridge {
    public static native hotReloadNative(target: string, patch: string): int;

    public run(a0: string, a1: string, a2: string): int {
    let c = new Counter();
    c.bump();
    arktest.assertEQ(c.tag(), 'V1', 'gen0: body');
    arktest.assertEQ(c.n, 1, 'gen0: instance state');
    let rc1 = HotReloadBridge.hotReloadNative(a0, a1);
    arktest.assertEQ(rc1, 0, 'first reload rc');
    c.bump();
    arktest.assertEQ(c.tag(), 'V2', 'gen1: body');
    arktest.assertEQ(c.n, 2, 'gen1: instance state survived');
    let rc2 = HotReloadBridge.hotReloadNative(a0, a2);
    arktest.assertEQ(rc2, 0, 'second reload rc');
    c.bump();
    arktest.assertEQ(c.tag(), 'V3', 'gen2: body');
    arktest.assertEQ(c.n, 3, 'gen2: instance state survived');
    // `Late` is in no patch and unused until now: it must fall through BOTH patches to the
    // retained original file
    arktest.assertEQ(new LateHolder().get(), 'LATE1', 'late class resolves through the chain');
    return 0;
}
}
