'use strict';
/**
 * Copyright (c) 2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the 'License');
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an 'AS IS' BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
export let n = 1;
export let b = false;
export let s = 'hello';
export let bi = 9007199254740991n;
export let jsUndefined = undefined;
export let jsNull = null;
export let jsFunc = function () { return 6; };
export let jsFuncWithParam = function (a, b) { return a + b; };
export let A = {
    'property1': 1,
    'property2': 2
};

export let B = {
    'property1': 'aaaaa',
    'property2': 'bbbbb'
};
export let jsArray = ['foo', 1, true];
export let jsArray1 = ['foo', 1, true];
export let objWithFunc = {
    'func': function () {
        return 'hello';
    }
};
export let objWithFuncParam = {
    'hello': function (a, b) {
        return a + b;
    }
};

export let jsIterable = ['a', 'b', 'c', 'd'];
export let testItetatorObject = {'a': 1, 'b': 2, 'c' : 3};

export class User {
	ID = 123;
}

export let doubledObj = {
    3.2: 'aaa'
};
