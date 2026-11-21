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

#include "core/reader/annotation_analyzer.h"
#include "core/reader/callable_analyzer.h"
#include "core/reader/enum_analyzer.h"
#include "core/reader/hierarchy_builder.h"
#include "core/reader/module_binding_analyzer.h"
#include "core/reader/property_analyzer.h"
#include "core/reader/structure_classifier.h"

#include <gtest/gtest.h>

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

BytecodeEvidence StoreByName(std::string name, uint16_t receiver)
{
    BytecodeEvidence evidence;
    evidence.kind = BytecodeEvidenceKind::STORE_PROPERTY_BY_NAME;
    evidence.range = {100U, 4U};
    evidence.registers[0] = receiver;
    evidence.registerCount = 1U;
    evidence.text = std::move(name);
    return evidence;
}

TEST(PropertyAnalyzerTest, RequiresTheDeclaredReceiverArgument)
{
    PhysicalMethod constructor;
    constructor.numVregs = 2U;
    constructor.numArgs = 4U;
    constructor.evidence.push_back(StoreByName("realField", 4U));     // vregs + a2
    constructor.evidence.push_back(StoreByName("foreignField", 5U));  // vregs + a3

    const auto fields = PropertyAnalyzer::FindStoredProperties(constructor, 2U, false);
    ASSERT_EQ(fields.size(), 1U);
    EXPECT_EQ(fields.front().name, "realField");
}

TEST(PropertyAnalyzerTest, ClassifiesSetterFirstAccessorsByArity)
{
    PhysicalMethod setter;
    setter.offset = 20U;
    setter.numArgs = 4U;
    PhysicalMethod getter;
    getter.offset = 30U;
    getter.numArgs = 3U;
    const std::vector<const PhysicalMethod *> methods {&setter, &getter};

    const auto roles = PropertyAnalyzer::ClassifyAccessorPair(methods);
    ASSERT_EQ(roles.size(), 2U);
    EXPECT_EQ(roles.at(setter.offset), EntityRole::SETTER);
    EXPECT_EQ(roles.at(getter.offset), EntityRole::GETTER);
}

TEST(EnumAnalyzerTest, FindsAKeyStoreWithoutAnArbitraryInstructionLimit)
{
    PhysicalMethod initializer;
    BytecodeEvidence load;
    load.kind = BytecodeEvidenceKind::LOAD_STRING;
    load.text = "FarMember";
    load.range = {10U, 3U};
    initializer.evidence.push_back(load);

    BytecodeEvidence save;
    save.kind = BytecodeEvidenceKind::STORE_ACCUMULATOR;
    save.registers[0] = 7U;
    save.registerCount = 1U;
    initializer.evidence.push_back(save);
    for (size_t index = 0; index < 32U; ++index) {
        BytecodeEvidence unrelated;
        unrelated.kind = BytecodeEvidenceKind::DEFINE_FUNCTION;
        initializer.evidence.push_back(std::move(unrelated));
    }
    BytecodeEvidence store;
    store.kind = BytecodeEvidenceKind::STORE_BY_VALUE;
    store.registers[0] = 9U;
    store.registers[1] = 7U;
    store.registerCount = 2U;
    initializer.evidence.push_back(store);

    const auto members = EnumAnalyzer::RecoverMembers(initializer);
    ASSERT_EQ(members.size(), 1U);
    EXPECT_EQ(members.front().name, "FarMember");
}

TEST(AnnotationAnalyzerTest, FormatsCopiedElementsWithoutBackendHandles)
{
    PhysicalAnnotation annotation;
    annotation.name = "Route";
    annotation.elements.push_back({"path", 'C', "/counter", true});
    EXPECT_EQ(AnnotationAnalyzer::Describe(annotation), "Route(path=/counter)");
    EXPECT_FALSE(AnnotationAnalyzer::IsCompilerOwned(annotation));

    annotation.name = "_ESSlotNumberAnnotation";
    EXPECT_TRUE(AnnotationAnalyzer::IsCompilerOwned(annotation));
}

