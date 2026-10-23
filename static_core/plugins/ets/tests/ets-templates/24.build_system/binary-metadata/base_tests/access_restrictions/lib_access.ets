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
desc: Library with access-restriction-annotated declarations
tags: [not-a-test, compile-only]
name: lib_access
arktsconfig: arktsconfig-lib.json
es2panda_options: ['emit-metadata']
---*/

import { annotation } from 'arkruntime'

@annotation.AccessRestriction(["@panda/lib"])
@Retention("SOURCE")
export @interface LibInternal {}

@LibInternal
export class RestrictedClass {
    public constructor() {}
}

export class RestrictedCtor {
    @LibInternal
    public constructor(x: int) {}

    public constructor() {}
}

export class RestrictedMembers {
    @LibInternal
    public ping(): void {}
}

@LibInternal
export interface RestrictedInterface {
    method(): void
}

export class RestrictedField {
    @LibInternal
    public secret: int = 0
}
