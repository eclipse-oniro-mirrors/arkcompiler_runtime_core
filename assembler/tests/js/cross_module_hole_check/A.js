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
 * End-to-end fixture (lazy scenario), consumer side. Reading the imported
 * binding makes es2abc emit `ldexternalmodulevar 0;
 * throw.undefinedifholewithname "x"` into func_main_0. The chain
 * A -> B -> SRC is acyclic; whether the throw survives the cross-module
 * elimination depends on B re-exporting a LAZILY imported binding.
 */
import { x } from './B.js';
export let y = x;
