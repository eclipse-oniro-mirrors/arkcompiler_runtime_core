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

import { CreateOrderHandler, CancelOrderHandler } from './modules/order_handlers';
import { OpenAccountHandler, CloseAccountHandler } from './modules/account_handlers';
import { AuthorizePaymentHandler, RefundPaymentHandler } from './modules/payment_handlers';
import { ScheduleDeliveryHandler, CancelDeliveryHandler } from './modules/delivery_handlers';

function exercise(label, handler, values) {
    print(label + '.valid=' + handler.handle(values, 1));
    print(label + '.invalid=' + handler.handle(values, values.length));
}

exercise('order.create', new CreateOrderHandler(), ['create-0', 'create-1']);
exercise('order.cancel', new CancelOrderHandler(), ['cancel-0', 'cancel-1']);
exercise('account.open', new OpenAccountHandler(), ['open-0', 'open-1']);
exercise('account.close', new CloseAccountHandler(), ['close-0', 'close-1']);
exercise('payment.authorize', new AuthorizePaymentHandler(), ['authorize-0', 'authorize-1']);
exercise('payment.refund', new RefundPaymentHandler(), ['refund-0', 'refund-1']);
exercise('delivery.schedule', new ScheduleDeliveryHandler(), ['schedule-0', 'schedule-1']);
exercise('delivery.cancel', new CancelDeliveryHandler(), ['delivery-cancel-0', 'delivery-cancel-1']);
