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

// === Multi-arg numeric: three-stage path, ExtractArgs extraction (2-16 non-rest args) ===

function verifyTwoInt(a: int, b: int) {
    arktest.assertTrue(a == 10)
    arktest.assertTrue(b == 20)
}

function verifyFiveInts(a: int, b: int, c: int, d: int, e: int) {
    arktest.assertTrue(a == 1)
    arktest.assertTrue(b == 2)
    arktest.assertTrue(c == 3)
    arktest.assertTrue(d == 4)
    arktest.assertTrue(e == 5)
}

function verifyByteShort(a: byte, b: short) {
    arktest.assertTrue(a == 10)
    arktest.assertTrue(b == 1000)
}

function verifyMixedNumeric(a: int, b: long, c: float, d: double) {
    arktest.assertTrue(a == 10)
    arktest.assertTrue(b == 20)
    arktest.assertTrue(c == 0.5f)
    arktest.assertTrue(d == 1.25)
}

function verifyEightInts(a0: int, a1: int, a2: int, a3: int,
                        a4: int, a5: int, a6: int, a7: int) {
    arktest.assertTrue(a0 == 1)
    arktest.assertTrue(a1 == 2)
    arktest.assertTrue(a2 == 3)
    arktest.assertTrue(a3 == 4)
    arktest.assertTrue(a4 == 5)
    arktest.assertTrue(a5 == 6)
    arktest.assertTrue(a6 == 7)
    arktest.assertTrue(a7 == 8)
}

// 16 non-rest args: boundary (maxArgSlots == 16 → three-stage)
function verifySixteenInts(a0: int, a1: int, a2: int, a3: int,
                           a4: int, a5: int, a6: int, a7: int,
                           a8: int, a9: int, a10: int, a11: int,
                           a12: int, a13: int, a14: int, a15: int) {
    arktest.assertTrue(a0 == 1)
    arktest.assertTrue(a1 == 2)
    arktest.assertTrue(a2 == 3)
    arktest.assertTrue(a3 == 4)
    arktest.assertTrue(a4 == 5)
    arktest.assertTrue(a5 == 6)
    arktest.assertTrue(a6 == 7)
    arktest.assertTrue(a7 == 8)
    arktest.assertTrue(a8 == 9)
    arktest.assertTrue(a9 == 10)
    arktest.assertTrue(a10 == 11)
    arktest.assertTrue(a11 == 12)
    arktest.assertTrue(a12 == 13)
    arktest.assertTrue(a13 == 14)
    arktest.assertTrue(a14 == 15)
    arktest.assertTrue(a15 == 16)
}

// 17 non-rest args: exceeds maxArgSlots → ConvertArgsFallback path
function verifySeventeenInts(a0: int, a1: int, a2: int, a3: int,
                            a4: int, a5: int, a6: int, a7: int,
                            a8: int, a9: int, a10: int, a11: int,
                            a12: int, a13: int, a14: int, a15: int,
                            a16: int) {
    arktest.assertTrue(a0 == 1)
    arktest.assertTrue(a16 == 17)
}

// === Multi-arg string: ExtractArgs + MaterializeArgs ===

function verifyTwoStrings(a: string, b: string) {
    arktest.assertTrue(a == 'Hello, ')
    arktest.assertTrue(b == 'World!')
}

function verifyThreeStrings(a: string, b: string, c: string) {
    arktest.assertTrue(a == '你好')
    arktest.assertTrue(b == ', ')
    arktest.assertTrue(c == '世界!')
}

function verifyFiveStrings(a: string, b: string, c: string, d: string, e: string) {
    arktest.assertTrue(a == 'a')
    arktest.assertTrue(b == 'b')
    arktest.assertTrue(c == 'c')
    arktest.assertTrue(d == 'd')
    arktest.assertTrue(e == 'e')
}

// === Mixed types: partial extraction (some extracted, some fallback) ===

function verifyNumericString(a: int, b: string, c: int, d: string) {
    arktest.assertTrue(a == 42)
    arktest.assertTrue(b == 'hello')
    arktest.assertTrue(c == 99)
    arktest.assertTrue(d == 'world')
}

function verifyNumericStringBool(a: int, b: string, c: boolean) {
    arktest.assertTrue(a == 42)
    arktest.assertTrue(b == 'hello')
    arktest.assertTrue(c == true)
}

function verifyAllMixed(a: int, b: string, c: boolean, d: double, e: string) {
    arktest.assertTrue(a == 42)
    arktest.assertTrue(b == 'hello')
    arktest.assertTrue(c == true)
    arktest.assertTrue(d == 3.14)
    arktest.assertTrue(e == 'world')
}

// === Varargs with multiple fixed params (three-stage + rest) ===

function verifyTwoFixedAndRest(a: int, b: int, ...rest: number[]) {
    arktest.assertTrue(a == 1)
    arktest.assertTrue(b == 2)
    arktest.assertTrue(rest.length == 3)
    arktest.assertTrue(rest[0] == 3)
    arktest.assertTrue(rest[1] == 4)
    arktest.assertTrue(rest[2] == 5)
}

function verifyThreeFixedAndRestStrings(a: string, b: string, c: string, ...rest: string[]) {
    arktest.assertTrue(a == 'a')
    arktest.assertTrue(b == 'b')
    arktest.assertTrue(c == 'c')
    arktest.assertTrue(rest.length == 2)
    arktest.assertTrue(rest[0] == 'd')
    arktest.assertTrue(rest[1] == 'e')
}

// === Single-arg (fallback path: numNonRest <= 1) ===

function verifySingleInt(a: int) {
    arktest.assertTrue(a == 42)
}

function verifySingleString(a: string) {
    arktest.assertTrue(a == 'hello')
}
