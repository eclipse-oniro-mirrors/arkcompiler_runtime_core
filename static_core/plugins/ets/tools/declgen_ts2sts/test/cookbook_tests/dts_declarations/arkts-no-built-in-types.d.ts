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

export declare let sym1: Symbol;
export declare const sym2: Symbol;
export declare const Direction: {
    Up: symbol;
    Down: symbol;
    Left: symbol;
    Right: symbol;
};
export declare const map: Map<Symbol, string>;
export declare const sym5: Symbol;
export declare function getSymbol(): Symbol;
export declare const mySym: Symbol;
export declare function printSymbol(value: Symbol): void;
export type SymbolAlias = symbol;
export declare const sym6: SymbolAlias;
export declare const obj3: {
    [key: symbol]: string;
};
export declare let a: SharedArrayBuffer;
export declare let buffers: SharedArrayBuffer[];
export declare function processData(buffer: SharedArrayBuffer): SharedArrayBuffer;
export declare class DataProcessor {
    private buffer;
    sharedBuffer: SharedArrayBuffer;
}
export interface BufferHolder {
    buffer: SharedArrayBuffer;
    getBuffer(): SharedArrayBuffer;
}
export type BufferType = SharedArrayBuffer;
export type ComplexType = SharedArrayBuffer | string;