TEST(AnnotationAnalyzerTest, AttachesRecordAndFieldAnnotationsToTheirOwners)
{
    PhysicalProgram program;
    program.fileSize = 1000U;
    PhysicalRecord module;
    module.offset = 10U;
    module.size = 10U;
    module.name = "sample";
    module.isArkTs = true;
    module.isModule = true;
    program.records.push_back(module);

    PhysicalRecord physicalInterface;
    physicalInterface.offset = 30U;
    physicalInterface.size = 20U;
    physicalInterface.name = "sample.PhysicalInterface";
    physicalInterface.ownerModuleName = "sample";
    physicalInterface.isInterface = true;
    physicalInterface.annotations.push_back({50U, 5U, "RecordAnnotation", {}});
    PhysicalField field;
    field.offset = 60U;
    field.size = 5U;
    field.name = "value";
    field.annotations.push_back({70U, 5U, "FieldAnnotation", {}});
    physicalInterface.fields.push_back(field);
    PhysicalMethod method;
    method.offset = 80U;
    method.size = 5U;
    method.name = "execute";
    method.numArgs = 2U;
    method.annotations.push_back({90U, 5U, "MethodAnnotation", {}});
    physicalInterface.methods.push_back(method);
    program.records.push_back(physicalInterface);

    const auto result = StructureClassifier::Build("sample.abc", program);
    ASSERT_TRUE(result.HasValue()) << result.Error().Message();
    const auto &index = *result.Value();
    ASSERT_FALSE(index.RootIds().empty());
    const auto *interfaceNode =
        FindDescendant(index, index.RootIds().front(), EntityKind::INTERFACE, "PhysicalInterface");
    ASSERT_NE(interfaceNode, nullptr);
    EXPECT_NE(FindDescendant(index, interfaceNode->id, EntityKind::ANNOTATION, "RecordAnnotation"), nullptr);
    const auto *fieldNode = FindDescendant(index, interfaceNode->id, EntityKind::FIELD, "value");
    ASSERT_NE(fieldNode, nullptr);
    EXPECT_NE(FindDescendant(index, fieldNode->id, EntityKind::ANNOTATION, "FieldAnnotation"), nullptr);
    const auto *methodNode = FindDescendant(index, interfaceNode->id, EntityKind::METHOD, "execute");
    ASSERT_NE(methodNode, nullptr);
    EXPECT_EQ(methodNode->role, EntityRole::INSTANCE_METHOD);
    EXPECT_NE(FindDescendant(index, methodNode->id, EntityKind::ANNOTATION, "MethodAnnotation"), nullptr);
}

TEST(AnnotationAnalyzerTest, KeepsCompilerOwnedAnnotationsAsOwnerBacking)
{
    PhysicalProgram program;
    program.fileSize = 1000U;
    PhysicalRecord module;
    module.offset = 10U;
    module.size = 10U;
    module.name = "sample";
    module.isArkTs = true;
    module.isModule = true;
    module.annotations.push_back({50U, 5U, "_ESSlotNumberAnnotation", {}});
    program.records.push_back(module);

    const auto result = StructureClassifier::Build("sample.abc", program);
    ASSERT_TRUE(result.HasValue()) << result.Error().Message();
    const auto &index = *result.Value();
    const auto *moduleNode = FindDescendant(index, index.RootIds().front(), EntityKind::MODULE, "sample");
    ASSERT_NE(moduleNode, nullptr);
    EXPECT_TRUE(std::any_of(moduleNode->backing.begin(), moduleNode->backing.end(), [](const auto &backing) {
        return backing.kind == PhysicalEntityKind::ANNOTATION && backing.name == "_ESSlotNumberAnnotation";
    }));
    EXPECT_EQ(FindDescendant(index, moduleNode->id, EntityKind::ANNOTATION, "_ESSlotNumberAnnotation"), nullptr);
}

TEST(HierarchyBuilderTest, OwnsStableIdsGroupsBackingAndConsumption)
{
    PhysicalProgram program;
    program.fileSize = 100U;
    HierarchyBuilder builder("sample.abc", program);
    ASSERT_TRUE(builder.Initialize());

    EntityNode module(builder.NextId(), builder.RootId(), EntityKind::MODULE, "sample");
    builder.AddBacking(&module, PhysicalEntityKind::RECORD, 10U, 5U, "sample");
    const auto moduleId = module.id;
    ASSERT_TRUE(builder.AddNode(std::move(module)));
    EXPECT_TRUE(builder.Index()->Find(moduleId)->defaultCollapsed);
    const auto groupId = builder.EnsureGroup(moduleId, EntityGroup::ABC_INTERNALS);
    ASSERT_NE(groupId, INVALID_ENTITY_ID);

    const auto *group = builder.Index()->Find(groupId);
    ASSERT_NE(group, nullptr);
    EXPECT_TRUE(group->defaultCollapsed);
    EXPECT_TRUE(builder.IsConsumed(PhysicalEntityKind::RECORD, 10U));
    EXPECT_EQ(builder.Index()->RootIds(), std::vector<EntityId>({builder.RootId()}));
}

