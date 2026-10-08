# 往游戏里注入 ImGui（"igui"）：可行性与数据交互（Stellaris 4.5.2，Windows，`-dx11`）

状态标记：✅ 已在运行中的游戏里实测 / 在 exe 反汇编里读到；🐧 只在 Linux 反编译里看到；⚠ 推断或未验证。
实验用的游戏版本：Cygnus v4.5.2，exe 时间戳 `0x6ABEAA3F`。所有 RVA 只对这一个版本有效。
引用的其他研究笔记在主检出目录的 `source/better_stellaris/research/`（该目录被 git 忽略）：`flags_index.md`、`event_targets.md`、`gui_native.md`（原生 GUI 系统，另一路调研）；`source/better_stellaris/16_measured_hotspots.md`（帧平滑的实测）。

## 0. 结论（先读这里）

1. **引擎自己就带着一套完整的 Dear ImGui，发布版里可以直接用。** Windows exe 里是 **ImGui 1.85**（主线，没有 docking / viewports），配 Win32 平台后端 + **DX11** 渲染后端，另有 ImPlot 和 ImNodes。控制台输入 `imgui on` 就能启动，`imgui show debug_view` 打开引擎自带的调试窗口。✅ 实测。
2. **渲染、输入、捕获仲裁（ImGui 想要鼠标 / 键盘时不再传给游戏）、中文输入法的文本输入开关，引擎都已经接好了。** 插件不需要自己做 Present 钩子、窗口子类化、DX11 后端。✅
3. **插件可以在引擎的 ImGui 上下文里画自己的界面。** 做法：插件自带一份 ImGui 1.85 源码，运行时把它的 `GImGui` 指向引擎的上下文，在引擎的 `ImGui::NewFrame` 之后画。实测画出了窗口、按钮、输入框、滑块、曲线图，以及 ImGui 的**完整演示窗口**。✅
4. **插件可以自己把引擎的 ImGui 启动起来**（不用玩家输入控制台命令）：在主线程上调用引擎的 `NImGuiWrapper::ImGuiInit`（RVA `0x1B11090`）。✅ **触发点要用 `HandleTurnTick` 钩子（tick 之间），不要用 Present 的 vtable 槽位钩子**：Steam 叠加层会在每次创建交换链时重新改写这个共享槽位，再加上已经加载的别的 DLL 也钩了同一个槽位，就会形成递归，实测 `EXCEPTION_STACK_OVERFLOW` 崩游戏（§9 E11）。
5. **读写游戏数据都跑通了**（读日期 / 速度 / 暂停状态，按钮调用引擎的 `SetPaused` / `SetGameSpeed`）。✅ 但有一个必须正视的约束：**UI 回调有 33–41% 是在 `HandleTurnTick` 里面被调用的**（帧平滑开启时，高速运行实测），见 §5。
6. 需要补的东西：**中文字体**（默认字体没有 CJK 字形；已验证可在引擎第一次构建图集之前合并进去 ✅）、**`imgui.ini` 重定向**（引擎默认把布局写到游戏目录里，违反启动器的插件规范）、**版本 / 布局防护**（引擎以后升级 ImGui 会让共享上下文的做法失效）、**多人游戏下的写操作**（⚠ 未验证）。

## 1. 引擎里的 ImGui（证据）

