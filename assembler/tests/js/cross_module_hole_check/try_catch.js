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
 * Rule S try/catch regression (reviewer-requested). `x` is a TDZ `let` used
 * inside try and catch:
 *  - `x = 1` in the try emits a WRITE-side hole-check throw followed by the
 *    Rule S store (stlexvar gen);
 *  - `y = x` in the catch emits a READ-side hole-check throw.
 * Rule S's soundness at the handler relies on the try_begin/try_end
 * predecessor intersection: the store's gen inside the try region must NOT
 * reach the handler entry, so the catch's read-side throw must SURVIVE the
 * optimizer (x is still hole there). Both throws are expected to remain in
 * the optimized func_main_0.
 */
let y;
try {
  x = 1;
} catch (e) {
  y = x;
}
let x;
