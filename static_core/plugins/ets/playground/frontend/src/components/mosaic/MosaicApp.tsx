/*
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

import React, {useCallback, useLayoutEffect, useState} from 'react';
import {Mosaic, MosaicWindow} from 'react-mosaic-component';
import 'react-mosaic-component/react-mosaic-component.css';
import '@blueprintjs/core/lib/css/blueprint.css';
import '@blueprintjs/icons/lib/css/blueprint-icons.css';
import ArkTSEditor from '../../pages/codeEditor/ArkTSEditor';
import cx from 'classnames';
import { Tab, Tabs } from '@blueprintjs/core';
import styles from './styles.module.scss';
import ControlPanel from '../controlPanel/ControlPanel';
import {useDispatch, useSelector} from 'react-redux';
import {withDisasm} from '../../store/selectors/appState';
import DisasmCode from '../../pages/disasmView/DisasmCode';
import {AppDispatch} from '../../store';
import {fetchOptions} from '../../store/actions/options';
import {fetchSyntax} from '../../store/actions/syntax';
import {selectErrLogs, selectOutLogs} from '../../store/selectors/logs';
import LogsView from '../../pages/logs/LogsView';
import {clearErrLogs, clearOutLogs} from '../../store/actions/logs';

export type ViewId = 'code' | 'disasm' | 'logs' | 'ast';

const TITLE_MAP: Record<ViewId, string> = {
    code: 'Code editor',
    disasm: 'Disassembly',
    logs: 'Logs',
    ast: 'AST viewer'
};

const MosaicApp = (): JSX.Element => {
    const [activeTab, setActiveTab] = useState<'output' | 'err'>('output');
    const withDisasmRender = useSelector(withDisasm);
    const outLogsSelector = useSelector(selectOutLogs);
    const errLogsSelector = useSelector(selectErrLogs);
    const dispatch = useDispatch<AppDispatch>();

    useLayoutEffect(() => {
        dispatch(fetchOptions());
        dispatch(fetchSyntax());
    }, []);

    const handleTabChange = (newTab: 'output' | 'err'): void => {
        setActiveTab(newTab);
    };


    const header = useCallback((id: string): JSX.Element => {
        switch (id) {
            case 'code':
                return <ControlPanel />;
            case 'disasm':
                return <div>disasm</div>;
            case 'logs':
                return (<Tabs
                    id="logs-tabs"
                    onChange={(tabId): void => handleTabChange(tabId as 'output' | 'err')}
                    selectedTabId={activeTab}
                >
                    <Tab id="output" title="Output" className={styles.tab} />
                    <Tab id="err" title="Error" className={styles.tab} />
                </Tabs>);
            default:
                return <></>;
        }
    }, [activeTab]);

    const handleClearOutLogs = (): void => {
        dispatch(clearOutLogs());
    };
    const handleClearErrLogs = (): void => {
        dispatch(clearErrLogs());
    };

    return (
        <div
            id='app'
            className={cx({[styles.App]: true})}
            data-testid='mosaic-app-component'
        >
            <Mosaic<ViewId>
                className={cx('mosaic-blueprint-theme', styles.mosaicContainer)}
                blueprintNamespace="bp5"
                renderTile={(id, path): JSX.Element => (
                    <MosaicWindow<ViewId>
                        path={path}
                        // @ts-ignore
                        title={header(id)}
                        toolbarControls={<></>}
                        className={styles.window}
                    >
                        {TITLE_MAP[id] === 'Code editor' ? (
                            <div className={cx({
                                [styles.codeContainer]: true,
                                [styles.part]: withDisasmRender
                            })}>
                                <div className={styles.code}>
                                    <ArkTSEditor />
                                </div>
                                {withDisasmRender && <div className={styles.disasm}>
                                    <DisasmCode />
                                </div>}
                            </div>
                        ) : TITLE_MAP[id] === 'Logs' ? (
                            <div className={styles.tabContent}>
                                {activeTab === 'output' ? (
                                    <LogsView logArr={outLogsSelector} clearFilters={handleClearOutLogs} />
                                ) : (
                                    <LogsView logArr={errLogsSelector} clearFilters={handleClearErrLogs} />
                                )}
                            </div>
                        ) : (
                            <h1>{TITLE_MAP[id]}</h1>
                        )}
                    </MosaicWindow>
                )}
                initialValue={{
                    direction: 'row',
                    first: 'code',
                    second: 'logs',
                    splitPercentage: withDisasmRender ? 70 : 50
                }}
            />
        </div>
    );
};

export default MosaicApp;