# 动态字节码 ABC size 优化：设计约束与验证

本 PR 基于 `4c3b3ada82`，以减少 ABC 体积为目标，保持语言结果、副作用及异常行为。
没有改变 opcode、指令操作数格式、ABC 布局或运行时对象布局。

## 提交组织

每个功能提交包含其实现和对应的单测；跨功能的 VM 执行回归单独提交。

| 逻辑单元 | 实现与边界 |
| --- | --- |
| 纯计算 DCE | 为 istrue、isfalse、stricteq、strictnoteq 标记 no_side_effect，复用 Cleanup |
| 窥孔优化 | 布尔包装、重复 ToNumeric、nullish 检查链 |
| 累加器复用 | 只读检查后的 acc 值复用，以及 catch 异常值直接供用户使用 |
| 数字常量折叠 | 扩展现有 SCCP，保留负零、合流和立即数编码约束 |
| range 参数 | 复用连续寄存器，并在现有干涉图着色中表达连续分配偏好 |
| CatchPhi 保存 | 在寄存器位置确定后，用必然成立的相等关系消除重复保存 |
| DynamicCse | 在上游现有实现中修复候选搜索退化和重复运行，保留原有复用条件 |
| 执行回归与说明 | 五种配置的 VM 断言及本文 |

累加器的两次改动归并在一起；range 复用及其着色偏好归并在一起；
DynamicCse 沿用上游结构，其候选管理修复单独提交。相互独立的优化保持分开，方便审查和回退。

## 优化约束

### 纯计算消除与窥孔优化

纯计算属性让 Cleanup 删除未使用的结果；本 PR 没有实现对象属性、数组、全局变量的通用死存储消除。
例如 `obj.x = 1; obj.x = 2` 的第一次写入可能触发 setter，不能按此规则删除。

布尔包装简化只接受已知的布尔生产者。反转严格比较时要求其没有其他用户，
修改 callruntime 布尔指令时保留对应指令族及立即数布局。
重复 ToNumeric 只在同块处理，保留第一次转换及其副作用和异常点；
类型证明只接受严格的整数、浮点 IR 类型，不把 BOOL、POINTER 或 ANY 当作 Number。

`x === null || x === undefined` 可折叠为 `x == null`，但必须使用同一 SSA 值，
中间块没有其他行为、目标没有需要调整的 Phi，且不穿越检查的异常区域边界。
规则依赖 Ark 的 nullish 比较语义，不能推广到任意宽松比较。

### 累加器与 range 参数

只有已确认保留 acc 的 `throw.undefinedifholewithname` 和两种宽度的
`throw.ifsupernotcorrectcall` 检查，才允许在生产者尚未标记为 ACC 时复用同一输入。
不能仅凭 acc-read 元数据推广到所有读取指令：字段写入指令在解释器中可能改写 acc。
真正写 acc、需要加载另一值或无法证明的路径仍阻止复用。
catch 入口的异常值只有能直接沿路径供用户使用时才省去 `sta`，否则保留寄存器保存。
`BytecodeGen::DoSta` 已统一处理 ACC 目标，无需在 CatchPhi 生成处重复判断。

range 复用要求整组输入已分配为连续寄存器。连续着色偏好不增加普通着色已使用的颜色范围，
不放宽干涉约束，也不改动预留临时区的现有流程。
range 专用辅助函数与调用处使用相同的 ENABLE_BYTECODE_OPT 条件编译范围，
避免关闭该功能后出现未使用函数警告。

### 数字常量与 SCCP

数字折叠沿用现有同类型常量限制；字符串、对象、BigInt、Symbol 不能作为普通数字运算折叠。
除零及部分非有限值运算继续保守保留。SCCP 在此流水线中受 `--branch-elimination` 控制，
单独设置 `--opt-level=2` 不保证执行 SCCP。

审查中修复了以下边界：

- **负零**：Number 运算以 double 计算；只有 int32 能精确表示且不是负零的结果才生成整数常量。
  原 PR 对 `-zero`、`zero * negative`、`minusFour % two` 曾产生正零。
  回归用倒数区分符号，避免比较右侧的 `-0` 也被错误折叠导致假通过。
- **合流**：常量格的 double 相等额外检查符号位，防止将可执行分支上的 +0 与 -0 合为同一常量。
  此处的格值判定不改变 JavaScript 严格相等的语义。
- **编码宽度**：INT64 IR 常量最终使用的 `ldai` 立即数仅为 int32；超出该范围的 Number 结果使用 double / `fldai`。
  此项是 INT64 路径加固，没有将它描述为已复现的普通 JS 错编译。

