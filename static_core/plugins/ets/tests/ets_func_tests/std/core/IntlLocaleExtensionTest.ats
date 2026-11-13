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

function testKnownExtensionsPreserved(): void {
    {
        let l = new Intl.Locale('en-US-u-ca-gregory');
        arktest.assertEQ(l.toString(), 'en-US-u-ca-gregory');
        arktest.assertEQ(l.calendar, 'gregory');
    }
    {
        let l = new Intl.Locale('en-US-u-co-emoji');
        arktest.assertEQ(l.toString(), 'en-US-u-co-emoji');
        arktest.assertEQ(l.collation, 'emoji');
    }
    {
        let l = new Intl.Locale('en-US-u-hc-h12');
        arktest.assertEQ(l.toString(), 'en-US-u-hc-h12');
        arktest.assertEQ(l.hourCycle, 'h12');
    }
    {
        let l = new Intl.Locale('en-US-u-kf-upper');
        arktest.assertEQ(l.toString(), 'en-US-u-kf-upper');
        arktest.assertEQ(l.caseFirst, 'upper');
    }
    {
        let l = new Intl.Locale('en-US-u-kn');
        arktest.assertEQ(l.toString(), 'en-US-u-kn');
        arktest.assertTrue(l.numeric as boolean);
    }
    {
        let l = new Intl.Locale('en-US-u-nu-latn');
        arktest.assertEQ(l.toString(), 'en-US-u-nu-latn');
        arktest.assertEQ(l.numberingSystem, 'latn');
    }
}

function testUnknownExtensionsPreserved(): void {
    {
        let l = new Intl.Locale('en-US-u-cu-cny');
        arktest.assertEQ(l.toString(), 'en-US-u-cu-cny');
    }
    {
        let l = new Intl.Locale('en-US-u-tz-uschi');
        arktest.assertEQ(l.toString(), 'en-US-u-tz-uschi');
    }
    {
        let l = new Intl.Locale('en-US-u-fw-mon');
        arktest.assertEQ(l.toString(), 'en-US-u-fw-mon');
    }
    {
        let l = new Intl.Locale('en-US-u-cu-eur');
        arktest.assertEQ(l.toString(), 'en-US-u-cu-eur');
    }
}

function testMixedKnownUnknownExtensions(): void {
    {
        let l = new Intl.Locale('en-US-u-ca-gregory-cu-cny');
        arktest.assertEQ(l.toString(), 'en-US-u-ca-gregory-cu-cny');
        arktest.assertEQ(l.calendar, 'gregory');
    }
    {
        let l = new Intl.Locale('en-US-u-cu-cny-nu-latn');
        arktest.assertEQ(l.toString(), 'en-US-u-cu-cny-nu-latn');
        arktest.assertEQ(l.numberingSystem, 'latn');
    }
    {
        let l = new Intl.Locale('en-US-u-hc-h12-cu-jpy-tz-jptyo');
        arktest.assertEQ(l.toString(), 'en-US-u-cu-jpy-hc-h12-tz-jptyo');
        arktest.assertEQ(l.hourCycle, 'h12');
    }
    {
        let l = new Intl.Locale('zh-Hans-CN-u-co-pinyin-cu-cny');
        arktest.assertEQ(l.toString(), 'zh-Hans-CN-u-co-pinyin-cu-cny');
        arktest.assertEQ(l.collation, 'pinyin');
    }
}

function testCaseNormalization(): void {
    {
        let l = new Intl.Locale('EN-gb-u-cu-cny');
        arktest.assertEQ(l.toString(), 'en-GB-u-cu-cny');
        arktest.assertEQ(l.language, 'en');
        arktest.assertEQ(l.region, 'GB');
    }
    {
        let l = new Intl.Locale('EN-us-U-CA-GREGORY');
        arktest.assertEQ(l.toString(), 'en-US-u-ca-gregory');
        arktest.assertEQ(l.language, 'en');
        arktest.assertEQ(l.region, 'US');
    }
    {
        let l = new Intl.Locale('ZH-hans-CN-u-CO-PINYIN');
        arktest.assertEQ(l.toString(), 'zh-Hans-CN-u-co-pinyin');
        arktest.assertEQ(l.language, 'zh');
        arktest.assertEQ(l.script, 'Hans');
        arktest.assertEQ(l.region, 'CN');
    }
}

