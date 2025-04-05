/**
 * Copyright (c) 2021-2025 Huawei Device Co., Ltd.
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

#include "plugins/ets/runtime/ets_vm.h"
#include <atomic>

#include "compiler/optimizer/ir/runtime_interface.h"
#include "include/mem/panda_smart_pointers.h"
#include "include/mem/panda_string.h"
#include "libpandabase/macros.h"
#include "plugins/ets/runtime/ani/ani_vm_api.h"
#include "plugins/ets/runtime/ets_class_linker_extension.h"
#include "plugins/ets/runtime/ets_coroutine.h"
#include "plugins/ets/runtime/ets_exceptions.h"
#include "plugins/ets/runtime/ets_handle.h"
#include "plugins/ets/runtime/ets_handle_scope.h"
#include "plugins/ets/runtime/ets_panda_file_items.h"
#include "plugins/ets/runtime/ets_runtime_interface.h"
#include "plugins/ets/runtime/mem/ets_reference_processor.h"
#include "plugins/ets/runtime/napi/ets_mangle.h"
#include "plugins/ets/runtime/napi/ets_napi_invoke_interface.h"
#include "plugins/ets/runtime/types/ets_method.h"
#include "plugins/ets/runtime/types/ets_promise.h"
#include "plugins/ets/runtime/types/ets_string.h"
#include "plugins/ets/runtime/types/ets_array.h"
#include "runtime/compiler.h"
#include "runtime/include/runtime.h"
#include "runtime/include/thread_scopes.h"
#include "runtime/init_icu.h"
#include "runtime/coroutines/stackful_coroutine_manager.h"
#include "runtime/coroutines/threaded_coroutine_manager.h"
#include "runtime/mem/lock_config_helper.h"
#include "plugins/ets/stdlib/native/init_native_methods.h"
#include "plugins/ets/runtime/types/ets_abc_runtime_linker.h"
#include "plugins/ets/runtime/types/ets_finalizable_weak_ref_list.h"

#include "plugins/ets/runtime/intrinsics/helpers/ets_to_string_cache.h"

#include "plugins/ets/runtime/ets_object_state_table.h"

namespace ark::ets {
// Create MemoryManager by RuntimeOptions
static mem::MemoryManager *CreateMM(Runtime *runtime, const RuntimeOptions &options)
{
    mem::MemoryManager::HeapOptions heapOptions {
        nullptr,                                      // is_object_finalizeble_func
        nullptr,                                      // register_finalize_reference_func
        options.GetMaxGlobalRefSize(),                // max_global_ref_size
        options.IsGlobalReferenceSizeCheckEnabled(),  // is_global_reference_size_check_enabled
        MT_MODE_TASK,                                 // multithreading mode
        options.IsUseTlabForAllocations(),            // is_use_tlab_for_allocations
        options.IsStartAsZygote(),                    // is_start_as_zygote
    };

    auto ctx = runtime->GetLanguageContext(panda_file::SourceLang::ETS);
    auto allocator = runtime->GetInternalAllocator();

    mem::GCTriggerConfig gcTriggerConfig(options, panda_file::SourceLang::ETS);

    mem::GCSettings gcSettings(options, panda_file::SourceLang::ETS);

    auto gcType = Runtime::GetGCType(options, panda_file::SourceLang::ETS);

    return mem::MemoryManager::Create(ctx, allocator, gcType, gcSettings, gcTriggerConfig, heapOptions);
}

/* static */
bool PandaEtsVM::CreateTaskManagerIfNeeded(const RuntimeOptions &options)
{
    if (options.GetWorkersType() == "taskmanager" && Runtime::GetTaskScheduler() == nullptr) {
        auto *taskScheduler = taskmanager::TaskScheduler::Create(
            options.GetTaskmanagerWorkersCount(), taskmanager::StringToTaskTimeStats(options.GetTaskStatsType()));
        if (taskScheduler == nullptr) {
            return false;
        }
        Runtime::SetTaskScheduler(taskScheduler);
    }
    return true;
}

