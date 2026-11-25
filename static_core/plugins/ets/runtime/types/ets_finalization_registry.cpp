/**
 * Copyright (c) 2025-2026 Huawei Device Co., Ltd.
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
#include "plugins/ets/runtime/types/ets_finreg_node.h"
#include "plugins/ets/runtime/types/ets_finalization_registry.h"

namespace ark::ets {
void EtsFinalizationRegistry::Enqueue(EtsFinRegNode *node, EtsFinalizationRegistry *finReg)
{
    Remove(node, finReg);
    ASSERT(node->GetNext() == nullptr);
    ASSERT(node->GetPrev() == nullptr);
    EtsFinRegNode *oldHead = finReg->GetFinalizationQueueHead();
    finReg->SetFinalizationQueueHead(node);
    node->SetNext(oldHead);
}

void EtsFinalizationRegistry::Remove(EtsFinRegNode *node, EtsFinalizationRegistry *finReg)
{
    auto *prev = node->GetPrev();
    auto *next = node->GetNext();
    if (node->HasToken()) {
        EtsInt bucketIdx = node->GetBucketIdx();
        ASSERT(0 <= bucketIdx && static_cast<uint32_t>(bucketIdx) < finReg->GetBuckets()->GetLength());
        if (prev == nullptr && next == nullptr) {
            finReg->GetBuckets()->Set(static_cast<uint32_t>(bucketIdx), nullptr);
        } else if (prev == nullptr) {
            finReg->GetBuckets()->Set(static_cast<uint32_t>(bucketIdx), next);
            ASSERT(next != nullptr);
            next->SetPrev(nullptr);
        } else if (next == nullptr) {
            ASSERT(prev != nullptr);
            prev->SetNext(nullptr);
        } else {
            prev->SetNext(next);
            next->SetPrev(prev);
        }

        finReg->DecrementMapSize();
    } else {
        if (prev == nullptr && next == nullptr) {
            finReg->SetNonUnregistrableList(nullptr);
        } else if (next == nullptr) {
            ASSERT(prev != nullptr);
            prev->SetNext(next);
        } else if (prev == nullptr) {
            finReg->SetNonUnregistrableList(next);
            ASSERT(next != nullptr);
            next->SetPrev(prev);
        } else {
            prev->SetNext(next);
            next->SetPrev(prev);
        }

        finReg->DecrementListSize();
    }
    node->SetPrev(nullptr);
    node->SetNext(nullptr);
}

void EtsFinalizationRegistry::Rehash(EtsTypedObjectArray<EtsFinRegNode> *newMap)
{
    auto *oldMap = GetBuckets();
    ObjectAccessor::SetObject(this, GetBucketsOffset(), newMap->GetCoreType());
    mapSize_ = 0;
    for (uint32_t oldMapIdx = 0; oldMapIdx < oldMap->GetLength(); oldMapIdx++) {
        auto *currNode = oldMap->Get(oldMapIdx);
        while (currNode != nullptr) {
            auto *next = currNode->GetNext();
            auto *token = currNode->GetToken()->GetReferent();
            currNode->SetPrev(nullptr);
            EtsFinRegNode *head = nullptr;
            if (token != nullptr) {
                uint32_t hash = token->GetHashCode();
                uint32_t bucketIdx = hash & (newMap->GetLength() - 1U);
                currNode->SetBucketIdx(static_cast<EtsInt>(bucketIdx));
                head = newMap->Get(bucketIdx);
                newMap->Set(bucketIdx, currNode);
                mapSize_++;
            } else {
                currNode->ClearToken();
                currNode->SetBucketIdx(-1);
                head = GetNonUnregistrableList();
                SetNonUnregistrableList(currNode);
                listSize_++;
            }
            currNode->SetNext(head);
            if (head != nullptr) {
                head->SetPrev(currNode);
            }
            currNode = next;
        }
    }
}

}  // namespace ark::ets
