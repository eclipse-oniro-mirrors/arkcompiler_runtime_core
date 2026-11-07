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

import { submitOrder, cancelOrder } from './modules/order';
import { openAccount, closeAccount } from './modules/account';
import { authorizePayment, refundPayment } from './modules/payment';
import { scheduleDelivery, cancelDelivery } from './modules/delivery';

function invoke(label, operation) {
    try {
        print('result:' + label + '=' + operation());
    } catch (error) {
        print('outer:' + label + '=' + error);
    }
}

invoke('order.submit', submitOrder);
invoke('order.cancel', cancelOrder);
invoke('account.open', openAccount);
invoke('account.close', closeAccount);
invoke('payment.authorize', authorizePayment);
invoke('payment.refund', refundPayment);
invoke('delivery.schedule', scheduleDelivery);
invoke('delivery.cancel', cancelDelivery);
