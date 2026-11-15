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

#include "core/document/abc_document.h"
#include "core/format/structure_formatter.h"
#include "core/reader/dynamic_name_parser.h"
#include "core/reader/dynamic_structure_reader.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <sstream>
#include <vector>

namespace panda::bytecode_explorer::test {
namespace {

const EntityNode *FindDescendant(const StructureIndex &index, EntityId parentId, EntityKind kind,
                                 const std::string &name)
{
    const auto *parent = index.Find(parentId);
    if (parent == nullptr) {
        return nullptr;
    }
    for (const auto childId : parent->children) {
        const auto *child = index.Find(childId);
        if (child != nullptr && child->kind == kind && child->name == name) {
            return child;
        }
        if (const auto *nested = FindDescendant(index, childId, kind, name); nested != nullptr) {
            return nested;
        }
    }
    return nullptr;
}

const EntityNode *FindRoot(const StructureIndex &index)
{
    return index.RootIds().empty() ? nullptr : index.Find(index.RootIds().front());
}

void AppendTree(const StructureIndex &index, EntityId id, size_t depth, std::ostringstream *output)
{
    const auto *node = index.Find(id);
    if (node == nullptr) {
        return;
    }
    *output << std::string(depth * 2U, ' ') << EntityKindName(node->kind) << ": " << node->name << '\n';
    for (const auto childId : node->children) {
        AppendTree(index, childId, depth + 1U, output);
    }
}

std::string DumpTree(const StructureIndex &index)
{
    std::ostringstream output;
    for (const auto rootId : index.RootIds()) {
        AppendTree(index, rootId, 0, &output);
    }
    return output.str();
}

TEST(DynamicNameParserTest, ParsesTopLevelAndNestedSemanticNames)
{
    const std::vector<std::string> scopes {"Outer", "Inner", "Counter"};

    auto function = DynamicNameParser::Parse("structure_alpha", "structure_alpha.#*#alphaEntry", scopes);
    ASSERT_TRUE(function.has_value());
    EXPECT_EQ(function->kind, EntityKind::FUNCTION);
    EXPECT_EQ(function->name, "alphaEntry");
    EXPECT_TRUE(function->parents.empty());

    auto nestedEnum = DynamicNameParser::Parse("structure_alpha", "structure_alpha.#&@0&@1%#State", scopes);
    ASSERT_TRUE(nestedEnum.has_value());
    EXPECT_EQ(nestedEnum->kind, EntityKind::ENUM);
    EXPECT_EQ(nestedEnum->name, "State");
    EXPECT_EQ(nestedEnum->parents, std::vector<std::string>({"Outer", "Inner"}));
    EXPECT_EQ(nestedEnum->parentKinds, std::vector<EntityKind>({EntityKind::NAMESPACE, EntityKind::NAMESPACE}));
    EXPECT_TRUE(nestedEnum->reliable);

    auto method = DynamicNameParser::Parse("structure_alpha", "structure_alpha.#~@2<#create", scopes);
    ASSERT_TRUE(method.has_value());
    EXPECT_EQ(method->kind, EntityKind::METHOD);
    EXPECT_EQ(method->role, EntityRole::STATIC_METHOD);
    EXPECT_EQ(method->name, "create");
    EXPECT_EQ(method->parents, std::vector<std::string>({"Counter"}));
    EXPECT_EQ(method->parentKinds, std::vector<EntityKind>({EntityKind::CLASS}));
}

TEST(DynamicNameParserTest, RejectsNamesOutsideTheModuleGrammar)
{
    const std::vector<std::string> scopes {"Counter"};

    EXPECT_FALSE(DynamicNameParser::Parse("entry", "other.#*#function", scopes).has_value());
    EXPECT_FALSE(DynamicNameParser::Parse("entry", "entry.unexplained", scopes).has_value());
    const auto unresolved = DynamicNameParser::Parse("entry", "entry.#~@9>#method", scopes);
    ASSERT_TRUE(unresolved.has_value());
    EXPECT_FALSE(unresolved->reliable);
    EXPECT_FALSE(unresolved->diagnostic.empty());
}

TEST(DynamicNameParserTest, PreservesUnknownScopeMarkersWithoutInventingNamespaces)
{
    const std::vector<std::string> scopes {"Mystery"};
    const auto parsed = DynamicNameParser::Parse("entry", "entry.#$@0>#method", scopes);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->parents, std::vector<std::string>({"Mystery"}));
    ASSERT_EQ(parsed->parentKinds.size(), 1U);
    EXPECT_EQ(parsed->parentKinds.front(), EntityKind::ABC_ITEM);
    EXPECT_FALSE(parsed->reliable);
}

TEST(DynamicNameParserTest, ParsesInlineAndNestedScopeNamesFromTheDynamicGrammar)
{
    const std::vector<std::string> scopes {"Indexed"};
    const auto parsed = DynamicNameParser::Parse("entry", "entry.#&@0&Inline~Worker>run*local*#leaf", scopes);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->reliable);
    EXPECT_EQ(parsed->kind, EntityKind::FUNCTION);
    EXPECT_EQ(parsed->name, "leaf");
    EXPECT_EQ(parsed->parents, std::vector<std::string>({"Indexed", "Inline", "Worker", "run", "local"}));
    EXPECT_EQ(parsed->parentKinds,
              std::vector<EntityKind>({EntityKind::NAMESPACE, EntityKind::NAMESPACE, EntityKind::CLASS,
                                       EntityKind::METHOD, EntityKind::FUNCTION}));

