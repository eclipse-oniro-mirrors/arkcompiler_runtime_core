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

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "helpers/helpers.h"
#include "libabckit/c/extensions/arkts/metadata_arkts.h"
#include "libabckit/c/metadata_core.h"

namespace libabckit::test {
namespace {
auto *g_api = AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto *g_inspect = AbckitGetInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto *g_arkInspect = AbckitGetArktsInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto *g_arkModify = AbckitGetArktsModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
constexpr auto INPUT = ABCKIT_ABC_DIR "ut/extensions/arkts/modify_api/annotations/imported_annotations.abc";

struct AnnotationCase {
    const char *method;
    const char *module;
    const char *name;
    const char *value;
};
constexpr AnnotationCase CASES[] = {
    {"regularMethod", "imported_annotations_rules", "Router", "regular"},
    {"namespacedMethod", "imported_annotations_decoy", "Router", "namespace"},
    {"aliasedMethod", "imported_annotations_rules", "Router", "alias"},
    {"localMethod", "imported_annotations", "Local", "local"},
};

bool CollectAnnotation(AbckitCoreAnnotation *annotation, void *data)
{
    static_cast<std::vector<AbckitCoreAnnotation *> *>(data)->push_back(annotation);
    return true;
}

void CheckAnnotation(AbckitCoreAnnotation *annotation, const AnnotationCase &expected)
{
    auto *interface = g_inspect->annotationGetInterface(annotation);
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    if (expected.module == nullptr) {
        EXPECT_EQ(interface, nullptr);
    } else {
        ASSERT_NE(interface, nullptr);
        auto *module = g_inspect->annotationInterfaceGetModule(interface);
        ASSERT_NE(module, nullptr);
        EXPECT_EQ(helpers::AbckitStringToString(g_inspect->moduleGetName(module)), expected.module);
        EXPECT_EQ(helpers::AbckitStringToString(g_inspect->annotationGetName(annotation)), expected.name);
    }
    std::vector<AbckitCoreAnnotationElement *> elements;
    ASSERT_TRUE(g_inspect->annotationEnumerateElements(annotation, &elements, [](auto *element, void *data) {
        static_cast<std::vector<AbckitCoreAnnotationElement *> *>(data)->push_back(element);
        return true;
    }));
    ASSERT_EQ(elements.size(), 1U);
    EXPECT_EQ(helpers::AbckitStringToString(g_inspect->annotationElementGetName(elements.front())), "source");
    auto *value = g_inspect->annotationElementGetValue(elements.front());
    EXPECT_EQ(helpers::AbckitStringToString(g_inspect->valueGetString(value)), expected.value);
    EXPECT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

void CheckAnnotations(AbckitFile *file, bool removed = false)
{
    for (const auto &expected : CASES) {
        SCOPED_TRACE(expected.method);
        auto *method = helpers::FindMethodByName(file, expected.method);
        ASSERT_NE(method, nullptr);
        auto *klass = g_inspect->functionGetParentClass(method);
        ASSERT_NE(klass, nullptr);
        std::vector<AbckitCoreAnnotation *> classAnnotations;
        std::vector<AbckitCoreAnnotation *> methodAnnotations;
        ASSERT_TRUE(g_inspect->classEnumerateAnnotations(klass, &classAnnotations, CollectAnnotation));
        ASSERT_TRUE(g_inspect->functionEnumerateAnnotations(method, &methodAnnotations, CollectAnnotation));
        if (removed) {
            EXPECT_TRUE(classAnnotations.empty());
            ASSERT_EQ(methodAnnotations.size(), 1U);
            CheckAnnotation(methodAnnotations.front(), expected);
            continue;
        }
        ASSERT_EQ(classAnnotations.size(), 1U);
        ASSERT_EQ(methodAnnotations.size(), 1U);
        CheckAnnotation(classAnnotations.front(), expected);
        CheckAnnotation(methodAnnotations.front(), expected);
    }
}

class LibAbcKitImportedAnnotationsTest : public ::testing::Test {};

// Test: test-kind=api, api=AbckitApiImpl::openAbc, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitImportedAnnotationsTest, ResolveDeclarationIdentity)
{
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT, &file);
    ASSERT_NE(file, nullptr);
    CheckAnnotations(file);
    g_api->closeFile(file);
    EXPECT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=AbckitApiImpl::openAbc, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitImportedAnnotationsTest, UnresolvedNamespaceDoesNotBindNamedImport)
{
    constexpr auto UNRESOLVED = ABCKIT_ABC_DIR "ut/extensions/arkts/modify_api/annotations/unresolved_annotations.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(UNRESOLVED, &file);
    ASSERT_NE(file, nullptr);
    auto *method = helpers::FindMethodByName(file, "unresolvedMethod");
    ASSERT_NE(method, nullptr);
    auto *klass = g_inspect->functionGetParentClass(method);
    ASSERT_NE(klass, nullptr);
    std::vector<AbckitCoreAnnotation *> annotations;
    ASSERT_TRUE(g_inspect->classEnumerateAnnotations(klass, &annotations, CollectAnnotation));
    ASSERT_TRUE(g_inspect->functionEnumerateAnnotations(method, &annotations, CollectAnnotation));
    ASSERT_EQ(annotations.size(), 2U);
    for (auto *annotation : annotations) {
        CheckAnnotation(annotation, {"unresolvedMethod", nullptr, nullptr, "missing"});
        EXPECT_TRUE(g_inspect->annotationIsExternal(annotation));
    }
    g_api->closeFile(file);
    EXPECT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=AbckitApiImpl::writeAbc, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitImportedAnnotationsTest, PreserveDeclarationIdentityAfterReopen)
{
    constexpr auto OUTPUT =
        ABCKIT_ABC_DIR "ut/extensions/arkts/modify_api/annotations/imported_annotations_roundtrip.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT, &file);
    ASSERT_NE(file, nullptr);
    CheckAnnotations(file);
    g_api->writeAbc(file, OUTPUT, strlen(OUTPUT));
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_api->closeFile(file);
    helpers::AssertOpenAbc(OUTPUT, &file);
    ASSERT_NE(file, nullptr);
    CheckAnnotations(file);
    g_api->closeFile(file);
    EXPECT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ArktsModifyApiImpl::classRemoveAnnotation, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitImportedAnnotationsTest, RemoveClassAnnotationsPreservesMethods)
{
    constexpr auto OUTPUT =
        ABCKIT_ABC_DIR "ut/extensions/arkts/modify_api/annotations/imported_annotations_removed.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT, &file);
    ASSERT_NE(file, nullptr);
    for (const auto &expected : CASES) {
        auto *method = helpers::FindMethodByName(file, expected.method);
        ASSERT_NE(method, nullptr);
        auto *klass = g_inspect->functionGetParentClass(method);
        std::vector<AbckitCoreAnnotation *> annotations;
        g_inspect->classEnumerateAnnotations(klass, &annotations, CollectAnnotation);
        ASSERT_EQ(annotations.size(), 1U);
        g_arkModify->classRemoveAnnotation(g_arkInspect->coreClassToArktsClass(klass),
                                           g_arkInspect->coreAnnotationToArktsAnnotation(annotations.front()));
        ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    }
    CheckAnnotations(file, true);
    g_api->writeAbc(file, OUTPUT, strlen(OUTPUT));
    ASSERT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_api->closeFile(file);
    helpers::AssertOpenAbc(OUTPUT, &file);
    ASSERT_NE(file, nullptr);
    CheckAnnotations(file, true);
    g_api->closeFile(file);
    EXPECT_EQ(g_api->getLastError(), ABCKIT_STATUS_NO_ERROR);
}
}  // namespace
}  // namespace libabckit::test
