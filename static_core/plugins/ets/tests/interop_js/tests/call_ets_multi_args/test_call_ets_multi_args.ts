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

const etsVm = globalThis.gtest.etsVm;
const PKG = 'Lcall_ets_multi_args/ETSGLOBAL;';

function getFn(name: string): Function {
    return etsVm.getFunction(PKG, name);
}

getFn('verifyTwoInt')(10, 20);
getFn('verifyFiveInts')(1, 2, 3, 4, 5);
getFn('verifyByteShort')(10, 1000);
getFn('verifyMixedNumeric')(10, 20, 0.5, 1.25);
getFn('verifyEightInts')(1, 2, 3, 4, 5, 6, 7, 8);
getFn('verifySixteenInts')(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
getFn('verifySeventeenInts')(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17);
getFn('verifyTwoStrings')('Hello, ', 'World!');
getFn('verifyThreeStrings')('你好', ', ', '世界!');
getFn('verifyFiveStrings')('a', 'b', 'c', 'd', 'e');
getFn('verifyNumericString')(42, 'hello', 99, 'world');
getFn('verifyNumericStringBool')(42, 'hello', true);
getFn('verifyAllMixed')(42, 'hello', true, 3.14, 'world');
getFn('verifyTwoFixedAndRest')(1, 2, 3, 4, 5);
getFn('verifyThreeFixedAndRestStrings')('a', 'b', 'c', 'd', 'e');
getFn('verifySingleInt')(42);
getFn('verifySingleString')('hello');
ASSERT_THROWS(TypeError, () => getFn('verifyTwoInt')('not a number', 20));
ASSERT_THROWS(TypeError, () => getFn('verifyTwoStrings')(123, 'hello'));
ASSERT_THROWS(TypeError, () => getFn('verifyFiveInts')(1, 2, 'bad', 4, 5));
ASSERT_THROWS(TypeError, () => getFn('verifyThreeStrings')('ok', 123, 'ok'));
