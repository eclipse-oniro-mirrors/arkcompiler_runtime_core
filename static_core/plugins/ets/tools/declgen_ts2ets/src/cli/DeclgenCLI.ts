/*
 * Copyright (c) 2022-2024 Huawei Device Co., Ltd.
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

import * as fs from 'node:fs';

import type { OptionValues } from 'commander';
import { Command } from 'commander';

import { CLI } from './CLI';

export interface DeclgenCLIOptions {
  outDir?: string;
  inputFiles: string[];
  tsconfig?: string;
  inputDirs: string[];
}

export class DeclgenCLI extends CLI<DeclgenCLIOptions> {
  constructor() {
    super();
  }

  doRead(): string[] {
    void this;

    return process.argv;
  }

  doInit(): Command {
    void this;

    const cliParser = new Command();

    cliParser.name('declgen').description('Declarations generator for ets.');
    cliParser.option('-o, --out <outDir>', 'Directory where to put generated declarations.');
    cliParser.option('-p, --project <tsconfigPath>', 'path to TS project config file');
    cliParser.option(
      '-d, --dir <projectDir>',
      'Directory with TS files to generate declrartions from.',
      (val, prev) => {
        return prev.concat(val);
      },
      [] as string[]
    );
    cliParser.option(
      '-f, --file <fileName>',
      'TS file to generate declarations from.',
      (val, prev) => {
        return prev.concat(val);
      },
      [] as string[]
    );

    return cliParser;
  }

  doOptions(opts: OptionValues): DeclgenCLIOptions {
    void this;
    return {
      outDir: opts.out,
      inputFiles: opts.file,
      tsconfig: opts.project,
      inputDirs: opts.dir
    };
  }

  doValidate(opts: DeclgenCLIOptions): Error | undefined {
    void this;

    for (const entity of [...opts.inputDirs, ...opts.inputFiles]) {
      if (!fs.existsSync(entity)) {
        return new Error(`No entity <${entity}> exists!`);
      }
    }

    if (opts.tsconfig && !fs.existsSync(opts.tsconfig)) {
      return new Error(`Invalid tsconfig path! No file <${opts.tsconfig}> exists!`);
    }

    return undefined;
  }
}