/**
 * Copyright (c) 2021-2026 Huawei Device Co., Ltd.
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

#include "unit_test.h"
#include "libarkbase/os/exec.h"
#include "libarkbase/os/filesystem.h"
#include "assembly-parser.h"
#include "paoc_validation.h"

#include <fstream>
#include <initializer_list>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace ark::compiler {
namespace {

[[maybe_unused]] std::string GetExecPath()
{
    return ark::os::file::File::GetExecutablePath().Value();
}

[[maybe_unused]] std::string GetExecDirectory()
{
    auto execPath = GetExecPath();
    auto pos = execPath.rfind('/');
    return execPath.substr(0U, pos);
}

[[maybe_unused]] std::string GetPaocFile()
{
    return GetExecDirectory() + "/bin/ark_aot";
}

[[maybe_unused]] std::string GetPandaStdlibFile()
{
    return GetExecDirectory() + "/pandastdlib/arkstdlib.abc";
}

const char *ConvertExecArg(std::string &arg)
{
    return arg.c_str();
}

const char *ConvertExecArg(const char *arg)
{
    return arg;
}

template <typename... Args>
decltype(auto) Exec(Args... args)
{
    return os::exec::Exec(ConvertExecArg(args)...);
}

// Parses a paoc command line into `options`, mirroring how ark_aot registers its parser. Parsing accepts any
// value for an enum-typed option; the value-space check is performed separately by Options::Validate().
static bool ParsePaocOptions(ark::paoc::Options &options, const std::vector<const char *> &args)
{
    ark::PandArgParser parser;
    options.AddOptions(&parser);
    std::vector<const char *> argv {"ark_aot"};
    argv.insert(argv.end(), args.begin(), args.end());
    return parser.Parse(static_cast<int>(argv.size()), argv.data());
}

size_t EventCount(const std::string &fname, const std::regex &rgx)
{
    std::ifstream infile(fname);
    size_t found = 0;
    for (std::string line; std::getline(infile, line);) {
        if (std::regex_match(line, rgx)) {
            found++;
        }
    }
    return found;
}

size_t EventCount(TmpFile &file, const std::regex &rgx)
{
    return EventCount(file.GetFileName(), rgx);
}

}  // namespace

static void PaocAotPgoEmit(const TmpFile &pandaFname)
{
    auto source = R"(
.record Test1 {}
.record A1 {}
.record B1 <extends=A1> {}

.function i32 A1.func(A1 a0) {
    ldai 1
    return
}

.function i32 B1.func(B1 a0) {
    ldai 2
    return
}

.function i32 Test1.__noinline__call_func(A1 a0) {
    call.virt A1.func, a0
    return
}

.function i32 Test1.main() {
    newobj v0, B1
    movi v1, 10
    movi v2, 0
    movi v3, 0
loop:
    lda v1
    jeq v2, exit
    call.short Test1.__noinline__call_func, v0
    add2 v3
    sta v3
    inci v2, 1
    jmp loop
exit:
    lda v3
    return
}
    )";
    pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res);
    ASSERT_TRUE(pandasm::AsmEmitter::Emit(pandaFname.GetFileName(), res.Value()));
}

// R.leaf is a large straight-line method: its estimated inlined instruction count exceeds the
// regular inline budget used by the test, but fits the hot one.
static constexpr int LEAF_ADD_COUNT = 100;

// R::mid_prof inlines R::leaf twice in AotPgoPaocHotInlineBudget: once as an independent root and
// once again when nested under R::root_cold.
static constexpr size_t MID_PROF_LEAF_INLINE_EVENTS = 2;

static constexpr auto PGO_INLINE_BUDGET_HEAD = R"(
.record R {}

.function i32 R.leaf(i32 a0) {
    ldai 1
)";

static constexpr auto PGO_INLINE_BUDGET_TAIL = R"(
    return
}

.function i32 R.mid_cold(i32 a0) {
    movi v1, 3
    call.short R.leaf, a0
    lda v1
    return
}

.function i32 R.mid_prof(i32 a0) {
    lda a0
    jeqz skip_mid
    ldai 9
skip_mid:
    call.short R.leaf, a0
    lda a0
    return
}

.function i32 R.root_prof(i32 a0) {
    lda a0
    jeqz skip_root
    ldai 9
skip_root:
    call.short R.mid_cold, a0
    call.short R.leaf, a0
    return
}

.function i32 R.root_cold(i32 a0) {
    movi v1, 4
    call.short R.mid_prof, a0
    call.short R.leaf, a0
    return
}

.function i32 R.main() {
    movi v0, 1
    call.short R.root_prof, v0
    call.short R.root_cold, v0
    ldai 0
    return
}
)";

static void PaocAotPgoInlineBudgetEmit(const TmpFile &pandaFname)
{
    std::string leafAdds;
    for (int i = 0; i < LEAF_ADD_COUNT; i++) {
        leafAdds += "    add2 a0\n";
    }
    auto source = std::string(PGO_INLINE_BUDGET_HEAD) + leafAdds + PGO_INLINE_BUDGET_TAIL;
    pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res);
    ASSERT_TRUE(pandasm::AsmEmitter::Emit(pandaFname.GetFileName(), res.Value()));
}

static void WriteMethodsList(const TmpFile &file, std::initializer_list<const char *> methods)
{
    std::ofstream out(file.GetFileName());
    for (const char *method : methods) {
        out << method << '\n';
    }
}

static void RunPaocWithPolicy(const TmpFile &pandaFile, const TmpFile &profileFile, const TmpFile &eventsFile,
                              const TmpFile &aotFile, const char *policy)
{
    auto location = "--paoc-location=" + os::GetCurrentWorkingDirectory() + "/target";
    auto profileArg = std::string("--paoc-use-profile:path=") + profileFile.GetFileName() + ",force";
    if (policy != nullptr) {
        auto policyArg = std::string("--paoc-compilation-policy=") + policy;
        auto res = Exec(GetPaocFile(), "--paoc-panda-files", pandaFile.GetFileName(), location, "--events-output=csv",
                        "--events-file", eventsFile.GetFileName(), profileArg, policyArg, "--gc-type=epsilon",
                        "--paoc-output", aotFile.GetFileName());
        ASSERT_TRUE(res) << "paoc failed with error: " << res.Error().ToString();
        ASSERT_EQ(res.Value(), 0U);
        return;
    }
    auto res = Exec(GetPaocFile(), "--paoc-panda-files", pandaFile.GetFileName(), location, "--events-output=csv",
                    "--events-file", eventsFile.GetFileName(), profileArg, "--gc-type=epsilon", "--paoc-output",
                    aotFile.GetFileName());
    ASSERT_TRUE(res) << "paoc failed with error: " << res.Error().ToString();
    ASSERT_EQ(res.Value(), 0U);
}

class AotPgoPaocLocationTest : public ::testing::Test {
public:
    template <typename CallbackT>
    bool CreateRuntime(CallbackT optSetter, const std::string &pandaFile, std::optional<std::string> eventsFile)
    {
        pandaFile_ = pandaFile;

        RuntimeOptions options;
        options.SetHeapSizeLimit(50_MB);  // NOLINT(readability-magic-numbers)
        options.SetGcType("epsilon");
        options.SetBootPandaFiles({GetPandaStdlibFile()});
        options.SetLoadRuntimes({"core"});
        options.SetCompilerEnableJit(false);
        if (eventsFile.has_value()) {
            options.SetEventsOutput("csv");
            options.SetEventsFile(*eventsFile);
        }
        optSetter(options);
        auto pandaFiles = options.GetPandaFiles();
        pandaFiles.push_back(pandaFile_);
        options.SetPandaFiles(pandaFiles);
        Logger::InitializeDummyLogging();
        return Runtime::Create(options);
    }

    auto ExecutePandaFile(const std::string &entryPoint, const std::vector<std::string> &args = {})
    {
        auto res = Runtime::GetCurrent()->ExecutePandaFile(pandaFile_, entryPoint, args);
        EXPECT_TRUE(res) << "ark failed with error: " << static_cast<int>(res.Error());
        return res.ValueOr(-1);
    }

    void DestroyRuntime()
    {
        Runtime::Destroy();
    }

    void CollectMethodsProfile(const TmpFile &profileFile, const TmpFile &targetPandaFname, const TmpFile &eventsFile)
    {
        ASSERT_TRUE(CreateRuntime(
            [&profileFile](auto &options) {
                options.SetCompilerProfilingThreshold(0);
                options.SetProfilerEnabled(true);
                options.SetProfilesaverEnabled(true);
                options.SetProfileOutput(profileFile.GetFileName());
            },
            targetPandaFname.GetFileName(), eventsFile.GetFileName()));
        ASSERT_EQ(ExecutePandaFile("Test1::main"), 20U);
        DestroyRuntime();
    }

private:
    std::string pandaFile_;
};

TEST_F(AotPgoPaocLocationTest, AotPgoPaocLocation)
{
#ifndef PANDA_EVENTS_ENABLED
    GTEST_SKIP();
#endif

    // Test basic functionality only in host mode.
    if (RUNTIME_ARCH != Arch::X86_64) {
        return;
    }
    TmpFile pandaFile("test.pf");
    TmpFile profileFile("test.prof");
    TmpFile eventsFile("events-out.csv");
    TmpFile aotFile("./test.an");
    // Create directory for --paoc-location ("target" directory)
    os::CreateDirectories("target");
    TmpFile targetDir("target");
    TmpFile targetPandaFname("target/test.pf");

    // Create host panda file
    PaocAotPgoEmit(pandaFile);
    // Create "target" panda file
    PaocAotPgoEmit(targetPandaFname);

    // Collect profile with "target" panda file
    ASSERT_TRUE(CreateRuntime(
        [&profileFile](auto &options) {
            options.SetCompilerProfilingThreshold(0);
            options.SetProfilerEnabled(true);
            options.SetProfilesaverEnabled(true);
            options.SetProfileOutput(profileFile.GetFileName());
        },
        targetPandaFname.GetFileName(), eventsFile.GetFileName()));
    ASSERT_EQ(ExecutePandaFile("Test1::main"), 20U);
    DestroyRuntime();

    ASSERT_EQ(EventCount(eventsFile, std::regex("InterpProfiling,START,Test1::__noinline__call_func,1")), 1);

    // AOT-compile host panda file, specify "target" --paoc-location
    {
        auto res = Exec(GetPaocFile(), "--paoc-panda-files", pandaFile.GetFileName(),
                        "--paoc-location=" + os::GetCurrentWorkingDirectory() + "/target",
                        "--compiler-regex=Test1::__noinline__call_func", "--events-output=csv", "--events-file",
                        eventsFile.GetFileName(),
                        std::string("--paoc-use-profile:path=") + profileFile.GetFileName() + ",force",
                        "--gc-type=epsilon", "--paoc-output", aotFile.GetFileName());
        ASSERT_TRUE(res) << "paoc failed with error: " << res.Error().ToString();
        ASSERT_EQ(res.Value(), 0U);
    }

    // Check that inline caches from profile info are applied
    ASSERT_EQ(EventCount(eventsFile,
                         std::regex("Inline,Test1::__noinline__call_func,B1::func,.*VIRTUAL_MONOMORPHIC,SUCCESS")),
              1);

    // Run "target" panda file with AOT
    ASSERT_TRUE(CreateRuntime(
        [&aotFile](auto &options) {
            options.SetAotFiles({aotFile.GetFileName()});
            options.SetEnableAn(true);
        },
        targetPandaFname.GetFileName(), eventsFile.GetFileName()));
    ASSERT_EQ(ExecutePandaFile("Test1::main"), 20U);
    DestroyRuntime();

    ASSERT_EQ(EventCount(eventsFile, std::regex("Deoptimization,.*")), 0);
}

// These argument checks must fail before creating a runtime or opening the compilation input. They are
// exercised in-process (rather than by spawning ark_aot) so they do not depend on the ark_aot binary layout.
TEST(AotCompilationPolicyTest, ProfileGuidedRequiresProfilePath)
{
    for (const auto *extraArg : {"", "--paoc-use-profile", "--paoc-use-profile:path=", "--paoc-use-profile:force",
                                 "--paoc-methods-from-file:path=unused.txt"}) {
        SCOPED_TRACE(extraArg);
        ark::paoc::Options options("ark_aot");
        std::vector<const char *> args {"--paoc-panda-files=unused.pf", "--paoc-compilation-policy=profile-guided"};
        if (*extraArg != '\0') {
            args.push_back(extraArg);
        }
        ASSERT_TRUE(ParsePaocOptions(options, args));
        // profile-guided is a valid enum value, so the enum check must pass...
        ASSERT_FALSE(options.Validate().has_value());
        // ...but the profile-guided policy must still require a usable profile path.
        auto error = ark::paoc::ValidateProfileGuidedPolicy(options);
        ASSERT_TRUE(error.has_value());
        EXPECT_NE(error->GetMessage().find("requires --paoc-use-profile:path=<file>"), std::string::npos)
            << error->GetMessage();
    }
}

TEST(AotCompilationPolicyTest, RejectsUnknownPolicy)
{
    ark::paoc::Options options("ark_aot");
    ASSERT_TRUE(ParsePaocOptions(options, {"--paoc-panda-files=unused.pf", "--paoc-compilation-policy=unknown"}));
    auto error = options.Validate();
    ASSERT_TRUE(error.has_value());
    EXPECT_NE(error->GetMessage().find("argument --paoc-compilation-policy: invalid value"), std::string::npos)
        << error->GetMessage();
}

TEST_F(AotPgoPaocLocationTest, AotPgoPaocMethodsFromProfile)
{
#ifndef PANDA_EVENTS_ENABLED
    GTEST_SKIP();
#endif

    // Test basic functionality only in host mode.
    if (RUNTIME_ARCH != Arch::X86_64) {
        GTEST_SKIP() << "AOT PGO workflow requires an x86_64 host";
    }
    TmpFile pandaFile("test.pf");
    TmpFile profileFile("test.prof");
    TmpFile eventsFile("events-out.csv");
    TmpFile aotFile("./test.an");
    // Create directory for --paoc-location ("target" directory)
    os::CreateDirectories("target");
    TmpFile targetDir("target");
    TmpFile targetPandaFname("target/test.pf");

    // Create host panda file
    PaocAotPgoEmit(pandaFile);
    // Create "target" panda file
    PaocAotPgoEmit(targetPandaFname);

    // Collect profile with "target" panda file. Only methods executed at runtime are profiled: Test1::main calls
    // Test1::__noinline__call_func, which virtually dispatches to B1::func; A1::func is never invoked and hence cold.
    ASSERT_TRUE(CreateRuntime(
        [&profileFile](auto &options) {
            options.SetCompilerProfilingThreshold(0);
            options.SetProfilerEnabled(true);
            options.SetProfilesaverEnabled(true);
            options.SetProfileOutput(profileFile.GetFileName());
        },
        targetPandaFname.GetFileName(), eventsFile.GetFileName()));
    ASSERT_EQ(ExecutePandaFile("Test1::main"), 20U);
    DestroyRuntime();

    // AOT-compile host panda file with method selection from the profile; no --compiler-regex here, so the hot/cold
    // distinction is exercised across all methods.
    {
        auto res = Exec(
            GetPaocFile(), "--paoc-panda-files", pandaFile.GetFileName(),
            "--paoc-location=" + os::GetCurrentWorkingDirectory() + "/target", "--events-output=csv", "--events-file",
            eventsFile.GetFileName(), std::string("--paoc-use-profile:path=") + profileFile.GetFileName() + ",force",
            "--paoc-compilation-policy=profile-guided", "--gc-type=epsilon", "--paoc-output", aotFile.GetFileName());
        ASSERT_TRUE(res) << "paoc failed with error: " << res.Error().ToString();
        ASSERT_EQ(res.Value(), 0U);
    }

    // Methods that appear in the profile (have branch/inline-cache data) are compiled.
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,Test1::__noinline__call_func.*,COMPILED")), 1);
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,Test1::main.*,COMPILED")), 1);
    // Methods without profile entries (no branch/IC/throw data) are skipped.
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,A1::func.*,COMPILED")), 0);
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,B1::func.*,COMPILED")), 0);
}

TEST_F(AotPgoPaocLocationTest, AotPgoPaocMethodsSelectionOffByDefault)
{
#ifndef PANDA_EVENTS_ENABLED
    GTEST_SKIP();
#endif

    // Test basic functionality only in host mode.
    if (RUNTIME_ARCH != Arch::X86_64) {
        GTEST_SKIP() << "AOT PGO workflow requires an x86_64 host";
    }
    TmpFile pandaFile("test.pf");
    TmpFile profileFile("test.prof");
    TmpFile eventsDefaultFile("events-default.csv");
    TmpFile eventsExplicitDefaultFile("events-explicit-default.csv");
    TmpFile eventsOptInFile("events-optin.csv");
    TmpFile aotFile("./test.an");
    os::CreateDirectories("target");
    TmpFile targetDir("target");
    TmpFile targetPandaFname("target/test.pf");

    PaocAotPgoEmit(pandaFile);
    PaocAotPgoEmit(targetPandaFname);

    // Collect profile: Test1::main and Test1::__noinline__call_func carry branch/IC data;
    // A1::func and B1::func are trivial and never enter the profile.
    CollectMethodsProfile(profileFile, targetPandaFname, eventsDefaultFile);

    // The default policy does not restrict compilation to methods with profile data.
    RunPaocWithPolicy(pandaFile, profileFile, eventsDefaultFile, aotFile, nullptr);
    ASSERT_EQ(EventCount(eventsDefaultFile, std::regex("Compilation,Test1::__noinline__call_func.*,COMPILED")), 1);
    ASSERT_EQ(EventCount(eventsDefaultFile, std::regex("Compilation,Test1::main.*,COMPILED")), 1);
    ASSERT_EQ(EventCount(eventsDefaultFile, std::regex("Compilation,A1::func.*,COMPILED")), 1);
    ASSERT_EQ(EventCount(eventsDefaultFile, std::regex("Compilation,B1::func.*,COMPILED")), 1);

    // Explicit default and omitted policy must select the same methods when a profile is supplied.
    RunPaocWithPolicy(pandaFile, profileFile, eventsExplicitDefaultFile, aotFile, "default");
    for (const auto *method : {"Test1::__noinline__call_func", "Test1::main", "A1::func", "B1::func"}) {
        auto compiled = std::regex(std::string("Compilation,") + method + ".*,COMPILED");
        ASSERT_EQ(EventCount(eventsExplicitDefaultFile, compiled), EventCount(eventsDefaultFile, compiled));
    }

    // The profile-guided policy opts in to partial compilation.
    RunPaocWithPolicy(pandaFile, profileFile, eventsOptInFile, aotFile, "profile-guided");
    ASSERT_EQ(EventCount(eventsOptInFile, std::regex("Compilation,Test1::__noinline__call_func.*,COMPILED")), 1);
    ASSERT_EQ(EventCount(eventsOptInFile, std::regex("Compilation,Test1::main.*,COMPILED")), 1);
    ASSERT_EQ(EventCount(eventsOptInFile, std::regex("Compilation,A1::func.*,COMPILED")), 0);
    ASSERT_EQ(EventCount(eventsOptInFile, std::regex("Compilation,B1::func.*,COMPILED")), 0);
}

TEST_F(AotPgoPaocLocationTest, AotPgoPaocHotInlineBudget)
{
#ifndef PANDA_EVENTS_ENABLED
    GTEST_SKIP();
#endif

    if (RUNTIME_ARCH != Arch::X86_64) {
        GTEST_SKIP() << "AOT PGO workflow requires an x86_64 host";
    }
    TmpFile pandaFile("test.pf");
    TmpFile profileFile("test.prof");
    TmpFile methodsFile("methods.txt");
    TmpFile eventsFile("events-out.csv");
    TmpFile aotFile("./test.an");
    os::CreateDirectories("target");
    TmpFile targetDir("target");
    TmpFile targetPandaFname("target/test.pf");

    PaocAotPgoInlineBudgetEmit(pandaFile);
    PaocAotPgoInlineBudgetEmit(targetPandaFname);

    // Collect profile: R.root_prof and R.mid_prof carry executed branches and enter the profile;
    // R.leaf, R.mid_cold and R.root_cold are straight-line and never enter it.
    ASSERT_TRUE(CreateRuntime(
        [&profileFile](auto &options) {
            options.SetCompilerProfilingThreshold(0);
            options.SetProfilerEnabled(true);
            options.SetProfilesaverEnabled(true);
            options.SetProfileOutput(profileFile.GetFileName());
        },
        targetPandaFname.GetFileName(), eventsFile.GetFileName()));
    ASSERT_EQ(ExecutePandaFile("R::main"), 0U);
    DestroyRuntime();

    // Compile with a small regular budget and a large hot budget. Under profile-guided compilation
    // every compiled root takes the hot budget, whether profiled (R.root_prof) or allowlisted
    // (R.root_cold).
    WriteMethodsList(methodsFile, {"R::root_cold"});
    {
        auto res = Exec(GetPaocFile(), "--paoc-panda-files", pandaFile.GetFileName(),
                        "--paoc-location=" + os::GetCurrentWorkingDirectory() + "/target", "--events-output=csv",
                        "--events-file", eventsFile.GetFileName(),
                        std::string("--paoc-methods-from-file:path=") + methodsFile.GetFileName() + ",iswhite=true",
                        std::string("--paoc-use-profile:path=") + profileFile.GetFileName() + ",force",
                        "--paoc-compilation-policy=profile-guided", "--compiler-inlining-max-insts=64",
                        "--compiler-inlining-max-insts-hot=4000", "--compiler-inlining-max-bc-size=1000",
                        "--gc-type=epsilon", "--paoc-output", aotFile.GetFileName());
        ASSERT_TRUE(res) << "paoc failed with error: " << res.Error().ToString();
        ASSERT_EQ(res.Value(), 0U);
    }

    // The large callee fits the hot budget of both the profiled caller and the allowlisted caller.
    ASSERT_EQ(EventCount(eventsFile, std::regex("Inline,R::root_prof,R::leaf,.*STATIC,SUCCESS")), 1);
    ASSERT_EQ(EventCount(eventsFile, std::regex("Inline,R::root_cold,R::leaf,.*STATIC,SUCCESS")), 1);
    // Both mid helpers are small enough for either budget.
    ASSERT_EQ(EventCount(eventsFile, std::regex("Inline,R::root_prof,R::mid_cold,.*STATIC,SUCCESS")), 1);
    ASSERT_EQ(EventCount(eventsFile, std::regex("Inline,R::root_cold,R::mid_prof,.*STATIC,SUCCESS")), 1);
    // The root method's hot budget propagates through nested inlining regardless of the intermediate
    // callee's profile status: the unprofiled mid_cold inlines leaf under root_prof's hot budget, and the
    // profiled mid_prof inlines leaf under root_cold's hot budget.
    ASSERT_EQ(EventCount(eventsFile, std::regex("Inline,R::mid_cold,R::leaf,.*STATIC,SUCCESS")), 1);
    // R::mid_prof inlines R::leaf once as an independent root and once again when nested under R::root_cold.
    ASSERT_EQ(EventCount(eventsFile, std::regex("Inline,R::mid_prof,R::leaf,.*STATIC,SUCCESS")),
              MID_PROF_LEAF_INLINE_EVENTS);
}

TEST_F(AotPgoPaocLocationTest, AotPgoPaocDefaultWithoutProfile)
{
#ifndef PANDA_EVENTS_ENABLED
    GTEST_SKIP();
#endif
    if (RUNTIME_ARCH != Arch::X86_64) {
        GTEST_SKIP() << "AOT PGO workflow requires an x86_64 host";
    }
    TmpFile pandaFile("test.pf");
    TmpFile eventsFile("events-default.csv");
    TmpFile aotFile("./test.an");
    PaocAotPgoEmit(pandaFile);

    auto res = Exec(GetPaocFile(), "--paoc-panda-files", pandaFile.GetFileName(), "--paoc-compilation-policy=default",
                    "--events-output=csv", "--events-file", eventsFile.GetFileName(), "--gc-type=epsilon",
                    "--paoc-output", aotFile.GetFileName());
    ASSERT_TRUE(res) << res.Error().ToString();
    ASSERT_EQ(res.Value(), 0U);
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,A1::func.*,COMPILED")), 1);
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,B1::func.*,COMPILED")), 1);
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,Test1::main.*,COMPILED")), 1);
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,Test1::__noinline__call_func.*,COMPILED")), 1);
}

TEST_F(AotPgoPaocLocationTest, AotPgoPaocMethodsFromProfileBrokenProfileFails)
{
    if (RUNTIME_ARCH != Arch::X86_64) {
        GTEST_SKIP() << "AOT PGO workflow requires an x86_64 host";
    }
    TmpFile pandaFile("test.pf");
    TmpFile aotFile("./test.an");
    TmpFile invalidProfile("invalid.prof");
    {
        std::ofstream out(invalidProfile.GetFileName());
        out << "not an AOT profile\n";
    }

    PaocAotPgoEmit(pandaFile);

    for (const auto *profilePath : {"/nonexistent/missing.prof", invalidProfile.GetFileName()}) {
        SCOPED_TRACE(profilePath);
        auto profileArg = std::string("--paoc-use-profile:path=") + profilePath;

        // The default policy retains best-effort profile loading unless force is set.
        auto res =
            Exec(GetPaocFile(), "--paoc-panda-files", pandaFile.GetFileName(), "--paoc-compilation-policy=default",
                 profileArg, "--gc-type=epsilon", "--paoc-output", aotFile.GetFileName());
        ASSERT_TRUE(res) << res.Error().ToString();
        ASSERT_EQ(res.Value(), 0U);

        res = Exec(GetPaocFile(), "--paoc-panda-files", pandaFile.GetFileName(), "--paoc-compilation-policy=default",
                   profileArg + ",force", "--gc-type=epsilon", "--paoc-output", aotFile.GetFileName());
        ASSERT_FALSE(res) << "force must reject an unusable profile under the default policy";

        // profile-guided requires a usable profile even without force; it cannot fall back to default.
        res = Exec(GetPaocFile(), "--paoc-panda-files", pandaFile.GetFileName(),
                   "--paoc-compilation-policy=profile-guided", profileArg, "--gc-type=epsilon", "--paoc-output",
                   aotFile.GetFileName());
        ASSERT_FALSE(res) << "profile-guided must reject an unusable profile without force";
    }
}

TEST_F(AotPgoPaocLocationTest, AotPgoPaocAllowlistWithProfileSelection)
{
#ifndef PANDA_EVENTS_ENABLED
    GTEST_SKIP();
#endif

    if (RUNTIME_ARCH != Arch::X86_64) {
        GTEST_SKIP() << "AOT PGO workflow requires an x86_64 host";
    }
    TmpFile pandaFile("test.pf");
    TmpFile profileFile("test.prof");
    TmpFile methodsFile("methods.txt");
    TmpFile eventsFile("events-out.csv");
    TmpFile aotFile("./test.an");
    os::CreateDirectories("target");
    TmpFile targetDir("target");
    TmpFile targetPandaFname("target/test.pf");

    PaocAotPgoEmit(pandaFile);
    PaocAotPgoEmit(targetPandaFname);

    // Collect profile: Test1::main and Test1::__noinline__call_func become hot (branch/inline-cache data); A1::func
    // and B1::func are trivial and stay cold.
    ASSERT_TRUE(CreateRuntime(
        [&profileFile](auto &options) {
            options.SetCompilerProfilingThreshold(0);
            options.SetProfilerEnabled(true);
            options.SetProfilesaverEnabled(true);
            options.SetProfileOutput(profileFile.GetFileName());
        },
        targetPandaFname.GetFileName(), eventsFile.GetFileName()));
    ASSERT_EQ(ExecutePandaFile("Test1::main"), 20U);
    DestroyRuntime();

    // Allowlist a cold method (A1::func): it must compile regardless of profile coverage; profile-covered methods
    // must also compile (union of allowlist and profile).
    WriteMethodsList(methodsFile, {"A1::func"});
    {
        auto res = Exec(GetPaocFile(), "--paoc-panda-files", pandaFile.GetFileName(),
                        "--paoc-location=" + os::GetCurrentWorkingDirectory() + "/target", "--events-output=csv",
                        "--events-file", eventsFile.GetFileName(),
                        std::string("--paoc-methods-from-file:path=") + methodsFile.GetFileName() + ",iswhite=true",
                        std::string("--paoc-use-profile:path=") + profileFile.GetFileName() + ",force",
                        "--paoc-compilation-policy=profile-guided", "--gc-type=epsilon", "--paoc-output",
                        aotFile.GetFileName());
        ASSERT_TRUE(res) << "paoc failed with error: " << res.Error().ToString();
        ASSERT_EQ(res.Value(), 0U);
    }

    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,A1::func.*,COMPILED")), 1);
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,Test1::main.*,COMPILED")), 1);
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,Test1::__noinline__call_func.*,COMPILED")), 1);
    // B1::func is neither allowlisted nor profiled.
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,B1::func.*,COMPILED")), 0);
}

TEST_F(AotPgoPaocLocationTest, AotPgoPaocDenylistWithProfileSelection)
{
#ifndef PANDA_EVENTS_ENABLED
    GTEST_SKIP();
#endif

    if (RUNTIME_ARCH != Arch::X86_64) {
        GTEST_SKIP() << "AOT PGO workflow requires an x86_64 host";
    }
    TmpFile pandaFile("test.pf");
    TmpFile profileFile("test.prof");
    TmpFile methodsFile("methods.txt");
    TmpFile eventsFile("events-out.csv");
    TmpFile aotFile("./test.an");
    os::CreateDirectories("target");
    TmpFile targetDir("target");
    TmpFile targetPandaFname("target/test.pf");

    PaocAotPgoEmit(pandaFile);
    PaocAotPgoEmit(targetPandaFname);

    ASSERT_TRUE(CreateRuntime(
        [&profileFile](auto &options) {
            options.SetCompilerProfilingThreshold(0);
            options.SetProfilerEnabled(true);
            options.SetProfilesaverEnabled(true);
            options.SetProfileOutput(profileFile.GetFileName());
        },
        targetPandaFname.GetFileName(), eventsFile.GetFileName()));
    ASSERT_EQ(ExecutePandaFile("Test1::main"), 20U);
    DestroyRuntime();

    // Denylist a hot method (Test1::__noinline__call_func): it must never compile even though it is profiled.
    WriteMethodsList(methodsFile, {"Test1::__noinline__call_func"});
    {
        auto res = Exec(GetPaocFile(), "--paoc-panda-files", pandaFile.GetFileName(),
                        "--paoc-location=" + os::GetCurrentWorkingDirectory() + "/target", "--events-output=csv",
                        "--events-file", eventsFile.GetFileName(),
                        std::string("--paoc-methods-from-file:path=") + methodsFile.GetFileName() + ",iswhite=false",
                        std::string("--paoc-use-profile:path=") + profileFile.GetFileName() + ",force",
                        "--paoc-compilation-policy=profile-guided", "--gc-type=epsilon", "--paoc-output",
                        aotFile.GetFileName());
        ASSERT_TRUE(res) << "paoc failed with error: " << res.Error().ToString();
        ASSERT_EQ(res.Value(), 0U);
    }

    // Denylisted and thus excluded even though profiled.
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,Test1::__noinline__call_func.*,COMPILED")), 0);
    // Profiled and not denylisted.
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,Test1::main.*,COMPILED")), 1);
    // Cold.
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,A1::func.*,COMPILED")), 0);
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,B1::func.*,COMPILED")), 0);
}

TEST_F(AotPgoPaocLocationTest, AotPgoPaocAllowlistAlone)
{
#ifndef PANDA_EVENTS_ENABLED
    GTEST_SKIP();
#endif

    if (RUNTIME_ARCH != Arch::X86_64) {
        GTEST_SKIP() << "AOT PGO workflow requires an x86_64 host";
    }
    TmpFile pandaFile("test.pf");
    TmpFile methodsFile("methods.txt");
    TmpFile eventsFile("events-out.csv");
    TmpFile aotFile("./test.an");

    PaocAotPgoEmit(pandaFile);

    // Without a profile, the allowlist alone selects the methods to compile.
    WriteMethodsList(methodsFile, {"A1::func"});
    {
        auto res = Exec(GetPaocFile(), "--paoc-panda-files", pandaFile.GetFileName(), "--events-output=csv",
                        "--events-file", eventsFile.GetFileName(),
                        std::string("--paoc-methods-from-file:path=") + methodsFile.GetFileName() + ",iswhite=true",
                        "--gc-type=epsilon", "--paoc-output", aotFile.GetFileName());
        ASSERT_TRUE(res) << "paoc failed with error: " << res.Error().ToString();
        ASSERT_EQ(res.Value(), 0U);
    }

    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,A1::func.*,COMPILED")), 1);
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,Test1::main.*,COMPILED")), 0);
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,Test1::__noinline__call_func.*,COMPILED")), 0);
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,B1::func.*,COMPILED")), 0);
}

TEST_F(AotPgoPaocLocationTest, AotPgoPaocDenylistAlone)
{
#ifndef PANDA_EVENTS_ENABLED
    GTEST_SKIP();
#endif

    if (RUNTIME_ARCH != Arch::X86_64) {
        GTEST_SKIP() << "AOT PGO workflow requires an x86_64 host";
    }
    TmpFile pandaFile("test.pf");
    TmpFile methodsFile("methods.txt");
    TmpFile eventsFile("events-out.csv");
    TmpFile aotFile("./test.an");

    PaocAotPgoEmit(pandaFile);

    // Without a profile, the denylist excludes a method and everything else is compiled.
    WriteMethodsList(methodsFile, {"A1::func"});
    {
        auto res = Exec(GetPaocFile(), "--paoc-panda-files", pandaFile.GetFileName(), "--events-output=csv",
                        "--events-file", eventsFile.GetFileName(),
                        std::string("--paoc-methods-from-file:path=") + methodsFile.GetFileName() + ",iswhite=false",
                        "--gc-type=epsilon", "--paoc-output", aotFile.GetFileName());
        ASSERT_TRUE(res) << "paoc failed with error: " << res.Error().ToString();
        ASSERT_EQ(res.Value(), 0U);
    }

    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,A1::func.*,COMPILED")), 0);
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,Test1::main.*,COMPILED")), 1);
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,Test1::__noinline__call_func.*,COMPILED")), 1);
    ASSERT_EQ(EventCount(eventsFile, std::regex("Compilation,B1::func.*,COMPILED")), 1);
}

}  // namespace ark::compiler
