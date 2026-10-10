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
 * Chain generation 3: an incremental patch carrying the module globals and `Counter` (tag 'V3').
 * `Late` is not loaded when this patch is built and is absent on purpose, see chain_v1/t.ets.
 */

class Counter {
    public n: int = 0;

    public bump(): int {
        this.n = this.n + 1;
        return this.n;
    }

    public tag(): string {
        return 'V3';
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
    return 0;
}
}