/* static */
Expected<PandaEtsVM *, PandaString> PandaEtsVM::Create(Runtime *runtime, const RuntimeOptions &options)
{
    ASSERT(runtime != nullptr);

    if (!PandaEtsVM::CreateTaskManagerIfNeeded(options)) {
        return Unexpected(PandaString("Cannot create TaskManager"));
    }

    auto mm = CreateMM(runtime, options);
    if (mm == nullptr) {
        return Unexpected(PandaString("Cannot create MemoryManager"));
    }

    auto allocator = mm->GetHeapManager()->GetInternalAllocator();
    auto vm = allocator->New<PandaEtsVM>(runtime, options, mm);
    if (vm == nullptr) {
        return Unexpected(PandaString("Cannot create PandaCoreVM"));
    }

    auto classLinker = EtsClassLinker::Create(runtime->GetClassLinker());
    if (!classLinker) {
        allocator->Delete(vm);
        mem::MemoryManager::Destroy(mm);
        return Unexpected(classLinker.Error());
    }
    vm->classLinker_ = std::move(classLinker.Value());

    vm->InitializeGC();

    const auto &icuPath = options.GetIcuDataPath();
    if (icuPath == "default") {
        SetIcuDirectory();
    } else {
        u_setDataDirectory(icuPath.c_str());
    }

    CoroutineManagerConfig cfg {
        // enable drain queue interface
        options.IsCoroutineEnableFeaturesAniDrainQueue(plugins::LangToRuntimeType(panda_file::SourceLang::ETS)),
        // enable migration
        options.IsCoroutineEnableFeaturesMigration(plugins::LangToRuntimeType(panda_file::SourceLang::ETS)),
        // enable migrate awakened coroutines
        options.IsCoroutineEnableFeaturesMigrateAwait(plugins::LangToRuntimeType(panda_file::SourceLang::ETS)),
        // workers_count
        options.GetCoroutineWorkersCount(plugins::LangToRuntimeType(panda_file::SourceLang::ETS)),
        // exclusive workers limit
        options.GetCoroutineEWorkersLimit(plugins::LangToRuntimeType(panda_file::SourceLang::ETS)),
        // enable perf stats
        options.IsCoroutineDumpStats(plugins::LangToRuntimeType(panda_file::SourceLang::ETS))};
    vm->coroutineManager_->Initialize(cfg, runtime, vm);

    return vm;
}

bool PandaEtsVM::Destroy(PandaEtsVM *vm)
{
    if (vm == nullptr) {
        return false;
    }

    vm->SaveProfileInfo();
    vm->UninitializeThreads();
    vm->StopGC();

    auto runtime = Runtime::GetCurrent();
    runtime->GetInternalAllocator()->Delete(vm);

    return true;
}

PandaEtsVM::PandaEtsVM(Runtime *runtime, const RuntimeOptions &options, mem::MemoryManager *mm)
    : EtsVM {napi::GetInvokeInterface()}, ani_vm {ani::GetVMAPI()}, runtime_(runtime), mm_(mm)
{
    ASSERT(runtime_ != nullptr);
    ASSERT(mm_ != nullptr);

    auto heapManager = mm_->GetHeapManager();
    auto allocator = heapManager->GetInternalAllocator();

    runtimeIface_ = allocator->New<EtsRuntimeInterface>();
    compiler_ = allocator->New<Compiler>(heapManager->GetCodeAllocator(), allocator, options,
                                         heapManager->GetMemStats(), runtimeIface_);
    stringTable_ = allocator->New<StringTable>();
    monitorPool_ = allocator->New<MonitorPool>(allocator);
    referenceProcessor_ = allocator->New<mem::ets::EtsReferenceProcessor>(mm_->GetGC());

    auto langStr = plugins::LangToRuntimeType(panda_file::SourceLang::ETS);
    const auto &coroType = options.GetCoroutineImpl(langStr);
    if (coroType == "stackful") {
        coroutineManager_ = allocator->New<StackfulCoroutineManager>(EtsCoroutine::Create<Coroutine>);
    } else {
        coroutineManager_ = allocator->New<ThreadedCoroutineManager>(EtsCoroutine::Create<Coroutine>);
    }
    rendezvous_ = allocator->New<Rendezvous>(this);
    objStateTable_ = MakePandaUnique<EtsObjectStateTable>(allocator);
    InitializeRandomEngine();
}

PandaEtsVM::~PandaEtsVM()
{
    auto allocator = mm_->GetHeapManager()->GetInternalAllocator();
    ASSERT(allocator != nullptr);

    allocator->Delete(rendezvous_);
    allocator->Delete(runtimeIface_);
    allocator->Delete(coroutineManager_);
    allocator->Delete(referenceProcessor_);
    allocator->Delete(monitorPool_);
    allocator->Delete(stringTable_);
    allocator->Delete(compiler_);

    objStateTable_.reset();

    ASSERT(mm_ != nullptr);
    mm_->Finalize();
    mem::MemoryManager::Destroy(mm_);
}

PandaEtsVM *PandaEtsVM::GetCurrent()
{
    // Use Thread class for possible to use it from native and manage threads
    return static_cast<PandaEtsVM *>(Thread::GetCurrent()->GetVM());
}

static void PreallocSpecialReference(PandaEtsVM *vm, mem::Reference *&ref, const char *desc, bool nonMovable = false)
{
    EtsClass *cls = vm->GetClassLinker()->GetClass(desc);
    if (cls == nullptr) {
        LOG(FATAL, RUNTIME) << "Cannot find a class for special object " << desc;
    }
    EtsObject *obj = nonMovable ? EtsObject::CreateNonMovable(cls) : EtsObject::Create(cls);
    if (obj == nullptr) {
        LOG(FATAL, RUNTIME) << "Cannot preallocate a special object " << desc;
    }
    ref = vm->GetGlobalObjectStorage()->Add(obj->GetCoreType(), ark::mem::Reference::ObjectType::GLOBAL);
}

