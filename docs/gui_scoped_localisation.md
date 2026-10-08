# 作用域本地化：在面板里显示脚本算出的数值（Stellaris 4.5.2）

状态标记：✅ 在运行中的游戏里实测 / 读到了反汇编；⚠ 推断或没验证。
前置阅读：`gui_native_system.md` §3.3（本地化命令与 scope）、`gui_plugin_api_investigation.md` §5.3 的 T2。
代码：guidll 仓库（`Yidhar/guidll`）的 `src/loc.cpp`、`src/core.cpp`；测试内容在 `Yidhar/guidll-test-mod`。

## 0. 结论

**做成了，而且只需要调引擎的一个函数。** 宿主把一段带 `[Root.xxx]` 的本地化文字连同玩家国家的 `CEventScope` 交给 `CGameText::ProcessWithScope`，引擎自己求值。不用解析变量、不用读旗标、不用钩任何东西。

1. ✅ **三种"脚本算出的值"都能显示**：存下来的变量（`[Root.guidll_test_counter]`）、带触发器的 `scripted_loc`（`[Root.GuidllTestFlag]`）、脚本值（`scripted_loc` 里 `value = value:xxx`，`[Root.GuidllTestValue]`），以及引擎自带的命令（`[Root.GetName]`）。值随脚本状态变化：计数器按三次 `+1` 变成 3；设置国家旗标后脚本值从 10 变成 15，旗标文字从"未设置"变成"已设置"。
2. ✅ **函数的 Windows 地址由 dumper 的指纹找到**（不是手写）：`CGameText_ProcessWithScope = 0x5E9350`，`CGameText_ctor = 0x5E4A60`。指纹见 `tools/sdk_dumper/CHANGELOG.md`。
3. ✅ **便宜**：一次求值 0.5–2 微秒（含造 scope 和释放；一句带四个引用的话 2.1 微秒）。整个声明面板每帧平均 60 微秒。
4. ✅ **对 mod 作者没有新语法**：声明面板的所有文字元素（`text`、`label`、按钮文字、`badge` 的两句）本来就是 loc 键，现在里面写 `[Root.xxx]` 就行。对插件开发者是接口里追加的 `localize(key, out, cap)`。
5. ⚠ **限制**：只有玩家国家这一个 scope（`This = From = Root = 玩家国家`）。选中的行星、舰队等的 scope 还没做（§6）。

## 1. 引擎里的东西

| 函数（Linux 符号） | Windows RVA | 怎么找到的 | 作用 |
|---|---|---|---|
| `CGameText::ProcessWithScope(CString const&, CEventScope const&)` | `0x5E9350` | `CGameText_ctor` 的调用者里形状唯一的一个（§2） | 本地化文字 + scope → 求值后的文字。**宿主调这个** |
| `CGameText::CGameText()` | `0x5E4A60` | 指纹：vtable、`+8` 和 `+0x190` 处的 `0x31` | 构造"文字处理上下文"（约 `0xA00` 字节，含 49 种 scope 类型的四张函数表） |
| （填表函数） | `0x5E8C90` | 构造后立刻调用（反汇编） | ⚠ 推断：对应 Linux 版里内联在 `ProcessWithScope` 开头的一大串 `local_xxx = GetCountryPromotionTargets / PromoteCountry / GetCountryPropertyTargets / GetCountryProperty ...` 赋值，即把各 scope 类型的函数填进表；没有逐条反汇编这个函数 |
| `CTextBase::ProcessString` | `0x9D3140` | `ProcessWithScope` 里唯一的大调用；有 186 个调用者 | 解析 `[...]`、逐段求值。参数（MSVC）：`rcx` = 上下文，`rdx` = 结果 CString，`r8` = 字符串视图，`r9b` = 假 |
| `CGameText::GenerateString`（虚函数，vtable 槽 1） | `0x5E68C0` | 引用 `Unknown property`；`vtable 0x238A6B8` | 处理一个 `[...]` 段：`GetScriptedLoc`、`ProcessVariables<T>` 等 |
| `GetScriptedLoc` | `0x5E61C0` | 引用 `Failed to return a result for scripted localization`；唯一调用者是 `GenerateString` | 在 `CScriptableLocalization` 表里找 `scripted_loc` 并递归求值 |
| `CString` 析构 | `0x15BE90`（SDK 的 `CString_Free`） | 已有 | 释放结果 |

