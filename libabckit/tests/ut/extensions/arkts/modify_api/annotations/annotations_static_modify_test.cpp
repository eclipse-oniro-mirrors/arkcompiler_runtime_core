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
#include "tests/helpers/helpers_runtime.h"
#include "tests/helpers/helpers.h"

namespace {
constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "ut/extensions/arkts/modify_api/annotations/annotations_static_modify.abc";
constexpr auto OUTPUT_PATH =
    ABCKIT_ABC_DIR "ut/extensions/arkts/modify_api/annotations/annotations_static_modify_updated.abc";
}  // namespace

namespace libabckit::test {

static auto g_impl = AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
static auto g_implI = AbckitGetInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
static auto g_implArkI = AbckitGetArktsInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
static auto g_implM = AbckitGetModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
static auto g_implArkM = AbckitGetArktsModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);

class LibAbcKitArkTSModifyApiAnnotationsTest : public ::testing::Test {};

static void ModifyModule(AbckitFile * /*unused*/, AbckitCoreFunction *method, AbckitGraph * /*unused*/)
{
    auto *module = g_implI->functionGetModule(method);
    const char *str = "NewAnnotation";
    struct AbckitArktsAnnotationInterfaceCreateParams annoIfParams {};
    annoIfParams.name = str;
    g_implArkM->moduleAddAnnotationInterface(g_implArkI->coreModuleToArktsModule(module), &annoIfParams);

    bool found = false;

    g_implI->moduleEnumerateAnnotationInterfaces(module, &found, [](AbckitCoreAnnotationInterface *annoI, void *data) {
        auto str = g_implI->annotationInterfaceGetName(annoI);

        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
        auto name = helpers::AbckitStringToString(str);

        if (name == "NewAnnotation") {
            auto *foundPtr = reinterpret_cast<bool *>(data);
            *foundPtr = true;
        }

        return true;
    });
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
    EXPECT_TRUE(found);
}

struct AnnoIFind {
    std::string name;
    AbckitCoreAnnotationInterface *ai = nullptr;
};

struct AnnoFind {
    std::string name;
    AbckitCoreAnnotation *anno = nullptr;
};

struct AnnoElemFind {
    std::string name;
    AbckitCoreAnnotationElement *annoElem = nullptr;
};

struct AnnoFieldFind {
    std::string name;
    AbckitCoreAnnotationInterfaceField *fld = nullptr;
};

static void ModifyAnnotationInterfaceAddField(AbckitFile *file, AbckitCoreFunction *method, AbckitGraph * /*unused*/)
{
    auto module = g_implI->functionGetModule(method);
    const char *str = "Anno14";
    AnnoIFind af;
    af.name = str;
    af.ai = nullptr;

    g_implI->moduleEnumerateAnnotationInterfaces(module, &af, [](AbckitCoreAnnotationInterface *annoI, void *data) {
        auto af1 = reinterpret_cast<AnnoIFind *>(data);

        auto str = g_implI->annotationInterfaceGetName(annoI);

        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
        auto name = helpers::AbckitStringToString(str);

        if (name == af1->name) {
            af1->ai = annoI;
        }

        return true;
    });
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
    EXPECT_TRUE(af.ai != nullptr);
    EXPECT_TRUE(af.name == str);

    struct AbckitArktsAnnotationInterfaceFieldCreateParams annoFieldCreateParams {};
    AbckitType *type = g_implM->createType(file, ABCKIT_TYPE_ID_U1);
    AbckitValue *value = g_implM->createValueU1(file, false);
    annoFieldCreateParams.name = "newField";
    annoFieldCreateParams.type = type;
    annoFieldCreateParams.defaultValue = value;
    auto field = g_implArkM->annotationInterfaceAddField(
        g_implArkI->coreAnnotationInterfaceToArktsAnnotationInterface(af.ai), &annoFieldCreateParams);
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

    auto annoI1 = g_implI->annotationInterfaceFieldGetInterface(
        g_implArkI->arktsAnnotationInterfaceFieldToCoreAnnotationInterfaceField(field));
    EXPECT_TRUE(annoI1 == af.ai);

    auto str1 = g_implI->annotationInterfaceFieldGetName(
        g_implArkI->arktsAnnotationInterfaceFieldToCoreAnnotationInterfaceField(field));
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

    auto name1 = helpers::AbckitStringToString(str1);
    EXPECT_TRUE(name1 == "newField");

    auto val1 = g_implI->annotationInterfaceFieldGetDefaultValue(
        g_implArkI->arktsAnnotationInterfaceFieldToCoreAnnotationInterfaceField(field));
    bool value1 = g_implI->valueGetU1(val1);
    EXPECT_TRUE(value1 == false);

    g_implI->annotationInterfaceEnumerateFields(af.ai, nullptr, [](AbckitCoreAnnotationInterfaceField *fld, void *) {
        auto str = g_implI->annotationInterfaceFieldGetName(fld);
        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

        auto name = helpers::AbckitStringToString(str);

        EXPECT_TRUE(name == "a" || name == "b" || name == "newField");
        return true;
    });
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
}

