/**
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#ifndef PANDA_PLUGINS_ETS_RUNTIME_PLATFORM_TYPES_H_
#define PANDA_PLUGINS_ETS_RUNTIME_PLATFORM_TYPES_H_

#include "plugins/ets/runtime/ets_coroutine.h"

namespace ark::ets {

class EtsClass;
class EtsMethod;
class EtsCoroutine;
class EtsClassLinker;
template <typename T>
class EtsTypedObjectArray;

// A set of types defined and used in platform implementation, owned by the VM
// NOLINTBEGIN(misc-non-private-member-variables-in-classes)
class PANDA_PUBLIC_API EtsPlatformTypes {
public:
    // Classes should follow the common naming schema

    // Arity threshold for functional types
    static constexpr uint32_t CORE_FUNCTION_ARITY_THRESHOLD = 17U;
    static constexpr uint32_t ASCII_CHAR_TABLE_SIZE = 128;

    /* Core runtime type system */
    EtsClass *coreObject {};        // IsObjectClass
    EtsClass *coreClass {};         // IsClassClass
    EtsClass *coreString {};        // IsStringClass
    EtsClass *coreLineString {};    // IsLineStringClass
    EtsClass *coreSlicedString {};  // IsSlicedStringClass
    EtsClass *coreTreeString {};    // IsTreeStringClass

    /* ets numeric classes */
    EtsClass *coreBoolean {};
    EtsClass *coreByte {};
    EtsClass *coreChar {};
    EtsClass *coreShort {};
    EtsClass *coreInt {};
    EtsClass *coreLong {};
    EtsClass *coreFloat {};
    EtsClass *coreDouble {};

    /* ets base language classes */
    EtsClass *coreBigint {};
    EtsClass *escompatError {};
    EtsClass *coreFunction {};
    std::array<EtsClass *, CORE_FUNCTION_ARITY_THRESHOLD> coreFunctions {};
    std::array<EtsClass *, CORE_FUNCTION_ARITY_THRESHOLD> coreFunctionRs {};
    EtsClass *coreTuple {};
    EtsClass *coreTupleN {};

    /* Runtime linkage classes */
    EtsClass *coreRuntimeLinker {};
    EtsClass *coreBootRuntimeLinker {};
    EtsClass *coreAbcRuntimeLinker {};
    EtsClass *coreMemoryRuntimeLinker {};
    EtsClass *coreAbcFile {};

    /* Error handling */
    EtsClass *coreOutOfMemoryError {};
    EtsClass *coreStackTraceElement {};
    EtsClass *coreLinkerClassNotFoundError {};

    /* StringBuilder */
    EtsClass *coreStringBuilder {};

    /* Concurrency classes */
    EtsClass *corePromise {};
    EtsClass *coreJob {};
    EtsMethod *corePromiseSubscribeOnAnotherPromise {};
    EtsClass *corePromiseRef {};
    EtsClass *coreWaitersList {};
    EtsClass *coreMutex {};
    EtsClass *coreEvent {};
    EtsClass *coreCondVar {};
    EtsClass *coreQueueSpinlock {};
    EtsClass *coreRWLock {};

    /* Finalization */
    EtsClass *coreFinalizableWeakRef {};
    EtsClass *coreFinalizationRegistry {};
    EtsMethod *coreFinalizationRegistryExecCleanup {};

    /* Containers */

    EtsClass *escompatArray {};
    EtsMethod *escompatArrayPop {};
    EtsMethod *escompatArrayGetLength {};
    EtsMethod *escompatArrayGet {};
    EtsMethod *escompatArraySet {};
    EtsClass *coreArrayBuffer {};
    EtsClass *escompatInt8Array {};
    EtsClass *escompatUint8Array {};
    EtsClass *escompatUint8ClampedArray {};
    EtsClass *escompatInt16Array {};
    EtsClass *escompatUint16Array {};
    EtsClass *escompatInt32Array {};
    EtsClass *escompatUint32Array {};
    EtsClass *escompatFloat32Array {};
    EtsClass *escompatFloat64Array {};
    EtsClass *escompatBigInt64Array {};
    EtsClass *escompatBigUint64Array {};
    EtsClass *containersArrayAsListInt {};
    EtsClass *coreRecord {};
    EtsMethod *coreRecordGetter {};
    EtsMethod *coreRecordSetter {};
    EtsClass *coreMap {};
    EtsClass *coreMapEntry {};
    EtsClass *coreSet {};

    /* InteropJS */
    EtsClass *interopJSValue {};

    /* TypeAPI */
    EtsClass *coreField {};
    EtsClass *coreMethod {};
    EtsClass *coreParameter {};
    EtsClass *coreClassType {};

    EtsClass *reflectInstanceField {};
    EtsClass *reflectInstanceMethod {};
    EtsClass *reflectStaticField {};
    EtsClass *reflectStaticMethod {};
    EtsClass *reflectConstructor {};

    /* Proxy */
    EtsClass *coreReflectProxy {};
    EtsMethod *coreReflectProxyConstructor {};
    EtsMethod *coreReflectProxyInvoke {};
    EtsMethod *coreReflectProxyInvokeSet {};
    EtsMethod *coreReflectProxyInvokeGet {};

    /* std.core.Process */
    EtsClass *coreProcess {};
    EtsMethod *coreProcessListUnhandledJobs {};
    EtsMethod *coreProcessListUnhandledPromises {};
    EtsMethod *coreProcessHandleUncaughtError {};

    EtsClass *stdcoreRegExpExecArray {};
    EtsClass *escompatJsonReplacer {};
    EtsClass *escompatErrorOptions {};
    EtsClass *escompatErrorOptionsImpl {};

    struct Entry {
        size_t slotIndex {};
    };

    /* Internal Caches */
    void CreateAndInitializeCaches();
    void VisitRoots(const GCRootVisitor &visitor) const;
    EtsTypedObjectArray<EtsString> *GetAsciiCacheTable() const
    {
        return asciiCharCache_;
    }
    Entry const *GetTypeEntry(const uint8_t *descriptor) const;

    // Managed object caches:
    static constexpr uint32_t GetAsciiCharCacheSize()
    {
        return ASCII_CHAR_TABLE_SIZE;
    }
    static constexpr size_t GetAsciiCharCacheOffset()
    {
        return MEMBER_OFFSET(EtsPlatformTypes, asciiCharCache_);
    }

    // Class entry offsets:
    static constexpr size_t GetEscompatArrayClassOffset()
    {
        return MEMBER_OFFSET(EtsPlatformTypes, escompatArray);
    }
    static constexpr size_t GetCoreLineStringClassOffset()
    {
        return MEMBER_OFFSET(EtsPlatformTypes, coreLineString);
    }
    static constexpr size_t GetCoreSlicedStringClassOffset()
    {
        return MEMBER_OFFSET(EtsPlatformTypes, coreSlicedString);
    }
    static constexpr size_t GetCoreTreeStringClassOffset()
    {
        return MEMBER_OFFSET(EtsPlatformTypes, coreTreeString);
    }

