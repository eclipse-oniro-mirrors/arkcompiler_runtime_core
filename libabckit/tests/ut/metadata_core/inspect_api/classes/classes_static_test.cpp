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

#include <gtest/gtest.h>
#include <string>
#include <optional>

#include "libabckit/cpp/abckit_cpp.h"
#include "helpers/helpers.h"

namespace libabckit::test {

class LibAbcKitInspectApiClassesTest : public ::testing::Test {};

// Test: test-kind=api, api=InspectApiImpl::classEnumerateFields, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitInspectApiClassesTest, ClassGetFieldsStatic)
{
    abckit::File file(ABCKIT_ABC_DIR "ut/metadata_core/inspect_api/classes/classes_static.abc");

    std::set<std::string> gotFieldNames;
    std::set<std::string> expectFieldNames = {"F1", "F2", "<property>I1F1"};

    for (const auto &module : file.GetModules()) {
        if (module.IsExternal()) {
            continue;
        }
        for (const auto &klass : module.GetClasses()) {
            for (const auto &field : klass.GetFields()) {
                gotFieldNames.emplace(field.GetName());
            }
        }
    }

    ASSERT_EQ(gotFieldNames, expectFieldNames);
}

// Test: test-kind=api, api=InspectApiImpl::classEnumerateMethods, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitInspectApiClassesTest, ClassGetAllMethodsStatic)
{
    abckit::File file(ABCKIT_ABC_DIR "ut/metadata_core/inspect_api/classes/classes_static.abc");

    std::set<std::string> gotMethodNames;
    std::set<std::string> expectMethodNames = {"C1M1:classes_static.C1;void;", "C1M2:void;",
                                               "_ctor_:classes_static.C1;void;"};

    for (const auto &module : file.GetModules()) {
        if (module.IsExternal()) {
            continue;
        }
        auto result = helpers::GetClassByName(module, "C1");
        ASSERT_NE(result, std::nullopt);
        const auto &klass = result.value();
        for (const auto &method : klass.GetAllMethods()) {
            gotMethodNames.emplace(method.GetName());
        }
    }

    ASSERT_EQ(gotMethodNames, expectMethodNames);
}

// Test: test-kind=api, api=InspectApiImpl::classGetSuperClass, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitInspectApiClassesTest, ClassGetSuperClassStatic)
{
    abckit::File file(ABCKIT_ABC_DIR "ut/metadata_core/inspect_api/classes/classes_static.abc");

    std::set<std::string> gotClassNames;
    std::set<std::string> expectClassNames = {"C1"};

    for (const auto &module : file.GetModules()) {
        if (module.IsExternal()) {
            continue;
        }
        auto result = helpers::GetClassByName(module, "C2");
        ASSERT_NE(result, std::nullopt);
        const auto &klass = result.value();
        gotClassNames.emplace(klass.GetSuperClass().GetName());
    }

    ASSERT_EQ(gotClassNames, expectClassNames);
}

// Test: test-kind=api, api=InspectApiImpl::classGetSubClasses, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitInspectApiClassesTest, ClassGetSubClassesStatic)
{
    abckit::File file(ABCKIT_ABC_DIR "ut/metadata_core/inspect_api/classes/classes_static.abc");

    std::set<std::string> gotClassNames;
    std::set<std::string> expectClassNames = {"C2"};

    for (const auto &module : file.GetModules()) {
        if (module.IsExternal()) {
            continue;
        }
        auto result = helpers::GetClassByName(module, "C1");
        ASSERT_NE(result, std::nullopt);
        const auto &klass = result.value();
        for (const auto &subClass : klass.GetSubClasses()) {
            gotClassNames.emplace(subClass.GetName());
        }
    }

    ASSERT_EQ(gotClassNames, expectClassNames);
}

// Test: test-kind=api, api=InspectApiImpl::classGetInterfaces, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitInspectApiClassesTest, ClassGetInterfacesStatic)
{
    abckit::File file(ABCKIT_ABC_DIR "ut/metadata_core/inspect_api/classes/classes_static.abc");

    std::set<std::string> gotInterfaceNames;
    std::set<std::string> expectInterfaceNames = {"I1"};

    for (const auto &module : file.GetModules()) {
        if (module.IsExternal()) {
            continue;
        }
        auto result = helpers::GetClassByName(module, "C2");
        ASSERT_NE(result, std::nullopt);
        const auto &klass = result.value();
        for (const auto &iface : klass.GetInterfaces()) {
            gotInterfaceNames.emplace(iface.GetName());
        }
    }

    ASSERT_EQ(gotInterfaceNames, expectInterfaceNames);
}

// Test: test-kind=api, api=InspectApiImpl::classEnumerateAnnotations, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitInspectApiClassesTest, ClassGetAnnotationsStatic)
{
    abckit::File file(ABCKIT_ABC_DIR "ut/metadata_core/inspect_api/classes/classes_static.abc");

    std::set<std::string> gotAnnotationNames;
    std::set<std::string> expectAnnotationNames = {"A1"};

    for (const auto &module : file.GetModules()) {
        if (module.IsExternal()) {
            continue;
        }
        auto result = helpers::GetClassByName(module, "C3");
        ASSERT_NE(result, std::nullopt);
        const auto &klass = result.value();
        for (const auto &anno : klass.GetAnnotations()) {
            gotAnnotationNames.emplace(anno.GetName());
        }
    }

    ASSERT_EQ(gotAnnotationNames, expectAnnotationNames);
}
}  // namespace libabckit::test