static void ModifyAnnotationInterfaceRemoveField(AbckitFile * /*unused*/, AbckitCoreFunction *method,
                                                 AbckitGraph * /*unused*/)
{
    auto module = g_implI->functionGetModule(method);
    const char *str = "Anno13";
    AnnoIFind af;
    af.name = str;
    af.ai = nullptr;

    g_implI->moduleEnumerateAnnotationInterfaces(module, &af, [](AbckitCoreAnnotationInterface *annoI, void *data) {
        auto af1 = reinterpret_cast<AnnoIFind *>(data);

        auto str = g_implI->annotationInterfaceGetName(annoI);

        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
        auto name = helpers::AbckitStringToString(str);

        if (name == af1->name) {
            af1->ai = annoI;
        }

        return true;
    });
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
    EXPECT_TRUE(af.ai != nullptr);
    EXPECT_TRUE(af.name == str);

    AnnoFieldFind aff;
    aff.name = "b";
    aff.fld = nullptr;

    g_implI->annotationInterfaceEnumerateFields(af.ai, &aff, [](AbckitCoreAnnotationInterfaceField *fld, void *data) {
        auto aff1 = reinterpret_cast<AnnoFieldFind *>(data);

        auto str = g_implI->annotationInterfaceFieldGetName(fld);
        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

        auto name = helpers::AbckitStringToString(str);
        if (name == aff1->name) {
            aff1->fld = fld;
        }
        return true;
    });

    EXPECT_TRUE(aff.name == "b");
    EXPECT_TRUE(aff.fld != nullptr);

    g_implArkM->annotationInterfaceRemoveField(
        g_implArkI->coreAnnotationInterfaceToArktsAnnotationInterface(af.ai),
        g_implArkI->coreAnnotationInterfaceFieldToArktsAnnotationInterfaceField(aff.fld));

    g_implI->annotationInterfaceEnumerateFields(af.ai, &aff, [](AbckitCoreAnnotationInterfaceField *fld, void *) {
        auto str = g_implI->annotationInterfaceFieldGetName(fld);
        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

        auto name = helpers::AbckitStringToString(str);

        EXPECT_TRUE(name == "a");
        return true;
    });
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
}

static void ModifyAnnotationAddAnnotationElement(AbckitFile *file, AbckitCoreFunction *method, AbckitGraph * /*unused*/)
{
    const char *str = "Anno9";
    AnnoFind af;
    af.name = str;
    af.anno = nullptr;

    g_implI->functionEnumerateAnnotations(method, &af, [](AbckitCoreAnnotation *anno, void *data) {
        auto af1 = reinterpret_cast<AnnoFind *>(data);

        auto annoI = g_implI->annotationGetInterface(anno);
        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

        auto str = g_implI->annotationInterfaceGetName(annoI);
        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

        auto name = helpers::AbckitStringToString(str);
        EXPECT_TRUE(name == "Anno9");

        if (name == af1->name) {
            af1->anno = anno;
        }

        return true;
    });

    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
    EXPECT_TRUE(af.anno != nullptr);
    EXPECT_TRUE(af.name == str);

    struct AbckitArktsAnnotationElementCreateParams annoElementCreateParams {};
    const double initVal = 0.12;
    auto value = g_implM->createValueDouble(file, initVal);
    annoElementCreateParams.value = value;
    annoElementCreateParams.name = "newValue";

    auto annoElem = g_implArkM->annotationAddAnnotationElement(g_implArkI->coreAnnotationToArktsAnnotation(af.anno),
                                                               &annoElementCreateParams);
    EXPECT_TRUE(annoElem != nullptr);
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

    auto str1 = g_implI->annotationElementGetName(g_implArkI->arktsAnnotationElementToCoreAnnotationElement(annoElem));
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

    auto name1 = helpers::AbckitStringToString(str1);

    EXPECT_TRUE(name1 == annoElementCreateParams.name);

    double val = g_implI->valueGetDouble(value);
    EXPECT_TRUE(val == 12e-2);
}