bool PandaEtsVM::Initialize()
{
    if (!ark::intrinsics::Initialize(ark::panda_file::SourceLang::ETS)) {
        LOG(ERROR, RUNTIME) << "Failed to initialize eTS intrinsics";
        return false;
    }

    if (!classLinker_->Initialize()) {
        LOG(FATAL, ETS) << "Cannot initialize ets class linker";
    }
    classLinker_->GetEtsClassLinkerExtension()->InitializeBuiltinClasses();

    if (Runtime::GetOptions().ShouldLoadBootPandaFiles()) {
        // NOLINTNEXTLINE(google-build-using-namespace)
        using namespace panda_file_items::class_descriptors;

        PreallocSpecialReference(this, oomObjRef_, OUT_OF_MEMORY_ERROR.data());
        PreallocSpecialReference(this, nullValueRef_, NULL_VALUE.data(), true);
        PreallocSpecialReference(this, finalizableWeakRefList_, FINALIZABLE_WEAK_REF.data());

        if (Thread::GetCurrent() != nullptr) {
            ASSERT(GetThreadManager()->GetMainThread() == Thread::GetCurrent());
            auto *coro = EtsCoroutine::GetCurrent();
            coro->SetupNullValue(GetNullValue());
            coro->GetLocalStorage().Set<EtsCoroutine::DataIdx::ETS_PLATFORM_TYPES_PTR>(
                ToUintPtr(classLinker_->GetEtsClassLinkerExtension()->GetPlatformTypes()));
            ASSERT(PlatformTypes(coro) != nullptr);

            doubleToStringCache_ = DoubleToStringCache::Create(coro);
            floatToStringCache_ = FloatToStringCache::Create(coro);
            longToStringCache_ = LongToStringCache::Create(coro);
        }
        referenceProcessor_->Initialize();
    }
    [[maybe_unused]] bool cachesCreated =
        (doubleToStringCache_ != nullptr && floatToStringCache_ != nullptr && longToStringCache_ != nullptr);
    LOG_IF(!cachesCreated, WARNING, ETS) << "Cannot initialize number-to-string caches";
    LOG_IF(cachesCreated, DEBUG, ETS) << "Initialized number-to-string caches";

    // Check if Intrinsics/native methods should be initialized, we don't want to attempt to
    // initialize  native methods in certain scenarios where we don't have ets stdlib at our disposal
    if (Runtime::GetOptions().ShouldInitializeIntrinsics()) {
        // NOTE(ksarychev, #18135): Implement napi module registration via loading a separate
        // library
        ani_env *env = EtsCoroutine::GetCurrent()->GetEtsNapiEnv();
        ark::ets::stdlib::InitNativeMethods(env);
    }

    const auto lang = plugins::LangToRuntimeType(panda_file::SourceLang::ETS);
    for (const auto &path : Runtime::GetOptions().GetNativeLibraryPath(lang)) {
        nativeLibraryProvider_.AddLibraryPath(ConvertToString(path));
    }

    return true;
}

bool PandaEtsVM::InitializeFinish()
{
    if (Runtime::GetOptions().ShouldLoadBootPandaFiles()) {
        // Preinitialize StackOverflowError, so we don't need to do this when stack overflow occurred
        EtsClass *cls = classLinker_->GetClass(panda_file_items::class_descriptors::STACK_OVERFLOW_ERROR.data());
        if (cls == nullptr) {
            LOG(FATAL, ETS) << "Cannot preinitialize StackOverflowError";
            return false;
        }
    }
    return true;
}

void PandaEtsVM::UninitializeThreads()
{
    // Wait until all threads finish the work
    coroutineManager_->WaitForDeregistration();
    coroutineManager_->DestroyMainCoroutine();
    coroutineManager_->Finalize();
}

void PandaEtsVM::PreStartup()
{
    ASSERT(mm_ != nullptr);

    mm_->PreStartup();
}

void PandaEtsVM::PreZygoteFork()
{
    ASSERT(mm_ != nullptr);
    ASSERT(compiler_ != nullptr);

    mm_->PreZygoteFork();
    compiler_->PreZygoteFork();
}

void PandaEtsVM::PostZygoteFork()
{
    ASSERT(compiler_ != nullptr);
    ASSERT(mm_ != nullptr);

    compiler_->PostZygoteFork();
    mm_->PostZygoteFork();
}

void PandaEtsVM::InitializeGC()
{
    ASSERT(mm_ != nullptr);

    mm_->InitializeGC(this);
}

void PandaEtsVM::StartGC()
{
    ASSERT(mm_ != nullptr);

    mm_->StartGC();
}

void PandaEtsVM::StopGC()
{
    ASSERT(mm_ != nullptr);

    if (GetGC()->IsGCRunning()) {
        mm_->StopGC();
    }
}

void PandaEtsVM::HandleReferences(const GCTask &task, const mem::GC::ReferenceClearPredicateT &pred)
{
    ASSERT(mm_ != nullptr);

    auto gc = mm_->GetGC();
    ASSERT(gc != nullptr);

    LOG(DEBUG, REF_PROC) << "Start processing cleared references";
    gc->ProcessReferences(gc->GetGCPhase(), task, pred);
    LOG(DEBUG, REF_PROC) << "Finish processing cleared references";

    GetGlobalObjectStorage()->ClearUnmarkedWeakRefs(gc, pred);
}

