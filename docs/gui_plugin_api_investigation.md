# 把 GUI 宿主做成公共接入点：给开发者和 mod 作者的接口调查（Stellaris 4.5.2）

状态标记：✅ 在运行中的游戏里实测 / 读到了源码或规范；⚠ 推断、只看了摘要或没验证；📎 外部资料（链接在 §11）。
前置阅读：`gui_imgui_feasibility.md`（引擎自带的 ImGui 能用、怎么用）、`gui_native_system.md`（原生 .gui 系统的限制）。
原型代码：`gui_probe/api_proto/`（接口头文件、两个消费方插件）和 `gui_probe/showcase/`（宿主一侧）——**这些代码已迁出，见下面的"实现状态"**；本文是调查记录，里面的文件名（`gui_showcase.dll`、`zz_gui_*`、`consumer_a/b`）是原型里的名字。

## 实现状态（2026-10-08）

调查之后按 §7 的建议做成了独立仓库（均为私有，需要时 `gh repo edit <仓库> --visibility public`）：

| 仓库 | 内容 |
|---|---|
| **`Yidhar/stellaris-guiexpand`** | 宿主插件（插件 id 和 DLL 名 `stellaris-guiexpand`）、公共 C 接口、两个示例插件、声明面板、参考皮肤、文档、CI。`main` 目前只有 GitHub 生成的 README / LICENSE，代码在草稿 PR #1（分支 `initial-import`），合并后才有 |
| **`Yidhar/stellaris-guiexpand-test-mod`** | 测试 mod：一个声明面板、四个 button effect、中英文 loc、安装脚本、静态检查。同样在草稿 PR #1 |

对 §10 的决定（项目最初叫 `guidll`，2026-10-08 在第一次发布前改名为 `stellaris-guiexpand`，仓库、插件 id、DLL、测试 mod 的键都随之改了；本文其余地方和 git 历史里可能还有旧名）：

| 项 | 做法 |
|---|---|
| 1 仓库名、账号、许可 | `Yidhar/stellaris-guiexpand`，MIT（头文件同样 MIT） |
| 2 插件 id 和 DLL 名 | `stellaris-guiexpand` / `stellaris_guiexpand.dll`（没有用建议的 `stellaris-gui`）；接口前缀 `Stl` / `STL_` 和导出函数 `StlGui_GetApi` 不变 |
| 3 声明文件夹 | 沿用 `interface/stl_gui/*.txt` |
| 4 指挥甲板 | **没有拆出**：留在宿主里作为可关闭的参考皮肤（`deck = 0`）。拆成独立消费方插件仍然是更干净的做法，**待你决定** |
| 5 启动器的加载顺序提示 | 没做；接口本来就不依赖加载顺序 |
| 6 v1 元素集 | 不变（text / separator / spacer / date / value / gauge / stat / badge / button / row） |

**之后做了 T2（作用域本地化）**：`[Root.xxx]` 现在在声明面板的所有文字里生效；接口追加了 `localize`。研究记录是 `gui_scoped_localisation.md`；dumper 多了两条指纹（`CGameText_ctor`、`CGameText_ProcessWithScope`，见 `tools/sdk_dumper/CHANGELOG.md`）。

