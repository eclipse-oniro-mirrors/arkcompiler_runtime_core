/**
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

#ifndef COMPILER_OPTIMIZER_IR_INST_H_
#define COMPILER_OPTIMIZER_IR_INST_H_

#include <array>
#include <vector>
#include <iostream>
#include "constants.h"
#include "datatype.h"

#ifdef PANDA_COMPILER_DEBUG_INFO
#include "debug_info.h"
#endif

#include "ir-dyn-base-types.h"
#include "marker.h"
#include "utils/arena_containers.h"
#include "utils/span.h"
#include "utils/bit_field.h"
#include "utils/bit_utils.h"
#include "utils/bit_vector.h"
#include "macros.h"
#include "mem/arena_allocator.h"
#include "opcodes.h"
#include "compiler_options.h"
#include "runtime_interface.h"
#include "spill_fill_data.h"
#include "compiler/code_info/vreg_info.h"
namespace panda::compiler {
class Inst;
class BasicBlock;
class Graph;
class GraphVisitor;
class VnObject;
class SaveStateItem;
class LocationsInfo;
using InstVector = ArenaVector<Inst *>;

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define INST_DEF(opcode, base, ...) class base;
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
OPCODE_LIST(INST_DEF)
#undef INST_DEF

/*
 * Condition code, used in Compare, If[Imm] and Select[Imm] instructions.
 *
 * N.B. BranchElimination and Peephole rely on the order of these codes. Change carefully.
 */
enum ConditionCode {
    // All types.
    CC_EQ = 0,  // ==
    CC_NE,      // !=
    // Signed integers and floating-point numbers.
    CC_LT,  // <
    CC_LE,  // <=
    CC_GT,  // >
    CC_GE,  // >=
    // Unsigned integers.
    CC_B,   // <
    CC_BE,  // <=
    CC_A,   // >
    CC_AE,  // >=
    // Compare result of bitwise AND with zero
    CC_TST_EQ,  // (lhs AND rhs) == 0
    CC_TST_NE,  // (lhs AND rhs) != 0
    // First and last aliases.
    CC_FIRST = CC_EQ,
    CC_LAST = CC_TST_NE,
};

inline ConditionCode GetInverseConditionCode(ConditionCode code)
{
    switch (code) {
        case ConditionCode::CC_EQ:
            return ConditionCode::CC_NE;
        case ConditionCode::CC_NE:
            return ConditionCode::CC_EQ;

        case ConditionCode::CC_LT:
            return ConditionCode::CC_GE;
        case ConditionCode::CC_LE:
            return ConditionCode::CC_GT;
        case ConditionCode::CC_GT:
            return ConditionCode::CC_LE;
        case ConditionCode::CC_GE:
            return ConditionCode::CC_LT;

        case ConditionCode::CC_B:
            return ConditionCode::CC_AE;
        case ConditionCode::CC_BE:
            return ConditionCode::CC_A;
        case ConditionCode::CC_A:
            return ConditionCode::CC_BE;
        case ConditionCode::CC_AE:
            return ConditionCode::CC_B;

        case ConditionCode::CC_TST_EQ:
            return ConditionCode::CC_TST_NE;
        case ConditionCode::CC_TST_NE:
            return ConditionCode::CC_TST_EQ;

        default:
            UNREACHABLE();
    }
}

inline ConditionCode InverseSignednessConditionCode(ConditionCode code)
{
    switch (code) {
        case ConditionCode::CC_EQ:
            return ConditionCode::CC_EQ;
        case ConditionCode::CC_NE:
            return ConditionCode::CC_NE;

        case ConditionCode::CC_LT:
            return ConditionCode::CC_B;
        case ConditionCode::CC_LE:
            return ConditionCode::CC_BE;
        case ConditionCode::CC_GT:
            return ConditionCode::CC_A;
        case ConditionCode::CC_GE:
            return ConditionCode::CC_AE;

        case ConditionCode::CC_B:
            return ConditionCode::CC_LT;
        case ConditionCode::CC_BE:
            return ConditionCode::CC_LE;
        case ConditionCode::CC_A:
            return ConditionCode::CC_GT;
        case ConditionCode::CC_AE:
            return ConditionCode::CC_GE;

        case ConditionCode::CC_TST_EQ:
            return ConditionCode::CC_TST_EQ;
        case ConditionCode::CC_TST_NE:
            return ConditionCode::CC_TST_NE;

        default:
            UNREACHABLE();
    }
}

inline bool IsSignedConditionCode(ConditionCode code)
{
    switch (code) {
        case ConditionCode::CC_LT:
        case ConditionCode::CC_LE:
        case ConditionCode::CC_GT:
        case ConditionCode::CC_GE:
            return true;

        case ConditionCode::CC_EQ:
        case ConditionCode::CC_NE:
        case ConditionCode::CC_B:
        case ConditionCode::CC_BE:
        case ConditionCode::CC_A:
        case ConditionCode::CC_AE:
        case ConditionCode::CC_TST_EQ:
        case ConditionCode::CC_TST_NE:
            return false;

        default:
            UNREACHABLE();
    }
}

inline ConditionCode SwapOperandsConditionCode(ConditionCode code)
{
    switch (code) {
        case ConditionCode::CC_EQ:
        case ConditionCode::CC_NE:
            return code;

        case ConditionCode::CC_LT:
            return ConditionCode::CC_GT;
        case ConditionCode::CC_LE:
            return ConditionCode::CC_GE;
        case ConditionCode::CC_GT:
            return ConditionCode::CC_LT;
        case ConditionCode::CC_GE:
            return ConditionCode::CC_LE;

        case ConditionCode::CC_B:
            return ConditionCode::CC_A;
        case ConditionCode::CC_BE:
            return ConditionCode::CC_AE;
        case ConditionCode::CC_A:
            return ConditionCode::CC_B;
        case ConditionCode::CC_AE:
            return ConditionCode::CC_BE;

        case ConditionCode::CC_TST_EQ:
        case ConditionCode::CC_TST_NE:
            return code;

        default:
            UNREACHABLE();
    }
}

template <typename T>
bool Compare(ConditionCode cc, T lhs, T rhs)
{
    using SignedT = std::make_signed_t<T>;
    using UnsignedT = std::make_unsigned_t<T>;
    auto lhs_u = bit_cast<UnsignedT>(lhs);
    auto rhs_u = bit_cast<UnsignedT>(rhs);
    auto lhs_s = bit_cast<SignedT>(lhs);
    auto rhs_s = bit_cast<SignedT>(rhs);

    switch (cc) {
        case ConditionCode::CC_EQ:
            return lhs_u == rhs_u;
        case ConditionCode::CC_NE:
            return lhs_u != rhs_u;
        case ConditionCode::CC_LT:
            return lhs_s < rhs_s;
        case ConditionCode::CC_LE:
            return lhs_s <= rhs_s;
        case ConditionCode::CC_GT:
            return lhs_s > rhs_s;
        case ConditionCode::CC_GE:
            return lhs_s >= rhs_s;
        case ConditionCode::CC_B:
            return lhs_u < rhs_u;
        case ConditionCode::CC_BE:
            return lhs_u <= rhs_u;
        case ConditionCode::CC_A:
            return lhs_u > rhs_u;
        case ConditionCode::CC_AE:
            return lhs_u >= rhs_u;
        case ConditionCode::CC_TST_EQ:
            return (lhs_u & rhs_u) == 0;
        case ConditionCode::CC_TST_NE:
            return (lhs_u & rhs_u) != 0;
        default:
            UNREACHABLE();
            return false;
    }
}

enum class Opcode {
    INVALID = -1,
// NOLINTBEGIN(readability-identifier-naming)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define INST_DEF(opcode, ...) opcode,
    OPCODE_LIST(INST_DEF)

#undef INST_DEF
    // NOLINTEND(readability-identifier-naming)
    NUM_OPCODES
};

/// Convert opcode to its string representation
constexpr std::array<const char *const, static_cast<size_t>(Opcode::NUM_OPCODES)> OPCODE_NAMES = {
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define INST_DEF(opcode, ...) #opcode,
    OPCODE_LIST(INST_DEF)
#undef INST_DEF
};

constexpr const char *GetOpcodeString(Opcode opc)
{
    ASSERT(static_cast<int>(opc) < static_cast<int>(Opcode::NUM_OPCODES));
    return OPCODE_NAMES[static_cast<int>(opc)];
}

/// Instruction flags. See `instrutions.yaml` section `flags` for more information.
namespace inst_flags {
namespace internal {
enum FlagsIndex {
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define FLAG_DEF(flag) flag##_INDEX,
    FLAGS_LIST(FLAG_DEF)
#undef FLAG_DEF
        FLAGS_COUNT
};
}  // namespace internal

enum Flags : uint32_t {
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define FLAG_DEF(flag) flag = (1U << internal::flag##_INDEX),
    FLAGS_LIST(FLAG_DEF)
#undef FLAG_DEF
        FLAGS_COUNT = internal::FLAGS_COUNT,
    NONE = 0
};

inline constexpr uintptr_t GetFlagsMask(Opcode opcode)
{
#define INST_DEF(OPCODE, BASE, FLAGS) FLAGS,  // NOLINT(cppcoreguidelines-macro-usage)
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    constexpr std::array<uintptr_t, static_cast<int>(Opcode::NUM_OPCODES)> INST_FLAGS_TABLE = {OPCODE_LIST(INST_DEF)};
#undef INST_DEF
    return INST_FLAGS_TABLE[static_cast<size_t>(opcode)];
}
}  // namespace inst_flags

#ifndef NDEBUG
namespace inst_modes {
namespace internal {
enum ModeIndex {
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define MODE_DEF(mode) mode##_INDEX,
    MODES_LIST(MODE_DEF)
#undef MODE_DEF
        MODES_COUNT
};
}  // namespace internal

enum Mode : uint8_t {
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define MODE_DEF(mode) mode = (1U << internal::mode##_INDEX),
    MODES_LIST(MODE_DEF)
#undef MODE_DEF
        MODES_COUNT = internal::MODES_COUNT,
};

inline constexpr uint8_t GetModesMask(Opcode opcode)
{
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    constexpr std::array<uint8_t, static_cast<int>(Opcode::NUM_OPCODES)> INST_MODES_TABLE = {INST_MODES_LIST};
    return INST_MODES_TABLE[static_cast<size_t>(opcode)];
}
}  // namespace inst_modes
#endif

namespace internal {
inline constexpr std::array<const char *, ShiftType::INVALID_SHIFT + 1> SHIFT_TYPE_NAMES = {"LSL", "LSR", "ASR", "ROR",
                                                                                            "INVALID"};
}  // namespace internal

inline const char *GetShiftTypeStr(ShiftType type)
{
    ASSERT(type <= INVALID_SHIFT);
    return internal::SHIFT_TYPE_NAMES[type];
}

/// Describes type of the object produced by an instruction.
class ObjectTypeInfo {
public:
    using ClassType = RuntimeInterface::ClassPtr;

    constexpr ObjectTypeInfo() = default;
    ObjectTypeInfo(ClassType klass, bool is_exact)
        : class_(reinterpret_cast<uintptr_t>(klass) | static_cast<uintptr_t>(is_exact))
    {
        ASSERT((reinterpret_cast<uintptr_t>(klass) & EXACT_MASK) == 0);
    }

    bool operator==(const ObjectTypeInfo &other) const
    {
        return class_ == other.class_;
    }

    // NOLINTNEXTLINE(google-explicit-constructor)
    operator bool() const
    {
        return class_ != 0;
    }

    ClassType GetClass() const
    {
        return reinterpret_cast<ClassType>(class_ & ~EXACT_MASK);
    }

    bool IsExact() const
    {
        return (class_ & EXACT_MASK) != 0;
    }

    bool IsValid() const
    {
        return class_ > 1;
    }

    static const ObjectTypeInfo INVALID;
    static const ObjectTypeInfo UNKNOWN;

private:
    explicit constexpr ObjectTypeInfo(uintptr_t klass) : class_(klass) {}

private:
    static constexpr uintptr_t EXACT_MASK = 1;
    // Lowest bit in ClassPtr is always zero due to alignment, we set it to 1 if `klass` is the exact class
    // of the object and to 0 if it is some superclass of that class
    uintptr_t class_ = 0;
};

using VRegType = VRegInfo::VRegType;

/// Class for storing panda bytecode's virtual register
class VirtualRegister final {
public:
    using ValueType = uint16_t;

    static constexpr unsigned BITS_FOR_VREG_TYPE = MinimumBitsToStore(VRegType::COUNT);
    static constexpr unsigned BITS_FOR_VREG = (sizeof(ValueType) * BITS_PER_BYTE) - BITS_FOR_VREG_TYPE;

    static constexpr ValueType INVALID = std::numeric_limits<ValueType>::max() >> BITS_FOR_VREG_TYPE;
    // This value we marked the virtual registers, that create in bridge for SS
    static constexpr ValueType BRIDGE = INVALID - 1U;
    static constexpr ValueType MAX_NUM_VIRT_REGS = BRIDGE - 2U;

    VirtualRegister() = default;
    explicit VirtualRegister(ValueType v, VRegType type) : value_(v)
    {
        ASSERT(ValidNumVirtualReg(value_));
        VRegTypeField::Set(type, &value_);
    }

    explicit operator uint16_t() const
    {
        return value_;
    }

    ValueType Value() const
    {
        return ValueField::Get(value_);
    }

    bool IsAccumulator() const
    {
        return VRegTypeField::Get(value_) == VRegType::ACC;
    }

    bool IsEnv() const
    {
        return IsSpecialReg() && !IsAccumulator();
    }

    bool IsSpecialReg() const
    {
        return VRegTypeField::Get(value_) != VRegType::VREG;
    }

    VRegType GetVRegType() const
    {
        return static_cast<VRegType>(VRegTypeField::Get(value_));
    }

    bool IsBridge() const
    {
        return ValueField::Get(value_) == BRIDGE;
    }

    static bool ValidNumVirtualReg(uint16_t num)
    {
        return num <= INVALID;
    }

private:
    ValueType value_ {INVALID};

    using ValueField = BitField<unsigned, 0, BITS_FOR_VREG>;
    using VRegTypeField = ValueField::NextField<unsigned, BITS_FOR_VREG_TYPE>;
};

// How many bits will be used in Inst's bit fields for number of inputs.
constexpr size_t BITS_PER_INPUTS_NUM = 3;
// Maximum number of static inputs
constexpr size_t MAX_STATIC_INPUTS = (1U << BITS_PER_INPUTS_NUM) - 1;

/// Currently Input class is just a wrapper for the Inst class.
class Input final {
public:
    Input() = default;
    explicit Input(Inst *inst) : inst_(inst) {}

    Inst *GetInst()
    {
        return inst_;
    }
    const Inst *GetInst() const
    {
        return inst_;
    }

    static inline uint8_t GetPadding(Arch arch, uint32_t inputs_count)
    {
        return static_cast<uint8_t>(!Is64BitsArch(arch) && inputs_count % 2U == 1U);
    }

private:
    Inst *inst_ {nullptr};
};

inline bool operator==(const Inst *lhs, const Input &rhs)
{
    return lhs == rhs.GetInst();
}

inline bool operator==(const Input &lhs, const Inst *rhs)
{
    return lhs.GetInst() == rhs;
}

inline bool operator==(const Input &lhs, const Input &rhs)
{
    return lhs.GetInst() == rhs.GetInst();
}

inline bool operator!=(const Inst *lhs, const Input &rhs)
{
    return lhs != rhs.GetInst();
}

inline bool operator!=(const Input &lhs, const Inst *rhs)
{
    return lhs.GetInst() != rhs;
}

inline bool operator!=(const Input &lhs, const Input &rhs)
{
    return lhs.GetInst() != rhs.GetInst();
}

/**
 * User is a intrusive list node, thus it stores pointers to next and previous users.
 * Also user has properties value to determine owner instruction and corresponding index of the input.
 */
class User final {
public:
    User() = default;
    User(bool is_static, unsigned index, unsigned size)
        : properties_(IsStaticFlag::Encode(is_static) | IndexField::Encode(index) | SizeField::Encode(size) |
                      BbNumField::Encode(BbNumField::MaxValue()))
    {
        ASSERT(index < 1U << (BITS_FOR_INDEX - 1U));
        ASSERT(size < 1U << (BITS_FOR_SIZE - 1U));
    }
    ~User() = default;

    // Copy/move semantic is disabled because we use tricky pointer arithmetic based on 'this' value
    NO_COPY_SEMANTIC(User);
    NO_MOVE_SEMANTIC(User);

    Inst *GetInst();
    const Inst *GetInst() const
    {
        return const_cast<User *>(this)->GetInst();
    }

    Inst *GetInput();
    const Inst *GetInput() const;

    bool IsDynamic() const
    {
        return !IsStaticFlag::Decode(properties_);
    }
    unsigned GetIndex() const
    {
        return IndexField::Decode(properties_);
    }
    unsigned GetSize() const
    {
        return SizeField::Decode(properties_);
    }

    VirtualRegister GetVirtualRegister() const
    {
        ASSERT(IsDynamic());
        return VirtualRegister(VRegField::Decode(properties_),
                               static_cast<VRegType>(VRegTypeField::Decode(properties_)));
    }

    void SetVirtualRegister(VirtualRegister reg)
    {
        static_assert(sizeof(reg) <= sizeof(uintptr_t), "Consider passing the register by reference");
        ASSERT(IsDynamic());
        VRegField::Set(reg.Value(), &properties_);
        VRegTypeField::Set(reg.GetVRegType(), &properties_);
    }

    uint32_t GetBbNum() const
    {
        ASSERT(IsDynamic());
        return BbNumField::Decode(properties_);
    }

    void SetBbNum(uint32_t bb_num)
    {
        ASSERT(IsDynamic());
        BbNumField::Set(bb_num, &properties_);
    }

    auto GetNext() const
    {
        return next_;
    }

    auto GetPrev() const
    {
        return prev_;
    }

    void SetNext(User *next)
    {
        next_ = next;
    }

    void SetPrev(User *prev)
    {
        prev_ = prev;
    }

    void Remove()
    {
        if (prev_ != nullptr) {
            prev_->next_ = next_;
        }
        if (next_ != nullptr) {
            next_->prev_ = prev_;
        }
    }

private:
    static constexpr unsigned BITS_FOR_INDEX = 21;
    static constexpr unsigned BITS_FOR_SIZE = BITS_FOR_INDEX;
    static constexpr unsigned BITS_FOR_BB_NUM = 20;
    using IndexField = BitField<unsigned, 0, BITS_FOR_INDEX>;
    using SizeField = IndexField::NextField<unsigned, BITS_FOR_SIZE>;
    using IsStaticFlag = SizeField::NextFlag;

    using BbNumField = IsStaticFlag::NextField<uint32_t, BITS_FOR_BB_NUM>;

    using VRegField = IsStaticFlag::NextField<unsigned, VirtualRegister::BITS_FOR_VREG>;
    using VRegTypeField = VRegField::NextField<unsigned, VirtualRegister::BITS_FOR_VREG_TYPE>;

    uint64_t properties_ {0};
    User *next_ {nullptr};
    User *prev_ {nullptr};
};

/**
 * List of users. Intended for range loop.
 * @tparam T should be User or const User
 */
template <typename T>
class UserList {
    template <typename U>
    struct UserIterator {
        UserIterator() = default;
        explicit UserIterator(U *u) : user_(u) {}

        UserIterator &operator++()
        {
            user_ = user_->GetNext();
            return *this;
        }
        bool operator!=(const UserIterator &other)
        {
            return user_ != other.user_;
        }
        U &operator*()
        {
            return *user_;
        }
        U *operator->()
        {
            return user_;
        }

    private:
        U *user_ {nullptr};
    };

public:
    using Iterator = UserIterator<T>;
    using ConstIterator = UserIterator<const T>;
    using PointerType = std::conditional_t<std::is_const_v<T>, T *const *, T **>;

    explicit UserList(PointerType head) : head_(head) {}

    // NOLINTNEXTLINE(readability-identifier-naming)
    Iterator begin()
    {
        return Iterator(*head_);
    }
    // NOLINTNEXTLINE(readability-identifier-naming)
    Iterator end()
    {
        return Iterator(nullptr);
    }
    // NOLINTNEXTLINE(readability-identifier-naming)
    ConstIterator begin() const
    {
        return ConstIterator(*head_);
    }
    // NOLINTNEXTLINE(readability-identifier-naming)
    ConstIterator end() const
    {
        return ConstIterator(nullptr);
    }
    bool Empty() const
    {
        return *head_ == nullptr;
    }
    T &Front()
    {
        return **head_;
    }
    const T &Front() const
    {
        return **head_;
    }

private:
    PointerType head_ {nullptr};
};

inline bool operator==(const User &lhs, const User &rhs)
{
    return lhs.GetInst() == rhs.GetInst();
}

/**
 * Operands class for instructions with fixed inputs count.
 * Actually, this class do absolutely nothing except that we can get sizeof of it when allocating memory.
 */
template <int N>
struct Operands {
    static_assert(N < MAX_STATIC_INPUTS, "Invalid inputs number");

    std::array<User, N> users;
    std::array<Input, N> inputs;
};

/**
 * Specialized version for instructions with variable inputs count.
 * Users and inputs are stored outside of this class.
 */
class DynamicOperands {
public:
    explicit DynamicOperands(ArenaAllocator *allocator) : allocator_(allocator) {}

    User *Users()
    {
        return users_;
    }

    NO_UB_SANITIZE Input *Inputs()
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        return reinterpret_cast<Input *>(users_ + capacity_) + 1;
    }

    /// Append new input (and user accordingly)
    unsigned Append(Inst *inst);

    /// Remove input and user with index `index`.
    void Remove(unsigned index);

    /// Reallocate inputs/users storage to a new one with specified capacity.
    void Reallocate(size_t new_capacity = 0);

    /// Get instruction to which these operands belongs to.
    Inst *GetOwnerInst() const
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        return reinterpret_cast<Inst *>(const_cast<DynamicOperands *>(this) + 1);
    }

    User *GetUser(unsigned index)
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        return &users_[capacity_ - index - 1];
    }

    Input *GetInput(unsigned index)
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        return &Inputs()[index];
    }

    void SetInput(unsigned index, Input input)
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        Inputs()[index] = input;
    }

    size_t Size() const
    {
        return size_;
    }

private:
    User *users_ {nullptr};
    size_t size_ {0};
    size_t capacity_ {0};
    ArenaAllocator *allocator_ {nullptr};
};

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define DECLARE_INST(TYPE) void Accept(GraphVisitor *v) override

/// Base class for all instructions, should not be instantiated directly
class InstBase {
    NO_COPY_SEMANTIC(InstBase);
    NO_MOVE_SEMANTIC(InstBase);

public:
    virtual ~InstBase() = default;

public:
    virtual void Accept(GraphVisitor *v) = 0;

    ALWAYS_INLINE void operator delete([[maybe_unused]] void *unused, [[maybe_unused]] size_t size)
    {
        UNREACHABLE();
    }
    ALWAYS_INLINE void *operator new([[maybe_unused]] size_t size, void *ptr) noexcept
    {
        return ptr;
    }
    ALWAYS_INLINE void operator delete([[maybe_unused]] void *unused1, [[maybe_unused]] void *unused2) noexcept {}

    void *operator new([[maybe_unused]] size_t size) = delete;

protected:
    InstBase() = default;
};

/// Base instruction class
class Inst : public MarkerSet, public InstBase {
public:
    DECLARE_INST(Inst);

public:
    /**
     * Create new instruction. All instructions must be created with this method.
     * It allocates additional space before Inst object for def-use structures.
     *
     * @tparam InstType - concrete type of instruction, shall be derived from Inst
     * @tparam Args - constructor arguments types
     * @param allocator - allocator for memory allocating
     * @param args - constructor arguments
     * @return - new instruction
     */
    template <typename InstType, typename... Args>
    [[nodiscard]] static InstType *New(ArenaAllocator *allocator, Args &&...args);

    // NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define INST_DEF(opcode, base, ...) inline const base *CastTo##opcode() const;
    OPCODE_LIST(INST_DEF)
#undef INST_DEF

    // NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define INST_DEF(opcode, base, ...) inline base *CastTo##opcode();
    OPCODE_LIST(INST_DEF)
#undef INST_DEF

