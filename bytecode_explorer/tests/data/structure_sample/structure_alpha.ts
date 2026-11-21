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

import { betaHelper, betaValue as renamedBeta } from './structure_beta';
import * as beta from './structure_beta';
export { betaHelper as reexportedHelper } from './structure_beta';
export * from './structure_beta';

export interface ErasedShape {
    name: string;
}

export @interface __$$ETS_ANNOTATION$$__Route {
    path: string = '/';
}

export enum Color {
    Red,
    Green = 4,
    Blue = 'blue'
}

export namespace Outer {
    export const answer = 42;

    export namespace Inner {
        export enum State {
            Off,
            On
        }

        export function nested(value: number): number {
            return value + answer;
        }
    }
}

@__$$ETS_ANNOTATION$$__Route({path: '/counter'})
export class Counter {
    private count: number = renamedBeta;
    static label: string = 'counter';

    constructor(start: number) {
        this.count = start;
    }

    increment(step: number): number {
        this.count += step;
        return this.count;
    }

    static create(): Counter {
        return new Counter(beta.betaValue);
    }

    get value(): number {
        return this.count;
    }

    set value(next: number) {
        this.count = next;
    }
}

export function alphaEntry(value: number): number {
    return betaHelper(value) + Outer.Inner.nested(value) + renamedBeta;
}

export function makeLocalBox(value: number): number {
    class LocalBox {
        private dataValue: number = value;

        get data(): number {
            return this.dataValue;
        }

        static get tag(): string {
            return 'local';
        }
    }

    return new LocalBox().data;
}