    const auto constructor = DynamicNameParser::Parse("entry", "entry.#~Worker=#Worker", scopes);
    ASSERT_TRUE(constructor.has_value());
    EXPECT_EQ(constructor->role, EntityRole::CONSTRUCTOR);
    EXPECT_EQ(constructor->name, "Worker");
    EXPECT_EQ(constructor->parents, std::vector<std::string>({"Worker"}));
    EXPECT_EQ(constructor->parentKinds, std::vector<EntityKind>({EntityKind::CLASS}));

    const auto anonymous = DynamicNameParser::Parse("entry", "entry.#*#^1", scopes);
    ASSERT_TRUE(anonymous.has_value());
    EXPECT_EQ(anonymous->kind, EntityKind::FUNCTION);
    EXPECT_EQ(anonymous->name, "<anonymous ^1>");

    const auto nestedAnonymous = DynamicNameParser::Parse("entry", "entry.#*^1*#child", scopes);
    ASSERT_TRUE(nestedAnonymous.has_value());
    EXPECT_EQ(nestedAnonymous->parents, std::vector<std::string>({"<anonymous ^1>"}));
    EXPECT_EQ(nestedAnonymous->parentKinds, std::vector<EntityKind>({EntityKind::FUNCTION}));
}

TEST(DynamicNameParserTest, PreservesDuplicatedIndexedScopesInHierarchyKeys)
{
    const std::vector<std::string> scopes {"Worker"};
    const auto parsed = DynamicNameParser::Parse("entry", "entry.#*@0^1*#child", scopes);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->parents, std::vector<std::string>({"Worker"}));
    ASSERT_EQ(parsed->parentKeys.size(), 1U);
    EXPECT_NE(parsed->parentKeys.front(), parsed->parents.front());
}

void ExpectCounterMembers(const StructureIndex &index, const EntityNode &counter)
{
    EXPECT_NE(FindDescendant(index, counter.id, EntityKind::FIELD, "count"), nullptr);
    const auto *staticField = FindDescendant(index, counter.id, EntityKind::FIELD, "label");
    ASSERT_NE(staticField, nullptr);
    EXPECT_TRUE(staticField->isStatic);
    const auto *constructor = FindDescendant(index, counter.id, EntityKind::METHOD, "Counter");
    ASSERT_NE(constructor, nullptr);
    EXPECT_EQ(constructor->role, EntityRole::CONSTRUCTOR);
    const auto *staticMethod = FindDescendant(index, counter.id, EntityKind::METHOD, "create");
    ASSERT_NE(staticMethod, nullptr);
    EXPECT_EQ(staticMethod->role, EntityRole::STATIC_METHOD);
    EXPECT_TRUE(staticMethod->isStatic);
    const auto *instanceMethod = FindDescendant(index, counter.id, EntityKind::METHOD, "increment");
    ASSERT_NE(instanceMethod, nullptr);
    EXPECT_EQ(instanceMethod->role, EntityRole::INSTANCE_METHOD);
    EXPECT_FALSE(instanceMethod->isStatic);
}