Linux 反编译里 `ProcessWithScope`（行 396402）把 `this` 画成结果，并内联了构造函数；Windows 版没有内联，所以多了 `0x5E4A60` 和 `0x5E8C90` 两个调用。**以 Windows 反汇编为准。**

## 2. `ProcessWithScope` 的调用约定（反汇编 `0x5E9350`）✅

```
rcx = 结果（返回值槽，一个 CString；调用后在 rax 里原样返回）
rdx = 要处理的文字（const CString*：数据在 +0x10（短文字内联，否则是指针），长度在 +0x20，容量在 +0x28）
r8  = const CEventScope*

sub rsp, 0xa40                          ; 栈上一个 CGameText 上下文，在 [rsp+0x40]
call CGameText::CGameText   (0x5E4A60)
call 填表函数               (0x5E8C90)
[ctx+0x10] = scope ; [ctx+0x918] = scope; [ctx+8] = 0
字符串视图 {数据指针, 长度, ...} 放在 [rsp+0x30]
call CTextBase::ProcessString (0x9D3140)   ; rcx = ctx, rdx = 结果, r8 = &视图, r9d = 0
call 0x1FB310 (上下文里一个 variant 的析构)
return 结果
```

两个小证据：`0x8462A0`（一个只有 `0x5B` 字节的使用者）把调用者给的返回值槽原样传进来，之后调用者对这个槽调用 CString 析构 → **结果槽由被调用者构造**。宿主传的是一个已初始化的空 CString（`cap = 15`，`size = 0`），两种语义（构造或追加）都安全。

## 3. 玩家国家的 `CEventScope`

不需要自己构造 `CEventScope`：`CExecuteButtonEffectCommand` 里本来就有一个（`+0x20`），按钮通道（`gui_imgui_feasibility.md` E13）早就会填：类型 `4`（国家）写在 scope 的 `+0x08`，国家 ID 在 `+0x10`，`+0x14`、`+0x1C` 清零；引擎默认构造的 scope 把 root / from / prev 指向自己（`+0x30`、`+0x38`），所以 This = From = Root = 玩家国家。宿主用引擎工厂造一个命令对象，**只把它当 scope 的容器**，用完用 vtable 的删除析构销毁，从不投递。

## 4. 实测记录

测试 mod 在 `Yidhar/guidll-test-mod`（`common/scripted_loc`、`common/script_values`、`button_effects` 里的 `guidll_test_bump_counter`），存档 `fmbase`，Stellaris 4.5.2。

| # | 做了什么 | 结果 |
|---|---|---|
| E27 | 对字面文字求值：`Empire: [Root.GetName]`、`[Root.GuidllTestValue]`、`[Root.guidll_test_counter]`（变量还不存在）、没有标记的普通文字 | `Empire: 夜泊技术官僚国`；`10`（基础值，旗标未设）；空串（变量不存在）；普通文字原样 ✅ |
| E28 | 三次 `post guidll_test_bump_counter`（经按钮通道），再求值 `[Root.guidll_test_counter]` | `3` ✅ |
| E29 | `post guidll_test_set_mark`，再求值 `[Root.GuidllTestValue]`、`[Root.GuidllTestFlag]` | `15`（`base 10` + 旗标的 `modifier add 5`）；旗标文字由"未设置"变"已设置" ✅ |
| E30 | 声明面板里的文字（`text = { text = GUIDLL_TEST_SC_COUNTER }` 等）| 面板显示"帝国：夜泊技术官僚国""计数器（存下来的变量）：3""旗标（scripted_loc）：已设置""脚本值：15"，按钮"计数器加 1" ✅（截图：guidll 仓库的 `docs/images/scoped_panel.png`） |
| E31 | 一次求值的耗时（200 次取平均，含造 scope、造 CString、求值、释放） | `[Root.GetName]` 0.5 微秒；`[Root.GuidllTestValue]` 0.7 微秒；四个引用的一句话 2.1 微秒 ✅ |
| E32 | 整个声明面板的每帧分发耗时（含求值） | 平均 60 微秒 / 帧 ✅ |
| E34 | **暂停状态下**再 `post guidll_test_bump_counter`（游戏日期不动），再求值计数器 | `3 → 4`：按钮效果在暂停时也执行，投递后的快照刷新会重新求值 ✅ |
| E33 | 宿主在运行的游戏里热替换（卸载旧的、注入新的）三次（开发用的 `dev_unload`） | 日志里每次都是 `imgui off/on` 重建上下文、字体加入图集（"fonts ours"），之后探针和面板求值照常工作；只有第二次之后截了图 ✅ |

## 5. 踩到的坑