### CatchPhi 保存分析

分析在寄存器位置和并行搬运确定后运行，证明“源寄存器与目标槽位在所有进入路径上已经相等”。
合流取交集，循环计算不动点，entry / catch 入口清空事实。
并行 SpillFill 先读取旧状态，再统一执行 kill / gen；在分析收敛前不删除保存。
达到事实数、状态内存或访问次数上限时，保留全部保存。

任何源或目标寄存器写入都会使事实失效，包括浮点写入。
动态字节码的浮点活跃区间映射为普通虚拟寄存器位置，不能按机器浮点寄存器分类跳过。
原 PR 曾因此误删保存；测试分别覆盖源寄存器和目标槽位的浮点覆盖。

### DynamicCse 与 super 检查

纯表达式只接受 istrue、isfalse、严格相等和严格不等。
属性读取可能触发 getter，加法可能触发 valueOf；输入 SSA 相同不足以证明这些操作可复用。
表达式合并要求支配关系，并沿用“原结果有被当前指令支配的既有用途”的收益启发式。
IC slot 不参与纯表达式的语义键；反馈采样变化对 JIT/AOT 性能的影响需另外测量。

super 检查只消除同一 SSA this 值、相同模式且已有支配检查正常通过的重复检查。
模式 0/1 分别处理 this 未绑定/已绑定的状态约束；不将 super() 前后的不同 SSA 定义合并。
执行回归保留 this 先于 super()、重复 super()、基类副作用顺序和 catch 捕获首次错误后的行为。

走读 TODO 的处理状态：

- **R1，已修复**：同块相同表达式的旧候选持续累积，加上块内支配判断的链表扫描，
  在无 CSE 收益的重复比较构造上产生严重编译退化。
  现在预先记录块内顺序，并移除只有已访问块内用户的候选；Phi、CatchPhi 和跨块用户保守保留。
  判断使用当前用户链，考虑 ReplaceUsers 新增的用途，不缓存失效的最后使用位置。
  不设置任意候选数量上限，不牺牲原有 size 收益；不宣称所有跨块图都有线性复杂度。
  1600 次重复的回归不依赖固定毫秒阈值；另有新增用户和重复运行的回归。
- **R2，已澄清并补测试**：`HasDominatedUse` 是收益启发式，不保证替换后的全部用途都处在原活跃范围内。
  如 `p=StrictEq(x,y); q=StrictEq(x,y); Use(p); ...; Use(q)`，替换会延长 p 的区间。
  这本身不是语义错误，也不单独证明寄存器压力增加；本 PR 不放宽或收紧这一策略。
- **R3，已澄清并补测试**：`HasCompatibleEhEndpoints` 检查端点，不分析中间所有路径。
  当前纯操作白名单及已正常通过的相同 super 检查未发现因此产生的语义反例。
  扩大到可抛异常或依赖可变状态的操作时必须重新证明。

## 验证方法

从工作区根目录构建：

```sh
prebuilts/build-tools/linux-x86/bin/ninja -C out/x64.release \
  arkcompiler/ets_frontend/es2abc BytecodeOptimizerTest GraphInterfaceTest -j 8
```

从 `out/x64.release` 运行单测：

```sh
env LD_LIBRARY_PATH=arkcompiler/runtime_core:thirdparty/zlib:thirdparty/bounds_checking_function \
  ./tests/unittest/runtime_core/runtime_core/BytecodeOptimizerTest --gtest_brief=1
env LD_LIBRARY_PATH=arkcompiler/runtime_core:thirdparty/zlib:thirdparty/bounds_checking_function \
  ./tests/unittest/runtime_core/runtime_core/GraphInterfaceTest --gtest_brief=1
```

从工作区根目录执行 VM 回归：

```sh
python3 arkcompiler/runtime_core/bytecode_optimizer/tests/run_abcsize_runtime.py \
  --build-dir out/x64.release
```

脚本为各配置独立编译 ABC，检查同步断言及异步 reject 断言。
EH 开关显式传 true / false；当前前端默认开启，不能将省略参数视为关闭。

本机 x64 release 验证：118 个 BytecodeOptimizerTest、222 个 GraphInterfaceTest，通过共 340 个。

| 配置 | VM 执行 | 样例 ABC 字节 |
| --- | --- | ---: |
| opt-level=0 | PASS | 9744 |
| opt-level=2，SCCP 关，EH 关 | PASS | 8908 |
| opt-level=2，SCCP 开，EH 关 | PASS | 8808 |
| opt-level=2，SCCP 关，EH 开 | PASS | 8436 |
| opt-level=2，SCCP 开，EH 开 | PASS | 8340 |

