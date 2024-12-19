/**
 * Copyright (c) 2023-2024 Huawei Device Co., Ltd.
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

#ifndef PANDA_PLUGINS_ETS_RUNTIME_INTEROP_JS_INTEROP_CONTEXT_H_
#define PANDA_PLUGINS_ETS_RUNTIME_INTEROP_JS_INTEROP_CONTEXT_H_

#include "plugins/ets/runtime/ets_coroutine.h"
#include "plugins/ets/runtime/ets_vm.h"
#include "plugins/ets/runtime/interop_js/ets_proxy/ets_class_wrapper.h"
#include "plugins/ets/runtime/interop_js/ets_proxy/ets_method_wrapper.h"
#include "plugins/ets/runtime/interop_js/ets_proxy/shared_reference_storage.h"
#include "plugins/ets/runtime/interop_js/js_job_queue.h"
#include "plugins/ets/runtime/interop_js/js_refconvert.h"
#include "plugins/ets/runtime/interop_js/interop_stacks.h"
#include "plugins/ets/runtime/interop_js/intrinsics/std_js_jsruntime.h"
#include "runtime/include/value.h"

#include "plugins/ets/runtime/interop_js/stack_info.h"

#include <node_api.h>
#include <unordered_map>

namespace ark {

class Class;
class ClassLinkerContext;

namespace mem {
class GlobalObjectStorage;
class Reference;
}  // namespace mem

}  // namespace ark

namespace ark::ets::interop::js {

class JSValue;

// Work-around for String JSValue and node_api
class JSValueStringStorage {
public:
    class CachedEntry {
    public:
        std::string const *Data()
        {
            return data_;
        }

    private:
        friend class JSValue;
        friend class JSValueStringStorage;

        explicit CachedEntry(std::string const *data) : data_(data) {}

        std::string const *data_ {};
    };

    explicit JSValueStringStorage() = default;

    CachedEntry Get(std::string &&str)
    {
        auto [it, inserted] = stringTab_.try_emplace(std::move(str), 0);
        it->second++;
        return CachedEntry(&it->first);
    }

    void Release(CachedEntry str)
    {
        auto it = stringTab_.find(*str.Data());
        ASSERT(it != stringTab_.end());
        if (--(it->second) == 0) {
            stringTab_.erase(it);
        }
    }

private:
    std::unordered_map<std::string, uint64_t> stringTab_;
};

class ConstStringStorage {
public:
    explicit ConstStringStorage(InteropCtx *ctx) : ctx_(ctx) {}

    void LoadDynamicCallClass(Class *klass);

    template <typename Callback>
    bool EnumerateStrings(size_t startFrom, size_t count, Callback cb);

    napi_value GetConstantPool();

    uint32_t AllocateSlotsInStringBuffer(uint32_t count)
    {
        // Atomic with relaxed order reason: ordering constraints are not required
        return qnameBufferSize_.fetch_add(count, std::memory_order_relaxed);
    }

protected:
    uint32_t GetStringBufferSize()
    {
        // Atomic with relaxed order reason: ordering constraints are not required
        return qnameBufferSize_.load(std::memory_order_relaxed);
    }

private:
    napi_value InitBuffer(size_t length);
    InteropCtx *Ctx();

private:
    std::vector<napi_value> stringBuffer_ {};
    napi_ref jsStringBufferRef_ {};
    Class *lastLoadedClass_ {};
    InteropCtx *ctx_ = nullptr;
    static std::atomic_uint32_t qnameBufferSize_;
};

class InteropCtx {
public:
    NO_COPY_SEMANTIC(InteropCtx);
    NO_MOVE_SEMANTIC(InteropCtx);

    PANDA_PUBLIC_API static void Init(EtsCoroutine *coro, napi_env env);
    virtual ~InteropCtx();

    static void Destroy(void *ptr)
    {
        auto *instance = reinterpret_cast<InteropCtx *>(ptr);
        Runtime::GetCurrent()->GetInternalAllocator()->Delete(instance);
        SharedEtsVmState::TryReleaseInstance();
    }

    static InteropCtx *Current(EtsCoroutine *coro)
    {
        // NOTE(konstanting): we may want to optimize this and take the cached pointer from the coroutine
        // itself. Just need to make sure that the worker's interop context ptr state is coherent
        // with coroutine's.
        auto *w = coro->GetWorker();
        return Current(w);
    }

    static InteropCtx *Current()
    {
        return Current(EtsCoroutine::GetCurrent());
    }

    PandaEtsVM *GetPandaEtsVM()
    {
        return sharedEtsVmState_->pandaEtsVm;
    }

    napi_env GetJSEnv() const
    {
        ASSERT(jsEnv_ != nullptr);
        return jsEnv_;
    }

    void SetJSEnv(napi_env env)
    {
        jsEnv_ = env;
    }

    napi_env GetJsEnvForEventLoopCallbacks() const
    {
        ASSERT(EtsCoroutine::GetCurrent() == EtsCoroutine::GetCurrent()->GetCoroutineManager()->GetMainThread());
        return jsEnvForEventLoopCallbacks_;
    }

    mem::GlobalObjectStorage *Refstor() const
    {
        return sharedEtsVmState_->pandaEtsVm->GetGlobalObjectStorage();
    }

    ClassLinker *GetClassLinker() const
    {
        // NOTE(konstanting): do we REALLY need this method here?
        return Runtime::GetCurrent()->GetClassLinker();
    }

    ClassLinkerContext *LinkerCtx() const
    {
        return sharedEtsVmState_->linkerCtx;
    }

    JSValueStringStorage *GetStringStor()
    {
        return &jsValueStringStor_;
    }

    LocalScopesStorage *GetLocalScopesStorage()
    {
        return &localScopesStorage_;
    }

    void DestroyLocalScopeForTopFrame(Frame *frame)
    {
        GetLocalScopesStorage()->DestroyLocalScopeForTopFrame(jsEnv_, frame);
    }

    mem::Reference *GetJSValueFinalizationRegistry() const
    {
        return jsvalueFregistryRef_;
    }

    Method *GetRegisterFinalizerMethod() const
    {
        return jsvalueFregistryRegister_;
    }

    // NOTE(vpukhov): implement in native code
    [[nodiscard]] bool PushOntoFinalizationRegistry(EtsCoroutine *coro, EtsObject *obj, EtsObject *cbarg)
    {
        auto queue = Refstor()->Get(jsvalueFregistryRef_);
        std::array<Value, 4U> args = {Value(queue), Value(obj->GetCoreType()), Value(cbarg->GetCoreType()),
                                      Value(static_cast<ObjectHeader *>(nullptr))};
        jsvalueFregistryRegister_->Invoke(coro, args.data());
        return !coro->HasPendingException();
    }

    // NOTE(vpukhov): replace with stack-like allocator
    template <typename T, size_t OPT_SZ>
    struct TempArgs {
    public:
        explicit TempArgs(size_t sz)
        {
            if (LIKELY(sz <= OPT_SZ)) {
                sp_ = {new (inlArr_.data()) T[sz], sz};
            } else {
                sp_ = {new T[sz], sz};
            }
        }

        ~TempArgs()
        {
            if (UNLIKELY(static_cast<void *>(sp_.data()) != inlArr_.data())) {
                delete[] sp_.data();
            } else {
                static_assert(std::is_trivially_destructible_v<T>);
            }
        }

        NO_COPY_SEMANTIC(TempArgs);
#if defined(__clang__)
        NO_MOVE_SEMANTIC(TempArgs);
#elif defined(__GNUC__)  // RVO bug
        NO_MOVE_OPERATOR(TempArgs);
        TempArgs(TempArgs &&other) : sp_(other.sp_), inlArr_(other.inlArr_)
        {
            if (LIKELY(sp_.size() <= OPT_SZ)) {
                sp_ = Span<T>(reinterpret_cast<T *>(inlArr_.data()), sp_.size());
            }
        }
#endif

        Span<T> &operator*()
        {
            return sp_;
        }
        Span<T> *operator->()
        {
            return &sp_;
        }
        T &operator[](size_t idx)
        {
            return sp_[idx];
        }

    private:
        Span<T> sp_;
        alignas(T) std::array<uint8_t, sizeof(T) * OPT_SZ> inlArr_;
    };

    template <typename T, size_t OPT_SZ = 8U>
    ALWAYS_INLINE static auto GetTempArgs(size_t sz)
    {
        return TempArgs<T, OPT_SZ>(sz);
    }

    InteropCallStack &CallStack()
    {
        return interopStk_;
    }

    JSRefConvertCache *GetRefConvertCache()
    {
        return &refconvertCache_;
    }

    Class *GetJSValueClass() const
    {
        return sharedEtsVmState_->jsValueClass;
    }

    Class *GetJSErrorClass() const
    {
        return sharedEtsVmState_->jsErrorClass;
    }

    Class *GetObjectClass() const
    {
        return sharedEtsVmState_->objectClass;
    }

    Class *GetStringClass() const
    {
        return sharedEtsVmState_->stringClass;
    }

    Class *GetBigIntClass() const
    {
        return sharedEtsVmState_->bigintClass;
    }

    Class *GetArrayAsListIntClass() const
    {
        return sharedEtsVmState_->arrayAsListIntClass;
    }

    Class *GetUndefinedClass() const
    {
        return sharedEtsVmState_->undefinedClass;
    }

    Class *GetPromiseClass() const
    {
        return sharedEtsVmState_->promiseClass;
    }

    Method *GetPromiseInteropConnectMethod() const
    {
        return sharedEtsVmState_->promiseInteropConnectMethod;
    }

    Class *GetErrorClass() const
    {
        return sharedEtsVmState_->errorClass;
    }

    Class *GetExceptionClass() const
    {
        return sharedEtsVmState_->exceptionClass;
    }

    Class *GetTypeClass() const
    {
        return sharedEtsVmState_->typeClass;
    }

    Class *GetBoxIntClass() const
    {
        return sharedEtsVmState_->boxIntClass;
    }

    Class *GetBoxLongClass() const
    {
        return sharedEtsVmState_->boxLongClass;
    }

    Class *GetArrayClass() const
    {
        return sharedEtsVmState_->arrayClass;
    }

    Class *GetArrayBufferClass() const
    {
        return sharedEtsVmState_->arraybufClass;
    }

    bool IsFunctionalInterface(Class *klass) const
    {
        return sharedEtsVmState_->functionalInterfaces.count(klass) > 0;
    }

    js_proxy::JSProxy *GetJsProxyInstance(EtsClass *cls) const
    {
        return sharedEtsVmState_->GetJsProxyInstance(cls);
    }

    void SetJsProxyInstance(EtsClass *cls, js_proxy::JSProxy *proxy)
    {
        sharedEtsVmState_->SetJsProxyInstance(cls, proxy);
    }

    EtsObject *CreateETSCoreJSError(EtsCoroutine *coro, JSValue *jsvalue);

    static void ThrowETSError(EtsCoroutine *coro, napi_value val);
    static void ThrowETSError(EtsCoroutine *coro, const char *msg);
    static void ThrowETSError(EtsCoroutine *coro, const std::string &msg)
    {
        ThrowETSError(coro, msg.c_str());
    }

    PANDA_PUBLIC_API static void ThrowJSError(napi_env env, const std::string &msg);
    static void ThrowJSTypeError(napi_env env, const std::string &msg);
    static void ThrowJSValue(napi_env env, napi_value val);

    void ForwardEtsException(EtsCoroutine *coro);
    void ForwardJSException(EtsCoroutine *coro);

    [[nodiscard]] static bool SanityETSExceptionPending()
    {
        auto coro = EtsCoroutine::GetCurrent();
        auto env = InteropCtx::Current(coro)->GetJSEnv();
        return coro->HasPendingException() && !NapiIsExceptionPending(env);
    }

    [[nodiscard]] static bool SanityJSExceptionPending()
    {
        auto coro = EtsCoroutine::GetCurrent();
        auto env = InteropCtx::Current(coro)->GetJSEnv();
        return !coro->HasPendingException() && NapiIsExceptionPending(env);
    }

    // Die and print execution stacks
    [[noreturn]] PANDA_PUBLIC_API static void Fatal(const char *msg);
    [[noreturn]] static void Fatal(const std::string &msg)
    {
        Fatal(msg.c_str());
    }

    void SetPendingNewInstance(EtsObject *newInstance)
    {
        pendingNewInstance_ = newInstance;
    }

    EtsObject *AcquirePendingNewInstance()
    {
        auto res = pendingNewInstance_;
        pendingNewInstance_ = nullptr;
        return res;
    }

    ets_proxy::EtsMethodWrappersCache *GetEtsMethodWrappersCache()
    {
        return &etsMethodWrappersCache_;
    }

    ets_proxy::EtsClassWrappersCache *GetEtsClassWrappersCache()
    {
        return &etsClassWrappersCache_;
    }

    ets_proxy::SharedReferenceStorage *GetSharedRefStorage()
    {
        return sharedEtsVmState_->etsProxyRefStorage.get();
    }

    EtsObject *GetUndefinedObject()
    {
        return EtsObject::FromCoreType(GetPandaEtsVM()->GetUndefinedObject());
    }

    ConstStringStorage *GetConstStringStorage()
    {
        return &constStringStorage_;
    }

    ALWAYS_INLINE void UpdateInteropStackInfoIfNeeded()
    {
        stackInfoManager_.UpdateStackInfoIfNeeded();
    }

protected:
    static InteropCtx *Current(CoroutineWorker *worker)
    {
        return worker->GetLocalStorage().Get<CoroutineWorker::DataIdx::INTEROP_CTX_PTR, InteropCtx *>();
    }

private:
    explicit InteropCtx(EtsCoroutine *coro, napi_env env);
    void InitJsValueFinalizationRegistry(EtsCoroutine *coro);
    void InitSharedEtsVmState(PandaEtsVM *vm);
    void InitExternalInterfaces();

    // per-EtsVM data, should be shared between contexts.
    // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
    struct SharedEtsVmState {
        NO_COPY_SEMANTIC(SharedEtsVmState);
        NO_MOVE_SEMANTIC(SharedEtsVmState);

        static std::shared_ptr<SharedEtsVmState> GetInstance(PandaEtsVM *vm);
        // should be called when we would like to check if there are no more InteropCtx instances left
        static void TryReleaseInstance();
        ~SharedEtsVmState() = default;

        js_proxy::JSProxy *GetJsProxyInstance(EtsClass *cls) const;
        void SetJsProxyInstance(EtsClass *cls, js_proxy::JSProxy *proxy);

        // Intentionally leaving these members public to avoid duplicating the InteropCtx's accessors.
        // Maybe its worth to add some e.g. VmState() method to the InteropCtx and move all its accessors here
        Class *jsRuntimeClass {};
        Class *jsValueClass {};
        Class *jsErrorClass {};
        Class *objectClass {};
        Class *stringClass {};
        Class *bigintClass {};
        Class *arrayAsListIntClass {};
        Class *undefinedClass {};
        Class *promiseClass {};
        Class *errorClass {};
        Class *exceptionClass {};
        Class *typeClass {};
        Class *arrayClass {};
        Class *arraybufClass {};
        Class *boxIntClass {};
        Class *boxLongClass {};
        PandaSet<Class *> functionalInterfaces {};
        Method *promiseInteropConnectMethod = nullptr;
        PandaEtsVM *pandaEtsVm = nullptr;
        // NOTE(konstanting): do we REALLY want to cache the classlinker context?
        ClassLinkerContext *linkerCtx {};
        PandaUniquePtr<ets_proxy::SharedReferenceStorage> etsProxyRefStorage {};

    private:
        explicit SharedEtsVmState(PandaEtsVM *vm);
        void CacheClasses(EtsClassLinker *etsClassLinker);

        // class -> proxy instance, should be accessed under a mutex, hence private
        PandaMap<EtsClass *, PandaUniquePtr<js_proxy::JSProxy>> jsProxies_;

        static std::shared_ptr<SharedEtsVmState> instance_;
        static os::memory::Mutex mutex_;

        // Allocator calls our private ctor
        friend class mem::Allocator;
    };
    // NOLINTEND(misc-non-private-member-variables-in-classes)
    std::shared_ptr<SharedEtsVmState> sharedEtsVmState_;

    // JSVM context
    napi_env jsEnv_ {};
    napi_env jsEnvForEventLoopCallbacks_ {};

    // various per-JSVM interfaces
    ExternalIfaceTable interfaceTable_;

    // caches
    JSValueStringStorage jsValueStringStor_ {};
    ConstStringStorage constStringStorage_;
    LocalScopesStorage localScopesStorage_ {};
    JSRefConvertCache refconvertCache_;

    // finalization registry for JSValues
    mem::Reference *jsvalueFregistryRef_ {};
    Method *jsvalueFregistryRegister_ {};

    // ets_proxy data
    EtsObject *pendingNewInstance_ {};
    ets_proxy::EtsMethodWrappersCache etsMethodWrappersCache_ {};
    ets_proxy::EtsClassWrappersCache etsClassWrappersCache_ {};

    // hybrid call stack support
    InteropCallStack interopStk_ {};

    StackInfoManager stackInfoManager_;

    // needs to access the jsEnv_ without the nullptr assert
    friend class JSNapiEnvScope;
    // Allocator calls our protected ctor
    friend class mem::Allocator;
};

inline JSRefConvertCache *RefConvertCacheFromInteropCtx(InteropCtx *ctx)
{
    return ctx->GetRefConvertCache();
}
inline napi_env JSEnvFromInteropCtx(InteropCtx *ctx)
{
    return ctx->GetJSEnv();
}
inline mem::GlobalObjectStorage *RefstorFromInteropCtx(InteropCtx *ctx)
{
    return ctx->Refstor();
}

template <typename Callback>
bool ConstStringStorage::EnumerateStrings(size_t startFrom, size_t count, Callback cb)
{
    auto jsArr = GetReferenceValue(Ctx()->GetJSEnv(), jsStringBufferRef_);
    for (size_t index = startFrom; index < startFrom + count; index++) {
        napi_value jsStr;
        napi_get_element(Ctx()->GetJSEnv(), jsArr, index, &jsStr);
        ASSERT(GetValueType(Ctx()->GetJSEnv(), jsStr) == napi_string);
        if (!cb(jsStr)) {
            return false;
        }
    }
    return true;
}

}  // namespace ark::ets::interop::js

#endif  // !PANDA_PLUGINS_ETS_RUNTIME_INTEROP_JS_INTEROP_CONTEXT_H_