    // Methods for instruction chaining inside basic blocks.
    Inst *GetNext()
    {
        return next_;
    }
    const Inst *GetNext() const
    {
        return next_;
    }
    Inst *GetPrev()
    {
        return prev_;
    }
    const Inst *GetPrev() const
    {
        return prev_;
    }
    void SetNext(Inst *next)
    {
        next_ = next;
    }
    void SetPrev(Inst *prev)
    {
        prev_ = prev;
    }

    // Id accessors
    auto GetId() const
    {
        return id_;
    }
    void SetId(int id)
    {
        id_ = id;
    }

    auto GetLinearNumber() const
    {
        return linear_number_;
    }
    void SetLinearNumber(LinearNumber number)
    {
        linear_number_ = number;
    }

    auto GetCloneNumber() const
    {
        return clone_number_;
    }
    void SetCloneNumber(int32_t number)
    {
        clone_number_ = number;
    }

    // Opcode accessors
    Opcode GetOpcode() const
    {
        return opcode_;
    }
    void SetOpcode(Opcode opcode)
    {
        opcode_ = opcode;
        SetField<FieldFlags>(inst_flags::GetFlagsMask(opcode));
    }
    const char *GetOpcodeStr() const
    {
        return GetOpcodeString(GetOpcode());
    }

    // Bytecode PC accessors
    uint32_t GetPc() const
    {
        return pc_;
    }
    void SetPc(uint32_t pc)
    {
        pc_ = pc;
    }

    // Type accessors
    DataType::Type GetType() const
    {
        return FieldType::Get(bit_fields_);
    }
    void SetType(DataType::Type type)
    {
        FieldType::Set(type, &bit_fields_);
    }
    bool HasType() const
    {
        return GetType() != DataType::Type::NO_TYPE;
    }

    virtual AnyBaseType GetAnyType() const
    {
        return AnyBaseType::UNDEFINED_TYPE;
    }

    // Parent basic block accessors
    BasicBlock *GetBasicBlock()
    {
        return bb_;
    }
    const BasicBlock *GetBasicBlock() const
    {
        return bb_;
    }
    void SetBasicBlock(BasicBlock *bb)
    {
        bb_ = bb;
    }

    // Instruction properties getters
    bool IsControlFlow() const
    {
        return GetFlag(inst_flags::CF);
    }
    bool IsVirtualLaunchCall() const
    {
        return GetOpcode() == Opcode::CallLaunchVirtual || GetOpcode() == Opcode::CallResolvedLaunchVirtual;
    }
    bool IsVirtualCall() const
    {
        return GetOpcode() == Opcode::CallVirtual || GetOpcode() == Opcode::CallResolvedVirtual ||
               IsVirtualLaunchCall();
    }
    bool IsStaticLaunchCall() const
    {
        return GetOpcode() == Opcode::CallLaunchStatic || GetOpcode() == Opcode::CallResolvedLaunchStatic;
    }
    bool IsStaticCall() const
    {
        return GetOpcode() == Opcode::CallStatic || GetOpcode() == Opcode::CallResolvedStatic || IsStaticLaunchCall();
    }
    bool IsMethodResolver() const
    {
        return opcode_ == Opcode::ResolveVirtual || opcode_ == Opcode::ResolveStatic;
    }
    bool IsFieldResolver() const
    {
        return opcode_ == Opcode::ResolveObjectField || opcode_ == Opcode::ResolveObjectFieldStatic;
    }
    bool IsResolver() const
    {
        return IsFieldResolver() || IsMethodResolver();
    }
    bool IsInitObject() const
    {
        return GetOpcode() == Opcode::InitObject;
    }
    bool IsMultiArray() const
    {
        return GetOpcode() == Opcode::MultiArray;
    }
    bool IsDynamicCall() const
    {
        return GetOpcode() == Opcode::CallDynamic;
    }
    bool IsLaunchCall() const
    {
        return IsStaticLaunchCall() || IsVirtualLaunchCall();
    }
    bool IsIndirectCall() const
    {
        return GetOpcode() == Opcode::CallIndirect;
    }
    bool IsIntrinsic() const
    {
        /* Opcode::Builtin is left for backward compatibility, the compiler
         * itself should never generate an instruction with such an opcode */
        return GetOpcode() == Opcode::Intrinsic || GetOpcode() == Opcode::Builtin;
    }

    /* IsBuiltin actual meaning would be "it MAY be inlined by the CG"
     * however, since we do not make guarantees about whether it will
     * actually be inlined nor the safety of the intrinsic itself, just
     * checking the instruction flags to see if it is suitable for any
     * particular optimization seems to be a better approach
     */
    static bool IsBuiltin()
    {
        return false;
    }

    bool IsCall() const
    {
        return GetFlag(inst_flags::CALL);
    }

    bool IsSpillFill() const
    {
        return GetOpcode() == Opcode::SpillFill;
    }

    bool IsNullCheck() const
    {
        return GetOpcode() == Opcode::NullCheck;
    }

    bool IsNullPtr() const
    {
        return GetOpcode() == Opcode::NullPtr;
    }

    bool IsReturn() const
    {
        return GetOpcode() == Opcode::Return || GetOpcode() == Opcode::ReturnI || GetOpcode() == Opcode::ReturnVoid;
    }

    bool IsUnresolved() const
    {
        switch (GetOpcode()) {
            case Opcode::UnresolvedLoadAndInitClass:
            case Opcode::UnresolvedLoadType:
            case Opcode::UnresolvedStoreStatic:
                return true;
            default:
                return false;
        }
    }
    bool IsLoad() const
    {
        return GetFlag(inst_flags::LOAD);
    }
    bool IsStore() const
    {
        return GetFlag(inst_flags::STORE);
    }
    bool IsAccRead() const;
    bool IsAccWrite() const;
    bool IsMemory() const
    {
        return IsLoad() || IsStore();
    }
    bool CanThrow() const
    {
        return GetFlag(inst_flags::CAN_THROW);
    }
    bool IsCheck() const
    {
        return GetFlag(inst_flags::IS_CHECK);
    }
    bool RequireState() const
    {
        return GetFlag(inst_flags::REQUIRE_STATE);
    }
    // Returns true if the instruction not removable in DCE
    bool IsNotRemovable() const
    {
        return GetFlag(inst_flags::NO_DCE);
    }

    // Returns true if the instruction doesn't have destination register
    bool NoDest() const
    {
        return GetFlag(inst_flags::PSEUDO_DST) || GetFlag(inst_flags::NO_DST) || GetType() == DataType::VOID;
    }

    bool HasPseudoDestination() const
    {
        return GetFlag(inst_flags::PSEUDO_DST);
    }

    bool HasImplicitRuntimeCall() const
    {
        return GetFlag(inst_flags::IMPLICIT_RUNTIME_CALL);
    }

    bool CanDeoptimize() const
    {
        return GetFlag(inst_flags::CAN_DEOPTIMIZE);
    }

    bool RequireTmpReg() const
    {
        return GetFlag(inst_flags::REQUIRE_TMP);
    }

    // Returns true if the instruction is low-level
    bool IsLowLevel() const
    {
        return GetFlag(inst_flags::LOW_LEVEL);
    }

    // Returns true if the instruction not hoistable
    bool IsNotHoistable() const
    {
        return GetFlag(inst_flags::NO_HOIST);
    }

    // Returns true Cse can't be applied to the instruction
    bool IsNotCseApplicable() const
    {
        return GetFlag(inst_flags::NO_CSE);
    }

    // Returns true if the instruction is a barrier
    virtual bool IsBarrier() const
    {
        return GetFlag(inst_flags::BARRIER);
    }

    // Returns true if opcode can not be moved throught runtime calls (REFERENCE type only)
    bool IsRefSpecial() const
    {
        bool result = GetFlag(inst_flags::REF_SPECIAL);
        ASSERT(!result || IsReferenceOrAny());
        return result;
    }

    // Returns true if the instruction is a commutative
    bool IsCommutative() const
    {
        return GetFlag(inst_flags::COMMUTATIVE);
    }

    // Returns true if the instruction allocates a new object on the heap
    bool IsAllocation() const
    {
        return GetFlag(inst_flags::ALLOC);
    }

    // Returns true if the instruction can be used in if-conversion
    bool IsIfConvertable() const
    {
        return GetFlag(inst_flags::IFCVT);
    }

    virtual bool IsRuntimeCall() const
    {
        return GetFlag(inst_flags::RUNTIME_CALL);
    }

    virtual bool IsPropagateLiveness() const;

    // Returns true if the instruction doesn't have side effects(call runtime, throw e.t.c.)
    virtual bool IsSafeInst() const
    {
        return false;
    }

    virtual bool IsBinaryInst() const
    {
        return false;
    }

    virtual bool IsBinaryImmInst() const
    {
        return false;
    }

    bool RequireRegMap() const;

    ObjectTypeInfo GetObjectTypeInfo() const
    {
        return object_type_info_;
    }

    bool HasObjectTypeInfo() const
    {
        return object_type_info_.IsValid();
    }

    void SetObjectTypeInfo(ObjectTypeInfo o)
    {
        object_type_info_ = o;
    }

    Inst *GetDataFlowInput(int index) const
    {
        return GetDataFlowInput(GetInput(index).GetInst());
    }
    static Inst *GetDataFlowInput(Inst *input_inst);

    bool IsPrecedingInSameBlock(const Inst *other) const;

    bool IsDominate(const Inst *other) const;

    bool InSameBlockOrDominate(const Inst *other) const;

    bool IsAdd() const
    {
        return GetOpcode() == Opcode::Add || GetOpcode() == Opcode::AddOverflowCheck;
    }

    bool IsSub() const
    {
        return GetOpcode() == Opcode::Sub || GetOpcode() == Opcode::SubOverflowCheck;
    }

    bool IsAddSub() const
    {
        return IsAdd() || IsSub();
    }

    const SaveStateInst *GetSaveState() const
    {
        return const_cast<Inst *>(this)->GetSaveState();
    }

    SaveStateInst *GetSaveState()
    {
        if (!RequireState()) {
            return nullptr;
        }
        if (GetInputsCount() == 0) {
            return nullptr;
        }
        auto ss = GetInput(GetInputsCount() - 1).GetInst();
        if (ss->GetOpcode() == Opcode::SaveStateDeoptimize) {
            return ss->CastToSaveStateDeoptimize();
        }
        if (ss->GetOpcode() != Opcode::SaveState) {
            return nullptr;
        }

        return ss->CastToSaveState();
    }

    void SetSaveState(Inst *inst)
    {
        ASSERT(RequireState());
        SetInput(GetInputsCount() - 1, inst);
    }

    virtual uint32_t GetInliningDepth() const;

    bool IsZeroRegInst() const;

    bool IsReferenceOrAny() const;
    bool IsMovableObject() const;

    /// Return instruction clone
    virtual Inst *Clone(const Graph *target_graph) const;

    uintptr_t GetFlagsMask() const
    {
        return GetField<FieldFlags>();
    }

    bool GetFlag(inst_flags::Flags flag) const
    {
        return (GetFlagsMask() & flag) != 0;
    }

    void SetFlag(inst_flags::Flags flag)
    {
        SetField<FieldFlags>(GetFlagsMask() | flag);
    }

    void ClearFlag(inst_flags::Flags flag)
    {
        SetField<FieldFlags>(GetFlagsMask() & ~static_cast<uintptr_t>(flag));
    }

#ifndef NDEBUG
    uint8_t GetModesMask() const
    {
        return inst_modes::GetModesMask(opcode_);
    }

    bool SupportsMode(inst_modes::Mode mode) const
    {
        return (GetModesMask() & mode) != 0;
    }
#endif

    void SetTerminator()
    {
        SetFlag(inst_flags::Flags::TERMINATOR);
    }

    bool IsTerminator() const
    {
        return GetFlag(inst_flags::TERMINATOR);
    }

    void InsertBefore(Inst *inst);
    void InsertAfter(Inst *inst);

    /// Return true if instruction has dynamic operands storage.
    bool IsOperandsDynamic() const
    {
        return GetField<InputsCount>() == MAX_STATIC_INPUTS;
    }

    /**
     * Add user to the instruction.
     * @param user - pointer to User object
     */
    void AddUser(User *user)
    {
        ASSERT(user && user->GetInst());
        user->SetNext(first_user_);
        user->SetPrev(nullptr);
        if (first_user_ != nullptr) {
            ASSERT(first_user_->GetPrev() == nullptr);
            first_user_->SetPrev(user);
        }
        first_user_ = user;
    }

    /**
     * Remove instruction from users.
     * @param user - pointer to User object
     */
    void RemoveUser(User *user)
    {
        ASSERT(user);
        ASSERT(HasUsers());
        if (user == first_user_) {
            first_user_ = user->GetNext();
        }
        user->Remove();
    }

    /**
     * Set input instruction in specified index.
     * Old input will be removed.
     * @param index - index of input to be set
     * @param inst - new input instruction TODO sherstennikov: currently it can be nullptr, is it correct?
     */
    void SetInput(unsigned index, Inst *inst)
    {
        CHECK_LT(index, GetInputsCount());
        auto &input = GetInputs()[index];
        auto user = GetUser(index);
        if (input.GetInst() != nullptr && input.GetInst()->HasUsers()) {
            input.GetInst()->RemoveUser(user);
        }
        if (inst != nullptr) {
            inst->AddUser(user);
        }
        input = Input(inst);
    }

    /**
     * Replace all inputs that points to specified instruction by new one.
     * @param old_input - instruction that should be replaced
     * @param new_input - new input instruction
     */
    void ReplaceInput(Inst *old_input, Inst *new_input)
    {
        unsigned index = 0;
        for (auto input : GetInputs()) {
            if (input.GetInst() == old_input) {
                SetInput(index, new_input);
            }
            index++;
        }
    }

    /**
     * Replace inputs that point to this instruction by given instruction.
     * @param inst - new input instruction
     */
    void ReplaceUsers(Inst *inst)
    {
        ASSERT(inst != this);
        ASSERT(inst != nullptr);
        for (auto it = GetUsers().begin(); it != GetUsers().end(); it = GetUsers().begin()) {
            it->GetInst()->SetInput(it->GetIndex(), inst);
        }
    }

    /**
     * Swap first 2 operands of the instruction.
     * NB! Don't swap inputs while iterating over instruction's users:
     * for (auto user : instruction.GetUsers()) {
     *     // Don't do this!
     *     user.GetInst()->SwapInputs();
     * }
     */
    void SwapInputs()
    {
        ASSERT(GetInputsCount() >= 2U);
        auto input0 = GetInput(0).GetInst();
        auto input1 = GetInput(1).GetInst();
        SetInput(0, input1);
        SetInput(1, input0);
    }

    /**
     * Append input instruction.
     * Available only for variadic inputs instructions, such as PHI.
     * @param input - input instruction
     * @return index in inputs container where new input is placed
     */
    unsigned AppendInput(Inst *input)
    {
        ASSERT(input != nullptr);
        ASSERT(IsOperandsDynamic());
        DynamicOperands *operands = GetDynamicOperands();
        return operands->Append(input);
    }

    unsigned AppendInput(Input input)
    {
        static_assert(sizeof(Input) <= sizeof(uintptr_t));  // Input become larger, so pass it by reference then
        return AppendInput(input.GetInst());
    }

    /**
     * Remove input from inputs container
     * Available only for variadic inputs instructions, such as PHI.
     * @param index - index of input in inputs container
     */
    virtual void RemoveInput(unsigned index)
    {
        ASSERT(IsOperandsDynamic());
        DynamicOperands *operands = GetDynamicOperands();
        ASSERT(index < operands->Size());
        operands->Remove(index);
    }

    /// Remove all inputs
    void RemoveInputs()
    {
        if (UNLIKELY(IsOperandsDynamic())) {
            for (auto inputs_count = GetInputsCount(); inputs_count != 0; --inputs_count) {
                RemoveInput(inputs_count - 1);
            }
        } else {
            for (size_t i = 0; i < GetInputsCount(); ++i) {
                SetInput(i, nullptr);
            }
        }
    }

    /// Remove all users
    template <bool WITH_INPUTS = false>
    void RemoveUsers()
    {
        auto users = GetUsers();
        while (!users.Empty()) {
            // NOLINTNEXTLINE(readability-braces-around-statements, bugprone-suspicious-semicolon)
            if constexpr (WITH_INPUTS) {
                auto &user = users.Front();
                user.GetInst()->RemoveInput(user.GetIndex());
                // NOLINTNEXTLINE(readability-misleading-indentation)
            } else {
                RemoveUser(&users.Front());
            }
        }
    }

    /**
     * Get input by index
     * @param index - index of input
     * @return input instruction
     */
    Input GetInput(unsigned index)
    {
        ASSERT(index < GetInputsCount());
        return GetInputs()[index];
    }

    Input GetInput(unsigned index) const
    {
        ASSERT(index < GetInputsCount());
        return GetInputs()[index];
    }

    Span<Input> GetInputs()
    {
        if (UNLIKELY(IsOperandsDynamic())) {
            DynamicOperands *operands = GetDynamicOperands();
            return Span<Input>(operands->Inputs(), operands->Size());
        }

        auto inputs_count {GetField<InputsCount>()};
        return Span<Input>(
            reinterpret_cast<Input *>(reinterpret_cast<uintptr_t>(this) -
                                      (inputs_count + Input::GetPadding(RUNTIME_ARCH, inputs_count)) * sizeof(Input)),
            inputs_count);
    }
    Span<const Input> GetInputs() const
    {
        return Span<const Input>(const_cast<Inst *>(this)->GetInputs());
    }

    virtual DataType::Type GetInputType([[maybe_unused]] size_t index) const
    {
        ASSERT(index < GetInputsCount());
        if (GetInput(index).GetInst()->IsSaveState()) {
            return DataType::NO_TYPE;
        }
        return GetType();
    }

    UserList<User> GetUsers()
    {
        return UserList<User>(&first_user_);
    }
    UserList<const User> GetUsers() const
    {
        return UserList<const User>(&first_user_);
    }

    size_t GetInputsCount() const
    {
        if (UNLIKELY(IsOperandsDynamic())) {
            return GetDynamicOperands()->Size();
        }
        return GetInputs().Size();
    }

    bool HasUsers() const
    {
        return first_user_ != nullptr;
    };

    bool HasSingleUser() const
    {
        return first_user_ != nullptr && first_user_->GetNext() == nullptr;
    }

    /// Reserve space in dataflow storage for specified inputs count
    void ReserveInputs(size_t capacity);

    virtual void SetLocation([[maybe_unused]] size_t index, [[maybe_unused]] Location location) {}

    virtual Location GetLocation([[maybe_unused]] size_t index) const
    {
        return Location::RequireRegister();
    }

    virtual Location GetDstLocation() const
    {
        return Location::MakeRegister(GetDstReg(), GetType());
    }

    virtual Location GetDstLocation([[maybe_unused]] unsigned index) const
    {
        ASSERT(index == 0);
        return GetDstLocation();
    }

    virtual void SetTmpLocation([[maybe_unused]] Location location) {}

    virtual Location GetTmpLocation() const
    {
        return Location::Invalid();
    }

    virtual bool CanBeNull() const
    {
        ASSERT_PRINT(GetType() == DataType::Type::REFERENCE, "CanBeNull only applies to reference types");
        return true;
    }

    virtual uint32_t Latency() const
    {
        return OPTIONS.GetCompilerSchedLatency();
    }

    template <typename Accessor>
    typename Accessor::ValueType GetField() const
    {
        return Accessor::Get(bit_fields_);
    }

    template <typename Accessor>
    void SetField(typename Accessor::ValueType value)
    {
        Accessor::Set(value, &bit_fields_);
    }

    uint64_t GetAllFields() const
    {
        return bit_fields_;
    }

    bool IsPhi() const
    {
        return opcode_ == Opcode::Phi;
    }

    bool IsCatchPhi() const
    {
        return opcode_ == Opcode::CatchPhi;
    }

    bool IsConst() const
    {
        return opcode_ == Opcode::Constant;
    }

    bool IsParameter() const
    {
        return opcode_ == Opcode::Parameter;
    }

    virtual bool IsBoolConst() const
    {
        return false;
    }

    bool IsSaveState() const
    {
        return opcode_ == Opcode::SaveState || opcode_ == Opcode::SafePoint || opcode_ == Opcode::SaveStateOsr ||
               opcode_ == Opcode::SaveStateDeoptimize;
    }

    bool IsClassInst() const
    {
        return opcode_ == Opcode::InitClass || opcode_ == Opcode::LoadClass || opcode_ == Opcode::LoadAndInitClass ||
               opcode_ == Opcode::UnresolvedLoadAndInitClass;
    }

    virtual size_t GetHashCode() const
    {
        // TODO (Aleksandr Popov) calculate hash code
        return 0;
    }

    virtual void SetVnObject([[maybe_unused]] VnObject *vn_obj) {}

    Register GetDstReg() const
    {
        return dst_reg_;
    }

    void SetDstReg(Register reg)
    {
        dst_reg_ = reg;
    }

    uint32_t GetVN() const
    {
        return vn_;
    }

    void SetVN(uint32_t vn)
    {
        vn_ = vn;
    }
    void Dump(std::ostream *out, bool new_line = true) const;
    virtual bool DumpInputs(std::ostream * /* out */) const;
    virtual void DumpOpcode(std::ostream * /* out */) const;

    virtual void SetDstReg([[maybe_unused]] unsigned index, Register reg)
    {
        ASSERT(index == 0);
        SetDstReg(reg);
    }

    virtual Register GetDstReg([[maybe_unused]] unsigned index) const
    {
        ASSERT(index == 0);
        return GetDstReg();
    }

    virtual size_t GetDstCount() const
    {
        return 1;
    }

    virtual uint32_t GetSrcRegIndex() const
    {
        return 0;
    }

    virtual void SetSrcReg([[maybe_unused]] unsigned index, [[maybe_unused]] Register reg) {}

    virtual Register GetSrcReg([[maybe_unused]] unsigned index) const
    {
        return INVALID_REG;
    }

    User *GetFirstUser() const
    {
        return first_user_;
    }

#ifdef PANDA_COMPILER_DEBUG_INFO
    InstDebugInfo *GetDebugInfo() const
    {
        return debug_info_;
    }

    void SetDebugInfo(InstDebugInfo *info)
    {
        debug_info_ = info;
    }

    RuntimeInterface::MethodPtr GetCurrentMethod() const
    {
        return current_method_;
    }

    void SetCurrentMethod(RuntimeInterface::MethodPtr current_method)
    {
        current_method_ = current_method;
    }
#endif

protected:
    using InstBase::InstBase;
    static constexpr int INPUT_COUNT = 0;

    Inst() = default;

    explicit Inst(Opcode opcode) : Inst(opcode, DataType::Type::NO_TYPE, INVALID_PC) {}

    explicit Inst(Opcode opcode, DataType::Type type, uint32_t pc) : pc_(pc), opcode_(opcode)
    {
        bit_fields_ = inst_flags::GetFlagsMask(opcode);
        SetField<FieldType>(type);
    }

protected:
    using FieldFlags = BitField<uint32_t, 0, MinimumBitsToStore(1U << inst_flags::FLAGS_COUNT)>;
    using FieldType = FieldFlags::NextField<DataType::Type, MinimumBitsToStore(DataType::LAST)>;
    using InputsCount = FieldType::NextField<uint32_t, BITS_PER_INPUTS_NUM>;
    using LastField = InputsCount;

    DynamicOperands *GetDynamicOperands() const
    {
        return reinterpret_cast<DynamicOperands *>(reinterpret_cast<uintptr_t>(this) - sizeof(DynamicOperands));
    }

private:
    User *GetUser(unsigned index)
    {
        if (UNLIKELY(IsOperandsDynamic())) {
            return GetDynamicOperands()->GetUser(index);
        }
        auto inputs_count {GetField<InputsCount>()};
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        return reinterpret_cast<User *>(reinterpret_cast<Input *>(this) -
                                        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                                        (inputs_count + Input::GetPadding(RUNTIME_ARCH, inputs_count))) -
               // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
               index - 1;
    }

    size_t OperandsStorageSize() const
    {
        if (UNLIKELY(IsOperandsDynamic())) {
            return sizeof(DynamicOperands);
        }

        auto inputs_count {GetField<InputsCount>()};
        return inputs_count * (sizeof(Input) + sizeof(User)) +
               Input::GetPadding(RUNTIME_ARCH, inputs_count) * sizeof(Input);
    }

private:
    /// Basic block this instruction belongs to
    BasicBlock *bb_ {nullptr};

#ifdef PANDA_COMPILER_DEBUG_INFO
    InstDebugInfo *debug_info_ {nullptr};
    RuntimeInterface::MethodPtr current_method_ {nullptr};
#endif

