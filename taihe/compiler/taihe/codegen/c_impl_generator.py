# Copyright (c) 2025 Huawei Device Co., Ltd.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from taihe.codegen.abi_generator import (
    GlobFuncABIInfo,
    PackageABIInfo,
    TypeABIInfo,
)
from taihe.semantics.declarations import (
    GlobFuncDecl,
    PackageDecl,
    PackageGroup,
)
from taihe.utils.analyses import AbstractAnalysis, AnalysisManager
from taihe.utils.outputs import COutputBuffer, OutputManager


class PackageCImplInfo(AbstractAnalysis[PackageDecl]):
    def __init__(self, am: AnalysisManager, p: PackageDecl) -> None:
        super().__init__(am, p)
        self.header = f"{p.name}.impl.h"
        self.source = f"{p.name}.impl.c"


class GlobFuncCImplInfo(AbstractAnalysis[GlobFuncDecl]):
    def __init__(self, am: AnalysisManager, f: GlobFuncDecl) -> None:
        super().__init__(am, f)
        self.macro = f"TH_EXPORT_C_API_{f.name}"


class CImplHeadersGenerator:
    def __init__(self, tm: OutputManager, am: AnalysisManager):
        self.tm = tm
        self.am = am

    def generate(self, pg: PackageGroup):
        for pkg in pg.packages:
            self.gen_package_file(pkg)

    def gen_package_file(self, pkg: PackageDecl):
        pkg_c_impl_info = PackageCImplInfo.get(self.am, pkg)
        pkg_c_impl_target = COutputBuffer.create(
            self.tm, f"include/{pkg_c_impl_info.header}", True
        )
        pkg_abi_info = PackageABIInfo.get(self.am, pkg)
        pkg_c_impl_target.include("taihe/common.h")
        pkg_c_impl_target.include(pkg_abi_info.header)
        for func in pkg.functions:
            self.gen_func(func, pkg_c_impl_target)

    def gen_func(
        self,
        func: GlobFuncDecl,
        pkg_c_impl_target: COutputBuffer,
    ):
        func_abi_info = GlobFuncABIInfo.get(self.am, func)
        func_c_impl_info = GlobFuncCImplInfo.get(self.am, func)
        func_impl = "C_FUNC_IMPL"
        params = []
        args = []
        for param in func.params:
            type_abi_info = TypeABIInfo.get(self.am, param.ty_ref.resolved_ty)
            pkg_c_impl_target.include(*type_abi_info.impl_headers)
            params.append(f"{type_abi_info.as_param} {param.name}")
            args.append(param.name)
        params_str = ", ".join(params)
        args_str = ", ".join(args)
        if return_ty_ref := func.return_ty_ref:
            type_abi_info = TypeABIInfo.get(self.am, return_ty_ref.resolved_ty)
            pkg_c_impl_target.include(*type_abi_info.impl_headers)
            return_ty_name = type_abi_info.as_owner
        else:
            return_ty_name = "void"
        pkg_c_impl_target.writeln(
            f"#define {func_c_impl_info.macro}({func_impl}) \\",
            f"    {return_ty_name} {func_abi_info.mangled_name}({params_str}) {{ \\",
            f"        return {func_impl}({args_str}); \\",
            f"    }}",
        )


class CImplSourcesGenerator:
    def __init__(self, tm: OutputManager, am: AnalysisManager):
        self.tm = tm
        self.am = am

    def generate(self, pg: PackageGroup):
        for pkg in pg.packages:
            self.gen_package_file(pkg)

    def gen_package_file(self, pkg: PackageDecl):
        pkg_c_impl_info = PackageCImplInfo.get(self.am, pkg)
        pkg_c_impl_target = COutputBuffer.create(
            self.tm, f"temp/{pkg_c_impl_info.source}", False
        )
        pkg_c_impl_target.include(pkg_c_impl_info.header)
        for func in pkg.functions:
            self.gen_func(func, pkg_c_impl_target)

    def gen_func(
        self,
        func: GlobFuncDecl,
        pkg_c_impl_target: COutputBuffer,
    ):
        func_c_impl_info = GlobFuncCImplInfo.get(self.am, func)
        func_c_impl_name = f"{func.name}_impl"
        params = []
        for param in func.params:
            type_abi_info = TypeABIInfo.get(self.am, param.ty_ref.resolved_ty)
            params.append(f"{type_abi_info.as_param} {param.name}")
        params_str = ", ".join(params)
        if return_ty_ref := func.return_ty_ref:
            type_abi_info = TypeABIInfo.get(self.am, return_ty_ref.resolved_ty)
            return_ty_name = type_abi_info.as_owner
        else:
            return_ty_name = "void"
        pkg_c_impl_target.writeln(
            f"{return_ty_name} {func_c_impl_name}({params_str}) {{",
            f"    // TODO",
            f"}}",
            f"{func_c_impl_info.macro}({func_c_impl_name});",
        )
