@echo off
goto :start
Copyright (c) 2021-2023 Huawei Device Co., Ltd.
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
:start

@echo off
echo Building all ArkTS docs ...
call build-cookbook.bat 
call build-stdlib.bat 
call build-tutorial.bat 
call build-spec.bat 
echo all pdf files are in appropriate 'build' sub-folder for every document
