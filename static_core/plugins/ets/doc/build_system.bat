@rem Copyright (c) 2024 Huawei Device Co., Ltd.
@rem Licensed under the Apache License, Version 2.0 (the "License");
@rem you may not use this file except in compliance with the License.
@rem You may obtain a copy of the License at
@rem 
@rem http://www.apache.org/licenses/LICENSE-2.0
@rem 
@rem Unless required by applicable law or agreed to in writing, software
@rem distributed under the License is distributed on an "AS IS" BASIS,
@rem WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
@rem See the License for the specific language governing permissions and
@rem limitations under the License.

@echo off
md __build
cd system_arkts
sphinx-build -n -b latex . ..\__build
cd ..\__build
latexmk -f -silent -pdf -dvi- -ps- *.tex
md ..\system_arkts\build
move *.pdf ..\system_arkts\build
cd ..
rmdir /S /Q __build