private:
    friend class EtsClassLinkerExtension;
    friend class mem::Allocator;
    // asciiCharCache_ must be allocated in a non-movable heap region; therefore, we should need to handle
    // this pointer in `ark::ets::PandaEtsVM::UpdateVmRefs`.
    EtsTypedObjectArray<EtsString> *asciiCharCache_ {nullptr};
    void PreloadType(EtsClassLinker *linker, EtsClass **slot, std::string_view descriptor);
    PandaUnorderedMap<const uint8_t *, Entry, utf::Mutf8Hash, utf::Mutf8Equal> entryTable_;

    explicit EtsPlatformTypes(EtsCoroutine *coro);

    /**
     * @brief Initialize all classes.
     * This method must be called after construction of `EtsPlatformTypes`
     * to ensure correct initialization of all classes
     */
    void InitializeClasses(EtsCoroutine *coro);
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

// Obtain EtsPlatformTypes pointer cached in the coroutine
ALWAYS_INLINE inline EtsPlatformTypes const *PlatformTypes(EtsCoroutine *coro)
{
    ASSERT(coro != nullptr);
    return coro->GetLocalStorage().Get<EtsCoroutine::DataIdx::ETS_PLATFORM_TYPES_PTR, EtsPlatformTypes *>();
}

// Obtain EtsPlatfromTypes pointer from the VM
EtsPlatformTypes const *PlatformTypes(PandaEtsVM *vm);

// Obtain EtsPlatfromTypes pointer from the current VM
EtsPlatformTypes const *PlatformTypes();

}  // namespace ark::ets

#endif  // !PANDA_PLUGINS_ETS_RUNTIME_PLATFORM_TYPES_H_
