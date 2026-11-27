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

#include "ets_interop_js_gtest.h"
#include "plugins/ets/runtime/interop_js/tooling/internal_api.h"
#include "plugins/ets/runtime/interop_js/interop_context.h"

#include <ani.h>
#include <gtest/gtest.h>

#include <vector>

namespace ark::ets::interop::js::testing {

namespace {

void IsStaticFrameCollector([[maybe_unused]] const void *frame, bool isStaticFrame, void *data)
{
    auto *vec = static_cast<std::vector<bool> *>(data);
    vec->push_back(isStaticFrame);
}

// Captures the union stack as a vector of frame types (static/dynamic).
// Returns false when the API itself fails, `isEmpty` mirrors UnionStackIsEmpty.
bool CaptureUnionStack(std::vector<bool> &isStaticFrameV, bool &isEmpty)
{
    if (!UnionStackIsEmpty(&isEmpty) || isEmpty) {
        return false;
    }
    return ForEachFrameInUnionStack(IsStaticFrameCollector, &isStaticFrameV);
}

}  // namespace

bool CompFrameType(const std::vector<bool> &isStaticFrameV, const std::vector<bool> &compType)
{
    bool res = true;
    ASSERT(compType.size() == isStaticFrameV.size());

    int frameSize = compType.size();
    for (int i = 0; i < frameSize; i++) {
        res = res && (compType[i] == isStaticFrameV[frameSize - i - 1]);
    }
    ASSERT(res);
    return res;
}

static bool TestHybridStack()
{
    bool isEmpty = true;
    if (!UnionStackIsEmpty(&isEmpty)) {
        return false;
    }
    ASSERT(!isEmpty);
    std::vector<bool> isStaticFrameV;
    [[maybe_unused]] bool res = ForEachFrameInUnionStack(IsStaticFrameCollector, &isStaticFrameV);
    ASSERT(res);

    /*
        #  Static MethodName: hybrid_stack.ETSGLOBAL::testHybridTest
        #  Static MethodName: hybrid_stack.ETSGLOBAL::staFun0

        *  Dynamic functionName: dyFun1

        #  Static MethodName: hybrid_stack.ETSGLOBAL::staFun1

        *  Dynamic functionName: dyFun2_1
        *  Dynamic functionName: dyFun2_2
        *  Dynamic functionName: dyFun2

        #  Static MethodName: hybrid_stack.ETSGLOBAL::staFun2

        *  Dynamic functionName: dyFun3_3
        *  Dynamic functionName: dyFun3

        #  Static MethodName: hybrid_stack.ETSGLOBAL::staFun3
        #  Static MethodName: hybrid_stack.ETSGLOBAL::staFun3_3
        #  Static MethodName: hybrid_stack.ETSGLOBAL::testAll

        *  Dynamic functionName: main
        *  Dynamic functionName: func_main_0
        *  Dynamic functionName: main
        *  Dynamic functionName:
        *  Dynamic functionName: func_main_0
    */
    std::vector<bool> compType {false, false, false, false, false, true, true,  true, false,
                                false, true,  false, false, false, true, false, true, true};

    return CompFrameType(isStaticFrameV, compType);
}

static bool TestHybridStackLambda()
{
    bool isEmpty = true;
    if (!UnionStackIsEmpty(&isEmpty)) {
        return false;
    }
    ASSERT(!isEmpty);
    std::vector<bool> isStaticFrameV;
    [[maybe_unused]] bool res = ForEachFrameInUnionStack(IsStaticFrameCollector, &isStaticFrameV);
    ASSERT(res);

    /*
      #  Static MethodName: hybrid_stack.ETSGLOBAL::testHybridTest_lambda     isCFrame:1

      *  Dynamic functionName: dyLambdaFun1

      #  Static MethodName: std.interop.js.DynamicFunction::invokeDynamic     isCFrame:0
      #  Static MethodName: std.interop.js.DynamicFunction::invoke0     isCFrame:0
      #  Static MethodName: hybrid_stack.ETSGLOBAL::lambda_invoke-2     isCFrame:0
      #  Static MethodName: hybrid_stack.%%lambda-lambda_invoke-2::invoke0     isCFrame:0
      #  Static MethodName: std.core.Lambda0::unsafeCall     isCFrame:0

      *  Dynamic functionName: dyLambdaFun2

      #  Static MethodName: std.interop.js.DynamicFunction::invokeDynamic     isCFrame:0
      #  Static MethodName: std.interop.js.DynamicFunction::invoke0     isCFrame:0
      #  Static MethodName: hybrid_stack.ETSGLOBAL::lambda_invoke-3     isCFrame:0
      #  Static MethodName: hybrid_stack.%%lambda-lambda_invoke-3::invoke0     isCFrame:0
      #  Static MethodName: hybrid_stack.ETSGLOBAL::testAll     isCFrame:0

      *  Dynamic functionName: main
      *  Dynamic functionName: func_main_0
      *  Dynamic functionName: main
      *  Dynamic functionName:
      *  Dynamic functionName: func_main_0
    */
    std::vector<bool> compType {false, false, false, false, false, true, true, true,  true,
                                true,  false, true,  true,  true,  true, true, false, true};

    return CompFrameType(isStaticFrameV, compType);
}

static bool TestHybridStackESValue()
{
    bool isEmpty = true;
    if (!UnionStackIsEmpty(&isEmpty)) {
        return false;
    }
    ASSERT(!isEmpty);
    std::vector<bool> isStaticFrameV;
    [[maybe_unused]] bool res = ForEachFrameInUnionStack(IsStaticFrameCollector, &isStaticFrameV);
    ASSERT(res);

    /*
      #  Static MethodName: hybrid_stack.ETSGLOBAL::testHybridTest_ESValue     isCFrame:1
      #  Static MethodName: hybrid_stack.ETSGLOBAL::lambda_invoke-2     isCFrame:0
      #  Static MethodName: hybrid_stack.%%lambda-lambda_invoke-2::invoke0     isCFrame:0
      #  Static MethodName: std.core.Lambda0::unsafeCall     isCFrame:0

      *  Dynamic functionName: dy_es1

      #  Static MethodName: std.interop.ESValue::invoke     isCFrame:0
      #  Static MethodName: hybrid_stack.ETSGLOBAL::staESFun1     isCFrame:0

      *  Dynamic functionName: dy_es2
      *  Dynamic functionName: dy_es3

      #  Static MethodName: std.interop.ESValue::invoke     isCFrame:0
      #  Static MethodName: hybrid_stack.ETSGLOBAL::staESFun2     isCFrame:0
      #  Static MethodName: hybrid_stack.ETSGLOBAL::testAll     isCFrame:0

      *  Dynamic functionName: main
      *  Dynamic functionName: func_main_0
      *  Dynamic functionName: main
      *  Dynamic functionName:
      *  Dynamic functionName: func_main_0
    */
    std::vector<bool> compType {false, false, false, false, false, true, true, true, false,
                                false, true,  true,  false, true,  true, true, true};

    return CompFrameType(isStaticFrameV, compType);
}

static bool TestHybridStackSTValue()
{
    bool isEmpty = true;
    if (!UnionStackIsEmpty(&isEmpty)) {
        return false;
    }
    ASSERT(!isEmpty);
    std::vector<bool> isStaticFrameV;
    [[maybe_unused]] bool res = ForEachFrameInUnionStack(IsStaticFrameCollector, &isStaticFrameV);
    ASSERT(res);

    /*
      #  Static MethodName: hybrid_stack.ETSGLOBAL::testHybridTest_STValue     isCFrame:1
      #  Static MethodName: hybrid_stack.ETSGLOBAL::lambda_invoke-6     isCFrame:0
      #  Static MethodName: hybrid_stack.%%lambda-lambda_invoke-6::invoke0     isCFrame:0
      #  Static MethodName: hybrid_stack.ETSGLOBAL::staSTfun1     isCFrame:0

      *  currnt Stack functionName:dy_ST1

      #  Static MethodName: hybrid_stack.ETSGLOBAL::staSTfun2     isCFrame:0

      *  currnt Stack functionName:dy_ST2
      *  currnt Stack functionName:main
      *  currnt Stack functionName:func_main_0
      *  currnt Stack functionName:main
      *  currnt Stack functionName:
      *  currnt Stack functionName:func_main_0
    */
    std::vector<bool> compType {false, false, false, false, false, false, true, false, true, true, true, true};

    return CompFrameType(isStaticFrameV, compType);
}

// Pure static call stack: main --(STValue)--> testAll -> staPure1 -> staPure2 ->
// native check. A single static segment sits on top of the dynamic JS bottom,
// no dynamic frame interleaves above it.
static bool TestPureStaticStack()
{
    bool isEmpty = true;
    std::vector<bool> isStaticFrameV;
    if (!CaptureUnionStack(isStaticFrameV, isEmpty)) {
        return false;
    }
    ASSERT(!isEmpty);

    /*
      #  Static MethodName: hybrid_stack.ETSGLOBAL::testPureStaticStack     isCFrame:1
      #  Static MethodName: hybrid_stack.ETSGLOBAL::staPure2     isCFrame:0
      #  Static MethodName: hybrid_stack.ETSGLOBAL::staPure1     isCFrame:0
      #  Static MethodName: hybrid_stack.ETSGLOBAL::testAll     isCFrame:0

      *  Dynamic functionName: main
      *  Dynamic functionName: func_main_0
      *  Dynamic functionName: main
      *  Dynamic functionName:
      *  Dynamic functionName: func_main_0
    */
    std::vector<bool> compType {false, false, false, false, false, true, true, true, true};

    return CompFrameType(isStaticFrameV, compType);
}

// Sta-to-Dyn observed from the dynamic side:
// main --(STValue)--> testAll -> staStaToDyn -> dyLeaf(JS) -> napi callback.
// The check itself runs inside a pure JS frame, so the topmost segment of the
// union stack must be dynamic, with the static segment below it.
static bool DoStaToDynCheck()
{
    bool isEmpty = true;
    std::vector<bool> isStaticFrameV;
    if (!CaptureUnionStack(isStaticFrameV, isEmpty)) {
        return false;
    }
    ASSERT(!isEmpty);

    /*
      *  Dynamic functionName: dyLeaf

      #  Static MethodName: hybrid_stack.ETSGLOBAL::staStaToDyn     isCFrame:0
      #  Static MethodName: hybrid_stack.ETSGLOBAL::testAll     isCFrame:0

      *  Dynamic functionName: main
      *  Dynamic functionName: func_main_0
      *  Dynamic functionName: main
      *  Dynamic functionName:
      *  Dynamic functionName: func_main_0
    */
    std::vector<bool> compType {false, false, false, false, false, true, true, false};

    return CompFrameType(isStaticFrameV, compType);
}

// Pure dynamic call stack observed from the dynamic side:
// main -> dyPure1 -> dyPure2 -> napi callback.
// No ETS static frame is involved at all: the whole union stack consists of
// dynamic frames only. The union stack has no static segment (no interop
// records and no static top frame), so UnionStackIsEmpty reports an empty
// union stack; the dynamic-side refinement (`if (!*isEmpty)`) is never
// reached for a dynamic-only stack.
static bool DoPureDynamicCheck()
{
    bool isEmpty = false;
    if (!UnionStackIsEmpty(&isEmpty)) {
        return false;
    }
    ASSERT(isEmpty);
    return isEmpty;
}

napi_value CheckStaToDynStack([[maybe_unused]] napi_env env, [[maybe_unused]] napi_callback_info info)
{
    napi_value result = nullptr;
    [[maybe_unused]] napi_status status = napi_get_boolean(env, DoStaToDynCheck(), &result);
    return result;
}

napi_value CheckPureDynamicStack([[maybe_unused]] napi_env env, [[maybe_unused]] napi_callback_info info)
{
    napi_value result = nullptr;
    [[maybe_unused]] napi_status status = napi_get_boolean(env, DoPureDynamicCheck(), &result);
    return result;
}

class HybridStackTsToEtsTest : public EtsInteropTest {
public:
    static bool GetAniEnv(ani_env **env)
    {
        ani_vm *aniVm;
        ani_size res;

        auto status = ANI_GetCreatedVMs(&aniVm, 1U, &res);
        if (status != ANI_OK || res == 0) {
            return false;
        }

        status = aniVm->GetEnv(ANI_VERSION_1, env);
        return status == ANI_OK && *env != nullptr;
    }