| 事实 | 证据 |
|---|---|
| 发布版 exe 带 ImGui + DX11 后端 | 字符串 `imgui_impl_dx11`（RVA 0x2568780）、`imgui_impl_win32`、`Dear ImGui`、`imgui.ini`、控制台命令字符串 `Enable ImGui` ✅ |
| 版本 1.85 | .rdata 里的版本字面量 `1.85`；`io.KeyMap`（旧式键表）在 `io+0x34`；`BackendPlatformName@io+0xD0`、`BackendRendererName@+0xD8`、`BackendPlatformUserData@+0xE0`、`BackendRendererUserData@+0xE8`；DX11 后端数据里 `VertexBufferSize=5000@+0x70`、`IndexBufferSize=10000@+0x74`（1.85 的默认值）✅ |
| 编译配置是默认的 | 探针里自带的 1.85 与引擎共享上下文后，演示窗口（表格、树、滚动、弹窗代码路径）工作正常；`sizeof(ImDrawIdx)=2`，`sizeof(ImGuiIO)=5464` ✅（没有出现错位的迹象）|
| 平台后端 | `ImGuiInit`（`0x1B11090`，入口是按渲染类型的跳转）：类型 0 = DX9（`imgui_impl_dx9`）、类型 1 = DX11（`imgui_impl_dx11`），类型 2–5 什么都不做。两者都先建 Win32 平台数据（含 XInput 动态加载）。✅ |
| 其他上下文 | `ImGuiInit` 同时创建 ImPlot 上下文和 ImNodes 上下文；`io.ConfigFlags \|= 0x20`（NoMouseCursorChange，光标由游戏自己画）✅ |
| 每帧流程 | `CGameIdler::RenderImGui`（虚函数，每帧由空闲器调用）：若上下文存在 → `NImGuiWrapper::ImGuiNewFrame`（DX11 NewFrame + Win32 NewFrame + `ImGui::NewFrame`）→ `CImGuiController::UpdateViews`（更新各个视图）→ `NImGuiWrapper::ImGuiRender`（`ImGui::Render` + `RenderDrawData`）🐧 |
| 输入 | 引擎的 SDL 事件循环（`CSdlEvents::HandlePdxEventsNormal`）把事件**内联**喂进 ImGui 的 `io`（`KeysDown`、滚轮、字符），每轮开头读 `io.WantCaptureMouse` / `WantCaptureKeyboard`（`io+0x38C/0x38D`）决定事件是否还给游戏。Windows 版函数 `0x1BA93F0` ✅🐧 |
| 文本输入 | `NImGuiWrapper::EnableImGuiTextInput` / `DisableImGuiTextInput` 开关 SDL 文本输入，供输入法使用 🐧 |
| 内置视图 | `debug_view`、`fleets`、`paragons`、`event_graph`、`storms`、`imgui_demo`、`imgui_guide`、`implot_demo`、`imnodes_demo`（`OnExecute_ImGui` 的 `Uis` 表）🐧；4.5.2 实测 Debug View 里有 Debug Stats / Fleets / Paragons / Event Graph / Traits ✅ |

### 1.1 4.5.2 的 RVA（本次定位）

| 名字 | RVA | 怎么找到的 |
|---|---|---|
| `GImGui`（`ImGuiContext*` 全局） | `0x28E2D58` | `GetCurrentContext` 就是 `mov rax,[GImGui]; ret` ✅ |
| `ImGui::GetCurrentContext` | `0x1E94BA0` | 同上 ✅ |
| `ImGui::GetIO` | `0x1E95470` | `GImGui + 8`（1.85 里 `IO` 紧跟 `Initialized`、`FontAtlasOwnedByContext`）✅ |
| `ImGui::NewFrame` | `0x1E9D240` | 字符串 `Debug##Default` 的唯一引用者（NewFrame 末尾创建的隐式窗口）；探针钩它并正常工作 ✅ |
| `NImGuiWrapper::ImGuiInit` | `0x1B11090` | 同一个函数里同时引用 `imgui_impl_win32` 和 `imgui_impl_dx11`；插件直接调用它成功启动 ImGui ✅ |
| `ImGui::MemAlloc` / `MemFree` | `0x1E998A0` / `0x1E998C0` | `inc/dec dword [GImGui+0x3B0]` 后 `jmp [全局]` ✅ |
| `GImAllocatorAllocFunc` / `FreeFunc` / `UserData` | `0x27FD320` / `0x27FD328` / `0x28E2D68` | 上面两个函数里的间接跳转和 `rdx` ✅ |
| `CGameState::HandleTurnTick`（探针用来统计） | `0x251800` | 与 `sdk::fn::CGameState_HandleTurnTick` 一致 ✅ |
| `CGameIdler::RenderImGui`（虚函数） | ⚠ 未定位 | 没有直接调用者；试过按成员链扫描没找到。不影响方案（钩 `NewFrame` 即可）|