static void ModifyAnnotationRemoveAnnotationElement(AbckitFile * /*unused*/, AbckitCoreFunction *method,
                                                    AbckitGraph * /*unused*/)
{
    const char *str = "Anno10";
    AnnoFind af;
    af.name = str;
    af.anno = nullptr;

    g_implI->functionEnumerateAnnotations(method, &af, [](AbckitCoreAnnotation *anno, void *data) {
        auto af1 = reinterpret_cast<AnnoFind *>(data);

        auto annoI = g_implI->annotationGetInterface(anno);
        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

        auto str = g_implI->annotationInterfaceGetName(annoI);
        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

        auto name = helpers::AbckitStringToString(str);
        EXPECT_TRUE(name == "Anno10");

        if (name == af1->name) {
            af1->anno = anno;
        }

        return true;
    });

    EXPECT_TRUE(af.anno != nullptr);
    EXPECT_TRUE(af.name == str);

    AnnoElemFind aef;
    aef.name = "a";
    g_implI->annotationEnumerateElements(af.anno, &aef, [](AbckitCoreAnnotationElement *annoElem, void *data) {
        auto aef1 = reinterpret_cast<AnnoElemFind *>(data);

        auto str = g_implI->annotationElementGetName(annoElem);
        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

        auto name = helpers::AbckitStringToString(str);
        EXPECT_TRUE(name == "a" || name == "b");

        if (name == aef1->name) {
            aef1->annoElem = annoElem;
        }

        return true;
    });
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
    EXPECT_TRUE(aef.annoElem != nullptr);

    g_implArkM->annotationRemoveAnnotationElement(
        g_implArkI->coreAnnotationToArktsAnnotation(af.anno),
        g_implArkI->coreAnnotationElementToArktsAnnotationElement(aef.annoElem));
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

    g_implI->annotationEnumerateElements(af.anno, &aef, [](AbckitCoreAnnotationElement *annoElem, void *data) {
        auto aef1 = reinterpret_cast<AnnoElemFind *>(data);

        auto str = g_implI->annotationElementGetName(annoElem);
        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

        auto name = helpers::AbckitStringToString(str);
        EXPECT_TRUE(name != aef1->name);

        return true;
    });
}

static void ModifyFunctionAddAnnotation(AbckitFile * /*unused*/, AbckitCoreFunction *method, AbckitGraph * /*unused*/)
{
    auto module = g_implI->functionGetModule(method);
    const char *str = "Anno11";
    AnnoIFind af;
    af.name = str;
    af.ai = nullptr;

    g_implI->moduleEnumerateAnnotationInterfaces(module, &af, [](AbckitCoreAnnotationInterface *annoI, void *data) {
        auto af1 = reinterpret_cast<AnnoIFind *>(data);

        auto str = g_implI->annotationInterfaceGetName(annoI);

        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
        auto name = helpers::AbckitStringToString(str);

        if (name == af1->name) {
            af1->ai = annoI;
        }

        return true;
    });
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
    EXPECT_TRUE(af.ai != nullptr);
    EXPECT_TRUE(af.name == str);

    struct AbckitArktsAnnotationCreateParams annoCreateParams {};
    annoCreateParams.ai = g_implArkI->coreAnnotationInterfaceToArktsAnnotationInterface(af.ai);
    auto anno = g_implArkM->functionAddAnnotation(g_implArkI->coreFunctionToArktsFunction(method), &annoCreateParams);
    EXPECT_TRUE(anno != nullptr);
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

    auto annoI = g_implI->annotationGetInterface(g_implArkI->arktsAnnotationToCoreAnnotation(anno));
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

    auto str1 = g_implI->annotationInterfaceGetName(annoI);
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

    auto name1 = helpers::AbckitStringToString(str1);
    EXPECT_TRUE(name1 == str);
}