这些大小是单个样例的整体结果，不能作为各 pass 的独立收益或业务包收益预测。

### 全量验证与边界

2026-10-09 在同步后的 standalone `x64.release` 上验证：

- runtime_core 的 21 个单测程序全部通过，共 1,120 项。
- ets_frontend 的 Compiler 1,079 项、FilesInfo 8 项、Bytecode 80 项全部通过。
  优化改变了 58 项汇编或体积统计预期，更新位于 ets_frontend 的配套提交。
- abc2program 的 6 份输出快照随优化结果更新；Verifier 两个破坏性校验用例
  改为定位真实方法／指令，避免依赖优化前的固定字节串，并断言数据确实被修改。
- Test262 使用 `--es2022 all --timeout=3000000`，分别运行普通和 `--abc2program` 混合编译。
  两套均执行 22,044 项：22,042 通过、2 失败，与同版本 frontend / runtime 配合
  纯上游 runtime_core 的对照结果一致。

两个剩余用例位于 `language/module-code/top-level-await/`：
`await-dynamic-import-resolution.js` 和 `dynamic-import-resolution.js`。
错误为导入模块的导出环境未初始化；关闭优化以及纯上游普通／混合编译单例仍失败。
具体模块调度根因尚未定位，未修改跳过清单，也未将失败计为通过。
默认严格模式及测试框架已有排除规则沿用官方脚本。

全量测试曾发现字段写入指令实际改写 acc，已收窄复用条件到三个已确认保留 acc 的检查。
实例、静态、公有、私有字段回归在上述五种配置中验证。
后续扩大范围必须核对解释器的实际行为，不能仅依赖 acc-read 元数据。

本次整理的命令、XML、逐用例失败和上游对照保存在
`/tmp/abcsize-wrapup-20261009/`；此前全量上游对照位于
`/tmp/abcsize-test262-20261009/`。
大包曾进行优化、发射和结构重读，未执行完整业务；此前大小数据不代表当前提交的测量。

## 保留的后续 TODO

### R4：range 覆盖和收益边界

以下是覆盖或收益限制，尚非已确认的错编译，不在本次整理中放宽：

- 当前按除 SaveState 外的全部输入检查连续性；输入中含 ACC 时可能保守拒绝。
  精确抽取实际 range 子区间需要逐 opcode 核对构图、调用语义和编码。
- 改色可能影响 Phi / 区间亲和布局；应同时统计 range 和其他搬运的净收益。
- 与其他 preference 组有外部干涉时，即使颜色未冲突也会拒绝，保留现有保守策略。
- range 临时区仍按输入数量预留，少 mov 不代表帧大小已缩减。
- 搜索窗口会多次扫描图和节点列表，其大型函数编译开销应单独测量，不能使用 R1 的计时作证据。

### T1：扩大窥孔及其他优化范围

此前的隔离原型没有进入生产实现。大包跳过 EH、关闭 SCCP 的实验结果：

| 原型 | 相对当前实现减少 ABC 字节 |
| --- | ---: |
| 更多已知 Boolean 结果后的 istrue 消除 | 932 |
| 转换组合及直接支配的跨块 ToNumeric | 0 |
| 严格比较 key 的输入 VN 排序 | 0 |
| typeof / callruntime 布尔转换加入 CSE | 0 |
| 同块 8 条 IR 内放宽已有用途条件 | 0 |

932 字节来自 305 条 callruntime.istrue 和 6 条 istrue；lda / sta / mov 数量不变。
原型的 57 个 IR 测试通过，VM 对 25 类值及组合执行 2,009 项断言，
覆盖 ±0、NaN、无穷、BigInt、Symbol、转换副作用与异常、getter、Proxy、TDZ。
当前实现、全部扩展和短距离 CSE 三种模式各在 EH 开 / 关执行，通过六组输出检查。
这些实验不构成完整业务或全平台等价证明。

后续候选包括已知结果类型分析（含 Phi）、布尔分支生成、累加器及低编号寄存器布局、
range 精确区间和临时区预留、CatchPhi 保存关系的保守复制传播。
必须先证明异常与副作用约束，再用最终 ABC 大小衡量收益；不因仍存在大量搬运指令而假定它们冗余。

尚未覆盖 ARM64、Debug/ASan、完整 GC 策略对照、JIT/AOT 矩阵及业务设备性能。
新增 pass 没有各自独立开关；opt-level=0 可整体回退，EH 和 branch-elimination 开关只控制相应范围。
