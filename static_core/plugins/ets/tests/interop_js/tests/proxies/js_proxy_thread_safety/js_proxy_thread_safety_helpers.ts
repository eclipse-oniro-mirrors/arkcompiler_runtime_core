/**
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

// JS-side helpers for the JSProxy thread-safety test. The ETS side loads this
// module and invokes its functions through the interface proxy conversion
// path (JSRefConvertInterface -> JSProxy::CreateInterfaceProxy).

export class Greeter {
    constructor(name) {
        this.name = name;
    }
    greet(who) {
        return `Hello, ${who}!`;
    }
}

export function getGreeter() {
    return new Greeter('js_greeter');
}

export function greet(greeter, who) {
    return greeter.greet(who);
}