static void ModifyFunctionRemoveAnnotation(AbckitFile * /*unused*/, AbckitCoreFunction *method,
                                           AbckitGraph * /*unused*/)
{
    const char *str = "Anno11";
    AnnoFind af;
    af.name = str;
    af.anno = nullptr;

    g_implI->functionEnumerateAnnotations(method, &af, [](AbckitCoreAnnotation *anno, void *data) {
        auto af1 = reinterpret_cast<AnnoFind *>(data);

        auto ai = g_implI->annotationGetInterface(anno);

        auto str = g_implI->annotationInterfaceGetName(ai);

        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
        auto name = helpers::AbckitStringToString(str);
        if (name == af1->name) {
            af1->anno = anno;
        }

        return true;
    });
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
    EXPECT_TRUE(af.anno != nullptr);
    EXPECT_TRUE(af.name == str);

    g_implArkM->functionRemoveAnnotation(g_implArkI->coreFunctionToArktsFunction(method),
                                         g_implArkI->coreAnnotationToArktsAnnotation(af.anno));
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

    g_implI->functionEnumerateAnnotations(method, &af, [](AbckitCoreAnnotation *anno, void *data) {
        auto af1 = reinterpret_cast<AnnoFind *>(data);

        auto ai = g_implI->annotationGetInterface(anno);

        auto str = g_implI->annotationInterfaceGetName(ai);

        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
        auto name = helpers::AbckitStringToString(str);

        EXPECT_TRUE(name != af1->name);

        return true;
    });
}

static void ModifyClassAddAnnotation(AbckitFile * /*unused*/, AbckitCoreFunction *method, AbckitGraph * /*unused*/)
{
    auto module = g_implI->functionGetModule(method);
    const char *str = "Anno12";
    AnnoIFind af;
    af.name = str;
    af.ai = nullptr;

    g_implI->moduleEnumerateAnnotationInterfaces(module, &af, [](AbckitCoreAnnotationInterface *annoI, void *data) {
        auto af1 = reinterpret_cast<AnnoIFind *>(data);

        auto str = g_implI->annotationInterfaceGetName(annoI);

        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
        auto name = helpers::AbckitStringToString(str);

        if (name == af1->name) {
            af1->ai = annoI;
        }

        return true;
    });
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
    EXPECT_TRUE(af.ai != nullptr);
    EXPECT_TRUE(af.name == str);

    auto klass = g_implI->functionGetParentClass(method);
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

    struct AbckitArktsAnnotationCreateParams annoCreateParams {};
    annoCreateParams.ai = g_implArkI->coreAnnotationInterfaceToArktsAnnotationInterface(af.ai);
    auto anno = g_implArkM->classAddAnnotation(g_implArkI->coreClassToArktsClass(klass), &annoCreateParams);
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

    auto annoI = g_implI->annotationGetInterface(g_implArkI->arktsAnnotationToCoreAnnotation(anno));
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

    auto str1 = g_implI->annotationInterfaceGetName(annoI);
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

    auto name1 = helpers::AbckitStringToString(str1);
    EXPECT_TRUE(name1 == str);
}

static void ModifyClassRemoveAnnotation(AbckitFile * /*unused*/, AbckitCoreFunction *method, AbckitGraph * /*unused*/)
{
    const char *str = "Anno1";
    AnnoFind af;
    af.name = str;
    af.anno = nullptr;

    auto klass = g_implI->functionGetParentClass(method);
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

    g_implI->classEnumerateAnnotations(klass, &af, [](AbckitCoreAnnotation *anno, void *data) {
        auto af1 = reinterpret_cast<AnnoFind *>(data);

        auto ai = g_implI->annotationGetInterface(anno);

        auto str = g_implI->annotationInterfaceGetName(ai);

        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
        auto name = helpers::AbckitStringToString(str);
        if (name == af1->name) {
            af1->anno = anno;
        }

        return true;
    });
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
    EXPECT_TRUE(af.anno != nullptr);
    EXPECT_TRUE(af.name == str);

    g_implArkM->classRemoveAnnotation(g_implArkI->coreClassToArktsClass(klass),
                                      g_implArkI->coreAnnotationToArktsAnnotation(af.anno));
    EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);

    g_implI->classEnumerateAnnotations(klass, &af, [](AbckitCoreAnnotation *anno, void *data) {
        auto af1 = reinterpret_cast<AnnoFind *>(data);

        auto ai = g_implI->annotationGetInterface(anno);

        auto str = g_implI->annotationInterfaceGetName(ai);

        EXPECT_TRUE(g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR);
        auto name = helpers::AbckitStringToString(str);

        EXPECT_TRUE(name != af1->name);

        return true;
    });
}

