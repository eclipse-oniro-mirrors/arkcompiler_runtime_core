# Ark 字节码浏览器（Ark Bytecode Explorer）

[English](README.md)

Ark 字节码浏览器是一个**离线的、基于浏览器的方舟字节码（`.abc`）查看器**。单个本地二进制即可在浏览器中打开交互式工作台：浏览文件的语义结构、阅读带指令级详情的反汇编、查看可交互的控制流图、检索元数据交叉引用与原始十六进制字节——零环境准备，数据不出本机。

## 特性

- **单二进制、完全离线**——网页 UI 内嵌于可执行文件，由只绑定 `127.0.0.1` 的回环 HTTP 服务提供；无任何外部网络访问。
- **六个联动视图**——结构树、带指令检查器的反汇编、可交互控制流图、带交叉引用的元数据浏览器、十六进制查看器、作用域统计；各视图间的选中状态保持同步。
- **页面内打开与切换文档**——无参启动进入空态页，通过原生文件对话框打开 ABC 文件，从服务端最近列表重开文档，原位切换文档（切换失败时保持原文档继续打开）。
- **只读且防御性**——工具绝不写 ABC 文件；损坏的实体数据以诊断信息呈现，而不是让进程终止。

## 如何下载与构建

### 下载代码:

```sh
repo init -u https://gitee.com/ark-standalone-build/manifest.git -b master
repo sync -c -j8
repo forall -c 'git lfs pull'
./prebuilts_download.sh
```

### 构建:

```sh
# Linux 平台（在 Linux 上构建）
./ark.py x64.release ark_bytecode_explorer --gn-args="is_standard_system=true enable_cmc_gc=false"
# Windows 平台（在 Linux 上交叉构建）
./ark.py mingw_x86_64.release ark_bytecode_explorer --gn-args="is_standard_system=true enable_cmc_gc=false"
```

产物位于 `out/<target>/arkcompiler/runtime_core/ark_bytecode_explorer`（Windows 下为 `.exe`）。

## 如何运行

```sh
BEX=out/x64.release/arkcompiler/runtime_core/ark_bytecode_explorer

$BEX file.abc    # 为 file.abc 打开工作台（自动打开浏览器标签页）
$BEX             # 空启动：从空态页打开文件并原位切换
$BEX --no-open   # 只打印本地 URL，不打开浏览器
```

选项：

- `-h`、`--help`——显示帮助，不打开 ABC
- `--version`——显示版本，不打开 ABC
- `--no-open`——只打印本地 URL，不打开浏览器
- `--dump-structure`——无头模式：打印语义结构树后退出
- `--show-internals`——结构输出中包含编译器生成与 ABC 内部节点

## 测试

```sh
# C++ 单元测试
ninja -C out/x64.release ark_bytecode_explorer_tests
./out/x64.release/tests/unittest/runtime_core/runtime_core/ark_bytecode_explorer_tests

# 前端单元测试（纯 node，无需浏览器）
cd arkcompiler/runtime_core/bytecode_explorer/web/frontend
node --test tests/*.test.mjs
```
