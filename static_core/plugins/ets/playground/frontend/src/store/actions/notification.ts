/*
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

import { createAsyncThunk } from '@reduxjs/toolkit';
import { INotification } from '../../models/notifications';
import { addMessage, removeMessage } from '../slices/notifications';

const DEFAULT_NOTIFICATION_DURATION = 3000;

export const getMessageId = () => (new Date()).getTime();

export const showMessage = createAsyncThunk(
    '@notification/showMessage',
    async (data: INotification, thunkAPI) => {
        const id = data.id || getMessageId();
        thunkAPI.dispatch(addMessage({ ...data, id }));
        if (data.duration === undefined || data.duration > 0) {
            setTimeout(() => {
                thunkAPI.dispatch(removeMessage(id));
            }, data.duration || DEFAULT_NOTIFICATION_DURATION);
        }
    },
);