    /// Next instruction within basic block
    Inst *next_ {nullptr};

    /// Previous instruction within basic block
    Inst *prev_ {nullptr};

    /// First user in users chain
    User *first_user_ {nullptr};

    /// This value hold properties of the instruction. It accessed via BitField types(f.e. FieldType).
    uint64_t bit_fields_ {0};

    /// Unique id of instruction
    uint32_t id_ {INVALID_ID};

    /// Unique id of instruction
    uint32_t vn_ {INVALID_VN};

    /// Bytecode pc
    uint32_t pc_ {INVALID_PC};

    /// Number used in cloning
    uint32_t clone_number_ {0};

    /// Instruction number getting while visiting graph
    LinearNumber linear_number_ {INVALID_LINEAR_NUM};

    ObjectTypeInfo object_type_info_ {};

    /// Opcode, see opcodes.def
    Opcode opcode_ {Opcode::INVALID};

    // Destination register type - defined in FieldType
    Register dst_reg_ {INVALID_REG};
};

/**
 * Proxy class that injects new field - type of the source operands - into property field of the instruction.
 * Should be used when instruction has sources of the same type and type of the instruction is not match to type of
 * sources. Examples: Cmp, Compare
 * @tparam T Base instruction class after which this mixin is injected
 */
template <typename T>
class InstWithOperandsType : public T {
public:
    using T::T;

    void SetOperandsType(DataType::Type type)
    {
        T::template SetField<FieldOperandsType>(type);
    }
    virtual DataType::Type GetOperandsType() const
    {
        return T::template GetField<FieldOperandsType>();
    }

protected:
    using FieldOperandsType =
        typename T::LastField::template NextField<DataType::Type, MinimumBitsToStore(DataType::LAST)>;
    using LastField = FieldOperandsType;
};

/**
 * Mixin for NeedBarrier flag.
 * @tparam T Base instruction class after which this mixin is injected
 */
template <typename T>
class NeedBarrierMixin : public T {
public:
    using T::T;

    void SetNeedBarrier(bool v)
    {
        T::template SetField<NeedBarrierFlag>(v);
    }
    bool GetNeedBarrier() const
    {
        return T::template GetField<NeedBarrierFlag>();
    }

protected:
    using NeedBarrierFlag = typename T::LastField::NextFlag;
    using LastField = NeedBarrierFlag;
};

enum class DynObjectAccessType {
    UNKNOWN = 0,  // corresponds by value semantic
    BY_NAME = 1,
    BY_INDEX = 2,
    LAST = BY_INDEX
};

enum class DynObjectAccessMode {
    UNKNOWN = 0,
    DICTIONARY = 1,
    ARRAY = 2,
    LAST = ARRAY,
};

/**
 * Mixin for dynamic object access properties.
 * @tparam T Base instruction class after which this mixin is injected
 */
template <typename T>
class DynObjectAccessMixin : public T {
public:
    using T::T;
    using Type = DynObjectAccessType;
    using Mode = DynObjectAccessMode;

    void SetAccessType(Type type)
    {
        T::template SetField<AccessType>(type);
    }

    Type GetAccessType() const
    {
        return T::template GetField<AccessType>();
    }

    void SetAccessMode(Mode mode)
    {
        T::template SetField<AccessMode>(mode);
    }

    Mode GetAccessMode() const
    {
        return T::template GetField<AccessMode>();
    }

protected:
    using AccessType = typename T::LastField::template NextField<Type, MinimumBitsToStore(Type::LAST)>;
    using AccessMode = typename AccessType::template NextField<Mode, MinimumBitsToStore(Mode::LAST)>;
    using LastField = AccessMode;
};

enum ObjectType {
    MEM_OBJECT = 0,
    MEM_STATIC,
    MEM_DYN_GLOBAL,
    MEM_DYN_INLINED,
    MEM_DYN_PROPS,
    MEM_DYN_ELEMENTS,
    MEM_DYN_CLASS,
    MEM_DYN_HCLASS,
    MEM_DYN_METHOD,
    MEM_DYN_PROTO_HOLDER,
    MEM_DYN_PROTO_CELL,
    MEM_DYN_CHANGE_FIELD,
    MEM_DYN_ARRAY_LENGTH,
    LAST = MEM_DYN_ARRAY_LENGTH
};

inline const char *ObjectTypeToString(ObjectType obj_type)
{
    static constexpr auto COUNT = static_cast<uint8_t>(ObjectType::LAST) + 1;
    static constexpr std::array<const char *, COUNT> OBJ_TYPE_NAMES = {
        "Object", "Static", "GlobalVar",        "Dynamic inlined", "Properties",     "Elements", "Class",
        "Hclass", "Method", "Prototype holder", "Prototype cell",  "IsChangeFieald", "Length"};
    auto idx = static_cast<uint8_t>(obj_type);
    ASSERT(idx <= LAST);
    return OBJ_TYPE_NAMES[idx];
}

/// This mixin aims to implement type id accessors.
class TypeIdMixin {
public:
    static constexpr uint32_t INVALID_ID = std::numeric_limits<uint32_t>::max();
    static constexpr uint32_t MEM_PROMISE_CLASS_ID = RuntimeInterface::MEM_PROMISE_CLASS_ID;
    static constexpr uint32_t MEM_DYN_CLASS_ID = MEM_PROMISE_CLASS_ID - ObjectType::MEM_DYN_CLASS;
    static constexpr uint32_t MEM_DYN_HCLASS_ID = MEM_PROMISE_CLASS_ID - ObjectType::MEM_DYN_HCLASS;
    static constexpr uint32_t MEM_DYN_METHOD_ID = MEM_PROMISE_CLASS_ID - ObjectType::MEM_DYN_METHOD;
    static constexpr uint32_t MEM_DYN_PROTO_HOLDER_ID = MEM_PROMISE_CLASS_ID - ObjectType::MEM_DYN_PROTO_HOLDER;
    static constexpr uint32_t MEM_DYN_PROTO_CELL_ID = MEM_PROMISE_CLASS_ID - ObjectType::MEM_DYN_PROTO_CELL;
    static constexpr uint32_t MEM_DYN_CHANGE_FIELD_ID = MEM_PROMISE_CLASS_ID - ObjectType::MEM_DYN_CHANGE_FIELD;
    static constexpr uint32_t MEM_DYN_GLOBAL_ID = MEM_PROMISE_CLASS_ID - ObjectType::MEM_DYN_GLOBAL;
    static constexpr uint32_t MEM_DYN_PROPS_ID = MEM_PROMISE_CLASS_ID - ObjectType::MEM_DYN_PROPS;
    static constexpr uint32_t MEM_DYN_ELEMENTS_ID = MEM_PROMISE_CLASS_ID - ObjectType::MEM_DYN_ELEMENTS;
    static constexpr uint32_t MEM_DYN_ARRAY_LENGTH_ID = MEM_PROMISE_CLASS_ID - ObjectType::MEM_DYN_ARRAY_LENGTH;

    TypeIdMixin(uint32_t type_id, RuntimeInterface::MethodPtr method) : type_id_(type_id), method_(method) {}

    TypeIdMixin() = default;
    NO_COPY_SEMANTIC(TypeIdMixin);
    NO_MOVE_SEMANTIC(TypeIdMixin);
    virtual ~TypeIdMixin() = default;

    void SetTypeId(uint32_t id)
    {
        type_id_ = id;
    }

    auto GetTypeId() const
    {
        return type_id_;
    }

    void SetMethod(RuntimeInterface::MethodPtr method)
    {
        method_ = method;
    }
    auto GetMethod() const
    {
        return method_;
    }

private:
    uint32_t type_id_ {0};
    // The pointer to the method in which this instruction is executed(inlined method)
    RuntimeInterface::MethodPtr method_ {nullptr};
};

/// This mixin aims to implement type of klass.
template <typename T>
class ClassTypeMixin : public T {
public:
    using T::T;

    void SetClassType(ClassType class_type)
    {
        T::template SetField<ClassTypeField>(class_type);
    }

    ClassType GetClassType() const
    {
        return T::template GetField<ClassTypeField>();
    }

protected:
    using ClassTypeField = typename T::LastField::template NextField<ClassType, MinimumBitsToStore(ClassType::COUNT)>;
    using LastField = ClassTypeField;
};

/// Mixin to check if null check inside CheckCast and IsInstance can be omitted.
template <typename T>
class OmitNullCheckMixin : public T {
public:
    using T::T;

    void SetOmitNullCheck(bool omit_null_check)
    {
        T::template SetField<OmitNullCheckFlag>(omit_null_check);
    }

    bool GetOmitNullCheck() const
    {
        return T::template GetField<OmitNullCheckFlag>();
    }

protected:
    using OmitNullCheckFlag = typename T::LastField::NextFlag;
    using LastField = OmitNullCheckFlag;
};

template <typename T>
class ScaleMixin : public T {
public:
    using T::T;

    void SetScale(uint32_t scale)
    {
        ASSERT(scale <= MAX_SCALE);
        T::template SetField<ScaleField>(scale);
    }

    uint32_t GetScale() const
    {
        return T::template GetField<ScaleField>();
    }

protected:
    using ScaleField = typename T::LastField::template NextField<uint32_t, MinimumBitsToStore(MAX_SCALE)>;
    using LastField = ScaleField;
};

template <typename T>
class ObjectTypeMixin : public T {
public:
    using T::T;

    void SetObjectType(ObjectType type)
    {
        T::template SetField<ObjectTypeField>(type);
    }

    ObjectType GetObjectType() const
    {
        return T::template GetField<ObjectTypeField>();
    }

protected:
    using ObjectTypeField = typename T::LastField::template NextField<ObjectType, MinimumBitsToStore(ObjectType::LAST)>;
    using LastField = ObjectTypeField;
};

/// This mixin aims to implement field accessors.
class FieldMixin {
public:
    explicit FieldMixin(RuntimeInterface::FieldPtr field) : field_(field) {}

    FieldMixin() = default;
    NO_COPY_SEMANTIC(FieldMixin);
    NO_MOVE_SEMANTIC(FieldMixin);
    virtual ~FieldMixin() = default;

    void SetObjField(RuntimeInterface::FieldPtr field)
    {
        field_ = field;
    }
    auto GetObjField() const
    {
        return field_;
    }

private:
    RuntimeInterface::FieldPtr field_ {nullptr};
};

/// This mixin aims to implement volatile accessors.
template <typename T>
class VolatileMixin : public T {
public:
    using T::T;

    void SetVolatile(bool is_volatile)
    {
        T::template SetField<IsVolatileFlag>(is_volatile);
    }
    bool GetVolatile() const
    {
        return T::template GetField<IsVolatileFlag>();
    }

protected:
    using IsVolatileFlag = typename T::LastField::NextFlag;
    using LastField = IsVolatileFlag;
};
/// Mixin for Inlined calls/returns.
template <typename T>
class InlinedInstMixin : public T {
public:
    using T::T;

    void SetInlined(bool v)
    {
        T::template SetField<IsInlinedFlag>(v);
    }
    bool IsInlined() const
    {
        return T::template GetField<IsInlinedFlag>();
    }

protected:
    using IsInlinedFlag = typename T::LastField::NextFlag;
    using LastField = IsInlinedFlag;
};

/// Mixin for Array/String instruction
template <typename T>
class ArrayInstMixin : public T {
public:
    using T::T;

    void SetIsArray(bool v)
    {
        T::template SetField<IsStringFlag>(!v);
    }

    void SetIsString(bool v)
    {
        T::template SetField<IsStringFlag>(v);
    }

    bool IsArray() const
    {
        return !(T::template GetField<IsStringFlag>());
    }

    bool IsString() const
    {
        return T::template GetField<IsStringFlag>();
    }

protected:
    using IsStringFlag = typename T::LastField::NextFlag;
    using LastField = IsStringFlag;
};

/// Mixin for instructions with immediate constant value
class ImmediateMixin {
public:
    explicit ImmediateMixin(uint64_t immediate) : immediate_(immediate) {}

    NO_COPY_SEMANTIC(ImmediateMixin);
    NO_MOVE_SEMANTIC(ImmediateMixin);
    virtual ~ImmediateMixin() = default;

    void SetImm(uint64_t immediate)
    {
        immediate_ = immediate;
    }
    auto GetImm() const
    {
        return immediate_;
    }

protected:
    ImmediateMixin() = default;

private:
    uint64_t immediate_ {0};
};

/// Mixin for instructions with ConditionCode
template <typename T>
class ConditionMixin : public T {
public:
    enum class Prediction { NONE, LIKELY, UNLIKELY, SIZE = UNLIKELY };

    using T::T;
    explicit ConditionMixin(ConditionCode cc)
    {
        T::template SetField<CcFlag>(cc);
    }
    NO_COPY_SEMANTIC(ConditionMixin);
    NO_MOVE_SEMANTIC(ConditionMixin);
    ~ConditionMixin() override = default;

    auto GetCc() const
    {
        return T::template GetField<CcFlag>();
    }
    void SetCc(ConditionCode cc)
    {
        T::template SetField<CcFlag>(cc);
    }
    void InverseConditionCode()
    {
        SetCc(GetInverseConditionCode(GetCc()));
        if (IsLikely()) {
            SetUnlikely();
        } else if (IsUnlikely()) {
            SetLikely();
        }
    }

    bool IsLikely() const
    {
        return T::template GetField<PredictionFlag>() == Prediction::LIKELY;
    }
    bool IsUnlikely() const
    {
        return T::template GetField<PredictionFlag>() == Prediction::UNLIKELY;
    }
    void SetLikely()
    {
        T::template SetField<PredictionFlag>(Prediction::LIKELY);
    }
    void SetUnlikely()
    {
        T::template SetField<PredictionFlag>(Prediction::UNLIKELY);
    }

protected:
    ConditionMixin() = default;

    using CcFlag = typename T::LastField::template NextField<ConditionCode, MinimumBitsToStore(ConditionCode::CC_LAST)>;
    using PredictionFlag = typename CcFlag::template NextField<Prediction, MinimumBitsToStore(Prediction::SIZE)>;
    using LastField = PredictionFlag;
};

/// Mixin for instrucion with ShiftType
class ShiftTypeMixin {
public:
    explicit ShiftTypeMixin(ShiftType shift_type) : shift_type_(shift_type) {}
    NO_COPY_SEMANTIC(ShiftTypeMixin);
    NO_MOVE_SEMANTIC(ShiftTypeMixin);
    virtual ~ShiftTypeMixin() = default;

    void SetShiftType(ShiftType shift_type)
    {
        shift_type_ = shift_type;
    }

    ShiftType GetShiftType() const
    {
        return shift_type_;
    }

protected:
    ShiftTypeMixin() = default;

private:
    ShiftType shift_type_ {INVALID_SHIFT};
};

/// Mixin for instructions with multiple return values
template <typename T, size_t N>
class MultipleOutputMixin : public T {
public:
    using T::T;

    Register GetDstReg(unsigned index) const override
    {
        ASSERT(index < N);
        if (index == 0) {
            return T::GetDstReg();
        }
        return dst_regs_[index - 1];
    }

    Location GetDstLocation(unsigned index) const override
    {
        ASSERT(index < N);
        if (index == 0) {
            return Location::MakeRegister(T::GetDstReg());
        }
        return Location::MakeRegister(dst_regs_[index - 1]);
    }

    void SetDstReg(unsigned index, Register reg) override
    {
        ASSERT(index < N);
        if (index == 0) {
            T::SetDstReg(reg);
        } else {
            dst_regs_[index - 1] = reg;
        }
    }

    size_t GetDstCount() const override
    {
        return N;
    }

private:
    std::array<Register, N - 1> dst_regs_;
};

/**
 * Instruction with fixed number of inputs.
 * Shall not be instantiated directly, only through derived classes.
 */
template <size_t N>
class FixedInputsInst : public Inst {
public:
    using Inst::Inst;

    static constexpr int INPUT_COUNT = N;

    void SetSrcReg(unsigned index, Register reg) override
    {
        ASSERT(index < N);
        src_regs_[index] = reg;
    }

    Register GetSrcReg(unsigned index) const override
    {
        ASSERT(index < N);
        return src_regs_[index];
    }

    Location GetLocation(size_t index) const override
    {
        return Location::MakeRegister(GetSrcReg(index), GetInputType(index));
    }

    void SetLocation(size_t index, Location location) override
    {
        SetSrcReg(index, location.GetValue());
    }

    void SetDstLocation(Location location)
    {
        SetDstReg(location.GetValue());
    }

    void SetTmpLocation(Location location) override
    {
        tmp_location_ = location;
    }

    Location GetTmpLocation() const override
    {
        return tmp_location_;
    }

    Inst *Clone(const Graph *target_graph) const override;

private:
    template <typename T, std::size_t... IS>
    constexpr auto CreateArray(T value, [[maybe_unused]] std::index_sequence<IS...> unused)
    {
        return std::array<T, sizeof...(IS)> {(static_cast<void>(IS), value)...};
    }

private:
    std::array<Register, N> src_regs_ = CreateArray(INVALID_REG, std::make_index_sequence<INPUT_COUNT>());
    Location tmp_location_ {};
};

/**
 * Instructions with fixed static inputs
 * We need to explicitly declare these proxy classes because some code can't work with the templated inst classes, for
 * example DEFINE_INST macro.
 */
class FixedInputsInst0 : public FixedInputsInst<0> {
public:
    DECLARE_INST(FixedInputsInst0);
    using FixedInputsInst::FixedInputsInst;

    FixedInputsInst0(Opcode opcode, DataType::Type type, uint32_t pc = INVALID_PC) : FixedInputsInst(opcode, type, pc)
    {
    }

    NO_COPY_SEMANTIC(FixedInputsInst0);
    NO_MOVE_SEMANTIC(FixedInputsInst0);
    ~FixedInputsInst0() override = default;
};

class FixedInputsInst1 : public FixedInputsInst<1> {
public:
    DECLARE_INST(FixedInputsInst1);
    using FixedInputsInst::FixedInputsInst;

    FixedInputsInst1(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input) : FixedInputsInst(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
    }

    NO_COPY_SEMANTIC(FixedInputsInst1);
    NO_MOVE_SEMANTIC(FixedInputsInst1);
    ~FixedInputsInst1() override = default;

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        if (RequireState()) {
            return DataType::NO_TYPE;
        }
        return GetType();
    }
};

class FixedInputsInst2 : public FixedInputsInst<2U> {
public:
    DECLARE_INST(FixedInputsInst2);
    using FixedInputsInst::FixedInputsInst;

    FixedInputsInst2(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1)
        : FixedInputsInst(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
    }

    NO_COPY_SEMANTIC(FixedInputsInst2);
    NO_MOVE_SEMANTIC(FixedInputsInst2);
    ~FixedInputsInst2() override = default;
};

class FixedInputsInst3 : public FixedInputsInst<3U> {
public:
    DECLARE_INST(FixedInputsInst3);
    using FixedInputsInst::FixedInputsInst;

    FixedInputsInst3(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, Inst *input2)
        : FixedInputsInst(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetInput(2, input2);
    }
};

class FixedInputsInst4 : public FixedInputsInst<4U> {
public:
    DECLARE_INST(FixedInputsInst4);
    using FixedInputsInst::FixedInputsInst;

    FixedInputsInst4(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, Inst *input2,
                     Inst *input3)
        : FixedInputsInst(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetInput(2, input2);
        SetInput(3, input3);
    }
};

/// Instruction with variable inputs count
class DynamicInputsInst : public Inst {
public:
    DECLARE_INST(DynamicInputsInst);
    using Inst::Inst;

    static constexpr int INPUT_COUNT = MAX_STATIC_INPUTS;

    Location GetLocation(size_t index) const override
    {
        if (locations_ == nullptr) {
            return Location::Invalid();
        }
        return locations_->GetLocation(index);
    }

    Location GetDstLocation() const override
    {
        if (locations_ == nullptr) {
            return Location::Invalid();
        }
        return locations_->GetDstLocation();
    }

    Location GetTmpLocation() const override
    {
        if (locations_ == nullptr) {
            return Location::Invalid();
        }
        return locations_->GetTmpLocation();
    }

    void SetLocation(size_t index, Location location) override
    {
        ASSERT(locations_ != nullptr);
        locations_->SetLocation(index, location);
    }

    void SetDstLocation(Location location)
    {
        ASSERT(locations_ != nullptr);
        locations_->SetDstLocation(location);
    }

    void SetTmpLocation(Location location) override
    {
        ASSERT(locations_ != nullptr);
        locations_->SetTmpLocation(location);
    }

    void SetLocationsInfo(LocationsInfo *info)
    {
        locations_ = info;
    }

    Register GetSrcReg(unsigned index) const override
    {
        return GetLocation(index).GetValue();
    }

    void SetSrcReg(unsigned index, Register reg) override
    {
        SetLocation(index, Location::MakeRegister(reg, GetInputType(index)));
    }

private:
    LocationsInfo *locations_ {nullptr};
};

/// Unary operation instruction
class UnaryOperation : public FixedInputsInst<1> {
public:
    DECLARE_INST(UnaryOperation);
    using FixedInputsInst::FixedInputsInst;
    UnaryOperation(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input) : FixedInputsInst(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        if (GetOpcode() == Opcode::Cast) {
            return GetInput(0).GetInst()->GetType();
        }
        return GetType();
    }

    bool IsSafeInst() const override
    {
        return true;
    }

    void SetVnObject(VnObject *vn_obj) override;

    Inst *Evaluate();
};

/// Binary operation instruction
class BinaryOperation : public FixedInputsInst<2U> {
public:
    DECLARE_INST(BinaryOperation);
    using FixedInputsInst::FixedInputsInst;

    BinaryOperation(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1)
        : FixedInputsInst(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
    }

    uint32_t Latency() const override
    {
        if (GetOpcode() == Opcode::Div) {
            return OPTIONS.GetCompilerSchedLatencyLong();
        }
        return OPTIONS.GetCompilerSchedLatency();
    }

    bool IsSafeInst() const override
    {
        return true;
    }

    bool IsBinaryInst() const override
    {
        return true;
    }

    Inst *Evaluate();

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        auto input_type = GetInput(index).GetInst()->GetType();
        auto type = GetType();
        if (GetOpcode() == Opcode::Sub && !DataType::IsTypeSigned(type)) {
            ASSERT(GetCommonType(input_type) == GetCommonType(type) || input_type == DataType::POINTER ||
                   DataType::GetCommonType(input_type) == DataType::INT64);
            return input_type;
        }
        if (GetOpcode() == Opcode::Add && type == DataType::POINTER) {
            ASSERT(GetCommonType(input_type) == GetCommonType(type) ||
                   DataType::GetCommonType(input_type) == DataType::INT64);
            return input_type;
        }
        return GetType();
    }
};

/// Binary operation instruction with c immidiate
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class BinaryImmOperation : public FixedInputsInst<1>, public ImmediateMixin {
public:
    DECLARE_INST(BinaryImmOperation);
    using FixedInputsInst::FixedInputsInst;

    explicit BinaryImmOperation(Opcode opcode, uint64_t imm) : FixedInputsInst(opcode), ImmediateMixin(imm) {}
    explicit BinaryImmOperation(Opcode opcode, DataType::Type type, uint32_t pc, uint64_t imm)
        : FixedInputsInst(opcode, type, pc), ImmediateMixin(imm)
    {
    }

    BinaryImmOperation(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input, uint64_t imm)
        : FixedInputsInst(opcode, type, pc), ImmediateMixin(imm)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        auto input_type = GetInput(index).GetInst()->GetType();
        auto type = GetType();
        if (GetOpcode() == Opcode::SubI && !DataType::IsTypeSigned(type)) {
            ASSERT(GetCommonType(input_type) == GetCommonType(type) || input_type == DataType::POINTER);
            return input_type;
        }
        if (GetOpcode() == Opcode::AddI && type == DataType::POINTER) {
            ASSERT(DataType::GetCommonType(input_type) == DataType::GetCommonType(GetType()) ||
                   (input_type == DataType::REFERENCE && GetType() == DataType::POINTER));
            return input_type;
        }
        return GetType();
    }

    void SetVnObject(VnObject *vn_obj) override;
    bool DumpInputs(std::ostream * /* out */) const override;

    bool IsSafeInst() const override
    {
        return true;
    }

    bool IsBinaryImmInst() const override
    {
        return true;
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        static_cast<BinaryImmOperation *>(clone)->SetImm(GetImm());
        return clone;
    }
};

