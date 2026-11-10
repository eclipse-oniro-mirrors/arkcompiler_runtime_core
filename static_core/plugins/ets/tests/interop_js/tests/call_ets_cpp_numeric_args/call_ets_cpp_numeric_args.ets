/**
 * Copyright (c) 2025-2026 Huawei Device Co., Ltd.
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

// === Two-arg numeric functions (three-stage path: numNonRest=2) ===

function addTwoInt(a: int, b: int): int {
    return a + b
}

function subTwoInt(a: int, b: int): int {
    return a - b
}

function mulTwoInt(a: int, b: int): int {
    return a * b
}

function maxTwoInt(a: int, b: int): int {
    return a > b ? a : b
}

function minTwoInt(a: int, b: int): int {
    return a < b ? a : b
}

function addTwoLong(a: long, b: long): long {
    return a + b
}

function addTwoDouble(a: double, b: double): double {
    return a + b
}

function subTwoDouble(a: double, b: double): double {
    return a - b
}

function mulTwoDouble(a: double, b: double): double {
    return a * b
}

function divTwoDouble(a: double, b: double): double {
    return a / b
}

function addByteShort(a: byte, b: short): int {
    return a + b
}

function addIntLong(a: int, b: long): long {
    return a + b
}

function addIntFloat(a: int, b: float): double {
    return a + b
}

function addFloatDouble(a: float, b: double): double {
    return a + b
}

function addIntDouble(a: int, b: double): double {
    return a + b
}

// === Three+ arg numeric functions (three-stage path: numNonRest>=3) ===

function addThreeInt(a: int, b: int, c: int): int {
    return a + b + c
}

function addFourInt(a: int, b: int, c: int, d: int): int {
    return a + b + c + d
}

function addFiveInt(a: int, b: int, c: int, d: int, e: int): int {
    return a + b + c + d + e
}

function addSixInt(a: int, b: int, c: int, d: int, e: int, f: int): int {
    return a + b + c + d + e + f
}

function addThreeDouble(a: double, b: double, c: double): double {
    return a + b + c
}

function addFourDouble(a: double, b: double, c: double, d: double): double {
    return a + b + c + d
}

function addFiveDouble(a: double, b: double, c: double, d: double, e: double): double {
    return a + b + c + d + e
}

function addMixedNumericFour(a: int, b: long, c: float, d: double): double {
    return a + b + c + d
}

function addMixedNumericFive(a: byte, b: short, c: int, d: long, e: double): double {
    return a + b + c + d + e
}

// === Eight-arg functions (three-stage path: numNonRest=8) ===

function sumEightInt(a0: int, a1: int, a2: int, a3: int,
                     a4: int, a5: int, a6: int, a7: int): int {
    return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7
}

function sumEightDouble(a0: double, a1: double, a2: double, a3: double,
                        a4: double, a5: double, a6: double, a7: double): double {
    return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7
}

// === Sixteen-arg function (boundary: maxArgSlots=16 → three-stage) ===

function sumSixteenInt(a0: int, a1: int, a2: int, a3: int,
                       a4: int, a5: int, a6: int, a7: int,
                       a8: int, a9: int, a10: int, a11: int,
                       a12: int, a13: int, a14: int, a15: int): int {
    return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 +
           a8 + a9 + a10 + a11 + a12 + a13 + a14 + a15
}

// === Seventeen-arg function (exceeds maxArgSlots → ConvertArgsFallback) ===

function sumSeventeenInt(a0: int, a1: int, a2: int, a3: int,
                         a4: int, a5: int, a6: int, a7: int,
                         a8: int, a9: int, a10: int, a11: int,
                         a12: int, a13: int, a14: int, a15: int,
                         a16: int): int {
    return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 +
           a8 + a9 + a10 + a11 + a12 + a13 + a14 + a15 + a16
}

// === Single-arg functions (fallback path: numNonRest<=1) ===

function identityInt(a: int): int {
    return a
}

function identityLong(a: long): long {
    return a
}

function identityDouble(a: double): double {
    return a
}

function negateInt(a: int): int {
    return -a
}

function absDouble(a: double): double {
    return a < 0.0 ? -a : a
}

// === Varargs with fixed params (three-stage + rest) ===

function sumTwoFixedAndRest(a: int, b: int, ...rest: number[]): number {
    let s: number = a + b
    for (let i = 0; i < rest.length; i++) {
        s += rest[i]
    }
    return s
}

function sumThreeFixedAndRest(a: int, b: int, c: int, ...rest: number[]): number {
    let s: number = a + b + c
    for (let i = 0; i < rest.length; i++) {
        s += rest[i]
    }
    return s
}
