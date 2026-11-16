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
 * Patch: `r` retyped from `A` to `B`. `Field::GetTypeId()` says REFERENCE for
 * both, so only the descriptor tells them apart. Carries V2 bodies for the
 * same reason as the sibling case.
 */

import { RuntimeDebug } from 'std/debug';

class A { public who(): string { return 'A'; } }
class B { public who(): string { return 'B'; } }

class Holder {
    public a: int = 1;
    public b: int = 2;
    public r: B = new B();
    public tag(): string { return 'V2'; }
}

/*
 * The native side is bound by the smoke driver (tests/ani/hotreload_smoke): bare paths,
 * the target resolved across the live panda files.
 */
class HotReloadBridge {
    public static native hotReloadNative(target: string, patch: string): int;

    public run(a0: string, a1: string, a2: string): int {
    let h = new Holder();
    h.a = 11;
    h.b = 22;
    arktest.assertEQ(h.a, 11, 'before: field a');
    arktest.assertEQ(h.b, 22, 'before: field b');
    arktest.assertEQ(h.r.who(), 'A', 'before: reference field');
    arktest.assertEQ(h.tag(), 'V1', 'before: method body');
    let rc = HotReloadBridge.hotReloadNative(a0, a1);
    // refused: the transaction modified nothing, the live object keeps its layout and bodies
    arktest.assertEQ(h.a, 11, 'refused: field a unchanged');
    arktest.assertEQ(h.b, 22, 'refused: field b unchanged');
    arktest.assertEQ(h.r.who(), 'A', 'refused: reference field unchanged');
    arktest.assertEQ(h.tag(), 'V1', 'refused: old body still active');
    return 0;
}
}
