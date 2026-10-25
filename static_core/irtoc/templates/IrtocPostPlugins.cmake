# Copyright (c) 2021-2024 Huawei Device Co., Ltd.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

get_target_property(PLUGIN_FILES_LIST interpreter_irt_combine IRTOC_PLUGIN_FILES)
get_target_property(BUILDER_INPUTS_LIST interpreter_irt_combine IRTOC_BUILDER_INPUTS)
set(GENERATED_DIRECTORY ${PANDA_BINARY_ROOT}/irtoc/generated)
file(MAKE_DIRECTORY "${GENERATED_DIRECTORY}")
set(PLUGINS_TXT ${GENERATED_DIRECTORY}/plugins.txt)
panda_gen_file(
    DATA ${GEN_PLUGIN_OPTIONS_YAML}
    TEMPLATE ${PANDA_ROOT}/irtoc/templates/plugins.txt.erb
    API ${PANDA_ROOT}/templates/plugin_options.rb
    EXTRA_DEPENDENCIES plugin_options_merge ${PLUGIN_FILES_LIST}
    OUTPUTFILE ${PLUGINS_TXT}
)

set(IRTOC_BUILDER_INPUTS_TXT ${GENERATED_DIRECTORY}/irtoc_builder_inputs.txt)
set(IRTOC_BUILDER_INPUTS_CONTENT "")
if (BUILDER_INPUTS_LIST)
    list(JOIN BUILDER_INPUTS_LIST "\n" IRTOC_BUILDER_INPUTS_CONTENT)
    set(IRTOC_BUILDER_INPUTS_CONTENT "${IRTOC_BUILDER_INPUTS_CONTENT}\n")
endif()
file(WRITE ${IRTOC_BUILDER_INPUTS_TXT} "${IRTOC_BUILDER_INPUTS_CONTENT}")

add_custom_target(irtoc_builder_inputs_txt
    DEPENDS ${IRTOC_BUILDER_INPUTS_TXT}
)
add_dependencies(irtoc_builder_inputs_txt interpreter_irt_combine)
add_dependencies(panda_gen_files irtoc_builder_inputs_txt)

add_custom_target(irtoc_plugins_txt
    DEPENDS plugin_options_gen ${PLUGINS_TXT}
)
add_dependencies(irtoc_plugins_txt interpreter_irt_combine)
add_dependencies(panda_gen_files irtoc_plugins_txt)