function testOptionsOverrideRebuildsTag(): void {
    {
        let l = new Intl.Locale('en-US-u-nu-latn', { calendar: 'gregory' } as Intl.LocaleOptions);
        arktest.assertEQ(l.toString(), 'en-US-u-ca-gregory-nu-latn');
        arktest.assertEQ(l.calendar, 'gregory');
        arktest.assertEQ(l.numberingSystem, 'latn');
    }
    {
        let l = new Intl.Locale('en-US-u-ca-gregory', { collation: 'emoji' } as Intl.LocaleOptions);
        arktest.assertEQ(l.toString(), 'en-US-u-ca-gregory-co-emoji');
    }
    {
        let l = new Intl.Locale('en-US', { region: 'GB' } as Intl.LocaleOptions);
        arktest.assertEQ(l.toString(), 'en-GB');
        arktest.assertEQ(l.region, 'GB');
    }
    {
        let l = new Intl.Locale('en-US-u-hc-h12', { hourCycle: 'h23' } as Intl.LocaleOptions);
        arktest.assertEQ(l.toString(), 'en-US-u-hc-h23');
        arktest.assertEQ(l.hourCycle, 'h23');
    }
    {
        let l = new Intl.Locale('en-US-u-kn', { calendar: 'gregory' } as Intl.LocaleOptions);
        arktest.assertEQ(l.toString(), 'en-US-u-ca-gregory-kn');
        arktest.assertTrue(l.numeric as boolean);
    }
    {
        let l = new Intl.Locale('en-US-u-kf-upper', { calendar: 'gregory' } as Intl.LocaleOptions);
        arktest.assertEQ(l.toString(), 'en-US-u-ca-gregory-kf-upper');
        arktest.assertEQ(l.caseFirst, 'upper');
    }
}

function testOptionsPreserveUnknownKeys(): void {
    {
        let l = new Intl.Locale('en-US-u-ca-gregory-cu-cny', { calendar: 'buddhist' } as Intl.LocaleOptions);
        arktest.assertEQ(l.toString(), 'en-US-u-ca-buddhist-cu-cny');
        arktest.assertEQ(l.calendar, 'buddhist');
    }
    {
        let l = new Intl.Locale('en-US-u-cu-cny-tz-uschi', { calendar: 'gregory' } as Intl.LocaleOptions);
        arktest.assertEQ(l.toString(), 'en-US-u-ca-gregory-cu-cny-tz-uschi');
    }
    {
        let l = new Intl.Locale('en-US-u-fw-mon-cu-eur', { collation: 'emoji' } as Intl.LocaleOptions);
        arktest.assertEQ(l.toString(), 'en-US-u-co-emoji-cu-eur-fw-mon');
    }
    {
        let l = new Intl.Locale('en-US-u-cu-cny', { hourCycle: 'h23' } as Intl.LocaleOptions);
        arktest.assertEQ(l.toString(), 'en-US-u-cu-cny-hc-h23');
    }
    {
        let l = new Intl.Locale('en-US-u-tz-uschi-cu-eur', { numberingSystem: 'latn' } as Intl.LocaleOptions);
        arktest.assertEQ(l.toString(), 'en-US-u-cu-eur-nu-latn-tz-uschi');
    }
}

