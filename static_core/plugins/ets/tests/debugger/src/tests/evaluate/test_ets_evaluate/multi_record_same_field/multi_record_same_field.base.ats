/*
 * Copyright (c) 2024-2026 Huawei Device Co., Ltd.
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
evaluation:
  expected_imports_base: True
---*/

// Multi-record abc: this file and the two imported helpers compile into ONE
// panda file, each record with its own ETSGLOBAL. All three records declare
// the same top-level name `shared` (111 / 999 / 888). Evaluating `shared` at
// the breakpoint below must resolve to THIS record's field (111), never to a
// sibling record's, regardless of class-enumeration order.
import { otherA } from './multi_record_same_field.helper_a';
import { otherB } from './multi_record_same_field.helper_b';

export let shared: int = 111;

function main(): void {
    undefined;  // #BP{number}
    console.log('shared: ' + shared + ', others: ' + otherA + ', ' + otherB);
    return;     // #BP
}
