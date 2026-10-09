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

function equal(actual, expected, name) {
    if (!Object.is(actual, expected)) {
        throw new Error(name + ': ' + actual + ' != ' + expected);
    }
}

function numeric() {
    let zero = 0;
    let negative = -1;
    let minusFour = -4;
    let two = 2;
    let max = 2147483647;
    let min = -2147483648;
    equal(1 / -zero, -Infinity, 'neg zero');
    equal(1 / (zero * negative), -Infinity, 'mul zero');
    equal(1 / (negative * zero), -Infinity, 'mul zero reversed');
    equal(1 / (minusFour % two), -Infinity, 'mod zero');
    equal(max + two, 2147483649, 'add wide');
    equal(min - two, -2147483650, 'sub wide');
    equal(max * two, 4294967294, 'mul wide');
    equal(-min, 2147483648, 'neg wide');
    let tiny = 1e-300;
    equal(1 / (tiny * -tiny), -Infinity, 'negative underflow');
    let huge = 1e300;
    equal(huge * huge, Infinity, 'overflow');
    equal((huge * huge) - (huge * huge), NaN, 'NaN');
}

function mergeZero(flag) {
    let value;
    if (flag) { value = 0.0 / -2.0; } else { value = 0.0 / 2.0; }
    return 1 / value;
}

function nullish(value) { return value === null || value === undefined; }
function notNullish(value) { return value !== null && value !== undefined; }
function dynamicSemantics() {
    let calls = 0;
    const object = { valueOf() { calls++; return 7; } };
    equal(!!object, true, 'object truth');
    equal(nullish(object), false, 'nullish object');
    equal(notNullish(object), true, 'nonnull object');
    equal(calls, 0, 'truth/nullish do not coerce');
    for (let value of [null, undefined, false, 0, '', Symbol('x'), 1n]) {
        equal(notNullish(value), !nullish(value), 'nullish complement');
    }
    object + 1;
    object + 1;
    equal(calls, 2, 'unused arithmetic retains coercion');
    let value = object;
    value++;
    equal(value, 8, 'ToNumeric');
    equal(calls, 3, 'single conversion');
    const symbol = Symbol('s');
    let caught = false;
    try { +symbol; } catch (error) { caught = error instanceof TypeError; }
    equal(caught, true, 'unused conversion still throws');
    let big = 2n;
    equal(big + big, 4n, 'BigInt');
}

function strictCse(x, y) {
    const first = x === y;
    const second = x === y;
    return [first, second, first];
}

function catchLoop(limit) {
    let value = 0;
    try {
        for (let i = 0; i < 5; i++) {
            value = i + 0.5;
            if (i === limit) { throw i; }
            value = i + 1.5;
        }
    } catch (error) { return value; }
    return value;
}

let baseCalls = 0;
class Base { constructor() { baseCalls++; } }
class Good extends Base {
    constructor() { super(); this.x = 1; this.y = this.x + 2; }
}
class Before extends Base {
    constructor() { this.x = 1; super(); }
}
class Twice extends Base {
    constructor() { super(); super(); }
}
class Caught extends Base {
    constructor() {
        try { this.x = 1; } catch (error) {
            equal(error instanceof ReferenceError, true, 'this before super');
        }
        super();
        this.x = 4;
    }
}
function expectReferenceError(ctor) {
    let caught = false;
    try { new ctor(); } catch (error) { caught = error instanceof ReferenceError; }
    equal(caught, true, 'super check');
}

function fieldInitializers() {
    class Fields {
        x = 1;
        y = 1;
        z = 1;
        #a = 1;
        #b = 1;
        #c = 1;
        static x = 1;
        static y = 1;
        static #s = 1;
        static #t = 1;
        values() { return [this.x, this.y, this.z, this.#a, this.#b, this.#c]; }
        static values() { return [this.x, this.y, this.#s, this.#t]; }
    }
    for (const value of new Fields().values()) { equal(value, 1, 'instance field initializer'); }
    for (const value of Fields.values()) { equal(value, 1, 'static field initializer'); }
}

function range(a, b, c, d, e, f) { return [a, b, c, d, e, f].join(':'); }
function rangeAndRest() {
    const a = 'a', b = 'b', c = 'c', d = 'd', e = 'e', f = 'f';
    equal(range(a, b, c, d, e, f), 'a:b:c:d:e:f', 'range');
    equal(range(f, a, a, c, b, d), 'f:a:a:c:b:d', 'range aliases');
    const source = { a, b, c, d };
    const { a: ignored, [b]: alsoIgnored, ...rest } = source;
    equal(Object.keys(rest).join(':'), 'c:d', 'excluded keys');
}

numeric();
equal(mergeZero(true), -Infinity, 'negative zero merge');
equal(mergeZero(false), Infinity, 'positive zero merge');
dynamicSemantics();
equal(strictCse(NaN, NaN).join(':'), 'false:false:false', 'NaN strict CSE');
const object = {};
equal(strictCse(object, object).join(':'), 'true:true:true', 'object strict CSE');
for (let i = 0; i < 5; i++) { equal(catchLoop(i), i + 0.5, 'catch live value'); }
equal(new Good().y, 3, 'super success');
equal(baseCalls, 1, 'base once');
expectReferenceError(Before);
equal(baseCalls, 1, 'base skipped');
expectReferenceError(Twice);
equal(baseCalls, 3, 'second base side effect precedes error');
equal(new Caught().x, 4, 'super after caught error');
rangeAndRest();
fieldInitializers();

async function reject() { throw new Error('async rejection'); }
reject().then(
    () => print('FAIL: rejection resolved'),
    error => { equal(error.message, 'async rejection', 'async accumulator'); print('PASS: abcsize runtime'); }
);