/// Unary operation that shifts its own operand prior the application.
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class UnaryShiftedRegisterOperation : public FixedInputsInst<1>, public ImmediateMixin, public ShiftTypeMixin {
public:
    DECLARE_INST(UnaryShiftedRegisterOperation);
    using FixedInputsInst::FixedInputsInst;

    explicit UnaryShiftedRegisterOperation(Opcode opcode, ShiftType shift_type, uint64_t imm)
        : FixedInputsInst(opcode), ImmediateMixin(imm), ShiftTypeMixin(shift_type)
    {
    }
    explicit UnaryShiftedRegisterOperation(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input, uint64_t imm,
                                           ShiftType shift_type)
        : FixedInputsInst(opcode, type, pc), ImmediateMixin(imm), ShiftTypeMixin(shift_type)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return GetType();
    }

    void SetVnObject(VnObject *vn_obj) override;
    bool DumpInputs(std::ostream * /* out */) const override;
    Inst *Clone(const Graph *target_graph) const override;
};

/// Binary operation that shifts its second operand prior the application.
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class BinaryShiftedRegisterOperation : public FixedInputsInst<2U>, public ImmediateMixin, public ShiftTypeMixin {
public:
    DECLARE_INST(BinaryShiftedRegisterOperation);
    using FixedInputsInst::FixedInputsInst;

    explicit BinaryShiftedRegisterOperation(Opcode opcode, ShiftType shift_type, uint64_t imm)
        : FixedInputsInst(opcode), ImmediateMixin(imm), ShiftTypeMixin(shift_type)
    {
    }
    explicit BinaryShiftedRegisterOperation(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1,
                                            uint64_t imm, ShiftType shift_type)
        : FixedInputsInst(opcode, type, pc), ImmediateMixin(imm), ShiftTypeMixin(shift_type)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return GetType();
    }

    void SetVnObject(VnObject *vn_obj) override;
    bool DumpInputs(std::ostream * /* out */) const override;
    Inst *Clone(const Graph *target_graph) const override;
};

class SpillFillInst;

/// Mixin to hold location data
class LocationDataMixin {
public:
    void SetLocationData(SpillFillData location_data)
    {
        location_data_ = location_data;
    }

    auto GetLocationData() const
    {
        return location_data_;
    }

    auto &GetLocationData()
    {
        return location_data_;
    }

protected:
    LocationDataMixin() = default;
    NO_COPY_SEMANTIC(LocationDataMixin);
    NO_MOVE_SEMANTIC(LocationDataMixin);
    virtual ~LocationDataMixin() = default;

private:
    SpillFillData location_data_ {};
};

/// Mixin to hold input types of call instruction
class InputTypesMixin {
public:
    InputTypesMixin() = default;
    NO_COPY_SEMANTIC(InputTypesMixin);
    NO_MOVE_SEMANTIC(InputTypesMixin);
    virtual ~InputTypesMixin() = default;

    void AllocateInputTypes(ArenaAllocator *allocator, size_t capacity)
    {
        ASSERT(allocator != nullptr);
        ASSERT(input_types_ == nullptr);
        input_types_ = allocator->New<ArenaVector<DataType::Type>>(allocator->Adapter());
        ASSERT(input_types_ != nullptr);
        input_types_->reserve(capacity);
        ASSERT(input_types_->capacity() >= capacity);
    }
    void AddInputType(DataType::Type type)
    {
        ASSERT(input_types_ != nullptr);
        input_types_->push_back(type);
    }
    ArenaVector<DataType::Type> *GetInputTypes()
    {
        return input_types_;
    }
    void CloneTypes(ArenaAllocator *allocator, InputTypesMixin *target_inst) const
    {
        if (UNLIKELY(input_types_ == nullptr)) {
            return;
        }
        target_inst->AllocateInputTypes(allocator, input_types_->size());
        for (auto input_type : *input_types_) {
            target_inst->AddInputType(input_type);
        }
    }

protected:
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
    ArenaVector<DataType::Type> *input_types_ {nullptr};
};

/// Mixin to hold method data
class MethodDataMixin {
public:
    static constexpr uint32_t INVALID_METHOD_ID = std::numeric_limits<uint32_t>::max();

    MethodDataMixin(uint32_t id, RuntimeInterface::MethodPtr method)
    {
        method_id_ = id;
        method_ = method;
    }

    void SetCallMethodId(uint32_t id)
    {
        method_id_ = id;
    }
    auto GetCallMethodId() const
    {
        return method_id_;
    }
    void SetCallMethod(RuntimeInterface::MethodPtr method)
    {
        method_ = method;
    }
    RuntimeInterface::MethodPtr GetCallMethod() const
    {
        return method_;
    }
    void SetFunctionObject(uintptr_t func)
    {
        function_ = func;
    }
    auto GetFunctionObject() const
    {
        return function_;
    }

protected:
    MethodDataMixin() = default;
    NO_COPY_SEMANTIC(MethodDataMixin);
    NO_MOVE_SEMANTIC(MethodDataMixin);
    virtual ~MethodDataMixin() = default;

private:
    uint32_t method_id_ {INVALID_METHOD_ID};
    RuntimeInterface::MethodPtr method_ {nullptr};
    uintptr_t function_ {0};
};

/// ResolveStatic
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class ResolveStaticInst : public FixedInputsInst1, public MethodDataMixin {
public:
    DECLARE_INST(ResolveStaticInst);
    using Base = FixedInputsInst1;
    using Base::Base;

    ResolveStaticInst(Opcode opcode, DataType::Type type, uint32_t pc, uint32_t method_id,
                      RuntimeInterface::MethodPtr method)
        : Base(opcode, type, pc), MethodDataMixin(method_id, method)
    {
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index == 0);
        // This is SaveState input
        return DataType::NO_TYPE;
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        ASSERT(target_graph != nullptr);
        auto inst_clone = FixedInputsInst1::Clone(target_graph);
        auto rslv_clone = static_cast<ResolveStaticInst *>(inst_clone);
        rslv_clone->SetCallMethodId(GetCallMethodId());
        rslv_clone->SetCallMethod(GetCallMethod());
        return inst_clone;
    }

    void DumpOpcode(std::ostream *out) const override;
};

/// ResolveVirtual
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class ResolveVirtualInst : public FixedInputsInst2, public MethodDataMixin {
public:
    DECLARE_INST(ResolveVirtualInst);
    using Base = FixedInputsInst2;
    using Base::Base;

    ResolveVirtualInst(Opcode opcode, DataType::Type type, uint32_t pc, uint32_t method_id,
                       RuntimeInterface::MethodPtr method)
        : Base(opcode, type, pc), MethodDataMixin(method_id, method)
    {
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(GetInputsCount() == 2U && index < GetInputsCount());
        switch (index) {
            case 0:
                return DataType::REFERENCE;
            case 1:
                // This is SaveState input
                return DataType::NO_TYPE;
            default:
                UNREACHABLE();
        }
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        ASSERT(target_graph != nullptr);
        auto inst_clone = FixedInputsInst2::Clone(target_graph);
        auto rslv_clone = static_cast<ResolveVirtualInst *>(inst_clone);
        rslv_clone->SetCallMethodId(GetCallMethodId());
        rslv_clone->SetCallMethod(GetCallMethod());
        return inst_clone;
    }

    void DumpOpcode(std::ostream *out) const override;
};

class InitStringInst : public FixedInputsInst2 {
public:
    DECLARE_INST(InitStringInst);
    using Base = FixedInputsInst2;
    using Base::Base;

    InitStringInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1,
                   StringCtorType ctor_type)
        : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetStringCtorType(ctor_type);
    }

    bool IsFromString() const
    {
        return GetField<StringCtorTypeField>() == StringCtorType::STRING;
    }
    bool IsFromCharArray() const
    {
        return GetField<StringCtorTypeField>() == StringCtorType::CHAR_ARRAY;
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        if (index == 0) {
            return DataType::REFERENCE;
        }
        return DataType::NO_TYPE;
    }

    void SetStringCtorType(StringCtorType ctor_type)
    {
        SetField<StringCtorTypeField>(ctor_type);
    }

    StringCtorType GetStringCtorType() const
    {
        return GetField<StringCtorTypeField>();
    }

    void DumpOpcode(std::ostream * /* unused */) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst2::Clone(target_graph);
        clone->CastToInitString()->SetStringCtorType(GetStringCtorType());
        return clone;
    }

private:
    using StringCtorTypeField = Base::LastField::NextField<StringCtorType, MinimumBitsToStore(StringCtorType::COUNT)>;
    using LastField = StringCtorTypeField;
};

/// Call instruction
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class CallInst : public InlinedInstMixin<DynamicInputsInst>, public InputTypesMixin, public MethodDataMixin {
public:
    DECLARE_INST(CallInst);
    using Base = InlinedInstMixin<DynamicInputsInst>;
    using Base::Base;

    CallInst(Opcode opcode, DataType::Type type, uint32_t pc, uint32_t method_id,
             RuntimeInterface::MethodPtr method = nullptr)
        : Base(opcode, type, pc), MethodDataMixin(method_id, method)
    {
    }

    int GetObjectIndex() const
    {
        return GetOpcode() == Opcode::CallResolvedVirtual ? 1 : 0;
    }

    Inst *GetObjectInst()
    {
        return GetInput(GetObjectIndex()).GetInst();
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(input_types_ != nullptr);
        ASSERT(index < input_types_->size());
        ASSERT(index < GetInputsCount());
        return (*input_types_)[index];
    }

    void DumpOpcode(std::ostream *out) const override;

    void SetCanNativeException(bool is_native)
    {
        SetField<IsNativeExceptionFlag>(is_native);
    }

    bool GetCanNativeException() const
    {
        return GetField<IsNativeExceptionFlag>();
    }

    Inst *Clone(const Graph *target_graph) const override;

    bool IsRuntimeCall() const override
    {
        return !IsInlined();
    }

protected:
    using IsNativeExceptionFlag = LastField::NextFlag;
    using LastField = IsNativeExceptionFlag;
};

// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class CallIndirectInst : public DynamicInputsInst, public InputTypesMixin {
public:
    DECLARE_INST(CallIndirectInst);
    using Base = DynamicInputsInst;
    using Base::Base;

    CallIndirectInst(Opcode opcode, DataType::Type type, uint32_t pc) : Base(opcode, type, pc) {}

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(input_types_ != nullptr);
        ASSERT(index < input_types_->size());
        ASSERT(index < GetInputsCount());
        return (*input_types_)[index];
    }

    Inst *Clone(const Graph *target_graph) const override;
};

/// Length methods instruction
class LengthMethodInst : public ArrayInstMixin<FixedInputsInst1> {
public:
    DECLARE_INST(LengthMethodInst);
    using Base = ArrayInstMixin<FixedInputsInst1>;
    using Base::Base;

    explicit LengthMethodInst(Opcode opcode, bool is_array = true) : Base(opcode)
    {
        SetIsArray(is_array);
    }
    LengthMethodInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input, bool is_array = true)
        : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
        SetIsArray(is_array);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return DataType::REFERENCE;
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        ASSERT(static_cast<LengthMethodInst *>(clone)->IsArray() == IsArray());
        return clone;
    }
};

/// Compare instruction
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class CompareInst : public InstWithOperandsType<ConditionMixin<FixedInputsInst2>> {
public:
    DECLARE_INST(CompareInst);
    using BaseInst = InstWithOperandsType<ConditionMixin<FixedInputsInst2>>;
    using BaseInst::BaseInst;

    CompareInst(Opcode opcode, DataType::Type type, uint32_t pc, ConditionCode cc) : BaseInst(opcode, type, pc)
    {
        SetCc(cc);
    }
    CompareInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, DataType::Type oper_type,
                ConditionCode cc)
        : BaseInst(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetOperandsType(oper_type);
        SetCc(cc);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return GetOperandsType();
    }
    void DumpOpcode(std::ostream * /* unused */) const override;

    void SetVnObject(VnObject *vn_obj) override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        ASSERT(clone->CastToCompare()->GetCc() == GetCc());
        ASSERT(clone->CastToCompare()->GetOperandsType() == GetOperandsType());
        return clone;
    }
};

/// Mixin for AnyTypeMixin instructions
template <typename T>
class AnyTypeMixin : public T {
public:
    using T::T;

    void SetAnyType(AnyBaseType any_type)
    {
        T::template SetField<AnyBaseTypeField>(any_type);
    }

    AnyBaseType GetAnyType() const override
    {
        return T::template GetField<AnyBaseTypeField>();
    }

    bool IsIntegerWasSeen() const
    {
        return T::template GetField<IntegerWasSeen>();
    }

    void SetIsIntegerWasSeen(bool value)
    {
        T::template SetField<IntegerWasSeen>(value);
    }

    bool IsSpecialWasSeen() const
    {
        return T::template GetField<SpecialWasSeen>();
    }

    profiling::AnyInputType GetAllowedInputType() const
    {
        return T::template GetField<AllowedInputType>();
    }

    void SetAllowedInputType(profiling::AnyInputType type)
    {
        T::template SetField<AllowedInputType>(type);
    }

    bool IsTypeWasProfiled() const
    {
        return T::template GetField<TypeWasProfiled>();
    }

    void SetIsTypeWasProfiled(bool value)
    {
        T::template SetField<TypeWasProfiled>(value);
    }

protected:
    using AnyBaseTypeField =
        typename T::LastField::template NextField<AnyBaseType, MinimumBitsToStore(AnyBaseType::COUNT)>;
    using IntegerWasSeen = typename AnyBaseTypeField::NextFlag;
    using SpecialWasSeen = typename IntegerWasSeen::NextFlag;
    using AllowedInputType =
        typename AnyBaseTypeField::template NextField<profiling::AnyInputType,
                                                      MinimumBitsToStore(profiling::AnyInputType::LAST)>;
    static_assert(SpecialWasSeen::END_BIT == AllowedInputType::END_BIT);
    using TypeWasProfiled = typename AllowedInputType::NextFlag;
    using LastField = TypeWasProfiled;
};

/// CompareAnyTypeInst instruction
class CompareAnyTypeInst : public AnyTypeMixin<FixedInputsInst1> {
public:
    DECLARE_INST(CompareAnyTypeInst);
    using BaseInst = AnyTypeMixin<FixedInputsInst1>;
    using BaseInst::BaseInst;

    CompareAnyTypeInst(Opcode opcode, uint32_t pc, AnyBaseType any_type = AnyBaseType::UNDEFINED_TYPE)
        : BaseInst(opcode, DataType::Type::BOOL, pc)
    {
        SetAnyType(any_type);
    }

    CompareAnyTypeInst(Opcode opcode, uint32_t pc, Inst *input0, AnyBaseType any_type = AnyBaseType::UNDEFINED_TYPE)
        : BaseInst(opcode, DataType::Type::BOOL, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetAnyType(any_type);
        SetInput(0, input0);
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return GetInput(index).GetInst()->GetType();
    }

    void DumpOpcode(std::ostream *out) const override;
    void SetVnObject(VnObject *vn_obj) override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        ASSERT(clone->CastToCompareAnyType()->GetAnyType() == GetAnyType());
        return clone;
    }
};

/// GetAnyTypeName instruction
class GetAnyTypeNameInst : public AnyTypeMixin<FixedInputsInst0> {
public:
    DECLARE_INST(GetAnyTypeNameInst);
    using BaseInst = AnyTypeMixin<FixedInputsInst0>;
    using BaseInst::BaseInst;

    GetAnyTypeNameInst(Opcode opcode, uint32_t pc, AnyBaseType any_type = AnyBaseType::UNDEFINED_TYPE)
        : BaseInst(opcode, DataType::Type::ANY, pc)
    {
        SetAnyType(any_type);
    }

    void DumpOpcode(std::ostream *out) const override;
    void SetVnObject(VnObject *vn_obj) override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToGetAnyTypeName()->SetAnyType(GetAnyType());
        return clone;
    }
};

/// CastAnyTypeValueInst instruction
class CastAnyTypeValueInst : public AnyTypeMixin<FixedInputsInst1> {
public:
    DECLARE_INST(CastAnyTypeValueInst);
    using BaseInst = AnyTypeMixin<FixedInputsInst1>;
    using BaseInst::BaseInst;

    CastAnyTypeValueInst(Opcode opcode, uint32_t pc, AnyBaseType any_type = AnyBaseType::UNDEFINED_TYPE)
        : BaseInst(opcode, AnyBaseTypeToDataType(any_type), pc)
    {
    }

    CastAnyTypeValueInst(Opcode opcode, uint32_t pc, Inst *input0, AnyBaseType any_type = AnyBaseType::UNDEFINED_TYPE)
        : BaseInst(opcode, AnyBaseTypeToDataType(any_type), pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetAnyType(any_type);
        SetInput(0, input0);
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return GetInput(index).GetInst()->GetType();
    }

    DataType::Type GetDeducedType() const
    {
        return AnyBaseTypeToDataType(GetAnyType());
    }

    void DumpOpcode(std::ostream *out) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph)->CastToCastAnyTypeValue();
        ASSERT(clone->GetAnyType() == GetAnyType());
        ASSERT(clone->GetType() == GetType());
        return clone;
    }
};

/// CastValueToAnyTypeInst instruction
class CastValueToAnyTypeInst : public AnyTypeMixin<FixedInputsInst1> {
public:
    DECLARE_INST(CastValueToAnyTypeInst);
    using BaseInst = AnyTypeMixin<FixedInputsInst1>;
    using BaseInst::BaseInst;

    CastValueToAnyTypeInst(Opcode opcode, uint32_t pc) : BaseInst(opcode, DataType::ANY, pc) {}

    CastValueToAnyTypeInst(Opcode opcode, uint32_t pc, AnyBaseType any_type, Inst *input0)
        : BaseInst(opcode, DataType::ANY, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetAnyType(any_type);
        SetInput(0, input0);
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return GetInput(index).GetInst()->GetType();
    }

    void DumpOpcode(std::ostream *out) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph)->CastToCastValueToAnyType();
        ASSERT(clone->GetAnyType() == GetAnyType());
        ASSERT(clone->GetType() == GetType());
        return clone;
    }
};

/// AnyTypeCheckInst instruction
class AnyTypeCheckInst : public AnyTypeMixin<FixedInputsInst2> {
public:
    DECLARE_INST(AnyTypeCheckInst);
    using BaseInst = AnyTypeMixin<FixedInputsInst2>;
    using BaseInst::BaseInst;

    AnyTypeCheckInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1,
                     AnyBaseType any_type = AnyBaseType::UNDEFINED_TYPE)
        : BaseInst(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetAnyType(any_type);
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return (index == 0) ? DataType::ANY : DataType::NO_TYPE;
    }

    void DumpOpcode(std::ostream *out) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        ASSERT(clone->CastToAnyTypeCheck()->GetAnyType() == GetAnyType());
        return clone;
    }

    DeoptimizeType GetDeoptimizeType() const;
};

/// HclassCheck instruction
enum class HclassChecks {
    ALL_CHECKS = 0,
    IS_FUNCTION,
    IS_NOT_CLASS_CONSTRUCTOR,
};
class HclassCheckInst : public AnyTypeMixin<FixedInputsInst2> {
public:
    DECLARE_INST(HclassCheckInst);
    using BaseInst = AnyTypeMixin<FixedInputsInst2>;
    using BaseInst::BaseInst;

    HclassCheckInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1,
                    AnyBaseType any_type = AnyBaseType::UNDEFINED_TYPE)
        : BaseInst(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetAnyType(any_type);
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return (index == 0) ? DataType::REFERENCE : DataType::NO_TYPE;
    }

    void SetCheckIsFunction(bool value)
    {
        SetField<CheckTypeIsFunction>(value);
    }

    bool GetCheckIsFunction() const
    {
        return GetField<CheckTypeIsFunction>();
    }

    void SetCheckFunctionIsNotClassConstructor(bool value = true)
    {
        SetField<CheckFunctionIsNotClassConstructor>(value);
    }

    bool GetCheckFunctionIsNotClassConstructor() const
    {
        return GetField<CheckFunctionIsNotClassConstructor>();
    }

    void DumpOpcode(std::ostream *out) const override;

    void ExtendFlags(Inst *inst);

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = static_cast<HclassCheckInst *>(FixedInputsInst2::Clone(target_graph));
        clone->SetCheckFunctionIsNotClassConstructor(GetCheckFunctionIsNotClassConstructor());
        clone->SetCheckIsFunction(GetCheckIsFunction());
        return clone;
    }

protected:
    using CheckTypeIsFunction = LastField::NextFlag;
    using CheckFunctionIsNotClassConstructor = CheckTypeIsFunction::NextFlag;
    using LastField = CheckFunctionIsNotClassConstructor;
};

/**
 * ConstantInst represent constant value.
 *
 * Available types: INT64, FLOAT32, FLOAT64, ANY. All integer types are stored as INT64 value.
 * Once type of constant is set, it can't be changed anymore.
 */
class ConstantInst : public Inst {
public:
    DECLARE_INST(ConstantInst);
    using Inst::Inst;

    template <typename T>
    explicit ConstantInst(Opcode /* unused */, T value, bool support_int32 = false) : Inst(Opcode::Constant)
    {
        ASSERT(GetTypeFromCType<T>() != DataType::NO_TYPE);
        // NOLINTNEXTLINE(readability-braces-around-statements, bugprone-branch-clone)
        if constexpr (GetTypeFromCType<T>() == DataType::FLOAT64) {
            value_ = bit_cast<uint64_t, double>(value);
            // NOLINTNEXTLINE(readability-braces-around-statements, readability-misleading-indentation)
        } else if constexpr (GetTypeFromCType<T>() == DataType::FLOAT32) {
            value_ = bit_cast<uint32_t, float>(value);
            // NOLINTNEXTLINE(readability-braces-around-statements, readability-misleading-indentation)
        } else if constexpr (GetTypeFromCType<T>() == DataType::ANY) {
            value_ = value.Raw();
            // NOLINTNEXTLINE(readability-braces-around-statements, readability-misleading-indentation)
        } else if (GetTypeFromCType<T>(support_int32) == DataType::INT32) {
            value_ = static_cast<int32_t>(value);
            // NOLINTNEXTLINE(readability-braces-around-statements, readability-misleading-indentation)
        } else {
            value_ = value;
        }

        SetType(GetTypeFromCType<T>(support_int32));
    }

    bool IsSafeInst() const override
    {
        return true;
    }

    uint64_t GetRawValue() const
    {
        return value_;
    }

    uint32_t GetInt32Value() const
    {
        ASSERT(GetType() == DataType::INT32);
        return static_cast<uint32_t>(value_);
    }

    uint64_t GetInt64Value() const
    {
        ASSERT(GetType() == DataType::INT64);
        return value_;
    }

    uint64_t GetIntValue() const
    {
        ASSERT(GetType() == DataType::INT64 || GetType() == DataType::INT32);
        return value_;
    }

    float GetFloatValue() const
    {
        ASSERT(GetType() == DataType::FLOAT32);
        return bit_cast<float, uint32_t>(static_cast<uint32_t>(value_));
    }

    double GetDoubleValue() const
    {
        ASSERT(GetType() == DataType::FLOAT64);
        return bit_cast<double, uint64_t>(value_);
    }

    ConstantInst *GetNextConst()
    {
        return next_const_;
    }
    void SetNextConst(ConstantInst *next_const)
    {
        next_const_ = next_const;
    }

    template <typename T>
    static constexpr DataType::Type GetTypeFromCType(bool support_int32 = false)
    {
        // NOLINTNEXTLINE(readability-braces-around-statements, bugprone-branch-clone)
        if constexpr (std::is_integral_v<T>) {
            if (support_int32 && sizeof(T) == sizeof(uint32_t)) {
                return DataType::INT32;
            }
            return DataType::INT64;
            // NOLINTNEXTLINE(readability-braces-around-statements, readability-misleading-indentation)
        } else if constexpr (std::is_same_v<T, float>) {
            return DataType::FLOAT32;
            // NOLINTNEXTLINE(readability-braces-around-statements, readability-misleading-indentation)
        } else if constexpr (std::is_same_v<T, double>) {
            return DataType::FLOAT64;
            // NOLINTNEXTLINE(readability-braces-around-statements, readability-misleading-indentation)
        } else if constexpr (std::is_same_v<T, DataType::Any>) {
            return DataType::ANY;
        }
        return DataType::NO_TYPE;
    }