void ExpectBetaAndNestedModels(const StructureIndex &index, const EntityNode &root, const EntityNode &alpha)
{
    const auto *beta = FindDescendant(index, root.id, EntityKind::MODULE, "structure_beta");
    ASSERT_NE(beta, nullptr);
    const auto *betaModel = FindDescendant(index, beta->id, EntityKind::CLASS, "BetaModel");
    ASSERT_NE(betaModel, nullptr);
    const auto *loneGetter = FindDescendant(index, betaModel->id, EntityKind::METHOD, "summary");
    ASSERT_NE(loneGetter, nullptr);
    EXPECT_EQ(loneGetter->role, EntityRole::GETTER);
    EXPECT_FALSE(loneGetter->isStatic);
    const auto *staticGetter = FindDescendant(index, betaModel->id, EntityKind::METHOD, "category");
    ASSERT_NE(staticGetter, nullptr);
    EXPECT_EQ(staticGetter->role, EntityRole::GETTER);
    EXPECT_TRUE(staticGetter->isStatic);
    const auto *localFactory = FindDescendant(index, alpha.id, EntityKind::FUNCTION, "makeLocalBox");
    ASSERT_NE(localFactory, nullptr);
    const auto *localBox = FindDescendant(index, localFactory->id, EntityKind::CLASS, "LocalBox");
    ASSERT_NE(localBox, nullptr) << DumpTree(index);
    const auto *nestedGetter = FindDescendant(index, localBox->id, EntityKind::METHOD, "data");
    ASSERT_NE(nestedGetter, nullptr);
    EXPECT_EQ(nestedGetter->role, EntityRole::GETTER);
    const auto *nestedStaticGetter = FindDescendant(index, localBox->id, EntityKind::METHOD, "tag");
    ASSERT_NE(nestedStaticGetter, nullptr);
    EXPECT_EQ(nestedStaticGetter->role, EntityRole::GETTER);
    EXPECT_TRUE(nestedStaticGetter->isStatic);
}

void ExpectSingleRouteAnnotation(const StructureIndex &index, const EntityNode &counter)
{
    EXPECT_NE(FindDescendant(index, counter.id, EntityKind::ANNOTATION, "Route"), nullptr);
    size_t routeAnnotationCount = 0U;
    for (const auto childId : counter.children) {
        const auto *child = index.Find(childId);
        if (child != nullptr && child->kind == EntityKind::ANNOTATION && child->name == "Route") {
            ++routeAnnotationCount;
        }
    }
    EXPECT_EQ(routeAnnotationCount, 1U);
}

void ExpectImportExportGroups(const StructureIndex &index, const EntityNode &alpha)
{
    const auto *imports = FindDescendant(index, alpha.id, EntityKind::GROUP, "Imports");
    ASSERT_NE(imports, nullptr);
    EXPECT_TRUE(imports->defaultCollapsed);
    EXPECT_NE(FindDescendant(index, imports->id, EntityKind::IMPORT, "betaHelper"), nullptr);
    EXPECT_NE(FindDescendant(index, imports->id, EntityKind::IMPORT, "beta"), nullptr);
    const auto *exports = FindDescendant(index, alpha.id, EntityKind::GROUP, "Exports");
    ASSERT_NE(exports, nullptr);
    EXPECT_NE(FindDescendant(index, exports->id, EntityKind::EXPORT, "reexportedHelper"), nullptr);
    const auto *starExport = FindDescendant(index, exports->id, EntityKind::EXPORT, "*");
    ASSERT_NE(starExport, nullptr);
    EXPECT_EQ(starExport->name, "*");
    EXPECT_EQ(starExport->displayName, "* from \"./structure_beta\"");
    const auto *indirectExport = FindDescendant(index, exports->id, EntityKind::EXPORT, "reexportedHelper");
    ASSERT_NE(indirectExport, nullptr);
    EXPECT_EQ(indirectExport->displayName, "{ betaHelper as reexportedHelper } from \"./structure_beta\"");
}

