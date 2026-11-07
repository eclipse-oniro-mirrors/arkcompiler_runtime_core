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

import { unusedImport, usedImport } from './modules/dependency';

const unusedExport = 10;
let usedExport = 20;
export { unusedExport, usedExport };

function readImport() { return usedImport(); }
function readExport() { return usedExport; }
function readText() { return 'before'; }
function otherText() { return 'before'; }
function untouched() { return 7; }
function debugLines(a) {
    let b = a + 1;
    print('first', b);
    let c = b * 2;
    print('second', c);
    return c;
}

print(readImport());
print(readExport());
print(readText());
print(otherText());