    inline bool IsEqualConst(double value, [[maybe_unused]] bool support_int32 = false)
    {
        return IsEqualConst(DataType::FLOAT64, bit_cast<uint64_t, double>(value));
    }
    inline bool IsEqualConst(float value, [[maybe_unused]] bool support_int32 = false)
    {
        return IsEqualConst(DataType::FLOAT32, bit_cast<uint32_t, float>(value));
    }
    inline bool IsEqualConst(DataType::Any value, [[maybe_unused]] bool support_int32 = false)
    {
        return IsEqualConst(DataType::ANY, value.Raw());
    }
    inline bool IsEqualConst(DataType::Type type, uint64_t value)
    {
        return GetType() == type && value_ == value;
    }
    template <typename T>
    inline bool IsEqualConst(T value, bool support_int32 = false)
    {
        static_assert(GetTypeFromCType<T>() == DataType::INT64);
        if (support_int32 && sizeof(T) == sizeof(uint32_t)) {
            return (GetType() == DataType::INT32 && static_cast<int32_t>(value_) == static_cast<int32_t>(value));
        }
        return (GetType() == DataType::INT64 && value_ == static_cast<uint64_t>(value));
    }

    inline bool IsEqualConstAllTypes(int64_t value, bool support_int32 = false)
    {
        return IsEqualConst(value, support_int32) || IsEqualConst(static_cast<float>(value)) ||
               IsEqualConst(static_cast<double>(value));
    }

    bool IsBoolConst() const override
    {
        ASSERT(IsConst());
        return (GetType() == DataType::INT32 || GetType() == DataType::INT64) &&
               (GetIntValue() == 0 || GetIntValue() == 1);
    }

    void SetImmTableSlot(ImmTableSlot imm_slot)
    {
        imm_slot_ = imm_slot;
    }

    auto GetImmTableSlot() const
    {
        return imm_slot_;
    }

    Location GetDstLocation() const override
    {
        if (GetImmTableSlot() != INVALID_IMM_TABLE_SLOT) {
            return Location::MakeConstant(GetImmTableSlot());
        }
        return Inst::GetDstLocation();
    }

    bool DumpInputs(std::ostream * /* out */) const override;

    Inst *Clone(const Graph *target_graph) const override;

private:
    uint64_t value_ {0};
    ConstantInst *next_const_ {nullptr};
    ImmTableSlot imm_slot_ {INVALID_IMM_TABLE_SLOT};
};

// Type describing the purpose of the SpillFillInst.
// RegAlloc may use this information to preserve correct order of several SpillFillInst
// instructions placed along each other in the graph.
enum SpillFillType {
    UNKNOWN,
    INPUT_FILL,
    CONNECT_SPLIT_SIBLINGS,
    SPLIT_MOVE,
};

class SpillFillInst : public FixedInputsInst0 {
public:
    DECLARE_INST(SpillFillInst);

    explicit SpillFillInst(ArenaAllocator *allocator, Opcode opcode, SpillFillType type = UNKNOWN)
        : FixedInputsInst0(opcode), spill_fills_(allocator->Adapter()), sf_type_(type)
    {
    }

    void AddMove(Register src, Register dst, DataType::Type type)
    {
        AddSpillFill(Location::MakeRegister(src, type), Location::MakeRegister(dst, type), type);
    }

    void AddSpill(Register src, StackSlot dst, DataType::Type type)
    {
        AddSpillFill(Location::MakeRegister(src, type), Location::MakeStackSlot(dst), type);
    }

    void AddFill(StackSlot src, Register dst, DataType::Type type)
    {
        AddSpillFill(Location::MakeStackSlot(src), Location::MakeRegister(dst, type), type);
    }

    void AddMemCopy(StackSlot src, StackSlot dst, DataType::Type type)
    {
        AddSpillFill(Location::MakeStackSlot(src), Location::MakeStackSlot(dst), type);
    }

    void AddSpillFill(const SpillFillData &spill_fill)
    {
        spill_fills_.emplace_back(spill_fill);
    }

    void AddSpillFill(const Location &src, const Location &dst, DataType::Type type)
    {
        spill_fills_.emplace_back(SpillFillData {src.GetKind(), dst.GetKind(), src.GetValue(), dst.GetValue(), type});
    }

    const ArenaVector<SpillFillData> &GetSpillFills() const
    {
        return spill_fills_;
    }

    ArenaVector<SpillFillData> &GetSpillFills()
    {
        return spill_fills_;
    }

    const SpillFillData &GetSpillFill(size_t n) const
    {
        ASSERT(n < spill_fills_.size());
        return spill_fills_[n];
    }

    SpillFillData &GetSpillFill(size_t n)
    {
        ASSERT(n < spill_fills_.size());
        return spill_fills_[n];
    }

    void RemoveSpillFill(size_t n)
    {
        ASSERT(n < spill_fills_.size());
        spill_fills_.erase(spill_fills_.begin() + n);
    }

    // Get register number, holded by n-th spill-fill
    Register GetInputReg(size_t n) const
    {
        ASSERT(n < spill_fills_.size());
        ASSERT(spill_fills_[n].SrcType() == LocationType::REGISTER);
        return spill_fills_[n].SrcValue();
    }

    void ClearSpillFills()
    {
        spill_fills_.clear();
    }

    SpillFillType GetSpillFillType() const
    {
        return sf_type_;
    }

    void SetSpillFillType(SpillFillType type)
    {
        sf_type_ = type;
    }

    bool DumpInputs(std::ostream * /* out */) const override;

#ifndef NDEBUG
    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph)->CastToSpillFill();
        clone->SetSpillFillType(GetSpillFillType());
        for (auto spill_fill : spill_fills_) {
            clone->AddSpillFill(spill_fill);
        }
        return clone;
    }
#endif

private:
    ArenaVector<SpillFillData> spill_fills_;
    SpillFillType sf_type_ {UNKNOWN};
};

// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class ParameterInst : public Inst, public LocationDataMixin {
public:
    DECLARE_INST(ParameterInst);
    using Inst::Inst;
    static constexpr uint16_t DYNAMIC_NUM_ARGS = std::numeric_limits<uint16_t>::max();
    static constexpr uint16_t INVALID_ARG_REF_NUM = std::numeric_limits<uint16_t>::max();

    explicit ParameterInst(Opcode /* unused */, uint16_t arg_number, DataType::Type type = DataType::NO_TYPE)
        : Inst(Opcode::Parameter, type, INVALID_PC), arg_number_(arg_number)
    {
    }
    uint16_t GetArgNumber() const
    {
        return arg_number_;
    }

    void SetArgNumber(uint16_t arg_number)
    {
        arg_number_ = arg_number;
    }
    uint16_t GetArgRefNumber() const
    {
        return arg_ref_number_;
    }

    void SetArgRefNumber(uint16_t arg_ref_number)
    {
        arg_ref_number_ = arg_ref_number;
    }
    bool DumpInputs(std::ostream * /* out */) const override;

    Inst *Clone(const Graph *target_graph) const override;

private:
    uint16_t arg_number_ {0};
    uint16_t arg_ref_number_ {INVALID_ARG_REF_NUM};
};

inline bool IsZeroConstant(const Inst *inst)
{
    return inst->IsConst() && inst->GetType() == DataType::INT64 && inst->CastToConstant()->GetIntValue() == 0;
}

inline bool IsZeroConstantOrNullPtr(const Inst *inst)
{
    return IsZeroConstant(inst) || inst->GetOpcode() == Opcode::NullPtr;
}

/// Phi instruction
class PhiInst : public AnyTypeMixin<DynamicInputsInst> {
public:
    DECLARE_INST(PhiInst);
    using BaseInst = AnyTypeMixin<DynamicInputsInst>;
    using BaseInst::BaseInst;

    PhiInst(Opcode opcode, DataType::Type type, uint32_t pc, AnyBaseType any_type = AnyBaseType::UNDEFINED_TYPE)
        : BaseInst(opcode, type, pc)
    {
        SetAnyType(any_type);
    }

    /// Get basic block corresponding to given input index. Returned pointer to basic block, can't be nullptr
    BasicBlock *GetPhiInputBb(unsigned index);
    const BasicBlock *GetPhiInputBb(unsigned index) const
    {
        return (const_cast<PhiInst *>(this))->GetPhiInputBb(index);
    }

    uint32_t GetPhiInputBbNum(unsigned index) const
    {
        ASSERT(index < GetInputsCount());
        return GetDynamicOperands()->GetUser(index)->GetBbNum();
    }

    void SetPhiInputBbNum(unsigned index, uint32_t bb_num)
    {
        ASSERT(index < GetInputsCount());
        GetDynamicOperands()->GetUser(index)->SetBbNum(bb_num);
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = DynamicInputsInst::Clone(target_graph);
        ASSERT(clone->CastToPhi()->GetAnyType() == GetAnyType());
        return clone;
    }

    AnyBaseType GetAssumedAnyType() const
    {
        return GetAnyType();
    }

    void SetAssumedAnyType(AnyBaseType type)
    {
        SetAnyType(type);
    }

    /// Get input instruction corresponding to the given basic block, can't be null.
    Inst *GetPhiInput(BasicBlock *bb);
    Inst *GetPhiDataflowInput(BasicBlock *bb);
    bool DumpInputs(std::ostream * /* out */) const override;
    void DumpOpcode(std::ostream *out) const override;

    // Get index of the given block in phi inputs
    size_t GetPredBlockIndex(const BasicBlock *block) const;

protected:
    using FlagIsLive = LastField::NextFlag;
    using LastField = FlagIsLive;
};

/**
 * Immediate for SavaState:
 * value - constant value to be stored
 * vreg - virtual register number
 */
struct SaveStateImm {
    uint64_t value;
    uint16_t vreg;
    DataType::Type type;
    VRegType vreg_type;
};

/**
 * Frame state saving instruction
 * Aims to save pbc registers before calling something that can raise exception
 */
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class SaveStateInst : public DynamicInputsInst {
public:
    DECLARE_INST(SaveStateInst);
    using DynamicInputsInst::DynamicInputsInst;

    SaveStateInst(Opcode opcode, uint32_t pc, void *method, CallInst *inst, uint32_t inlining_depth)
        : DynamicInputsInst(opcode, DataType::NO_TYPE, pc),
          method_(method),
          caller_inst_(inst),
          inlining_depth_(inlining_depth)
    {
    }

    bool DumpInputs(std::ostream *out) const override;

    void AppendBridge(Inst *inst)
    {
        ASSERT(inst != nullptr);
        auto new_input = AppendInput(inst);
        SetVirtualRegister(new_input, VirtualRegister(VirtualRegister::BRIDGE, VRegType::VREG));
    }

    void SetVirtualRegister(size_t index, VirtualRegister reg)
    {
        static_assert(sizeof(reg) <= sizeof(uintptr_t), "Consider passing the register by reference");
        ASSERT(index < GetInputsCount());
        GetDynamicOperands()->GetUser(index)->SetVirtualRegister(reg);
    }

    VirtualRegister GetVirtualRegister(size_t index) const
    {
        ASSERT(index < GetInputsCount());
        return GetDynamicOperands()->GetUser(index)->GetVirtualRegister();
    }

    bool Verify() const
    {
        for (size_t i {0}; i < GetInputsCount(); ++i) {
            if (static_cast<uint16_t>(GetVirtualRegister(i)) == VirtualRegister::INVALID) {
                return false;
            }
        }
        return true;
    }

    bool RemoveNumericInputs()
    {
        size_t idx = 0;
        size_t inputs_count = GetInputsCount();
        bool removed = false;
        while (idx < inputs_count) {
            auto input_inst = GetInput(idx).GetInst();
            if (DataType::IsTypeNumeric(input_inst->GetType())) {
                RemoveInput(idx);
                inputs_count--;
                removed = true;
            } else {
                idx++;
            }
        }
        return removed;
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return GetInput(index).GetInst()->GetType();
    }
    auto GetMethod() const
    {
        return method_;
    }
    auto SetMethod(void *method)
    {
        method_ = method;
    }

    auto GetCallerInst() const
    {
        return caller_inst_;
    }
    auto SetCallerInst(CallInst *inst)
    {
        caller_inst_ = inst;
    }

    uint32_t GetInliningDepth() const override
    {
        return inlining_depth_;
    }
    void SetInliningDepth(uint32_t inlining_depth)
    {
        inlining_depth_ = inlining_depth;
    }

    void AppendImmediate(uint64_t imm, uint16_t vreg, DataType::Type type, VRegType vreg_type);

    const ArenaVector<SaveStateImm> *GetImmediates() const
    {
        return immediates_;
    }

    const SaveStateImm &GetImmediate(size_t index) const
    {
        ASSERT(immediates_ != nullptr && index < immediates_->size());
        return (*immediates_)[index];
    }

    void AllocateImmediates(ArenaAllocator *allocator, size_t size = 0);

    size_t GetImmediatesCount() const
    {
        if (immediates_ == nullptr) {
            return 0;
        }
        return immediates_->size();
    }

    void SetRootsRegMaskBit(size_t reg)
    {
        ASSERT(reg < roots_regs_mask_.size());
        roots_regs_mask_.set(reg);
    }

    void SetRootsStackMaskBit(size_t slot)
    {
        if (roots_stack_mask_ != nullptr) {
            roots_stack_mask_->SetBit(slot);
        }
    }

    ArenaBitVector *GetRootsStackMask()
    {
        return roots_stack_mask_;
    }

    auto &GetRootsRegsMask()
    {
        return roots_regs_mask_;
    }

    void CreateRootsStackMask(ArenaAllocator *allocator)
    {
        ASSERT(roots_stack_mask_ == nullptr);
        roots_stack_mask_ = allocator->New<ArenaBitVector>(allocator);
        roots_stack_mask_->Reset();
    }

    Inst *Clone(const Graph *target_graph) const override;
#ifndef NDEBUG
    void SetInputsWereDeleted()
    {
        SetField<FlagInputsWereDeleted>(true);
    }

    bool GetInputsWereDeleted()
    {
        return GetField<FlagInputsWereDeleted>();
    }
#endif

protected:
#ifndef NDEBUG
    using FlagInputsWereDeleted = LastField::NextFlag;
    using LastField = FlagInputsWereDeleted;
#endif

private:
    ArenaVector<SaveStateImm> *immediates_ {nullptr};
    void *method_ {nullptr};
    // If instruction is in the inlined graph, this variable points to the inliner's call instruction.
    CallInst *caller_inst_ {nullptr};
    uint32_t inlining_depth_ {0};
    ArenaBitVector *roots_stack_mask_ {nullptr};
    std::bitset<BITS_PER_UINT32> roots_regs_mask_ {0};
};

/// Load value from array or string
class LoadInst : public ArrayInstMixin<NeedBarrierMixin<FixedInputsInst2>> {
public:
    DECLARE_INST(LoadInst);
    using Base = ArrayInstMixin<NeedBarrierMixin<FixedInputsInst2>>;
    using Base::Base;

    explicit LoadInst(Opcode opcode, bool is_array = true) : Base(opcode)
    {
        SetIsArray(is_array);
    }
    LoadInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, bool need_barrier = false,
             bool is_array = true)
        : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetNeedBarrier(need_barrier);
        SetIsArray(is_array);
    }

    Inst *GetArray()
    {
        return GetInput(0).GetInst();
    }
    Inst *GetIndex()
    {
        return GetInput(1).GetInst();
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        switch (index) {
            case 0: {
                auto input_type = GetInput(0).GetInst()->GetType();
                ASSERT(input_type == DataType::NO_TYPE || input_type == DataType::REFERENCE ||
                       input_type == DataType::ANY);
                return input_type;
            }
            case 1:
                return DataType::INT32;
            default:
                return DataType::NO_TYPE;
        }
    }

    uint32_t Latency() const override
    {
        return OPTIONS.GetCompilerSchedLatencyLong();
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = NeedBarrierMixin<FixedInputsInst2>::Clone(target_graph);
        ASSERT(static_cast<LoadInst *>(clone)->IsArray() == IsArray());
        return clone;
    }
};

class LoadCompressedStringCharInst : public FixedInputsInst3 {
public:
    DECLARE_INST(LoadCompressedStringCharInst);
    using Base = FixedInputsInst3;
    using Base::Base;

    LoadCompressedStringCharInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1,
                                 Inst *input2)
        : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetInput(2, input2);
    }

    Inst *GetArray()
    {
        return GetInput(0).GetInst();
    }
    Inst *GetIndex()
    {
        return GetInput(1).GetInst();
    }
    Inst *GetLength() const
    {
        return GetInput(2U).GetInst();
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        switch (index) {
            case 0:
                return DataType::REFERENCE;
            case 1:
                return DataType::INT32;
            case 2U:
                return DataType::INT32;
            default:
                return DataType::NO_TYPE;
        }
    }

    uint32_t Latency() const override
    {
        return OPTIONS.GetCompilerSchedLatencyLong();
    }
};

// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class LoadCompressedStringCharInstI : public FixedInputsInst2, public ImmediateMixin {
public:
    DECLARE_INST(LoadCompressedStringCharInstI);
    using Base = FixedInputsInst2;
    using Base::Base;

    LoadCompressedStringCharInstI(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1,
                                  uint64_t imm)
        : Base(opcode, type, pc), ImmediateMixin(imm)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        switch (index) {
            case 0:
                return DataType::REFERENCE;
            case 1:
                return DataType::INT32;
            default:
                return DataType::NO_TYPE;
        }
    }

    uint32_t Latency() const override
    {
        return OPTIONS.GetCompilerSchedLatencyLong();
    }
};
/// Store value into array element
class StoreInst : public NeedBarrierMixin<FixedInputsInst3> {
public:
    DECLARE_INST(StoreInst);
    using Base = NeedBarrierMixin<FixedInputsInst3>;
    using Base::Base;

    StoreInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, Inst *input2,
              bool need_barrier = false)
        : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetInput(2, input2);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }

    Inst *GetArray()
    {
        return GetInput(0).GetInst();
    }
    Inst *GetIndex()
    {
        return GetInput(1).GetInst();
    }
    Inst *GetStoredValue()
    {
        return GetInput(2U).GetInst();
    }
    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        switch (index) {
            case 0: {
                auto input_type = GetInput(0).GetInst()->GetType();
                ASSERT(input_type == DataType::ANY || input_type == DataType::REFERENCE);
                return input_type;
            }
            case 1:
                return DataType::INT32;
            case 2U:
                return GetType();
            default:
                return DataType::NO_TYPE;
        }
    }

    // StoreArray call barriers twice,so we need to save input register for second call
    bool IsPropagateLiveness() const override
    {
        return GetType() == DataType::REFERENCE;
    }
};

/// Load value from array, using array index as immediate
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class LoadInstI : public VolatileMixin<ArrayInstMixin<NeedBarrierMixin<FixedInputsInst1>>>, public ImmediateMixin {
public:
    DECLARE_INST(LoadInstI);
    using Base = VolatileMixin<ArrayInstMixin<NeedBarrierMixin<FixedInputsInst1>>>;
    using Base::Base;

    LoadInstI(Opcode opcode, uint64_t imm, bool is_array = true) : Base(opcode), ImmediateMixin(imm)
    {
        SetIsArray(is_array);
    }
    LoadInstI(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input, uint64_t imm, bool is_volatile = false,
              bool need_barrier = false, bool is_array = true)
        : Base(opcode, type, pc), ImmediateMixin(imm)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
        SetIsArray(is_array);
        SetVolatile(is_volatile);
        SetNeedBarrier(need_barrier);
    }

    Inst *GetArray()
    {
        return GetInput(0).GetInst();
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index == 0);
        auto input_type = GetInput(0).GetInst()->GetType();
        ASSERT(input_type == DataType::ANY || input_type == DataType::REFERENCE);
        return input_type;
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }

    bool DumpInputs(std::ostream * /* out */) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = static_cast<LoadInstI *>(FixedInputsInst::Clone(target_graph));
        clone->SetImm(GetImm());
        ASSERT(clone->IsArray() == IsArray());
        ASSERT(clone->GetVolatile() == GetVolatile());
        return clone;
    }

    uint32_t Latency() const override
    {
        return OPTIONS.GetCompilerSchedLatencyLong();
    }
};

/// Load value from pointer with offset
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class LoadMemInstI : public VolatileMixin<NeedBarrierMixin<FixedInputsInst1>>, public ImmediateMixin {
public:
    DECLARE_INST(LoadMemInstI);
    using Base = VolatileMixin<NeedBarrierMixin<FixedInputsInst1>>;
    using Base::Base;

    LoadMemInstI(Opcode opcode, uint64_t imm) : Base(opcode), ImmediateMixin(imm) {}
    LoadMemInstI(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input, uint64_t imm, bool is_volatile = false,
                 bool need_barrier = false)
        : Base(opcode, type, pc), ImmediateMixin(imm)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
        SetVolatile(is_volatile);
        SetNeedBarrier(need_barrier);
    }

    Inst *GetPointer()
    {
        return GetInput(0).GetInst();
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index == 0);
        auto input_0_type = GetInput(0).GetInst()->GetType();
        ASSERT(input_0_type == DataType::POINTER || input_0_type == DataType::REFERENCE);
        return input_0_type;
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }

    bool DumpInputs(std::ostream * /* out */) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = static_cast<LoadMemInstI *>(FixedInputsInst::Clone(target_graph));
        clone->SetImm(GetImm());
        ASSERT(clone->GetVolatile() == GetVolatile());
        return clone;
    }

    uint32_t Latency() const override
    {
        return OPTIONS.GetCompilerSchedLatencyLong();
    }
};

/// Store value into array element, using array index as immediate
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class StoreInstI : public VolatileMixin<NeedBarrierMixin<FixedInputsInst2>>, public ImmediateMixin {
public:
    DECLARE_INST(StoreInstI);
    using Base = VolatileMixin<NeedBarrierMixin<FixedInputsInst2>>;
    using Base::Base;

    StoreInstI(Opcode opcode, uint64_t imm) : Base(opcode), ImmediateMixin(imm) {}
    StoreInstI(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, uint64_t imm,
               bool is_volatile = false, bool need_barrier = false)
        : Base(opcode, type, pc), ImmediateMixin(imm)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetVolatile(is_volatile);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }

    Inst *GetArray()
    {
        return GetInput(0).GetInst();
    }
    Inst *GetStoredValue()
    {
        return GetInput(1).GetInst();
    }
    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        switch (index) {
            case 0: {
                auto input_type = GetInput(0).GetInst()->GetType();
                ASSERT(input_type == DataType::ANY || input_type == DataType::REFERENCE);
                return input_type;
            }
            case 1:
                return GetType();
            default:
                UNREACHABLE();
        }
    }

    bool DumpInputs(std::ostream * /* out */) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = static_cast<StoreInstI *>(FixedInputsInst::Clone(target_graph));
        clone->SetImm(GetImm());
        ASSERT(clone->GetVolatile() == GetVolatile());
        return clone;
    }

    // StoreArrayI call barriers twice,so we need to save input register for second call
    bool IsPropagateLiveness() const override
    {
        return GetType() == DataType::REFERENCE;
    }
};

/// Store value into pointer by offset
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class StoreMemInstI : public VolatileMixin<NeedBarrierMixin<FixedInputsInst2>>, public ImmediateMixin {
public:
    DECLARE_INST(StoreMemInstI);
    using Base = VolatileMixin<NeedBarrierMixin<FixedInputsInst2>>;
    using Base::Base;

    StoreMemInstI(Opcode opcode, uint64_t imm) : Base(opcode), ImmediateMixin(imm) {}
    StoreMemInstI(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, uint64_t imm,
                  bool is_volatile = false, bool need_barrier = false)
        : Base(opcode, type, pc), ImmediateMixin(imm)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetVolatile(is_volatile);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }

    Inst *GetPointer()
    {
        return GetInput(0).GetInst();
    }
    Inst *GetStoredValue()
    {
        return GetInput(1).GetInst();
    }
    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        switch (index) {
            case 0: {
                auto input_0_type = GetInput(0).GetInst()->GetType();
                ASSERT(input_0_type == DataType::POINTER || input_0_type == DataType::REFERENCE);
                return input_0_type;
            }
            case 1:
                return GetType();
            default:
                UNREACHABLE();
        }
    }

    bool DumpInputs(std::ostream * /* out */) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = static_cast<StoreMemInstI *>(FixedInputsInst::Clone(target_graph));
        clone->SetImm(GetImm());
        ASSERT(clone->GetVolatile() == GetVolatile());
        return clone;
    }
};

