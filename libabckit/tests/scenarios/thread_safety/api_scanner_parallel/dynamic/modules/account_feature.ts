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

import { Telemetry, authorize, Storage } from './platform_api';
import { geolocation } from '@ohos.geolocation';

export function createAccount() {
    Telemetry.report('account.create');
    authorize('account');
    Storage.put('account', 'created');
    geolocation.getCurrentLocation();
}

export function auditAccount() {
    Telemetry.report('account.audit');
    Storage.put('account', 'audited');
}

export class AccountController {
    handle() {
        authorize('account.controller');
        geolocation.getCurrentLocation();
    }

    localOnly() {
        return 'account.local';
    }
}
