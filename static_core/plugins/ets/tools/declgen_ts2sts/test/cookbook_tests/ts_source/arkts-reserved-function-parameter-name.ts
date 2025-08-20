/*
 * Copyright (c) 2022-2025 Huawei Device Co., Ltd.
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

export class A {
    value = 24;
    foo(this: A, a: number): void { }
}
export interface Itest {
    foo(this: number): void
}

export namespace ns {
    export function foo(this: number): void { }
}

export function greet(this: string) {
    return 0
}

export const fun = function bar(this: number, a: number): number {
    return a + 1
}

export const foo = function qux(short: number, long: string, int: number): number {
    return short + 1
}

export const fox = function run(byte: number, char: string): number {
    return byte + 1
}

export const corge = function plugh(float: string, double: string): string {
    return float + double
}

export function grault(boolean: number): void { }

export function waldo(abstract: string, async: string): void { }