1. **只能在 tick 之间求值。** 求值会读游戏状态。宿主已有"只在 `HandleTurnTick` 之外读状态"的规则（快照），这里同样：`LocScoped` 在 tick 里返回上一次的值（还没有就显示 `…`），在 tick 之外、每个快照求一次。
2. **开发命令的 `scoped` 探针一连三次返回失败，但引擎函数根本没被调到。** 给 `ScopedText` 的两个失败点（造 scope 失败、引擎抛异常）加了日志，两个都没响，说明是探针自己的 tick 守卫（`g_tick_depth == 0`）把它挡掉了；改成把探针排队到"下一个 tick 之外的帧"（`RunProbes()`）之后全部成功。**推断**（没有直接记录 `g_tick_depth`）：开发命令文件每 20 帧轮询一次，在速度 3 下这些帧恰好落在 tick 里；面板每帧求值，会遇到 tick 之外的帧，所以不受影响。**教训**：定期轮询的测试入口要把需要 tick 之外的动作排队，不要在轮询的那一帧里直接判断。
3. **结果槽要传初始化过的空 CString**（见 §2）。
4. **日志是 UTF-8，终端按 GBK 显示**，所以日志里的中文是乱码；比较时看字节或截图，别被它骗了。
5. **`[` 之后一定走引擎。** `LocScoped` 先取原文（`PdxLocalize` 的结果，不去掉标记），里面没有 `[` 就走原来的 `LocKey` 路径（不产生开销）。含空格的"键"原样返回，不求值（和 `LocKey` 一致）。

## 6. 限制与未决

| 项 | 说明 |
|---|---|
| 只有玩家国家的 scope | 选中的行星 / 舰队 / 星系的 scope 需要别的 `CScopeObjectReference` 类型值（国家是 `4`）；`NButtonEffectUtils::DetermineScopeFromInterface`（Linux 行 3965997–3966117）里有按界面选 scope 的逻辑。要做的话是给接口加一个"scope 描述"（类型 + ID），宿主按类型写进 scope。⚠ 没做，也没确认各类型的数值 |
| 多个 scope（`From`、`Prev`、事件目标） | 默认 `This = From = Root`。要不同的值需要用 `CEventScope` 的参数 / 事件目标容器，没研究 |
| 错误怎么显示 | 写错的表达式（`[Root.nonsense]`）引擎怎么处理（原样、空、记日志）⚠ 没测 |
| 嵌套的 `$KEY$`、`§` 颜色 | `PdxLocalize` 已经展开 `$KEY$`；`§` 标记在求值后被 `StripMarkup` 去掉（面板用单色）。没有专门测嵌套 |
| 语言切换 | 原文缓存按键保存，游戏里换语言后不会刷新，要重新载入面板（和 `LocKey` 一样）⚠ 没做 |
| 结果长度 | `TakeCString` 限 4096 字节 |
| 多人 | 求值只读游戏状态，不产生命令；`scripted_loc` 和脚本值在 `common/` 里，所有客户端一致。⚠ 没实测 |
| 直接读变量而不经本地化 | 不需要。引擎的 `ProcessVariables<T>`（Linux 行 392492）读的是同一份数据 |
| 想要数值而不是文字 | 目前拿到的是格式化后的字符串（`15`、`3`）。要数值得自己解析，或者让 mod 用 `scripted_loc` 输出格式化好的文字。⚠ 如果以后要画图（进度条）需要数值，再研究 `value:` 的直接求值 |

## 7. 对 guidll 的影响

- 声明面板：`text`、`label`、按钮文字、`badge` 的两句都用 `LocScoped`，mod 作者的 loc 文件里写 `[Root.xxx]` 即可（`docs/mod-authors.md` 新增"显示脚本算出的值"）。
- 插件接口：`StlGuiApi` 追加 `localize(key, out, cap)`（`docs/developers.md`）。追加成员不改变 API 版本。
- `tools/live/guidll_test.py` 和宿主的开发命令 `loc <键>` / `scoped <文字>` / `scopedbench <文字>`。

## 8. 复验（游戏更新后）

1. `python tools/sdk_dumper/dump.py`：`CGameText_ctor`、`CGameText_ProcessWithScope` 各自唯一命中；`validate.py --addresses` 对没有手工表的新版本会跳过。
2. 在 guidll 里 `python tools/extract_sdk.py <新头文件>` 重新生成子集，重新编译。
3. 游戏里装测试 mod，`scoped [Root.GetName]` 应该得到国家名。
