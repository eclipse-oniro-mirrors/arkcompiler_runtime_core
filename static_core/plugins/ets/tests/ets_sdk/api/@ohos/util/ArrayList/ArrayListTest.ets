/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import { ArrayList } from "@ohos.util.ArrayList";
import { BusinessError } from "@ohos.base";

const TypeErrorCodeId: number = 401;
const OutOfBoundsErrorCodeId: number = 10200001;

function main() {
    const suite = new ArkTestsuite("ArrayList API tests");

    suite.addTest("Testing ArrayList add", testArrayList0001);
    suite.addTest("Testing ArrayList basic operations", testArrayList0002);
    suite.addTest("Testing ArrayList sort", testArrayList0003);
    suite.addTest("Testing ArrayList exception handling for empty container", testArrayList0004);
    suite.addTest("Testing ArrayList exception handling for invalid index", testArrayList0005);
    suite.addTest("Testing ArrayList key iteration", testArrayList0006);
    suite.addTest("Testing ArrayList insert and remove with floor index", testArrayList0007);

    return suite.run();
}

function testedIterator(arrayList: ArrayList<number>, testArray: Array<number>): boolean {
    let itr = arrayList.$_iterator();
    let tmp = itr.next().value;
    let testArray1 = new Array<number>();
    do {
        testArray1.push(tmp!);
        tmp = itr.next().value;
    } while (tmp != undefined);

    for (let k = 0; k < arrayList.length; k++) {
        if (testArray1[k] !== testArray[k]) {
            return false;
        }
    }
    return true;
}

function testArrayList0001() {
    let arr = new ArrayList<number>();
    arr.add(1);
    arr.add(2);

    for (let i = 0; i < arr.length; i++) {
        assertEQ(arr[i], i + 1, `The element at index ${i} should be ${i + 1}`);
    }
}

function testArrayList0002() {
    let arr1 = new ArrayList<number>();
    let testArray = new Array<number>();

    for (let i = 0; i < 10; i++) {
        arr1.add(i);
        testArray.push(i);
    }
    for (let i = 0; i < testArray.length; i++) {
        assertEQ(arr1[i], testArray[i], `The element at index ${i} should be ${testArray[i]}`);
    }

    assertTrue(arr1.has(2), "The ArrayList should contain the value 2");
    assertEQ(arr1.getCapacity(), 10, "The capacity of the ArrayList should be 10");
    assertEQ(arr1.getLastIndexOf(1), 1, "The last index of value 1 should be 1");
    assertEQ(arr1.getIndexOf(5), 5, "The index of value 5 should be 5");

    let array = arr1.convertToArray();
    for (let i = 0; i < testArray.length; i++) {
        assertEQ(array[i], testArray[i], `The element at index ${i} should be ${testArray[i]} in the converted array`);
    }

    let newArrayList = arr1.clone();
    for (let i = 0; i < testArray.length; i++) {
        assertEQ(newArrayList[i], testArray[i],
            `The element at index ${i} should be ${testArray[i]} in the cloned ArrayList`);
    }

    arr1.insert(999, 3);
    testArray.splice(3, 0, 999);
    for (let i = 0; i < testArray.length; i++) {
        assertEQ(arr1[i], testArray[i], `The element at index ${i} should be ${testArray[i]} after insertion`);
    }

    arr1.removeByIndex(9);
    testArray.splice(9, 1);
    for (let i = 0; i < testArray.length; i++) {
        assertEQ(arr1[i], testArray[i], `The element at index ${i} should be ${testArray[i]} after removal by index`);
    }

    const removeRes = arr1.remove(7);
    testArray.splice(8, 1);
    for (let i = 0; i < testArray.length; i++) {
        assertEQ(arr1[i], testArray[i], `The element at index ${i} should be ${testArray[i]} after removal by value`);
    }
    assertTrue(removeRes, "The remove operation should return true");

    arr1.removeByRange(1, 3);
    testArray.splice(1, 2);
    for (let i = 0; i < testArray.length; i++) {
        assertEQ(arr1[i], testArray[i], `The element at index ${i} should be ${testArray[i]} after removal by range`);
    }

    arr1.forEach((value, index) => {
        assertEQ(value, testArray[index], `The element at index ${index} should be ${testArray[index]} during forEach`);
    });

    let subArrayList = arr1.subArrayList(1, 3);
    const newTestArray = testArray.slice(1, 3);
    for (let i = 0; i < subArrayList.length; i++) {
        assertEQ(newTestArray[i], subArrayList[i],
            `The element at index ${i} should be ${newTestArray[i]} in the subArrayList`);
    }

    let j = 0;
    for (const data of arr1) {
        assertEQ(data, testArray[j], `The element at index ${j} should be ${testArray[j]} during iteration`);
      j++;
    }

    assertTrue(testedIterator(arr1, testArray), "The iterator should correctly iterate over all elements");

    arr1.replaceAllElements((item, index) => {
        return item * 2;
    });

    for (let i = 0; i < testArray.length; i++) {
        assertEQ(arr1[i], testArray[i] * 2,
            `The element at index ${i} should be ${testArray[i] * 2} after replaceAllElements`);
    }
}