void PandaEtsVM::HandleGCRoutineInMutator()
{
    // Handle references only in coroutine
    ASSERT(Coroutine::GetCurrent() != nullptr);
    ASSERT(GetMutatorLock()->HasLock());
    auto coroutine = EtsCoroutine::GetCurrent();
    auto *coroManager = coroutine->GetCoroutineManager();

    if (finRegLastIndex_ != 0 && UpdateFinRegCoroCountAndCheckIfCleanupNeeded()) {
        auto *objArray =
            EtsObjectArray::FromCoreType(GetGlobalObjectStorage()->Get(registeredFinalizationRegistryInstancesRef_));
        auto *event = Runtime::GetCurrent()->GetInternalAllocator()->New<CompletionEvent>(nullptr, coroManager);
        Method *cleanup = PlatformTypes(this)->coreFinalizationRegistryExecCleanup->GetPandaMethod();
        auto args = PandaVector<Value> {Value(objArray->GetCoreType())};
        [[maybe_unused]] bool launchResult =
            coroManager->Launch(event, cleanup, std::move(args), CoroutineLaunchMode::SAME_WORKER);
        ASSERT(launchResult);
    }
    coroutine->GetPandaVM()->CleanFinalizableReferenceList();
}

void PandaEtsVM::HandleGCFinished() {}

bool PandaEtsVM::CheckEntrypointSignature(Method *entrypoint)
{
    ASSERT(entrypoint != nullptr);

    if (entrypoint->GetNumArgs() == 0) {
        return true;
    }

    if (entrypoint->GetNumArgs() > 1) {
        return false;
    }

    auto *pf = entrypoint->GetPandaFile();
    ASSERT(pf != nullptr);
    panda_file::MethodDataAccessor mda(*pf, entrypoint->GetFileId());
    panda_file::ProtoDataAccessor pda(*pf, mda.GetProtoId());

    if (pda.GetArgType(0).GetId() != panda_file::Type::TypeId::REFERENCE) {
        return false;
    }

    auto name = pf->GetStringData(pda.GetReferenceType(0));
    std::string_view expectedName(panda_file_items::class_descriptors::STRING_ARRAY);

    return utf::IsEqual({name.data, name.utf16Length},
                        {utf::CStringAsMutf8(expectedName.data()), expectedName.length()});
}

static EtsObjectArray *CreateArgumentsArray(const std::vector<std::string> &args, PandaEtsVM *etsVm)
{
    ASSERT(etsVm != nullptr);

    const char *classDescripor = panda_file_items::class_descriptors::STRING_ARRAY.data();
    EtsClass *arrayKlass = etsVm->GetClassLinker()->GetClass(classDescripor);
    if (arrayKlass == nullptr) {
        LOG(FATAL, RUNTIME) << "Class " << classDescripor << " not found";
        return nullptr;
    }

    EtsCoroutine *coroutine = EtsCoroutine::GetCurrent();
    [[maybe_unused]] EtsHandleScope scope(coroutine);
    EtsObjectArray *etsArray = EtsObjectArray::Create(arrayKlass, args.size());
    EtsHandle<EtsObjectArray> arrayHandle(coroutine, etsArray);

    for (size_t i = 0; i < args.size(); i++) {
        EtsString *str = EtsString::CreateFromMUtf8(args[i].data(), args[i].length());
        arrayHandle.GetPtr()->Set(i, str->AsObject());
    }

    return arrayHandle.GetPtr();
}

coretypes::String *PandaEtsVM::CreateString(Method *ctor, ObjectHeader *obj)
{
    EtsString *str = nullptr;
    ASSERT(ctor->GetNumArgs() > 0);  // must be at list this argument
    if (ctor->GetNumArgs() == 1) {
        str = EtsString::CreateNewEmptyString();
    } else if (ctor->GetNumArgs() == 2U) {
        ASSERT(ctor->GetArgType(1).GetId() == panda_file::Type::TypeId::REFERENCE);
        auto *strData = utf::Mutf8AsCString(ctor->GetRefArgType(1).data);
        if (std::strcmp("[C", strData) == 0) {
            auto *array = reinterpret_cast<EtsArray *>(obj);
            str = EtsString::CreateNewStringFromChars(0, array->GetLength(), array);
        } else if ((std::strcmp("Lstd/core/String;", strData) == 0)) {
            str = EtsString::CreateNewStringFromString(reinterpret_cast<EtsString *>(obj));
        } else {
            LOG(FATAL, ETS) << "Non-existent ctor";
        }
    } else {
        LOG(FATAL, ETS) << "Must be 1 or 2 ctor args";
    }
    return str->GetCoreType();
}