function testMultipleKnownExtensions(): void {
    {
        let l = new Intl.Locale('en-US-u-ca-gregory-co-emoji-hc-h12-kf-upper-nu-latn');
        arktest.assertEQ(l.toString(), 'en-US-u-ca-gregory-co-emoji-hc-h12-kf-upper-nu-latn');
        arktest.assertEQ(l.calendar, 'gregory');
        arktest.assertEQ(l.collation, 'emoji');
        arktest.assertEQ(l.hourCycle, 'h12');
        arktest.assertEQ(l.caseFirst, 'upper');
        arktest.assertEQ(l.numberingSystem, 'latn');
    }
    {
        let l = new Intl.Locale('ja-JP-u-ca-japanese-hc-h12-nu-latn');
        arktest.assertEQ(l.toString(), 'ja-JP-u-ca-japanese-hc-h12-nu-latn');
        arktest.assertEQ(l.calendar, 'japanese');
        arktest.assertEQ(l.hourCycle, 'h12');
        arktest.assertEQ(l.numberingSystem, 'latn');
    }
}

function testBooleanExtensionKn(): void {
    {
        let l = new Intl.Locale('en-US-u-kn');
        arktest.assertEQ(l.toString(), 'en-US-u-kn');
        arktest.assertTrue(l.numeric as boolean);
    }
    {
        let l = new Intl.Locale('en-US-u-kn-true');
        arktest.assertEQ(l.toString(), 'en-US-u-kn');
        arktest.assertTrue(l.numeric as boolean);
    }
    {
        let l = new Intl.Locale('en-US-u-kn-false');
        arktest.assertEQ(l.toString(), 'en-US-u-kn-false');
        arktest.assertFalse(l.numeric as boolean);
    }
}

function testHourCyclePreserved(): void {
    {
        let l = new Intl.Locale('ja-JP-u-ca-japanese-hc-h12');
        arktest.assertEQ(l.toString(), 'ja-JP-u-ca-japanese-hc-h12');
        arktest.assertEQ(l.hourCycle, 'h12');
    }
    {
        let l = new Intl.Locale('de-DE-u-hc-h23');
        arktest.assertEQ(l.toString(), 'de-DE-u-hc-h23');
        arktest.assertEQ(l.hourCycle, 'h23');
    }
    {
        let l = new Intl.Locale('en-US-u-hc-h11');
        arktest.assertEQ(l.toString(), 'en-US-u-hc-h11');
        arktest.assertEQ(l.hourCycle, 'h11');
    }
    {
        let l = new Intl.Locale('en-US-u-hc-h24');
        arktest.assertEQ(l.toString(), 'en-US-u-hc-h24');
        arktest.assertEQ(l.hourCycle, 'h24');
    }
}

function testExtensionOrderPreserved(): void {
    {
        let l = new Intl.Locale('en-US-u-ca-gregory-nu-latn');
        arktest.assertEQ(l.toString(), 'en-US-u-ca-gregory-nu-latn');
    }
    {
        let l = new Intl.Locale('en-US-u-nu-latn-ca-gregory');
        arktest.assertEQ(l.toString(), 'en-US-u-ca-gregory-nu-latn');
    }
    {
        let l = new Intl.Locale('en-US-u-cu-cny-ca-gregory');
        arktest.assertEQ(l.toString(), 'en-US-u-ca-gregory-cu-cny');
    }
}

function testBaseNameWithoutExtensions(): void {
    {
        let l = new Intl.Locale('en-US-u-ca-gregory-cu-cny');
        arktest.assertEQ(l.baseName, 'en-US');
    }
    {
        let l = new Intl.Locale('zh-Hans-CN-u-co-pinyin-cu-cny');
        arktest.assertEQ(l.baseName, 'zh-Hans-CN');
    }
    {
        let l = new Intl.Locale('ja-JP-u-ca-japanese-hc-h12');
        arktest.assertEQ(l.baseName, 'ja-JP');
    }
    {
        let l = new Intl.Locale('en-u-cu-eur');
        arktest.assertEQ(l.baseName, 'en');
    }
}

function testPrivateUseTags(): void {
    {
        let l = new Intl.Locale('en-US-u-cu-cny-x-foo');
        arktest.assertEQ(l.language, 'en');
        arktest.assertEQ(l.region, 'US');
    }
}