**这些地址现在由 `tools/sdk_dumper` 生成，不再手写**（改动记录见 `tools/sdk_dumper/CHANGELOG.md`）：`sdk::glob::GImGui` / `GImAllocatorAllocFunc` / `GImAllocatorFreeFunc` / `GImAllocatorUserData`、`sdk::fn::ImGui_NewFrame` / `NImGuiWrapper_ImGuiNewFrame` / `ImGui_ImplWin32_NewFrame` / `NImGuiWrapper_ImGuiInit`，以及版本 / 布局守卫用的 `sdk::rt::ImGuiContext_sizeof`、`ImGuiContext_io_MetricsActiveAllocations`、`ImGuiIO_ImeWindowHandle`、`ImGuiIO_BackendPlatformUserData`。dumper 的指纹和上表"怎么找到的"不完全相同：`GImGui` 取 `ImGui::NewFrame` 加载的第一个全局；分配器三件套取所有内联的 `MemAlloc` / `MemFree` 站点（`mov R,[GImGui]; test; je; inc|dec [R+0x3B0]; mov rdx,[UserData]; call [Alloc|Free]`，170 个站点）的一致意见；`ImGuiInit` 用 `imgui_impl_win32` + `xinput1_4.dll` 两个字符串并取其所属的主函数（那段代码在 switch 分支的 `.pdata` 片段里）。`GetCurrentContext` / `GetIO` / `MemAlloc` / `MemFree` / `HandleTurnTick` 的行没有进 dumper（插件不需要）。

## 2. 实验记录（全部在真实游戏里，测试存档 `fmbase`）

| # | 做了什么 | 结果 |
|---|---|---|
| E1 | 控制台 `imgui on`、`imgui show debug_view` | 引擎自带的 Debug View 窗口出现；点击 `Time` 展开出 `ticks_per_turn` 输入框；点击 `Fleets` 打开舰队视图。点击没有穿透到地图 ✅ |
| E2 | 注入探针 DLL（自带 ImGui 1.85，`GImGui` 指向引擎上下文，钩 `ImGui::NewFrame`） | 探针窗口与引擎的 Debug View 并排绘制；读到的 `FrameCount`、`DisplaySize`、版本号都合理，帧计数逐帧推进 ✅ |
| E3 | 读：日期 / 速度 / 暂停；写：Pause/Resume、x1–x5 按钮调用引擎 `SetPaused` / `SetGameSpeed` | 点 Resume 后日期在走，再点暂停，状态实时反映在面板里 ✅ |
| E4 | 输入仲裁 | 悬停在面板上时 `WantCaptureMouse=1`；点击不会触发下面的游戏界面。在 `InputText` 里输入 `ab cd 12`（含空格）：文字进输入框，`WantCaptureKeyboard=1`，游戏**保持暂停**，没有触发字母热键 ✅ |
| E5 | 自启动：Present 虚表槽补丁（与 bench DLL 同一做法）每帧回调，游戏进入后调用引擎 `ImGuiInit` | 没有输入任何控制台命令，面板从第 1 帧开始出现 ✅ |
| E6 | 控制台 `imgui off` 再 `imgui on` | 面板消失，再出现；**上下文指针变了**，面板在新上下文里重新建出；游戏没崩 ✅ |
| E7 | 勾选探针里的"显示 ImGui 演示窗口"，展开 Widgets 等 | 完整的 `ImGui::ShowDemoWindow`（带树、滚动、嵌套布局）正常 ✅ |
| E8 | 卸载（事件通知，钩子摘除，等回调退出，自行 FreeLibrary），共做了 4 次 | 游戏继续运行，引擎自己的 Debug View 不受影响 ✅ |
| E9 | 中文：在 `ImGuiInit` 返回后、第一次 `NewFrame` 之前，向引擎的字体图集 `AddFontDefault` + 合并微软雅黑（常用简体范围） | 面板里的 `中文测试` 正常显示；不合并时显示 `????` ✅ |
| E10 | 高速运行时 UI 回调落在 `HandleTurnTick` 里的比例（钩 `0x251800` 计数，帧平滑为引擎默认的开启） | 速度 5：8 秒里约 33%，另一段 1 秒里 58/140 = 41% ✅ |

