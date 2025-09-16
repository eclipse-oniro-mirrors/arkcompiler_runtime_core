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

#ifndef LIBABCKIT_SRC_HELPERS_COMMON_H
#define LIBABCKIT_SRC_HELPERS_COMMON_H

#include "metadata_inspect_impl.h"

namespace libabckit {

bool ModuleEnumerateNamespacesHelper(AbckitCoreModule *m, void *data, bool (*cb)(AbckitCoreNamespace *ns, void *data));
bool ModuleEnumerateClassesHelper(AbckitCoreModule *m, void *data, bool (*cb)(AbckitCoreClass *klass, void *data));
bool ModuleEnumerateInterfacesHelper(AbckitCoreModule *m, void *data,
                                     bool (*cb)(AbckitCoreInterface *iface, void *data));
bool ModuleEnumerateEnumsHelper(AbckitCoreModule *m, void *data, bool (*cb)(AbckitCoreEnum *enm, void *data));
bool ModuleEnumerateTopLevelFunctionsHelper(AbckitCoreModule *m, void *data,
                                            bool (*cb)(AbckitCoreFunction *function, void *data));
bool ModuleEnumerateFieldsHelper(AbckitCoreModule *m, void *data, bool (*cb)(AbckitCoreModuleField *field, void *data));
bool ModuleEnumerateAnnotationInterfacesHelper(AbckitCoreModule *m, void *data,
                                               bool (*cb)(AbckitCoreAnnotationInterface *ai, void *data));

bool NamespaceEnumerateNamespacesHelper(AbckitCoreNamespace *n, void *data,
                                        bool (*cb)(AbckitCoreNamespace *ns, void *data));
bool NamespaceEnumerateClassesHelper(AbckitCoreNamespace *n, void *data,
                                     bool (*cb)(AbckitCoreClass *klass, void *data));
bool NamespaceEnumerateInterfacesHelper(AbckitCoreNamespace *n, void *data,
                                        bool (*cb)(AbckitCoreInterface *iface, void *data));
bool NamespaceEnumerateEnumsHelper(AbckitCoreNamespace *n, void *data, bool (*cb)(AbckitCoreEnum *enm, void *data));
bool NamespaceEnumerateTopLevelFunctionsHelper(AbckitCoreNamespace *n, void *data,
                                               bool (*cb)(AbckitCoreFunction *function, void *data));
bool NamespaceEnumerateFieldsHelper(AbckitCoreNamespace *n, void *data,
                                    bool (*cb)(AbckitCoreNamespaceField *field, void *data));

bool ClassEnumerateMethodsHelper(AbckitCoreClass *klass, void *data,
                                 bool (*cb)(AbckitCoreFunction *method, void *data));
bool ClassEnumerateFieldsHelper(AbckitCoreClass *klass, void *data,
                                bool (*cb)(AbckitCoreClassField *field, void *data));
bool ClassEnumerateSubClassesHelper(AbckitCoreClass *klass, void *data,
                                    bool (*cb)(AbckitCoreClass *subClass, void *data));
bool ClassEnumerateInterfacesHelper(AbckitCoreClass *klass, void *data,
                                    bool (*cb)(AbckitCoreInterface *iface, void *data));

bool InterfaceEnumerateMethodsHelper(AbckitCoreInterface *iface, void *data,
                                     bool (*cb)(AbckitCoreFunction *method, void *data));
bool InterfaceEnumerateFieldsHelper(AbckitCoreInterface *iface, void *data,
                                    bool (*cb)(AbckitCoreInterfaceField *field, void *data));
bool InterfaceEnumerateSuperInterfacesHelper(AbckitCoreInterface *iface, void *data,
                                             bool (*cb)(AbckitCoreInterface *superInterface, void *data));
bool InterfaceEnumerateSubInterfacesHelper(AbckitCoreInterface *iface, void *data,
                                           bool (*cb)(AbckitCoreInterface *subInterface, void *data));
bool InterfaceEnumerateClassesHelper(AbckitCoreInterface *iface, void *data,
                                     bool (*cb)(AbckitCoreClass *klass, void *data));

bool EnumEnumerateMethodsHelper(AbckitCoreEnum *enm, void *data, bool (*cb)(AbckitCoreFunction *method, void *data));
bool EnumEnumerateFieldsHelper(AbckitCoreEnum *enm, void *data, bool (*cb)(AbckitCoreEnumField *field, void *data));

bool IsDynamic(AbckitTarget target);

AbckitType *GetOrCreateType(AbckitFile *file, AbckitTypeId id, size_t rank, AbckitCoreClass *klass);

}  // namespace libabckit

#endif  // LIBABCKIT_SRC_HELPERS_COMMON_H