function testMoreUnknownKeys(): void {
    {
        let l = new Intl.Locale('en-US-u-cf-standard');
        arktest.assertEQ(l.toString(), 'en-US-u-cf-standard');
    }
    {
        let l = new Intl.Locale('en-US-u-em-emoji');
        arktest.assertEQ(l.toString(), 'en-US-u-em-emoji');
    }
    {
        let l = new Intl.Locale('en-US-u-lb-strict');
        arktest.assertEQ(l.toString(), 'en-US-u-lb-strict');
    }
    {
        let l = new Intl.Locale('en-US-u-ms-metric');
        arktest.assertEQ(l.toString(), 'en-US-u-ms-metric');
    }
    {
        let l = new Intl.Locale('en-US-u-rg-uszzzz');
        arktest.assertEQ(l.toString(), 'en-US-u-rg-uszzzz');
    }
    {
        let l = new Intl.Locale('en-US-u-sd-gbsct');
        arktest.assertEQ(l.toString(), 'en-US-u-sd-gbsct');
    }
    {
        let l = new Intl.Locale('en-US-u-va-posix');
        arktest.assertEQ(l.toString(), 'en-US-u-va-posix');
    }
    {
        let l = new Intl.Locale('en-US-u-cf-account-cu-eur-fw-mon-tz-uschi');
        arktest.assertEQ(l.toString(), 'en-US-u-cf-account-cu-eur-fw-mon-tz-uschi');
    }
}

function testFromLocaleWithExtensions(): void {
    {
        let src = new Intl.Locale('en-US-u-ca-gregory-cu-cny-tz-uschi');
        let l = new Intl.Locale(src);
        arktest.assertEQ(l.toString(), 'en-US-u-ca-gregory-cu-cny-tz-uschi');
        arktest.assertEQ(l.calendar, 'gregory');
    }
    {
        let src = new Intl.Locale('zh-Hans-CN-u-co-pinyin-cu-cny');
        let l = new Intl.Locale(src);
        arktest.assertEQ(l.toString(), 'zh-Hans-CN-u-co-pinyin-cu-cny');
        arktest.assertEQ(l.collation, 'pinyin');
    }
    {
        let src = new Intl.Locale('en-US-u-ca-gregory-cu-cny');
        let l = new Intl.Locale(src, { hourCycle: 'h23' } as Intl.LocaleOptions);
        arktest.assertEQ(l.toString(), 'en-US-u-ca-gregory-cu-cny-hc-h23');
        arktest.assertEQ(l.hourCycle, 'h23');
        arktest.assertEQ(l.calendar, 'gregory');
    }
}

function testMaximizeMinimizePreservesExtensions(): void {
    {
        let l = new Intl.Locale('en-u-ca-gregory-cu-cny');
        let max = l.maximize();
        arktest.assertEQ(max.calendar, 'gregory');
    }
    {
        let l = new Intl.Locale('en-u-ca-gregory-cu-cny');
        let min = l.minimize();
        arktest.assertEQ(min.calendar, 'gregory');
        arktest.assertEQ(min.toString(), 'en-u-ca-gregory-cu-cny');
    }
}

function testOptionsNumericRebuildsTag(): void {
    {
        let l = new Intl.Locale('en-US', { numeric: true } as Intl.LocaleOptions);
        arktest.assertEQ(l.toString(), 'en-US-u-kn');
        arktest.assertTrue(l.numeric as boolean);
    }
    {
        let l = new Intl.Locale('en-US', { numeric: false } as Intl.LocaleOptions);
        arktest.assertEQ(l.toString(), 'en-US-u-kn-false');
        arktest.assertFalse(l.numeric as boolean);
    }
    {
        let l = new Intl.Locale('en-US-u-cu-cny', { numeric: true } as Intl.LocaleOptions);
        arktest.assertEQ(l.toString(), 'en-US-u-cu-cny-kn');
    }
    {
        let l = new Intl.Locale('en-US-u-cu-cny', { numeric: false } as Intl.LocaleOptions);
        arktest.assertEQ(l.toString(), 'en-US-u-cu-cny-kn-false');
    }
}

