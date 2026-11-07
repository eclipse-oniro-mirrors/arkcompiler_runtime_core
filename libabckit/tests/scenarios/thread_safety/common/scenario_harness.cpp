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

#include "scenario_harness.h"

#include <iomanip>
#include <sstream>
#include <unordered_map>

#include <gtest/gtest.h>

#include "libabckit/c/ir_core.h"
#include "libabckit/c/isa/isa_dynamic.h"

namespace libabckit::test::scenario {
namespace {

// Generated from the same ISA schema as the API: identify pool operands without assuming numeric offsets are stable.
#include "inst_props_helpers_dynamic.inc"

const auto *INSPECT = AbckitGetInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
const auto *GRAPH = AbckitGetGraphApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
const auto *ISA = AbckitGetIsaApiDynamicImpl(ABCKIT_VERSION_RELEASE_1_0_0);

void AppendString(AbckitString *string, std::ostream &out)
{
    ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(string, nullptr);
    auto *text = INSPECT->abckitStringToString(string);
    ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(text, nullptr);
    out << std::quoted(text);
}

void AppendFunction(AbckitCoreFunction *function, std::ostream &out)
{
    ASSERT_NE(function, nullptr);
    auto *module = INSPECT->functionGetModule(function);
    ASSERT_NE(module, nullptr);
    ASSERT_NO_FATAL_FAILURE(AppendString(INSPECT->moduleGetName(module), out));
    auto *klass = INSPECT->functionGetParentClass(function);
    ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
    if (klass != nullptr) {
        ASSERT_NO_FATAL_FAILURE(AppendString(INSPECT->classGetName(klass), out));
    }
    ASSERT_NO_FATAL_FAILURE(AppendString(INSPECT->functionGetName(function), out));
}

void AppendLiterals(AbckitLiteralArray *array, std::ostream &out)
{
    ASSERT_NE(array, nullptr);
    std::vector<AbckitLiteral *> literals;
    ASSERT_TRUE(
        INSPECT->literalArrayEnumerateElements(array, &literals, [](AbckitFile *, AbckitLiteral *lit, void *data) {
            static_cast<std::vector<AbckitLiteral *> *>(data)->push_back(lit);
            return true;
        }));
    ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
    out << '[';
    for (auto *lit : literals) {
        auto tag = INSPECT->literalGetTag(lit);
        ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
        out << tag << ':';
        switch (tag) {
            case ABCKIT_LITERAL_TAG_STRING:
                ASSERT_NO_FATAL_FAILURE(AppendString(INSPECT->literalGetString(lit), out));
                break;
            case ABCKIT_LITERAL_TAG_METHOD:
                ASSERT_NO_FATAL_FAILURE(AppendString(INSPECT->literalGetMethod(lit), out));
                break;
            case ABCKIT_LITERAL_TAG_BOOL:
                out << INSPECT->literalGetBool(lit);
                break;
            case ABCKIT_LITERAL_TAG_BYTE:
                out << static_cast<unsigned>(INSPECT->literalGetU8(lit));
                break;
            case ABCKIT_LITERAL_TAG_SHORT:
                out << INSPECT->literalGetU16(lit);
                break;
            case ABCKIT_LITERAL_TAG_INTEGER:
                out << INSPECT->literalGetU32(lit);
                break;
            case ABCKIT_LITERAL_TAG_LONG:
                out << INSPECT->literalGetU64(lit);
                break;
            case ABCKIT_LITERAL_TAG_FLOAT:
                out << std::hexfloat << INSPECT->literalGetFloat(lit);
                break;
            case ABCKIT_LITERAL_TAG_DOUBLE:
                out << std::hexfloat << INSPECT->literalGetDouble(lit);
                break;
            case ABCKIT_LITERAL_TAG_METHODAFFILIATE:
                out << INSPECT->literalGetMethodAffiliate(lit);
                break;
            case ABCKIT_LITERAL_TAG_NULLVALUE:
                break;
            default:
                FAIL() << "Unsupported literal tag in scenario comparison: " << tag;
        }
        ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
        out << ';';
    }
    out << ']';
}

using InstPositions = std::unordered_map<AbckitInst *, size_t>;

void AppendInstruction(AbckitInst *inst, const InstPositions &positions, std::ostream &out)
{
    const auto opcode = ISA->iGetOpcode(inst);
    ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
    auto *type = GRAPH->iGetType(inst);
    ASSERT_NE(type, nullptr);
    auto typeId = INSPECT->typeGetTypeId(type);
    ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
    out << opcode << ':' << typeId << '(';
    auto count = GRAPH->iGetInputCount(inst);
    ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
    for (uint32_t index = 0; index < count; ++index) {
        auto *input = GRAPH->iGetInput(inst, index);
        ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
        auto found = positions.find(input);
        ASSERT_NE(found, positions.end());
        out << found->second << ',';
    }
    out << ')';
    if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_CONSTANT) {
        if (typeId == ABCKIT_TYPE_ID_F64) {
            out << std::hexfloat << GRAPH->iGetConstantValueF64(inst);
        } else if (typeId == ABCKIT_TYPE_ID_I32) {
            out << GRAPH->iGetConstantValueI32(inst);
        } else if (typeId == ABCKIT_TYPE_ID_U64) {
            out << GRAPH->iGetConstantValueU64(inst);
        } else {
            ASSERT_EQ(typeId, ABCKIT_TYPE_ID_I64);
            out << GRAPH->iGetConstantValueI64(inst);
        }
    }
    if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_IF) {
        out << "cc=" << ISA->iGetConditionCode(inst);
    }
    ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);

    const int stringIndex = HasStringIdOperandDynamic(opcode) ? GetStringIdOperandIndexDynamic(opcode) : -1;
    const int methodIndex = HasMethodIdOperandDynamic(opcode) ? GetMethodIdOperandIndexDynamic(opcode) : -1;
    const int arrayIndex = HasLiteralArrayIdOperandDynamic(opcode) ? GetLiteralArrayIdOperandIndexDynamic(opcode) : -1;
    const bool import = opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR ||
                        opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_WIDE_LDEXTERNALMODULEVAR;
    auto immediates = GRAPH->iGetImmediateCount(inst);
    ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
    for (size_t index = 0; index < immediates; ++index) {
        out << "imm" << index << '=';
        if (static_cast<int>(index) == stringIndex) {
            ASSERT_NO_FATAL_FAILURE(AppendString(GRAPH->iGetString(inst), out));
        } else if (static_cast<int>(index) == methodIndex) {
            ASSERT_NO_FATAL_FAILURE(AppendFunction(GRAPH->iGetFunction(inst), out));
        } else if (static_cast<int>(index) == arrayIndex) {
            ASSERT_NO_FATAL_FAILURE(AppendLiterals(GRAPH->iGetLiteralArray(inst), out));
        } else if (import && index == 0) {
            auto *descriptor = ISA->iGetImportDescriptor(inst);
            ASSERT_NE(descriptor, nullptr);
            auto *module = INSPECT->importDescriptorGetImportedModule(descriptor);
            ASSERT_NE(module, nullptr);
            ASSERT_NO_FATAL_FAILURE(AppendString(INSPECT->moduleGetName(module), out));
            ASSERT_NO_FATAL_FAILURE(AppendString(INSPECT->importDescriptorGetName(descriptor), out));
            ASSERT_NO_FATAL_FAILURE(AppendString(INSPECT->importDescriptorGetAlias(descriptor), out));
        } else {
            out << GRAPH->iGetImmediate(inst, index);
            ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
        }
        out << ';';
    }
    out << '\n';
}

