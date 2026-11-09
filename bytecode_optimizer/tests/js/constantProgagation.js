/*
 Copyright (c) 2024 Shenzhen Kaihong Digital Industry Development Co., Ltd.
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at
 *
 http://www.apache.org/licenses/LICENSE-2.0
 *
 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 *
 Copyright (c) 2024-2026 Huawei Device Co., Ltd.
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at
 *
 http://www.apache.org/licenses/LICENSE-2.0
 *
 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

function sccpFoldGreater()
{
    let a = 2;
    let b = 1;
    let intMaxVal = 2147483647;
    print(a > b);
    a = intMaxVal;
    print(a > b);

    a = true;
    b = false;
    print(a > b);

    a = 2.1;
    b = 2.2;
    print(a > b);
}

function sccpFoldGreaterNoEffect()
{
    let a = 2;
    let b = 1.1;
    let intMaxVal = 2147483647;
    print(a > b);
    b = false;
    print(a > b);

    a = 1.1;
    b = 1;
    print(a > b);

    b = true;
    print(a > b);
    b = intMaxVal;
    print(a > b);

    a = true;
    print(a > b);
    b = 1.1;
    print(a > b);
    b = 1;
    print(a > b);
}

function sccpFoldGreaterEq()
{
    let a = 2;
    let b = 1;
    let intMaxVal = 2147483647;
    print(a >= b);
    a = intMaxVal;
    print(a >= b);
    a = 1;
    print(a >= b);

    a = true;
    b = false;
    print(a >= b);
    b = true;
    print(a >= b);

    a = 2.1;
    b = 2.2;
    print(a >= b);
    b = 2.1;
    print(a >= b);
}

function sccpFoldGreaterEqNoEffect()
{
    let a = 2;
    let b = 1.1;
    let intMaxVal = 2147483647;
    print(a >= b);
    b = false;
    print(a >= b);

    a = 1.1;
    b = 1;
    print(a >= b);
    b = true;
    print(a >= b);
    b = intMaxVal;
    print(a >= b);

    a = true;
    print(a >= b);
    b = 1.1;
    print(a >= b);
    b = 1;
    print(a >= b);
}

function sccpFoldLess()
{
    let a = 2;
    let b = 1;
    let intMaxVal = 2147483647;
    print(a < b);
    a = intMaxVal;
    print(a < b);

    a = true;
    b = false;
    print(a < b);

    a = 2.1;
    b = 2.2;
    print(a < b);
}

function sccpFoldLessNoEffect()
{
    let a = 2;
    let b = 1.1;
    let intMaxVal = 2147483647;
    print(a < b);
    b = false;
    print(a < b);

    a = 1.1;
    b = 1;
    print(a < b);
    b = true;
    print(a < b);
    b = intMaxVal;
    print(a < b);

    a = true;
    print(a < b);
    b = 1.1;
    print(a < b);
    b = 1;
    print(a < b);
}

function sccpFoldLessEq()
{
    let a = 2;
    let b = 1;
    let intMaxVal = 2147483647;
    print(a <= b);
    a = intMaxVal;
    print(a <= b);
    a = 1;
    print(a <= b);

    a = false;
    b = true;
    print(a <= b);
    b = false;
    print(a <= b);

    a = 3.1;
    b = 3.2;
    print(a <= b);
    b = 3.1;
    print(a <= b);
}

function sccpFoldLessEqNoEffect()
{
    let a = 2;
    let b = 1.1;
    let intMaxVal = 2147483647;
    print(a <= b);
    b = false;
    print(a <= b);

    a = 1.1;
    b = 1;
    print(a <= b);
    b = true;
    print(a <= b);
    b = intMaxVal;
    print(a <= b);

    a = true;
    print(a <= b);
    b = 1.1;
    print(a <= b);
    b = 1;
    print(a < b);
}

function sccpFoldEq()
{
    let a = 1;
    let b = 1;
    let intMaxVal = 2147483647;
    print(a == b);
    print(a == intMaxVal);

    a = true;
    b = true;
    print(a == b);

    a = 3.2;
    b = 3.3;
    print(a == b);
}

function sccpFoldEqNoEffect()
{
    let a = 2;
    let b = 1.1;
    let intMaxVal = 2147483647;
    print(a == b);
    b = false;
    print(a == b);

    a = 1.1;
    b = 1;
    print(a == b);
    b = true;
    print(a == b);
    b = intMaxVal;
    print(a == b);

    a = true;
    print(a == b);
    b = 1.1;
    print(a == b);
    b = 1;
    print(a == b);
}

function sccpFoldStrictEq()
{
    let a = 1;
    let b = 1;
    let intMaxVal = 2147483647;
    print(a === b);
    print(a === intMaxVal);

    a = true;
    b = true;
    print(a === b);

    a = 3.2;
    b = 3.3;
    print(a === b);
}

function sccpFoldStrictEqNoEffect()
{
    let a = 2;
    let b = 1.1;
    let intMaxVal = 2147483647;
    print(a === b);
    b = false;
    print(a === b);

    a = 1.1;
    b = 1;
    print(a === b);
    b = true;
    print(a === b);
    b = intMaxVal;
    print(a === b);

    a = true;
    print(a === b);
    b = 1.1;
    print(a === b);
    b = 1;
    print(a === b);
}

function sccpFoldNotEq()
{
    let a = 1;
    let b = 1;
    let intMaxVal = 2147483647;
    print(a != b);
    print(a != intMaxVal);

    a = true;
    b = true;
    print(a != b);

    a = 3.2;
    b = 3.3;
    print(a != b);
}

function sccpFoldNotEqNoEffect()
{
    let a = 2;
    let b = 1.1;
    let intMaxVal = 2147483647;
    print(a != b);
    b = false;
    print(a != b);

    a = 1.1;
    b = 1;
    print(a != b);
    b = true;
    print(a != b);
    b = intMaxVal;
    print(a != b);

    a = true;
    print(a != b);
    b = 1.1;
    print(a != b);
    b = 1;
    print(a != b);
}

function sccpFoldStrictNotEq()
{
    let a = 1;
    let b = 1;
    let intMaxVal = 2147483647;
    print(a !== b);
    print(a !== intMaxVal);

    a = true;
    b = true;
    print(a !== b);

    a = 3.2;
    b = 3.3;
    print(a !== b);

}

function sccpFoldStrictNotEqNoEffect()
{
    let a = 2;
    let b = 1.1;
    let intMaxVal = 2147483647;
    print(a !== b);
    b = false;
    print(a !== b);

    a = 1.1;
    b = 1;
    print(a !== b);
    b = true;
    print(a !== b);
    b = intMaxVal;
    print(a !== b);

    a = true;
    print(a !== b);
    b = 1.1;
    print(a !== b);
    b = 1;
    print(a !== b);
}

function sccpNoEffectNanWithInt() {
    let a = NaN;
    let intVal = 1;
    let intMaxVal = 2147483647;
    let intMinVal = -2147483648;
    print(a === intVal);
    print(a != intVal);
    print(a !== intVal);
    print(a > intVal);
    print(a >= intVal);
    print(b >= intVal);
    print(b > intVal);

    // Check NAN with int min.
    print(a === intMinVal);
    print(a !== intMinVal);
    print(a > intMinVal);
    print(a >= intMinVal);
    print(a < intMinVal);
    print(a <= intMinVal);
    print(a == intMinVal);
    print(a != intMinVal);

    // Check NAN with int maxVal.
    print(a === intMaxVal);
    print(a !== intMaxVal);
    print(a > intMaxVal);
    print(a >= intMaxVal);
    print(a < intMaxVal);
    print(a <= intMaxVal);
    print(a == intMaxVal);
    print(a != intMaxVal);
}

function sccpNoEffectNanWithBoolAndDouble() {
    let a = NaN;
    let doubleVal = 1.1;
    let trueVal = true;
    let falseVal = false;

    // Check NAN with double val.
    print(a === doubleVal);
    print(a !== doubleVal);
    print(a > doubleVal);
    print(a >= doubleVal);
    print(a < doubleVal);
    print(a <= doubleVal);
    print(a == doubleVal);
    print(a != doubleVal);

    // Check NAN with true.
    print(a === trueVal);
    print(a !== trueVal);
    print(a > trueVal);
    print(a >= trueVal);
    print(a < trueVal);
    print(a <= trueVal);
    print(a == trueVal);
    print(a != trueVal);

    // Check NAN with false.
    print(a === falseVal);
    print(a !== falseVal);
    print(a > falseVal);
    print(a >= falseVal);
    print(a < falseVal);
    print(a <= falseVal);
    print(a == falseVal);
    print(a != falseVal);
}

function sccpNoEffectInfinityWithInt() {
    let a = Infinity;
    let intVal = 1;
    let intMaxVal = 2147483647;
    let intMinVal = -2147483648;

    print(a === intVal);
    print(a != intVal);
    print(a !== intVal);
    print(a > intVal);
    print(a >= intVal);
    print(b >= intVal);
    print(b > intVal);

    // Check Infinity with int min.
    print(a === intMinVal);
    print(a !== intMinVal);
    print(a > intMinVal);
    print(a >= intMinVal);
    print(a < intMinVal);
    print(a <= intMinVal);
    print(a == intMinVal);
    print(a != intMinVal);

    // Check Infinity with int maxVal.
    print(a === intMaxVal);
    print(a !== intMaxVal);
    print(a > intMaxVal);
    print(a >= intMaxVal);
    print(a < intMaxVal);
    print(a <= intMaxVal);
    print(a == intMaxVal);
    print(a != intMaxVal);
}

function sccpNoEffectInfinityWithBoolAndDouble() {
    let doubleVal = 1.1;
    let trueVal = true;
    let falseVal = false;

    // Check Infinity with double val.
    print(a === doubleVal);
    print(a !== doubleVal);
    print(a > doubleVal);
    print(a >= doubleVal);
    print(a < doubleVal);
    print(a <= doubleVal);
    print(a == doubleVal);
    print(a != doubleVal);

    // Check Infinity with true.
    print(a === trueVal);
    print(a !== trueVal);
    print(a > trueVal);
    print(a >= trueVal);
    print(a < trueVal);
    print(a <= trueVal);
    print(a == trueVal);
    print(a != trueVal);

    // Check Infinity with false.
    print(a === falseVal);
    print(a !== falseVal);
    print(a > falseVal);
    print(a >= falseVal);
    print(a < falseVal);
    print(a <= falseVal);
    print(a == falseVal);
    print(a != falseVal);
}

function sccpNoEffectArithmetic() {
    let a = 1;
    let b = 2;
    print(a >> b);
    print(a << b);
    print(a >>> b);

    a = 1.2;
    b = 2.2;
    print(a >> b);
    print(a << b);
    print(a >>> b);
}

function sccpFoldArithmetic() {
    let a = 6;
    let b = 3;
    print(a + b);
    print(a - b);
    print(a * b);
    print(a / b);
    print(a % b);
    print(-a);
    let c = a + b;
    print(c * b);
}

function sccpFoldArithmeticDouble() {
    let a = 1.5;
    let b = 0.5;
    print(a + b);
    print(a - b);
    print(a * b);
    print(a / b);
    print(a % b);
    print(-a);
}

function sccpNumericBoundaries() {
    let zero = 0;
    let negative = -1;
    let minusFour = -4;
    let two = 2;
    let max = 2147483647;
    let min = -2147483648;
    print(-zero);
    print(zero * negative);
    print(negative * zero);
    print(minusFour % two);
    print(max + two);
    print(max * two);
    print(-min);
}

function sccpMixedNumericLiterals() {
    print(1e2 + 3);
    print(2.5 * 4);
    print(1.5e2 - 0.5);
    print(300 / 1e1);
    print(2e1 * 3e0);
    print(0.25 + 1.5e1);
    print(10 % 2.5);
    print((1e1 + 2.5) * 1e1);
}

function sccpMixedNumericOperands() {
    let integer = 4;
    let fraction = 2.5;
    print(integer + fraction);
    print(fraction + integer);
    print(integer - fraction);
    print(fraction - integer);
    print(integer * fraction);
    print(fraction * integer);
    print(integer / fraction);
    print(fraction / integer);
    print(integer % fraction);
    print(fraction % integer);
    // A folded intermediate can change representation from double to integer and vice versa.
    print((fraction * integer) + fraction);
    print((2147483647 + integer) - integer);
}

function sccpMixedNumericBoundaries() {
    let zero = 0;
    let negative = -2.5;
    print(zero * negative);
    print(negative * zero);
    print(zero / negative);
    print(-10 % 2.5);
    print(-2.5 % 2);
    print(2147483647 + 0.5);
    print(-2147483648 - 0.5);
}

function sccpMixedNumericNoFold() {
    print(2.5 / 0);
    print(2.5 % 0);
    print(2.5 / -0);
    print(2.5 % -0);
    print(true + 2.5);
    print(2.5 + true);
    print(false * 2.5);
    print(2.5 * false);
    let infinity = 1e308 * 2;
    print(infinity / 2);
    print(2 / infinity);
    print(infinity % 2);
    print(2 % infinity);
    let nan = infinity - infinity;
    print(nan / 2);
    print(2 % nan);
}

function sccpMixedIntegerLimit() {
    // The IR test replaces this integer with values outside the safe conversion range.
    let integer = 1;
    print(integer + 0.5);
    print(0.5 + integer);
}

function sccpIfCheck() {
    if (true) {
        print(true);
    }

    if (false) {
        print(false);
    }
    let a = 1;
    let b = 2;
    if (a < b) {
        print(true);
    }

    if (a > b) {
        print(true);
    }
}

function sccpPhi(a) {
    let b = a;
    if (a) {
        b = true;
    } else {
        b = true;
    }
    print(b);
}

function sccpPhi2()
{
    let a = 1;
    if (true) {
        a = 2;
    } else {
        a = 1;
    }
    print(a);
}

function sccpPhiNoEffect(a) {
    let b = a;
    if (a){
        b = true;
    } else {
        b = false;
    }
    print(b);
}
