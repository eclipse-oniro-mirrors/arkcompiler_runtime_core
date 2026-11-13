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

function main(): int {
    const suite = new arktest.ArkTestsuite('DateIsoParse')

    suite.addTest('Test ISO field range validation', testFieldRangeValidation)
    suite.addTest('Test ISO day rollover', testDayRollover)
    suite.addTest('Test ISO hour rollover', testHourRollover)
    suite.addTest('Test ISO leap year', testLeapYear)
    suite.addTest('Test ISO year range', testYearRange)
    suite.addTest('Test ISO partial formats', testPartialFormats)
    suite.addTest('Test ISO invalid formats', testInvalidFormats)
    suite.addTest('Test ISO timezone formats', testTimezoneFormats)
    suite.addTest('Test ISO milliseconds', testMilliseconds)
    suite.addTest('Test ISO expanded year', testExpandedYear)
    suite.addTest('Test ISO edge cases', testEdgeCases)
    suite.addTest('Test non-ISO fallback', testNonIsoFallback)

    return suite.run()
}

function testFieldRangeValidation(): void {
    // Month out of range (1-12 required)
    arktest.expectError(() => { new Date('2020-13-01T00:00:00Z') },
        'Invalid time value: \'2020-13-01T00:00:00Z\'')
    arktest.expectError(() => { new Date('2020-00-01T00:00:00Z') },
        'Invalid time value: \'2020-00-01T00:00:00Z\'')
    arktest.expectError(() => { new Date('2020-99-01T00:00:00Z') },
        'Invalid time value: \'2020-99-01T00:00:00Z\'')

    // Day out of range (1-31 required)
    arktest.expectError(() => { new Date('2020-01-00T00:00:00Z') },
        'Invalid time value: \'2020-01-00T00:00:00Z\'')
    arktest.expectError(() => { new Date('2020-01-32T00:00:00Z') },
        'Invalid time value: \'2020-01-32T00:00:00Z\'')
    arktest.expectError(() => { new Date('2020-01-99T00:00:00Z') },
        'Invalid time value: \'2020-01-99T00:00:00Z\'')

    // Hour out of range (>24 rejected)
    arktest.expectError(() => { new Date('2000-01-01T25:00:00Z') },
        'Invalid time value: \'2000-01-01T25:00:00Z\'')
    arktest.expectError(() => { new Date('2000-01-01T99:00:00Z') },
        'Invalid time value: \'2000-01-01T99:00:00Z\'')

    // Minute out of range
    arktest.expectError(() => { new Date('2000-01-01T00:60:00Z') },
        'Invalid time value: \'2000-01-01T00:60:00Z\'')
    arktest.expectError(() => { new Date('2000-01-01T00:99:00Z') },
        'Invalid time value: \'2000-01-01T00:99:00Z\'')

    // Second out of range
    arktest.expectError(() => { new Date('2000-01-01T00:00:60Z') },
        'Invalid time value: \'2000-01-01T00:00:60Z\'')
    arktest.expectError(() => { new Date('2000-01-01T00:00:99Z') },
        'Invalid time value: \'2000-01-01T00:00:99Z\'')
}