function testArrayList0003() {
    let arr2 = new ArrayList<number>();
    arr2.add(4);
    arr2.add(3);
    arr2.add(1);
    arr2.add(2);
    arr2.add(0);
    arr2.sort((a,b) => a - b);
    for (let i = 0; i < 5; i++) {
        assertEQ(arr2[i], i, `The element at index ${i} should be ${i} after sorting in ascending order`);
    }

    arr2.clear();
    assertEQ(arr2.length, 0, "The length of the ArrayList should be 0 after clearing all elements");
    assertTrue(arr2.isEmpty(), "The ArrayList should be empty after clearing all elements");
    arr2.add(4);
    arr2.add(3);
    arr2.add(1);
    arr2.add(2);
    arr2.add(0);
    arr2.sort((a,b) => a - b);
    for (let i = 0; i < 5; i++) {
        assertEQ(arr2[i], i, `The element at index ${i} should be ${i} after re-sorting in ascending order`);
    }

    arr2.insert(50, 0);
    assertEQ(arr2[0], 50, "The first element should be 50 after inserting 50 at index 0");
    arr2.increaseCapacityTo(20);
    assertEQ(arr2.getCapacity(), 20, "The capacity of the ArrayList should be 20 after increasing capacity");

    arr2.trimToCurrentLength();
    assertEQ(arr2.getCapacity(), 6, "The capacity of the ArrayList should match its current length (6) after trimming");
}

function testArrayList0004() {
    let testList = new ArrayList<number>();
    expectThrow(() => {testList.removeByIndex(0)}, exceptionCheckEmptyContainer);
    expectThrow(() => {testList.removeByRange(0, 1)}, exceptionCheckEmptyContainer);
    expectThrow(() => {testList.subArrayList(0, 1)}, exceptionCheckEmptyContainer);
    testList.add(7);
    testList.add(9);
    assertEQ(testList[1], 9, "The element at index 1 should be 9 after adding 7 and 9");
}

function testArrayList0005() {
    let myArrayList = new ArrayList<number>();
    myArrayList.add(1);
    expectThrow(() => {myArrayList[2147483648]}, exceptionCheckType);
}

function testArrayList0006() {
    let test1 = new ArrayList<number>();
    for (let k = 0; k < 10; k++) {
        test1.add(k);
    }
    let keyName = "";
    let key: number = 0;
    for (key in test1) {
        keyName += key;
    }
    assertEQ(keyName, "0123456789", "The concatenated keys should match the sequence '0123456789'");
}

function testArrayList0007() {
    let myAl = new ArrayList<number>();
    myAl.add(1);
    myAl.add(2);
    myAl.add(3);
    myAl.insert(30, Math.floor(1.3));
    assertEQ(myAl[1], 30, "The element at index 1 should be 30 after insertion");
    myAl.removeByIndex(Math.floor(1.3));
    assertEQ(myAl[1], 2, "The element at index 1 should be 2 after removal");
}

function exceptionCheckEmptyContainer(e: Error | Exception): boolean | string {
    return (e instanceof BusinessError) && ((e as BusinessError).message == `Container is empty`) &&
        ((e as BusinessError).code == OutOfBoundsErrorCodeId);
}

function exceptionCheckType(e: Error | Exception): boolean | string {
    return (e instanceof BusinessError) && ((e as BusinessError).message ==
        `The type of \"index\" must be small integer.`) && ((e as BusinessError).code == TypeErrorCodeId);
}
