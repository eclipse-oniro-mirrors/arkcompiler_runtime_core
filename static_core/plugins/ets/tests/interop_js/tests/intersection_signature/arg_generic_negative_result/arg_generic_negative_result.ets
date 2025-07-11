/**
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
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

import {
    tsNumber,
    tsString,
    checkAgeNameGeneric,
    AgeNameGenericClass,
    instanceAgeNameGenericClass,
    ChildAgeNameGenericClass,
    instanceChildAgeNameGenericClass,
} from 'main_js';

class TypeWrong {
    age = tsNumber;
}
function checkArgFuFromSts(): boolean {
    return checkAgeNameGeneric(new TypeWrong()) as boolean == false;
}

function checkTypeClass(): boolean {
    return isThrowing(() => { new AgeNameGenericClass(tsNumber); });
}

function checkClassMethod(): boolean {
    return isThrowing(() => {
        const Type = new AgeNameGenericClass(tsString, tsNumber);
        Type.createUser(tsNumber);
    });
}
function checkInstanceClass(): boolean {
    return isThrowing(() => { instanceAgeNameGenericClass.createUser(tsNumber); });
}

function checkChildClass(): boolean {
    return isThrowing(() => { new ChildAgeNameGenericClass(tsNumber); });
}

function checkChildClassMethod(): boolean {
    return isThrowing(() => {
        const Type = new ChildAgeNameGenericClass(tsString, tsNumber);
        Type.createUser(tsNumber);
    });
}

function checkInstanceChildClassMethod(): boolean {
    return isThrowing(() => { instanceChildAgeNameGenericClass.createUser(tsNumber); });
}

function isThrowing(f: () => void): boolean {
    try {
        f();
        return false
    } catch (e) {
        return true
    }
}