function testDayRollover(): void {
    // Day <= 31 but exceeds days in month: rolls over (compatible with dynamic runtime)

    // February (28/29 days)
    const feb31 = new Date('2020-02-31T00:00:00Z')
    arktest.assertEQ(feb31.toISOString(), '2020-03-02T00:00:00.000Z')
    const feb30 = new Date('2020-02-30T00:00:00Z')
    arktest.assertEQ(feb30.toISOString(), '2020-03-01T00:00:00.000Z')
    const feb29Leap = new Date('2020-02-29T00:00:00Z')
    arktest.assertEQ(feb29Leap.toISOString(), '2020-02-29T00:00:00.000Z')
    const feb29NonLeap = new Date('2021-02-29T00:00:00Z')
    arktest.assertEQ(feb29NonLeap.toISOString(), '2021-03-01T00:00:00.000Z')
    const feb28 = new Date('2020-02-28T00:00:00Z')
    arktest.assertEQ(feb28.toISOString(), '2020-02-28T00:00:00.000Z')

    // April (30 days)
    const apr30 = new Date('2024-04-30T00:00:00Z')
    arktest.assertEQ(apr30.toISOString(), '2024-04-30T00:00:00.000Z')
    const apr31 = new Date('2024-04-31T00:00:00Z')
    arktest.assertEQ(apr31.toISOString(), '2024-05-01T00:00:00.000Z')

    // June (30 days)
    const jun30 = new Date('2024-06-30T00:00:00Z')
    arktest.assertEQ(jun30.toISOString(), '2024-06-30T00:00:00.000Z')
    const jun31 = new Date('2024-06-31T00:00:00Z')
    arktest.assertEQ(jun31.toISOString(), '2024-07-01T00:00:00.000Z')

    // September (30 days)
    const sep30 = new Date('2024-09-30T00:00:00Z')
    arktest.assertEQ(sep30.toISOString(), '2024-09-30T00:00:00.000Z')
    const sep31 = new Date('2024-09-31T00:00:00Z')
    arktest.assertEQ(sep31.toISOString(), '2024-10-01T00:00:00.000Z')

    // November (30 days)
    const nov30 = new Date('2024-11-30T00:00:00Z')
    arktest.assertEQ(nov30.toISOString(), '2024-11-30T00:00:00.000Z')
    const nov31 = new Date('2024-11-31T00:00:00Z')
    arktest.assertEQ(nov31.toISOString(), '2024-12-01T00:00:00.000Z')

    // 31-day months: day=31 is valid (no rollover)
    const jan31 = new Date('2024-01-31T00:00:00Z')
    arktest.assertEQ(jan31.toISOString(), '2024-01-31T00:00:00.000Z')
    const mar31 = new Date('2024-03-31T00:00:00Z')
    arktest.assertEQ(mar31.toISOString(), '2024-03-31T00:00:00.000Z')
    const may31 = new Date('2024-05-31T00:00:00Z')
    arktest.assertEQ(may31.toISOString(), '2024-05-31T00:00:00.000Z')
    const jul31 = new Date('2024-07-31T00:00:00Z')
    arktest.assertEQ(jul31.toISOString(), '2024-07-31T00:00:00.000Z')
    const aug31 = new Date('2024-08-31T00:00:00Z')
    arktest.assertEQ(aug31.toISOString(), '2024-08-31T00:00:00.000Z')
    const oct31 = new Date('2024-10-31T00:00:00Z')
    arktest.assertEQ(oct31.toISOString(), '2024-10-31T00:00:00.000Z')
    const dec31 = new Date('2024-12-31T00:00:00Z')
    arktest.assertEQ(dec31.toISOString(), '2024-12-31T00:00:00.000Z')
}

function testHourRollover(): void {
    // hour=24 rolls over to next day only when minute/second/ms are 0 (compatible with dynamic runtime)
    const h24 = new Date('2000-01-01T24:00:00.000Z')
    arktest.assertEQ(h24.toISOString(), '2000-01-02T00:00:00.000Z')

    // hour=24 with non-zero minute is rejected by dynamic runtime
    arktest.expectError(() => { new Date('2000-01-01T24:30:00.000Z') },
        'Invalid time value: \'2000-01-01T24:30:00.000Z\'')
}

function testLeapYear(): void {
    // Feb 29 valid in leap years
    const y2024 = new Date('2024-02-29T00:00:00Z')
    arktest.assertEQ(y2024.toISOString(), '2024-02-29T00:00:00.000Z')

    const y2000 = new Date('2000-02-29T00:00:00Z')
    arktest.assertEQ(y2000.toISOString(), '2000-02-29T00:00:00.000Z')

    const y1600 = new Date('1600-02-29T00:00:00Z')
    arktest.assertEQ(y1600.toISOString(), '1600-02-29T00:00:00.000Z')

    // Feb 29 in non-leap years rolls to Mar 1
    const y2100 = new Date('2100-02-29T00:00:00Z')
    arktest.assertEQ(y2100.toISOString(), '2100-03-01T00:00:00.000Z')

    const y1900 = new Date('1900-02-29T00:00:00Z')
    arktest.assertEQ(y1900.toISOString(), '1900-03-01T00:00:00.000Z')

    const y2023 = new Date('2023-02-29T00:00:00Z')
    arktest.assertEQ(y2023.toISOString(), '2023-03-01T00:00:00.000Z')

    // Feb 28 always valid
    const feb28 = new Date('2023-02-28T00:00:00Z')
    arktest.assertEQ(feb28.toISOString(), '2023-02-28T00:00:00.000Z')
}

function testYearRange(): void {
    // Year out of maxDateYears
    arktest.expectError(() => { new Date('+300000-01-01T00:00:00Z') },
        'Invalid time value: \'+300000-01-01T00:00:00Z\'')
    arktest.expectError(() => { new Date('-300000-01-01T00:00:00Z') },
        'Invalid time value: \'-300000-01-01T00:00:00Z\'')

    // Boundary year +275760 is valid
    const maxYear = new Date('+275760-01-01T00:00:00Z')
    arktest.assertEQ(maxYear.toISOString(), '+275760-01-01T00:00:00.000Z')

    // Expanded year with sign
    const negYear = new Date('-000001-07-01T00:00Z')
    arktest.assertEQ(negYear.toISOString(), '-000001-07-01T00:00:00.000Z')
}

