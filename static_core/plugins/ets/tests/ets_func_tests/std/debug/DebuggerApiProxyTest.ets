/**
* Copyright (c) 2024-2026 Huawei Device Co., Ltd.
* Licensed under the Apache License, Version 2.0 (the 'License');
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an 'AS IS' BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

import { DebuggerAPI, LocalVariable, This, CapturedVariable, GlobalClass, GlobalFunction, GlobalVariable, InstanceFieldProxy, StaticFieldProxy, ValueProxy } from 'std/debug'

class TestTarget {
    public static staticVal: int = 100
    public fieldVal: int
    public fnField: Function = (() => {})

    public constructor(v: int) {
        this.fieldVal = v
    }

    public methodDouble(x: int): int {
        return x * 2
    }

    public static staticMethodAdd(a: int, b: int): int {
        return a + b
    }
}

function testLocalVariableBasicConstruction(): void {
   let proxy = new LocalVariable(0, 0, 'x', 0 as long, 'I')
   arktest.assertNE(proxy, undefined)
   arktest.assertNE(proxy, null)
}

function testThisGet(): void {
   let obj = new TestTarget(42)
   let thisProxy = new This(obj)
   let result = thisProxy.value()
   arktest.assertEQ(result, obj)
}

function testThisSetThrows(): void {
   let obj = new TestTarget(42)
   let thisProxy = new This(obj)
   arktest.expectError(() => {
       thisProxy.setValue(new ValueProxy(obj))
       return
   }, new Error(`cannot reassign 'this'`))
}

function testThisGetField(): void {
   let obj = new TestTarget(42)
   let thisProxy = new This(obj)
   let fieldProxy = thisProxy.getField('fieldVal')
   arktest.assertNE(fieldProxy, undefined)
   let val = fieldProxy.value()
   arktest.assertEQ(val, 42)
}

function testThisSetField(): void {
   let obj = new TestTarget(42)
   let thisProxy = new This(obj)
   thisProxy.getField('fieldVal').setValue(new ValueProxy(99 as Object))
   arktest.assertEQ(obj.fieldVal, 99)
   obj.fieldVal = 42
}

function testThisCallMethod(): void {
   let obj = new TestTarget(42)
   let thisProxy = new This(obj)
   let result = thisProxy.call('methodDouble', 5)
   arktest.assertEQ(result.value(), 10)
}

function testThisGetFieldNotFound(): void {
   let obj = new TestTarget(42)
   let thisProxy = new This(obj)
   arktest.expectError(() => {
       thisProxy.getField('nonExistentField')
   }, new Error(`field 'nonExistentField' not found on 'this'`))
}

function testThisCallMethodNotFound(): void {
   let obj = new TestTarget(42)
   let thisProxy = new This(obj)
   arktest.expectError(() => {
       thisProxy.call('nonExistent', 0)
    }, new Error(`method or callable field 'nonExistent' not found on 'this'`))
}

function testThisNotCallable(): void {
   let obj = new TestTarget(42)
   let thisProxy = new This(obj)
   arktest.expectError(() => {
       thisProxy.invoke()
   }, new Error(`'this' is not callable`))
}

function testGlobalClassGetThrows(): void {
   let cls = Class.from<TestTarget>()
   let proxy = new GlobalClass(cls)
   arktest.expectError(() => {
       proxy.value()
   }, new TypeError('cannot get value of a class'))
}

function testGlobalClassSetThrows(): void {
   let cls = Class.from<TestTarget>()
   let proxy = new GlobalClass(cls)
   arktest.expectError(() => {
       proxy.setValue(new ValueProxy(0 as Object))
       return
   }, new TypeError('cannot set value of a class'))
}

function testGlobalClassInvokeThrows(): void {
   let cls = Class.from<TestTarget>()
   let proxy = new GlobalClass(cls)
   arktest.expectError(() => {
       proxy.invoke()
   }, new TypeError('cannot invoke a class'))
}

function testGlobalClassGetFieldStaticField(): void {
   let cls = Class.from<TestTarget>()
   let proxy = new GlobalClass(cls)
   let fieldProxy = proxy.getField('staticVal')
   arktest.assertNE(fieldProxy, undefined)
   let val = fieldProxy.value()
   arktest.assertEQ(val, 100)
}

function testGlobalClassSetFieldStaticField(): void {
   let cls = Class.from<TestTarget>()
   let proxy = new GlobalClass(cls)
   let fieldProxy = proxy.getField('staticVal')
   fieldProxy.setValue(new ValueProxy(200 as Object))
   arktest.assertEQ(TestTarget.staticVal, 200)
   fieldProxy.setValue(new ValueProxy(100 as Object))
   arktest.assertEQ(TestTarget.staticVal, 100)
}

function testGlobalClassCallStaticMethod(): void {
   let cls = Class.from<TestTarget>()
   let proxy = new GlobalClass(cls)
   let result = proxy.call('staticMethodAdd', 10, 20)
   arktest.assertEQ(result.value(), 30)
}

function testGlobalClassStaticFieldNotFound(): void {
   let cls = Class.from<TestTarget>()
   let proxy = new GlobalClass(cls)
   arktest.expectError(() => {
       proxy.getField('NON_EXISTENT')
   }, new Error(`static field 'NON_EXISTENT' not found on class 'DebuggerApiProxyTest.TestTarget'`))
}

function testGlobalClassStaticMethodNotFound(): void {
   let cls = Class.from<TestTarget>()
   let proxy = new GlobalClass(cls)
   arktest.expectError(() => {
       proxy.call('nonExistentMethod', 0)
   }, new Error(`static method 'nonExistentMethod' not found on class 'DebuggerApiProxyTest.TestTarget'`))
}

function testCapturedVariableBasicAccess(): void {
   let target = new TestTarget(42)
   let cls = Class.from<TestTarget>()
   let field = cls.getInstanceField('fieldVal')!
   arktest.assertNE(field, undefined)
   let proxy = new CapturedVariable('fieldVal', target, field)
   let val = proxy.value()
   arktest.assertEQ(val, 42)
   proxy.setValue(new ValueProxy(77 as Object))
   arktest.assertEQ(target.fieldVal, 77)
   target.fieldVal = 42
}

function testCapturedVariableGetDisplayName(): void {
   let target = new TestTarget(42)
   let cls = Class.from<TestTarget>()
   let field = cls.getInstanceField('fieldVal')!
   let proxy = new CapturedVariable('capturedX', target, field)
   arktest.expectError(() => {
       proxy.getField('nonExistentField')
   }, new Error(`field 'nonExistentField' not found on 'captured variable capturedX'`))
}

function testChainedFieldAccess(): void {
   let target = new TestTarget(42)
   let proxy = new This(target)
   let fieldProxy = proxy.getField('fieldVal')
   arktest.assertNE(fieldProxy, undefined)
   let val = fieldProxy.value()
   arktest.assertEQ(val, 42)
   fieldProxy.setValue(new ValueProxy(100 as Object))
   arktest.assertEQ(target.fieldVal, 100)
   target.fieldVal = 42
}

function testGlobalFunctionGetThrows(): void {
   let cls = Class.from<TestTarget>()
   let method = cls.getStaticMethod('staticMethodAdd')!
   arktest.assertNE(method, undefined)
   let proxy = new GlobalFunction(method)
   arktest.expectError(() => {
       proxy.value()
   }, new TypeError('cannot get value of a function'))
}

function testGlobalFunctionSetThrows(): void {
   let cls = Class.from<TestTarget>()
   let method = cls.getStaticMethod('staticMethodAdd')!
   let proxy = new GlobalFunction(method)
   arktest.expectError(() => {
       proxy.setValue(new ValueProxy(0 as Object))
       return
   }, new TypeError('cannot reassign global function'))
}

function testGlobalFunctionInvoke(): void {
   let cls = Class.from<TestTarget>()
   let method = cls.getStaticMethod('staticMethodAdd')!
   let proxy = new GlobalFunction(method)
   let result = proxy.invoke(3, 7)
   arktest.assertEQ(result.value(), 10)
}

function testGlobalFunctionGetFieldThrows(): void {
   let cls = Class.from<TestTarget>()
   let method = cls.getStaticMethod('staticMethodAdd')!
   let proxy = new GlobalFunction(method)
   arktest.expectError(() => {
       proxy.getField('x')
   }, new TypeError('cannot get field on a function'))
}

function testGlobalFunctionCallThrows(): void {
   let cls = Class.from<TestTarget>()
   let method = cls.getStaticMethod('staticMethodAdd')!
   let proxy = new GlobalFunction(method)
   arktest.expectError(() => {
       proxy.call('x', 0)
   }, new TypeError('cannot call method on a function'))
}

function testGlobalVariableThrowsNotImplemented(): void {
   let proxy = new GlobalVariable('TEST_GLOBAL')
   arktest.expectError(() => {
       proxy.value()
   }, new Error('Global variable access not yet implemented'))
}

function testInstanceFieldProxyGetSet(): void {
   let target = new TestTarget(42)
   let cls = Class.from<TestTarget>()
   let field = cls.getInstanceField('fieldVal')!
   let proxy = new InstanceFieldProxy(target, field)
   arktest.assertEQ(proxy.value(), 42)
   proxy.setValue(new ValueProxy(55 as Object))
   arktest.assertEQ(target.fieldVal, 55)
   target.fieldVal = 42
}

function testStaticFieldProxyGetSet(): void {
   let cls = Class.from<TestTarget>()
   let proxy = new StaticFieldProxy(cls, 'staticVal')
   arktest.assertEQ(proxy.value(), 100)
   proxy.setValue(new ValueProxy(300 as Object))
   arktest.assertEQ(TestTarget.staticVal, 300)
   proxy.setValue(new ValueProxy(100 as Object))
   arktest.assertEQ(TestTarget.staticVal, 100)
}

function testThisInvokeCallable(): void {
   let obj = new TestTarget(42)
   let cls = Class.from<TestTarget>()
   let fnField = cls.getInstanceField('fnField')!
   arktest.assertNE(fnField, undefined)
   let fn: Function = (x: int): int => x + 1
   fnField.setValue(obj, fn)
   obj.fnField = fn
   let thisProxy = new This(obj)
   arktest.expectError(() => {
       thisProxy.invoke(5)
   }, new Error(`'this' is not callable`))
   fnField.setValue(obj, undefined as Any)
}

function testVariableProxyGetFieldStaticFieldFallback(): void {
   let cls = Class.from<TestTarget>()
   let method = cls.getStaticMethod('staticMethodAdd')!
   let fnProxy = new GlobalFunction(method)
   arktest.expectError(() => {
       fnProxy.getField('anything')
   }, new TypeError('cannot get field on a function'))
}

function testVariableProxyCallMethodNotFound(): void {
   let obj = new TestTarget(42)
   let thisProxy = new This(obj)
   arktest.expectError(() => {
       thisProxy.call('nonExistent', 0)
   }, new Error(`method or callable field 'nonExistent' not found on 'this'`))
}

function testVariableProxyGetFieldNullValue(): void {
   let nullProxy = new This(null)
   arktest.expectError(() => {
       nullProxy.getField('anything')
   }, new Error(`cannot get field 'anything' on 'this' (value is null)`))
}

function testVariableProxyCallNullValue(): void {
   let nullProxy = new This(null)
   arktest.expectError(() => {
       nullProxy.call('anything', 0)
   }, new Error(`cannot call method 'anything' on 'this' (value is null)`))
}

function testThisGetNull(): void {
   let nullProxy = new This(null)
   let result = nullProxy.value()
   arktest.assertTrue(result == null)
}

function testInstanceFieldProxyGetOnModifiedField(): void {
   let target = new TestTarget(42)
   let cls = Class.from<TestTarget>()
   let field = cls.getInstanceField('fieldVal')!
   let proxy = new InstanceFieldProxy(target, field)
   target.fieldVal = 100
   let val = proxy.value()
   arktest.assertEQ(val, 100)
}

function testStaticFieldProxySetThenGet(): void {
   let cls = Class.from<TestTarget>()
   let proxy = new StaticFieldProxy(cls, 'staticVal')
   let originalVal = proxy.value()
   arktest.assertEQ(originalVal, 100)
   proxy.setValue(new ValueProxy(500 as Object))
   arktest.assertEQ(proxy.value(), 500)
   proxy.setValue(new ValueProxy(100 as Object))
   arktest.assertEQ(proxy.value(), 100)
}

function testCapturedVariableInvoke(): void {
   let target = new TestTarget(42)
   let cls = Class.from<TestTarget>()
   let field = cls.getInstanceField('fieldVal')!
   let proxy = new CapturedVariable('fieldVal', target, field)
   arktest.expectError(() => {
       proxy.invoke()
   }, new Error(`'captured variable fieldVal' is not callable`))
}

function testCapturedVariableRefTypeGetField(): void {
   let target = new TestTarget(42)
   let cls = Class.from<TestTarget>()
   let field = cls.getInstanceField('fnField')!
   target.fnField = ((x: int): int => x + 1)
   let proxy = new CapturedVariable('fnField', target, field)
   let result = proxy.value()
   arktest.assertNE(result, undefined)
   arktest.assertNE(result, null)
   target.fnField = ((x: int): int => x + 1)
}

function testCapturedVariableRefTypeCallMethod(): void {
   let target = new TestTarget(42)
   let cls = Class.from<TestTarget>()
   let field = cls.getInstanceField('fnField')!
   target.fnField = ((x: int): int => x + 1)
   arktest.assertNE(field, undefined)
   let proxy = new CapturedVariable('fnField', target, field)
   let fn = proxy.value() as Function
   let result = fn.unsafeCall(5)
   arktest.assertEQ(result, 6)
   target.fnField = ((x: int): int => x + 1)
}

function testCapturedVariableNullCaptureObj(): void {
   let cls = Class.from<TestTarget>()
   let field = cls.getInstanceField('fieldVal')!
   let proxy = new CapturedVariable('fieldVal', null, field)
   arktest.expectError(() => {
       proxy.value()
   }, new Error(`cannot access captured variable 'fieldVal' (capture object is null)`))
}

function testCapturedVariableMultiCapture(): void {
   let targetA = new TestTarget(10)
   let targetB = new TestTarget(20)
   let cls = Class.from<TestTarget>()
   let field = cls.getInstanceField('fieldVal')!
   let proxyA = new CapturedVariable('a', targetA, field)
   let proxyB = new CapturedVariable('b', targetB, field)
   arktest.assertEQ(proxyA.value(), 10)
   arktest.assertEQ(proxyB.value(), 20)
   proxyA.setValue(new ValueProxy(99 as Object))
   arktest.assertEQ(targetA.fieldVal, 99)
   arktest.assertEQ(targetB.fieldVal, 20)
   targetA.fieldVal = 10
}

function main(): int {
   let suite = new arktest.ArkTestsuite('DebuggerAPI_ProxyTypeSystem')
   suite.addTest('LocalVariable_BasicConstruction', testLocalVariableBasicConstruction)
   suite.addTest('This_Get', testThisGet)
   suite.addTest('This_GetNull', testThisGetNull)
   suite.addTest('This_SetThrows', testThisSetThrows)
   suite.addTest('This_GetField', testThisGetField)
   suite.addTest('This_SetField', testThisSetField)
   suite.addTest('This_CallMethod', testThisCallMethod)
   suite.addTest('This_GetFieldNotFound', testThisGetFieldNotFound)
   suite.addTest('This_CallMethodNotFound', testThisCallMethodNotFound)
   suite.addTest('This_NotCallable', testThisNotCallable)
   suite.addTest('This_InvokeCallable', testThisInvokeCallable)
   suite.addTest('GlobalClass_GetThrows', testGlobalClassGetThrows)
   suite.addTest('GlobalClass_SetThrows', testGlobalClassSetThrows)
   suite.addTest('GlobalClass_InvokeThrows', testGlobalClassInvokeThrows)
   suite.addTest('GlobalClass_GetFieldStaticField', testGlobalClassGetFieldStaticField)
   suite.addTest('GlobalClass_SetFieldStaticField', testGlobalClassSetFieldStaticField)
   suite.addTest('GlobalClass_CallStaticMethod', testGlobalClassCallStaticMethod)
   suite.addTest('GlobalClass_StaticFieldNotFound', testGlobalClassStaticFieldNotFound)
   suite.addTest('GlobalClass_StaticMethodNotFound', testGlobalClassStaticMethodNotFound)
   suite.addTest('CapturedVariable_BasicAccess', testCapturedVariableBasicAccess)
   suite.addTest('CapturedVariable_GetDisplayName', testCapturedVariableGetDisplayName)
   suite.addTest('CapturedVariable_Invoke', testCapturedVariableInvoke)
   suite.addTest('CapturedVariable_RefType_GetField', testCapturedVariableRefTypeGetField)
   suite.addTest('CapturedVariable_RefType_CallMethod', testCapturedVariableRefTypeCallMethod)
   suite.addTest('CapturedVariable_NullCaptureObj', testCapturedVariableNullCaptureObj)
   suite.addTest('CapturedVariable_MultiCapture', testCapturedVariableMultiCapture)
   suite.addTest('ChainedFieldAccess', testChainedFieldAccess)
   suite.addTest('GlobalFunction_GetThrows', testGlobalFunctionGetThrows)
   suite.addTest('GlobalFunction_SetThrows', testGlobalFunctionSetThrows)
   suite.addTest('GlobalFunction_Invoke', testGlobalFunctionInvoke)
   suite.addTest('GlobalFunction_GetFieldThrows', testGlobalFunctionGetFieldThrows)
   suite.addTest('GlobalFunction_CallThrows', testGlobalFunctionCallThrows)
   suite.addTest('GlobalVariable_ThrowsNotImplemented', testGlobalVariableThrowsNotImplemented)
   suite.addTest('InstanceFieldProxy_GetSet', testInstanceFieldProxyGetSet)
   suite.addTest('InstanceFieldProxy_GetOnModifiedField', testInstanceFieldProxyGetOnModifiedField)
   suite.addTest('StaticFieldProxy_GetSet', testStaticFieldProxyGetSet)
   suite.addTest('StaticFieldProxy_SetThenGet', testStaticFieldProxySetThenGet)
   suite.addTest('VariableProxy_GetField_StaticFieldFallback', testVariableProxyGetFieldStaticFieldFallback)
   suite.addTest('VariableProxy_Call_MethodNotFound', testVariableProxyCallMethodNotFound)
   suite.addTest('VariableProxy_GetField_NullValue', testVariableProxyGetFieldNullValue)
   suite.addTest('VariableProxy_Call_NullValue', testVariableProxyCallNullValue)
   return suite.run()
}
