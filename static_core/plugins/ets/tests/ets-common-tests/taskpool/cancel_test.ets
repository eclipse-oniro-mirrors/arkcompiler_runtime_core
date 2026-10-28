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

import { CoroutineExtras, AtomicFlag } from 'std/debug/concurrency';

class Event {
    constructor() {
        this.promise = new Promise<boolean>((resolve: (val: boolean) => void) => {
            this.resolveFn = resolve;
        })
        this.job = new CompletableJob<boolean>();
        this.promise.then((result: boolean) => {
            this.job.finish(result);
        }).catch((e: Error) => {
            this.job.fail(e);
        });
    }

    public wait(): boolean {
        return this.job.Await();
    }

    public fire(val: boolean = true) {
        this.resolveFn!(val);
    }

    private promise: Promise<boolean>;
    private resolveFn: ((val: boolean) => void) | null = null;
    private job: CompletableJob<boolean>;
}

function sleep(duration: int): Promise<void> {
    return new Promise<void>((resolve) => {
        setTimeout(() => {
            resolve(undefined);
        }, duration);
    });
}

function blockRunnerTask(started: AtomicFlag, release: AtomicFlag): string {
    started.set(true);
    while (!release.get()) {
    }
    return 'first';
}

async function waitForFlag(flag: AtomicFlag, timeoutMs: number): Promise<boolean> {
    const deadline = Date.now() + timeoutMs;
    while (!flag.get() && Date.now() < deadline) {
        await sleep(1);
    }
    return flag.get();
}

async function cancelByTaskIdTest(){
    let event = new Event();
    let event2 = new Event();
    let taskFunc = async (duration: int): Promise<string> => {
        event.fire();
        while (!taskpool.Task.isCanceled()) {
            await sleep(10);
        }
        return 'canceled';
    }

    let task = new taskpool.Task(taskFunc, 200);
    let p = taskpool.execute(task)
    event.wait();
    taskpool.cancel(task.taskId);
    arktest.assertEQ(await p, 'canceled');
}

async function cancelDelayedTaskByTaskIdTest(): Promise<void> {
    let task = new taskpool.Task(async (): Promise<string> => {
        await sleep(10);
        return 'executed';
    });
    let promise = taskpool.executeDelayed(1000, task);

    taskpool.cancel(task.taskId);

    let canceled = false;
    try {
        await promise;
    } catch (e) {
        canceled = true;
        arktest.assertEQ(e.message, 'taskpool:: task has been canceled');
    }
    arktest.assertTrue(canceled);
}

async function cancelSequenceRunnerWaitingTaskByTaskIdTest(): Promise<void> {
    const startTimeoutMs: number = 1000.0;
    let runner = new taskpool.SequenceRunner();
    let firstStarted = new AtomicFlag(false);
    let releaseFirst = new AtomicFlag(false);
    let firstTask = new taskpool.Task(blockRunnerTask, firstStarted, releaseFirst);
    let waitingTask = new taskpool.Task((): string => 'second');
    let firstPromise = runner.execute(firstTask);
    let waitingPromise = runner.execute(waitingTask);

    try {
        arktest.assertTrue(
            await waitForFlag(firstStarted, startTimeoutMs),
            'the first SequenceRunner task should start before canceling the waiting task'
        );
        taskpool.cancel(waitingTask.taskId);
    } finally {
        releaseFirst.set(true);
    }

    let canceled = false;
    try {
        await waitingPromise;
    } catch (e) {
        canceled = true;
        arktest.assertEQ(e.message, 'taskpool:: sequenceRunner task has been canceled');
    }
    arktest.assertTrue(canceled);
    arktest.assertEQ(await firstPromise, 'first');
}

async function cancelAsyncRunnerWaitingTaskByTaskIdTest(): Promise<void> {
    const startTimeoutMs: number = 1000.0;
    let runner = new taskpool.AsyncRunner(1);
    let firstStarted = new AtomicFlag(false);
    let releaseFirst = new AtomicFlag(false);
    let firstTask = new taskpool.Task(blockRunnerTask, firstStarted, releaseFirst);
    let waitingTask = new taskpool.Task((): string => 'second');
    let firstPromise = runner.execute(firstTask);
    let waitingPromise = runner.execute(waitingTask);

    try {
        arktest.assertTrue(
            await waitForFlag(firstStarted, startTimeoutMs),
            'the first AsyncRunner task should start before canceling the waiting task'
        );
        taskpool.cancel(waitingTask.taskId);
    } finally {
        releaseFirst.set(true);
    }

    let canceled = false;
    try {
        await waitingPromise;
    } catch (e) {
        canceled = true;
        arktest.assertEQ(e.message, 'taskpool:: asyncRunner task has been canceled');
    }
    arktest.assertTrue(canceled);
    arktest.assertEQ(await firstPromise, 'first');
}

function main(): int {
    let commonTaskSuite = new arktest.ArkTestsuite('taskpool.CancelTest');
    commonTaskSuite.addAsyncTest('cancelByTaskIdTest', cancelByTaskIdTest);
    commonTaskSuite.addAsyncTest('cancelDelayedTaskByTaskIdTest', cancelDelayedTaskByTaskIdTest);
    commonTaskSuite.addAsyncTest(
        'cancelSequenceRunnerWaitingTaskByTaskIdTest', cancelSequenceRunnerWaitingTaskByTaskIdTest);
    commonTaskSuite.addAsyncTest(
        'cancelAsyncRunnerWaitingTaskByTaskIdTest', cancelAsyncRunnerWaitingTaskByTaskIdTest);

    let res = commonTaskSuite.run();
    CoroutineExtras.stopTaskpool();
    return res;
}
