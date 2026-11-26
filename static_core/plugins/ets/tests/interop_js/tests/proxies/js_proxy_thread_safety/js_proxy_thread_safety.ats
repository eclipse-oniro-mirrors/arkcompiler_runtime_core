/**
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

let eaw = new EAWorker(true);

const MODULE_NAME =
    '../../plugins/ets/tests/interop_js/tests/proxies/js_proxy_thread_safety/js_proxy_thread_safety_helpers';

function workerCallback(): boolean {
    let module = ESValue.load(MODULE_NAME);
    let getGreeterFn = module.getProperty('getGreeter');
    let greeter = getGreeterFn.invoke();
    let greetFn = module.getProperty('greet');
    let res = greetFn.invoke(greeter, ESValue.wrapString('worker'));
    return res.toString() === 'Hello, worker!';
}

function testConcurrentInterfaceProxyConversion(): void {
    // Warm up the module on the main coroutine first.
    let module = ESValue.load(MODULE_NAME);
    let getGreeterFn = module.getProperty('getGreeter');
    let greeter = getGreeterFn.invoke();
    let greetFn = module.getProperty('greet');
    let mainRes = greetFn.invoke(greeter, ESValue.wrapString('main')).toString();
    arktest.assertEQ(mainRes, 'Hello, main!');

    // Concurrently trigger the same conversion path from a worker.
    let workerRes = eaw.run<boolean>(workerCallback).Await() as boolean;
    arktest.assertTrue(workerRes);

    // Main-coroutine proxies must keep working after the worker round.
    let mainRes2 = greetFn.invoke(greeter, ESValue.wrapString('main2')).toString();
    arktest.assertEQ(mainRes2, 'Hello, main2!');
}

function jsProxyThreadSafetyTest(): boolean {
    eaw.start();
    let suite = new arktest.ArkTestsuite('JSProxy.ThreadSafety');
    suite.addTest('testConcurrentInterfaceProxyConversion', testConcurrentInterfaceProxyConversion);
    const res = suite.run();
    eaw.join();
    return res == 0;
}
