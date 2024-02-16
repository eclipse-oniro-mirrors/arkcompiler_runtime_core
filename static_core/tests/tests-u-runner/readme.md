# Universal test runner

## Prerequisites
-   Panda build
-   Python3 with required libs (`tqdm`, `dataclasses`, `python-dotenv`, etc). Make sure that `scripts/install-deps-ubuntu` has run with option `-i=test`
-   Suite `ets-es-checked` requires [node and some packages](#ets-es-checked-dependencies)

## Quick run

It is possible to run tests either using script `runner.sh` or `main.py` directly.

### Shell script
Script `runner.sh` activates the virtual environment with all required libraries
installed by `scripts/install-deps-ubuntu -i=test` and then runs test(s).
This way is preferable.

After install script finishes you can run

```bash
export PROJECT=/path/to/panda
export BUILD=/path/to/panda/build

$PROJECT/tests/tests-u-runner/runner.sh $PROJECT <test-suite-name> --build-dir $BUILD
```

List of possible values for `<test-suite-name>` is below.

### Python script
You can run `main.py` directly. In order to do so you have to activate
the virtual environment `$HOME/.venv-panda` manually or propose all required
libraries in your working environment. Then `main.py` will run test(s) for you.

```bash
export PROJECT=/path/to/panda
export BUILD=/path/to/panda/build

python3 $PROJECT/tests/tests-u-runner/main.py <test-suite-name> --build-dir $BUILD
```

### Supported test suites

-   `--parser` - parser (aka regression) tests
-   `--hermes` - Hermes JS runtime tests. To run tests from the Hermes suite, specify environment variables `HERMES_REVISION` and `HERMES_URL` in the `.env` file.
-   `--test262` - Test262 JS parser and runtime tests. To run tests from the test262 suite, specify environment variables `TEST262_REVISION` and `TEST262_URL` in the `.env` file.
-   `--ets-func-tests` - tests for ArkTS: standard library and ets func tests
-   `--ets-runtime` - ETS runtime tests
-   `--ets-cts` - CTS language specification tests
-   `--ets-es-checked` - ETS tests that cross validate results with ts

Additionally, a test suite can be specified with the option `--test-suite`: for example, for test262 `--test-suite test262`,
for ets-func-tests `--test-suite ets_func_tests`

### ArkTS important notes

The full command for running all stdlib tests and ets_func_tests would be:

`$PROJECT/tests/tests-u-runner/runner.sh $PROJECT --ets-func-tests --build-dir $BUILD`

or

`python3 $PROJECT/tests/tests-u-runner/main.py --ets-func-tests --build-dir $BUILD`

ArkTS STDLIB tests are generated automatically from Jinja2 template. By default, runner starts test code generation automatically.
As well test generator can be run as a standalone command, the corresponding procedure is described in plugins/ets/tests/stdlib-templates/readme.md

## Reproducing CI test run

In case of fail on CI runner will show its options - use it to run test locally.

For example, to run the test262 test suite with AOT FULL INLINE the build options could be following:

`$PROJECT/tests/tests-u-runner/runner.sh $PROJECT --test262 --gc-type=g1-gc --aot --aot-args='--compiler-inline-full-intrinsics=true' --aot-args='--compiler-memory-size-limit=4294967296' --build-dir $BUILD`

## Yaml configuration files

Any option can be set through yaml configuration file as `--config $YAML_CONFIG_FILE`.
The folder `cfg` contains several example config files which should be edited to set the real `$panda` and `$panda_build` paths,
and any custom paths.
tests/tests-u-runner/readme.md
To see full list of supported options use option `--generate-config $YAML_CONFIG_FILE`:

`$PROJECT/tests/tests-u-runner/runner.sh $PROJECT --generate-config $YAML_CONFIG_FILE <test-suite-name> --build-dir $BUILD`

The generated file will contain all supported options with default values.

> **Note**: if an option is set both in yaml config file and in command line interface the latter value will
> be applied. So cli value has a higher priority.

## Important runner options

Note: after `/` the option name from config file is specified.

-   `--build-dir`/`general.build` - the path to the compiled project. Referenced as $BUILD in this readme.
-   `--test-root`/`general.test-root` - the folder where test suite is located. It must exist before runner starts. By default, Hermes and Test262 test suites are downloaded to `/tmp/<test-suite-name>` folder. Then they are unpacking, transforming and copying to `<panda-build>\<test-suite-name>` folder
-   `--list-root`/`general.list-root` - the folder where test lists are located. It must exist before runner starts. By default, it's the test suite plugin.
-   `--work-dir`/`general.work-dir` - path to the working temp folder with gen, intermediate and report folders.
where tests are generated from templates. For Hermes and Test262 it's a path where tests transformed for run are copied.

## Test lists

Runner supports following kinds of test lists:

-   **excluded** test lists - tests specified here are excluded from execution. "Don't try to run"
-   **ignored** test lists - tests specified here run, but failures are ignored. Such lists are named "known failures list" or KFL as well.

Test list name usually has the following format: `<test-suite-name>[-<additional info>]-<kind>[-<architecture>][-<configuration>][-<sanitizer>][-<opt-level>][-REPEATS].txt`

-   `kind` is one of `excluded` or `ignored`
-   `architecture` is one of `ARM32`, `ARM64`, `AMD32`, `AMD64`.  If an architecture is set explicitly, the test list is applied only to this architecture. If none is set, the list is applied to any architecture.
-   `configuration` is one of `INT`, `AOT`, `AOT-FULL`, `IRTOC`, `LLVM`, `JIT` or other used value for `interpreter-type` option. If a configuration is set explicitly, the test list is applied only to this configuration. If none is set, the list is applied to any configuration.
-   `sanitizer` is one of `ASAN` or `TSAN`. If a sanitizer is set explicitly, the test list is applied only to this build configuration. If none is set, the list is applied to any configuration.
-   `opt-level` is `OLx`, where `x` is opt-level, usually 0 or 2.
-   `REPEATS` is set if the test list should apply to runs with the option `--jit-repeats` sets number of repeats more than 1.

Examples:

-   `test262-flaky-ignored-JIT.txt` - list of ignored tests from Test262 suite, applied only in JIT configuration. `flaky` is additional info, it's only for more clear description what tests are collected there.
-   `hermes-excluded.txt` - list of excluded tests from Hermes suite, applied in any configuration.
-   `parser-js-ignored.txt` - list of ignored tests from JS Parser suite, applied in any configuration.
-   `ets-func-tests-ignored.txt` - list of ignored tests in `ets-func-tests` test suite
-   `ets-cts-FastVerify-ignored-OL2.txt` - list of ignored tests for `ets-cts` test suite and for opt-level=2.  `FastVerify` is additional info.

In any test list the test is specified as path to the test file relative from the `test_root`: Examples:

-   array-large.js
-   built-ins/Date/UTC/fp-evaluation-order.js
-   tests/stdlib/std/math/sqrt-negative-01.ets

Test file specified in the option `--test-file` should be set in this format too.
By default, ignored or excluded lists are located in corresponded runner plugin folder (for example for ArkTS should be `$PROJECT/tests/tests-u-runner/runner/plugins/ets`)

All test lists are loaded automatically from the specified `LIST_ROOT` and based on following options:
- architecture:
  - from cli one of: `--test-list-arch=ARCH`, where ARCH is one of `amd64`, `arm32`, `arm64`
  - from config file: `test-lists.architecture: ARCH`. Values are the same.
- sanitizer:
  - from cli on of: `--test-list-san=SAN`, where SAN in one of `asan` or `tsan`
  - from config file: `test-lists.sanitizer: SAN`. Values are the same

> **Note**: these options just specifies what test lists to load and do not affect on how and where to start the runner
> itself and binaries used within.

## Utility runner options:

-   `--skip-test-lists`/`test-lists.skip-test-lists: True` - do not use ignored or excluded lists, run all available tests, report all found failures
-   `--test-list TEST_LIST`/`test-lists.explicit-list: TEST_LIST` - run tests ONLY listed in TEST\_LIST file.
-   `--test-file TEST_FILE`/`test-lists.explicit-file: TEST_FILE` - run ONLY ONE specified test. **Attention** - not test suite, but the single test from the suite.
-   `--update-excluded`/`test-lists.update-excluded: True` - regenerates excluded test lists
-   `--update-expected`/`test-lists.update-expected: True` - regenerates expected test lists (applied only for JS Parser test suite)
-   `--report-format`/`general.report-format` - specifies in what format to generate failure reports. By default, `md`. Possible value: `html`. As well reports in the plain text format with `.log` extension are always generated.
-   `--filter FILTER`/`test-lists.filter: FILTER` - test filter regexp
-   `--show-progress`/`general.show-progress: True` - show progress bar during test execution
-   `--time-report`/`time-report.enable: True` - generates report with grouping tests by execution time.

## Verbose and logging options:

-  `--verbose`, `-v` - Enable verbose output. Possible values one of:
   - `all` - the most detailed output,
   - `short` - test status and output.
   - if none specified (by default): only test status for new failed tests
   - in config file use `general.verbose` property with the save values.
-  `--verbose-filter` - Filter for what kind of tests to output stdout and stderr.
   Supported values:
   - `all` - for all executed tests both passed and failed.
   - `ignored` - for new failures and tests from ignored test lists including both passed and failed. '
   - `new` - only for new failures. Default value.
   - in config file use `general.verbose-filter` property with the same values.

## Generation options:

-   `--generate-only`/`general.generate-only` - only generate tests without running them. Tests are run as usual without this option.
-   `--force-generate`/`ets.force-generate` - force ETS tests generation from templates

## Timeout options:

All timeouts are set in seconds

-   `--es2panda-timeout`/`es2panda.timeout` - es2panda translator timeout
-   `--paoc-timeout`/`ark_aot.timeout` - paoc compiler timeout
-   `--timeout`/`ark.timeout` - ark runtime timeout

## Configuration options:

-   `--aot`/`ark_aot.enable: True` - use AOT compilation
-   `--jit`/`ark.jit.enable` - use JIT in interpreter
-   `--interpreter-type`/`ark.interpreter-type` - use explicitly specified interpreter for es2panda. Popular values: `irtoc`, `llvm`, `cpp`.

## JIT specific option
-  `--jit-preheat-repeats` - compound option to set additional parameters for JIT preheat actions.
   Example how to set and supported values (they are default as well): `"num_repeats=30,compiler_threshold=20"`.
   Works only with `--jit` options. If to use it with default values just set the empty string: `--jit-preheat-repeats=""`
- in config file use `ark.jit.num_repeats` and `ark.jit.compiler_threshold` with explicit values.

## Other options:

To get runner all options use: `main.py --help` or `runner.sh $PROJECT --help`

```
  --no-run-gc-in-place  enable --run-gc-in-place mode
  --gc-type GC_TYPE     Type of garbage collector
  --heap-verifier       Heap verifier options
  --no-bco              disable bytecodeopt
  --arm64-compiler-skip
                        use skiplist for tests failing on aarch64 in AOT or JIT mode
  --arm64-qemu          launch all binaries in qemu aarch64
  --arm32-qemu          launch all binaries in qemu arm
  --aot-args AOT_ARGS   Additional arguments that will passed to ark_aot
```

## Execution time report

It is possible to collect statistics how long separate tests work. In the result report tests are grouped by execution time.

The grouping edges are set in seconds. For example, the value `1 5 10` specifies 4 groups - less than 1 second, from 1 second to 5 seconds, from 5 seconds to 10 seconds and from 10 seconds and more. For the last group the report contains real durations.

-   Specify the option `--time-report`/`time-report.enable: True`
-   Specify the option `--time-edges TIME_EDGES` in the format "1 5 10"
or in yaml config file `time-report.time-edges: [1, 5, 10]` in the list of integers format.
-   After test run the short report will be output to the console
-   And full report will be created at the `<report_root>/time_report.txt`


## Linter and type checks

A script running linter and type checks starts as:

`$PROJECT/tests/tests-u-runner/linter.sh $PROJECT`

It performs checks by running `pylint` and `mypy`.

For `pylint` settings see `.pylintrc` file. For `mypy` settings see `mypy.ini` file.

## ETS ES checked dependencies
- ruby (installed by default with `$PROJECT/scripts/install-deps-ubuntu -i=dev`)
- node and ts-node, to install them see commands below

```bash
sudo apt-get -y install npm
sudo npm install -g n
sudo n install 21.4.0
cd $PROJECT/tests/tests-u-runner/tools/generate-es-checked
npm install
```
