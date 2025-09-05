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

#include "plugins/ets/runtime/ani/verify/env_ani_verifier.h"

#include "plugins/ets/runtime/ani/verify/ani_verifier.h"

namespace ark::ets::ani::verify {

EnvANIVerifier::EnvANIVerifier(ANIVerifier *verifier, const __ani_interaction_api *interactionAPI)
    : verifier_(verifier), interactionAPI_(interactionAPI)
{
    DoPushNatveFrame();
}

VEnv *EnvANIVerifier::GetEnv(ani_env *env)
{
    // NOTE: Use different ani_env * for each native frames, #28620
    return reinterpret_cast<VEnv *>(env);
}

VEnv *EnvANIVerifier::AttachThread(ani_env *env)
{
    // NOTE: Use different ani_env * for each native frames, #28620
    return reinterpret_cast<VEnv *>(env);
}

void EnvANIVerifier::DoPushNatveFrame()
{
    Frame frame {
        SubFrameType::NATIVE_FRAME, 0, {}, MakePandaUnique<ArenaAllocator>(SpaceType::SPACE_TYPE_INTERNAL), nullptr};
    frame.refsAllocator = frame.refsAllocatorHolder.get();
    frames_.push_back(std::move(frame));
}

void EnvANIVerifier::PushNatveFrame()
{
    ASSERT(!frames_.empty());
    DoPushNatveFrame();
}

std::optional<PandaString> EnvANIVerifier::PopNativeFrame()
{
    ASSERT(!frames_.empty());
    switch (frames_.back().frameType) {
        case SubFrameType::NATIVE_FRAME:
            frames_.pop_back();
            return {};
        case SubFrameType::LOCAL_SCOPE:
            return "It is necessary to call DestroyLocalScope(), after calling CreateLocalScope()";
        case SubFrameType::ESCAPE_LOCAL_SCOPE:
            return "It is necessary to call DestroyEscapeLocalScope(), after calling CreateEscapeLocalScope()";
    }
    UNREACHABLE();
    return "Verification logic was broken";
}

void EnvANIVerifier::CreateLocalScope()
{
    ASSERT(!frames_.empty());
    ArenaAllocator *allocator = frames_.back().refsAllocator;
    frames_.push_back(Frame {SubFrameType::LOCAL_SCOPE, 0, {}, {}, allocator});
}

std::optional<PandaString> EnvANIVerifier::DestroyLocalScope()
{
    ASSERT(!frames_.empty());
    switch (frames_.back().frameType) {
        case SubFrameType::NATIVE_FRAME:
            return "Illegal call DestroyLocalScope() without first calling CreateLocalScope()";
        case SubFrameType::LOCAL_SCOPE:
            frames_.pop_back();
            return {};
        case SubFrameType::ESCAPE_LOCAL_SCOPE:
            return "Illegal call DestroyLocalScope(), after calling CreateEscapeLocalScope()";
    }
    UNREACHABLE();
    return "Verification logic was broken";
}

void EnvANIVerifier::CreateEscapeLocalScope()
{
    ASSERT(!frames_.empty());
    ArenaAllocator *allocator = frames_.back().refsAllocator;
    frames_.push_back(Frame {SubFrameType::ESCAPE_LOCAL_SCOPE, 0, {}, {}, allocator});
}

std::optional<PandaString> EnvANIVerifier::DestroyEscapeLocalScope([[maybe_unused]] VRef *vref)
{
    ASSERT(!frames_.empty());
    switch (frames_.back().frameType) {
        case SubFrameType::NATIVE_FRAME:
            return "Illegal call DestroyEscapeLocalScope() without first calling CreateEscapeLocalScope()";
        case SubFrameType::LOCAL_SCOPE:
            return "Illegal call DestroyEscapeLocalScope() after calling CreateLocalScope()";
        case SubFrameType::ESCAPE_LOCAL_SCOPE: {
            // Drop top frame
            ASSERT(IsValidVerifiedRef(frames_.back(), vref));
            frames_.pop_back();
            return {};
        }
    }
    UNREACHABLE();
    return "Verification logic was broken";
}

VRef *EnvANIVerifier::DoAddLocalVerifiedRef(ani_ref ref, ANIRefType type)
{
    ASSERT(!frames_.empty());
    Frame &frame = frames_.back();

    VRef *vref = frame.refsAllocator->New<VRef>(ref, type);
    [[maybe_unused]] auto ret = frame.refs.emplace(vref);
    ASSERT(ret.second);
    return vref;
}

void EnvANIVerifier::DeleteLocalVerifiedRef(VRef *vref)
{
    ASSERT(!frames_.empty());
    Frame &frame = frames_.back();

    auto it = frame.refs.find(vref);
    ASSERT(it != frame.refs.cend());
    frame.refs.erase(it);
}

bool EnvANIVerifier::IsValidInCurrentFrame(VRef *vref)
{
    ASSERT(!frames_.empty());
    for (auto it = frames_.crbegin(); it != frames_.crend(); ++it) {
        if (IsValidVerifiedRef(*it, vref)) {
            return true;
        }
        if (it->frameType == SubFrameType::NATIVE_FRAME) {
            break;
        }
    }
    return false;
}

bool EnvANIVerifier::CanBeDeletedFromCurrentScope(VRef *vref)
{
    ASSERT(!frames_.empty());
    Frame &frame = frames_.back();
    return IsValidVerifiedRef(frame, vref);
}

/*static*/
bool EnvANIVerifier::IsValidVerifiedRef(const Frame &frame, VRef *vref)
{
    return frame.refs.find(vref) != frame.refs.cend();
}

VRef *EnvANIVerifier::AddGloablVerifiedRef(ani_ref gref)
{
    return verifier_->AddGloablVerifiedRef(gref);
}

void EnvANIVerifier::DeleteDeleteGlobalRef(VRef *vgref)
{
    verifier_->DeleteDeleteGlobalRef(vgref);
}

bool EnvANIVerifier::IsValidGlobalVerifiedRef(VRef *vgref)
{
    return verifier_->IsValidGlobalVerifiedRef(vgref);
}

}  // namespace ark::ets::ani::verify