class ScopedGraph final {
public:
    explicit ScopedGraph(AbckitGraph *graph) : graph_(graph) {}
    ~ScopedGraph()
    {
        Api()->destroyGraph(graph_);
    }

private:
    AbckitGraph *graph_;
};
}  // namespace

void CaptureGraph(AbckitCoreFunction *function, Observation &observation)
{
    auto *graph = INSPECT->createGraphFromFunction(function);
    ASSERT_NE(graph, nullptr);
    ScopedGraph owner(graph);
    ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
    std::vector<AbckitBasicBlock *> blocks;
    ASSERT_TRUE(GRAPH->gVisitBlocksRpo(graph, &blocks, [](AbckitBasicBlock *block, void *data) {
        static_cast<std::vector<AbckitBasicBlock *> *>(data)->push_back(block);
        return true;
    }));
    ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
    std::unordered_map<AbckitBasicBlock *, size_t> blockPositions;
    InstPositions positions;
    std::vector<std::vector<AbckitInst *>> instructions(blocks.size());
    for (size_t index = 0; index < blocks.size(); ++index) {
        blockPositions.emplace(blocks[index], index);
        auto *inst = GRAPH->bbGetFirstInst(blocks[index]);
        ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
        while (inst != nullptr) {
            positions.emplace(inst, positions.size());
            instructions[index].push_back(inst);
            inst = GRAPH->iGetNext(inst);
            ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
        }
    }
    std::ostringstream out;
    ASSERT_NO_FATAL_FAILURE(AppendFunction(function, out));
    for (size_t index = 0; index < blocks.size(); ++index) {
        auto *block = blocks[index];
        out << "\nblock " << index << ':';
        for (auto query : {GRAPH->bbIsStart, GRAPH->bbIsEnd, GRAPH->bbIsTryBegin, GRAPH->bbIsTry, GRAPH->bbIsTryEnd,
                           GRAPH->bbIsCatchBegin, GRAPH->bbIsCatch}) {
            out << query(block);
            ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
        }
        // Predecessor order matters for Phi inputs; successor order matters for conditional branches.
        for (bool predecessors : {true, false}) {
            auto count = predecessors ? GRAPH->bbGetPredBlockCount(block) : GRAPH->bbGetSuccBlockCount(block);
            ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
            out << (predecessors ? " pred:" : " succ:");
            for (uint32_t edge = 0; edge < count; ++edge) {
                auto *next = predecessors ? GRAPH->bbGetPredBlock(block, edge) : GRAPH->bbGetSuccBlock(block, edge);
                ASSERT_EQ(Api()->getLastError(), ABCKIT_STATUS_NO_ERROR);
                auto found = blockPositions.find(next);
                ASSERT_NE(found, blockPositions.end());
                out << found->second << ',';
            }
        }
        out << '\n';
        for (auto *inst : instructions[index]) {
            ASSERT_NO_FATAL_FAILURE(AppendInstruction(inst, positions, out));
        }
    }
    observation.graphs.push_back(out.str());
}

void Compare(const Observation &serial, const Observation &parallel)
{
    ASSERT_FALSE(serial.graphs.empty());
    ASSERT_EQ(serial.graphs.size(), parallel.graphs.size());
    for (size_t index = 0; index < serial.graphs.size(); ++index) {
        EXPECT_EQ(serial.graphs[index], parallel.graphs[index]) << "Persisted function #" << index;
    }
    if (ShouldVerifyRuntimeOutput()) {
        EXPECT_EQ(serial.output, parallel.output);
    }
}

std::string NormalizeElapsedTime(const std::string &output)
{
    std::istringstream input(output);
    std::ostringstream normalized;
    std::string line;
    bool elapsedValue = false;
    while (std::getline(input, line)) {
        if (elapsedValue) {
            normalized << "<elapsed-ms>\n";
        } else {
            normalized << line << '\n';
        }
        elapsedValue = line == "Ellapsed time:";
    }
    return normalized.str();
}
}  // namespace libabckit::test::scenario
