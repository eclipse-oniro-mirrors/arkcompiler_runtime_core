/*
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

function assertWithLog(condition: boolean, testName: string): void {
    console.log(testName + (condition ? ' success!' : ' failed!'));
    arktest.assertTrue(condition);
}

function extraModule(): ESValue {
    return ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
}

async function expectNumberPromise(
    name: string, functionName: string, expected: number, args: ESValue[] = []): Promise<void> {
    const method = extraModule().getProperty(functionName);
    const result = await method.invoke(...args).toPromise();
    assertWithLog(result.toNumber() === expected, name);
}

async function expectStringRejection(name: string, functionName: string, expected: string): Promise<void> {
    let caught = false;
    try {
        await extraModule().getProperty(functionName).invoke().toPromise();
    } catch (error) {
        caught = (error as ESError).getValue().toString() === expected;
    }
    assertWithLog(caught, name);
}

async function testResolvePrimitiveValues(): Promise<void> {
    const module = extraModule();
    const text = await module.getProperty('resolveText')
        .invoke(ESValue.wrapString('interop-resolved'))
        .toPromise();
    assertWithLog(text.toString() === 'interop-resolved', 'resolve string preserves value');

    const bool = await module.getProperty('resolveBoolean')
        .invoke(ESValue.wrapBoolean(false)).toPromise();
    assertWithLog(!bool.toBoolean(), 'resolve false preserves boolean');

    const numericValue = await module.getProperty('resolveNumber')
        .invoke(ESValue.wrapNumber(-17.25)).toPromise();
    assertWithLog(numericValue.toNumber() === -17.25,
        'resolve negative fraction preserves number');

    const nullValue = await module.getProperty('resolveNull').invoke().toPromise();
    assertWithLog(nullValue.isNull(), 'resolve null preserves null');

    const undefinedValue = await module.getProperty('resolveUndefined').invoke().toPromise();
    assertWithLog(undefinedValue.isUndefined(), 'resolve undefined preserves undefined');
}

async function testResolveObjectAndArray(): Promise<void> {
    const module = extraModule();
    const objectValue = await module.getProperty('resolveObject')
        .invoke(ESValue.wrapNumber(39)).toPromise();
    const fieldValue = objectValue.getProperty('value');
    const labelValue = objectValue.getProperty('label');
    assertWithLog(fieldValue.toNumber() === 39,
        'resolve object number property');
    assertWithLog(labelValue.toString() === 'resolved-object',
        'resolve object string property');

    const arrayValue = await module.getProperty('resolveArray')
        .invoke(ESValue.wrapNumber(4), ESValue.wrapNumber(9)).toPromise();
    const first = arrayValue.getProperty('0');
    const second = arrayValue.getProperty('1');
    assertWithLog(first.toNumber() === 4 && second.toNumber() === 9,
        'resolve array indexed values');
}

async function testThenableAssimilation(): Promise<void> {
    const result = await extraModule().getProperty('resolveThenable')
        .invoke(ESValue.wrapNumber(81)).toPromise();
    assertWithLog(result.toNumber() === 81,
        'resolve assimilates thenable result');

    const nested = await extraModule().getProperty('resolveNestedObject')
        .invoke(ESValue.wrapNumber(63)).toPromise();
    assertWithLog(nested.getProperty('nested').toNumber() === 63,
        'resolve flattens nested promise');
}

async function testRejectAndRecover(): Promise<void> {
    let caughtText = false;
    try {
        await extraModule().getProperty('rejectText')
            .invoke(ESValue.wrapString('explicit-reason')).toPromise();
    } catch (error) {
        caughtText = (error as ESError).getValue().toString() === 'explicit-reason';
    }
    assertWithLog(caughtText,
        'reject string transfers reason');

    const recovered = await extraModule().getProperty('rejectThenRecover')
        .invoke(ESValue.wrapNumber(14)).toPromise();
    assertWithLog(recovered.toNumber() === 42,
        'catch callback recovers rejected promise');
}

async function testFulfillmentChain(): Promise<void> {
    const result = await extraModule().getProperty('chainWithFinally')
        .invoke(ESValue.wrapNumber(12)).toPromise();
    assertWithLog(result.toNumber() === 17,
        'finally fulfillment continues to then');

    const continued = await extraModule().getProperty('chainRecoverAndContinue')
        .invoke(ESValue.wrapNumber(8)).toPromise();
    assertWithLog(continued.toNumber() === 15,
        'recovery value reaches later callbacks');

    const sum = await extraModule().getProperty('parallelResolveSum')
        .invoke(ESValue.wrapNumber(23), ESValue.wrapNumber(19)).toPromise();
    assertWithLog(sum.toNumber() === 42,
        'all fulfillment values reach chained callback');
}

async function testRejectChainPropagation(): Promise<void> {
    await expectStringRejection(
        'catch returning rejected promise propagates reason', 'chainRejectAgain',
        'second-rejection');
    await expectStringRejection(
        'all rejection preserves first reason', 'parallelRejectReason', 'parallel-reason');
    await expectStringRejection(
        'finally preserves rejected promise reason', 'finallyRejectPreservesReason',
        'preserved-reason');

    let callbackError = false;
    try {
        await extraModule().getProperty('callbackThrows')
            .invoke(ESValue.wrapNumber(1)).toPromise();
    } catch (error) {
        callbackError = (error as Error).message === 'callback-threw';
    }
    assertWithLog(callbackError,
        'throw in fulfillment callback rejects next promise');
}

async function testRaceAndSettledResults(): Promise<void> {
    const race = await extraModule().getProperty('raceResolvedAndRejected')
        .invoke(ESValue.wrapNumber(57)).toPromise();
    assertWithLog(race.toNumber() === 57,
        'race returns immediate fulfillment');

    const settled = await extraModule().getProperty('settledMixedValues').invoke().toPromise();
    const fulfilled = settled.getProperty('0');
    const rejected = settled.getProperty('1');
    assertWithLog(fulfilled.getProperty('status').toString() === 'fulfilled',
        'allSettled fulfillment status');
    assertWithLog(fulfilled.getProperty('value').toNumber() === 7,
        'allSettled fulfillment payload');
    assertWithLog(rejected.getProperty('status').toString() === 'rejected',
        'allSettled rejection status');
    assertWithLog(rejected.getProperty('reason').toString() === 'settled-reason',
        'allSettled rejection payload');
}

async function testCallbackOrdering(): Promise<void> {
    const callback = (step: number): void => {
        const expected = step === 1 ? 1 : 2;
        assertWithLog(step === expected,
            'chain callback order ' + step.toString());
    };
    const result = await extraModule().getProperty('callbackOrder')
        .invoke(ESValue.wrapNumber(100), ESValue.wrap(callback)).toPromise();
    assertWithLog(result.toNumber() === 102,
        'ordered callbacks transform fulfillment');
}

async function testSingleSettlementAndPassThrough(): Promise<void> {
    const module = extraModule();
    const resolved = await module.getProperty('resolveWinsSettlementRace').invoke().toPromise();
    assertWithLog(resolved.toNumber() === 31,
        'first resolve wins settlement race');

    await expectStringRejection(
        'first reject wins settlement race', 'rejectWinsSettlementRace',
        'first-settlement-wins');

    const passed = await module.getProperty('passThroughChain')
        .invoke(ESValue.wrapNumber(73)).toPromise();
    assertWithLog(passed.toNumber() === 73,
        'missing fulfillment handlers pass value through');
}

export async function runAdditionalPromiseTests(): Promise<void> {
    await testResolvePrimitiveValues();
    await testResolveObjectAndArray();
    await testThenableAssimilation();
    await testRejectAndRecover();
    await testFulfillmentChain();
    await testRejectChainPropagation();
    await testRaceAndSettledResults();
    await testCallbackOrdering();
    await testSingleSettlementAndPassThrough();
}

async function checkToPromise(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let sleepRetNumber = module.getProperty('sleepRetNumber');
    let p = sleepRetNumber.invoke(ESValue.wrapNumber(5000)).toPromise();
    let res = await p;
    assertWithLog(res.toNumber() === 0xcafe, 'checkToPromise');
}

async function checkToNumber(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let getPromiseNumber = module.getProperty('getPromiseNumber');
    let p = getPromiseNumber.invoke().toPromise();
    let res = false;
    await p.then((value: ESValue) => {
        res = value.toNumber() === 42;
    }).catch((e: Error) => {
        res = false;
    });
    assertWithLog(res, 'checkToNumber');
}

async function checkToString(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let getPromiseString = module.getProperty('getPromiseString');
    let p = getPromiseString.invoke().toPromise();
    let res = false;
    await p.then((value: ESValue) => {
        res = value.toString() === ' abc ';
    }).catch((e: Error) => {
        res = false;
    });
    assertWithLog(res, 'checkToString');
}

async function checkToBoolean(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let getPromiseBoolean = module.getProperty('getPromiseBoolean');
    let p = getPromiseBoolean.invoke().toPromise();
    let res = false;
    await p.then((value: ESValue) => {
        res = value.toBoolean();
    }).catch((e: Error) => {
        res = false;
    });
    assertWithLog(res, 'checkToBoolean');
}

async function checkToBigInt(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let getPromiseBigInt = module.getProperty('getPromiseBigInt');
    let p = getPromiseBigInt.invoke().toPromise();
    let res = false;
    await p.then((value: ESValue) => {
        res = value.toBigInt() === 123456789n;
    }).catch((e: Error) => {
        res = false;
    });
    assertWithLog(res, 'checkToBigInt');
}

function testInteropRejectStringCatch(): int {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let func = module.getProperty('asyncFunctionRejectAndFinallyString')
    let rejectedPromise = func.invoke().toPromise()
    rejectedPromise.catch((res: Error): void => {
        if ((res as ESError).getValue().toString() === 'MyError') {
            console.log('reject catch success!');
        } else {
            console.log('reject catch failed!');
            console.log(res.message)
        }
    })
    return 0
}

function testInteropRejectStringThen(): int {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let func = module.getProperty('asyncFunctionRejectAndFinallyString')
    let rejectedPromise = func.invoke().toPromise()
    rejectedPromise.then(
        (value): void => { },
        (res: Error): void => {
            if ((res as ESError).getValue().toString() === 'MyError') {
                console.log('reject then success!');
            } else {
                console.log('reject then failed!');
                console.log(res.message)
            }
        });
    return 0
}

function testInteropFinally(): int {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let func = module.getProperty('asyncFunctionRejectAndFinallyString')
    let rejectedPromise = func.invoke().toPromise()
    rejectedPromise.finally((): void => {
        console.log('reject finally success!');
    });
    return 0
}

async function testInteropAll(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let asyncArrayFunctionBoolean = module.getProperty('asyncArrayFunctionBoolean');
    let asyncArrayFunctionBoolean2 = module.getProperty('asyncArrayFunctionBoolean2');
    const results = await Promise.all([asyncArrayFunctionBoolean.invoke().toPromise(), asyncArrayFunctionBoolean2.invoke().toPromise()]);
    assertWithLog(results[0].toBoolean() && !results[1].toBoolean(), 'testInteropAll');
    arktest.assertTrue(results[0].toBoolean());
    arktest.assertTrue(!results[1].toBoolean());
}

async function checkNestedPromise(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let nestedPromiseFunc = module.getProperty('nestedPromise');
    let p = nestedPromiseFunc.invoke().toPromise();
    let res = await p;
    assertWithLog(res.toNumber() === 42, 'checkNestedPromise');
}

async function checkRaceTest(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let raceFunc = module.getProperty('raceTest');
    let p = raceFunc.invoke().toPromise();
    let res = await p;
    assertWithLog(res.toNumber() === 2, 'checkRaceTest');
}

async function checkAllSettledTest(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let allSettledFunc = module.getProperty('allSettledTest');
    let p = allSettledFunc.invoke().toPromise();
    let res = await p;
    let valid = res.getProperty('length').toNumber() === 2;
    let firstResult = res.getProperty('0');
    valid = valid && firstResult.getProperty('status').toString() === 'fulfilled';
    valid = valid && firstResult.getProperty('value').toNumber() === 1;
    let secondResult = res.getProperty('1');
    valid = valid && secondResult.getProperty('status').toString() === 'rejected';
    valid = valid && secondResult.getProperty('reason').toString() === 'error';
    assertWithLog(valid, 'checkAllSettledTest');
    arktest.assertTrue(res.getProperty('length').toNumber() === 2);
    arktest.assertTrue(firstResult.getProperty('status').toString() === 'fulfilled');
    arktest.assertTrue(firstResult.getProperty('value').toNumber() === 1);
    arktest.assertTrue(secondResult.getProperty('status').toString() === 'rejected');
    arktest.assertTrue(secondResult.getProperty('reason').toString() === 'error');
}

async function checkChainTest(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let chainFunc = module.getProperty('chainTest');
    let p = chainFunc.invoke().toPromise();
    let res = await p;
    assertWithLog(res.toNumber() === 4, 'checkChainTest');
}

async function checkObjectPromise(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let objectPromiseFunc = module.getProperty('objectPromise');
    let p = objectPromiseFunc.invoke().toPromise();
    let res = await p;
    let valid = res.isObject();
    let name = res.getProperty('name');
    let count = res.getProperty('count');
    valid = valid && name.toString() === 'test';
    valid = valid && count.toNumber() === 42;
    assertWithLog(valid, 'checkObjectPromise');
    arktest.assertTrue(res.isObject());
    arktest.assertTrue(name.toString() === 'test');
    arktest.assertTrue(count.toNumber() === 42);
}

async function checkArrayPromise(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let arrayPromiseFunc = module.getProperty('arrayPromise');
    let p = arrayPromiseFunc.invoke().toPromise();
    let res = await p;
    assertWithLog(res.getProperty('length').toNumber() === 3, 'checkArrayPromise');
}

async function checkNanPromise(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let nanPromiseFunc = module.getProperty('nanPromise');
    let p = nanPromiseFunc.invoke().toPromise();
    let res = await p;
    assertWithLog(res.isNumber() && isNaN(res.toNumber()), 'checkNanPromise');
    arktest.assertTrue(res.isNumber());
    arktest.assertTrue(isNaN(res.toNumber()));
}

async function checkInfinityPromise(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let infinityPromiseFunc = module.getProperty('infinityPromise');
    let p = infinityPromiseFunc.invoke().toPromise();
    let res = await p;
    assertWithLog(res.isNumber() && res.toNumber() === Infinity, 'checkInfinityPromise');
    arktest.assertTrue(res.isNumber());
    arktest.assertTrue(res.toNumber() === Infinity);
}

async function checkNullPromise(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let nullPromiseFunc = module.getProperty('nullPromise');
    let p = nullPromiseFunc.invoke().toPromise();
    let res = await p;
    assertWithLog(res.isNull(), 'checkNullPromise');
}

async function checkUndefinedPromise(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let undefinedPromiseFunc = module.getProperty('undefinedPromise');
    let p = undefinedPromiseFunc.invoke().toPromise();
    let res = await p;
    assertWithLog(res.isUndefined(), 'checkUndefinedPromise');
}

async function checkThrowTypeError(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let throwFunc = module.getProperty('throwError');
    let p = throwFunc.invoke().toPromise();
    let res = false;
    await p.catch((e: Error) => {
        res = e.message.includes('Test error');
    });
    assertWithLog(res, 'checkThrowTypeError');
}

async function checkThrowNullError(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let throwFunc = module.getProperty('throwNullError');
    let p = throwFunc.invoke().toPromise();
    let res = false;
    await p.catch((e: Error) => {
        e = e as ESError;
        res = e.getValue().isNull();
    });
    assertWithLog(res, 'checkThrowNullError');
}

async function checkAwaitCatch(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let awaitCatchFunc = module.getProperty('awaitCatch');
    let p = awaitCatchFunc.invoke().toPromise();
    let res = await p;
    assertWithLog(res.toNumber() === 1, 'checkAwaitCatch');
}

async function checkTimeoutSuccess(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let timeoutFunc = module.getProperty('timeoutSuccessTest');
    let p = timeoutFunc.invoke().toPromise();
    let res = await p;
    assertWithLog(res.toNumber() === 42, 'checkTimeoutSuccess');
}

async function checkTimeoutFail(): Promise<void> {
    let module = ESValue.load(
        '../../plugins/ets/tests/interop_js/tests/test_esvalue_to_promise/esvalue_to_promise_test');
    let timeoutFunc = module.getProperty('timeoutFailTest');
    let p = timeoutFunc.invoke().toPromise();
    let res = false;
    await p.catch((e: Error) => {
        e = e as ESError;
        res = e.getValue().toString() === 'timeout';
    });
    assertWithLog(res, 'checkTimeoutFail');
}
