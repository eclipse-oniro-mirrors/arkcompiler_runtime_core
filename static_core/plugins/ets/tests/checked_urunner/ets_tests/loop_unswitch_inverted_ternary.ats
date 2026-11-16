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

// Regression test for LoopUnswitch of a loop-invariant condition that appears twice
// inside the loop with opposite condition codes, e.g. a pair of opposite ternaries on
// the same loop-invariant flag (the second one negated), which both branch on it and
// compile to `jnez`/`jeqz`. Modeled on WeightedFlowLayout.measureNonStretch, where such
// a pair silently swapped padding offsets. LoopUnswitch must plant, in every loop copy,
// the constants that make each condition agree with the unswitch guard's polarity,
// derived from each condition's own CC/imm.

//! CHECKER      Inverted loop-invariant ternaries are unswitched correctly (JIT)
//! RUN          force_jit: true,
//!              options: "--compiler-regex='.*measureNonStretch.*'",
//!              entry: "loop_unswitch_inverted_ternary.ETSGLOBAL::main", result: 0
//! EVENT        /Compilation,.*measureNonStretch.*,.*COMPILED/
//! METHOD       "WeightedFlowLayout::measureNonStretch"
//! PASS_BEFORE  "LoopUnswitch"
//! # the loop contains the opposite conditions on the loop-invariant flag parameter
//! INST_COUNT   /IfImm NE b\s+v1, 0x0/, 1
//! INST_COUNT   /IfImm EQ b\s+v1, 0x0/, 1
//! PASS_AFTER   "LoopUnswitch"
//! # both in-loop conditions are folded to constants, only the guard tests the flag
//! INST_COUNT   /IfImm (NE|EQ) b\s+v1, 0x0/, 1
//! # the guard and the folded conditions in both loop copies, plus loop-exit checks
//! TRUE         IR_COUNT(/IfImm (NE|EQ) b\s+v\d+, 0x0/) >= 5

//! CHECKER      Inverted loop-invariant ternaries are unswitched correctly (AOT)
//! SKIP_IF      @architecture == "arm32"
//! RUN_PAOC     options: "--compiler-regex='.*measureNonStretch.*'"
//! METHOD       "WeightedFlowLayout::measureNonStretch"
//! PASS_AFTER   "LoopUnswitch"
//! # both in-loop conditions are folded to constants, only the guard tests the flag
//! INST_COUNT   /IfImm (NE|EQ) b\s+v1, 0x0/, 1
//! # the guard and the folded conditions in both loop copies, plus loop-exit checks
//! TRUE         IR_COUNT(/IfImm (NE|EQ) b\s+v\d+, 0x0/) >= 5
//! RUN          entry: "loop_unswitch_inverted_ternary.ETSGLOBAL::main", result: 0

class Padding {
    public amountByX: number = 20;
    public amountByY: number = 20;
}

class WeightedFlowLayout {
    public main: number = 8;

    public measureNonStretch(isHorizontal: boolean, iterations: int): number {
        let padding = new Padding();
        let result: number = 0;
        let i: int = 0;
        while (i < iterations) {
            let a = isHorizontal ? this.main : padding.amountByX;
            let b = !isHorizontal ? padding.amountByY : this.main;
            result = a * 100 + b;
            i++;
        }
        return result;
    }
}

function main(): void {
    let layout = new WeightedFlowLayout();
    // false case: both padding offsets are selected, so the expected value is 20 * 100 + 20 == 2020
    arktest.assertEQ(layout.measureNonStretch(false, 3), 2020);
    // true case: the main extent is selected for both, so the expected value is 8 * 100 + 8 == 808
    arktest.assertEQ(layout.measureNonStretch(true, 3), 808);
}