Expected<int, Runtime::Error> PandaEtsVM::InvokeEntrypointImpl(Method *entrypoint, const std::vector<std::string> &args)
{
    ASSERT(Runtime::GetCurrent()->GetLanguageContext(*entrypoint).GetLanguage() == panda_file::SourceLang::ETS);

    EtsCoroutine *coroutine = EtsCoroutine::GetCurrent();
    ASSERT(coroutine != nullptr);

    ScopedManagedCodeThread sj(coroutine);
    if (!classLinker_->InitializeClass(coroutine, EtsClass::FromRuntimeClass(entrypoint->GetClass()))) {
        LOG(ERROR, RUNTIME) << "Cannot initialize class '" << entrypoint->GetClass()->GetName() << "'";
        return Unexpected(Runtime::Error::CLASS_NOT_INITIALIZED);
    }

    [[maybe_unused]] EtsHandleScope scope(coroutine);
    if (entrypoint->GetNumArgs() == 0) {
        auto v = entrypoint->Invoke(coroutine, nullptr);
        return v.GetAs<int>();
    }

    if (entrypoint->GetNumArgs() == 1) {
        EtsObjectArray *etsObjectArray = CreateArgumentsArray(args, this);
        EtsHandle<EtsObjectArray> argsHandle(coroutine, etsObjectArray);
        Value argVal(argsHandle.GetPtr()->AsObject()->GetCoreType());
        auto v = entrypoint->Invoke(coroutine, &argVal);

        return v.GetAs<int>();
    }

    // What if entrypoint->GetNumArgs() > 1 ?
    LOG(ERROR, RUNTIME) << "ets entrypoint has args count more than 1 : " << entrypoint->GetNumArgs();
    return Unexpected(Runtime::Error::INVALID_ENTRY_POINT);
}

ObjectHeader *PandaEtsVM::GetOOMErrorObject()
{
    auto obj = GetGlobalObjectStorage()->Get(oomObjRef_);
    ASSERT(obj != nullptr);
    return obj;
}

ObjectHeader *PandaEtsVM::GetNullValue()
{
    auto obj = GetGlobalObjectStorage()->Get(nullValueRef_);
    ASSERT(obj != nullptr);
    return obj;
}

bool PandaEtsVM::LoadNativeLibrary(EtsEnv *env, const PandaString &name)
{
    ASSERT_PRINT(Coroutine::GetCurrent()->IsInNativeCode(), "LoadNativeLibrary must be called at native");

    if (auto error = nativeLibraryProvider_.LoadLibrary(env, name)) {
        LOG(ERROR, RUNTIME) << "Cannot load library " << name << ": " << error.value();
        return false;
    }

    return true;
}

void PandaEtsVM::ResolveNativeMethod(Method *method)
{
    ASSERT_PRINT(method->IsNative(), "Method should be native");
    std::string className = utf::Mutf8AsCString(method->GetClassName().data);
    std::string methodName = utf::Mutf8AsCString(method->GetName().data);
    std::string name = MangleMethodName(className, methodName);
    auto ptr = nativeLibraryProvider_.ResolveSymbol(PandaString(name));
    if (ptr == nullptr) {
        std::string signature;
        signature.append(EtsMethod::FromRuntimeMethod(method)->GetMethodSignature(false));
        name = MangleMethodNameWithSignature(name, signature);
        ptr = nativeLibraryProvider_.ResolveSymbol(PandaString(name));
        if (ptr == nullptr) {
            PandaStringStream ss;
            ss << "No implementation found for " << method->GetFullName() << ", tried " << name;

            auto coroutine = EtsCoroutine::GetCurrent();
            ThrowEtsException(coroutine, panda_file_items::class_descriptors::LINKER_UNRESOLVED_METHOD_ERROR, ss.str());
            return;
        }
    }

    method->SetNativePointer(ptr);
}

static void PrintExceptionInfo(EtsCoroutine *coro, EtsHandle<EtsObject> exception, PandaStringStream &ss)
{
    auto cls = exception->GetClass();

    PandaVector<uint8_t> strBuf;
    auto const performCall = [coro, &exception, &strBuf](EtsMethod *method) -> std::optional<std::string_view> {
        ASSERT(method != nullptr);
        std::array<Value, 1> args = {Value(exception->GetCoreType())};
        EtsObject *callRes = EtsObject::FromCoreType(
            EtsMethod::ToRuntimeMethod(method)->Invoke(coro, args.data()).GetAs<ObjectHeader *>());
        if (coro->HasPendingException() || callRes == EtsObject::FromCoreType(coro->GetNullValue())) {
            return std::nullopt;
        }
        return EtsString::FromEtsObject(callRes)->ConvertToStringView(&strBuf);
    };

    ss << std::endl << performCall(cls->GetMethod("toString")).value_or("invoke toString failed");
    if (PlatformTypes(coro)->escompatError->IsAssignableFrom(cls)) {
        ss << std::endl << performCall(cls->GetMethod("<get>stack")).value_or("exception dump failed");
    }
}

void PandaEtsVM::HandleUncaughtException()
{
    auto coro = EtsCoroutine::GetCurrent();
    ASSERT(coro != nullptr);
    ScopedManagedCodeThread sj(coro);
    [[maybe_unused]] EtsHandleScope scope(coro);

    EtsHandle<EtsObject> exception(coro, EtsObject::FromCoreType(coro->GetException()));

    PandaStringStream logStream;
    logStream << "Unhandled exception: " << exception->GetCoreType()->ClassAddr<Class>()->GetName();

    auto descr = exception->GetClass()->GetDescriptor();
    if (descr != panda_file_items::class_descriptors::OUT_OF_MEMORY_ERROR &&
        descr != panda_file_items::class_descriptors::STACK_OVERFLOW_ERROR) {
        coro->ClearException();
        PrintExceptionInfo(coro, exception, logStream);
    }
    LOG(ERROR, RUNTIME) << logStream.str();
    // _exit guarantees a safe completion in case of multi-threading as static destructors aren't called
    _exit(1);
}

