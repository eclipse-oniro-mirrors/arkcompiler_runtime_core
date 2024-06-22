/**
 * Copyright (c) 2021-2024 Huawei Device Co., Ltd.
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

#include "ets_coroutine.h"
#include "types/ets_string.h"
#include "intrinsics/helpers/ets_to_string_cache.h"
#include "intrinsics/helpers/ets_intrinsics_helpers.h"
#include "ets_vm.h"
#include "gtest/gtest.h"
#include "runtime/include/runtime.h"
#include "runtime/include/thread.h"

#include <array>
#include <thread>
#include <random>
#include <sstream>

namespace ark::ets::test {

static constexpr uint32_t TEST_THREADS = 8;
static constexpr uint32_t TEST_ITERS = 1;
static constexpr uint32_t TEST_ARRAY_SIZE = 1000;
static constexpr int32_t VALUE_RANGE = 1000;

enum GenType { COPY, SHUFFLE, INDEPENDENT };

class EtsToStringCacheTest : public testing::TestWithParam<std::tuple<const char *, GenType>> {
public:
    EtsToStringCacheTest() : engine_(std::random_device {}())
    {
        // Logger::InitializeStdLogging(Logger::Level::INFO, Logger::ComponentMaskFromString("runtime") |
        // Logger::ComponentMaskFromString("coroutines"));

        RuntimeOptions options;
        options.SetShouldLoadBootPandaFiles(true);
        options.SetShouldInitializeIntrinsics(false);
        options.SetLoadRuntimes({"ets"});

        auto stdlib = std::getenv("PANDA_STD_LIB");
        if (stdlib == nullptr) {
            std::cerr << "PANDA_STD_LIB env variable should be set and point to etsstdlib.abc" << std::endl;
            std::abort();
        }
        options.SetBootPandaFiles({stdlib});

        options.SetGcType(std::get<0>(GetParam()));
        options.SetCompilerEnableJit(false);
        if (!Runtime::Create(options)) {
            UNREACHABLE();
        }
    }

    ~EtsToStringCacheTest() override
    {
        Runtime::Destroy();
    }

    NO_COPY_SEMANTIC(EtsToStringCacheTest);
    NO_MOVE_SEMANTIC(EtsToStringCacheTest);

    void SetUp() override
    {
        ASSERT(Runtime::GetCurrent() != nullptr);
        ASSERT(PandaEtsVM::GetCurrent() != nullptr);
        mainCoro_ = EtsCoroutine::CastFromThread(PandaEtsVM::GetCurrent()->GetCoroutineManager()->GetMainThread());
        ASSERT(mainCoro_ != nullptr);
    }

    void TestMainLoop(double value, [[maybe_unused]] bool needCheck)
    {
        auto etsVm = mainCoro_->GetPandaVM();
        auto *cache = etsVm->GetDoubleToStringCache();
        auto *etsCoro = EtsCoroutine::GetCurrent();
        [[maybe_unused]] auto *str = cache->GetOrCache(etsCoro, value);
#ifndef NDEBUG
        // don't always check to increase pressure
        if (needCheck) {
            ASSERT(!str->IsUtf16());
            auto res = str->GetMutf8();

            intrinsics::helpers::FpToStringDecimalRadix(value,
                                                        [&res](std::string_view expected) { ASSERT(expected == res); });

            auto resValue = std::stod(std::string(res));
            auto eps = std::numeric_limits<double>::epsilon() * 2 * std::abs(value);
            ASSERT(std::abs(resValue - value) < eps);
        }
#endif
    }

    void TestConcurrentInsertion(const std::array<double, TEST_ARRAY_SIZE> &values)
    {
        auto runtime = Runtime::GetCurrent();
        auto coro = mainCoro_->GetCoroutineManager()->CreateEntrypointlessCoroutine(runtime, runtime->GetPandaVM(),
                                                                                    true, "worker");
        std::mt19937 engine(std::random_device {}());
        std::uniform_real_distribution<> dis(-VALUE_RANGE, VALUE_RANGE);
        std::bernoulli_distribution bern(1.0 / TEST_THREADS);
        coro->ManagedCodeBegin();
        ASSERT(coro == EtsCoroutine::GetCurrent());
        for (uint32_t i = 0; i < TEST_ITERS; i++) {
            for (auto value : values) {
                bool needCheck = bern(engine);
                TestMainLoop(value, needCheck);
            }
        }

        coro->ManagedCodeEnd();
        mainCoro_->GetCoroutineManager()->DestroyEntrypointlessCoroutine(coro);
    }

    void DoTest(void (EtsToStringCacheTest::*setup)())
    {
        (this->*setup)();
        for (uint32_t i = 0; i < TEST_THREADS; i++) {
            if (std::get<1>(GetParam()) == GenType::SHUFFLE) {
                std::shuffle(values_.begin(), values_.end(), engine_);
            } else if (std::get<1>(GetParam()) == GenType::INDEPENDENT && i > 0) {
                (this->*setup)();
            }
            threadValues_[i] = values_;
        }

        for (uint32_t i = 0; i < TEST_THREADS; i++) {
            threads_[i] = std::thread([this, i]() { TestConcurrentInsertion(threadValues_[i]); });
        }

        for (uint32_t i = 0; i < TEST_THREADS; i++) {
            threads_[i].join();
        }
    }

    void SetupSimple()
    {
        std::uniform_real_distribution<double> dist(-VALUE_RANGE, VALUE_RANGE);
        std::generate(values_.begin(), values_.end(), [&dist, this]() { return dist(engine_); });
    }

    void SetupExp()
    {
        std::uniform_int_distribution<int> dist(-VALUE_RANGE, VALUE_RANGE);
        std::generate(values_.begin(), values_.end(), [&dist, this]() { return std::pow(2U, dist(engine_)); });
    }

    void SetupRepeatedHashes()
    {
        std::uniform_real_distribution<double> dist(-VALUE_RANGE, VALUE_RANGE);

        std::generate(values_.begin(), values_.end(), [&dist, this]() {
            constexpr auto UNIQUE_HASHES = 3;
            double value;
            do {
                value = dist(engine_);
            } while (DoubleToStringCache::GetIndex(value) > UNIQUE_HASHES);
            return value;
        });
    }

    void SetupUniqueHashes()
    {
        std::uniform_real_distribution<double> dist(-VALUE_RANGE, VALUE_RANGE);

        std::unordered_map<uint32_t, double> cacheIndexToValue;
        while (cacheIndexToValue.size() < DoubleToStringCache::SIZE / 2U) {
            auto value = dist(engine_);
            cacheIndexToValue[DoubleToStringCache::GetIndex(value)] = value;
        }
        auto it = cacheIndexToValue.begin();
        std::generate(values_.begin(), values_.end(), [&cacheIndexToValue, &it]() {
            if (it == cacheIndexToValue.end()) {
                it = cacheIndexToValue.begin();
            }
            return (*it++).second;
        });
    }

    EtsCoroutine *GetMainCoro()
    {
        return mainCoro_;
    }

    std::mt19937 &GetEngine()
    {
        return engine_;
    }

private:
    std::array<double, TEST_ARRAY_SIZE> values_ {};
    std::array<std::array<double, TEST_ARRAY_SIZE>, TEST_THREADS> threadValues_ {};
    std::mt19937 engine_;

    EtsCoroutine *mainCoro_ {};
    std::array<std::thread, TEST_THREADS> threads_;
};

TEST_P(EtsToStringCacheTest, ConcurrentInsertion)
{
    DoTest(&EtsToStringCacheTest::SetupSimple);
}

TEST_P(EtsToStringCacheTest, ConcurrentInsertionExp)
{
    DoTest(&EtsToStringCacheTest::SetupExp);
}

TEST_P(EtsToStringCacheTest, ConcurrentInsertionRepeatedHashes)
{
    DoTest(&EtsToStringCacheTest::SetupRepeatedHashes);
}

TEST_P(EtsToStringCacheTest, ConcurrentInsertionUniqueHashes)
{
    DoTest(&EtsToStringCacheTest::SetupUniqueHashes);
}

TEST_P(EtsToStringCacheTest, BitcastTestCached)
{
    auto &engine = GetEngine();
    auto coro = GetMainCoro();
    auto etsVm = coro->GetPandaVM();
    std::uniform_int_distribution<uint32_t> dis(0, std::numeric_limits<uint32_t>::max());
    coro->ManagedCodeBegin();
    auto etsCoro = EtsCoroutine::GetCurrent();
    ASSERT(coro == etsCoro);
    auto *cache = etsVm->GetDoubleToStringCache();

    for (uint32_t i = 0; i < TEST_ARRAY_SIZE; i++) {
        auto longValue = (static_cast<uint64_t>(dis(engine)) << BITS_PER_UINT32) | dis(engine);
        auto value = bit_cast<double>(longValue);
        auto *str = cache->GetOrCache(etsCoro, value);
        ASSERT(!str->IsUtf16());
        auto res = str->GetMutf8();

        bool correct;
        auto eps = std::numeric_limits<double>::epsilon() * std::abs(value);
        double resValue = 0;
        if (std::isnan(value)) {
            correct = res == "NaN";
        } else {
            std::istringstream iss {std::string(res)};
            iss >> resValue;
            correct = std::abs(resValue - value) <= eps;
        }

        if (!correct) {
            std::cerr << std::setprecision(intrinsics::helpers::DOUBLE_MAX_PRECISION) << "Error:\n"
                      << "long: " << std::hex << longValue << "\n"
                      << "double: " << value << "\n"
                      << "str: " << res << "\n"
                      << "delta: " << std::abs(resValue - value) << "\n"
                      << "eps: " << eps << std::endl;
            UNREACHABLE();
        }
    }

    coro->ManagedCodeEnd();
}

TEST_P(EtsToStringCacheTest, BitcastTestRaw)
{
    auto &engine = GetEngine();
    std::uniform_int_distribution<uint32_t> dis(0, std::numeric_limits<uint32_t>::max());

    for (uint32_t i = 0; i < TEST_ARRAY_SIZE; i++) {
        auto longValue = (static_cast<uint64_t>(dis(engine)) << BITS_PER_UINT32) | dis(engine);
        auto value = bit_cast<double>(longValue);
        std::string res;
        intrinsics::helpers::FpToStringDecimalRadix(value, [&res](std::string_view expected) { res = expected; });

        bool correct;
        auto eps = std::numeric_limits<double>::epsilon() * std::abs(value);
        double resValue = 0;
        if (std::isnan(value)) {
            correct = res == "NaN";
        } else {
            std::istringstream iss {std::string(res)};
            iss >> resValue;
            correct = std::abs(resValue - value) <= eps;
        }

        if (!correct) {
            std::cerr << std::setprecision(intrinsics::helpers::DOUBLE_MAX_PRECISION) << "Error:\n"
                      << "long: " << std::hex << longValue << "\n"
                      << "double: " << value << "\n"
                      << "str: " << res << "\n"
                      << "delta: " << std::abs(resValue - value) << "\n"
                      << "eps: " << eps << std::endl;
            UNREACHABLE();
        }
    }
}

[[maybe_unused]] static bool SymEq(float x, float y)
{
    if (std::isnan(x)) {
        return std::isnan(y);
    }
    auto delta = std::abs(x - y);
    auto eps = std::abs(x) * (2 * std::numeric_limits<float>::epsilon());
    return delta <= eps;
}

TEST_P(EtsToStringCacheTest, BitcastTestFloat)
{
    auto &engine = GetEngine();
    std::uniform_int_distribution<uint32_t> dis(0, std::numeric_limits<uint32_t>::max());

    for (uint32_t i = 0; i < TEST_ARRAY_SIZE; i++) {
        auto intValue = dis(engine);
        auto value = bit_cast<float>(intValue);

        if (std::isnan(value)) {
            continue;
        }
        {
            std::string resFloat;
            intrinsics::helpers::FpToStringDecimalRadix(value, [&resFloat](std::string_view str) { resFloat = str; });
            float parsedFromFloat = 0;
            std::istringstream iss {resFloat};
            iss >> parsedFromFloat;
            ASSERT_DO(SymEq(value, parsedFromFloat), std::cerr << "Error:\n"
                                                               << "float value: " << value << "\n"
                                                               << "float str: " << resFloat << "\n"
                                                               << "float parsed: " << parsedFromFloat << "\n");
        }
        {
            auto doubleValue = static_cast<double>(value);
            std::string resDouble;
            intrinsics::helpers::FpToStringDecimalRadix(doubleValue,
                                                        [&resDouble](std::string_view str) { resDouble = str; });
            float parsedFromDouble = 0;
            std::istringstream iss {resDouble};
            iss >> parsedFromDouble;
            ASSERT(SymEq(value, parsedFromDouble));
            ASSERT_DO(SymEq(value, parsedFromDouble), std::cerr << "Error:\n"
                                                                << "float value: " << value << "\n"
                                                                << "double str: " << resDouble << "\n"
                                                                << "float parsed: " << parsedFromDouble << "\n");
        }
    }
}

INSTANTIATE_TEST_SUITE_P(EtsToStringCacheTestSuite, EtsToStringCacheTest,
                         testing::Combine(testing::Values("stw", "gen-gc", "g1-gc"),
                                          testing::Values(GenType::COPY, GenType::SHUFFLE, GenType::INDEPENDENT)));

}  // namespace ark::ets::test