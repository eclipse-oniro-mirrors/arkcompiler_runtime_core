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
 * Patch: only the body of `f` changes. Nothing about its name, signature or the sites that reference it moves.
 *
 * Four call shapes, printed before and after, so they can be compared against each other rather
 * than against an expectation written down somewhere else:
 *
 *   direct  calling `f` by name
 *   local   through a value taken BEFORE the reload
 *   static  through a value taken before the reload and parked in a static field
 *   fresh   through a value taken AFTER the reload
 *   late    through a value made at a reference site first loaded after the reload, compared for
 *           equality against one taken before it: same function, so they must be equal
 *
 * argv: <targetPath> <patchPath>
 */

import { RuntimeDebug } from 'std/debug';

type Fn = () => int;

function f(): int {
    return 200;
}

class Holder {
    public static g: Fn = f;
}

/*
 * A second reference site, deliberately TWO levels from `main`: under the default
 * `--verification-mode=ahead-of-time` verifying a class loads what its methods reference, but only
 * one level deep, so `LazyHolder` is loaded before the reload and `Lazy` is not. The synthesised
 * class behind `Lazy`'s reference to `f` is therefore first loaded AFTER the reload, from the patch,
 * and resolves its `FunctionReference` annotation against the new generation. The value it makes and
 * the value `main` took beforehand denote the same function and must compare equal --- which they do
 * only if the commit re-points the older synthesised class's cached target (`typeMetaData_`).
 */
class Lazy {
    public static make(): Fn {
        return f;
    }
}

class LazyHolder {
    public static get(): Fn {
        return Lazy.make();
    }
}

/*
 * The native side is bound by the smoke driver (tests/ani/hotreload_smoke): bare paths,
 * the target resolved across the live panda files.
 */
class HotReloadBridge {
    public static native hotReloadNative(target: string, patch: string): int;

    public run(a0: string, a1: string, a2: string): int {
    let g: Fn = f;
    arktest.assertEQ(f(), 100, 'before: direct call');
    arktest.assertEQ(g(), 100, 'before: local value');
    arktest.assertEQ(Holder.g(), 100, 'before: static field value');
    let rc = HotReloadBridge.hotReloadNative(a0, a1);
    arktest.assertEQ(rc, 0, 'reload rc');
    let fresh: Fn = f;
    let late: Fn = LazyHolder.get();
    arktest.assertEQ(f(), 200, 'applied: direct call');
    arktest.assertEQ(g(), 200, 'applied: pre-reload value reaches the new body');
    arktest.assertEQ(Holder.g(), 200, 'applied: static field value');
    arktest.assertEQ(fresh(), 200, 'applied: fresh value');
    arktest.assertEQ(late(), 200, 'applied: late-loaded reference site');
    arktest.assertTrue(g == late, 'function-value identity redirected across the swap');
    return 0;
}
}