void PandaEtsVM::SweepVmRefs(const GCObjectVisitor &gcObjectVisitor)
{
    PandaVM::SweepVmRefs(gcObjectVisitor);
}

void HandleEmptyArguments(const PandaVector<Value> &arguments, const GCRootVisitor &visitor,
                          const EtsCoroutine *coroutine)
{
    // arguments may be empty in the following cases:
    // 1. The entrypoint is static and doesn't accept any arguments
    // 2. The coroutine is launched.
    // 3. The entrypoint is the main method
    Method *entrypoint = coroutine->GetManagedEntrypoint();
    panda_file::ShortyIterator it(entrypoint->GetShorty());
    size_t argIdx = 0;
    ++it;  // skip return type
    if (!entrypoint->IsStatic()) {
        // handle 'this' argument
        ASSERT(arguments[argIdx].IsReference());
        ObjectHeader *arg = arguments[argIdx].GetAs<ObjectHeader *>();
        ASSERT(arg != nullptr);
        visitor(mem::GCRoot(mem::RootType::ROOT_THREAD, arg));
        ++argIdx;
    }
    while (it != panda_file::ShortyIterator()) {
        if ((*it).GetId() == panda_file::Type::TypeId::REFERENCE) {
            ASSERT(arguments[argIdx].IsReference());
            ObjectHeader *arg = arguments[argIdx].GetAs<ObjectHeader *>();
            if (arg != nullptr) {
                visitor(mem::GCRoot(mem::RootType::ROOT_THREAD, arg));
            }
        }
        ++it;
        ++argIdx;
    }
}

void PandaEtsVM::AddRootProvider(mem::RootProvider *provider)
{
    os::memory::LockHolder lock(rootProviderlock_);
    ASSERT(rootProviders_.find(provider) == rootProviders_.end());
    rootProviders_.insert(provider);
}

void PandaEtsVM::RemoveRootProvider(mem::RootProvider *provider)
{
    os::memory::LockHolder lock(rootProviderlock_);
    ASSERT(rootProviders_.find(provider) != rootProviders_.end());
    rootProviders_.erase(provider);
}

void PandaEtsVM::VisitVmRoots(const GCRootVisitor &visitor)
{
    GetThreadManager()->EnumerateThreads([visitor](ManagedThread *thread) {
        const auto coroutine = EtsCoroutine::CastFromThread(thread);
        if (auto etsNapiEnv = coroutine->GetEtsNapiEnv()) {
            auto etsStorage = etsNapiEnv->GetEtsReferenceStorage();
            etsStorage->GetAsReferenceStorage()->VisitObjects(visitor, mem::RootType::ROOT_NATIVE_LOCAL);
        }
        if (!coroutine->HasManagedEntrypoint()) {
            return true;
        }
        const PandaVector<Value> &arguments = coroutine->GetManagedEntrypointArguments();
        if (!arguments.empty()) {
            HandleEmptyArguments(arguments, visitor, coroutine);
        }
        return true;
    });
    visitor(mem::GCRoot(mem::RootType::ROOT_VM, doubleToStringCache_->GetCoreType()));
    visitor(mem::GCRoot(mem::RootType::ROOT_VM, floatToStringCache_->GetCoreType()));
    visitor(mem::GCRoot(mem::RootType::ROOT_VM, longToStringCache_->GetCoreType()));
    {
        os::memory::LockHolder lock(rootProviderlock_);
        for (auto *rootProvider : rootProviders_) {
            rootProvider->VisitRoots(visitor);
        }
    }
}

template <bool REF_CAN_BE_NULL>
void PandaEtsVM::UpdateMovedVmRef(Value &ref)
{
    ASSERT(ref.IsReference());
    ObjectHeader *arg = ref.GetAs<ObjectHeader *>();
    if constexpr (REF_CAN_BE_NULL) {
        if (arg == nullptr) {
            return;
        }
    } else {
        ASSERT(arg != nullptr);
    }
    if (arg->IsForwarded()) {
        ObjectHeader *forwardAddress = mem::GetForwardAddress(arg);
        ref = Value(forwardAddress);
        LOG(DEBUG, GC) << "Forward root object: " << arg << " -> " << forwardAddress;
    }
}

void PandaEtsVM::UpdateManagedEntrypointArgRefs(EtsCoroutine *coroutine)
{
    PandaVector<Value> &arguments = coroutine->GetManagedEntrypointArguments();
    if (!arguments.empty()) {
        // arguments may be empty in the following cases:
        // 1. The entrypoint is static and doesn't accept any arguments
        // 2. The coroutine is launched.
        // 3. The entrypoint is the main method
        Method *entrypoint = coroutine->GetManagedEntrypoint();
        panda_file::ShortyIterator it(entrypoint->GetShorty());
        size_t argIdx = 0;
        ++it;  // skip return type
        if (!entrypoint->IsStatic()) {
            // handle 'this' argument
            UpdateMovedVmRef<false>(arguments[argIdx]);
            ++argIdx;
        }
        while (it != panda_file::ShortyIterator()) {
            if ((*it).GetId() == panda_file::Type::TypeId::REFERENCE) {
                UpdateMovedVmRef<true>(arguments[argIdx]);
            }
            ++it;
            ++argIdx;
        }
    }
}