/// Bounds check, using array index as immediate
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class BoundsCheckInstI : public ArrayInstMixin<FixedInputsInst<2U>>, public ImmediateMixin {
public:
    DECLARE_INST(BoundsCheckInstI);
    using Base = ArrayInstMixin<FixedInputsInst<2U>>;
    using Base::Base;

    BoundsCheckInstI(Opcode opcode, uint64_t imm, bool is_array = true) : Base(opcode), ImmediateMixin(imm)
    {
        SetIsArray(is_array);
    }

    BoundsCheckInstI(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, uint64_t imm,
                     bool is_array = true)
        : Base(opcode, type, pc), ImmediateMixin(imm)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetIsArray(is_array);
    }

    bool DumpInputs(std::ostream * /* out */) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToBoundsCheckI()->SetImm(GetImm());
        ASSERT(clone->CastToBoundsCheckI()->IsArray() == IsArray());
        return clone;
    }
};

/// Bounds check instruction
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class BoundsCheckInst : public ArrayInstMixin<FixedInputsInst<3U>> {
public:
    DECLARE_INST(BoundsCheckInst);
    using Base = ArrayInstMixin<FixedInputsInst<3U>>;
    using Base::Base;

    explicit BoundsCheckInst(Opcode opcode, bool is_array = true) : Base(opcode)
    {
        SetIsArray(is_array);
    }

    BoundsCheckInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, Inst *input2,
                    bool is_array = true)
        : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetInput(2, input2);
        SetIsArray(is_array);
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        ASSERT(clone->CastToBoundsCheck()->IsArray() == IsArray());
        return clone;
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        if (index == GetInputsCount() - 1) {
            return DataType::NO_TYPE;
        }
        return GetType();
    }
};

class NullCheckInst : public FixedInputsInst2 {
public:
    DECLARE_INST(NullCheckInst);
    using Base = FixedInputsInst2;
    using Base::Base;

    NullCheckInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1) : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
    }

    bool IsImplicit() const
    {
        return GetField<IsImplicitFlag>();
    }

    void SetImplicit(bool is_implicit = true)
    {
        SetField<IsImplicitFlag>(is_implicit);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        if (index == GetInputsCount() - 1) {
            return DataType::NO_TYPE;
        }
        return DataType::REFERENCE;
    }

private:
    using IsImplicitFlag = LastField::NextFlag;
    using LastField = IsImplicitFlag;
};

/// Return immediate
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class ReturnInstI : public FixedInputsInst<0>, public ImmediateMixin {
public:
    DECLARE_INST(ReturnInstI);
    using FixedInputsInst::FixedInputsInst;

    ReturnInstI(Opcode opcode, uint64_t imm) : FixedInputsInst(opcode), ImmediateMixin(imm) {}
    ReturnInstI(Opcode opcode, DataType::Type type, uint32_t pc, uint64_t imm)
        : FixedInputsInst(opcode, type, pc), ImmediateMixin(imm)
    {
    }

    bool DumpInputs(std::ostream * /* out */) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToReturnI()->SetImm(GetImm());
        return clone;
    }
};

class ReturnInlinedInst : public FixedInputsInst<1> {
public:
    DECLARE_INST(ReturnInlinedInst);
    using FixedInputsInst::FixedInputsInst;

    ReturnInlinedInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input) : FixedInputsInst(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
    }

    bool IsExtendedLiveness() const
    {
        return GetField<IsExtendedLivenessFlag>();
    }

    void SetExtendedLiveness(bool is_extened_liveness = true)
    {
        SetField<IsExtendedLivenessFlag>(is_extened_liveness);
    }

private:
    using IsExtendedLivenessFlag = LastField::NextFlag;
    using LastField = IsExtendedLivenessFlag;
};

/// Monitor instruction
class MonitorInst : public FixedInputsInst2 {
public:
    DECLARE_INST(MonitorInst);
    using Base = FixedInputsInst2;
    using Base::Base;

    MonitorInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1) : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
    }

    bool IsExit() const
    {
        return GetField<Exit>();
    }

    bool IsEntry() const
    {
        return !GetField<Exit>();
    }

    void SetExit()
    {
        SetField<Exit>(true);
    }

    void SetEntry()
    {
        SetField<Exit>(false);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        if (index == 1) {
            return DataType::NO_TYPE;
        }
        return DataType::REFERENCE;
    }

    void DumpOpcode(std::ostream * /* unused */) const override;

protected:
    using Exit = LastField::NextFlag;
    using LastField = Exit;
};

#include "intrinsics_flags.inl"

// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class IntrinsicInst : public InlinedInstMixin<DynamicInputsInst>, public InputTypesMixin, public TypeIdMixin {
public:
    DECLARE_INST(IntrinsicInst);
    using Base = InlinedInstMixin<DynamicInputsInst>;
    using Base::Base;
    using IntrinsicId = RuntimeInterface::IntrinsicId;

    IntrinsicInst(Opcode opcode, IntrinsicId intrinsic_id) : Base(opcode), intrinsic_id_(intrinsic_id)
    {
        AdjustFlags(intrinsic_id, this);
    }

    IntrinsicInst(Opcode opcode, DataType::Type type, uint32_t pc, IntrinsicId intrinsic_id)
        : Base(opcode, type, pc), intrinsic_id_(intrinsic_id)
    {
        AdjustFlags(intrinsic_id, this);
    }

    IntrinsicId GetIntrinsicId() const
    {
        return intrinsic_id_;
    }

    void SetIntrinsicId(IntrinsicId intrinsic_id)
    {
        intrinsic_id_ = intrinsic_id;
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(input_types_ != nullptr);
        ASSERT(index < input_types_->size());
        ASSERT(index < GetInputsCount());
        return (*input_types_)[index];
    }

    uint32_t GetImm(size_t index) const
    {
        ASSERT(HasImms() && index < GetImms().size());
        return GetImms()[index];
    }

    void SetImm(size_t index, uint32_t imm)
    {
        ASSERT(HasImms() && index < GetImms().size());
        GetImms()[index] = imm;
    }

    ArenaVector<uint32_t> &GetImms()
    {
        return *imms_;
    }

    const ArenaVector<uint32_t> &GetImms() const
    {
        return *imms_;
    }

    bool HasImms() const
    {
        return imms_ != nullptr;
    }

    void AddImm(ArenaAllocator *allocator, uint32_t imm)
    {
        if (imms_ == nullptr) {
            imms_ = allocator->New<ArenaVector<uint32_t>>(allocator->Adapter());
        }
        imms_->push_back(imm);
    }

    bool IsNativeCall() const;

    bool HasArgumentsOnStack() const
    {
        return GetField<ArgumentsOnStack>();
    }

    void SetArgumentsOnStack()
    {
        SetField<ArgumentsOnStack>(true);
    }

    bool IsReplaceOnDeoptimize() const
    {
        return GetField<ReplaceOnDeoptimize>();
    }

    void SetReplaceOnDeoptimize()
    {
        SetField<ReplaceOnDeoptimize>(true);
    }

    bool HasIdInput() const
    {
        return GetField<IdInput>();
    }

    void SetHasIdInput()
    {
        SetField<IdInput>(true);
    }

    bool IsMethodFirstInput() const
    {
        return GetField<MethodFirstInput>();
    }

    void SetMethodFirstInput()
    {
        SetField<MethodFirstInput>(true);
    }

    Inst *Clone(const Graph *target_graph) const override;

    bool CanBeInlined()
    {
        return IsInlined();
    }

    void SetRelocate()
    {
        SetField<Relocate>(true);
    }

    bool GetRelocate() const
    {
        return GetField<Relocate>();
    }

    void DumpOpcode(std::ostream *out) const override;
    bool DumpInputs(std::ostream *out) const override;

    void DumpImms(std::ostream *out) const;

protected:
    using ArgumentsOnStack = LastField::NextFlag;
    using Relocate = ArgumentsOnStack::NextFlag;
    using ReplaceOnDeoptimize = Relocate::NextFlag;
    using IdInput = ReplaceOnDeoptimize::NextFlag;
    using MethodFirstInput = IdInput::NextFlag;
    using LastField = MethodFirstInput;

private:
    IntrinsicId intrinsic_id_ {RuntimeInterface::IntrinsicId::COUNT};
    ArenaVector<uint32_t> *imms_ {nullptr};  // record imms appeared in intrinsics
};

#include <get_intrinsics_names.inl>
#include <intrinsics_enum.inl>
#include <can_encode_builtin.inl>

/// Cast instruction
class CastInst : public InstWithOperandsType<FixedInputsInst1> {
public:
    DECLARE_INST(CastInst);
    using BaseInst = InstWithOperandsType<FixedInputsInst1>;
    using BaseInst::BaseInst;

    CastInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, DataType::Type oper_type)
        : BaseInst(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetOperandsType(oper_type);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index == 0);
        auto type = GetOperandsType();
        return type != DataType::NO_TYPE ? type : GetInput(0).GetInst()->GetType();
    }

    void SetVnObject(VnObject *vn_obj) override;

    void DumpOpcode(std::ostream * /* out */) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = static_cast<CastInst *>(FixedInputsInst::Clone(target_graph));
        ASSERT(clone->GetOperandsType() == GetOperandsType());
        return clone;
    }

    bool IsDynamicCast() const;
};

/// Cmp instruction
class CmpInst : public InstWithOperandsType<FixedInputsInst2> {
public:
    DECLARE_INST(CmpInst);
    using BaseInst = InstWithOperandsType<FixedInputsInst2>;
    using BaseInst::BaseInst;

    CmpInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, DataType::Type oper_type)
        : BaseInst(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetOperandsType(oper_type);
    }

    bool IsFcmpg() const
    {
        ASSERT(DataType::IsFloatType(GetOperandsType()));
        return GetField<Fcmpg>();
    }
    bool IsFcmpl() const
    {
        ASSERT(DataType::IsFloatType(GetOperandsType()));
        return !GetField<Fcmpg>();
    }
    void SetFcmpg()
    {
        ASSERT(DataType::IsFloatType(GetOperandsType()));
        SetField<Fcmpg>(true);
    }
    void SetFcmpg(bool v)
    {
        ASSERT(DataType::IsFloatType(GetOperandsType()));
        SetField<Fcmpg>(v);
    }
    void SetFcmpl()
    {
        ASSERT(DataType::IsFloatType(GetOperandsType()));
        SetField<Fcmpg>(false);
    }
    void SetFcmpl(bool v)
    {
        ASSERT(DataType::IsFloatType(GetOperandsType()));
        SetField<Fcmpg>(!v);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return GetOperandsType();
    }

    void SetVnObject(VnObject *vn_obj) override;

    void DumpOpcode(std::ostream * /* out */) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        ASSERT(clone->CastToCmp()->GetOperandsType() == GetOperandsType());
        return clone;
    }

protected:
    using Fcmpg = LastField::NextFlag;
    using LastField = Fcmpg;
};

/// Load value from instance field
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class LoadObjectInst : public ObjectTypeMixin<VolatileMixin<NeedBarrierMixin<FixedInputsInst1>>>,
                       public TypeIdMixin,
                       public FieldMixin {
public:
    DECLARE_INST(LoadObjectInst);
    using Base = ObjectTypeMixin<VolatileMixin<NeedBarrierMixin<FixedInputsInst1>>>;
    using Base::Base;

    LoadObjectInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input, uint32_t type_id,
                   RuntimeInterface::MethodPtr method, RuntimeInterface::FieldPtr field, bool is_volatile = false,
                   bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method), FieldMixin(field)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
        SetVolatile(is_volatile);
        SetNeedBarrier(need_barrier);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        ASSERT(GetInputsCount() == 1);
        auto input_type = GetInput(0).GetInst()->GetType();
        ASSERT(input_type == DataType::NO_TYPE || input_type == DataType::REFERENCE || input_type == DataType::ANY);
        return input_type;
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier() || GetVolatile();
    }

    void DumpOpcode(std::ostream *out) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToLoadObject()->SetTypeId(GetTypeId());
        clone->CastToLoadObject()->SetMethod(GetMethod());
        clone->CastToLoadObject()->SetObjField(GetObjField());
        ASSERT(clone->CastToLoadObject()->GetVolatile() == GetVolatile());
        ASSERT(clone->CastToLoadObject()->GetObjectType() == GetObjectType());
        return clone;
    }

    uint32_t Latency() const override
    {
        return OPTIONS.GetCompilerSchedLatencyLong();
    }
};

/// Load value from memory by offset
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class LoadMemInst : public ScaleMixin<VolatileMixin<NeedBarrierMixin<FixedInputsInst2>>> {
public:
    DECLARE_INST(LoadMemInst);
    using Base = ScaleMixin<VolatileMixin<NeedBarrierMixin<FixedInputsInst2>>>;
    using Base::Base;

    LoadMemInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, bool is_volatile = false,
                bool need_barrier = false)
        : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetVolatile(is_volatile);
        SetNeedBarrier(need_barrier);
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        ASSERT(GetInputsCount() == 2U);
        if (index == 1U) {
            return DataType::IsInt32Bit(GetInput(1).GetInst()->GetType()) ? DataType::INT32 : DataType::INT64;
        }

        ASSERT(index == 0U);
        auto input_0_type = GetInput(0).GetInst()->GetType();
        ASSERT((GetInput(0).GetInst()->IsConst() && input_0_type == DataType::INT64) ||
               input_0_type == DataType::POINTER || input_0_type == DataType::REFERENCE);
        return input_0_type;
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier() || GetVolatile();
    }

    void DumpOpcode(std::ostream *out) const override;
    bool DumpInputs(std::ostream * /* unused */) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        ASSERT(clone->CastToLoad()->GetVolatile() == GetVolatile());
        ASSERT(clone->CastToLoad()->GetScale() == GetScale());
        return clone;
    }

    uint32_t Latency() const override
    {
        return OPTIONS.GetCompilerSchedLatencyLong();
    }
};

/// Load value from dynamic object
class LoadObjectDynamicInst : public DynObjectAccessMixin<FixedInputsInst3> {
public:
    DECLARE_INST(LoadObjectDynamicInst);
    using Base = DynObjectAccessMixin<FixedInputsInst3>;
    using Base::Base;

    LoadObjectDynamicInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, Inst *input2)
        : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetInput(2, input2);
    }

    bool IsBarrier() const override
    {
        return true;
    }
};

/// Store value to dynamic object
class StoreObjectDynamicInst : public DynObjectAccessMixin<FixedInputsInst<4U>> {
public:
    DECLARE_INST(StoreObjectDynamicInst);
    using Base = DynObjectAccessMixin<FixedInputsInst<4U>>;
    using Base::Base;

    StoreObjectDynamicInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, Inst *input2,
                           Inst *input3)
        : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetInput(2, input2);
        SetInput(3, input3);
    }

    bool IsBarrier() const override
    {
        return true;
    }
};

/// Resolve instance field
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class ResolveObjectFieldInst : public NeedBarrierMixin<FixedInputsInst1>, public TypeIdMixin {
public:
    DECLARE_INST(ResolveObjectFieldInst);
    using Base = NeedBarrierMixin<FixedInputsInst1>;
    using Base::Base;

    ResolveObjectFieldInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input, uint32_t type_id,
                           RuntimeInterface::MethodPtr method, bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
        SetNeedBarrier(need_barrier);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(GetInputsCount() == 1U);
        ASSERT(index == 0);
        // This is SaveState input
        return DataType::NO_TYPE;
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToResolveObjectField()->SetTypeId(GetTypeId());
        clone->CastToResolveObjectField()->SetMethod(GetMethod());
        return clone;
    }

    uint32_t Latency() const override
    {
        return OPTIONS.GetCompilerSchedLatencyLong();
    }

    void DumpOpcode(std::ostream *out) const override;
};

/// Load value from resolved instance field
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class LoadResolvedObjectFieldInst : public VolatileMixin<NeedBarrierMixin<FixedInputsInst2>>, public TypeIdMixin {
public:
    DECLARE_INST(LoadResolvedObjectFieldInst);
    using Base = VolatileMixin<NeedBarrierMixin<FixedInputsInst2>>;
    using Base::Base;

    LoadResolvedObjectFieldInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1,
                                uint32_t type_id, RuntimeInterface::MethodPtr method, bool is_volatile = false,
                                bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetVolatile(is_volatile);
        SetNeedBarrier(need_barrier);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(GetInputsCount() == 2U);
        ASSERT(index < GetInputsCount());
        return index == 0 ? DataType::REFERENCE : DataType::UINT32;
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier() || GetVolatile();
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToLoadResolvedObjectField()->SetTypeId(GetTypeId());
        clone->CastToLoadResolvedObjectField()->SetMethod(GetMethod());
        ASSERT(clone->CastToLoadResolvedObjectField()->GetVolatile() == GetVolatile());
        return clone;
    }

    uint32_t Latency() const override
    {
        return OPTIONS.GetCompilerSchedLatencyLong();
    }

    void DumpOpcode(std::ostream *out) const override;
};

/// Store value into instance field
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class StoreObjectInst : public ObjectTypeMixin<VolatileMixin<NeedBarrierMixin<FixedInputsInst2>>>,
                        public TypeIdMixin,
                        public FieldMixin {
public:
    DECLARE_INST(StoreObjectInst);
    using Base = ObjectTypeMixin<VolatileMixin<NeedBarrierMixin<FixedInputsInst2>>>;
    using Base::Base;
    static constexpr size_t STORED_INPUT_INDEX = 1;

    StoreObjectInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, uint32_t type_id,
                    RuntimeInterface::MethodPtr method, RuntimeInterface::FieldPtr field, bool is_volatile = false,
                    bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method), FieldMixin(field)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetVolatile(is_volatile);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier() || GetVolatile();
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        ASSERT(GetInputsCount() == 2U);
        return index == 0 ? DataType::REFERENCE : GetType();
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToStoreObject()->SetTypeId(GetTypeId());
        clone->CastToStoreObject()->SetMethod(GetMethod());
        clone->CastToStoreObject()->SetObjField(GetObjField());
        ASSERT(clone->CastToStoreObject()->GetVolatile() == GetVolatile());
        ASSERT(clone->CastToStoreObject()->GetObjectType() == GetObjectType());
        return clone;
    }

    // StoreObject call barriers twice,so we need to save input register for second call
    bool IsPropagateLiveness() const override
    {
        return GetType() == DataType::REFERENCE;
    }

    void DumpOpcode(std::ostream *out) const override;
};

/// Store value into resolved instance field
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class StoreResolvedObjectFieldInst : public VolatileMixin<NeedBarrierMixin<FixedInputsInst3>>, public TypeIdMixin {
public:
    DECLARE_INST(StoreResolvedObjectFieldInst);
    using Base = VolatileMixin<NeedBarrierMixin<FixedInputsInst3>>;
    using Base::Base;
    static constexpr size_t STORED_INPUT_INDEX = 1;

    StoreResolvedObjectFieldInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1,
                                 Inst *input2, uint32_t type_id, RuntimeInterface::MethodPtr method,
                                 bool is_volatile = false, bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetInput(2, input2);
        SetVolatile(is_volatile);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier() || GetVolatile();
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        ASSERT(GetInputsCount() == 3U);
        if (index == 0) {
            return DataType::REFERENCE;  // null-check
        }
        if (index == 1) {
            return GetType();  // stored value
        }
        return DataType::UINT32;  // field offset
    }

    void DumpOpcode(std::ostream *out) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToStoreResolvedObjectField()->SetTypeId(GetTypeId());
        clone->CastToStoreResolvedObjectField()->SetMethod(GetMethod());
        ASSERT(clone->CastToStoreResolvedObjectField()->GetVolatile() == GetVolatile());
        return clone;
    }

    // StoreResolvedObjectField calls barriers twice,
    // so we need to save input register for the second call.
    bool IsPropagateLiveness() const override
    {
        return GetType() == DataType::REFERENCE;
    }
};

/// Store value in memory by offset
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class StoreMemInst : public ScaleMixin<VolatileMixin<NeedBarrierMixin<FixedInputsInst3>>> {
public:
    DECLARE_INST(StoreMemInst);
    using Base = ScaleMixin<VolatileMixin<NeedBarrierMixin<FixedInputsInst3>>>;
    using Base::Base;

    static constexpr size_t STORED_INPUT_INDEX = 2;

    StoreMemInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, Inst *input2,
                 bool is_volatile = false, bool need_barrier = false)
        : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetInput(2, input2);
        SetVolatile(is_volatile);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier() || GetVolatile();
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        ASSERT(GetInputsCount() == 3U);
        if (index == 1U) {
            return DataType::IsInt32Bit(GetInput(1).GetInst()->GetType()) ? DataType::INT32 : DataType::INT64;
        }
        if (index == 2U) {
            return GetType();
        }

        ASSERT(index == 0U);
        auto input_0_type = GetInput(0).GetInst()->GetType();
        ASSERT(input_0_type == DataType::POINTER || input_0_type == DataType::REFERENCE);
        return input_0_type;
    }

    void DumpOpcode(std::ostream *out) const override;
    bool DumpInputs(std::ostream * /* unused */) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        ASSERT(clone->CastToStore()->GetVolatile() == GetVolatile());
        ASSERT(clone->CastToStore()->GetScale() == GetScale());
        return clone;
    }
};

/// Load static field from class.
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class LoadStaticInst : public VolatileMixin<NeedBarrierMixin<FixedInputsInst1>>, public TypeIdMixin, public FieldMixin {
public:
    DECLARE_INST(LoadStaticInst);
    using Base = VolatileMixin<NeedBarrierMixin<FixedInputsInst1>>;
    using Base::Base;

    LoadStaticInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input, uint32_t type_id,
                   RuntimeInterface::MethodPtr method, RuntimeInterface::FieldPtr field, bool is_volatile = false,
                   bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method), FieldMixin(field)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
        SetVolatile(is_volatile);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier() || GetVolatile();
    }

    void DumpOpcode(std::ostream *out) const override;

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        ASSERT(index == 0);
        return DataType::REFERENCE;
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToLoadStatic()->SetTypeId(GetTypeId());
        clone->CastToLoadStatic()->SetMethod(GetMethod());
        clone->CastToLoadStatic()->SetObjField(GetObjField());
        ASSERT(clone->CastToLoadStatic()->GetVolatile() == GetVolatile());
        return clone;
    }

    uint32_t Latency() const override
    {
        return OPTIONS.GetCompilerSchedLatencyLong();
    }
};

/// Resolve static instance field
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class ResolveObjectFieldStaticInst : public NeedBarrierMixin<FixedInputsInst1>, public TypeIdMixin {
public:
    DECLARE_INST(ResolveObjectFieldStaticInst);
    using Base = NeedBarrierMixin<FixedInputsInst1>;
    using Base::Base;

    ResolveObjectFieldStaticInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input, uint32_t type_id,
                                 RuntimeInterface::MethodPtr method, bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
        SetNeedBarrier(need_barrier);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(GetInputsCount() == 1U);
        ASSERT(index == 0);
        // This is SaveState input
        return DataType::NO_TYPE;
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToResolveObjectFieldStatic()->SetTypeId(GetTypeId());
        clone->CastToResolveObjectFieldStatic()->SetMethod(GetMethod());
        return clone;
    }

    uint32_t Latency() const override
    {
        return OPTIONS.GetCompilerSchedLatencyLong();
    }

    void DumpOpcode(std::ostream *out) const override;
};

/// Load value from resolved static instance field
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class LoadResolvedObjectFieldStaticInst : public VolatileMixin<NeedBarrierMixin<FixedInputsInst1>>, public TypeIdMixin {
public:
    DECLARE_INST(LoadResolvedObjectFieldStaticInst);
    using Base = VolatileMixin<NeedBarrierMixin<FixedInputsInst1>>;
    using Base::Base;

    LoadResolvedObjectFieldStaticInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input, uint32_t type_id,
                                      RuntimeInterface::MethodPtr method, bool is_volatile = false,
                                      bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
        SetVolatile(is_volatile);
        SetNeedBarrier(need_barrier);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(GetInputsCount() == 1U);
        ASSERT(index < GetInputsCount());
        return DataType::REFERENCE;
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier() || GetVolatile();
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToLoadResolvedObjectFieldStatic()->SetTypeId(GetTypeId());
        clone->CastToLoadResolvedObjectFieldStatic()->SetMethod(GetMethod());
        ASSERT(clone->CastToLoadResolvedObjectFieldStatic()->GetVolatile() == GetVolatile());
        return clone;
    }

    uint32_t Latency() const override
    {
        return OPTIONS.GetCompilerSchedLatencyLong();
    }

    void DumpOpcode(std::ostream *out) const override;
};