TEST(HierarchyBuilderTest, OutOfRangeBackingDiagnosticCarriesEntityContext)
{
    PhysicalProgram program;
    program.fileSize = 100U;
    HierarchyBuilder builder("sample.abc", program);
    ASSERT_TRUE(builder.Initialize());

    EntityNode node(builder.NextId(), builder.RootId(), EntityKind::CLASS, "sample");
    // A reconstructed class can register several backing entries; every
    // out-of-range one must say which entity and which physical range failed
    // instead of repeating a bare fixed message.
    builder.AddBacking(&node, PhysicalEntityKind::METHOD, 200U, 8U, "#~@0=#Foo");
    builder.AddBacking(&node, PhysicalEntityKind::LITERAL_ARRAY, 900U, 0U, "");
    ASSERT_EQ(node.diagnostics.size(), 2U);
    EXPECT_EQ(node.diagnostics[0].severity, DiagnosticSeverity::WARNING);
    EXPECT_EQ(node.diagnostics[0].message, "Physical range is outside the ABC file: Method '#~@0=#Foo' at 0xc8+8");
    EXPECT_EQ(node.diagnostics[1].message, "Physical range is outside the ABC file: Literal Array at 0x384+0");
    // Out-of-range backing is rejected, not consumed.
    EXPECT_TRUE(node.backing.empty());
    EXPECT_FALSE(node.primaryRange.has_value());
}

TEST(HierarchyBuilderTest, BatchesLargePhysicalFallbackInventories)
{
    PhysicalProgram program;
    program.fileSize = 10000U;
    PhysicalRecord module;
    module.offset = 10U;
    module.size = 10U;
    module.name = "sample";
    module.isArkTs = true;
    module.isModule = true;
    for (uint32_t index = 0; index < 600U; ++index) {
        PhysicalMethod method;
        method.offset = 100U + index;
        method.size = 1U;
        method.name = "unclassified_" + std::to_string(index);
        module.methods.push_back(std::move(method));
    }
    program.records.push_back(std::move(module));
    for (uint32_t index = 0; index < 600U; ++index) {
        program.literalArrays.push_back({FileRange {1000U + index, 0U}, 1U, {}, {}});
    }

    const auto result = StructureClassifier::Build("sample.abc", program);
    ASSERT_TRUE(result.HasValue()) << result.Error().Message();
    const auto &index = *result.Value();
    EXPECT_LT(index.Count(EntityKind::ABC_ITEM), 12U);

    size_t methodBackings = 0U;
    size_t literalBackings = 0U;
    size_t largestBackingBatch = 0U;
    for (const auto *node : index.NodesInIdOrder()) {
        largestBackingBatch = std::max(largestBackingBatch, node->backing.size());
        methodBackings += std::count_if(node->backing.begin(), node->backing.end(),
                                        [](const auto &backing) { return backing.kind == PhysicalEntityKind::METHOD; });
        literalBackings += std::count_if(node->backing.begin(), node->backing.end(), [](const auto &backing) {
            return backing.kind == PhysicalEntityKind::LITERAL_ARRAY;
        });
    }
    EXPECT_EQ(methodBackings, 600U);
    EXPECT_EQ(literalBackings, 600U);
    EXPECT_LE(largestBackingBatch, 256U);
}