**没验证的**：多人游戏（§6）；铁人模式 / 成就是否被影响（控制台命令会提示"使用任何调试命令都将无法获得成就"，但我们自己调用 `ImGuiInit` 不经过控制台，是否打标记没查）；DX9 渲染类型（类型 0）；Steam 覆盖层 / 其他注入插件同时存在时的行为；游戏更新后的指纹稳定性。

测试产生的 `imgui.ini`（引擎写在游戏目录）已删除；`continue_game.json` 已恢复；我启动的游戏进程已关闭。

## 3. 三种架构，比较

| | A. 共享引擎的 ImGui 上下文（**推荐**，已验证） | B. 插件自带完整 ImGui 栈 | C. 扩展原生 GUI |
|---|---|---|---|
| 做法 | 自带 1.85 源码，`SetCurrentContext(引擎的)`，钩 `NewFrame` 后绘制 | 自己的上下文 + 自己的 DX11 后端 + 自己的 Present 钩子 + 窗口子类化接输入 | 钩原生 GUI 的文本 / 按钮 / 窗口提供者（见 `gui_native.md`）|
| 渲染 | 引擎的后端，零代码 | 自己写（imgui 自带 `imgui_impl_dx11` 可直接用）| 引擎原生渲染 |
| 输入 / 捕获 | 引擎已经做好（含仲裁、IME）| 要自己拦窗口消息，并决定何时不把事件交给游戏 | 原生 |
| 版本耦合 | **强**：源码版本、`imconfig.h`、`ImDrawIdx` 大小必须与引擎一致；引擎升级 ImGui 会破坏 | 无 | 对原生 GUI 内部强耦合 |
| 与其他覆盖 / 注入的冲突 | 小：只在需要自启动时补丁 Present 槽 | 有：Present 钩子要和 Steam 覆盖层、live2d、bench 等共存；卸载顺序问题（见 bench 的说明）| 无 |
| 工作量 | 小：探针约 300 行 | 中：输入仲裁是难点 | 大，且受原生控件能力限制 |
| 失败模式 | 版本不一致时静默崩溃或花屏 → 必须有运行时防护 | 输入泄漏 / 抢焦点 | 受限于原生控件 |

**建议**：以 A 为主；运行时检查引擎的 ImGui 版本字符串与布局，不一致就**不绘制**并写日志；B 作为引擎将来升级 ImGui 后的后备（Present 槽补丁和窗口子类化的现成代码在 perf 仓库的 bench 和 live2d 仓库里都有）。

## 4. 把 A 做成插件时要处理的事