/// Store value into static field.
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class StoreStaticInst : public VolatileMixin<NeedBarrierMixin<FixedInputsInst2>>,
                        public TypeIdMixin,
                        public FieldMixin {
public:
    DECLARE_INST(StoreStaticInst);
    using Base = VolatileMixin<NeedBarrierMixin<FixedInputsInst2>>;
    using Base::Base;
    static constexpr size_t STORED_INPUT_INDEX = 1;

    StoreStaticInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, uint32_t type_id,
                    RuntimeInterface::MethodPtr method, RuntimeInterface::FieldPtr field, bool is_volatile = false,
                    bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method), FieldMixin(field)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetVolatile(is_volatile);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier() || GetVolatile();
    }

    void DumpOpcode(std::ostream *out) const override;

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        if (index == 0) {
            return DataType::REFERENCE;
        }
        return GetType();
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToStoreStatic()->SetTypeId(GetTypeId());
        clone->CastToStoreStatic()->SetMethod(GetMethod());
        clone->CastToStoreStatic()->SetObjField(GetObjField());
        ASSERT(clone->CastToStoreStatic()->GetVolatile() == GetVolatile());
        return clone;
    }

    // StoreStatic call barriers twice,so we need to save input register for second call
    bool IsPropagateLiveness() const override
    {
        return GetType() == DataType::REFERENCE;
    }
};

/// Store value into unresolved static field.
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class UnresolvedStoreStaticInst : public NeedBarrierMixin<FixedInputsInst2>, public TypeIdMixin {
public:
    DECLARE_INST(UnresolvedStoreStaticInst);
    using Base = NeedBarrierMixin<FixedInputsInst2>;
    using Base::Base;

    UnresolvedStoreStaticInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1,
                              uint32_t type_id, RuntimeInterface::MethodPtr method, bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return true;
    }

    void DumpOpcode(std::ostream *out) const override;

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        if (index == 1) {
            // This is SaveState input
            return DataType::NO_TYPE;
        }
        ASSERT(index == 0);
        return GetType();
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToUnresolvedStoreStatic()->SetTypeId(GetTypeId());
        clone->CastToUnresolvedStoreStatic()->SetMethod(GetMethod());
        return clone;
    }
};

/// Store value into resolved static field.
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class StoreResolvedObjectFieldStaticInst : public NeedBarrierMixin<FixedInputsInst2>, public TypeIdMixin {
public:
    DECLARE_INST(StoreResolvedObjectFieldStaticInst);
    using Base = NeedBarrierMixin<FixedInputsInst2>;
    using Base::Base;

    StoreResolvedObjectFieldStaticInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1,
                                       uint32_t type_id, RuntimeInterface::MethodPtr method, bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return true;
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(GetInputsCount() == 2U);
        ASSERT(index < GetInputsCount());
        if (index == 0) {
            return DataType::REFERENCE;
        }
        return GetType();  // stored value
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToStoreResolvedObjectFieldStatic()->SetTypeId(GetTypeId());
        clone->CastToStoreResolvedObjectFieldStatic()->SetMethod(GetMethod());
        return clone;
    }

    void DumpOpcode(std::ostream *out) const override;
};

/// Create new object
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class NewObjectInst : public NeedBarrierMixin<FixedInputsInst2>, public TypeIdMixin {
public:
    DECLARE_INST(NewObjectInst);
    using Base = NeedBarrierMixin<FixedInputsInst2>;
    using Base::Base;

    NewObjectInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, uint32_t type_id,
                  RuntimeInterface::MethodPtr method, bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }
    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        if (index == 0) {
            return DataType::REFERENCE;
        }
        return DataType::NO_TYPE;
    }

    void DumpOpcode(std::ostream *out) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToNewObject()->SetTypeId(GetTypeId());
        clone->CastToNewObject()->SetMethod(GetMethod());
        return clone;
    }
};

/// Create new array
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class NewArrayInst : public NeedBarrierMixin<FixedInputsInst3>, public TypeIdMixin {
public:
    DECLARE_INST(NewArrayInst);
    using Base = NeedBarrierMixin<FixedInputsInst3>;
    using Base::Base;

    static constexpr size_t INDEX_CLASS = 0;
    static constexpr size_t INDEX_SIZE = 1;
    static constexpr size_t INDEX_SAVE_STATE = 2;

    NewArrayInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input_class, Inst *input_size,
                 Inst *input_save_state, uint32_t type_id, RuntimeInterface::MethodPtr method,
                 bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(INDEX_CLASS, input_class);
        SetInput(INDEX_SIZE, input_size);
        SetInput(INDEX_SAVE_STATE, input_save_state);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        switch (index) {
            case INDEX_CLASS:
                return GetInput(0).GetInst()->GetType();
            case INDEX_SIZE:
                return DataType::INT32;
            case INDEX_SAVE_STATE:
                // This is SaveState input
                return DataType::NO_TYPE;
            default:
                UNREACHABLE();
        }
    }

    void DumpOpcode(std::ostream *out) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToNewArray()->SetTypeId(GetTypeId());
        clone->CastToNewArray()->SetMethod(GetMethod());
        return clone;
    }
};

// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class LoadConstArrayInst : public NeedBarrierMixin<FixedInputsInst1>, public TypeIdMixin {
public:
    DECLARE_INST(LoadConstArrayInst);
    using Base = NeedBarrierMixin<FixedInputsInst1>;
    using Base::Base;

    LoadConstArrayInst(Opcode opcode, DataType::Type type, int32_t pc, Inst *input, uint32_t type_id,
                       RuntimeInterface::MethodPtr method, bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return DataType::NO_TYPE;
    }

    void DumpOpcode(std::ostream *out) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToLoadConstArray()->SetTypeId(GetTypeId());
        clone->CastToLoadConstArray()->SetMethod(GetMethod());
        return clone;
    }
};

// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class FillConstArrayInst : public NeedBarrierMixin<FixedInputsInst2>, public TypeIdMixin, public ImmediateMixin {
public:
    DECLARE_INST(FillConstArrayInst);
    using Base = NeedBarrierMixin<FixedInputsInst2>;
    using Base::Base;

    FillConstArrayInst(Opcode opcode, DataType::Type type, int32_t pc, Inst *input0, Inst *input1, uint32_t type_id,
                       RuntimeInterface::MethodPtr method, uint64_t imm, bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method), ImmediateMixin(imm)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return index == 0 ? DataType::REFERENCE : DataType::NO_TYPE;
    }

    void DumpOpcode(std::ostream *out) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToFillConstArray()->SetTypeId(GetTypeId());
        clone->CastToFillConstArray()->SetMethod(GetMethod());
        clone->CastToFillConstArray()->SetImm(GetImm());
        return clone;
    }
};

/// Checkcast
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class CheckCastInst : public OmitNullCheckMixin<ClassTypeMixin<NeedBarrierMixin<FixedInputsInst3>>>,
                      public TypeIdMixin {
public:
    DECLARE_INST(CheckCastInst);
    using Base = OmitNullCheckMixin<ClassTypeMixin<NeedBarrierMixin<FixedInputsInst3>>>;
    using Base::Base;

    CheckCastInst(Opcode opcode, DataType::Type type, int32_t pc, Inst *input0, Inst *input1, Inst *input2,
                  uint32_t type_id, RuntimeInterface::MethodPtr method, ClassType class_type, bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetInput(2, input2);
        SetClassType(class_type);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        ASSERT(GetInputsCount() == 3U);
        if (index < 2U) {
            return DataType::REFERENCE;
        }
        return DataType::NO_TYPE;
    }

    void DumpOpcode(std::ostream *out) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToCheckCast()->SetTypeId(GetTypeId());
        clone->CastToCheckCast()->SetMethod(GetMethod());
        ASSERT(clone->CastToCheckCast()->GetClassType() == GetClassType());
        ASSERT(clone->CastToCheckCast()->GetOmitNullCheck() == GetOmitNullCheck());
        return clone;
    }
};

/// Is instance
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class IsInstanceInst : public OmitNullCheckMixin<ClassTypeMixin<NeedBarrierMixin<FixedInputsInst3>>>,
                       public TypeIdMixin {
public:
    DECLARE_INST(IsInstanceInst);
    using Base = OmitNullCheckMixin<ClassTypeMixin<NeedBarrierMixin<FixedInputsInst3>>>;
    using Base::Base;

    IsInstanceInst(Opcode opcode, DataType::Type type, int32_t pc, Inst *input0, Inst *input1, Inst *input2,
                   uint32_t type_id, RuntimeInterface::MethodPtr method, ClassType class_type,
                   bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetInput(2, input2);
        SetClassType(class_type);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        ASSERT(GetInputsCount() == 3U);
        if (index < 2U) {
            return DataType::REFERENCE;
        }
        return DataType::NO_TYPE;
    }

    void DumpOpcode(std::ostream *out) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToIsInstance()->SetTypeId(GetTypeId());
        clone->CastToIsInstance()->SetMethod(GetMethod());
        ASSERT(clone->CastToIsInstance()->GetClassType() == GetClassType());
        ASSERT(clone->CastToIsInstance()->GetOmitNullCheck() == GetOmitNullCheck());
        return clone;
    }
};

/// Load data from constant pool.
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class LoadFromPool : public NeedBarrierMixin<FixedInputsInst1>, public TypeIdMixin {
public:
    DECLARE_INST(LoadFromPool);
    using Base = NeedBarrierMixin<FixedInputsInst1>;
    using Base::Base;

    LoadFromPool(Opcode opcode, DataType::Type type, int32_t pc, Inst *input, uint32_t type_id,
                 RuntimeInterface::MethodPtr method, bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return DataType::NO_TYPE;
    }

    void DumpOpcode(std::ostream *out) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        static_cast<LoadFromPool *>(clone)->SetTypeId(GetTypeId());
        static_cast<LoadFromPool *>(clone)->SetMethod(GetMethod());
        return clone;
    }
};

/// Load data from dynamic constant pool.
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class LoadFromPoolDynamic : public NeedBarrierMixin<FixedInputsInst1>, public TypeIdMixin {
public:
    DECLARE_INST(LoadFromPoolDynamic);
    using Base = NeedBarrierMixin<FixedInputsInst1>;
    using Base::Base;

    LoadFromPoolDynamic(Opcode opcode, DataType::Type type, int32_t pc, Inst *input, uint32_t type_id,
                        RuntimeInterface::MethodPtr method, bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
        SetNeedBarrier(need_barrier);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return DataType::ANY;
    }

    void DumpOpcode(std::ostream *out) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        static_cast<LoadFromPoolDynamic *>(clone)->SetTypeId(GetTypeId());
        static_cast<LoadFromPoolDynamic *>(clone)->SetMethod(GetMethod());
        return clone;
    }

    bool IsString() const
    {
        return GetField<StringFlag>();
    }

    void SetString(bool v)
    {
        SetField<StringFlag>(v);
    }

    void SetVnObject(VnObject *vn_obj) override;

protected:
    using StringFlag = LastField::NextFlag;
    using LastField = StringFlag;
};

/// Initialization or loading of the class.
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class ClassInst : public NeedBarrierMixin<FixedInputsInst1>, public TypeIdMixin {
public:
    DECLARE_INST(ClassInst);
    using Base = NeedBarrierMixin<FixedInputsInst1>;
    using Base::Base;

    ClassInst(Opcode opcode, DataType::Type type, int32_t pc, Inst *input, uint32_t type_id,
              RuntimeInterface::MethodPtr method, RuntimeInterface::ClassPtr klass, bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method), klass_(klass)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }

    void DumpOpcode(std::ostream *out) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        static_cast<ClassInst *>(clone)->SetTypeId(GetTypeId());
        static_cast<ClassInst *>(clone)->SetMethod(GetMethod());
        static_cast<ClassInst *>(clone)->SetClass(GetClass());
        return clone;
    }

    RuntimeInterface::ClassPtr GetClass() const
    {
        return klass_;
    }

    void SetClass(RuntimeInterface::ClassPtr klass)
    {
        klass_ = klass;
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        return DataType::NO_TYPE;
    }

private:
    RuntimeInterface::ClassPtr klass_ {nullptr};
};

/// Loading of the runtime class.
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class RuntimeClassInst : public NeedBarrierMixin<FixedInputsInst0>, public TypeIdMixin {
public:
    DECLARE_INST(RuntimeClassInst);
    using Base = NeedBarrierMixin<FixedInputsInst0>;
    using Base::Base;

    RuntimeClassInst(Opcode opcode, DataType::Type type, int32_t pc, uint32_t type_id,
                     RuntimeInterface::MethodPtr method, RuntimeInterface::ClassPtr klass, bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method), klass_(klass)
    {
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }

    void DumpOpcode(std::ostream *out) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        static_cast<RuntimeClassInst *>(clone)->SetTypeId(GetTypeId());
        static_cast<RuntimeClassInst *>(clone)->SetMethod(GetMethod());
        static_cast<RuntimeClassInst *>(clone)->SetClass(GetClass());
        return clone;
    }

    RuntimeInterface::ClassPtr GetClass() const
    {
        return klass_;
    }

    void SetVnObject(VnObject *vn_obj) override;

    void SetClass(RuntimeInterface::ClassPtr klass)
    {
        klass_ = klass;
    }

private:
    RuntimeInterface::ClassPtr klass_ {nullptr};
};

/// Get global var address inst
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class GlobalVarInst : public NeedBarrierMixin<FixedInputsInst2>, public TypeIdMixin {
public:
    DECLARE_INST(GlobalVarInst);
    using Base = NeedBarrierMixin<FixedInputsInst2>;
    using Base::Base;

    GlobalVarInst(Opcode opcode, DataType::Type type, uint32_t pc, uintptr_t address)
        : Base(opcode, type, pc), address_(address)
    {
    }

    GlobalVarInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, uint32_t type_id,
                  RuntimeInterface::MethodPtr method, uintptr_t address, bool need_barrier = false)
        : Base(opcode, type, pc), TypeIdMixin(type_id, method), address_(address)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetNeedBarrier(need_barrier);
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }

    void DumpOpcode(std::ostream *out) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        static_cast<GlobalVarInst *>(clone)->SetTypeId(GetTypeId());
        static_cast<GlobalVarInst *>(clone)->SetMethod(GetMethod());
        return clone;
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        if (index == 0) {
            return DataType::ANY;
        }
        return DataType::NO_TYPE;
    }

    uintptr_t GetAddress() const
    {
        return address_;
    }

private:
    uintptr_t address_ {0};
};

/// Get object pointer from the specific source.
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class LoadImmediateInst : public Inst {
public:
    DECLARE_INST(LoadImmediateInst);
    using Base = Inst;
    using Base::Base;

    enum class ObjectType { UNKNOWN, CLASS, METHOD, CONSTANT_POOL, STRING, PANDA_FILE_OFFSET, LAST };

    LoadImmediateInst(Opcode opcode, DataType::Type type, uint32_t pc, void *obj,
                      ObjectType obj_type = ObjectType::CLASS)
        : Base(opcode, type, pc), obj_(reinterpret_cast<uint64_t>(obj))
    {
        SetObjectType(obj_type);
    }

    LoadImmediateInst(Opcode opcode, DataType::Type type, uint32_t pc, uint64_t obj,
                      ObjectType obj_type = ObjectType::CLASS)
        : Base(opcode, type, pc), obj_(obj)
    {
        SetObjectType(obj_type);
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = Inst::Clone(target_graph);
        clone->CastToLoadImmediate()->SetObjectType(GetObjectType());
        clone->CastToLoadImmediate()->obj_ = obj_;
        return clone;
    }

    void *GetObject() const
    {
        return reinterpret_cast<void *>(obj_);
    }

    ObjectType GetObjectType() const
    {
        return GetField<ObjectTypeField>();
    }

    void SetObjectType(ObjectType obj_type)
    {
        SetField<ObjectTypeField>(obj_type);
    }

    void SetMethod(RuntimeInterface::MethodPtr obj)
    {
        ASSERT(GetObjectType() == ObjectType::METHOD);
        obj_ = reinterpret_cast<uint64_t>(obj);
    }

    RuntimeInterface::MethodPtr GetMethod() const
    {
        ASSERT(GetObjectType() == ObjectType::METHOD);
        return reinterpret_cast<RuntimeInterface::MethodPtr>(obj_);
    }

    bool IsMethod() const
    {
        return GetField<ObjectTypeField>() == ObjectType::METHOD;
    }

    void SetClass(RuntimeInterface::ClassPtr obj)
    {
        ASSERT(GetObjectType() == ObjectType::CLASS);
        obj_ = reinterpret_cast<uint64_t>(obj);
    }

    RuntimeInterface::ClassPtr GetClass() const
    {
        ASSERT(GetObjectType() == ObjectType::CLASS);
        return reinterpret_cast<RuntimeInterface::ClassPtr>(obj_);
    }

    bool IsConstantPool() const
    {
        return GetField<ObjectTypeField>() == ObjectType::CONSTANT_POOL;
    }

    uintptr_t GetConstantPool() const
    {
        ASSERT(GetObjectType() == ObjectType::CONSTANT_POOL);
        return static_cast<uintptr_t>(obj_);
    }

    bool IsClass() const
    {
        return GetField<ObjectTypeField>() == ObjectType::CLASS;
    }

    bool IsString() const
    {
        return GetField<ObjectTypeField>() == ObjectType::STRING;
    }

    uintptr_t GetString() const
    {
        ASSERT(GetObjectType() == ObjectType::STRING);
        return static_cast<uintptr_t>(obj_);
    }

    bool IsPandaFileOffset() const
    {
        return GetField<ObjectTypeField>() == ObjectType::PANDA_FILE_OFFSET;
    }

    uint64_t GetPandaFileOffset() const
    {
        ASSERT(GetObjectType() == ObjectType::PANDA_FILE_OFFSET);
        return obj_;
    }

    void SetVnObject(VnObject *vn_obj) override;

    void DumpOpcode(std::ostream * /* unused */) const override;

private:
    uint64_t obj_ {0};
    using ObjectTypeField = Base::LastField::NextField<ObjectType, MinimumBitsToStore(ObjectType::LAST)>;
    using LastField = ObjectTypeField;
};

/// Get function from the specific source.
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class FunctionImmediateInst : public Inst {
public:
    DECLARE_INST(FunctionImmediateInst);
    using Base = Inst;
    using Base::Base;

    FunctionImmediateInst(Opcode opcode, DataType::Type type, uint32_t pc, uintptr_t ptr)
        : Base(opcode, type, pc), function_ptr_(ptr)
    {
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = Inst::Clone(target_graph);
        clone->CastToFunctionImmediate()->function_ptr_ = function_ptr_;
        return clone;
    }

    uintptr_t GetFunctionPtr() const
    {
        return function_ptr_;
    }

    void SetFunctionPtr(uintptr_t ptr)
    {
        function_ptr_ = ptr;
    }

    void SetVnObject(VnObject *vn_obj) override;
    void DumpOpcode(std::ostream *out) const override;

private:
    uintptr_t function_ptr_ {0};
};

/// Get object from the specific source(handle).
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class LoadObjFromConstInst : public Inst {
public:
    DECLARE_INST(LoadObjFromConstInst);
    using Base = Inst;
    using Base::Base;

    LoadObjFromConstInst(Opcode opcode, DataType::Type type, uint32_t pc, uintptr_t ptr)
        : Base(opcode, type, pc), object_ptr_(ptr)
    {
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = Inst::Clone(target_graph);
        clone->CastToLoadObjFromConst()->object_ptr_ = object_ptr_;
        return clone;
    }

    uintptr_t GetObjPtr() const
    {
        return object_ptr_;
    }

    void SetObjPtr(uintptr_t ptr)
    {
        object_ptr_ = ptr;
    }

    void SetVnObject(VnObject *vn_obj) override;
    void DumpOpcode(std::ostream *out) const override;

private:
    uintptr_t object_ptr_ {0};
};

/// Select instruction
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class SelectInst : public ConditionMixin<InstWithOperandsType<FixedInputsInst<4U>>> {
public:
    DECLARE_INST(SelectInst);
    using Base = ConditionMixin<InstWithOperandsType<FixedInputsInst<4U>>>;
    using Base::Base;

    SelectInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, Inst *input2, Inst *input3,
               DataType::Type oper_type, ConditionCode cc)
        : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetInput(2, input2);
        SetInput(3, input3);
        SetOperandsType(oper_type);
        SetCc(cc);
        if (IsReferenceOrAny()) {
            SetFlag(inst_flags::REF_SPECIAL);
            // Select instruction cannot be generated before LICM where NO_HOIST flag is checked
            // Set it just for consistency
            SetFlag(inst_flags::NO_HOIST);
        }
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        if (index < 2U) {
            return GetType();
        }
        return GetOperandsType();
    }

    void DumpOpcode(std::ostream * /* unused */) const override;
    void SetVnObject(VnObject *vn_obj) override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        ASSERT(clone->CastToSelect()->GetCc() == GetCc());
        ASSERT(clone->CastToSelect()->GetOperandsType() == GetOperandsType());
        return clone;
    }
};

/// SelectImm with comparison with immediate
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class SelectImmInst : public InstWithOperandsType<ConditionMixin<FixedInputsInst3>>, public ImmediateMixin {
public:
    DECLARE_INST(SelectImmInst);
    using Base = InstWithOperandsType<ConditionMixin<FixedInputsInst3>>;
    using Base::Base;

    SelectImmInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, Inst *input2,
                  uint64_t imm, DataType::Type oper_type, ConditionCode cc)
        : Base(opcode, type, pc), ImmediateMixin(imm)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetInput(2, input2);
        SetOperandsType(oper_type);
        SetCc(cc);
        if (IsReferenceOrAny()) {
            SetFlag(inst_flags::REF_SPECIAL);
            SetFlag(inst_flags::NO_HOIST);
        }
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        if (index < 2U) {
            return GetType();
        }
        return GetOperandsType();
    }

    void DumpOpcode(std::ostream * /* unused */) const override;
    bool DumpInputs(std::ostream * /* unused */) const override;
    void SetVnObject(VnObject *vn_obj) override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        ASSERT(clone->CastToSelectImm()->GetCc() == GetCc());
        ASSERT(clone->CastToSelectImm()->GetOperandsType() == GetOperandsType());
        clone->CastToSelectImm()->SetImm(GetImm());
        return clone;
    }
};

/// Conditional jump instruction
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class IfInst : public InstWithOperandsType<ConditionMixin<FixedInputsInst2>> {
public:
    DECLARE_INST(IfInst);
    using Base = InstWithOperandsType<ConditionMixin<FixedInputsInst2>>;
    using Base::Base;

    IfInst(Opcode opcode, DataType::Type type, uint32_t pc, ConditionCode cc) : Base(opcode, type, pc)
    {
        SetCc(cc);
    }

    IfInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, DataType::Type oper_type,
           ConditionCode cc, RuntimeInterface::MethodPtr method = nullptr)
        : Base(opcode, type, pc), method_(method)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetOperandsType(oper_type);
        SetCc(cc);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return GetOperandsType();
    }

    void DumpOpcode(std::ostream * /* unused */) const override;

    void SetVnObject(VnObject *vn_obj) override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        ASSERT(static_cast<IfInst *>(clone)->GetCc() == GetCc());
        ASSERT(static_cast<IfInst *>(clone)->GetOperandsType() == GetOperandsType());
        static_cast<IfInst *>(clone)->SetMethod(GetMethod());
        return clone;
    }

    void SetMethod(RuntimeInterface::MethodPtr method)
    {
        method_ = method;
    }

    RuntimeInterface::MethodPtr GetMethod() const
    {
        return method_;
    }

private:
    RuntimeInterface::MethodPtr method_ {nullptr};
};

