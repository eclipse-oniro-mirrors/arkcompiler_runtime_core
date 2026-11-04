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

/*---
    tags: [no-warmup]
---*/

const WORKER_COUNT: int = 16;
const ITERATIONS_PER_WORKER: int = 64;

function waitForStart(startFlag: Int32Array): void {
    while (Atomics.load(startFlag, 0) == 0) {
        Coroutine.Schedule();
    }
}

function assertComplexLocale(locale: Intl.Locale): void {
    arktest.assertEQ(locale.language, 'en');
    arktest.assertEQ(locale.region, 'US');
    arktest.assertEQ(locale.calendar, 'gregory');
    arktest.assertEQ(locale.numberingSystem, 'latn');
    arktest.assertEQ(locale.baseName, 'en-US');
}

function assertTaggedLocale(tagId: int, locale: Intl.Locale): void {
    if (tagId == 0) {
        arktest.assertEQ(locale.language, 'zh');
        arktest.assertEQ(locale.script, 'Hans');
        arktest.assertEQ(locale.region, 'CN');
        arktest.assertEQ(locale.baseName, 'zh-Hans-CN');
    } else if (tagId == 1) {
        arktest.assertEQ(locale.language, 'fr');
        arktest.assertEQ(locale.region, 'FR');
        arktest.assertEQ(locale.baseName, 'fr-FR');
    } else if (tagId == 2) {
        arktest.assertEQ(locale.language, 'de');
        arktest.assertEQ(locale.region, 'DE');
        arktest.assertEQ(locale.baseName, 'de-DE');
    } else {
        arktest.assertEQ(locale.language, 'ja');
        arktest.assertEQ(locale.region, 'JP');
        arktest.assertEQ(locale.baseName, 'ja-JP');
    }
}

function createTaggedLocale(tagId: int): Intl.Locale {
    if (tagId == 0) {
        return new Intl.Locale('zh-Hans-CN');
    }
    if (tagId == 1) {
        return new Intl.Locale('fr-FR');
    }
    if (tagId == 2) {
        return new Intl.Locale('de-DE');
    }
    return new Intl.Locale('ja-JP');
}

function sameTagWorker(startFlag: Int32Array): void {
    waitForStart(startFlag);
    for (let i = 0; i < ITERATIONS_PER_WORKER; i++) {
        assertComplexLocale(new Intl.Locale('en-US-u-ca-gregory-nu-latn'));
    }
}

async function testConcurrentSameTagCacheHits(): Promise<void> {
    assertComplexLocale(new Intl.Locale('en-US-u-ca-gregory-nu-latn'));

    let startFlag = new Int32Array(new ArrayBuffer(4));
    let promises: Promise<Any>[] = [];

    for (let i = 0; i < WORKER_COUNT; i++) {
        promises.push(taskpool.execute(sameTagWorker, startFlag));
    }

    Atomics.store(startFlag, 0, 1);
    await Promise.all(promises);
}

function testSequentialCacheReplacement(): void {
    for (let i = 0; i < WORKER_COUNT; i++) {
        for (let j = 0; j < ITERATIONS_PER_WORKER; j++) {
            let tagId = (i + j) % 4;
            assertTaggedLocale(tagId, createTaggedLocale(tagId));
        }
    }
}

function testLocaleOptionsStillOverrideCachedData(): void {
    let base = new Intl.Locale('zh-Hans-CN');
    arktest.assertEQ(base.language, 'zh');
    arktest.assertEQ(base.script, 'Hans');
    arktest.assertEQ(base.region, 'CN');

    let overridden = new Intl.Locale('zh-Hans-CN', { region: 'SG', calendar: 'gregory' } as Intl.LocaleOptions);
    arktest.assertEQ(overridden.language, 'zh');
    arktest.assertEQ(overridden.script, 'Hans');
    arktest.assertEQ(overridden.region, 'SG');
    arktest.assertEQ(overridden.calendar, 'gregory');
}

function mixedTagWorker(startFlag: Int32Array, workerId: Int32Array): void {
    waitForStart(startFlag);
    for (let i = 0; i < ITERATIONS_PER_WORKER; i++) {
        let tagId = (workerId[0] + i) % 4;
        assertTaggedLocale(tagId.toInt(), createTaggedLocale(tagId.toInt()));
    }
}

async function testConcurrentDifferentTagCacheReplacement(): Promise<void> {
    let startFlag = new Int32Array(new ArrayBuffer(4));
    let promises: Promise<Any>[] = [];

    for (let i = 0; i < WORKER_COUNT; i++) {
        let workerId = new Int32Array(new ArrayBuffer(4));
        workerId[0] = i;
        promises.push(taskpool.execute(mixedTagWorker, startFlag, workerId));
    }

    Atomics.store(startFlag, 0, 1);
    await Promise.all(promises);
}

function optionsWorker(startFlag: Int32Array, workerId: Int32Array): void {
    waitForStart(startFlag);
    for (let i = 0; i < ITERATIONS_PER_WORKER; i++) {
        let tagId = (workerId[0] % 3).toInt();
        if (tagId == 0) {
            let l = new Intl.Locale('en-US-u-ca-gregory-cu-cny', { hourCycle: 'h23' } as Intl.LocaleOptions);
            arktest.assertEQ(l.language, 'en');
            arktest.assertEQ(l.region, 'US');
            arktest.assertEQ(l.calendar, 'gregory');
            arktest.assertEQ(l.hourCycle, 'h23');
        } else if (tagId == 1) {
            let l = new Intl.Locale('zh-Hans-CN-u-co-pinyin-cu-cny', { numberingSystem: 'latn' } as Intl.LocaleOptions);
            arktest.assertEQ(l.language, 'zh');
            arktest.assertEQ(l.script, 'Hans');
            arktest.assertEQ(l.region, 'CN');
            arktest.assertEQ(l.collation, 'pinyin');
            arktest.assertEQ(l.numberingSystem, 'latn');
        } else {
            let l = new Intl.Locale('ja-JP', { calendar: 'japanese', hourCycle: 'h12' } as Intl.LocaleOptions);
            arktest.assertEQ(l.language, 'ja');
            arktest.assertEQ(l.region, 'JP');
            arktest.assertEQ(l.calendar, 'japanese');
            arktest.assertEQ(l.hourCycle, 'h12');
        }
    }
}

async function testConcurrentMixedTagsAndOptions(): Promise<void> {
    let startFlag = new Int32Array(new ArrayBuffer(4));
    let promises: Promise<Any>[] = [];

    for (let i = 0; i < WORKER_COUNT; i++) {
        let workerId = new Int32Array(new ArrayBuffer(4));
        workerId[0] = i;
        promises.push(taskpool.execute(optionsWorker, startFlag, workerId));
    }

    Atomics.store(startFlag, 0, 1);
    await Promise.all(promises);
}

function main(): int {
    let suite = new arktest.ArkTestsuite('IntlLocaleThreadSafety');
    suite.addAsyncTest('concurrent same tag cache hits', testConcurrentSameTagCacheHits);
    suite.addAsyncTest('concurrent different tag cache replacement', testConcurrentDifferentTagCacheReplacement);
    suite.addAsyncTest('concurrent mixed tags and options', testConcurrentMixedTagsAndOptions);
    suite.addTest('sequential cache replacement', testSequentialCacheReplacement);
    suite.addTest('locale options override cached data', testLocaleOptionsStillOverrideCachedData);

    let result = suite.run();
    CoroutineExtras.stopTaskpool();
    return result;
}