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
#include "abckit_wrapper/file_view.h"
#include "class_hierarchy_builder.h"
#include "logger.h"

AbckitWrapperErrorCode abckit_wrapper::FileView::Init(const std::string_view &path)
{
    this->file_ = std::make_unique<abckit::File>(path);
    for (auto &coreModule : file_->GetModules()) {
        LOG_I << "Found Module:" << coreModule.GetName();
        auto module = std::make_unique<Module>(coreModule, this->objectPool_);
        module->owningModule_ = module.get();
        const auto rc = module->Init();
        if (ABCKIT_WRAPPER_ERROR(rc)) {
            LOG_E << "Failed to initialize module:" << rc;
            return rc;
        }
        this->moduleTable_.emplace(module->GetFullyQualifiedName(), module.get());
        this->objectPool_.Add(std::move(module));
    }

    ClassHierarchyBuilder classHierarchyBuilder(*this);
    auto rc = classHierarchyBuilder.Build();
    if (ABCKIT_WRAPPER_ERROR(rc)) {
        LOG_E << "Failed to build class hierarchy:" << rc;
        return rc;
    }

    return ERR_SUCCESS;
}

std::optional<abckit_wrapper::Module *> abckit_wrapper::FileView::GetModule(const std::string &moduleName) const
{
    if (this->moduleTable_.find(moduleName) == this->moduleTable_.end()) {
        return std::nullopt;
    }

    return this->moduleTable_.at(moduleName);
}

std::optional<abckit_wrapper::Object *> abckit_wrapper::FileView::GetObject(const std::string &objectName) const
{
    return this->objectPool_.Get(objectName);
}

bool abckit_wrapper::FileView::ModulesAccept(ModuleVisitor &visitor)
{
    for (auto &[_, module] : this->moduleTable_) {
        if (!module->Accept(visitor)) {
            return false;
        }
    }

    return true;
}

void abckit_wrapper::FileView::ClearObjectsData()
{
    this->objectPool_.ClearObjectsData();
}

void abckit_wrapper::FileView::Save(const std::string_view &path) const
{
    if (this->file_) {
        this->file_->WriteAbc(path);
    }
}
