/*
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

// Keep this a separate classic script: a missing or invalid module must not
// prevent the shell from explaining why initialization failed.
async function bootstrapExplorer(loadMain = () => import('/assets/main.js')) {
  const setText = (id, text) => {
    const element = document.getElementById(id);
    if (element !== null) {
      element.textContent = text;
    }
  };
  const fail = (message) => {
    setText('file-name', 'ABC opening failed');
    setText('status-text', 'Local explorer initialization failed');
    setText('error-state', message);
    document.getElementById('error-state')?.classList.remove('hidden');
  };
  setText('status-text', 'Loading explorer JavaScript…');
  let timer;
  let main;
  let timedOut = false;
  try {
    main = await Promise.race([
      loadMain(),
      new Promise((_, reject) => {
        timer = setTimeout(() => {
          timedOut = true;
          reject(new Error('Module load timeout'));
        }, 30000);
      }),
    ]);
  } catch {
    // Import diagnostics can contain URLs; never echo them or the access token.
    fail(`Explorer JavaScript ${timedOut ? 'loading timed out' : 'could not be loaded'}. ` +
      'Reload the page; if this persists, restart the local explorer.');
    return;
  } finally {
    clearTimeout(timer);
  }
  try {
    // Backend progress has no overall deadline. Each API exchange is bounded.
    await main.start();
  } catch {
    fail('Explorer initialization failed. Reload the page; if this persists, restart the local explorer.');
  }
}

if (typeof document !== 'undefined') {
  void bootstrapExplorer();
}
