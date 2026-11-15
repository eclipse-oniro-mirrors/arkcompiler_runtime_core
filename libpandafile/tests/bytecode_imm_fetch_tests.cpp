/**
 * Copyright (c) 2024-2026 Huawei Device Co., Ltd.
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

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <sstream>
#include <type_traits>

#include "bytecode_instruction-inl.h"

namespace panda::test {

TEST(BytecodeInstruction, Signed)
{
    {
        // jeqz 23
        const uint8_t bytecode[] = {0x4f, 0x17};
        BytecodeInstruction inst(bytecode);
        EXPECT_EQ(static_cast<uint8_t>(inst.GetOpcode()), 0x4f);
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM8_V8_V8_V8, 0>()), static_cast<int8_t>(0x17));
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM8_V8_V8_V8, 0, true>()), static_cast<int8_t>(0x17));
        EXPECT_EQ(inst.GetImmData(0), static_cast<int64_t>(0x17));
        EXPECT_EQ(inst.GetImmCount(), 1);
    }

    {
        // jmp -22
        const uint8_t bytecode[] = {0x4d, 0xea};
        BytecodeInstruction inst(bytecode);
        EXPECT_EQ(static_cast<uint8_t>(inst.GetOpcode()), 0x4d);
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM8, 0>()), static_cast<int8_t>(-22));
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM8, 0, true>()), static_cast<int8_t>(-22));
        EXPECT_EQ(inst.GetImmData(0), static_cast<int64_t>(-22));
        EXPECT_EQ(inst.GetImmCount(), 1);
    }

    {
        // ldai 30
        const uint8_t bytecode[] = {0x62, 0x1e, 0x00, 0x00, 0x00};
        BytecodeInstruction inst(bytecode);
        EXPECT_EQ(static_cast<uint8_t>(inst.GetOpcode()), 0x62);
        EXPECT_EQ(inst.GetFormat(), BytecodeInstruction::Format::IMM32);
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM32, 0>()), static_cast<int32_t>(0x1e));
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM32, 0, true>()), static_cast<int32_t>(0x1e));
        EXPECT_EQ(inst.GetImmData(0), static_cast<int64_t>(0x1e));
        EXPECT_EQ(inst.GetImmCount(), 1);
    }

    {
        // fldai 3.14
        const uint8_t bytecode[] = {0x63, 0x1f, 0x85, 0xeb, 0x51, 0xb8, 0x1e, 0x09, 0x40};
        BytecodeInstruction inst(bytecode);
        EXPECT_EQ(static_cast<uint8_t>(inst.GetOpcode()), 0x63);
        EXPECT_EQ((bit_cast<double>(inst.GetImm<BytecodeInstruction::Format::IMM64, 0, true>())), 3.14);
        EXPECT_EQ(inst.GetImmData(0), static_cast<int64_t>(0x40091eb851eb851f));
        EXPECT_EQ(inst.GetImmCount(), 1);
    }
}

TEST(BytecodeInstruction, ImmediateTypeAndRawBits)
{
    constexpr uint64_t HIGH_BIT_U8 = 0x8e;
    constexpr uint64_t PI_RAW_BITS = 0x40091eb851eb851f;

    {
        // callthis2 142, v0, v2, v3
        const uint8_t bytecode[] = {0x2f, 0x8e, 0x00, 0x02, 0x03};
        BytecodeInstruction inst(bytecode);
        auto type = inst.GetImmType(0);
        EXPECT_EQ(type.kind, BytecodeImmediateKind::UNSIGNED);
        EXPECT_EQ(type.bitWidth, 8U);
        EXPECT_EQ(inst.GetImmRaw(0), HIGH_BIT_U8);
        EXPECT_EQ(inst.GetImmData(0), static_cast<int64_t>(142));

        BytecodeInstructionSafe safeInst(bytecode, bytecode, &bytecode[sizeof(bytecode) - 1]);
        auto safeType = safeInst.GetImmType(0);
        EXPECT_EQ(safeType.kind, BytecodeImmediateKind::UNSIGNED);
        EXPECT_EQ(safeType.bitWidth, 8U);
        EXPECT_EQ(safeInst.GetImmRaw(0), HIGH_BIT_U8);
        EXPECT_EQ(safeInst.GetImmData(0), static_cast<int64_t>(142));
        EXPECT_TRUE(safeInst.IsValid());
    }

    {
        // fldai 3.14
        const uint8_t bytecode[] = {0x63, 0x1f, 0x85, 0xeb, 0x51, 0xb8, 0x1e, 0x09, 0x40};
        BytecodeInstruction inst(bytecode);
        auto type = inst.GetImmType(0);
        EXPECT_EQ(type.kind, BytecodeImmediateKind::FLOAT);
        EXPECT_EQ(type.bitWidth, 64U);
        EXPECT_EQ(inst.GetImmRaw(0), PI_RAW_BITS);

        BytecodeInstructionSafe safeInst(bytecode, bytecode, &bytecode[sizeof(bytecode) - 1]);
        auto safeType = safeInst.GetImmType(0);
        EXPECT_EQ(safeType.kind, BytecodeImmediateKind::FLOAT);
        EXPECT_EQ(safeType.bitWidth, 64U);
        EXPECT_EQ(safeInst.GetImmRaw(0), PI_RAW_BITS);
        EXPECT_TRUE(safeInst.IsValid());
    }
}

TEST(BytecodeInstruction, ImmediateQueryBounds)
{
    const uint8_t complete[] = {0x2f, 0x8e, 0x00, 0x02, 0x03};
    BytecodeInstruction inst(complete);
    BytecodeInstructionSafe safeInst(complete, complete, &complete[sizeof(complete) - 1]);
#ifndef NDEBUG
    EXPECT_DEATH(static_cast<void>(inst.GetImmType(1)), "such index");
    EXPECT_DEATH(static_cast<void>(inst.GetImmRaw(1)), "such index");
    EXPECT_DEATH(static_cast<void>(safeInst.GetImmType(1)), "such index");
    EXPECT_DEATH(static_cast<void>(safeInst.GetImmRaw(1)), "such index");
#else
    EXPECT_EQ(inst.GetImmType(1).bitWidth, 0U);
    EXPECT_EQ(inst.GetImmRaw(1), 0U);
    EXPECT_EQ(safeInst.GetImmType(1).bitWidth, 0U);
    EXPECT_EQ(safeInst.GetImmRaw(1), 0U);
    EXPECT_TRUE(safeInst.IsValid());
#endif

    const uint8_t truncated[] = {0x2f};
    BytecodeInstructionSafe truncatedRaw(truncated, truncated, truncated);
    EXPECT_EQ(truncatedRaw.GetImmRaw(0), 0U);
    EXPECT_FALSE(truncatedRaw.IsValid());

    BytecodeInstructionSafe truncatedData(truncated, truncated, truncated);
    EXPECT_EQ(truncatedData.GetImmData(0), 0);
    EXPECT_FALSE(truncatedData.IsValid());
}

static void ExpectOperandDescriptor(const BytecodeOperandDescriptor &descriptor, BytecodeOperandKind kind,
                                    size_t typedIndex, size_t bitWidth)
{
    EXPECT_EQ(descriptor.kind, kind);
    EXPECT_EQ(descriptor.typedIndex, typedIndex);
    EXPECT_EQ(descriptor.bitWidth, bitWidth);
}

TEST(BytecodeInstruction, OperandDescriptors)
{
    const uint8_t zeroOperandBytecode[] = {0x00};
    BytecodeInstruction zeroOperandInst(zeroOperandBytecode);
    EXPECT_EQ(zeroOperandInst.GetOpcode(), BytecodeInstruction::Opcode::LDUNDEFINED);
    EXPECT_EQ(zeroOperandInst.GetOperandCount(), 0U);

    // throw.undefinedifholewithname id4660
    const uint8_t throwBytecode[] = {0xfe, 0x09, 0x34, 0x12};
    BytecodeInstruction throwInst(throwBytecode);
    EXPECT_EQ(throwInst.GetOpcode(), BytecodeInstruction::Opcode::THROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16);
    EXPECT_EQ(throwInst.GetOperandCount(), 1U);
    ExpectOperandDescriptor(throwInst.GetOperandDescriptor(0), BytecodeOperandKind::ID, 0U, 16U);

    // deprecated.getmodulenamespace id305419896
    const uint8_t deprecatedBytecode[] = {0xfc, 0x23, 0x78, 0x56, 0x34, 0x12};
    BytecodeInstruction deprecatedInst(deprecatedBytecode);
    EXPECT_EQ(deprecatedInst.GetOpcode(), BytecodeInstruction::Opcode::DEPRECATED_GETMODULENAMESPACE_PREF_ID32);
    EXPECT_EQ(deprecatedInst.GetOperandCount(), 1U);
    ExpectOperandDescriptor(deprecatedInst.GetOperandDescriptor(0), BytecodeOperandKind::ID, 0U, 32U);

    // newobjrange 11, 3, v6
    const uint8_t rangeBytecode[] = {0x08, 0x0b, 0x03, 0x06};
    BytecodeInstruction rangeInst(rangeBytecode);
    EXPECT_EQ(rangeInst.GetOpcode(), BytecodeInstruction::Opcode::NEWOBJRANGE_IMM8_IMM8_V8);
    EXPECT_EQ(rangeInst.GetOperandCount(), 3U);
    ExpectOperandDescriptor(rangeInst.GetOperandDescriptor(0), BytecodeOperandKind::IMMEDIATE, 0U, 0U);
    ExpectOperandDescriptor(rangeInst.GetOperandDescriptor(1), BytecodeOperandKind::IMMEDIATE, 1U, 0U);
    ExpectOperandDescriptor(rangeInst.GetOperandDescriptor(2), BytecodeOperandKind::REGISTER, 0U, 0U);

    // callruntime.definesendableclass 513, id1027, id1541, 2055, v9
    const uint8_t mixedBytecode[] = {0xfb, 0x07, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
    BytecodeInstruction mixedInst(mixedBytecode);
    EXPECT_EQ(mixedInst.GetOpcode(),
              BytecodeInstruction::Opcode::CALLRUNTIME_DEFINESENDABLECLASS_PREF_IMM16_ID16_ID16_IMM16_V8);
    EXPECT_EQ(mixedInst.GetOperandCount(), 5U);
    ExpectOperandDescriptor(mixedInst.GetOperandDescriptor(0), BytecodeOperandKind::IMMEDIATE, 0U, 0U);
    ExpectOperandDescriptor(mixedInst.GetOperandDescriptor(1), BytecodeOperandKind::ID, 0U, 16U);
    ExpectOperandDescriptor(mixedInst.GetOperandDescriptor(2), BytecodeOperandKind::ID, 1U, 16U);
    ExpectOperandDescriptor(mixedInst.GetOperandDescriptor(3), BytecodeOperandKind::IMMEDIATE, 1U, 0U);
    ExpectOperandDescriptor(mixedInst.GetOperandDescriptor(4), BytecodeOperandKind::REGISTER, 0U, 0U);

#ifndef NDEBUG
    EXPECT_DEATH(static_cast<void>(zeroOperandInst.GetOperandDescriptor(0)), "such index");
    EXPECT_DEATH(static_cast<void>(mixedInst.GetOperandDescriptor(5)), "such index");
#else
    ExpectOperandDescriptor(zeroOperandInst.GetOperandDescriptor(0), BytecodeOperandKind::REGISTER, 0U, 0U);
    ExpectOperandDescriptor(mixedInst.GetOperandDescriptor(5), BytecodeOperandKind::REGISTER, 0U, 0U);
#endif
}

TEST(BytecodeInstructionSafe, OperandDescriptors)
{
    const uint8_t zeroOperandBytecode[] = {0x00};
    BytecodeInstructionSafe zeroOperandInst(zeroOperandBytecode, zeroOperandBytecode, zeroOperandBytecode);
    EXPECT_EQ(zeroOperandInst.GetOpcode(), BytecodeInstructionSafe::Opcode::LDUNDEFINED);
    EXPECT_EQ(zeroOperandInst.GetOperandCount(), 0U);

    // throw.undefinedifholewithname id4660
    const uint8_t throwBytecode[] = {0xfe, 0x09, 0x34, 0x12};
    BytecodeInstructionSafe throwInst(throwBytecode, throwBytecode, &throwBytecode[sizeof(throwBytecode) - 1]);
    EXPECT_EQ(throwInst.GetOpcode(), BytecodeInstructionSafe::Opcode::THROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16);
    EXPECT_EQ(throwInst.GetOperandCount(), 1U);
    ExpectOperandDescriptor(throwInst.GetOperandDescriptor(0), BytecodeOperandKind::ID, 0U, 16U);

    // deprecated.getmodulenamespace id305419896
    const uint8_t deprecatedBytecode[] = {0xfc, 0x23, 0x78, 0x56, 0x34, 0x12};
    BytecodeInstructionSafe deprecatedInst(deprecatedBytecode, deprecatedBytecode,
                                           &deprecatedBytecode[sizeof(deprecatedBytecode) - 1]);
    EXPECT_EQ(deprecatedInst.GetOpcode(), BytecodeInstructionSafe::Opcode::DEPRECATED_GETMODULENAMESPACE_PREF_ID32);
    EXPECT_EQ(deprecatedInst.GetOperandCount(), 1U);
    ExpectOperandDescriptor(deprecatedInst.GetOperandDescriptor(0), BytecodeOperandKind::ID, 0U, 32U);

    // newobjrange 11, 3, v6
    const uint8_t rangeBytecode[] = {0x08, 0x0b, 0x03, 0x06};
    BytecodeInstructionSafe rangeInst(rangeBytecode, rangeBytecode, &rangeBytecode[sizeof(rangeBytecode) - 1]);
    EXPECT_EQ(rangeInst.GetOpcode(), BytecodeInstructionSafe::Opcode::NEWOBJRANGE_IMM8_IMM8_V8);
    EXPECT_EQ(rangeInst.GetOperandCount(), 3U);
    ExpectOperandDescriptor(rangeInst.GetOperandDescriptor(0), BytecodeOperandKind::IMMEDIATE, 0U, 0U);
    ExpectOperandDescriptor(rangeInst.GetOperandDescriptor(1), BytecodeOperandKind::IMMEDIATE, 1U, 0U);
    ExpectOperandDescriptor(rangeInst.GetOperandDescriptor(2), BytecodeOperandKind::REGISTER, 0U, 0U);

    // callruntime.definesendableclass 513, id1027, id1541, 2055, v9
    const uint8_t mixedBytecode[] = {0xfb, 0x07, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
    BytecodeInstructionSafe mixedInst(mixedBytecode, mixedBytecode, &mixedBytecode[sizeof(mixedBytecode) - 1]);
    EXPECT_EQ(mixedInst.GetOpcode(),
              BytecodeInstructionSafe::Opcode::CALLRUNTIME_DEFINESENDABLECLASS_PREF_IMM16_ID16_ID16_IMM16_V8);
    EXPECT_EQ(mixedInst.GetOperandCount(), 5U);
    ExpectOperandDescriptor(mixedInst.GetOperandDescriptor(0), BytecodeOperandKind::IMMEDIATE, 0U, 0U);
    ExpectOperandDescriptor(mixedInst.GetOperandDescriptor(1), BytecodeOperandKind::ID, 0U, 16U);
    ExpectOperandDescriptor(mixedInst.GetOperandDescriptor(2), BytecodeOperandKind::ID, 1U, 16U);
    ExpectOperandDescriptor(mixedInst.GetOperandDescriptor(3), BytecodeOperandKind::IMMEDIATE, 1U, 0U);
    ExpectOperandDescriptor(mixedInst.GetOperandDescriptor(4), BytecodeOperandKind::REGISTER, 0U, 0U);

#ifndef NDEBUG
    EXPECT_DEATH(static_cast<void>(zeroOperandInst.GetOperandDescriptor(0)), "such index");
    EXPECT_DEATH(static_cast<void>(mixedInst.GetOperandDescriptor(5)), "such index");
#else
    ExpectOperandDescriptor(zeroOperandInst.GetOperandDescriptor(0), BytecodeOperandKind::REGISTER, 0U, 0U);
    ExpectOperandDescriptor(mixedInst.GetOperandDescriptor(5), BytecodeOperandKind::REGISTER, 0U, 0U);
#endif
    EXPECT_TRUE(zeroOperandInst.IsValid());
    EXPECT_TRUE(mixedInst.IsValid());
}

TEST(BytecodeInstruction, UnsignedOneImm)
{
    {
        // callthis2 142, v0, v2, v3
        const uint8_t bytecode[] = {0x2f, 0x8e, 0x00, 0x02, 0x03};
        BytecodeInstruction inst(bytecode);
        EXPECT_EQ(static_cast<uint8_t>(inst.GetOpcode()), 0x2f);
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM8_V8_V8_V8, 0>()), static_cast<int8_t>(0x8e));
        EXPECT_NE((inst.GetImm<BytecodeInstruction::Format::IMM8_V8_V8_V8, 0>()), static_cast<uint8_t>(0x8e));
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM8_V8_V8_V8, 0, false>()), static_cast<uint8_t>(0x8e));
        EXPECT_EQ(inst.GetImmData(0), static_cast<int64_t>(0x8e));
        EXPECT_EQ(inst.GetImmCount(), 1);
    }

    {
        // neg 13
        const uint8_t bytecode[] = {0x1f, 0x0d};
        BytecodeInstruction inst(bytecode);
        EXPECT_EQ(static_cast<uint8_t>(inst.GetOpcode()), 0x1f);
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM8, 0>()), static_cast<int8_t>(0x0d));
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM8, 0, false>()), static_cast<uint8_t>(0x0d));
        EXPECT_EQ(inst.GetImmData(0), static_cast<int64_t>(0x0d));
        EXPECT_EQ(inst.GetImmCount(), 1);
    }

    {
        // tryldglobalbyname 128, id6
        const uint8_t bytecode[] = {0x8c, 0x80, 0x00, 0x06, 0x00};
        BytecodeInstruction inst(bytecode);
        EXPECT_EQ(static_cast<uint8_t>(inst.GetOpcode()), 0x8c);
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM16_ID16, 0>()), static_cast<int16_t>(0x80));
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM16_ID16, 0, false>()), static_cast<uint16_t>(0x80));
        EXPECT_EQ(inst.GetImmData(0), static_cast<int64_t>(0x80));
        EXPECT_EQ(inst.GetImmCount(), 1);
    }
}

TEST(BytecodeInstruction, UnsignedTwoImm)
{
    {
        // stlexvar 0, 2
        const uint8_t bytecode[] = {0x3d, 0x20};
        BytecodeInstruction inst(bytecode);
        EXPECT_EQ(static_cast<uint8_t>(inst.GetOpcode()), 0x3d);
        EXPECT_EQ(inst.GetFormat(), BytecodeInstruction::Format::IMM4_IMM4);
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM4_IMM4, 0>()), 0);
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM4_IMM4, 1>()), 2);
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM4_IMM4, 0, false>()), 0);
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM4_IMM4, 1, false>()), 2);
        EXPECT_EQ(inst.GetImmData(0), static_cast<int64_t>(0));
        EXPECT_EQ(inst.GetImmData(1), static_cast<int64_t>(2));
        EXPECT_EQ(inst.GetImmCount(), 2);
    }

    {
        // definefunc 2, id8, 1
        const uint8_t bytecode[] = {0x33, 0x02, 0x08, 0x00, 0x01};
        BytecodeInstruction inst(bytecode);
        EXPECT_EQ(static_cast<uint8_t>(inst.GetOpcode()), 0x33);
        EXPECT_EQ(inst.GetFormat(), BytecodeInstruction::Format::IMM8_ID16_IMM8);
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM8_ID16_IMM8, 0>()), static_cast<int8_t>(2));
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM8_ID16_IMM8, 1>()), static_cast<int8_t>(1));
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM8_ID16_IMM8, 0, false>()), static_cast<int8_t>(2));
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM8_ID16_IMM8, 1, false>()), static_cast<int8_t>(1));
        EXPECT_EQ(inst.GetImmData(0), static_cast<int64_t>(2));
        EXPECT_EQ(inst.GetImmData(1), static_cast<int64_t>(1));
        EXPECT_EQ(inst.GetImmCount(), 2);
    }

    {
        // defineclasswithbuffer 2, id9, id12, 2, v3
        const uint8_t bytecode[] = {0x35, 0x02, 0x09, 0x00, 0x0c, 0x00, 0x02, 0x00, 0x03};
        BytecodeInstruction inst(bytecode);
        EXPECT_EQ(static_cast<uint8_t>(inst.GetOpcode()), 0x35);
        EXPECT_EQ(inst.GetFormat(), BytecodeInstruction::Format::IMM8_ID16_ID16_IMM16_V8);
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM8_ID16_ID16_IMM16_V8, 0>()), static_cast<int8_t>(2));
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM8_ID16_ID16_IMM16_V8, 1>()), static_cast<int16_t>(2));
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM8_ID16_ID16_IMM16_V8, 0, false>()),
                  static_cast<uint8_t>(2));
        EXPECT_EQ((inst.GetImm<BytecodeInstruction::Format::IMM8_ID16_ID16_IMM16_V8, 1, false>()),
                  static_cast<uint16_t>(2));
        EXPECT_EQ(inst.GetImmData(0), static_cast<int64_t>(2));
        EXPECT_EQ(inst.GetImmData(1), static_cast<int64_t>(2));
        EXPECT_EQ(inst.GetImmCount(), 2);
    }
}

TEST(BytecodeInstruction, OutputOperator)
{
    {
        const uint8_t bytecode[] = {0x4f, 0x17};
        BytecodeInstruction inst(bytecode);
        std::ostringstream oss;
        oss << inst;
        EXPECT_EQ(oss.str(), "jeqz 23");
    }

    {
        const uint8_t bytecode[] = {0x4d, 0xea};
        BytecodeInstruction inst(bytecode);
        std::ostringstream oss;
        oss << inst;
        EXPECT_EQ(oss.str(), "jmp -22");
    }

    {
        const uint8_t bytecode[] = {0x62, 0x1e, 0x00, 0x00, 0x00};
        BytecodeInstruction inst(bytecode);
        std::ostringstream oss;
        oss << inst;
        EXPECT_EQ(oss.str(), "ldai 30");
    }

    {
        const uint8_t bytecode[] = {0x63, 0x1f, 0x85, 0xeb, 0x51, 0xb8, 0x1e, 0x09, 0x40};
        BytecodeInstruction inst(bytecode);
        std::ostringstream oss;
        oss << inst;
        EXPECT_EQ(oss.str(), "fldai 3.14");
    }

    {
        const uint8_t bytecode[] = {0x2d, 0x80, 0x00};
        BytecodeInstruction inst(bytecode);
        std::ostringstream oss;
        oss << inst;
        EXPECT_EQ(oss.str(), "callthis0 128, v0");
    }
}

TEST(BytecodeInstruction, GetLiteralIndex)
{
    {
        // legal ins, and have 0 literal id
        const uint8_t bytecode[] = {0x05};
        BytecodeInstruction inst(bytecode);
        EXPECT_EQ(static_cast<int>(inst.GetLiteralIndex()), -1);
    }
    {
        // legal ins, and have 1 literal id in loc 0
        const uint8_t bytecode[] = {0x06, 0x00, 0x00, 0x00};
        BytecodeInstruction inst(bytecode);
        EXPECT_EQ(static_cast<int>(inst.GetLiteralIndex()), 0);
    }
    {
        // legal ins, and have 1 literal id in loc 1
        const uint8_t bytecode[] = {0x35, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        BytecodeInstruction inst(bytecode);
        EXPECT_EQ(static_cast<int>(inst.GetLiteralIndex()), 1);
    }
    {
        // illegal ins
        const uint8_t bytecode[] = {0xff};
        BytecodeInstruction inst(bytecode);
        EXPECT_EQ(static_cast<int>(inst.GetLiteralIndex()), -1);
    }
}

TEST(BytecodeInstruction, GetLastVReg)
{
    {
        // newobjrange 0xb, 0x3, v6
        const uint8_t bytecode[] = {0x08, 0x0b, 0x03, 0x06};
        BytecodeInstruction inst(bytecode);
        EXPECT_EQ(inst.GetLastVReg().value(), 6);
    }
}
}  // namespace panda::test
