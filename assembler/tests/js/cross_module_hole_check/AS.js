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
 * End-to-end fixture (SDK scenario), consumer side. Reads an import from BS so
 * es2abc emits `ldexternalmodulevar 0; throw.undefinedifholewithname "x"` into
 * func_main_0. The chain AS -> BS -> '@ohos:mock_sdk' is acyclic: the SDK
 * record is absent from the compilation and (per the SDK contract) cannot
 * import back into project source, so the throw must be eliminated.
 */
import { x } from './BS.js';
export let y = x;