TEST(DynamicStructureReaderTest, BuildsSemanticFirstHierarchyFromOneDynamicAbc)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();

    auto result = DynamicStructureReader::Build(document.Value()->Summary().path, document.Value()->File());
    ASSERT_TRUE(result.HasValue()) << result.Error().Message();
    const auto &index = *result.Value();
    const auto *root = FindRoot(index);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->kind, EntityKind::FILE);
    EXPECT_EQ(index.Count(EntityKind::MODULE), 2U);
    EXPECT_EQ(index.Count(EntityKind::INTERFACE), 0U);  // ErasedShape has no physical representation.

    const auto *alpha = FindDescendant(index, root->id, EntityKind::MODULE, "structure_alpha");
    ASSERT_NE(alpha, nullptr);
    const auto *outer = FindDescendant(index, alpha->id, EntityKind::NAMESPACE, "Outer");
    ASSERT_NE(outer, nullptr) << DumpTree(index);
    const auto *inner = FindDescendant(index, outer->id, EntityKind::NAMESPACE, "Inner");
    ASSERT_NE(inner, nullptr);
    ASSERT_NE(FindDescendant(index, inner->id, EntityKind::FUNCTION, "nested"), nullptr);

    const auto *color = FindDescendant(index, alpha->id, EntityKind::ENUM, "Color");
    ASSERT_NE(color, nullptr);
    EXPECT_NE(FindDescendant(index, color->id, EntityKind::ENUM_MEMBER, "Red"), nullptr);
    EXPECT_NE(FindDescendant(index, color->id, EntityKind::ENUM_MEMBER, "Green"), nullptr);
    EXPECT_NE(FindDescendant(index, color->id, EntityKind::ENUM_MEMBER, "Blue"), nullptr);
    const auto *state = FindDescendant(index, inner->id, EntityKind::ENUM, "State");
    ASSERT_NE(state, nullptr);
    EXPECT_NE(FindDescendant(index, state->id, EntityKind::ENUM_MEMBER, "Off"), nullptr);

    const auto *annotationInterface = FindDescendant(index, alpha->id, EntityKind::ANNOTATION_INTERFACE, "Route");
    ASSERT_NE(annotationInterface, nullptr);
    EXPECT_NE(FindDescendant(index, annotationInterface->id, EntityKind::FIELD, "path"), nullptr);

    const auto *counter = FindDescendant(index, alpha->id, EntityKind::CLASS, "Counter");
    ASSERT_NE(counter, nullptr) << DumpTree(index);
    ASSERT_NO_FATAL_FAILURE(ExpectCounterMembers(index, *counter));
    ASSERT_NO_FATAL_FAILURE(ExpectBetaAndNestedModels(index, *root, *alpha));
    ExpectSingleRouteAnnotation(index, *counter);

    ASSERT_NO_FATAL_FAILURE(ExpectImportExportGroups(index, *alpha));
}

TEST(DynamicStructureReaderTest, RetainsPhysicalBackingAndConservativeInternalFallback)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();
    auto result = DynamicStructureReader::Build(document.Value()->Summary().path, document.Value()->File());
    ASSERT_TRUE(result.HasValue()) << result.Error().Message();
    const auto &index = *result.Value();
    const auto *root = FindRoot(index);
    ASSERT_NE(root, nullptr);

    const auto *counter = FindDescendant(index, root->id, EntityKind::CLASS, "Counter");
    ASSERT_NE(counter, nullptr);
    EXPECT_FALSE(counter->backing.empty());
    EXPECT_TRUE(counter->primaryRange.has_value());

    const auto *internals = FindDescendant(index, root->id, EntityKind::GROUP, "ABC Internals");
    ASSERT_NE(internals, nullptr);
    EXPECT_TRUE(internals->defaultCollapsed);
    EXPECT_FALSE(internals->children.empty());
}

TEST(DynamicStructureReaderTest, BuildsNonMergeModuleWithScopeMetadata)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_NONMERGE_TEST_ABC);
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();
    auto result = DynamicStructureReader::Build(document.Value()->Summary().path, document.Value()->File());
    ASSERT_TRUE(result.HasValue()) << result.Error().Message();

    const auto &index = *result.Value();
    const auto *root = FindRoot(index);
    ASSERT_NE(root, nullptr);
    const auto *module = FindDescendant(index, root->id, EntityKind::MODULE, "structure_alpha.ts");
    ASSERT_NE(module, nullptr) << DumpTree(index);
    EXPECT_NE(FindDescendant(index, module->id, EntityKind::CLASS, "Counter"), nullptr);
    EXPECT_NE(FindDescendant(index, module->id, EntityKind::ANNOTATION_INTERFACE, "Route"), nullptr);
    EXPECT_NE(FindDescendant(index, module->id, EntityKind::FUNCTION, "alphaEntry"), nullptr);
}

TEST(DynamicStructureReaderTest, UsesInitializerEvidenceWhenScopeMetadataIsAbsent)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_NO_SCOPE_TEST_ABC);
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();
    auto result = DynamicStructureReader::Build(document.Value()->Summary().path, document.Value()->File());
    ASSERT_TRUE(result.HasValue()) << result.Error().Message();

    const auto &index = *result.Value();
    const auto *root = FindRoot(index);
    ASSERT_NE(root, nullptr);
    const auto *module = FindDescendant(index, root->id, EntityKind::MODULE, "structure_beta.ts");
    ASSERT_NE(module, nullptr) << DumpTree(index);
    const auto *model = FindDescendant(index, module->id, EntityKind::CLASS, "BetaModel");
    ASSERT_NE(model, nullptr) << DumpTree(index);
    EXPECT_NE(FindDescendant(index, model->id, EntityKind::FIELD, "title"), nullptr);
    const auto *instanceMethod = FindDescendant(index, model->id, EntityKind::METHOD, "format");
    ASSERT_NE(instanceMethod, nullptr) << DumpTree(index);
    EXPECT_EQ(instanceMethod->role, EntityRole::INSTANCE_METHOD);
    const auto *staticMethod = FindDescendant(index, model->id, EntityKind::METHOD, "create");
    ASSERT_NE(staticMethod, nullptr) << DumpTree(index);
    EXPECT_EQ(staticMethod->role, EntityRole::STATIC_METHOD);
    const auto *getter = FindDescendant(index, model->id, EntityKind::METHOD, "summary");
    ASSERT_NE(getter, nullptr) << DumpTree(index);
    EXPECT_EQ(getter->role, EntityRole::GETTER);
    EXPECT_NE(FindDescendant(index, module->id, EntityKind::FUNCTION, "betaHelper"), nullptr);
}

