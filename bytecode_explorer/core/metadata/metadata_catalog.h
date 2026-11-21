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

#ifndef BYTECODE_EXPLORER_CORE_METADATA_METADATA_CATALOG_H
#define BYTECODE_EXPLORER_CORE_METADATA_METADATA_CATALOG_H

#include "core/metadata/metadata_model.h"

#include <map>
#include <memory>
#include <vector>

namespace panda::panda_file {
class File;
}  // namespace panda::panda_file

namespace panda::bytecode_explorer {

struct PhysicalProgram;
class StructureIndex;
struct CatalogBuilderContext;
namespace test {
class MetadataCatalogTestAccess;
}  // namespace test

class MetadataCatalog final {
public:
    static Result<std::shared_ptr<const MetadataCatalog>> Build(const panda_file::File &file,
                                                                const PhysicalProgram &physical,
                                                                const StructureIndex &structure,
                                                                MetadataLimits limits = {});

    const std::vector<MetadataObject> &Objects() const
    {
        return objects_;
    }

    const MetadataObject *Find(const MetadataObjectKey &key) const;

    const std::vector<MetadataReference> &StructuralReferences() const
    {
        return structuralReferences_;
    }

    const std::vector<EntityId> &EntitiesForMethod(uint64_t methodOffset) const;

    bool IsPartial() const
    {
        return partial_;
    }

    uint64_t EstimatedBytes() const
    {
        return estimatedBytes_;
    }

    const std::vector<std::string> &Diagnostics() const
    {
        return diagnostics_;
    }

private:
    friend class test::MetadataCatalogTestAccess;
    friend struct CatalogBuilderContext;

    struct BuildObserver {
        size_t stringDecodeCount {0U};
        uint64_t peakConstructionBytes {0U};
        size_t referenceCountUpdates {0U};
        size_t requiredValidationReferenceVisits {0U};
        size_t diagnosticAdmissionChecks {0U};
    };

    static Result<std::shared_ptr<const MetadataCatalog>> BuildImpl(const panda_file::File &file,
                                                                    const PhysicalProgram &physical,
                                                                    const StructureIndex &structure,
                                                                    MetadataLimits limits, BuildObserver *observer);

    std::vector<MetadataObject> objects_;
    std::vector<MetadataReference> structuralReferences_;
    std::map<uint64_t, std::vector<EntityId>> methodEntities_;
    bool partial_ {false};
    uint64_t estimatedBytes_ {0U};
    std::vector<std::string> diagnostics_;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_METADATA_METADATA_CATALOG_H
