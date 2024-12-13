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
    const seed = 123;
    let strNumber = '';
    let result;
    

    function generateNumber(seed) {
        const modulus = Math.pow(2, 32);
        const a = 1664525;
        const c = 1013904223;
      
        seed = (a * seed + c) % modulus;
        
        return Math.floor((seed / modulus) * 100); 
      }
    
    function decimalStringToNumberToString(str) {
        const res = Number(str);
        return String(res);
    } 

    const data = generateNumber(seed);
    strNumber = data.toString(10);

    const start = process.hrtime.bigint();

    for (let i = 0; i < 10000; i++) {
        result = decimalStringToNumberToString(strNumber);
    }
    const end = process.hrtime.bigint();
    timing = end - start;
    console.log('Benchmark result: Decimal ' + timing);

    return null;
}

main();