function testPartialFormats(): void {
    // Year only — defaults to Jan 1 UTC
    const yearOnly = new Date('1995')
    arktest.assertEQ(yearOnly.toISOString(), '1995-01-01T00:00:00.000Z')

    // Year-month only — defaults to day 1
    const yearMonth = new Date('1995-12')
    arktest.assertEQ(yearMonth.toISOString(), '1995-12-01T00:00:00.000Z')

    // Date only (no time) — TZ defaults to UTC
    const dateOnly = new Date('1995-12-17')
    arktest.assertEQ(dateOnly.toISOString(), '1995-12-17T00:00:00.000Z')
}

function testInvalidFormats(): void {
    // Empty string
    arktest.expectError(() => { new Date('') },
        'Invalid time value: \'\'')

    // Trailing garbage
    arktest.expectError(() => { new Date('1995-12-17T03:24:00abc') },
        'Invalid time value: \'1995-12-17T03:24:00abc\'')
    arktest.expectError(() => { new Date('1995abc') },
        'Invalid time value: \'1995abc\'')

    // Leading/trailing space
    arktest.expectError(() => { new Date(' 2024-02-19T08:00:00') },
        'Invalid time value: \' 2024-02-19T08:00:00\'')
    arktest.expectError(() => { new Date('2024-02-19T08:00:00 ') },
        'Invalid time value: \'2024-02-19T08:00:00 \'')

    // Missing milliseconds after dot
    arktest.expectError(() => { new Date('2024-02-28T00:55:31.') },
        'Invalid time value: \'2024-02-28T00:55:31.\'')

    // Pure numeric strings (not ISO)
    arktest.expectError(() => { new Date('275761') },
        'Invalid time value: \'275761\'')
    arktest.expectError(() => { new Date('1622505600000') },
        'Invalid time value: \'1622505600000\'')
}

function testTimezoneFormats(): void {
    // Z (UTC)
    const z = new Date('2024-01-01T00:00:00Z')
    arktest.assertEQ(z.toISOString(), '2024-01-01T00:00:00.000Z')

    // +HH:mm
    const plusColon = new Date('2024-01-01T00:00:00+08:00')
    arktest.assertEQ(plusColon.toISOString(), '2023-12-31T16:00:00.000Z')

    // +HHmm (no colon)
    const plusNoColon = new Date('2024-01-01T00:00:00+0800')
    arktest.assertEQ(plusNoColon.toISOString(), '2023-12-31T16:00:00.000Z')

    // -HH:mm
    const minusColon = new Date('2024-01-01T00:00:00-05:00')
    arktest.assertEQ(minusColon.toISOString(), '2024-01-01T05:00:00.000Z')

    // TZ offset out of range
    arktest.expectError(() => { new Date('2024-05-20T14:30+25:00') },
        'Invalid time value: \'2024-05-20T14:30+25:00\'')
    arktest.expectError(() => { new Date('2024-05-20T14:30+08:60') },
        'Invalid time value: \'2024-05-20T14:30+08:60\'')

    // No timezone with T — local time
    const localT = new Date('2024-02-19T08:00:00')
    const localRef = new Date(2024, 1, 19, 8, 0, 0)
    arktest.assertEQ(localT.toISOString(), localRef.toISOString())

    // No timezone without T — UTC
    const noT = new Date('1970-01-01')
    arktest.assertEQ(noT.toISOString(), '1970-01-01T00:00:00.000Z')
}

function testMilliseconds(): void {
    // 3-digit ms
    const ms3 = new Date('2024-02-28T00:55:31.123Z')
    arktest.assertEQ(ms3.toISOString(), '2024-02-28T00:55:31.123Z')

    // 4-digit ms (truncated to 3)
    const ms4 = new Date('2024-02-28T00:55:31.1234Z')
    arktest.assertEQ(ms4.toISOString(), '2024-02-28T00:55:31.123Z')

    // 2-digit ms (padded with 0)
    const ms2 = new Date('2024-02-28T00:55:31.12Z')
    arktest.assertEQ(ms2.toISOString(), '2024-02-28T00:55:31.120Z')

    // 1-digit ms (padded with 00)
    const ms1 = new Date('2024-02-28T00:55:31.1Z')
    arktest.assertEQ(ms1.toISOString(), '2024-02-28T00:55:31.100Z')

    // 6-digit ms (truncated to 3)
    const ms6 = new Date('2024-02-28T00:55:31.123456Z')
    arktest.assertEQ(ms6.toISOString(), '2024-02-28T00:55:31.123Z')
}