TEST(ModuleBindingAnalyzerTest, PresentsAllBindingKindsWithoutCallingRequestsPackages)
{
    ModuleBinding regular;
    regular.kind = ModuleBindingKind::REGULAR_IMPORT;
    regular.localName = "local";
    regular.importName = "remote";
    regular.moduleRequest = "./dependency";
    const auto imported = ModuleBindingAnalyzer::Analyze(regular);
    EXPECT_EQ(imported.kind, EntityKind::IMPORT);
    EXPECT_EQ(imported.group, EntityGroup::IMPORTS);
    EXPECT_EQ(imported.name, "local");
    EXPECT_NE(imported.details.find("request=./dependency"), std::string::npos);
    EXPECT_EQ(imported.details.find("package="), std::string::npos);

    for (const auto kind :
         {ModuleBindingKind::LOCAL_EXPORT, ModuleBindingKind::INDIRECT_EXPORT, ModuleBindingKind::STAR_EXPORT}) {
        ModuleBinding binding;
        binding.kind = kind;
        const auto exported = ModuleBindingAnalyzer::Analyze(binding);
        EXPECT_EQ(exported.kind, EntityKind::EXPORT);
        EXPECT_EQ(exported.group, EntityGroup::EXPORTS);
    }

    ModuleBinding renamedExport;
    renamedExport.kind = ModuleBindingKind::LOCAL_EXPORT;
    renamedExport.localName = "original";
    renamedExport.exportName = "alias";
    const auto renamed = ModuleBindingAnalyzer::Analyze(renamedExport);
    EXPECT_EQ(renamed.name, "alias");
    EXPECT_NE(renamed.details.find("local=original"), std::string::npos);
    EXPECT_NE(renamed.details.find("export=alias"), std::string::npos);
}

TEST(ModuleBindingAnalyzerTest, DiagnosesInvalidModuleRequestIndexes)
{
    ModuleBinding binding;
    binding.kind = ModuleBindingKind::NAMESPACE_IMPORT;
    binding.localName = "namespaceAlias";
    binding.moduleRequestValid = false;
    binding.moduleRequestIndex = 7U;
    const auto presentation = ModuleBindingAnalyzer::Analyze(binding);
    ASSERT_TRUE(presentation.diagnostic.has_value());
    EXPECT_NE(presentation.diagnostic->find("7"), std::string::npos);
}

void ExpectBindingPresentation(const ModuleBinding &binding, const char *name, const char *displayName)
{
    const auto presentation = ModuleBindingAnalyzer::Analyze(binding);
    EXPECT_EQ(presentation.name, name);
    EXPECT_EQ(presentation.displayName, displayName);
}

TEST(ModuleBindingAnalyzerTest, FormatsSourceLikeLabelsWithoutChangingSemanticNames)
{
    ModuleBinding regular;
    regular.kind = ModuleBindingKind::REGULAR_IMPORT;
    regular.localName = "renamedBeta";
    regular.importName = "betaValue";
    regular.moduleRequest = "./structure_beta";
    ExpectBindingPresentation(regular, "renamedBeta", "{ betaValue as renamedBeta } from \"./structure_beta\"");

    ModuleBinding namespaceImport;
    namespaceImport.kind = ModuleBindingKind::NAMESPACE_IMPORT;
    namespaceImport.localName = "beta";
    namespaceImport.moduleRequest = "./structure_beta";
    ExpectBindingPresentation(namespaceImport, "beta", "* as beta from \"./structure_beta\"");

    ModuleBinding localExport;
    localExport.kind = ModuleBindingKind::LOCAL_EXPORT;
    localExport.localName = "Counter";
    localExport.exportName = "Counter";
    ExpectBindingPresentation(localExport, "Counter", "Counter");

    ModuleBinding indirectExport;
    indirectExport.kind = ModuleBindingKind::INDIRECT_EXPORT;
    indirectExport.importName = "betaHelper";
    indirectExport.exportName = "reexportedHelper";
    indirectExport.moduleRequest = "./structure_beta";
    ExpectBindingPresentation(indirectExport, "reexportedHelper",
                              "{ betaHelper as reexportedHelper } from \"./structure_beta\"");

    ModuleBinding starExport;
    starExport.kind = ModuleBindingKind::STAR_EXPORT;
    starExport.moduleRequest = "./structure_beta";
    ExpectBindingPresentation(starExport, "*", "* from \"./structure_beta\"");

    starExport.moduleRequest = R"(./quoted\"module)";
    const auto quoted = ModuleBindingAnalyzer::Analyze(starExport);
    EXPECT_EQ(quoted.displayName, R"expected(* from "./quoted\\\"module")expected");

    starExport.moduleRequestValid = false;
    const auto invalid = ModuleBindingAnalyzer::Analyze(starExport);
    EXPECT_EQ(invalid.displayName, "*");
    EXPECT_TRUE(invalid.diagnostic.has_value());

    ModuleBinding namelessImport;
    namelessImport.kind = ModuleBindingKind::REGULAR_IMPORT;
    namelessImport.moduleRequest = "./structure_beta";
    ExpectBindingPresentation(namelessImport, "*", "*");

    ModuleBinding namelessIndirectExport;
    namelessIndirectExport.kind = ModuleBindingKind::INDIRECT_EXPORT;
    namelessIndirectExport.moduleRequest = "./structure_beta";
    ExpectBindingPresentation(namelessIndirectExport, "*", "*");
}

