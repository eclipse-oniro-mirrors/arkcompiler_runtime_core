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
 * Loads one target path independently into two AbcRuntimeLinker contexts. Neither file has been
 * superseded, so the bare-path reload must refuse the genuinely ambiguous target.
 * The linker-specific half of the old scenario (each receiver selecting its own context) lives
 * in tests/ani/hotreload_app, which calls the protected entry the way the framework does.
 *
 * argv: <targetPath> <patchPath>
 */
/*
 * The native side is bound by the smoke driver (tests/ani/hotreload_smoke): bare paths,
 * the target resolved across the live panda files.
 */
class HotReloadBridge {
    public static native hotReloadNative(target: string, patch: string): int;

    public run(a0: string, a1: string, a2: string): int {
    let firstLinker = new AbcRuntimeLinker(undefined, [a0]);
    let secondLinker = new AbcRuntimeLinker(undefined, [a0]);
    let firstClass = firstLinker.loadClass('generation.Counter', false);
    let secondClass = secondLinker.loadClass('generation.Counter', false);
    // Null checks first: a failed loadClass makes the distinctness check vacuously false,
    // which produces a misleading error. Name what actually failed.
    arktest.assertTrue(firstClass != undefined, 'first loadClass returned undefined');
    arktest.assertTrue(secondClass != undefined, 'second loadClass returned undefined');
    arktest.assertTrue(firstClass != secondClass, 'the two contexts published distinct classes');
    let rc = HotReloadBridge.hotReloadNative(a0, a1);
    arktest.assertEQ(rc, 21, 'an ambiguous target must be refused');
    return 0;
}
}