// Test: test-kind=api, api=ArktsModifyApiImpl::moduleAddAnnotationInterface, abc-kind=ArkTS2, category=positive,
// extension=c
TEST_F(LibAbcKitArkTSModifyApiAnnotationsTest, ModuleAddAnnotationInterface)
{
    helpers::TransformMethod(INPUT_PATH, OUTPUT_PATH, "foo", ModifyModule);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ArktsModifyApiImpl::annotationInterfaceAddField, abc-kind=ArkTS2, category=positive,
// extension=c
TEST_F(LibAbcKitArkTSModifyApiAnnotationsTest, AnnotationInterfaceAddField)
{
    helpers::TransformMethod(INPUT_PATH, OUTPUT_PATH, "bar5", ModifyAnnotationInterfaceAddField);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ArktsModifyApiImpl::annotationInterfaceRemoveField, abc-kind=ArkTS2, category=positive,
// extension=c
TEST_F(LibAbcKitArkTSModifyApiAnnotationsTest, AnnotationInterfaceRemoveField)
{
    helpers::TransformMethod(INPUT_PATH, OUTPUT_PATH, "bar4", ModifyAnnotationInterfaceRemoveField);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ArktsModifyApiImpl::annotationAddAnnotationElement, abc-kind=ArkTS2, category=positive,
// extension=c
TEST_F(LibAbcKitArkTSModifyApiAnnotationsTest, AnnotationAddAnnotationElement)
{
    helpers::TransformMethod(INPUT_PATH, OUTPUT_PATH, "bar1", ModifyAnnotationAddAnnotationElement);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ArktsModifyApiImpl::annotationRemoveAnnotationElement, abc-kind=ArkTS2, category=positive,
// extension=c
TEST_F(LibAbcKitArkTSModifyApiAnnotationsTest, AnnotationRemoveAnnotationElement)
{
    helpers::TransformMethod(INPUT_PATH, OUTPUT_PATH, "bar2", ModifyAnnotationRemoveAnnotationElement);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ArktsModifyApiImpl::functionAddAnnotation, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitArkTSModifyApiAnnotationsTest, FunctionAddAnnotation)
{
    helpers::TransformMethod(INPUT_PATH, OUTPUT_PATH, "foo", ModifyFunctionAddAnnotation);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ArktsModifyApiImpl::functionRemoveAnnotation, abc-kind=ArkTS2, category=positive,
// extension=c
TEST_F(LibAbcKitArkTSModifyApiAnnotationsTest, FunctionRemoveAnnotation)
{
    helpers::TransformMethod(INPUT_PATH, OUTPUT_PATH, "bar3", ModifyFunctionRemoveAnnotation);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ArktsModifyApiImpl::classAddAnnotation, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitArkTSModifyApiAnnotationsTest, ClassAddAnnotation)
{
    helpers::TransformMethod(INPUT_PATH, OUTPUT_PATH, "foo", ModifyClassAddAnnotation);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ArktsModifyApiImpl::classRemoveAnnotation, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitArkTSModifyApiAnnotationsTest, ClassRemoveAnnotation)
{
    helpers::TransformMethod(INPUT_PATH, OUTPUT_PATH, "foo", ModifyClassRemoveAnnotation);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ArktsModifyApiImpl::annotationSetName, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitArkTSModifyApiAnnotationsTest, ClassAnnotationSetNameStatic)
{
    abckit::File file(INPUT_PATH);

    std::set<std::string> gotAnnotationNames;
    std::set<std::string> expectAnnotationNames = {"New", "Anno2"};

    for (const auto &module : file.GetModules()) {
        if (module.IsExternal()) {
            continue;
        }
        auto result = helpers::GetClassByName(module, "Class1");
        ASSERT_NE(result, std::nullopt);
        const auto &klass = result.value();
        for (const auto &anno : klass.GetAnnotations()) {
            if (anno.GetName() == "Anno1") {
                abckit::arkts::Annotation(anno).SetName("New");
            }
        }
        for (const auto &anno : klass.GetAnnotations()) {
            gotAnnotationNames.emplace(anno.GetName());
        }
    }
    file.WriteAbc(OUTPUT_PATH);

    ASSERT_EQ(gotAnnotationNames, expectAnnotationNames);
    ASSERT_EQ(helpers::ExecuteStaticAbc(INPUT_PATH, "annotations_static_modify", "main"),
              helpers::ExecuteStaticAbc(OUTPUT_PATH, "annotations_static_modify", "main"));
}

// Test: test-kind=api, api=ArktsModifyApiImpl::annotationSetName, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitArkTSModifyApiAnnotationsTest, ClassFieldAnnotationSetNameStatic)
{
    abckit::File file(ABCKIT_ABC_DIR "ut/extensions/arkts/modify_api/annotations/annotations_static_modify.abc");

    std::set<std::string> gotAnnotationNames;
    std::set<std::string> expectAnnotationNames = {"New", "Anno4"};

    for (const auto &module : file.GetModules()) {
        if (module.IsExternal()) {
            continue;
        }
        auto result = helpers::GetClassByName(module, "Class1");
        ASSERT_NE(result, std::nullopt);
        const auto &klass = result.value();
        const auto &fields = klass.GetFields();
        const auto &field = fields[0];
        for (const auto &anno : field.GetAnnotations()) {
            if (anno.GetName() == "Anno3") {
                abckit::arkts::Annotation(anno).SetName("New");
            }
        }
        for (const auto &anno : field.GetAnnotations()) {
            gotAnnotationNames.emplace(anno.GetName());
        }
    }
    file.WriteAbc(OUTPUT_PATH);

    ASSERT_EQ(gotAnnotationNames, expectAnnotationNames);
    ASSERT_EQ(helpers::ExecuteStaticAbc(INPUT_PATH, "annotations_static_modify", "main"),
              helpers::ExecuteStaticAbc(OUTPUT_PATH, "annotations_static_modify", "main"));
}

// Test: test-kind=api, api=ArktsModifyApiImpl::annotationSetName, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitArkTSModifyApiAnnotationsTest, InterfaceAnnotationSetNameStatic)
{
    abckit::File file(ABCKIT_ABC_DIR "ut/extensions/arkts/modify_api/annotations/annotations_static_modify.abc");

    std::set<std::string> gotAnnotationNames;
    std::set<std::string> expectAnnotationNames = {"New", "Anno6"};

    for (const auto &module : file.GetModules()) {
        if (module.IsExternal()) {
            continue;
        }
        auto result = helpers::GetInterfaceByName(module, "Interface1");
        ASSERT_NE(result, std::nullopt);
        const auto &iface = result.value();
        for (const auto &anno : iface.GetAnnotations()) {
            if (anno.GetName() == "Anno5") {
                abckit::arkts::Annotation(anno).SetName("New");
            }
        }
        for (const auto &anno : iface.GetAnnotations()) {
            gotAnnotationNames.emplace(anno.GetName());
        }
    }
    file.WriteAbc(OUTPUT_PATH);

    ASSERT_EQ(gotAnnotationNames, expectAnnotationNames);
    ASSERT_EQ(helpers::ExecuteStaticAbc(INPUT_PATH, "annotations_static_modify", "main"),
              helpers::ExecuteStaticAbc(OUTPUT_PATH, "annotations_static_modify", "main"));
}

// Test: test-kind=api, api=ArktsModifyApiImpl::annotationSetName, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitArkTSModifyApiAnnotationsTest, InterfaceFieldAnnotationSetNameStatic)
{
    abckit::File file(ABCKIT_ABC_DIR "ut/extensions/arkts/modify_api/annotations/annotations_static_modify.abc");

    std::set<std::string> gotAnnotationNames;
    std::set<std::string> expectAnnotationNames = {"New", "Anno8"};

    for (const auto &module : file.GetModules()) {
        if (module.IsExternal()) {
            continue;
        }
        auto result = helpers::GetInterfaceByName(module, "Interface1");
        ASSERT_NE(result, std::nullopt);
        const auto &iface = result.value();
        const auto &fields = iface.GetFields();
        const auto &field = fields[0];
        for (const auto &anno : field.GetAnnotations()) {
            if (anno.GetName() == "Anno7") {
                abckit::arkts::Annotation(anno).SetName("New");
            }
        }
        for (const auto &anno : field.GetAnnotations()) {
            gotAnnotationNames.emplace(anno.GetName());
        }
    }
    file.WriteAbc(OUTPUT_PATH);

    ASSERT_EQ(gotAnnotationNames, expectAnnotationNames);
    ASSERT_EQ(helpers::ExecuteStaticAbc(INPUT_PATH, "annotations_static_modify", "main"),
              helpers::ExecuteStaticAbc(OUTPUT_PATH, "annotations_static_modify", "main"));
}
}  // namespace libabckit::test
