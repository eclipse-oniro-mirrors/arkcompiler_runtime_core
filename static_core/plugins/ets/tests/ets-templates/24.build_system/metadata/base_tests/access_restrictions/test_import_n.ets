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
desc: >-
    Negative: usage of access-restricted declarations (restricted class,
    restricted constructor via super() and new, restricted method, restricted
    interface type reference, and restricted field access) from a non-allowed
    package must be reported with ESE154780 when the declarations come from a
    binary dependency.
files: [lib_access.ets]
tags: [compile-only, negative]
name: test_import_n
arktsconfig: arktsconfig-import.json
expected_error:
  - "Semantic error ESE154780: Access to 'RestrictedClass' is restricted by 'LibInternal'"
  - "Semantic error ESE154780: Access to 'constructor' is restricted by 'LibInternal'"
  - "Semantic error ESE154780: Access to 'ping' is restricted by 'LibInternal'"
  - "Semantic error ESE154780: Access to 'RestrictedInterface' is restricted by 'LibInternal'"
  - "Semantic error ESE154780: Access to 'secret' is restricted by 'LibInternal'"
---*/

import {
    RestrictedClass,
    RestrictedCtor,
    RestrictedMembers,
    RestrictedInterface,
    RestrictedField,
} from '@panda/lib/lib_access'

class Sub extends RestrictedCtor {
    public constructor() {
        super(1)
    }
}

function useInterface(param: RestrictedInterface): void {}

function main(): void {
    let a: RestrictedClass = new RestrictedClass()
    let b: RestrictedCtor = new RestrictedCtor(1)
    let c: Sub = new Sub()
    let d = new RestrictedMembers()
    d.ping()
    let e = new RestrictedField()
    let f: int = e.secret
    useInterface(e)
}
