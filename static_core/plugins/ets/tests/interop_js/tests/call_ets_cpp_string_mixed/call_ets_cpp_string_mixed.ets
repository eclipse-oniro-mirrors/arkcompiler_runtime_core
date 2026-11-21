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

// === Two-arg string functions (three-stage: ExtractArgs + MaterializeArgs) ===

function concatTwoStrings(a: string, b: string): string {
    return a + b
}

function compareTwoStrings(a: string, b: string): int {
    if (a == b) {
        return 0
    }
    return a < b ? -1 : 1
}

function stringLengthSum(a: string, b: string): int {
    return a.length + b.length
}

function firstCharConcat(a: string, b: string): string {
    if (a.length == 0 || b.length == 0) {
        return ''
    }
    return a.substring(0, 1) + b.substring(0, 1)
}

// === Three+ arg string functions ===

function concatThreeStrings(a: string, b: string, c: string): string {
    return a + b + c
}

function concatFourStrings(a: string, b: string, c: string, d: string): string {
    return a + b + c + d
}

function concatFiveStrings(a: string, b: string, c: string, d: string, e: string): string {
    return a + b + c + d + e
}

function concatThreeWithSeparator(a: string, b: string, c: string): string {
    return a + ',' + b + ',' + c
}

function joinFiveWithPipe(a: string, b: string, c: string, d: string, e: string): string {
    return a + '|' + b + '|' + c + '|' + d + '|' + e
}

// === Mixed numeric + string functions ===

function verifyIntString(a: int, b: string): boolean {
    return a == 42 && b == 'hello'
}

function verifyStringInt(a: string, b: int): boolean {
    return a == 'world' && b == 99
}

function verifyNumericStringNumeric(a: int, b: string, c: int): boolean {
    return a == 1 && b == 'mid' && c == 3
}

function verifyIntStringDoubleString(a: int, b: string, c: double, d: string): boolean {
    return a == 10 && b == 'str1' && c == 3.14 && d == 'str2'
}

function verifyAllFiveMixed(a: int, b: string, c: double, d: string, e: int): boolean {
    return a == 1 && b == 'two' && c == 3.0 && d == 'four' && e == 5
}

function verifyAllSixMixed(a: int, b: string, c: double, d: string, e: int, f: string): boolean {
    return a == 1 && b == 'two' && c == 3.0 && d == 'four' && e == 5 && f == 'six'
}

function verifyLongStringLong(a: long, b: string, c: long): boolean {
    return a == 1000000 && b == 'middle' && c == 2000000
}

function verifyByteStringShort(a: byte, b: string, c: short): boolean {
    return a == 10 && b == 'mid' && c == 1000
}

// === Numeric verification returning string ===

function verifyIntPairReturnString(a: int, b: int): string {
    if (a == 12 && b == 34) {
        return 'match'
    }
    return 'no_match'
}

function verifyDoublePairReturnString(a: double, b: double): string {
    if (a == 1.5 && b == 2.5) {
        return 'match'
    }
    return 'no_match'
}

// === Single-arg string (fallback path: numNonRest<=1) ===

function identityString(a: string): string {
    return a
}

function stringLength(a: string): int {
    return a.length
}

function reverseString(a: string): string {
    let result: string = ''
    for (let i = a.length - 1; i >= 0; i--) {
        result += a.substring(i, i + 1)
    }
    return result
}

// === String varargs with fixed params ===

function concatTwoFixedAndRestStrings(a: string, b: string, ...rest: string[]): string {
    let s: string = a + b
    for (let i = 0; i < rest.length; i++) {
        s += rest[i]
    }
    return s
}

// === Six-arg mixed (three-stage with interleaved extracted/fallback) ===

function verifySixInterleaved(a: int, b: string, c: int, d: string, e: int, f: string): boolean {
    return a == 1 && b == 'a' && c == 2 && d == 'b' && e == 3 && f == 'c'
}

// === Eight-arg all strings ===

function concatEightStrings(a: string, b: string, c: string, d: string,
                            e: string, f: string, g: string, h: string): string {
    return a + b + c + d + e + f + g + h
}
