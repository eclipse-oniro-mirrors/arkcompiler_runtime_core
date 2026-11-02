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
 * End-to-end fixture (lazy scenario), re-export side. The frontend rewrites
 * this into an IMPLICIT INDIRECT export that reuses the lazy import's module
 * request index (CheckImplicitIndirectExport / ConvertLocalExportToIndirect),
 * and marks the './SRC.js' request lazy in the module-request phase literal.
 * The re-export does not trigger the lazy load, so `x` may still be hole for
 * consumers — the cross-module elimination must keep A's throw.
 */
import lazy { x } from './SRC.js';
export { x };