void PandaEtsVM::UpdateVmRefs()
{
    GetThreadManager()->EnumerateThreads([](ManagedThread *thread) {
        auto coroutine = EtsCoroutine::CastFromThread(thread);
        if (auto etsNapiEnv = coroutine->GetEtsNapiEnv()) {
            auto etsStorage = etsNapiEnv->GetEtsReferenceStorage();
            etsStorage->GetAsReferenceStorage()->UpdateMovedRefs();
        }
        if (!coroutine->HasManagedEntrypoint()) {
            return true;
        }
        UpdateManagedEntrypointArgRefs(coroutine);
        return true;
    });

    objStateTable_->EnumerateObjectStates([](EtsObjectStateInfo *info) {
        auto *obj = info->GetEtsObject()->GetCoreType();
        if (obj->IsForwarded()) {
            info->SetEtsObject(EtsObject::FromCoreType(ark::mem::GetForwardAddress(obj)));
        }
    });

    {
        os::memory::LockHolder lock(rootProviderlock_);
        for (auto *rootProvider : rootProviders_) {
            rootProvider->UpdateRefs();
        }
    }
}

static mem::Reference *EtsNapiObjectToGlobalReference(ets_object globalRef)
{
    ASSERT(globalRef != nullptr);
    auto *ref = reinterpret_cast<mem::Reference *>(globalRef);
    return ref;
}

void PandaEtsVM::DeleteGlobalRef(ets_object globalRef)
{
    auto *ref = EtsNapiObjectToGlobalReference(globalRef);
    if (!ref->IsGlobal()) {
        LOG(FATAL, ETS_NAPI) << "Try to remote non-global ref: " << std::hex << ref;
        return;
    }
    GetGlobalObjectStorage()->Remove(ref);
}

static mem::Reference *EtsNapiObjectToWeakReference(ets_weak weakRef)
{
    ASSERT(weakRef != nullptr);
    auto *ref = reinterpret_cast<mem::Reference *>(weakRef);
    return ref;
}

void PandaEtsVM::DeleteWeakGlobalRef(ets_weak weakRef)
{
    auto *ref = EtsNapiObjectToWeakReference(weakRef);
    if (!ref->IsWeak()) {
        LOG(FATAL, ETS_NAPI) << "Try to remote non-weak ref: " << std::hex << ref;
        return;
    }
    GetGlobalObjectStorage()->Remove(ref);
}

static void SortFinalizationRegistryArray(EtsHandle<EtsObjectArray> &objArrayHandle, size_t &finRegLastIndex)
{
    size_t head = 0;
    size_t tail = objArrayHandle->GetLength() - 1;
    while (head < tail) {
        while (head < tail && objArrayHandle->Get(head) != nullptr) {
            head++;
        }
        while (head < tail && objArrayHandle->Get(tail) == nullptr) {
            tail--;
        }
        if (head < tail) {
            objArrayHandle->Set(head, objArrayHandle->Get(tail));
            objArrayHandle->Set(tail, nullptr);
        }
    }
    finRegLastIndex = tail;
}

static void EnsureFinalizationRegistryInstancesCapacity(EtsCoroutine *coro, size_t &finRegLastIndex,
                                                        mem::Reference *&ref)
{
    auto *vm = coro->GetPandaVM();
    EtsClass *objectClass = vm->GetClassLinker()->GetClassRoot(EtsClassRoot::OBJECT);
    if (ref == nullptr) {
        constexpr uint32_t SIZE = 10;
        auto *objArray = EtsObjectArray::Create(objectClass, SIZE, SpaceType::SPACE_TYPE_NON_MOVABLE_OBJECT);
        ref = vm->GetGlobalObjectStorage()->Add(objArray->GetCoreType(), ark::mem::Reference::ObjectType::GLOBAL);
        finRegLastIndex = 0;
        return;
    }
    auto *objArray = EtsObjectArray::FromCoreType(vm->GetGlobalObjectStorage()->Get(ref));
    [[maybe_unused]] EtsHandleScope scope(coro);
    EtsHandle<EtsObjectArray> objArrayHandle(coro, objArray);
    ASSERT(objArrayHandle.GetPtr() != nullptr);
    size_t finRegCapacity = objArrayHandle->GetLength();
    if (finRegLastIndex >= finRegCapacity) {
        SortFinalizationRegistryArray(objArrayHandle, finRegLastIndex);
        if (finRegLastIndex >= finRegCapacity) {
            auto *newFinalizationRegistryInstances = EtsObjectArray::Create(objectClass, (finRegCapacity * 2U) + 1U,
                                                                            SpaceType::SPACE_TYPE_NON_MOVABLE_OBJECT);
            objArrayHandle->CopyDataTo(newFinalizationRegistryInstances);
            vm->GetGlobalObjectStorage()->Remove(ref);
            ref = vm->GetGlobalObjectStorage()->Add(newFinalizationRegistryInstances->GetCoreType(),
                                                    ark::mem::Reference::ObjectType::GLOBAL);
        }
    }
}