TEST(CallableAnalyzerTest, UsesModuleInitializerDefinitionsForLegacyNames)
{
    PhysicalRecord record;
    record.name = "legacy.ts";
    PhysicalMethod initializer;
    initializer.name = "func_main_0";
    initializer.offset = 10U;
    BytecodeEvidence functionDefinition;
    functionDefinition.kind = BytecodeEvidenceKind::DEFINE_FUNCTION;
    functionDefinition.targetOffset = 20U;
    initializer.evidence.push_back(functionDefinition);
    BytecodeEvidence classDefinition;
    classDefinition.kind = BytecodeEvidenceKind::DEFINE_CLASS;
    classDefinition.targetOffset = 30U;
    initializer.evidence.push_back(classDefinition);
    record.methods.push_back(initializer);

    PhysicalMethod function;
    function.name = "legacyFunction";
    function.offset = 20U;
    record.methods.push_back(function);
    PhysicalMethod constructor;
    constructor.name = "LegacyClass";
    constructor.offset = 30U;
    record.methods.push_back(constructor);

    const auto result = CallableAnalyzer::Analyze(record);
    ASSERT_TRUE(result.unclassified.empty());
    ASSERT_EQ(result.methods.size(), 3U);
    EXPECT_EQ(result.methods[1].parsed.kind, EntityKind::FUNCTION);
    EXPECT_EQ(result.methods[1].parsed.name, "legacyFunction");
    EXPECT_EQ(result.methods[2].parsed.role, EntityRole::CONSTRUCTOR);
    EXPECT_EQ(result.methods[2].parsed.parentKinds, std::vector<EntityKind>({EntityKind::CLASS}));
}

void AddModuleMethod(PhysicalRecord &module, uint32_t offset, std::string name)
{
    PhysicalMethod method;
    method.offset = offset;
    method.size = 5U;
    method.name = std::move(name);
    module.methods.push_back(std::move(method));
}

TEST(CallableAnalyzerTest, ReusesAReconstructedCallableScopeForItsPhysicalMethod)
{
    PhysicalProgram program;
    program.fileSize = 1000U;
    PhysicalRecord module;
    module.offset = 10U;
    module.size = 10U;
    module.name = "entry";
    module.isArkTs = true;
    module.isModule = true;
    AddModuleMethod(module, 20U, "entry.#~Worker=#Worker");
    AddModuleMethod(module, 30U, "entry.#~Worker>#outer");
    AddModuleMethod(module, 40U, "entry.#~Worker>outer*#inner");
    program.records.push_back(std::move(module));

    const auto result = StructureClassifier::Build("sample.abc", program);
    ASSERT_TRUE(result.HasValue()) << result.Error().Message();
    const auto &index = *result.Value();
    const auto *worker = FindDescendant(index, index.RootIds().front(), EntityKind::CLASS, "Worker");
    ASSERT_NE(worker, nullptr);
    const auto *outer = FindDescendant(index, worker->id, EntityKind::METHOD, "outer");
    ASSERT_NE(outer, nullptr);
    EXPECT_NE(FindDescendant(index, outer->id, EntityKind::FUNCTION, "inner"), nullptr);

    size_t outerCount = 0U;
    for (const auto childId : worker->children) {
        const auto *child = index.Find(childId);
        if (child != nullptr && child->kind == EntityKind::METHOD && child->name == "outer") {
            ++outerCount;
        }
    }
    EXPECT_EQ(outerCount, 1U);
    EXPECT_TRUE(std::any_of(outer->backing.begin(), outer->backing.end(), [](const auto &backing) {
        return backing.kind == PhysicalEntityKind::METHOD && backing.range.offset == 30U;
    }));
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