迁移时的改动：日志、设置和开发用的命令文件放进插件文件夹（`logs\`、`config\stellaris_guiexpand.ini`），不再放在 DLL 旁边；演示用的 effect 改名 `guiexpand_test_*`，由测试 mod 提供；面板注册表用选项（标题是不是 loc 键、窗口大小）代替对"声明面板"的硬编码。迁移后在游戏里重新验证过（两种注入顺序、四个 effect、故障隔离含三次故障后停用、布局核对、宿主卸载、启动器接受清单）；没有重跑的是"留下没配对的 ImGui 栈"和"不注销就卸载"两项（原型里验证过，代码未改）。

## 0. 结论

**方向（你提出的）：我们自己当宿主和公共库，对开发者给一个接口，对 mod 作者给一套语法，作为同一个接入点。调查支持这个方向，原型也跑通了。**

1. **开发者侧：一个纯 C 的接口**。宿主 DLL 只导出一个函数 `StlGui_GetApi(version)`，返回函数表；所有结构体以 `size` 开头、只追加不删除；字符串 UTF-8、不跨边界传递所有权。插件在**自己的线程里轮询**发现宿主（启动器没有依赖字段，加载顺序不定），注册绘制回调；回调在主线程、引擎的 ImGui 帧里被调用。✅ 实测：插件比宿主先加载、宿主晚到，注册都成功。
2. **绘制方式三选一，推荐 A + C，B 暂缓**：
   A 插件自带一份 ImGui 1.85（默认配置）并绑定到共享上下文；
   C 宿主给一张精简的 C 绘制函数表，插件不含任何 ImGui（任何语言都能用）；
   B 像 ReShade 那样宿主导出整张 ImGui 函数表（要生成约 700 个函数的转发，收益是插件不必自带 ImGui 源码）。
   ✅ A 与 C 同时在同一个引擎上下文里画，A 用 `/MT`、宿主用 `/MD`，没有问题。
3. **自带 ImGui 的最大风险是配置不一致**（比如把 `ImDrawIdx` 改成 32 位，很常见）。✅ 宿主在回调上下文里带上引擎 ImGui 的类型大小，绑定辅助函数运行时比对，不一致就拒绝绘制并说明原因；实测一个 32 位索引的插件被拒绝，其余面板不受影响。
4. **故障隔离是宿主的责任**。✅ 回调里的空指针访问被捕获、ImGui 的栈（窗口、分组、颜色、样式变量、字体）被复原；不注销就被卸载的插件被检测并摘除；三次故障后停用该面板。
5. **mod 作者侧：Paradox 脚本语法的声明文件，放 `interface/stl_gui/*.txt`**。✅ 引擎对这个文件夹（以及 `common/stl_gui`、`gfx/stl_gui`）里的文件一个字都不记；`interface/` 和 `localisation/` 不参与多人校验，所以不影响多人兼容；这和 live2d 插件的做法一致（放在引擎不读的文件夹里，没有宿主时 mod 什么都不做）。
6. **声明的数据绑定只用引擎自己求值的东西**：日期、资源、国家统计、`button_effect` 的可用状态与执行、loc 键。✅ 实测：声明文件 → 宿主 → 真实鼠标点击 → 引擎命令 → 状态联动。**mod 脚本里算出来的数值（变量、`scripted_loc`、脚本值）当时还显示不了，后来做到了**：声明文字里写 `[Root.xxx]`，宿主把它和玩家国家的 `CEventScope` 交给引擎的 `CGameText::ProcessWithScope`，见 `gui_scoped_localisation.md`。
7. **声明面板和插件面板走同一个注册表**：宿主自己的声明渲染器就是一个"消费方"，这样两条接入路径的行为（可见性、故障隔离、卸载处理）一致，不会各写一套。
8. **开销**：4 个面板合计平均 **约 110 微秒/帧**（含它们自己生成控件的时间），约占一帧（~21 ms）的 0.5%。✅
9. **没验证的**：多人下的行为（ImGui 是否可用、命令是否按预期同步）；`imgui off/on` 重启上下文时已连接的消费方（设计上每次回调都带最新的上下文指针，没单独测）；作用域本地化；字体里没有的生僻字。

需要你拍板的事项在 §10。

## 1. 约束：启动器规范和现有插件

| 约束 | 来源 | 对接口设计的影响 |
|---|---|---|
| 清单 `stl-plugin.json` 没有依赖或加载顺序字段；插件按 playset 顺序**依次**加载，各自等待 | `D:\stellaris-Launcher\docs\PLUGINS.md`、手册 §5.4 ✅ | 消费方不能假设宿主已加载：要在自己的线程里轮询发现。启动器可以加一个可选的顺序提示，但接口不能依赖它 |
| 插件**不得加载其他插件**，不得装加载器 | 手册 §12 ✅ | 发现只能是"宿主已经在进程里就用"，用 `GetModuleHandle` + `GetProcAddress` 即可，不违规 |
| `DllMain` 在启动器创建的远程线程上、持有加载器锁，不得等待、不得加载库 | 手册 §6.1 ✅ | 发现和注册必须在插件自己的线程里，不能在 `DllMain` 里 |
| 启动器**从不 `FreeLibrary`**；插件**不得**自我卸载 | 手册 §6.6 ✅ | 生产环境里宿主永远不会被卸载，消费方持有宿主函数表指针是安全的；注销接口只为开发热重载保留（展示 DLL 的卸载事件是开发用的，违反 §6.6，不能进产品） |
| 多个插件常钩同一个函数（`Present`）；钩子必须调用原函数、不得假设自己第一或最后 | 手册 §6.5 ✅ | **接口不能要求插件自己钩任何东西**；宿主也不钩 Present（会和 Steam 叠加层递归爆栈，见 `gui_imgui_feasibility.md` E11）。宿主只钩三个引擎函数：`ImGui::NewFrame`、`ImGuiInit`、`HandleTurnTick` |
| 建议插件静态链接 CRT（`/MT`） | 手册 §6.7 ✅ | 不同插件有各自的堆：**接口里没有任何分配所有权跨边界**，不传 `std::string` 等 C++ 类型；自带 ImGui 的插件要把 ImGui 的分配器指向引擎的那一对 |
| 引擎调用只能在主线程 | 手册 §6.3 ✅ | 回调在主线程；注册可以来自任何线程，宿主内部加锁；其他接口只能在回调里调 |
| 现有三个插件（perf、live2d、桥接器）**都没有对外导出接口**；对外控制靠各自的命名管道加 Python 脚本 | 在 `D:\stellaris-perf`、`D:\stellaris-live2d` 里搜 `GetProcAddress` / `dllexport` ✅ | 这是第一个有插件间接口的插件，没有既有约定要兼容，但也没有先例可抄；接口需要自己立规矩（§4） |
| 每个插件仓库带一份由主仓库 dumper 生成的 SDK 子集（`sdk/stellaris_sdk.hpp`），游戏补丁后由维护者重新提取 | perf 的 `tools/extract_sdk.py`、live2d 的 `tools/` ✅ | 第三方开发者**无法自己重新生成 SDK**（dumper 需要私有的 Linux 反编译），所以宿主要把常用数据通过接口给出来，让他们不必带 SDK（§4.5） |

## 2. 先例

- **ReShade 附加组件接口**📎：附加组件在 `DllMain` 里调用 `reshade::register_addon(hinstDLL)`，它"在当前进程里寻找 ReShade 实例并在找到后初始化 API"；附加组件可选地导出 `AddonInit` / `AddonUninit`；`register_overlay(title, callback)` 注册一个叠加层回调，回调在 ReShade 构建 GUI 时被调用，窗口的 `Begin/End` 由宿主做；ImGui 的共享方式是"在 `imgui.h` 之后包含 `reshade.hpp`，它会把所有 ImGui 函数改写成使用宿主管理的那个实例"，附加组件不需要自己编译 ImGui；文档称该接口"通过 ImGui 函数表"设计成不重新编译也能支持旧的附加组件（⚠ 这一点我只看了摘要，没读它的源码）。**借鉴**：宿主管窗口的 `Begin/End`（我们的 `STL_PANEL_WINDOW`）、版本化的接口、注册回调而不是让插件自己钩。**不照搬**：整张 ImGui 函数表（§4.4）。
- **SSE-ImGui（Skyrim SE）**📎：靠 SKSE 的消息机制，在"加载后"阶段注册自己的监听者，在"输入就绪"阶段把 ImGui 接口广播给其他插件，插件在这个接口里注册并渲染；接口"不是线程安全的，因为渲染和输入都在同一线程"。**借鉴**：生命周期阶段的概念、"回调在渲染线程"这条规则要写明。启动器没有消息机制，所以我们用"导出函数 + 插件轮询"代替广播。
- **Dear ImGui 官方对跨 DLL 的说明**📎：堆和全局变量不跨 DLL 共享，每个 DLL/静态库边界都要调用 `SetCurrentContext()` + `SetAllocatorFunctions()`；热重载同理；并且"通过共享库使用 Dear ImGui 不被推荐，不保证前后向 ABI 兼容"。**含义**：方案 A 正是官方描述的做法，但它把"两边是同一个 ImGui 版本和同一份配置"的责任交给了我们，所以运行期布局核对不是可有可无的（§4.6）；这也是不把"ImGui 函数表"当作稳定 ABI 的理由。
- **live2d 插件（本地先例）**✅：扫描 `dlc_load.json` 的 `enabled_mods`，经 `.mod` 的 `path=` 找到每个 mod 的文件夹，另有开发用的 `extra_mod_dirs`；声明放在引擎不读的文件夹（`gfx/portraits/live2d/*.txt`）以免 `error.log` 里出现 `Unexpected token`；用游戏自己的脚本语法；"没有插件时 mod 什么都不做，游戏画它一直画的东西"；文件变化时重新扫描。**照搬**：以上全部（§5）。

## 3. 三类接入者

| 接入者 | 要什么 | 接入点 |
|---|---|---|
| 插件开发者（C / C++ / Rust 等） | 画自己的面板和 HUD，读游戏数据，触发游戏动作，不必自己钩任何东西，不必带 SDK | **C 接口**（§4）：`stellaris_gui_api.h`，可选的 ImGui 绑定头 |
| mod 作者（不写 C++） | 给自己的 mod 加一个面板：文字、数值、按钮，按钮触发 mod 自己的脚本；写错了要有提示；多语言 | **声明语法**（§5）：`interface/stl_gui/*.txt` |
| 外部工具（Python、MCP 智能体） | 让工具能看见 / 操作面板，或把外部数据展示到游戏里 | 本调查不展开：桥接器的 JSON-RPC 管道已经是这个角色；宿主可以以后把"面板注册表"通过桥接器暴露成 MCP 工具（⚠ 没设计） |

## 4. 开发者接口

### 4.1 规则（`api_proto/stellaris_gui_api.h` 头部有同样的文字）

- 纯 C，不跨边界传 C++ 类型，不跨边界转移分配所有权。
- 每个结构体以 `uint32_t size` 开头：调用方填它编译时看到的 `sizeof`，接收方只读写 `size` 以内的成员；新成员只追加。老插件配新宿主、新插件配老宿主都能工作。
- 字符串 UTF-8，NUL 结尾，接收方需要保留时自己复制。
- 回调在主线程，引擎的 ImGui 帧内；接口里除了注册 / 注销，其他都**不是线程安全的**，只能在回调里调。
- 宿主从不在插件注册的回调以外调用插件，并在调用前检查回调代码是否还在映射中。

### 4.2 发现与加载顺序

宿主导出 `StlGui_GetApi(uint32_t requested_version)`，返回函数表指针（不支持请求的版本则返回 `NULL`）。插件的辅助头 `stellaris_gui_client.h` 提供 `stl_gui_try_connect(宿主 DLL 名, 版本)`：`GetModuleHandleW` + `GetProcAddress`。插件在自己的线程里每 250 ms 调一次，直到成功。✅ 实测：两个消费方比宿主早注入，宿主一出现就在 250 ms 内发现并注册（在各自的线程里，不在主线程）；面板在宿主的 ImGui 启动之前就已经注册，没有丢。

### 4.3 面板

`register_panel(desc)` 返回句柄（>0）或 0（id 重复、没有回调）。`desc` 里：`id`（稳定且唯一，`<插件 id>.<面板>`）、`title`、`draw(ctx, user)`、`flags`：

- `STL_PANEL_WINDOW`：宿主开一个窗口（`Begin(title)`）包住回调并保证 `End`，回调只管内容。窗口默认可移动、可关闭（关闭只是隐藏；重新打开要靠宿主的面板列表——⚠ 原型里没有这个窗口，只有 `panel <id> 0/1` 命令）。
- `STL_PANEL_OVERLAY`：回调自己画任何东西（HUD、自己开窗口），宿主只在之后复原 ImGui 的栈。

`StlGuiCallbackCtx` 带：API 版本；引擎的 ImGui 上下文指针、分配器对、版本号；引擎 ImGui 的 `sizeof` 四个量（§4.6）；绘制函数表 `ui`；宿主函数表 `api`；宿主的三种字体（`ImFont*`）。上下文指针**每次回调都传**，因为引擎重启 ImGui 时会换。

### 4.4 绘制方式的比较

| | A 自带 ImGui | B 宿主导出函数表（ReShade 式） | C 精简的 C 绘制表 |
|---|---|---|---|
| 插件要带什么 | ImGui 1.85 源码（默认 `imconfig.h`）+ 一个绑定头 | 只要 `imgui.h` 和生成的转发头 | 只要 `stellaris_gui_api.h` |
| 语言 | C++ | C++ | **任何**能调 C 函数的（C、Rust、Python ctypes…） |
| 能用多少 ImGui | 全部，包括自定义控件、ImDrawList、ImPlot（若也自带） | 全部 | 精简：文字、按钮、复选框、滑块、进度条、提示、光标和可用区域、占位、`DrawList` 的线 / 矩形 / 圆 / 文字 |
| 版本锁定 | **插件必须是 1.85 且配置相同**，否则靠运行期核对拒绝 | 宿主一份，插件按宿主的版本编译；宿主升级 ImGui 时旧表要继续提供（ReShade 的做法，维护成本高） | **无**：接口不暴露 ImGui，引擎换 ImGui 版本也不影响插件 |
| 宿主成本 | 零（已实现） | 大：生成并长期维护约 700 个函数的转发表，每种 ImGui 版本一张 | 小：16 个函数（已实现） |
| 实测 | ✅ `/MT` 的插件同时画，2759+ 次回调无问题 | ⚠ 没做 | ✅ 纯 C 的插件，没有任何 ImGui 代码 |

**推荐 A + C，B 等有需求再做。** C 是所有语言的底线，也是 mod 作者的声明渲染器会用到的那一类能力；A 给需要完整 ImGui 的 C++ 开发者，代价由防护兜底（§4.6）。

### 4.5 数据和动作

不需要带 SDK 就能做的事（`StlGuiApi`）：

- `get_snapshot`：日期、速度、暂停、玩家国家 id 与名字、最多 32 项资源（库存、月净值、上限），带 tick 计数。宿主只在 tick 之间更新，所以回调读到的永远是一致的快照（UI 回调有三到四成发生在 `HandleTurnTick` 里面，这条规则对开发者隐形）。
- `effect_state(key, reason)` / `post_effect(key)`：mod 的 `common/button_effects` 条目的可用状态（引擎自己求值 `potential` / `allow`，拒绝时给出引擎生成的原因文字）和执行（走 `CExecuteButtonEffectCommand`，已验证单人可用，见 `gui_imgui_feasibility.md` E13）。
- `set_speed` / `set_paused`：经引擎自己的 setter，排队到 tick 之间。
- `log`：写进宿主的日志，带插件 id 前缀。

第三方想读更多（舰队、行星……）就仍要带 SDK 并自己读内存；这条路不被接口阻止，但需要维护者发布每个游戏版本对应的 SDK 子集。是否要在接口里逐步加更多快照字段（舰队摘要等），取决于使用情况。

### 4.6 故障隔离和防护（宿主的责任）✅ 除"三次故障后停用"外都实测过

| 情形 | 宿主的行为 | 实测 |
|---|---|---|
| 回调抛出异常（空指针访问） | SEH 捕获，记录异常码，复原 ImGui 栈，游戏继续；三次故障后停用该面板 | `0xC0000005` 被捕获，面板下一帧照常被调用 |
| 回调留下没配对的 ImGui 栈 | 调用前后比较 `CurrentWindowStack`、`GroupStack`、`ColorStack`、`StyleVarStack`、`FontStack`，复原多出来的 | 一个插件故意留下窗口、分组、颜色、样式变量、字体各一项，宿主复原了 5 项，后面的面板不受影响 |
| 插件没注销就被卸载 | 每帧调用前用 `VirtualQuery` 检查回调地址是否仍是已提交的可执行内存，否则摘除 | 无崩溃，下一帧摘除；⚠ 极端情形下该地址可能被别的模块重新占用，只是尽力而为 |
| 插件正常注销 | `unregister_panel`，宿主标记为死亡 | 干净摘除 |
| 自带 ImGui 的配置与引擎的不同 | 回调上下文带 `sizeof(ImGuiIO)`、`ImGuiStyle`、`ImDrawVert`、`ImDrawIdx`；绑定辅助函数比对，不一致返回 `false` 并给出原因，插件不绘制 | 32 位 `ImDrawIdx` 的插件："refusing to draw: sizeof(ImDrawIdx) differs from the engine's"，其余面板正常 |
| 宿主编译期 | 宿主用 `static_assert` 对照 SDK 里 dumper 从引擎代码里读出的布局常量（见 `tools/sdk_dumper/CHANGELOG.md`） | ✅ |

宿主不能防的：插件把堆或 ImGui 内部状态写坏、死循环、在回调里做长时间阻塞。这些靠规范（回调要短）和以后的每面板计时（⚠ 没做）。

### 4.7 版本与兼容

- `StlGuiApi::version` 和 `STL_GUI_API_VERSION` 是主版本：删除或改变语义才加；追加成员不加。
- 插件请求一个版本，宿主支持就返回表，不支持返回 `NULL`；插件应当友好降级（不注册，写日志）。
- 游戏版本与接口版本**分开**：接口里有 `game_exe_timestamp`（宿主是为哪个 exe 构建的），启动器按清单里的 `exe_timestamps` 在加载宿主前就挡掉不匹配的游戏版本；消费方插件如果不碰引擎内存就**不需要**自己的 `exe_timestamps` 限制，宿主在游戏更新后换版本即可，消费方不必重新编译。这是做公共库的主要价值之一。
- 引擎的 ImGui 版本变化：宿主按新版本重新编译；方案 C 的插件不受影响；方案 A 的插件要按新版本重新编译，否则被布局核对拒绝（不崩溃）。

### 4.8 原型清单（`gui_probe/api_proto/`）

| 文件 | 内容 |
|---|---|
| `stellaris_gui_api.h` | 公共 C 接口（面板、快照、动作、绘制表、回调上下文） |
| `stellaris_gui_client.h` | 发现宿主的头文件（纯 C） |
| `stellaris_gui_imgui.hpp` | 自带 ImGui 的插件用的绑定与布局核对 |
| `consumer_a.cpp` | 消费方 A：自带 ImGui、`/MT`；有"故意出错"、"故意泄漏 ImGui 栈"、两种卸载方式的开关 |
| `consumer_b.c` | 消费方 B：纯 C，没有 ImGui |
| `CMakeLists.txt` | 另含 `consumer_abad`（32 位索引配置，负面测试） |
| `../showcase/showcase.cpp` | 宿主一侧：注册表、分发、故障隔离、导出函数、声明面板渲染 |

## 5. mod 作者的声明语法

### 5.1 放在哪里 ✅

候选 `interface/stl_gui/`、`common/stl_gui/`、`gfx/stl_gui/`，每个放一个带未知键的探针文件，游戏启动后查 `error.log`、`game.log`、`setup.log`、`debug.log`：**引擎对三个文件夹都没有任何记录**；宿主的扫描同时确实读到了这 4 个文件（对照，证明 mod 被加载了）。

**选 `interface/stl_gui/*.txt`**：

- `checksum_manifest.txt` 只列 `common/*.txt|*.shader|*.csv`、`events/*.txt`、`map/*.txt|*.shader`；`interface/` 和 `localisation/` **不参与多人校验**（`gui_native_system.md`，读自游戏文件 ✅）。声明文件放在 `common/` 下会让每个客户端必须一致，没有理由；放 `interface/` 下玩家可以有不同的 UI 而不触发 OOS。
- 语义上就是界面。
- 动作（`common/button_effects`）本来就在被校验的 `common/` 里，所有客户端一致，引擎在每个客户端重新检查 `potential` / `allow`——这正好是"UI 只能通过命令改状态"。

### 5.2 语法 v1（原型已实现，`showcase.cpp` 里的 `LexScript` / `ParseScriptBlock` / `DrawDeclNode`）

和游戏一样的 Paradox 脚本：`key = value`、`key = { ... }`、`#` 注释、引号字符串、重复的键按顺序保留（一个块里可以有多个 `button`）。

```
stl_gui_version = 1                  # 必须；没有或不是 1，整个文件被忽略并记日志

panel = {
	id = overview                    # mod 内唯一；宿主里的 id 是 "<mod 名>:<id>"
	title = MYMOD_TITLE              # loc 键；含空格的文字原样显示
	size = { 380 440 }               # 可选，首次打开时的大小

	content = {
		text      = { text = MYMOD_INTRO }
		separator = yes
		spacer    = 6                # 像素
		date      = { label = MYMOD_DATE }
		value     = { label = MYMOD_ENERGY  resource = energy  show = stock }     # stock | net | income | expense | max
		gauge     = { label = MYMOD_MIN     resource = minerals }                  # 库存对上限的进度条
		stat      = { label = MYMOD_COL     stat = colonies }                      # colonies pops empire_size military_power tech_power economy_power
		badge     = { probe = my_effect  yes = MYMOD_ON  no = MYMOD_OFF }          # 一个 button_effect 当作"现在允许吗"的问题
		row = {                      # 横向排列
			button = { text = MYMOD_SET    effect = my_set_effect }
			button = { text = MYMOD_CLEAR  effect = my_clear_effect }
		}
	}
}
```

规则：

- **未知的键或元素**：忽略并写日志（不是错误），这样旧宿主能读新文件的一部分；**语法错误**：整个文件忽略，日志里给出文件路径。✅ 实测。
- **版本**：`stl_gui_version` 只加不改：v1 的元素永远保持含义，新元素放进 v2+ 的文件；设计上宿主读所有 `≤ 当前版本` 的文件（原型只认 `1`）。
- **一个文件多个面板**、**一个 mod 多个文件**都可以；同一个 mod 里重名的面板 id，先注册的保留、后来的被拒绝并记日志（不同 mod 之间用 `<mod 名>:` 前缀隔开）。

### 5.3 数据绑定的分层

| 层 | 内容 | 状态 |
|---|---|---|
| T0 已实现并实测 | 日期；资源（库存 / 月净值 / 收入 / 支出 / 上限）；国家统计（殖民地、人口、帝国规模、军事 / 科技 / 经济力量）；`button_effect` 的可用状态与引擎生成的原因文字；按钮执行；loc 键 | ✅ |
| T1 用现有手段能做 | **用 `button_effect` 当布尔查询**：mod 写一个 `effect = { }` 为空、`potential` / `allow` 里写任意触发器的 `button_effect`，面板用 `badge` 显示它是否成立，原因文字（引擎对触发器的描述）可以当提示。这样 mod 作者能把**任意触发器**（国家旗标、科技、事件目标……）显示出来，宿主不用解析旗标或变量 | ✅ 原型里的"国家旗标已设置 / 未设置" |
| T2 ✅ **已实现**（`gui_scoped_localisation.md`，只有玩家国家的 scope） | **显示脚本算出的数值**（变量、脚本值、`scripted_loc`）。下面是实现前的分析：游戏原生 GUI 的做法是：先在 effect 里把值存进变量，再在本地化里写 `[Root.my_var]` 或 scripted_loc，由引擎在当前作用域里求值（`gui_native_system.md`：只能读已存的变量或 `scripted_loc` 的结果）。宿主需要"带作用域的本地化"：引擎里是 `CGameText::GenerateString(CString&, SStringToken const&, bool)`（Linux 反编译第 391523 行），作用域在 `CGameText` 对象内部而不在参数里，所以要构造或取得一个带玩家国家作用域的 `CGameText`（我们已经能构造玩家国家的 `CEventScope`，见 E13）；还要在 Windows exe 里给它加指纹。预计规模：中等，需要一次逆向 | ⚠ 没做，建议作为 M2 的第一项 |
| T3 以后 | 可见条件（`visible_when = { probe = ... }`）、停靠位置、快捷键、选项卡、图标、列表 | ⚠ 设计未定 |

### 5.4 本地化

标题、文字、按钮文字都用 loc 键，经引擎的 `PdxLocalize`（和桥接器同一个函数）取；mod 带自己的 `localisation/<语言>/*.yml`。✅ 实测：测试 mod 的中文 loc 被取到（"Mod 面板（脚本声明）"、"注入 100 能量币"…）。含空格的值或引擎不认识的键原样显示。窗口标题用 `显示文字###内部 id`，语言切换时窗口位置和状态不丢。

**已知问题：字形。** 引擎的 ImGui 字体图集在 ImGui 启动时一次建成，不能再加字形；现在只含常用汉字和宿主自己界面里的字，mod 的 loc 里的生僻字会显示成 `?`。**解法（⚠ 没做）**：宿主在启动 ImGui 之前就扫描启用的 mod，把它们 `localisation/` 文件里出现的字符并入字形范围——宿主的 ImGui 是由我们的钩子在游戏里启动的，扫描可以先做。

### 5.5 动作与多人

声明里的按钮只能引用 `common/button_effects` 里的条目（按键名），宿主通过 `CExecuteButtonEffectCommand` 执行：所有客户端用同一份（被校验的）脚本，引擎在每个客户端重新检查 `potential` / `allow`。声明文件本身不能执行任何东西，也没有自己的表达式语言。⚠ 多人没实测。

### 5.6 写错了怎么办、编辑器支持

- 实测：语法错误的文件被忽略并记日志（含路径），缺 `stl_gui_version` 的文件被忽略并记日志，同一个 mod 里正确的面板不受影响；`scan` 命令重新扫描会摘掉旧面板、注册新面板，窗口位置因为 `###id` 保持不变 ✅。
- 现状的缺口：错误只写进宿主的日志，mod 作者在游戏里看不到。**建议**宿主有一个"问题"面板（列出每个声明文件的错误和警告），并监视文件修改时间自动重扫（live2d 就是这样做的）。
- 编辑器：CWTools 的 Stellaris 配置（启动器的冲突检查文档里已经引用了它，MIT）支持自定义规则文件；给 `stl_gui` 写一份 `.cwt` 规则，mod 作者在 VS Code 里就有补全和校验。另外提供一个独立的校验脚本（离线，不需要游戏）。⚠ 没做；live2d 的记忆里"linter 需要讨论"同样适用。

## 6. 实测记录

接 `gui_imgui_feasibility.md` 的 E1–E15。测试存档 `fmbase`，测试 mod `zz_gui_showcase`，Stellaris 4.5.2。

| # | 做了什么 | 结果 |
|---|---|---|
| E16 | 两个消费方比宿主先注入；宿主后注入，之后才自启动 ImGui | 消费方在宿主出现后 250 ms 内发现并从各自的线程注册；宿主的 ImGui 5 秒后启动，注册没有丢 ✅ |
| E17 | 消费方 A（自带 ImGui 1.85、`/MT`）、消费方 B（纯 C、`/MT`）、宿主（`/MD`）同时往引擎的上下文里画 | 2759+ 次回调、无崩溃；A 的 `PlotLines`、`DrawList`、`PushFont`（宿主的字体）、B 的绘制表都正常；窗口并排显示 ✅ |
| E18 | A 的回调里空指针访问 | 宿主捕获 `0xC0000005`，记日志，复原栈，游戏继续，面板下一帧照常（第 1 次故障，共 3 次容忍）✅ |
| E19 | A 的回调留下一个窗口、分组、颜色、样式变量、字体 | 宿主日志 "left 5 ImGui stack entries open, restored"，后续面板不受影响 ✅ |
| E20 | B 不注销就被卸载（`FreeLibraryAndExitThread`） | 下一帧 "its code is no longer mapped … dropped"，无崩溃 ✅ |
| E21 | A 正常注销再卸载 | 干净摘除 ✅ |
| E22 | mod 声明的面板（`interface/stl_gui/zz_overview.txt`）：宿主扫描启用的 mod、解析、渲染；用真实鼠标点「设置旗标」 | 日志 "mod scan: 4 file(s), 1 declared panel(s)"；中文 loc 取到；点击后按钮效果经引擎执行，`badge` 由红变绿，「设置旗标」变灰（悬停显示引擎的拒绝原因"× 没有标识 zz_gui_marked"）、「清除旗标」变可用 ✅ |
| E23 | 三个候选文件夹各放探针文件 | 引擎在所有日志里都没有记录；宿主同时读到了这些文件（对照）✅ |
| E24 | 语法错误的声明文件 / 缺 `stl_gui_version` 的文件 / 重新扫描 | 前两者被忽略并记日志，正确的面板不受影响；重扫替换旧面板 ✅ |
| E25 | 自带 ImGui 但 `ImDrawIdx` 为 32 位的消费方 | "refusing to draw: sizeof(ImDrawIdx) differs from the engine's"，其余面板正常 ✅ |
| E26 | 分发开销 | 3 个面板 106.6 µs/帧、4 个面板 109.5–133.1 µs/帧（两次运行，含各面板自己生成控件的时间）✅ |

没测或没读的：多人；ReShade 函数表的实现细节（只看了文档摘要）；`imgui off/on` 与已连接消费方；消费方自己加字体；非 MSVC 工具链（MinGW、Rust）编出来的消费方（C ABI 理论上可以）。

原型里已知的粗糙处（不是接口问题）：窗口位置按注册顺序排列，会重叠；提示框（tooltip）字号没有按界面缩放；`ui_scale` 没有放进回调上下文（现在宿主只对 `WINDOW` 类面板调了 `SetWindowFontScale`，`OVERLAY` 类拿不到）。

## 7. 仓库形态和分发（独立仓库前的建议）

```
stellaris-gui/                         插件 id: stellaris-gui，DLL: stellaris_gui.dll（原型里叫 gui_showcase.dll）
  plugin/stl-plugin.json  defaults/    启动器规范 v2；game.exe_timestamps 列出支持的游戏版本
  src/                                 宿主：三个钩子、快照、注册表、分发与故障隔离、声明面板的解析与渲染、日志
  include/stellaris_gui/               stellaris_gui_api.h  stellaris_gui_client.h  stellaris_gui_imgui.hpp   （只有头文件，宽松许可）
  sdk/stellaris_sdk.hpp                主仓库 dumper 的子集，和 perf、live2d 同样的提取方式（tools/extract_sdk.py）
  examples/                            c/、cpp-imgui/、mod/（一个完整的示例 mod：声明、button_effects、loc）
  docs/                                开发者指南、mod 作者指南、语法参考、接口变更记录
  tools/                               check_panels.py（离线校验声明文件）、cwtools/stl_gui.cwt、extract_sdk.py
  tests/                               解析器的离线测试；宿主的自检
```

分发：GitHub 发行版（启动器的 `update.github`）里放插件包；另外一个 `stellaris-gui-sdk-<版本>.zip`（头文件 + 示例 + 校验脚本）给开发者。CI 构建、跑解析器测试、校验清单与 SDK（和 perf 一样的 `check_plugin.py`）。

**展示用的"指挥甲板"建议不并进宿主**，做成宿主的第一个消费方插件（`stellaris-gui-deck` 之类）：既是接口的参考实现，又保证宿主不依赖皮肤。宿主自己只带一个"面板列表 / 问题"窗口和快捷键。

对启动器（你的另一个仓库）的建议：清单里加一个**可选**的 `"after": ["stellaris-gui"]` 加载顺序提示，让消费方在宿主之后加载（省掉一点轮询），但接口不依赖它。

## 8. 风险和未决

| 项 | 说明 | 建议 |
|---|---|---|
| ImGui 版本和配置锁定（方案 A） | 插件必须用 1.85 默认配置；引擎升级 ImGui 时所有 A 类插件要重编 | 已有运行期核对（不崩溃）；文档写明；以后真有需求再做 B |
| 多人 | ImGui 在多人下是否可用没验证；声明的按钮在多人下的命令同步没验证 | 在做独立仓库前，找一次联机实测（至少两个客户端，一台加载宿主、一台不加载） |
| 字形覆盖 | 图集一次建成 | §5.4 的"启动前扫描 mod 的 loc"；开发者插件用到特殊字形需要宿主的 `request_glyphs`（⚠ 没设计） |
| 作用域本地化（T2） | 显示脚本数值要它 | M2 的第一项研究 |
| 回调耗时 | 一个慢回调拖慢整个帧 | 每面板计时并显示在"面板列表"窗口；超过预算时警告（⚠ 没做） |
| 热键与菜单冲突 | 多个插件抢同一个键 | 宿主提供热键注册表（带冲突检测）；SKSE Menu Framework 有同样的设计📎，⚠ 没细读 |
| 窗口布局保存 | 引擎的 ImGui 默认写 `imgui.ini` 到游戏目录，违反启动器规范 | 宿主把 `IniFilename` 指到自己的 `config\`，或只保存自己面板的位置（⚠ 没做，当前每帧置空） |
| 开发热重载 | 重载宿主会让消费方持有的函数表指针悬空 | 生产环境宿主永不卸载，所以不是问题；开发时消费方应当检查宿主的模块句柄是否变了并重新连接（原型没做） |
| 兼容性承诺 | 谁来保证 C ABI 的 v1 不破 | 把 `stellaris_gui_api.h` 的变更放进 `CHANGELOG`，评审时看"只追加"；在 CI 里对头文件做一次 ABI 快照对比（⚠ 没做） |
| 许可 | 头文件要让闭源插件也能用 | 头文件用宽松许可（MIT / Apache-2.0）；宿主的许可你定 |
| 名字 | 宿主的 DLL 名、插件 id 现在是原型名 | 见 §10 |

## 9. 里程碑

1. **M0 宿主 MVP**：把展示 DLL 里属于宿主的部分抽成独立仓库的插件骨架（遵守启动器规范：`config\`、`logs\`、`GetModuleHandleEx` 找自己的目录、不自我卸载）；API v1 冻结候选；INI 重定向；日志文件；"面板列表 / 问题"窗口；展示指挥甲板改成独立的消费方。
2. **M1 声明语法加固**：元素集定稿；错误显示在游戏里；文件修改后自动重扫；启动前扫描 loc 并入字形；CWTools 规则 + 离线校验脚本；示例 mod 与文档。
3. **M2 脚本数值显示**：作用域本地化的逆向与指纹（进 dumper）；`value` / `text` 支持带作用域的 loc；可选地直接读旗标和变量。
4. **M3 体验**：热键与菜单注册表；窗口布局持久化；主题 / 缩放 API（`ui_scale` 放进上下文）；每面板计时。
5. **M4 联机与外部工具**：多人实测与结论；经桥接器把面板注册表暴露给 MCP。

## 10. 需要你决定的

1. **仓库的名字、账号、许可**（头文件建议宽松许可）。
2. **插件 id 和 DLL 名**：建议 `stellaris-gui` / `stellaris_gui.dll`（原型的 `gui_showcase.dll` 只是临时名；接口里 `StlGui_GetApi`、头文件前缀 `Stl` / `STL_` 也可以一起改）。
3. **声明文件夹** `interface/stl_gui/*.txt` 是否接受（实测依据在 §5.1）。
4. **"指挥甲板"是并进宿主还是做成独立的消费方插件**（建议独立）。
5. **要不要给启动器加可选的加载顺序提示**（接口不依赖它）。
6. **v1 的元素集**：现在是 text / separator / spacer / date / value / gauge / stat / badge / button / row；T2 以后的元素是否要在 v1 里预留名字。

## 11. 资料

- ReShade 附加组件接口参考：<https://github.com/crosire/reshade/blob/main/REFERENCE.md>
- SSE-ImGui（SKSE 插件用消息机制向其他插件提供 ImGui）：<https://github.com/ryobg/sse-imgui>
- Dear ImGui 关于 DLL 边界的说明（提交"Added commentary about DLL boundaries"，#3836）：<https://skia.googlesource.com/external/github.com/ocornut/imgui/+/6f4b9c65ae87bf99cadbaf60967be4f8171c0bb8%5E%21/>
- 启动器的插件规范与开发者手册：`D:\stellaris-Launcher\docs\PLUGINS.md`、`PLUGIN_HANDBOOK.md`
- live2d 的 mod 声明设计：`D:\stellaris-live2d\docs\portrait-mod-design.md`