function testExpandedYear(): void {
    // 6-digit expanded year with + sign
    const plus100k = new Date('+100000-01-01T00:00:00Z')
    arktest.assertEQ(plus100k.toISOString(), '+100000-01-01T00:00:00.000Z')

    // 6-digit expanded year with - sign
    const minus1 = new Date('-000001-07-01T00:00Z')
    arktest.assertEQ(minus1.toISOString(), '-000001-07-01T00:00:00.000Z')

    // Year 0 must be prefixed with + sign; -000000 is invalid per ECMA262 21.4.1.32.1,
    // ISO parser rejects it directly without Legacy fallback.
    const plus0 = new Date('+000000-01-01T00:00:00Z')
    arktest.assertEQ(plus0.getFullYear(), 0)
    arktest.expectError(() => { new Date('-000000') },
        'Invalid time value: \'-000000\'')

    // 5-digit unsigned year is NOT standard ISO (removed non-standard 5-digit branch)
    // ISO parser reads 4 digits "1000", then fails — falls back to LegacyDateParser
    const y5 = new Date('10000-01-01')
    arktest.assertEQ(y5.getFullYear(), 10000)

    // Expanded year out of range
    arktest.expectError(() => { new Date('+300000-01-01T00:00:00Z') },
        'Invalid time value: \'+300000-01-01T00:00:00Z\'')
}

function testEdgeCases(): void {
    // parseYearPart: expanded year too short (falls back to LegacyDateParser)
    const expand5 = new Date('+12345')
    arktest.assertEQ(expand5.getFullYear(), 12345)

    // parseYearPart: unsigned year too short (falls back to LegacyDateParser)
    const year3 = new Date('123')
    arktest.assertEQ(year3.getFullYear(), 123)

    // parseDatePart: no separator after year (garbage char)
    arktest.expectError(() => { new Date('2024X01-01') },
        'Invalid time value: \'2024X01-01\'')

    // parseDatePart: no separator after month
    arktest.expectError(() => { new Date('2024-01X01') },
        'Invalid time value: \'2024-01X01\'')

    // parse: non-T char after date
    arktest.expectError(() => { new Date('2024-01-01X') },
        'Invalid time value: \'2024-01-01X\'')

    // parseTimePart: truncated hour
    arktest.expectError(() => { new Date('2024-01-01T2') },
        'Invalid time value: \'2024-01-01T2\'')

    // parseTimePart: missing colon after hour
    arktest.expectError(() => { new Date('2024-01-01T1200') },
        'Invalid time value: \'2024-01-01T1200\'')

    // parseTimePart: truncated minute
    arktest.expectError(() => { new Date('2024-01-01T12:0') },
        'Invalid time value: \'2024-01-01T12:0\'')

    // parseTimePart: HH:mm:ss without ms
    const hmsOnly = new Date('2024-01-01T12:30:45Z')
    arktest.assertEQ(hmsOnly.toISOString(), '2024-01-01T12:30:45.000Z')

    // parseTimePart: HH:mm without :ss
    const hmOnly = new Date('2024-01-01T12:30Z')
    arktest.assertEQ(hmOnly.toISOString(), '2024-01-01T12:30:00.000Z')
}

function testNonIsoFallback(): void {
    // Non-ISO formats fall back to LegacyDateParser
    const rfc = new Date('Tue, 19 Feb 2024 08:00:00 GMT')
    arktest.assertEQ(rfc.toISOString(), '2024-02-19T08:00:00.000Z')

    // Non-standard ISO width (1-digit month) falls back to Legacy
    const md1 = new Date('2020-4-31')
    arktest.assertEQ(md1.getFullYear(), 2020)
    arktest.assertEQ(md1.getMonth(), 4)
    arktest.assertEQ(md1.getDate(), 1)
    const md2 = new Date('2020-2-31')
    arktest.assertEQ(md2.getFullYear(), 2020)
    arktest.assertEQ(md2.getMonth(), 2)
    arktest.assertEQ(md2.getDate(), 2)
    const md3 = new Date('2020-12-31')
    arktest.assertEQ(md3.getFullYear(), 2020)
    arktest.assertEQ(md3.getMonth(), 11)
    arktest.assertEQ(md3.getDate(), 31)

    // Slash format
    const slash = new Date('06/15/2025')
    arktest.assertEQ(slash.getFullYear(), 2025)
    arktest.assertEQ(slash.getMonth(), 5)
    arktest.assertEQ(slash.getDate(), 15)

    // Month name format
    const monthName = new Date('August 19, 1975 23:15:30 UTC')
    arktest.assertEQ(monthName.toISOString(), '1975-08-19T23:15:30.000Z')
}