TEST(DynamicStructureReaderTest, RepresentsPlainDynamicInputAsScriptWithoutInventingAModule)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_SCRIPT_TEST_ABC);
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();
    auto result = DynamicStructureReader::Build(document.Value()->Summary().path, document.Value()->File());
    ASSERT_TRUE(result.HasValue()) << result.Error().Message();

    const auto &index = *result.Value();
    const auto *root = FindRoot(index);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(index.Count(EntityKind::MODULE), 0U);
    const auto *script = FindDescendant(index, root->id, EntityKind::SCRIPT, "<script>");
    ASSERT_NE(script, nullptr) << DumpTree(index);
    EXPECT_NE(FindDescendant(index, script->id, EntityKind::FUNCTION, "scriptHelper"), nullptr);
    EXPECT_NE(FindDescendant(index, script->id, EntityKind::CLASS, "ScriptModel"), nullptr);
}

TEST(DynamicStructureReaderTest, OrdersSemanticDeclarationsBeforeGroupsDeterministically)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();
    const auto first = DynamicStructureReader::Build(document.Value()->Summary().path, document.Value()->File());
    const auto second = DynamicStructureReader::Build(document.Value()->Summary().path, document.Value()->File());
    ASSERT_TRUE(first.HasValue()) << first.Error().Message();
    ASSERT_TRUE(second.HasValue()) << second.Error().Message();
    EXPECT_EQ(DumpTree(*first.Value()), DumpTree(*second.Value()));

    const auto *root = FindRoot(*first.Value());
    ASSERT_NE(root, nullptr);
    const auto *module = FindDescendant(*first.Value(), root->id, EntityKind::MODULE, "structure_alpha");
    ASSERT_NE(module, nullptr);
    size_t lastSemantic = 0U;
    size_t firstGroup = module->children.size();
    for (size_t index = 0; index < module->children.size(); ++index) {
        const auto *child = first.Value()->Find(module->children[index]);
        ASSERT_NE(child, nullptr);
        if (child->kind == EntityKind::GROUP) {
            firstGroup = std::min(firstGroup, index);
        } else {
            lastSemantic = index;
        }
    }
    EXPECT_LT(lastSemantic, firstGroup) << DumpTree(*first.Value());
}

TEST(StructureFormatterTest, DumpsSemanticTreeAndOptionallyIncludesInternals)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();
    auto result = DynamicStructureReader::Build(document.Value()->Summary().path, document.Value()->File());
    ASSERT_TRUE(result.HasValue()) << result.Error().Message();

    const auto semantic = StructureFormatter::Format(*result.Value(), false);
    EXPECT_NE(semantic.find("[Module] structure_alpha"), std::string::npos);
    EXPECT_NE(semantic.find("[Namespace] Outer"), std::string::npos);
    EXPECT_NE(semantic.find("[Annotation Interface] Route"), std::string::npos);
    EXPECT_NE(semantic.find("[Import] * as beta from \"./structure_beta\""), std::string::npos);
    EXPECT_NE(semantic.find("[Import] { betaValue as renamedBeta } from \"./structure_beta\""),
              std::string::npos);
    EXPECT_NE(semantic.find("[Export] * from \"./structure_beta\""), std::string::npos);
    EXPECT_NE(semantic.find("[Export] { betaHelper as reexportedHelper } from \"./structure_beta\""),
              std::string::npos);
    EXPECT_EQ(semantic.find("ABC Internals"), std::string::npos);
    EXPECT_EQ(semantic.find("<module initializer>"), std::string::npos);

    const auto complete = StructureFormatter::Format(*result.Value(), true);
    EXPECT_NE(complete.find("ABC Internals"), std::string::npos);
    EXPECT_NE(complete.find("<module initializer>"), std::string::npos);
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
