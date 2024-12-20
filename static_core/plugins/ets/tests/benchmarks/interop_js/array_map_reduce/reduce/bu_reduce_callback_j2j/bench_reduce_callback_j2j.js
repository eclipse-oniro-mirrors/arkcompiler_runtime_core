/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

function main() {

    const min = 0;
    const max = 9999999;
    const arrayLength = 100;
    let testArray = [];
    let sumArray;

    function generateRandomNumber(min, max) {
        return Math.floor(Math.random() * (max - min + 1)) + min;
    }

    function generateRandomArray(length, min, max, arr ) {
        for (let i = 0; i < length; i++) {
            const randomNumber = Math.floor(Math.random() * (max - min + 1)) + min;
            arr.push(randomNumber);
        }
    }

    function sumFunction(acc, curr) {
        return acc + curr;
    }

    generateRandomArray(arrayLength, min, max, testArray);

    const start = process.hrtime.bigint();

    for (let i = 0; i < 1000; i++) {
        sumArray = testArray.reduce(sumFunction, 0);
    }
    const end = process.hrtime.bigint();
    timing = end - start;
    console.log('Benchmark result: reduce_callback_j2j ' + timing);

    return null;
}

main();
