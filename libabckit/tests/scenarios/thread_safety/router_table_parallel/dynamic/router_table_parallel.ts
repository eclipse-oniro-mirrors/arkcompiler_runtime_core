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

import { IRouterHandler } from './modules/IRouterHandler';
import { orderRouterMap } from './modules/order_router_map';
import { accountRouterMap } from './modules/account_router_map';
import { paymentRouterMap } from './modules/payment_router_map';
import { deliveryRouterMap } from './modules/delivery_router_map';

function dispatch(domain: string, routerMap: Map<string, IRouterHandler>) {
    routerMap.forEach((handler: IRouterHandler, route: string) => {
        print('dispatch:' + domain + ':' + route);
        handler.handle();
    });
}

dispatch('order', orderRouterMap);
dispatch('account', accountRouterMap);
dispatch('payment', paymentRouterMap);
dispatch('delivery', deliveryRouterMap);
