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

#ifndef BYTECODE_EXPLORER_CORE_METADATA_METADATA_INDEX_SERVICE_H
#define BYTECODE_EXPLORER_CORE_METADATA_METADATA_INDEX_SERVICE_H

#include "core/common/task_runner.h"
#include "core/document/abc_document.h"
#include "core/metadata/metadata_reference_indexer.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace panda::bytecode_explorer {

namespace test {
class MetadataIndexServiceTestAccess;
}  // namespace test

class MetadataIndexService final {
public:
    static std::shared_ptr<MetadataIndexService> Create(std::shared_ptr<const AbcDocument> document,
                                                        std::shared_ptr<const MethodIndex> methods,
                                                        std::shared_ptr<const MetadataCatalog> catalog,
                                                        MetadataIndexLimits limits = {});
    static std::shared_ptr<MetadataIndexService> CreateForTesting(std::shared_ptr<const MetadataCatalog> catalog,
                                                                  std::vector<MethodMetadata> methods,
                                                                  MetadataReferenceIndexer::DecodeMethod decode,
                                                                  MetadataIndexLimits limits = {});

    ~MetadataIndexService();

    MetadataIndexService(const MetadataIndexService &) = delete;
    MetadataIndexService &operator=(const MetadataIndexService &) = delete;
    MetadataIndexService(MetadataIndexService &&) = delete;
    MetadataIndexService &operator=(MetadataIndexService &&) = delete;

    bool Start();
    MetadataIndexStatus Status() const;
    Result<MetadataPage<MetadataObject>> QueryObjects(const MetadataObjectQuery &query) const;
    Result<MetadataObjectDetail> QueryObject(const MetadataObjectKey &key) const;
    Result<MetadataPage<MetadataReference>> QueryReferences(const MetadataObjectKey &key,
                                                            std::optional<std::string> cursor, size_t limit) const;
    void WaitForIdleForTesting();
    uint64_t EstimatedPersistentBytesForTesting() const;

    // Passkey: only the Create factories can mint tokens, which keeps the constructor
    // usable with std::make_shared while staying unreachable for any external caller.
    class ConstructorToken {
        friend class MetadataIndexService;

    private:
        ConstructorToken() = default;
    };

    MetadataIndexService(ConstructorToken, std::shared_ptr<const AbcDocument> document,
                         std::shared_ptr<const MethodIndex> methodIndex, std::shared_ptr<const MetadataCatalog> catalog,
                         std::vector<MethodMetadata> methods, MetadataReferenceIndexer::DecodeMethod decode,
                         MetadataIndexLimits limits);

private:
    friend class test::MetadataIndexServiceTestAccess;

    struct ObjectKeyLess {
        bool operator()(const MetadataObjectKey &left, const MetadataObjectKey &right) const noexcept;
    };

    struct ReferenceSourceKey {
        MetadataObjectKey target;
        MetadataReferenceKind kind {MetadataReferenceKind::RECORD_NAME};
        uint64_t sourceOrder {0U};
        std::optional<uint64_t> sourceMethodOffset;
        std::optional<uint32_t> operandIndex;
        bool hasReliableRange {false};
        uint64_t rangeOffset {0U};
        uint64_t rangeSize {0U};
        std::optional<uint32_t> pc;
    };

    struct ReferenceSourceKeyLess {
        using is_transparent = void;

        bool operator()(const ReferenceSourceKey &left, const ReferenceSourceKey &right) const noexcept;
        bool operator()(const ReferenceSourceKey &left, const MetadataObjectKey &right) const noexcept;
        bool operator()(const MetadataObjectKey &left, const ReferenceSourceKey &right) const noexcept;
    };

    static ReferenceSourceKey MakeReferenceSourceKey(const MetadataReference &reference) noexcept;

    template <class Post>
    bool StartWithPost(Post &&post)
    {
        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            if (phase_ != MetadataIndexPhase::NOT_STARTED) {
                return false;
            }
            phase_ = MetadataIndexPhase::BUILDING;
        }
        try {
            std::forward<Post>(post)();
        } catch (const std::exception &error) {
            MarkFailedFromException("Cannot schedule metadata reference index: ", error.what());
            return false;
        } catch (...) {
            MarkFailedFromException("Cannot schedule metadata reference index: unknown failure");
            return false;
        }
        return true;
    }

    bool StartWithPublishHookForTesting(std::function<void()> beforePublish);
    void RunWithFailureBoundary() noexcept;
    void RunWithPublishHookFailureBoundary(std::function<void()> &beforePublish) noexcept;
    void Run();
    void RunWithPublish(MetadataReferenceIndexer::PublishBatch publish);
    void MarkFailed(std::string diagnostic) noexcept;
    void MarkFailedFromException(std::string_view prefix, const char *detail = nullptr) noexcept;
    bool Publish(MetadataIndexBatch batch);
    const std::vector<MethodMetadata> &Methods() const;

    MetadataPage<MetadataObject> CollectObjectPage(const MetadataObjectQuery &query, size_t startIndex) const;
    MetadataPage<MetadataReference> CollectReferencePage(const MetadataObjectKey &key, size_t limit,
                                                         size_t startIndex) const;
    bool IsReferenceOverridden(const MetadataReference &reference) const;
    void StagePublishedBatch(MetadataIndexBatch *batch,
                             std::map<MetadataObjectKey, MetadataObject, ObjectKeyLess> *stagedObjects,
                             std::map<ReferenceSourceKey, MetadataReference, ReferenceSourceKeyLess> *stagedReferences);
    template <typename StagedObjects, typename StagedReferences, typename Preparation>
    bool CommitPublish(StagedObjects *stagedObjects, StagedReferences *stagedReferences, Preparation *preparation);

    std::shared_ptr<const AbcDocument> document_;
    std::shared_ptr<const MethodIndex> methodIndex_;
    std::shared_ptr<const MetadataCatalog> catalog_;
    std::vector<MethodMetadata> methods_;
    MetadataReferenceIndexer::DecodeMethod decode_;
    MetadataIndexLimits limits_;

    mutable std::shared_mutex mutex_;
    MetadataIndexPhase phase_ {MetadataIndexPhase::NOT_STARTED};
    uint64_t revision_ {0U};
    size_t scannedMethods_ {0U};
    size_t objectCount_ {0U};
    size_t referenceCount_ {0U};
    uint64_t estimatedPersistentBytes_ {0U};
    bool budgetExhausted_ {false};
    std::map<MetadataObjectKey, MetadataObject, ObjectKeyLess> dynamicObjects_;
    std::map<ReferenceSourceKey, MetadataReference, ReferenceSourceKeyLess> dynamicReferences_;
    std::shared_ptr<const std::vector<std::string>> diagnostics_ {std::make_shared<const std::vector<std::string>>()};

    TaskRunner runner_;
    CancellationSource cancellation_;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_METADATA_METADATA_INDEX_SERVICE_H