void PandaEtsVM::RegisterFinalizationRegistryInstance(EtsObject *instance)
{
    ASSERT_MANAGED_CODE();
    EtsCoroutine *coroutine = EtsCoroutine::GetCurrent();
    [[maybe_unused]] EtsHandleScope scope(coroutine);
    EtsHandle<EtsObject> instanceHandle(coroutine, instance);
    EnsureFinalizationRegistryInstancesCapacity(coroutine, finRegLastIndex_,
                                                registeredFinalizationRegistryInstancesRef_);
    auto *objArray =
        EtsObjectArray::FromCoreType(GetGlobalObjectStorage()->Get(registeredFinalizationRegistryInstancesRef_));
    ASSERT(objArray->GetLength() != 0);
    objArray->Set(finRegLastIndex_, instanceHandle.GetPtr());
    finRegLastIndex_++;
}

void PandaEtsVM::RegisterFinalizerForObject(EtsCoroutine *coro, const EtsHandle<EtsObject> &object,
                                            void (*finalizer)(void *), void *finalizerArg)
{
    ASSERT_MANAGED_CODE();
    auto *weakRef = EtsFinalizableWeakRef::Create(coro);
    weakRef->SetFinalizer(finalizer, finalizerArg);
    weakRef->SetReferent(object.GetPtr());
    auto *coreList = GetGlobalObjectStorage()->Get(finalizableWeakRefList_);
    auto *weakRefList = EtsFinalizableWeakRefList::FromCoreType(coreList);
    os::memory::LockHolder lh(finalizableWeakRefListLock_);
    weakRefList->Push(coro, weakRef);
}

void PandaEtsVM::CleanFinalizableReferenceList()
{
    auto *coreList = GetGlobalObjectStorage()->Get(finalizableWeakRefList_);
    auto *weakRefList = EtsFinalizableWeakRefList::FromCoreType(coreList);
    os::memory::LockHolder lh(finalizableWeakRefListLock_);
    weakRefList->UnlinkClearedReferences(EtsCoroutine::GetCurrent());
}

void PandaEtsVM::BeforeShutdown()
{
    ScopedManagedCodeThread managedScope(EtsCoroutine::GetCurrent());
    auto *coreList = GetGlobalObjectStorage()->Get(finalizableWeakRefList_);
    auto *weakRefList = EtsFinalizableWeakRefList::FromCoreType(coreList);
    weakRefList->TraverseAndFinalize();
}

ClassLinkerContext *PandaEtsVM::CreateApplicationRuntimeLinker(const PandaVector<PandaString> &abcFiles)
{
    auto *coro = EtsCoroutine::GetCurrent();
    ASSERT(coro != nullptr);

    [[maybe_unused]] ScopedManagedCodeThread sj(coro);
    [[maybe_unused]] EtsHandleScope scope(coro);

    const auto exceptionHandler = [this, coro]() {
        ASSERT(coro->HasPendingException());
        [[maybe_unused]] ScopedNativeCodeThread nj(coro);
        HandleUncaughtException();
        UNREACHABLE();
    };

    auto *klass = PlatformTypes(this)->coreAbcRuntimeLinker;
    EtsHandle<EtsAbcRuntimeLinker> linkerHandle(coro, EtsAbcRuntimeLinker::FromEtsObject(EtsObject::Create(klass)));

    EtsHandle<EtsObjectArray> pathsHandle(
        coro, EtsObjectArray::Create(classLinker_->GetClassRoot(EtsClassRoot::STRING), abcFiles.size()));
    for (size_t idx = 0; idx < abcFiles.size(); ++idx) {
        auto *str = EtsString::CreateFromMUtf8(abcFiles[idx].data(), abcFiles[idx].length());
        if (UNLIKELY(str == nullptr)) {
            // Handle possible OOM
            exceptionHandler();
        }
        pathsHandle->Set(idx, str->AsObject());
    }
    std::array args {Value(linkerHandle->GetCoreType()), Value(nullptr), Value(pathsHandle->GetCoreType())};

    auto *ctor =
        klass->GetDirectMethod(GetLanguageContext().GetCtorName(), "Lstd/core/RuntimeLinker;[Lstd/core/String;:V");
    ASSERT(ctor != nullptr);
    ctor->GetPandaMethod()->InvokeVoid(coro, args.data());
    if (coro->HasPendingException()) {
        // Print exceptions thrown in constructor (e.g. if file not found) and exit
        exceptionHandler();
    }

    // Save global reference to created application `AbcRuntimeLinker`
    GetGlobalObjectStorage()->Add(linkerHandle->GetCoreType(), mem::Reference::ObjectType::GLOBAL);
    // Safe to return a non-managed object
    return linkerHandle->GetClassLinkerContext();
}

/* static */
void PandaEtsVM::Abort(const char *message /* = nullptr */)
{
    Runtime::Abort(message);
}
}  // namespace ark::ets