1. **分配器**：自带的那份 ImGui 必须用引擎的分配器：`ImGui::SetAllocatorFunctions(*(void**)(base+0x27FD320), *(void**)(base+0x27FD328), *(void**)(base+0x28E2D68))`。共享上下文里的容器会被两边的代码分配和释放。✅（探针这样做，没有出现崩溃）
2. **每帧重新取上下文指针**（`imgui off/on` 会换掉它，E6）。
3. **版本 / 布局防护**：✅ **编译期已做**（展示 DLL 里的 `static_assert`）：dumper 从引擎代码里读出 `sizeof(ImGuiContext)`（`0x3F70`）、`io.MetricsActiveAllocations` 在上下文里的偏移（`0x3B0`）、`io.ImeWindowHandle`（`0x118`）、`io.BackendPlatformUserData`（`0xE0`）放进 SDK（`sdk::rt::ImGui*`），插件用自带的 ImGui 头文件的 `sizeof` / `offsetof` 与它们比较，不一致就编译失败（引擎升级了 ImGui 之后重新 dump、重新编译就会暴露）。⚠ 运行期的版本字符串比较没做（`GetVersion` 没有指纹，且编译期检查已经覆盖了布局）。
4. **`imgui.ini`**：引擎的 ImGui 默认把窗口布局写到**游戏目录**的 `imgui.ini`（本次实测产生了一个，已删除）。启动器规范禁止写游戏目录，所以要设置 `io.IniFilename` 指向插件目录。落在哪里：规范说更新会替换整个文件夹但保留 `config\`，所以持久的界面状态应放 `config\`（启动器的插件页会把它当文本显示，可接受），或者给自己的窗口加 `ImGuiWindowFlags_NoSavedSettings`。
5. **中文字体**：在 `ImGuiInit` 返回后、第一次 `NewFrame` 前合并字体（E9）。如果玩家先输入了 `imgui on`（图集已经建好），就得让引擎重建字体纹理（⚠ 需要定位后端的字体纹理创建函数或释放 `pFontSampler`，未验证）。
6. **DPI / 缩放**：游戏进程对 DPI 不感知，ImGui 的 `DisplaySize` 是逻辑像素（4K 屏上读到 1920×1080）；界面大小用 `io.FontGlobalScale` 或自带字体大小调整。
7. **性能**：回调每帧执行（帧平滑下一个 tick 内可能有很多帧）；界面里的数据要按 tick 缓存，不要每帧遍历游戏数据。
8. **ImPlot / ImNodes**：引擎里已有它们的上下文，理论上同样可以用（⚠ 版本未核对）；经济曲线、事件图这类用途很合适。
9. **窗口句柄（实测踩到的坑，§9 E12）**：`ImGuiInit` 用 `GetActiveWindow()` 取窗口句柄（IAT 槽位 `0x2217110`），存进 `io.ImeWindowHandle`（`io+0x118`）和 Win32 后端数据。游戏窗口**不是活动窗口**时（切走了、从外部热重载 DLL）拿到 0，之后引擎每帧的 `if (io.ImeWindowHandle) ImGui_ImplWin32_NewFrame()` 被跳过，`io.DisplaySize` 停在 −1，窗口生成了几何体但什么都画不出来。修法：`ImGuiInit` 返回后检查，句柄为空就用进程自己的主窗口补上 `io.ImeWindowHandle` 和后端数据的 `hWnd`（第一个成员）；每帧自检，已经坏掉的上下文也能自愈。
10. **默认字体槽位**：第一个加进图集的字体会成为 `io.FontDefault`，引擎自己的 ImGui 视图（Debug View 等）用的就是它。先 `AddFontDefault()` 占住这个槽位，自己的字体放后面，用 `PushFont` 显式选；否则引擎的视图会跟着变成我们字体的字号。
11. **字形范围**：`GetGlyphRangesChineseSimplifiedCommon()` 不含"擎"、"钮"这类字，也不含箭头 / 几何符号。用 `ImFontGlyphRangesBuilder` 把界面字符串里出现的字全部加进去（展示 DLL 里由 `gen_ui_glyphs.py` 从源码生成）；运行时出现的生僻字（国家名、星球名）仍会显示成 `?`。
12. **ImGui 已经在运行时加载**：图集已建好，不能再加字体。展示 DLL 在这种情况下用控制台命令 `imgui off` + `imgui on`（`CConsole::RunCommandNow`）让引擎重建上下文，再由 `ImGuiInit` 的钩子把字体加进去。

## 5. 数据交互：怎么读、怎么写

### 5.1 读（单人，已验证的范围）

- 插件在 `NewFrame` 回调里就是**主线程**，可以直接读 SDK 里定位好的结构（日期 `CGameState_date_hours`、`CInGameIdler` 的速度 / 暂停等）。✅ 引擎的日期换算：`n = hours/24 − 1,825,000`，`年 = n/360`，12 个月 × 30 天。
- **必须正视的约束（E10）**：帧平滑开启时，UI 回调有三分之一到四成发生在 `HandleTurnTick` **里面**——也就是模拟正在跑、工作线程可能还在改状态的时候（之前已经确认 `RenderFrameInParallelFor` 会在并行阶段中途渲染帧）。因此：
  - 面板应读**快照**：在 `HandleTurnTick` 返回之后（perf 插件已经有这个钩子，探针里也有）整理一份数据，UI 只读快照；
  - 或者在回调里检查"当前不在 tick 内"再读；
  - 日期这类单个字段的读取在探针里没出过问题，但**不能据此推断遍历容器是安全的**。⚠
- 现成的数据层：桥接器已有 104 个定位好的引擎函数、53 个实体数据库、268 条命令、约 19k 行管理器代码，全部通过 JSON-RPC 暴露。桥接器的方法是"入队 lambda → Present 里主线程统一执行 → future"。**GUI 可以直接复用它们当界面数据模型**：在主线程上 `Enqueue(fn); ProcessAll(); fut.get()` 就能同步拿到结果（需要一个很小的改动：导出一个同步入口，并避免在 `ProcessAll` 内部重入）。⚠ 设计，未实现。

### 5.2 写

- 探针直接调用了 `SetPaused` / `SetGameSpeed`：它们改的是本机界面 / 流速状态，单人没问题。✅
- **会改变模拟状态的操作不要直接写内存或直接调用引擎函数**：走引擎的命令系统（桥接器的 `CommandBuilder` + `PostCommand`，AGENTS.md 里有规则）。这既是正确性要求，也是多人同步的要求：命令会进同步命令队列，在所有客户端按同一顺序执行；直接改状态会让本机与其他客户端分叉。⚠
- 另一个坑：UI 回调有时在 tick 中间，此时发命令 / 改状态不安全；先入队，在 tick 之间（`HandleTurnTick` 之外）统一提交。

### 5.3 脚本（mod）与界面之间的数据

这是"让 mod 作者方便做自定义 UI"的核心，**目前只有思路，没有验证**：

- **读 mod 数据**：旗标容器（`CPdxIntegerFlags`）的布局已知（`flags_index.md`）；事件目标容器布局已知（`event_targets.md`）；变量容器和脚本值的读取没研究。可以用引擎自带的触发器 / 脚本值求值函数（桥接器里 `CallPredicate` 的做法）让界面显示由 mod 的脚本表达的数值，而不是手工解析。
- **声明式面板**：像 live2d 插件扫描启用的 mod 里的肖像注册那样，扫描启用 mod 里的面板声明文件（例如 `interface/stl_panels/*.txt`，Paradox 脚本语法），声明控件（文本、按钮、进度条、列表）和绑定的数据源（旗标、变量、脚本值、触发器）。modder 不写 C++。⚠
- **界面 → 脚本**：✅ **通道已验证（单人）：`CExecuteButtonEffectCommand`（`sdk::cmd::execute_button_effect`）。** 原生 GUI 的 `effectButtonType` 点击时就是造这条命令（`CEffectButton::ExecuteEffect`），所以 ImGui 按钮可以走同一条路径，执行 mod 里 `common/button_effects/` 定义的任意脚本效果，结果由引擎自己的 `potential` / `allow` 触发器把关（详见 §9 E13）。⚠ **多人下的行为没测**：它是普通的可序列化命令，理论上和 GUI 按钮一样会同步到所有客户端并在每个客户端重新检查 `IsValid`，但这一点没有实测。控制台命令路线（多人受限、会禁用成就）不需要用。

## 6. 风险

| 风险 | 说明 |
|---|---|
| 引擎升级 ImGui | 共享上下文会静默出错 → §4.3 的防护是必须的 |
| 游戏更新 | 地址全部变化 → 指纹放进 `tools/sdk_dumper`（NewFrame、ImGuiInit、GImGui、分配器全局都有字符串或结构可抓）|
| 多人游戏 | ⚠ 只读面板应当没问题（不改模拟）；任何写操作要走命令。引擎的 ImGui 在多人下是否可用没验证（`CConsoleCmdManager::SetIsMultiplayer` 说明控制台命令集在多人里不同）|
| 成就 / 铁人 | ⚠ 控制台命令会禁用成就；我们的路径不经过控制台，但没有核实引擎是否在别处标记 |
| 与其他覆盖层共存 | A 方案**不要钩 Present**（§9 E11：和 Steam 叠加层 + 其他钩了同一个 vtable 槽位的 DLL 叠在一起会递归爆栈）；自启动改在 `HandleTurnTick` 钩子里做，A 方案本身只钩 `ImGui::NewFrame`、`ImGuiInit`、`HandleTurnTick` 三个引擎函数。B 方案（自带后端 + Present 钩子）需要认真处理这个问题 |
| 输入法 | 中文输入法在控制台里会把按键吞进候选栏（实测，`WM_IME_CONTROL` 可关闭）；ImGui 输入框的输入法行为没有逐项验证 |
| 帧平滑 | 开启时回调有大量在 tick 内；关闭时回调只在 tick 之间 —— 前者要快照，后者没有这个问题（代价见 `16_measured_hotspots.md` §1b）|

## 7. 探针（可丢弃的验证代码）

位置：`docs/gui_probe/`（`poc.cpp`、`CMakeLists.txt`），实时测试脚本在 `docs/gui_probe/live/`。RVA 写死了，只适用于 4.5.2，**不是产品代码**。注意 `poc.cpp` 的自启动用的是 Present vtable 槽位钩子，在装了 Steam 叠加层 + 别的槽位钩子的环境里会爆栈（§9 E11）；更完整、没有这个问题的实现在 `docs/gui_probe/showcase/`（§9.2）。

- 构建：ImGui v1.85 源码放在上一级的 `imgui185/`（`git clone --depth 1 --branch v1.85 https://github.com/ocornut/imgui.git imgui185`，`CMakeLists.txt` 里的 `IMGUI_DIR` 指向 `../imgui185`，按需修改），MinHook v1.3.4 由 CMake 获取；`cmake -S . -B build -G "Visual Studio 17 2022" -A x64` 然后 `cmake --build build --config Release`。
- 使用：`stl inject <gui_poc.dll>`；游戏进入后约 10 秒自动启动引擎 ImGui，面板出现；卸载：设置事件 `Local\gui_poc_unload_<pid>`（`live/poc_unload.py`）。
- 实测脚本：`live/imgui_probe.py`（读档、输入控制台命令、截屏；依赖 D:\stellaris-perf 的 `bench/scripts/game_session.py`）、`live/click.py`（点击）；`live/xref.py`、`gdis.py`、`grange.py`、`wantcap.py` 是定位用的小工具（基于 `tools/sdk_dumper/win_extract.py`）。

## 8. 如果继续：建议的里程碑

1. **M0 产品化探针**：独立仓库（需要你决定名字和账号），启动器插件规范 v2（清单、`config\`、`logs\`），`imgui.ini` 重定向，中文字体，C ABI 的"注册面板"接口（让 perf / live2d / 桥接器的状态页都能挂进来）。（版本 / 布局防护和 SDK 指纹进 dumper 已经做完，见 §4.3、§1.1。）
2. **M1 数据层**：桥接器的同步入口 + tick 快照；只读面板（国家概览、舰队、事件日志）。
3. **M2 mod 声明式面板**：先研究变量 / 脚本值读取和"界面 → 脚本"的同步路径，再定格式。
4. **B 方案后备**：保留自带后端的开关，用于引擎 ImGui 版本变化时。

## 9. 第二轮：展示 DLL 与"界面 → 脚本"通道（实测）

### 9.1 实验记录

| # | 做了什么 | 结果 |
|---|---|---|
| E11 | 展示 DLL 第一版沿用探针的做法：建一个临时 D3D11 设备 + 交换链取 `IDXGISwapChain::Present` 的 vtable 槽位，写入自己的钩子。当时游戏里已经加载了 bench DLL（它也钩这个槽位）。 | 注入后约 1 秒游戏崩溃，`EXCEPTION_STACK_OVERFLOW`，调用栈在 `gameoverlayrenderer64.dll` 和 `stellaris_bench.dll` 之间来回。解释：DXGI 交换链的 vtable 是共享的，Steam 叠加层在**新建交换链**时会把当前槽位内容当作"原函数"保存再写自己的钩子——我们的临时交换链触发了它，于是 叠加层 → 我们/bench → 叠加层 形成环。**改成在 `HandleTurnTick` 钩子里自启动，不再碰 Present**，之后没再出现。（同一类问题也是之前"bench DLL 重载溢出 Present 钩子"的原因。）|
| E12 | 在游戏窗口不是活动窗口的状态下，从外部热重载 DLL（让引擎的 ImGui 重启）。 | 画面里没有任何自绘窗口，但帧回调和数据读取都正常。`dump` 显示 `DisplaySize = -1×-1`、有 3 个窗口 / 2 万多顶点。反汇编：`ImGuiInit`（`0x1B11090`）里 `call [0x2217110]`（= `USER32!GetActiveWindow`）→ 存进 `io+0x118`（`ImeWindowHandle`）和 Win32 后端数据；`ImGuiNewFrame` 包装函数（`0x3345A0`）里 `if (io+0x118 != 0) call 0x1B28470`（= `ImGui_ImplWin32_NewFrame`，用 `GetClientRect` 设 `DisplaySize`）。窗口不活动 → 句柄 0 → 平台 NewFrame 被跳过。补上句柄后一帧内恢复 `1920×1080`（见 §4.9）。|
| E13 | **`CExecuteButtonEffectCommand` 作为界面 → 脚本通道。** 测试 mod `zz_gui_showcase` 在 `common/button_effects/` 定义 4 个效果；DLL 用引擎工厂建命令（`sdk::cmd::execute_button_effect`，大小 `0x198`），把作用域写成"玩家国家"（`scope@+0x20`：`+0x08 = 4`（国家）、`+0x10 = 国家 ID`、`+0x14` / `+0x1C` 清零；默认构造的 `CEventScope` 的 root/from/prev 指向自身，所以 This = From = Root = 玩家国家），`effect@+0x190` 写 `TGameDatabase<CButtonEffectDatabase>` 里的条目（库 `+0x50` 数组、`+0x5C` 个数、条目键字符串在 `+0x20`），调命令自己的 `IsValid`（vtable 槽 8），通过后 `PostCommandToSession(cmd, false)`。 | ① 暂停状态下发 `add_resource energy 100`：能量币 `0 → 100`，原版顶栏同步变化，`game.log` 里有脚本的 `log` 输出。**暂停时命令也会执行**（不依赖 tick）。② `allow = NOT has_country_flag` / `allow = has_country_flag` 两个效果互为开关：发出一个后，另一个的 `IsValid` 立刻翻转。③ `allow` 为假的效果（需要 100 万合金）：`IsValid` 返回假并给出引擎自己生成的原因（"× 拥有大于等于 1000000.00 合金"），界面据此禁用按钮；强行发也会被挡下。④ 真实鼠标点击 ImGui 按钮走通整条路径。|
| E14 | 键盘模拟控制台命令（SendInput 扫描码）驱动游戏。 | 中文输入法把按键吞进候选栏，命令行变成乱码；关 IME 后仍不稳定，多次 Esc 还会打开游戏的系统菜单。**不要再用键盘模拟**：展示 DLL 提供了文件命令 `console <行>`（`CConsole::RunCommandNow`，长行用引擎分配器），在游戏进程里直接执行。|
| E15 | 展示 DLL 热重载（卸载旧的、注入新的，不重启游戏）。 | 新 DLL 发现引擎 ImGui 已在运行但图集不是自己的，于是用 `imgui off` + `imgui on` 重建上下文并在 `ImGuiInit` 钩子里加字体。上下文地址有时不变，所以"是否换了上下文"不能靠指针比较，`IniFilename` 等每帧都要重设。|

### 9.2 展示 DLL（`docs/gui_probe/showcase/`）

一个和原版界面完全不同风格的自定义面板，真实读写游戏：

- 底部状态胶囊（日期、暂停 / 5 档速度、五种资源的库存与月净值），点左侧圆球打开 / 收起"指挥甲板"；`Ctrl+Shift+G` 显示 / 隐藏。
- 指挥甲板五页：总览（国力雷达图——五个维度都按"银河系里同项最强的帝国 = 100%"归一，资源环形仪表）、经济（资源账本 + 库存面积曲线 + 月净值柱 + 悬停十字线）、时间（速度表盘、双环星历、帧时间 / tick 速率曲线）、脚本通道（4 个按钮效果，状态和拒绝原因来自引擎，执行记录）、设置（4 套主题、星空开关、运行信息）。
- 自绘：渐变面板、发光、星空与流星，图标全部由 `ImDrawList` 图元画出（没有图标字体），环形仪表 / 雷达 / 面积图用顶点色渐变。字体：Segoe UI + 微软雅黑（CJK 合并）+ Bahnschrift（数字）。
- 数据：只在 tick 之间取快照；国家名用引擎的 `CPersistentName::BuildString`；资源从 `CCountry` 的预算 / 库存数组读（同桥接器）。

截图：`docs/gui_probe/showcase/screenshots/`。构建与运行见 `docs/gui_probe/showcase/README.md`。
