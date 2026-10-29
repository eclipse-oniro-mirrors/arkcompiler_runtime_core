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

#ifndef ASSEMBLER_HOLE_CHECK_TARGET_H
#define ASSEMBLER_HOLE_CHECK_TARGET_H

#include <cstdint>

namespace panda::pandasm {

// Uniform identity of the object a `throw.undefinedifholewithname` (or the
// matching `stlexvar`/`stmodulevar`) checks.
//
// GUARANTEE of "same variable": the only reliable discriminator is an
// absolute, env-independent binding identity. The frontend assigns each
// variable binding a unique per-function `varId` (one per `binder::Variable`)
// and records it for every throw and store. `ldlexvar level, slot` is
// RELATIVE to the current lexical env, so two throws with the same
// `(level, slot)` can reference different bindings once a `newlexenv`/
// `poplexenv` appears between them; the variable name (`string_id`) collides
// under shadowing. Neither is a guarantee. The `varId` is: it names the
// binding itself, regardless of the env stack.
//
// `IsSameObject` therefore matches by `varId` alone (both sides must carry
// one). There is no fallback: if the frontend record is absent for an
// instruction, that instruction simply has no identity and the pass does not
// eliminate on its basis. `ldexternalmodulevar` is never recorded (its store
// lives in another module); its cross-module check runs at ins level in
// AsmEmitter::EmitPrograms instead.
class HoleCheckTarget {
public:
    enum Kind : uint8_t {
        INVALID = 0U,
        LEXICAL = 1U,
        MODULE_LOCAL = 2U,
    };

    constexpr HoleCheckTarget() = default;

    static constexpr HoleCheckTarget ForId(Kind kind, uint32_t varId)
    {
        return HoleCheckTarget(kind, varId);
    }

    static constexpr HoleCheckTarget Invalid()
    {
        return HoleCheckTarget();
    }

    constexpr bool IsValid() const
    {
        return varId_ != 0U;
    }

    constexpr Kind GetKind() const
    {
        return kind_;
    }

    constexpr bool HasVarId() const
    {
        return varId_ != 0U;
    }

    constexpr uint32_t GetVarId() const
    {
        return varId_;
    }

    // Two targets identify the same object iff both carry a varId and it
    // agrees — the absolute binding identity, the only guarantee.
    constexpr bool IsSameObject(const HoleCheckTarget &other) const
    {
        return varId_ != 0U && varId_ == other.varId_;
    }

    constexpr bool operator==(const HoleCheckTarget &other) const
    {
        return IsSameObject(other);
    }

    constexpr bool operator!=(const HoleCheckTarget &other) const
    {
        return !IsSameObject(other);
    }

private:
    explicit constexpr HoleCheckTarget(Kind kind, uint32_t varId) : kind_(kind), varId_(varId) {}

    Kind kind_ {INVALID};
    uint32_t varId_ {0U};  // 0 = absent (frontend-recorded absolute binding id)
};

}  // namespace panda::pandasm

#endif  // ASSEMBLER_HOLE_CHECK_TARGET_H
