# SDK dumper 改动记录

每次改 `tools/sdk_dumper` 的规则（新增指纹、改启发式、改输出）都在这里记一笔：改了什么、为什么、怎么验证的、生成的头文件有什么变化。游戏补丁后单纯重新 dump 不用记。最新的在最上面。

## 2026-10-08：引擎自带 Dear ImGui 的定位规则（Stellaris 4.5.2，exe 时间戳 `0x6ABEAA3F`）

### 为什么

`docs/gui_imgui_feasibility.md` 的研究表明引擎里编译进了完整的 Dear ImGui 1.85，插件可以把自己的界面画进引擎的上下文。展示 DLL（`docs/gui_probe/showcase/`）当时用的是手工找到的 7 个内部地址（`GImGui`、分配器三个全局、`ImGui::NewFrame`、`NImGuiWrapper::ImGuiInit`）写死在源码里，游戏一更新就失效。项目规则（`CLAUDE.md`）是地址由 dumper 生成、不手写，所以把它们改成指纹，并顺手把"引擎 ImGui 与插件自带 ImGui 的布局是否一致"的守卫所需的常量也读出来。

### 改了什么

| 文件 | 改动 |
|---|---|
| `functions.py` | 新增 `strings` 匹配的 `"primary": True` 选项：字符串引用落在链式 `.pdata` 片段（switch 分支、冷代码）里时，取该片段所属的主函数，而不是要求片段自己 16 字节对齐。新增 4 条指纹，见下表。 |
| `anchors.py` | 新增 `imgui_internals()`（全局变量与布局常量）和辅助 `top_vote()`；`REQUIRED` 加入 `GImGui`、`GImAllocatorAllocFunc`、`GImAllocatorFreeFunc`、`GImAllocatorUserData`，定位不到整个 anchors 阶段失败。模块说明里补了这条规则。 |
| `validate.py` | 新增 `ADDRESS_TRUTH`（按 exe 的 `TimeDateStamp` 记录手工核对过的地址）和 `--addresses` 模式：对已有表的 exe，检查 `out/functions.json`、`out/anchors.json` 是否复现这些值；没有表的版本跳过并提示。 |
| `dump.py` | 全流程末尾调用 `validate.py --addresses`，失败并入原有的"与手工核对不符"退出。 |
| `README.md` | 阶段表和"使用头文件"补充了这些规则与用法；修正 `--skip-linux` 的说明（见"已知问题"）。 |
| `emit_sdk.py` | 没有改。新条目走现有的 `glob` / `rt` / `fn` 输出路径。 |

新增的函数指纹（`sdk::fn`）：

| 名字 | 指纹 | 4.5.2 的 RVA |
|---|---|---|
| `ImGui_NewFrame` | 唯一引用字符串 `Debug##Default` 的函数（`primary`） | `0x1E9D240` |
| `NImGuiWrapper_ImGuiNewFrame` | `ImGui_NewFrame` 的唯一调用者（`caller_of`） | `0x3345A0` |
| `ImGui_ImplWin32_NewFrame` | 上面那个包装函数里，`cmp qword ptr [r + 0x118], r` / `je` 之后的第一个调用（`call_in`） | `0x1B28470` |
| `NImGuiWrapper_ImGuiInit` | 同时引用 `imgui_impl_win32` 和 `xinput1_4.dll` 的函数（`primary`） | `0x1B11090` |

新增的全局变量（`sdk::glob`）和布局常量（`sdk::rt`），由 `anchors.py` 推导：

| 名字 | 推导方式 | 值 |
|---|---|---|
| `GImGui` | `ImGui::NewFrame` 加载的第一个全局 | `0x28E2D58` |
| `GImAllocatorAllocFunc` / `GImAllocatorFreeFunc` / `GImAllocatorUserData` | 所有内联的 `ImGui::MemAlloc` / `MemFree` 站点（`mov R,[GImGui]; test R,R; je; inc｜dec dword [R+D]; mov rdx,[用户数据]; [mov ecx,大小]; call [函数指针]`，24 个分配站点、146 个释放站点）按票数取一致的那个；要求第二名不到第一名的十分之一 | `0x27FD320` / `0x27FD328` / `0x28E2D68` |
| `ImGuiContext_io_MetricsActiveAllocations` | 上述站点里 `inc` / `dec` 的位移 | `0x3B0` |
| `ImGuiContext_sizeof` | 上述分配站点里唯一大于 `0x2000` 字节的（`IM_NEW(ImGuiContext)`） | `0x3F70` |
| `ImGuiIO_ImeWindowHandle` | 包装函数在调 Win32 NewFrame 前测试的 `io` 成员 | `0x118` |
| `ImGuiIO_BackendPlatformUserData` | Win32 NewFrame 开头第一个经 `GetIO()` 加载的成员 | `0xE0` |

### 生成的头文件变化

`stellaris_bridge/include/sdk/stellaris_sdk.hpp`：**只增加 16 行，没有任何已有行变化**（4 个 `glob`、4 个 `rt`、4 个 `fn` 各带注释行）。这也说明新规则没有扰动已有的定位结果。

### 验证

- 7 个手工找到的地址和 4 个布局常量，规则推导出的值**逐个相同**（`validate.py --addresses`：12 ok, 0 wrong）；`validate.py` 原有的 47 项仍全部通过。
- 展示 DLL 改用 `sdk::` 符号并加了 4 条 `static_assert`（`sizeof(ImGuiContext)`、`ImGuiContext::IO` 内 `MetricsActiveAllocations` 的偏移、`ImGuiIO::ImeWindowHandle`、`ImGuiIO::BackendPlatformUserData` 与 `sdk::rt` 比较），用自带的 ImGui v1.85 编译通过；这是对"引擎里的 ImGui 与插件自带的那份布局一致"的独立确认。
- 实机回归：重新启动游戏，注入改后的 DLL，它从 tick 钩子启动引擎的 ImGui，显示尺寸 1920×1080，画面与改动前一致。
- 没有做的：实时核对（`live_verify.py`）。验证时游戏已退出；已提交的 4.5.2 头文件本来就是静态生成的（`live=static`），所以对头文件没有影响。

### 补丁后怎么办

游戏更新后照常 `python tools/sdk_dumper/dump.py`。新规则失败时会指出是哪一条：

- `ImGui_NewFrame` / `NImGuiWrapper_ImGuiInit` 失败：看字符串 `Debug##Default` / `imgui_impl_win32` 是否还在、还是不是只被这一个函数引用。
- `ImGui_ImplWin32_NewFrame` 失败：包装函数里 `io.ImeWindowHandle` 的判断是不是换了形状（引擎改成不检查句柄，或升级了 ImGui 后位移变了）。
- `GImGui` 一族失败：`ImGui::MemAlloc` / `MemFree` 不再内联成这种形状；输出里会列出每个候选的票数。
- 新的 exe 没有 `ADDRESS_TRUTH` 表项：`validate.py --addresses` 跳过。需要的话在核对过新地址后加一项。

### 已知问题（没改）

- README 原来写 `dump.py --skip-linux` 不需要 `source/`，但 `globals.py` 仍然要读 `source/stellaris_4.5_source.cpp` 做方法体投票。在没有 `source/` 的检出（例如 worktree）里整套流程会在 `globals.py` 失败。这次是沿用同一个 exe 生成的 `out/globals.json`，其余阶段按顺序手动跑完（`functions.py`、`anchors.py`、`emit_sdk.py`、`validate.py`、`validate.py --addresses`）。README 的说明已改成实际情况。
