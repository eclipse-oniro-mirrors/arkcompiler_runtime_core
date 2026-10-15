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

/*---
  files: [lib_dummy.ets]
  arktsconfig: arktsconfig.json
  es2panda_options: [simultaneous]
---*/

import { Dummy } from './lib_dummy';

// A `undefined`-typed super parameter and an `object`-typed override parameter both erase to
// std.core.Object, so the override bridge has the same assembler signature as the overridden
// method itself. Before the fix, simultaneous mode emitted the self-recursive bridge in place of
// Foo2.foo, so running this test overflowed the stack.
class Foo {
    fireFoo(param?: object): int {
        return this.foo(param, undefined)
    }

    foo(x?: object, y?: undefined): int {
        return 1
    }
}

class Foo2 extends Foo {
    foo(x?: object, y?: object): int {
        return Dummy.VALUE + 1
    }
}

function main(): void {
    let b: Foo2 = new Foo2()
    arktest.assertEQ(b.fireFoo(), Dummy.VALUE + 1)
    arktest.assertEQ(new Foo().fireFoo(), 1)
    arktest.assertEQ(b.foo(), Dummy.VALUE + 1)
    arktest.assertEQ(b.foo(undefined, undefined), Dummy.VALUE + 1)
    arktest.assertEQ(b.foo({}, {}), Dummy.VALUE + 1)
}