/// IfImm instruction with immediate
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class IfImmInst : public InstWithOperandsType<ConditionMixin<FixedInputsInst1>>, public ImmediateMixin {
public:
    DECLARE_INST(IfImmInst);
    using Base = InstWithOperandsType<ConditionMixin<FixedInputsInst1>>;
    using Base::Base;

    IfImmInst(Opcode opcode, DataType::Type type, uint32_t pc, ConditionCode cc, uint64_t imm)
        : Base(opcode, type, pc), ImmediateMixin(imm)
    {
        SetCc(cc);
    }

    IfImmInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input, uint64_t imm, DataType::Type oper_type,
              ConditionCode cc, RuntimeInterface::MethodPtr method = nullptr)
        : Base(opcode, type, pc), ImmediateMixin(imm), method_(method)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
        SetOperandsType(oper_type);
        SetCc(cc);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return GetOperandsType();
    }

    void DumpOpcode(std::ostream * /* unused */) const override;
    bool DumpInputs(std::ostream * /* unused */) const override;
    void SetVnObject(VnObject *vn_obj) override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        ASSERT(clone->CastToIfImm()->GetCc() == GetCc());
        ASSERT(clone->CastToIfImm()->GetOperandsType() == GetOperandsType());
        clone->CastToIfImm()->SetMethod(GetMethod());
        clone->CastToIfImm()->SetImm(GetImm());
        return clone;
    }

    BasicBlock *GetEdgeIfInputTrue();
    BasicBlock *GetEdgeIfInputFalse();

    void SetMethod(RuntimeInterface::MethodPtr method)
    {
        method_ = method;
    }

    RuntimeInterface::MethodPtr GetMethod() const
    {
        return method_;
    }

private:
    size_t GetTrueInputEdgeIdx();
    RuntimeInterface::MethodPtr method_ {nullptr};
};

/// Load element from a pair of values, using index as immediate
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class LoadPairPartInst : public FixedInputsInst1, public ImmediateMixin {
public:
    DECLARE_INST(LoadPairPartInst);
    using FixedInputsInst1::FixedInputsInst1;

    explicit LoadPairPartInst(Opcode opcode, uint64_t imm) : FixedInputsInst1(opcode), ImmediateMixin(imm) {}

    LoadPairPartInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input, uint64_t imm)
        : FixedInputsInst1(opcode, type, pc, input), ImmediateMixin(imm)
    {
    }

    uint32_t GetSrcRegIndex() const override
    {
        return GetImm();
    }

    bool DumpInputs(std::ostream * /* out */) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToLoadPairPart()->SetImm(GetImm());
        return clone;
    }

    uint32_t Latency() const override
    {
        return OPTIONS.GetCompilerSchedLatencyLong();
    }
};

/// Load a pair of consecutive values from array
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class LoadArrayPairInst : public NeedBarrierMixin<MultipleOutputMixin<FixedInputsInst2, 2U>> {
public:
    DECLARE_INST(LoadArrayPairInst);
    using Base = NeedBarrierMixin<MultipleOutputMixin<FixedInputsInst2, 2U>>;
    using Base::Base;

    LoadArrayPairInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1,
                      bool need_barrier = false)
        : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetNeedBarrier(need_barrier);
    }

    Inst *GetArray()
    {
        return GetInput(0).GetInst();
    }
    Inst *GetIndex()
    {
        return GetInput(1).GetInst();
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph)->CastToLoadArrayPair();
#ifndef NDEBUG
        for (size_t i = 0; i < GetDstCount(); ++i) {
            clone->SetDstReg(i, GetDstReg(i));
        }
#endif
        return clone;
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        switch (index) {
            case 0:
                return DataType::REFERENCE;
            case 1:
                return DataType::INT32;
            default:
                return DataType::NO_TYPE;
        }
    }

    uint32_t Latency() const override
    {
        return 0;
    }
};

/// Store a pair of consecutive values to array
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class StoreArrayPairInst : public NeedBarrierMixin<FixedInputsInst<4U>> {
public:
    DECLARE_INST(StoreVectorInst);
    using Base = NeedBarrierMixin<FixedInputsInst<4U>>;
    using Base::Base;

    StoreArrayPairInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, Inst *input2,
                       Inst *input3, bool need_barrier = false)
        : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetInput(2, input2);
        SetInput(3, input3);
        SetNeedBarrier(need_barrier);
    }

    Inst *GetArray()
    {
        return GetInput(0).GetInst();
    }
    Inst *GetIndex()
    {
        return GetInput(1).GetInst();
    }
    Inst *GetStoredValue(uint64_t index)
    {
        return GetInput(2U + index).GetInst();
    }
    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        switch (index) {
            case 0:
                return DataType::REFERENCE;
            case 1:
                return DataType::INT32;
            case 2U:
            case 3U:
                return GetType();
            default:
                return DataType::NO_TYPE;
        }
    }

    // StoreArrayPair call barriers twice,so we need to save input register for second call
    bool IsPropagateLiveness() const override
    {
        return GetType() == DataType::REFERENCE;
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }
};

/// Load a pair of consecutive values from array, using array index as immediate
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class LoadArrayPairInstI : public NeedBarrierMixin<MultipleOutputMixin<FixedInputsInst1, 2U>>, public ImmediateMixin {
public:
    DECLARE_INST(LoadArrayPairInstI);
    using Base = NeedBarrierMixin<MultipleOutputMixin<FixedInputsInst1, 2U>>;
    using Base::Base;

    explicit LoadArrayPairInstI(Opcode opcode, uint64_t imm) : Base(opcode), ImmediateMixin(imm) {}

    LoadArrayPairInstI(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input, uint64_t imm,
                       bool need_barrier = false)
        : Base(opcode, type, pc), ImmediateMixin(imm)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
        SetNeedBarrier(need_barrier);
    }

    Inst *GetArray()
    {
        return GetInput(0).GetInst();
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }
    bool DumpInputs(std::ostream * /* out */) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph)->CastToLoadArrayPairI();
        clone->SetImm(GetImm());
#ifndef NDEBUG
        for (size_t i = 0; i < GetDstCount(); ++i) {
            clone->SetDstReg(i, GetDstReg(i));
        }
#endif
        return clone;
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        if (index == 0) {
            return DataType::REFERENCE;
        }
        return DataType::NO_TYPE;
    }

    uint32_t Latency() const override
    {
        return 0;
    }
};

/// Store a pair of consecutive values to array, using array index as immediate
// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class StoreArrayPairInstI : public NeedBarrierMixin<FixedInputsInst3>, public ImmediateMixin {
public:
    DECLARE_INST(StoreArrayPairInstI);
    using Base = NeedBarrierMixin<FixedInputsInst3>;
    using Base::Base;

    explicit StoreArrayPairInstI(Opcode opcode, uint64_t imm) : Base(opcode), ImmediateMixin(imm) {}

    StoreArrayPairInstI(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1, Inst *input2,
                        uint64_t imm, bool need_barrier = false)
        : Base(opcode, type, pc), ImmediateMixin(imm)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetInput(2, input2);
        SetNeedBarrier(need_barrier);
    }

    Inst *GetArray()
    {
        return GetInput(0).GetInst();
    }
    Inst *GetFirstValue()
    {
        return GetInput(1).GetInst();
    }
    Inst *GetSecondValue()
    {
        return GetInput(2U).GetInst();
    }
    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        switch (index) {
            case 0:
                return DataType::REFERENCE;
            case 1:
            case 2U:
                return GetType();
            default:
                return DataType::NO_TYPE;
        }
    }

    // StoreArrayPairI call barriers twice,so we need to save input register for second call
    bool IsPropagateLiveness() const override
    {
        return GetType() == DataType::REFERENCE;
    }

    bool IsBarrier() const override
    {
        return Inst::IsBarrier() || GetNeedBarrier();
    }

    bool DumpInputs(std::ostream * /* out */) const override;

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        clone->CastToStoreArrayPairI()->SetImm(GetImm());
        return clone;
    }
};

/// CatchPhiInst instruction
class CatchPhiInst : public DynamicInputsInst {
public:
    DECLARE_INST(CatchPhiInst);
    using DynamicInputsInst::DynamicInputsInst;

    CatchPhiInst(Opcode opcode, DataType::Type type, uint32_t pc) : DynamicInputsInst(opcode, type, pc) {}

    const ArenaVector<const Inst *> *GetThrowableInsts() const
    {
        return throw_insts_;
    }

    const Inst *GetThrowableInst(size_t i) const
    {
        ASSERT(throw_insts_ != nullptr && i < throw_insts_->size());
        return throw_insts_->at(i);
    }

    void AppendThrowableInst(const Inst *inst);
    void ReplaceThrowableInst(const Inst *old_inst, const Inst *new_inst);
    void RemoveInput(unsigned index) override;

    bool IsAcc() const
    {
        return GetField<IsAccFlag>();
    }

    void SetIsAcc()
    {
        SetField<IsAccFlag>(true);
    }

protected:
    using IsAccFlag = LastField::NextFlag;
    using LastField = IsAccFlag;

private:
    size_t GetThrowableInstIndex(const Inst *inst)
    {
        ASSERT(throw_insts_ != nullptr);
        auto it = std::find(throw_insts_->begin(), throw_insts_->end(), inst);
        ASSERT(it != throw_insts_->end());
        return std::distance(throw_insts_->begin(), it);
    }

private:
    ArenaVector<const Inst *> *throw_insts_ {nullptr};
};

class TryInst : public FixedInputsInst0 {
public:
    DECLARE_INST(TryInst);
    using FixedInputsInst0::FixedInputsInst0;

    explicit TryInst(Opcode opcode, BasicBlock *end_bb = nullptr)
        : FixedInputsInst0(opcode, DataType::NO_TYPE, INVALID_PC), try_end_bb_(end_bb)
    {
    }

    void AppendCatchTypeId(uint32_t id, uint32_t catch_edge_index);

    const ArenaVector<uint32_t> *GetCatchTypeIds() const
    {
        return catch_type_ids_;
    }

    const ArenaVector<uint32_t> *GetCatchEdgeIndexes() const
    {
        return catch_edge_indexes_;
    }

    size_t GetCatchTypeIdsCount() const
    {
        return (catch_type_ids_ == nullptr ? 0 : catch_type_ids_->size());
    }

    Inst *Clone(const Graph *target_graph) const override;

    void SetTryEndBlock(BasicBlock *try_end_bb)
    {
        try_end_bb_ = try_end_bb;
    }

    BasicBlock *GetTryEndBlock() const
    {
        return try_end_bb_;
    }

private:
    ArenaVector<uint32_t> *catch_type_ids_ {nullptr};
    ArenaVector<uint32_t> *catch_edge_indexes_ {nullptr};
    BasicBlock *try_end_bb_ {nullptr};
};

TryInst *GetTryBeginInst(const BasicBlock *try_begin_bb);

/// Mixin for Deoptimize instructions
template <typename T>
class DeoptimizeTypeMixin : public T {
public:
    using T::T;

    void SetDeoptimizeType(DeoptimizeType deopt_type)
    {
        T::template SetField<DeoptimizeTypeField>(deopt_type);
    }

    void SetDeoptimizeType(Inst *inst)
    {
        switch (inst->GetOpcode()) {
            case Opcode::NullCheck:
                SetDeoptimizeType(DeoptimizeType::NULL_CHECK);
                break;
            case Opcode::BoundsCheck:
                SetDeoptimizeType(DeoptimizeType::BOUNDS_CHECK);
                break;
            case Opcode::ZeroCheck:
                SetDeoptimizeType(DeoptimizeType::ZERO_CHECK);
                break;
            case Opcode::NegativeCheck:
                SetDeoptimizeType(DeoptimizeType::NEGATIVE_CHECK);
                break;
            case Opcode::NotPositiveCheck:
                SetDeoptimizeType(DeoptimizeType::NEGATIVE_CHECK);
                break;
            case Opcode::CheckCast:
                SetDeoptimizeType(DeoptimizeType::CHECK_CAST);
                break;
            case Opcode::AnyTypeCheck: {
                SetDeoptimizeType(inst->CastToAnyTypeCheck()->GetDeoptimizeType());
                break;
            }
            case Opcode::AddOverflowCheck:
            case Opcode::SubOverflowCheck:
            case Opcode::NegOverflowAndZeroCheck:
                SetDeoptimizeType(DeoptimizeType::OVERFLOW);
                break;
            case Opcode::DeoptimizeIf:
                SetDeoptimizeType(static_cast<DeoptimizeTypeMixin *>(inst)->GetDeoptimizeType());
                break;
            case Opcode::Intrinsic:
                SetDeoptimizeType(DeoptimizeType::NOT_PROFILED);
                break;
            default:
                UNREACHABLE();
                break;
        }
    }

    DeoptimizeType GetDeoptimizeType() const
    {
        return T::template GetField<DeoptimizeTypeField>();
    }

protected:
    using DeoptimizeTypeField =
        typename T::LastField::template NextField<DeoptimizeType, MinimumBitsToStore(DeoptimizeType::COUNT)>;
    using LastField = DeoptimizeTypeField;
};

// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class DeoptimizeInst : public DeoptimizeTypeMixin<FixedInputsInst1> {
public:
    DECLARE_INST(DeoptimizeInst);
    using Base = DeoptimizeTypeMixin<FixedInputsInst1>;
    using Base::Base;

    DeoptimizeInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input,
                   DeoptimizeType deopt_type = DeoptimizeType::NOT_PROFILED)
        : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input);
        SetDeoptimizeType(deopt_type);
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        ASSERT(clone->CastToDeoptimize()->GetDeoptimizeType() == GetDeoptimizeType());
        return clone;
    }

    void DumpOpcode(std::ostream *out) const override;
};

// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class DeoptimizeIfInst : public DeoptimizeTypeMixin<FixedInputsInst2> {
    DECLARE_INST(DeoptimizeInst);
    using Base = DeoptimizeTypeMixin<FixedInputsInst2>;

public:
    using Base::Base;

    DeoptimizeIfInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1,
                     DeoptimizeType deopt_type)
        : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetDeoptimizeType(deopt_type);
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst::Clone(target_graph);
        ASSERT(clone->CastToDeoptimizeIf()->GetDeoptimizeType() == GetDeoptimizeType());
        return clone;
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        switch (index) {
            case 0:
                return GetInput(0).GetInst()->GetType();
            case 1:
                return DataType::NO_TYPE;
            default:
                UNREACHABLE();
        }
    }

    void DumpOpcode(std::ostream *out) const override;
};

// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class DeoptimizeCompareInst : public InstWithOperandsType<DeoptimizeTypeMixin<ConditionMixin<FixedInputsInst3>>> {
public:
    DECLARE_INST(DeoptimizeCompareInst);
    using Base = InstWithOperandsType<DeoptimizeTypeMixin<ConditionMixin<FixedInputsInst3>>>;
    using Base::Base;

    explicit DeoptimizeCompareInst(Opcode opcode, const DeoptimizeIfInst *deopt_if, const CompareInst *compare)
        : Base(opcode, deopt_if->GetType(), deopt_if->GetPc())
    {
        SetDeoptimizeType(deopt_if->GetDeoptimizeType());
        SetOperandsType(compare->GetOperandsType());
        SetCc(compare->GetCc());
    }

    DeoptimizeCompareInst(Opcode opcode, const DeoptimizeIfInst *deopt_if, const CompareInst *compare, Inst *input0,
                          Inst *input1, Inst *input2)
        : Base(opcode, deopt_if->GetType(), deopt_if->GetPc())
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetDeoptimizeType(deopt_if->GetDeoptimizeType());
        SetInput(0, input0);
        SetInput(1, input1);
        SetInput(2, input2);
        SetOperandsType(compare->GetOperandsType());
        SetCc(compare->GetCc());
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst3::Clone(target_graph);
        ASSERT(clone->CastToDeoptimizeCompare()->GetDeoptimizeType() == GetDeoptimizeType());
        ASSERT(clone->CastToDeoptimizeCompare()->GetOperandsType() == GetOperandsType());
        ASSERT(clone->CastToDeoptimizeCompare()->GetCc() == GetCc());
        return clone;
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        switch (index) {
            case 0:
            case 1:
                return GetInput(index).GetInst()->GetType();
            case 2U:
                return DataType::NO_TYPE;
            default:
                UNREACHABLE();
        }
    }

    void DumpOpcode(std::ostream *out) const override;
};

// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class DeoptimizeCompareImmInst : public InstWithOperandsType<DeoptimizeTypeMixin<ConditionMixin<FixedInputsInst2>>>,
                                 public ImmediateMixin {
public:
    DECLARE_INST(DeoptimizeCompareImmInst);
    using Base = InstWithOperandsType<DeoptimizeTypeMixin<ConditionMixin<FixedInputsInst2>>>;
    using Base::Base;

    explicit DeoptimizeCompareImmInst(Opcode opcode, const DeoptimizeIfInst *deopt_if, const CompareInst *compare,
                                      uint64_t imm)
        : Base(opcode, deopt_if->GetType(), deopt_if->GetPc()), ImmediateMixin(imm)
    {
        SetDeoptimizeType(deopt_if->GetDeoptimizeType());
        SetOperandsType(compare->GetOperandsType());
        SetCc(compare->GetCc());
    }

    DeoptimizeCompareImmInst(Opcode opcode, const DeoptimizeIfInst *deopt_if, const CompareInst *compare, Inst *input0,
                             Inst *input1, uint64_t imm)
        : Base(opcode, deopt_if->GetType(), deopt_if->GetPc()), ImmediateMixin(imm)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
        SetDeoptimizeType(deopt_if->GetDeoptimizeType());
        SetOperandsType(compare->GetOperandsType());
        SetCc(compare->GetCc());
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        auto clone = FixedInputsInst2::Clone(target_graph);
        ASSERT(clone->CastToDeoptimizeCompareImm()->GetDeoptimizeType() == GetDeoptimizeType());
        ASSERT(clone->CastToDeoptimizeCompareImm()->GetOperandsType() == GetOperandsType());
        ASSERT(clone->CastToDeoptimizeCompareImm()->GetCc() == GetCc());
        clone->CastToDeoptimizeCompareImm()->SetImm(GetImm());
        return clone;
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        switch (index) {
            case 0:
                return GetInput(0).GetInst()->GetType();
            case 1:
                return DataType::NO_TYPE;
            default:
                UNREACHABLE();
        }
    }

    void DumpOpcode(std::ostream *out) const override;
    bool DumpInputs(std::ostream *out) const override;
};

// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class ThrowInst : public FixedInputsInst2, public MethodDataMixin {
public:
    DECLARE_INST(ThrowInst);
    using Base = FixedInputsInst2;
    using Base::Base;

    ThrowInst(Opcode opcode, DataType::Type type, uint32_t pc, Inst *input0, Inst *input1) : Base(opcode, type, pc)
    {
        SetField<InputsCount>(INPUT_COUNT);
        SetInput(0, input0);
        SetInput(1, input1);
    }

    DataType::Type GetInputType(size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        if (index == 0) {
            return DataType::REFERENCE;
        }
        return DataType::NO_TYPE;
    }

    bool IsInlined() const
    {
        auto ss = GetSaveState();
        ASSERT(ss != nullptr);
        return ss->GetCallerInst() != nullptr;
    }

    Inst *Clone(const Graph *target_graph) const override
    {
        ASSERT(target_graph != nullptr);
        auto inst_clone = FixedInputsInst2::Clone(target_graph);
        auto throw_clone = static_cast<ThrowInst *>(inst_clone);
        throw_clone->SetCallMethodId(GetCallMethodId());
        throw_clone->SetCallMethod(GetCallMethod());
        return inst_clone;
    }
};

class BinaryOverflowInst : public IfInst {
public:
    DECLARE_INST(BinaryOverflowInst);
    using Base = IfInst;
    using Base::Base;

    BinaryOverflowInst(Opcode opcode, DataType::Type type, uint32_t pc, ConditionCode cc) : Base(opcode, type, pc, cc)
    {
        SetOperandsType(type);
    }

    DataType::Type GetInputType([[maybe_unused]] size_t index) const override
    {
        ASSERT(index < GetInputsCount());
        return GetType();
    }

    DataType::Type GetOperandsType() const override
    {
        return GetType();
    }
};

inline bool IsVolatileMemInst(Inst *inst)
{
    switch (inst->GetOpcode()) {
        case Opcode::LoadObject:
            return inst->CastToLoadObject()->GetVolatile();
        case Opcode::StoreObject:
            return inst->CastToStoreObject()->GetVolatile();
        case Opcode::LoadStatic:
            return inst->CastToLoadStatic()->GetVolatile();
        case Opcode::StoreStatic:
            return inst->CastToStoreStatic()->GetVolatile();
        case Opcode::UnresolvedStoreStatic:
        case Opcode::LoadResolvedObjectFieldStatic:
        case Opcode::StoreResolvedObjectFieldStatic:
            return true;
        default:
            return false;
    }
}

// Check if instruction is pseudo-user for mutli-output instruction
inline bool IsPseudoUserOfMultiOutput(Inst *inst)
{
    ASSERT(inst != nullptr);
    switch (inst->GetOpcode()) {
        case Opcode::LoadPairPart:
            return true;
        default:
            return false;
    }
}

template <typename InstType, typename... Args>
InstType *Inst::New(ArenaAllocator *allocator, Args &&...args)
{
    static_assert(alignof(InstType) >= alignof(uintptr_t));
    // NOLINTNEXTLINE(readability-braces-around-statements, bugprone-branch-clone)
    if constexpr (std::is_same_v<InstType, SpillFillInst>) {
        auto data = reinterpret_cast<uintptr_t>(allocator->Alloc(sizeof(InstType), DEFAULT_ALIGNMENT));
        ASSERT(data != 0);
        return new (reinterpret_cast<void *>(data)) InstType(allocator, std::forward<Args>(args)...);
        // NOLINTNEXTLINE(readability-braces-around-statements, readability-misleading-indentation)
    } else if constexpr (InstType::INPUT_COUNT == 0) {
        auto data = reinterpret_cast<uintptr_t>(allocator->Alloc(sizeof(InstType), DEFAULT_ALIGNMENT));
        ASSERT(data != 0);
        return new (reinterpret_cast<void *>(data)) InstType(std::forward<Args>(args)...);
        // NOLINTNEXTLINE(readability-braces-around-statements, readability-misleading-indentation)
    } else if constexpr (InstType::INPUT_COUNT == MAX_STATIC_INPUTS) {
        constexpr size_t OPERANDS_SIZE = sizeof(DynamicOperands);
        static_assert((OPERANDS_SIZE % alignof(InstType)) == 0);
        auto data = reinterpret_cast<uintptr_t>(allocator->Alloc(OPERANDS_SIZE + sizeof(InstType), DEFAULT_ALIGNMENT));
        ASSERT(data != 0);
        auto inst = new (reinterpret_cast<void *>(data + OPERANDS_SIZE)) InstType(std::forward<Args>(args)...);
        [[maybe_unused]] auto operands = new (reinterpret_cast<void *>(data)) DynamicOperands(allocator);
        static_cast<Inst *>(inst)->SetField<InputsCount>(InstType::INPUT_COUNT);
        return inst;
    } else {  // NOLINT(readability-misleading-indentation)
        constexpr size_t OPERANDS_SIZE = sizeof(Operands<InstType::INPUT_COUNT>);
        constexpr auto ALIGNMENT {GetLogAlignment(alignof(Operands<InstType::INPUT_COUNT>))};
        static_assert((OPERANDS_SIZE % alignof(InstType)) == 0);
        auto data = reinterpret_cast<uintptr_t>(allocator->Alloc(OPERANDS_SIZE + sizeof(InstType), ALIGNMENT));
        ASSERT(data != 0);
        auto operands = new (reinterpret_cast<void *>(data)) Operands<InstType::INPUT_COUNT>;
        unsigned idx = InstType::INPUT_COUNT - 1;
        for (auto &user : operands->users) {
            new (&user) User(true, idx--, InstType::INPUT_COUNT);
        }
        auto inst = new (reinterpret_cast<void *>(data + OPERANDS_SIZE)) InstType(std::forward<Args>(args)...);
        static_cast<Inst *>(inst)->SetField<InputsCount>(InstType::INPUT_COUNT);
        return inst;
    }
}

inline Inst *User::GetInput()
{
    return GetInst()->GetInput(GetIndex()).GetInst();
}

inline const Inst *User::GetInput() const
{
    return GetInst()->GetInput(GetIndex()).GetInst();
}

inline std::ostream &operator<<(std::ostream &os, const Inst &inst)
{
    inst.Dump(&os, false);
    return os;
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define INST_DEF(opcode, base, ...)                 \
    inline const base *Inst::CastTo##opcode() const \
    {                                               \
        ASSERT(GetOpcode() == Opcode::opcode);      \
        return static_cast<const base *>(this);     \
    }
OPCODE_LIST(INST_DEF)
#undef INST_DEF

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define INST_DEF(opcode, base, ...)            \
    inline base *Inst::CastTo##opcode()        \
    {                                          \
        ASSERT(GetOpcode() == Opcode::opcode); \
        return static_cast<base *>(this);      \
    }
OPCODE_LIST(INST_DEF)
#undef INST_DEF
}  // namespace panda::compiler

#endif  // COMPILER_OPTIMIZER_IR_INST_H_
