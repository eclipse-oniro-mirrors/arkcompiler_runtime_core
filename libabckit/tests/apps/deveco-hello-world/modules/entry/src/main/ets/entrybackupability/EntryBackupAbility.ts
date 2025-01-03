/**
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

import hilog from "@ohos:hilog";
import BackupExtensionAbility from "@ohos:application.BackupExtensionAbility";
import type { BundleVersion } from "@ohos:application.BackupExtensionAbility";
export default class EntryBackupAbility extends BackupExtensionAbility {
    async onBackup() {
        hilog.info(0x0000, 'testTag', 'onBackup ok');
    }
    async onRestore(bundleVersion: BundleVersion) {
        hilog.info(0x0000, 'testTag', 'onRestore ok %{public}s', JSON.stringify(bundleVersion));
    }
}