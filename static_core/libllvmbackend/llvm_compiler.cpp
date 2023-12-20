/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#include "optimizer/ir/graph.h"
#include "compiler_options.h"

#include "llvm_compiler.h"
#include "llvm_ark_interface.h"
#include "llvm_logger.h"
#include "llvm_options.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/ExecutionEngine/Interpreter.h>
#include <llvm/InitializePasses.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/PassRegistry.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/StringSaver.h>

namespace panda::llvmbackend {

LLVMCompiler::LLVMCompiler(Arch arch) : arch_(arch)
{
#ifndef REQUIRED_LLVM_VERSION
#error Internal build error, missing cmake variable
#else
#define STRINGIFY(s) STR(s)  // NOLINT(cppcoreguidelines-macro-usage)
#define STR(s) #s            // NOLINT(cppcoreguidelines-macro-usage)
    // LLVM_VERSION_STRING is defined in llvm-config.h
    // REQUIRED_LLVM_VERSION is defined in libllvmbackend/CMakeLists.txt
    static_assert(std::string_view {LLVM_VERSION_STRING} == STRINGIFY(REQUIRED_LLVM_VERSION));

    const std::string current_llvm_lib_version = llvm::LLVMContext::getLLVMVersion();
    if (current_llvm_lib_version != STRINGIFY(REQUIRED_LLVM_VERSION)) {
        llvm::report_fatal_error(llvm::Twine("Incompatible LLVM version " + current_llvm_lib_version + ". " +
                                             std::string(STRINGIFY(REQUIRED_LLVM_VERSION)) + " is required."),
                                 false);
    }
#undef STR
#undef STRINGIFY
#endif
    InitializeLLVMTarget(arch);
    InitializeLLVMPasses();
    InitializeLLVMOptions();
#ifdef NDEBUG
    context_.setDiscardValueNames(true);
#endif
    context_.setOpaquePointers(true);

    LLVMLogger::Init(OPTIONS.GetLlvmLog());
}

panda::llvmbackend::LLVMCompilerOptions LLVMCompiler::InitializeLLVMCompilerOptions()
{
    panda::llvmbackend::LLVMCompilerOptions llvm_compiler_options {};
    llvm_compiler_options.optimize = !panda::compiler::OPTIONS.IsCompilerNonOptimizing();
    llvm_compiler_options.optlevel = llvm_compiler_options.optimize ? 2U : 0U;
    llvm_compiler_options.dump_module_after_optimizations = OPTIONS.IsLlvmDumpAfter();
    llvm_compiler_options.dump_module_before_optimizations = OPTIONS.IsLlvmDumpBefore();
    llvm_compiler_options.inline_module_file = OPTIONS.GetLlvmInlineModule();
    llvm_compiler_options.pipeline_file = OPTIONS.GetLlvmPipeline();

    llvm_compiler_options.do_irtoc_inline = !llvm_compiler_options.inline_module_file.empty();

    return llvm_compiler_options;
}

void LLVMCompiler::InitializeDefaultLLVMOptions()
{
    if (panda::compiler::OPTIONS.IsCompilerNonOptimizing()) {
        constexpr auto DISABLE = llvm::cl::boolOrDefault::BOU_FALSE;
        SetLLVMOption("fast-isel", DISABLE);
        SetLLVMOption("global-isel", DISABLE);
    }
}

void LLVMCompiler::InitializeLLVMOptions()
{
    llvm::cl::ResetAllOptionOccurrences();
    InitializeDefaultLLVMOptions();
    auto llvm_options_str = OPTIONS.GetLlvmOptions();
    if (llvm_options_str.length() == 0) {
        return;
    }

    llvm::BumpPtrAllocator alloc;
    llvm::StringSaver string_saver(alloc);
    llvm::SmallVector<const char *, 0> parsed_argv;
    parsed_argv.emplace_back("");  // First argument is an executable
    llvm::cl::TokenizeGNUCommandLine(llvm_options_str, string_saver, parsed_argv);
    llvm::cl::ParseCommandLineOptions(parsed_argv.size(), parsed_argv.data());
}

template <typename T>
void LLVMCompiler::SetLLVMOption(const char *option, T val)
{
    auto opts = llvm::cl::getRegisteredOptions();
    auto entry = opts.find(option);
    ASSERT(entry != opts.end());
    static_cast<llvm::cl::opt<T> *>(entry->second)->setValue(val);
}

// Instantiate for irtoc_compiler.cpp
template void LLVMCompiler::SetLLVMOption(const char *option, bool val);

llvm::Triple LLVMCompiler::GetTripleForArch(Arch arch)
{
    std::string error;
    std::string triple_name;
    switch (arch) {
        case Arch::AARCH64:
            triple_name = OPTIONS.GetLlvmTriple();
            break;
        case Arch::X86_64:
#ifdef PANDA_TARGET_LINUX
            triple_name = OPTIONS.WasSetLlvmTriple() ? OPTIONS.GetLlvmTriple() : "x86_64-unknown-linux-gnu";
#elif defined(PANDA_TARGET_MACOS)
            triple_name = OPTIONS.WasSetLlvmTriple() ? OPTIONS.GetLlvmTriple() : "x86_64-apple-darwin-gnu";
#elif defined(PANDA_TARGET_WINDOWS)
            triple_name = OPTIONS.WasSetLlvmTriple() ? OPTIONS.GetLlvmTriple() : "x86_64-unknown-windows-unknown";
#else
            triple_name = OPTIONS.WasSetLlvmTriple() ? OPTIONS.GetLlvmTriple() : "x86_64-unknown-unknown-unknown";
#endif
            break;

        default:
            UNREACHABLE();
    }
    llvm::Triple triple(llvm::Triple::normalize(triple_name));
    [[maybe_unused]] auto target = llvm::TargetRegistry::lookupTarget("", triple, error);
    ASSERT_PRINT(target != nullptr, error);
    return triple;
}

std::string LLVMCompiler::GetCPUForArch(Arch arch)
{
    std::string cpu;
    switch (arch) {
        case Arch::AARCH64:
            cpu = OPTIONS.GetLlvmCpu();
            break;
        case Arch::X86_64:
            cpu = OPTIONS.WasSetLlvmCpu() ? OPTIONS.GetLlvmCpu() : "";
            break;
        default:
            UNREACHABLE();
    }
    return cpu;
}

void LLVMCompiler::InitializeLLVMTarget(Arch arch)
{
    switch (arch) {
        case Arch::AARCH64: {
            LLVMInitializeAArch64TargetInfo();
            LLVMInitializeAArch64Target();
            LLVMInitializeAArch64TargetMC();
            LLVMInitializeAArch64AsmPrinter();
            LLVMInitializeAArch64AsmParser();
            break;
        }
#ifdef PANDA_TARGET_AMD64
        case Arch::X86_64: {
            LLVMInitializeX86TargetInfo();
            LLVMInitializeX86Target();
            LLVMInitializeX86TargetMC();
            LLVMInitializeX86AsmPrinter();
            LLVMInitializeX86AsmParser();
            break;
        }
#endif
        default:
            LLVM_LOG(FATAL, INFRA) << GetArchString(arch) << std::string(" is unsupported. ");
    }
    LLVM_LOG(DEBUG, INFRA) << "Available targets";
    for (auto target : llvm::TargetRegistry::targets()) {
        LLVM_LOG(DEBUG, INFRA) << "\t" << target.getName();
    }
}

void LLVMCompiler::InitializeLLVMPasses()
{
    llvm::PassRegistry &registry = *llvm::PassRegistry::getPassRegistry();
    initializeCore(registry);
    initializeScalarOpts(registry);
    initializeObjCARCOpts(registry);
    initializeVectorization(registry);
    initializeIPO(registry);
    initializeAnalysis(registry);
    initializeTransformUtils(registry);
    initializeInstCombine(registry);
    initializeAggressiveInstCombine(registry);
    initializeInstrumentation(registry);
    initializeTarget(registry);
    initializeCodeGen(registry);
    initializeGlobalISel(registry);
}

}  // namespace panda::llvmbackend