    bool RegisterETSFunction(ani_env *env)
    {
        ani_ref classRef = GetClassRefObject(env, "hybrid_stack.ETSGLOBAL");
        if (classRef == nullptr) {
            return false;
        }

        std::array methods = {
            ani_native_function {"testHybridTest", nullptr, reinterpret_cast<void *>(TestHybridStack)},
            ani_native_function {"testHybridTestLambda", nullptr, reinterpret_cast<void *>(TestHybridStackLambda)},
            ani_native_function {"testHybridTestESValue", nullptr, reinterpret_cast<void *>(TestHybridStackESValue)},
            ani_native_function {"testHybridTestSTValue", nullptr, reinterpret_cast<void *>(TestHybridStackSTValue)},
            ani_native_function {"testPureStaticStack", nullptr, reinterpret_cast<void *>(TestPureStaticStack)},
        };
        return env->Module_BindNativeFunctions(static_cast<ani_module>(classRef), methods.data(), methods.size()) ==
               ANI_OK;
    }

    // Registers C++ napi callbacks on the JS global object so that the union
    // stack can also be observed from inside pure dynamic (JS) frames.
    static bool RegisterJsCallbacks()
    {
        napi_env env = GetJsEnv();
        napi_value globalObj = nullptr;
        napi_value checksObj = nullptr;
        [[maybe_unused]] napi_status status = napi_get_global(env, &globalObj);
        if (status != napi_ok) {
            return false;
        }

        status = napi_create_object(env, &checksObj);
        if (status != napi_ok) {
            return false;
        }
        status = napi_set_named_property(env, globalObj, "unionStackChecks", checksObj);
        if (status != napi_ok) {
            return false;
        }

        napi_value staToDynFn = nullptr;
        status =
            napi_create_function(env, "checkStaToDynStack", NAPI_AUTO_LENGTH, CheckStaToDynStack, nullptr, &staToDynFn);
        if (status != napi_ok) {
            return false;
        }
        status = napi_set_named_property(env, checksObj, "checkStaToDynStack", staToDynFn);
        if (status != napi_ok) {
            return false;
        }

        napi_value pureDynamicFn = nullptr;
        status = napi_create_function(env, "checkPureDynamicStack", NAPI_AUTO_LENGTH, CheckPureDynamicStack, nullptr,
                                      &pureDynamicFn);
        if (status != napi_ok) {
            return false;
        }
        return napi_set_named_property(env, checksObj, "checkPureDynamicStack", pureDynamicFn) == napi_ok;
    }
};

TEST_F(HybridStackTsToEtsTest, check_ts_to_ets_hybridstack)
{
    ani_env *aniEnv {};
    ASSERT_TRUE(GetAniEnv(&aniEnv));

    ASSERT_TRUE(RegisterETSFunction(aniEnv));
    ASSERT_TRUE(RegisterJsCallbacks());
    ASSERT_TRUE(RunJsTestSuite("entry_stack.ts"));
}

}  // namespace ark::ets::interop::js::testing