function testKeyWithoutValueReturnsTrue(): void {
    {
        let l = new Intl.Locale('en-US-u-ca');
        arktest.assertEQ(l.calendar, 'true');
    }
    {
        let l = new Intl.Locale('en-US-u-co');
        arktest.assertEQ(l.collation, 'true');
    }
    {
        let l = new Intl.Locale('en-US-u-nu');
        arktest.assertEQ(l.numberingSystem, 'true');
    }
    {
        let l = new Intl.Locale('en-US-u-hc');
        arktest.assertEQ(l.hourCycle, 'true');
    }
    {
        let l = new Intl.Locale('en-US-u-kf');
        arktest.assertEQ(l.caseFirst, '');
    }
}

function testKeyWithoutValueOmitsValueInToString(): void {
    {
        let l = new Intl.Locale('en-US-u-ca');
        arktest.assertEQ(l.toString(), 'en-US-u-ca');
    }
    {
        let l = new Intl.Locale('en-US-u-co');
        arktest.assertEQ(l.toString(), 'en-US-u-co');
    }
    {
        let l = new Intl.Locale('en-US-u-nu');
        arktest.assertEQ(l.toString(), 'en-US-u-nu');
    }
    {
        let l = new Intl.Locale('en-US-u-hc');
        arktest.assertEQ(l.toString(), 'en-US-u-hc');
    }
    {
        let l = new Intl.Locale('en-US-u-kf');
        arktest.assertEQ(l.toString(), 'en-US-u-kf');
    }
    {
        let l = new Intl.Locale('en-US-u-ca-co-nu-hc-kf');
        arktest.assertEQ(l.toString(), 'en-US-u-ca-co-hc-kf-nu');
    }
    {
        let l = new Intl.Locale('en-US-u-ca-gregory-co');
        arktest.assertEQ(l.toString(), 'en-US-u-ca-gregory-co');
    }
}

function testKfWithoutValueReturnsEmpty(): void {
    {
        let l = new Intl.Locale('en-US-u-kf');
        arktest.assertEQ(l.caseFirst, '');
    }
    {
        let l = new Intl.Locale('en-US-u-kf-true');
        arktest.assertEQ(l.caseFirst, '');
    }
    {
        let l = new Intl.Locale('en-US-u-kf-upper');
        arktest.assertEQ(l.caseFirst, 'upper');
    }
}

function main(): int {
    let suite = new arktest.ArkTestsuite('IntlLocaleExtension');
    suite.addTest('known extensions preserved in toString', testKnownExtensionsPreserved);
    suite.addTest('unknown extensions preserved in toString', testUnknownExtensionsPreserved);
    suite.addTest('mixed known+unknown extensions', testMixedKnownUnknownExtensions);
    suite.addTest('case normalization in toString', testCaseNormalization);
    suite.addTest('options override rebuilds tag', testOptionsOverrideRebuildsTag);
    suite.addTest('options override preserves unknown keys', testOptionsPreserveUnknownKeys);
    suite.addTest('multiple known extensions preserved', testMultipleKnownExtensions);
    suite.addTest('boolean kn extension', testBooleanExtensionKn);
    suite.addTest('hourCycle extension preserved', testHourCyclePreserved);
    suite.addTest('extension key alphabetical order', testExtensionOrderPreserved);
    suite.addTest('baseName excludes extensions', testBaseNameWithoutExtensions);
    suite.addTest('private use tags', testPrivateUseTags);
    suite.addTest('more unknown key types preserved', testMoreUnknownKeys);
    suite.addTest('from Locale object preserves extensions', testFromLocaleWithExtensions);
    suite.addTest('maximize/minimize preserves extensions', testMaximizeMinimizePreservesExtensions);
    suite.addTest('options numeric rebuilds tag', testOptionsNumericRebuildsTag);
    suite.addTest('key without value returns true property', testKeyWithoutValueReturnsTrue);
    suite.addTest('key without value omits value in toString', testKeyWithoutValueOmitsValueInToString);
    suite.addTest('kf without value returns empty string', testKfWithoutValueReturnsEmpty);
    return suite.run();
}
