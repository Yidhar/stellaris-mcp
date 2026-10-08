# Stellaris 4.5 原生 GUI 系统研究

> 问题："为什么用原生 mod 做自定义 UI 这么不方便，它的边界到底在哪？"
>
> 方法：
> - 反编译（Linux 构建）`source/stellaris_4.5_source.cpp`，用 mmap 流式扫描。
> - 游戏文件：`interface/**/*.gui|*.gfx`、`common/button_effects`、`common/scripted_loc`、`unchecked_defines`、`checksum_manifest.txt`。
> - Windows `stellaris.exe` 的字符串（只确认字符串存在，没有定位函数 RVA）。
> - 两个真实 mod：Ancient Empire（AE，2389589691）与 Voyage to Eternity（VoE，2766998502）。
>
> 证据标记：
> - ✅ 已读到：反编译函数 + 行号，或游戏文件路径。
> - ⚠ 推断：没有直接读到，或有不确定处，会写明。
>
> 行号都是 `stellaris_4.5_source.cpp` 的行号。token 号来自 `tools/sdk_dumper/linux_tokens.json`（Windows 与反编译的 token 号相同）。
> 结构体偏移是 Linux 的 GCC 布局，本文只报语义，不报 Windows 偏移。

---

## 0. 简短结论

1. 原生 GUI 不是数据绑定框架，而是"声明式布局 + 命令式 C++ 绑定"。
   - `.gui` 只描述外观和位置。每个窗口背后都是一个 C++ 类，类的构造函数按名字取窗口模板（`CGuiView::CGuiView(name)` → `CGui::GetGuiType(name)`，7130411）。
   - 运行时 C++ 按名字查子控件再写入。引擎里 `CContainerWindow::Get*Recursive*` 约 1500 处，`ChangeString(` 约 2100 处（粗略正则，含定义）。
   - `.gui` 里没有 `visible`/`enabled`/`datacontext`/`onclick` 这类绑定语法。
2. 脚本能影响 UI 的通道只有三条：
   - 事件窗口（`custom_gui` + 选项）。
   - `effectButtonType`，每帧求值 `potential`/`allow`/文本，点击发出 `CExecuteButtonEffectCommand`。
   - 带 scope 的本地化命令（`[Root.GetName]`、`[Scope.变量]`、`scripted_loc`）。
3. 只有 `effectButtonType` 能被脚本控制显隐、可用性和文本。图标、帧、进度条、滚动条、勾选框、输入框的值都归 C++。
   - 因此 mod 的"滑条/状态指示"只能用大量叠放的按钮伪造。AE 的一行难度条用 30 个 effectButton，18 档各配一个阈值 `potential`。
4. 没有脚本可达的文本输入、动态列表、可变数量的控件。唯一的动态列表是事件的 `option_list`（每个选项一个容器，容器名由 `custom_gui` 指定）。
   - AE 的 `ag_auto_starbase.2` 用了 99 个 option。
   - AE 的国家选择器是 12 个固定槽位，加 `event_target:..._marked_country_N`。
5. UI 状态就是游戏状态。翻页、选中项都存成国家/全局变量，通过 `CExecuteButtonEffectCommand` 走命令通道，进存档，也进多人同步和 checksum（`common/*.txt` 被校验，`interface/` 不被校验）。
6. 覆盖机制很粗：
   - 顶层控件按名字（不区分大小写）进全局表，后加载者整体替换，没有合并。
   - 文件按 VFS 排序后依次读取，所以 mod 才会用 `!!!_` / `zzy_` 前缀抢顺序。
   - 想往原版窗口里加一个按钮，就得复制整个窗口。AE 复制的 `fleet_view.gui` 是 1936 行，而 4.5 原版已是 2354 行，所以被它自己改名成 `.bak`。
7. 开发体验差：
   - 没有 include/组件/模板。靠 `@变量` 和 `inline_script` 生成 `button_effects`（VoE 做法）。
   - 分辨率适配要每个坐标写两遍（AE 有 381 个 `if_scaled_resolution` 块、221 个 `_scaled` 变量）。
   - 报错只写日志。只有 `.gui`/`.gfx`/贴图/本地化能热重载（`reload_gui`），`button_effects` 和事件不能。
8. 每帧成本：`CGameIdler::Idle` 每帧调用 `CEffectButton::UpdateAllInstances`（400540），对所有"父级已渲染"的 effectButton 求值 `potential`+`allow`+文本。AE 约有 1900 个 effectButton。
9. 对 DLL 插件，最有价值的几个入口（见 §6）：
   - 直接投递 `execute_button_effect` 命令（SDK 已有）。
   - 在主线程用 `CGui::ReloadFile` 运行时注入 `.gui`。
   - 对已有命名控件调用 `ChangeString`。
   - 挂钩 `GetScriptedLoc` 添加新本地化命令。
   - 复用引擎自带的 Dear ImGui（带文本输入）做完全独立的 UI。
   - 新增控件类型和新增脚本效果原语很难。
10. 本文没能确认的事项（Windows RVA、VFS 排序是否区分大小写、`@变量` 是否文件作用域、必需控件缺失时的行为）集中列在 §7。

---

## 1. 文件格式：`.gui` / `.gfx`

### 1.1 `.gui` 语法

- 顶层必须是 `guiTypes = { ... }`。
  - 证据：`CGui::ReadMember`（8257098）里 token 0x7e（`guitypes`）调用 `CReader::Read<CGuiType*>`（8258785），把每个子块用 `Create` 工厂造成 `CGuiType` 并按名字加入全局树。✅
- 子块形如 `<类型关键字> = { name = "..." ... }`，可嵌套。
  - 子块的子块按"父类型自己的树"登记，不进全局表。✅
  - 例如 `CContainerWindowType::ReadMember`（8215492）里有 `CTernary<CGuiType*>::Add(this+0x2d0, ...)`，见 8216540。
- 关键字不区分大小写。
  - 全局树的 `Add` 对每个字符 `tolower`（8217568）。✅
  - 原版同一个控件混用 `OverlappingElementsBoxType`（134 次）和 `overlappingElementsBoxType`（55 次）。✅
- `@name = 值` 定义变量，`@[ 表达式 ]` 是内联算术。
  - 原版：`planet_view.gui` 有 `x = @[ -shroudPlaneRadius ]`。AE 一个文件里就有数百个 `@ag_*_x_pos`。✅
  - 读取时 `Read<CGuiType*>` 在遇到 `@` 开头的 token 时调用 `ParseAdvancedStatement`（8258826）。✅
  - 作用域：每个文件各自构造一个 `CReader`（`InitGUI` 循环 194305），AE 40 个文件没有任何跨文件引用 `@变量`。⚠ 推断为文件作用域，没读 `CReader` 内部确认。
- 注释用 `#`。
- 没有 include、继承、模板、条件编译。✅ 全库搜索 `.gui` 没有 `include`/`import` 语法。唯一的条件机制是 `if_resolution` / `if_scaled_resolution`（见 §1.3）。
- 出错行为：
  - 未知属性会落到 `CPersistent::ReadMember`（8103618）→ `CReader::ReportUnexpected`，写 `Unexpected token %s. %s`，然后继续加载。✅
  - 同一窗口内同名子控件会写 `Found duplicate <type> "<name>" in window "<win>".`（8215549、8215632、8215679、8215734、8215771、8215808、8215849、8216003、8216183、8216222、8216260、8216331、8216555、8216689）。✅
  - 文件级错误格式 `Error: "..." in file: "..." near line: N`（8103408 附近）。✅

### 1.2 控件类型一览

下表的类映射来自 `CGui::Create`（8257217）的 `switch(token)`：
- 0x7f→`CButtonType`（8257231）、0x85→`CTextBoxType`、0x8c→`CScrollbarType`、0x9b→`CWindowType`。
- 0xa0/0xa3→radio 组、0xa9→`CListboxType`、0xaa→`CEditBoxType`、0xb2→`CPositionType`、0xb3→`CLayoutType`。
- 0xb6→`CIconType`、0xb9→`CDropDownMenuType`、0x120→`CInstantTextBoxType`、0x14e→`COverlappingElementsBoxType`。
- 0x264→`CContainerWindowType`、0x265→`CExtendedScrollbarType`、0x26c→`CDropDownBoxType`、0x288→`CCurveGraphType`、0x396→`CSpinnerType`。

另有 15 个类型名字符串在 `CContainerWindow::GetLookupData<T>`（8199235–8199512）里。✅

其余类型（buttonType、checkboxType、smoothListboxType、gridBoxType、effectButtonType 等）走游戏侧工厂；token 号从 token 表读出。⚠ 工厂分发细节没完整追完。

属性列中的数字是"原版 `interface/**/*.gui` 里的使用次数"（我写的脚本扫描，✅）。

| 关键字 | token | 类型类 → 实例类 | 关键属性（出现次数） | 谁驱动内容 |
|---|---|---|---|---|
| `containerWindowType` | 0x264 | `CContainerWindowType` → `CContainerWindow` | name、size(2941)、position(2742)、background(1688)、orientation(542)、moveable(460)、clipping(454)、origo(175)、margin(50)、dynamic_extra_height(63)、show_position/hide_position/animation_type/animation_time(~50)、if_scaled_resolution(38) | 子控件由 C++ 按名取 |
| `windowType` | 0x9b | `CWindowType` → 固定/可缩放窗口 | fullScreen、horizontalBorder、verticalBorder、dontRender、moveable（总共 24 个） | 旧式，C++ |
| `buttonType` | 0x271 | `CButtonType` → `CButtonStandard` | position(1977)、quadTextureSprite(1306)、orientation(870)、clicksound(820)、spriteType(773)、shortcut(515)、buttonFont/font(~1000)、buttonText(471)、text(187)、pdx_tooltip(416)、format、scale、frame、web_link、actionShortcut | C++ 回调 |
| `guiButtonType` | 0x7f | `CButtonType`（作为 scrollbar/spinner/dropdown 的子按钮，198 次） | quadTextureSprite、parent、tooltip | C++ |
| `effectButtonType` | 0x36c3 | `CEffectButtonType` → `CEffectButton` | 继承按钮全部属性 + `effect = "<button_effect 名>"`（token 0x59） | **脚本** |
| `iconType` | 0xb6 | `CIconType` → `CIcon`（`CIcon` 继承 `CButton`） | spriteType(2015)、position、alwaysTransparent(1486)、quadTextureSprite(787)、scale(644)、frame(238)、centerPosition、rotation、mirror | C++ 设 frame/sprite |
| `instantTextBoxType` | 0x120 | `CInstantTextBoxType` → `CInstantTextBox` | font、name、position、maxWidth、maxHeight、format(2652)、text(2513)、fixedSize(1889)、text_color_code(783)、vertical_alignment、multiline、truncate、scrollbartype、appendtext、bordersize | C++ `ChangeString` |
| `textBoxType` | 0x85 | `CTextBoxType` → `CTextBox` | 旧式，仅 1–2 处 | C++ |
| `editBoxType` | 0xaa | `CEditBoxType` → `CEditBox` | font、size、texturefile、cursor、max_characters、allow_multi_line、use_special_chars、bordersize（共 85 个） | C++（文本不暴露给脚本） |
| `checkboxType` | 0xc4 | → `CCheckBox` | quadTextureSprite、spriteType、clicksound、shortcut（79 个） | C++ |
| `scrollbarType` | 0x8c | `CScrollbarType` → `CScrollbar` | slider、track、leftbutton、rightbutton、minValue、maxValue、stepSize、startValue、horizontal、priority、borderSize（18 个） | C++ |
| `extendedScrollbarType` | 0x265 | → `CExtendedScrollbar` | slider、track、startValue、horizontal（5 个） | C++ |
| `spinnerType` | 0x396 | → `CSpinner` | leftbutton、rightbutton、maxValue、startValue、horizontal（40 个） | C++ |
| `listboxType` | 0xa9 | → `CStandardlistbox` | scrollbartype、spacing、bordersize、autohide_scrollbar、priority（35+8 个） | C++（事件 `option_list` 属此类） |
| `smoothListboxType` | 0x1fb | → `CSmoothListbox` | scrollbartype、spacing、bordersize、offset、autohide_scrollbar（140+105 个） | C++ |
| `gridBoxType` | 0x275 | → `CStandardGridBox`（`CGridBoxBase`） | slotsize(257)、max_slots_horizontal(227)、max_slots_vertical、add_horizontal、padding、format、is_dynamic、resizeparent | C++ 往里填 |
| `OverlappingElementsBoxType` | 0x14e | → `COverlappingElementsBox` | format(left/right/…)、spacing、first_on_top、direction（134+55 个） | C++（事件选项的 trigger/effect 图标） |
| `dropDownBoxType` / `dropDownMenuType` | 0x26c / 0xb9 | → `CDropDownBox` | expandedWindow、expandButton、instantTextBoxType、iconType（13 个） | C++ |
| `positionType` | 0xb2 | `CPositionType`（无实例，命名锚点） | position、dynamic_extra_height（233 个） | C++ 按名取坐标 |
| `layoutType` | 0xb3 | `CLayoutType` | — | — |
| `curveGraphType` | 0x288 | → `CCurveGraph` | 原版 `interface/` 里零使用 ✅ | — |
| `radioButtonGroupType` / `scrollbargroupType` / `multiSpriteButtonType` / `3dButtonType` / `3dIconType` | 0xa0 / 0xa3 / 0xb4 / 0xab / 0xb1 | 遗留类型 | 原版几乎不用 ⚠ | — |

补充：
- 容器里还有 `background = { name quadTextureSprite|spriteType ... }` 子块（4203 次），用于窗口底图。✅
- 图表类型 token（`PieChartType` 0x153、`BarChartType` 0x166、`LineChartType` 0x168）存在，对应类 `C2dPieChartType`、`CLineChartType`；原版 `.gui` 没有用到。✅

**Stellaris 专有的唯一语法扩展**：`effectButtonType`。
- `CGameGraphics::ReadGameSpecificGuiTypeMember`（4254601）只有一个分支：父类型 token 为 0x264（`containerWindowType`）且子 token 为 0x36c3（`effectButtonType`）时，new `CEffectButtonType` 并 `AddButtonType`。✅
- 通用入口是 `CGuiType::ReadMember` 末尾的 `_pReadGameSpecificFunc`（8266550）。✅
- 含义：`effectButtonType` 只能做 `containerWindowType` 的直接子项。⚠ 这是读代码得到的推论，没做实际加载验证。

### 1.3 通用属性与分辨率条件

`CGuiType::ReadMember`（8266446）处理所有控件共有的属性：

| 属性 | token | 说明 |
|---|---|---|
| `name` | 0x1b | 控件名（C++ 按它查找） |
| `tooltip` / `tooltipText` / `delayedTooltipText` | 0x92 / 0x93 / 0x94 | 静态本地化 key |
| `pdx_tooltip` / `pdx_tooltip_delayed` | 0x16c / 0x16d | 静态 tooltip key（原版 416 次） |
| `pdx_tooltip_anchor_offset` / `pdx_tooltip_anchor_orientation` | 0x34f / 0x350 | tooltip 锚点；居中方向会警告"Probably no support for centered…"（8266528） |
| `tooltip_mode_enabled` | 0x3ad | 手柄 tooltip 模式 |
| `hide` | 0x2a7 | 初始隐藏 |
| `dontRender` | 0xa5 | 不渲染 |
| `hint_tag` | 0x1ca | 教程提示标签 |
| `if_resolution` | 0x412 | 以原始分辨率判断（8266500） |
| `if_scaled_resolution` | 0x439 | 以"分辨率 ÷ UI 缩放"判断（8266487–8266497） |

- 两个条件块内可写 `min_width`/`max_width`/`min_height`/`max_height`（token 0x411/0x410/0x40f/0x40e）。
- 条件成立时才读取块内其余属性，用来覆盖（读入同一个对象）。✅
- 布局属性：`position {x y}`、`size {width height | x y}`、`scale`、`orientation`、`origo`、`margin`、`clipping`、`moveable`。
- `orientation` / `origo` 共 9 个取值：`upper_left`、`upper_right`、`lower_left`、`lower_right`、`center`、`center_up`、`center_down`、`center_left`、`center_right`。✅ 见 8169620–8169660 的 `strcmp` 链。

### 1.4 `.gfx`（精灵与字体）

顶层 `spriteTypes = { ... }`。下表为扫描原版 `interface/**/*.gfx`（131 个文件）的结果：✅

| 关键字 | 次数 | 关键属性 |
|---|---|---|
| `spriteType` | 8640 | name、texturefile(7902)、alwaysTransparent、noOfFrames(1214)、effectFile(883，着色器)、animation(641)、masking_texture(611)、default_frame、sprite_sheet_sprite_type、legacy_lazy_load、transparencecheck |
| `corneredTileSpriteType` | 334 | texturefile、bordersize、size、effectFile、noOfFrames、tilingcenter（九宫格） |
| `progressbartype` | 195 | color、colortwo、texturefile1/2、size、effectfile、flipdirection、horizontal |
| `portraittype` | 21 | type、character、masking_texture、alternate_configurations… |
| `flagspritetype` | 18 | bg_position/size、symbol_position/size、masking_texture |
| `frameanimatedspritetype` | 5 | animation_rate_fps、looping、play_on_show |
| `textspritetype` / `animatedmaptext` / `piecharttype` | 4 / 3 / 2 | — |
| `bitmapfont` / `bitmapfont_override` | 13 / 33 | fontfiles、languages、ttf_font、ttf_size、vertical_offset |

- `animation = { animationmaskfile animationtexturefile animationrotation animationlooping animationtime animationdelay animationblendmode animationtype animationframes ... }` 在 `core.gfx` 里可见：按钮的流光动画，由着色器 + 遮罩贴图完成。✅
- 精灵"帧"（`noOfFrames`）只能由 C++ 或 `.gui` 的静态 `frame = N` 选择。脚本不能改帧。
- 进度条：`CProgressbarSprite::SetState(ushort)`（8293997）由 C++ 设置。没有脚本入口。✅
- 精灵注册：`CGuiGraphics::AddSpriteType`（8262142）把新精灵插到哈希桶链表头，`GetSpriteType`（8262263）从链表头找，所以后加载者胜出，旧的不释放。✅
- 精灵名是否区分大小写：`GetSpriteType` 用 `bcmp` 精确比较，但读入时是否已小写没确认。⚠

### 1.5 发现、加载、覆盖

启动流程（`CGameApplication::InitGUI`，194016）：✅
1. `VFSGetEnumeratedFiles("interface", ".gfx", 递归)`（194095）。排除 `load_screen.gfx`、`load_screen_font.gfx`，逐个读入（194197–194201）。
2. `VFSGetEnumeratedFiles(..., ".gui", 递归)`（194241）。排除 `load_screen.gui`，追加 PdxOnline 资源，逐个读入。
3. `CGui::InitGUI`（8257142）：对每个类型调用 `InitPostRead`。
4. `CGuiGraphics::InitSpriteTypes`（194367）：加载贴图。

覆盖规则：
- 文件顺序：`VFSEnumerateFiles`（8009801）对所有挂载目录（原版 + 各 mod）合并后做 `std::__stable_sort` + `std::__unique`（8009857、8009860）。
  - 所以 mod 间的加载顺序就是文件名排序，同相对路径只保留一份（由 VFS 挂载决定谁胜出，⚠ 这一点是 Paradox 通行做法，没在此读 VFS 挂载代码）。
  - 排序比较器 `CPdxArray<CString>::SLessThan` 是否区分大小写，⚠ 没确认。
- 控件覆盖：全局树 `CTernary<CGuiType*>::Add`（8217568）在名字已存在时直接把值指针改成新对象，计数加一，没有任何"重复"警告，旧对象泄漏。✅
  - 所以后加载文件里同名的顶层控件，整体替换前面的，不合并。
  - 重载用 `AddLocked` 变体（8218106）。
- 实证：
  - AE 的 `!!!_ag_icon_placeholder.gfx`、`common/scripted_loc/!!!_ag_overwrite_scripted_loc.txt`、`events/!!!_ag_overwrite_events.txt` 靠前缀抢最先加载；`interface/zzy_overwrite_fleet_view.gui.bak` 的前缀 `zzy_` 想抢最后加载。✅
  - AE 的 `List of Modified Vanilla Contents.txt` 写明它在原版 `fleet_view` 的 `containerWindowType "fleet_view"` 里加了一个按钮。该文件现已改成 `.bak`（停用）。✅
- 原版自己的顶层名没有冲突：178 个 `.gui` 里共 1193 个顶层控件（`containerWindowType` 914、`positionType` 233、`windowType` 22、`buttonType` 7…），名字全部唯一。✅

多人同步：`checksum_manifest.txt` 只列出 `common/*.txt|*.shader|*.csv`、`events/*.txt`、`map/*.txt|*.shader`。✅
- 也就是说 `interface/**`（`.gui`/`.gfx`）和 `localisation/` 不参与 checksum，各客户端 UI 可以不一致。
- `common/button_effects/*.txt` 与事件参与 checksum，必须一致。

### 1.6 热重载与调试

- 控制台 `reload_gui`（exe 字符串："Reload all GUI files and tries to re-initiate open GUIs"）。✅
  - 实现 `OnExecute_ReloadGUI`（3807925）：枚举所有 `interface/*.gui`，对每个文件调用 `CReloadManager::Reload`。
  - 单文件路径：`CClausewitzReloadManager::ReloadSingleTarget`（7072499）→ `CGui::ReloadFile(CGui::_pGui, path)`（8258105）。
  - 重载时 `CGui` 把标志 `this[0x318]=1`，用 `AddLocked` 替换同名类型。
  - `CReloadableInterface` 让已打开的视图用新类型重建。
- 同系列控制台命令：`reload texture all`、`reload mesh all`、`reloadfx all`、`reload text|localize`、`filewatcher`（切换文件监视器）、`guibounds`、`gui_extra_height <像素>`。✅
  - 在本反编译里 `CGameApplication::InitFileWatchers`（194824）函数体为空。⚠ 文件监视器对 GUI 是否生效没验证。
- 内置调试窗口：`CDebugViewImGui`（5543050）的 "Reload GUI" 按钮（5544231）、"Guibounds" 复选框（5543883）。✅
  - `guibounds` 打开时 `UpdateGuiboundsTooltip`（400962）显示悬停控件信息。
  - 每个 `CGuiType` 在创建时存了所在文件名和行号（`CGui::Create` 8257451–8257459），所以信息很可能含"文件:行"。⚠
- 脚本侧调试：`script_profiler` 控制台命令；`button_effect.<名>.potential|.allowed|.tooltip(allowed_and_effect)|.effect` 是 profiler 的标签（1840572、1840731、1840806）。✅
- 找不到控件：`<窗口>: Could not find buttonType "<名>" in window "<窗口>".`，每次写入都带 `flush`（8200375–8200393）。✅ 与 `01_ui_per_frame.md` U4 一致。

---

## 2. 类层次、布局、运行期创建

### 2.1 类层次（从构造函数反推）

构造函数里调用的基类：✅
```
CGuiObject
 ├─ CButton (+CKeyEvent)           ← CIcon、CButtonStandard、CButtonDrag
 │      └─ CEffectButton           ← 基类没有从构造函数里确认 ⚠（推测继承 CButtonStandard：
 │                                   CEffectButtonType 继承 CButtonType，并复用 InstantiateStandardButton 的参数）
 ├─ CInstantTextBox、CTextBox、CEditBox(+CTextBuffer)、CCheckBox
 ├─ CScrollbar、CExtendedScrollbar、CSpinner(+CGraphicalObject)
 ├─ CSmoothListbox、CStandardlistbox、CDropDownBox(+CGraphicalObject)
 ├─ CGridBoxBase(+CGuiOffset+CGraphicalObject) ← CStandardGridBox
 └─ COverlappingElementsBox(+CGraphicalObject)
CContainerWindow、CFixedWindow  （多重继承，容器自带各类型子控件的名字索引）
CGuiView : CReloadableInterface   （顶层视图基类）
```
类型侧（来自 `.gui` 的模板对象）：`CGuiType` → `CButtonType`、`CIconType`、`CContainerWindowType`、`CWindowType`、`CInstantTextBoxType`、`CEffectButtonType` …

- `CContainerWindow` 内部为每种控件类型各有一个名字索引（`SGuiObjectLookup<T>`，`GetLookupData<T>` 8199223 起），所以 `GetButtonRecursive` / `GetInstantTextBoxRecursive` / `GetIconRecursive` … 都是按"类型 + 名字"查找。✅
- 带 `Assertive` 的版本找不到会写日志，带 `NonAssertive` 的版本静默返回空（8200354 对 8201037）。✅

### 2.2 顶层视图与窗口层

- 每个视图类 `CGuiView(const char* name)`：构造里 `CGui::GetGuiType(name)` 取窗口模板，同时把名字交给 `CReloadableInterface` 用于重载。✅ 7130411–7130426。
- `ConstructWindow` 在某个窗口层创建容器窗口，例如事件窗口：`CInGameIdler::GetWindowLayer(7)` + `CWindowLayer::CreateContainerWindow`。✅ 4007269–4007280。
- 默认 `ShouldUpdate` 等于 `IsShown`，窗口打开时每帧 `Update`，`CInGameIdler::UpdateInternal` 遍历 78 个顶层视图。✅ 来自 `01_ui_per_frame.md`，未重复核对。

### 2.3 布局

- 绝对布局：每个控件相对父容器，由 `orientation`（父的哪个锚点）+ `origo`（自身原点）+ `position` 决定。没有弹性布局、没有约束、没有自动换行。✅
- 唯一的"自动排布"控件：
  - `OverlappingElementsBoxType`：按 `format`/`spacing`/`first_on_top`/`direction` 重叠或排列。
  - `gridBoxType`：按 `slotsize`、`max_slots_*`、`add_horizontal`、`padding` 排格子。
  - `listboxType`/`smoothListboxType`：纵向列表，`spacing`、滚动条。
  - 它们的子项都由 C++ 添加。
- 纵向自适应：`dynamic_extra_height`（原版容器 63 处），额外高度 = `clamp(窗口高度-720, 0, GUI_EXTRA_HEIGHT_MAX)`（194325–194335）。`GUI_EXTRA_HEIGHT_MAX` 在原版是 0（`unchecked_defines/00_interface.txt`），所以默认不生效。✅

### 2.4 缩放与 DPI

- 设计基准分辨率：`GUI_BASELINE_WIDTH=1280`、`GUI_BASELINE_HEIGHT=720`，`MIN_GUI_SCALE=0.5`（`unchecked_defines/00_interface.txt:118-121`）。✅
- 用户缩放：`gui_scale`（`CGraphicsSettings`，SDK 里 token 0x28e，浮点）。`if_scaled_resolution` 把分辨率除以缩放后再比较（8266487–8266497）。✅
- 文本框实例化时把尺寸乘以 GUI 缩放（`CInstantTextBoxType::Instantiate`，8270792）。✅
- 实际代价：AE 在 `interface/*.gui` 里有 381 个 `if_scaled_resolution` 块，954 个 `@变量` 中有 221 个是 `_scaled` 后缀，同一份布局为 1440p 以上再写一遍。✅

### 2.5 运行期创建控件的 API

C++ 侧可用（都按"类型名"，不是任意参数）：✅
- `CGui::CreateContainerWindow(name, nav, parent)`（8255940）。
- `CGui::CreateIcon|CreateButtonStandard|CreateInstantTextBox|CreateEditBox|CreateScrollbar|CreateSpinner|CreateCheckBox|CreateStandardListbox|CreateSmoothListbox|CreateOverlappingElementsBox|CreateStandardGridBox|CreateDropDownBox|…(name)`（8255340–8256161）。
- `CContainerWindow::CreateContainerWindow(typeName, instanceName)`（8204832）：事件窗口创建每个选项容器用的就是它。
- `CContainerWindow::CreateButton|CreateIcon|CreateInstantTextBox|CreateStandardGridBox|CreateOverlappingElementsBox|CreateExtendedScrollbar|CreateDropDownBox`（8204434–8204953）。
- 精灵：`CGuiGraphics::CreateVisible2dObj(spriteName, parent, ...)`（8260665）、`AddSpriteType`（8262142）、`CreateTextSprite`（8263032）。

要创建"新类型的控件"，有两条路：
- 构造 `CContainerWindowType(CGuiGraphics&, CGui&)` 等类型对象并 `Add` 进全局树。
- 或者把 `.gui` 文本经 `CGui::ReloadFile(path)`（8258105：`CTextLexer(path)` + `CReader`）读入。`CTextLexer(CFile*, bool)`（8100414）存在，是否能喂内存缓冲，⚠ 没验证。

脚本层面没有任何"创建控件"的 API。

---

## 3. 数据绑定

### 3.1 总览：没有声明式绑定

- 窗口的数据在 C++ 里"推"给控件：`GetInstantTextBoxRecursive("title")` → `ChangeString(...)`。
  - 例如 `CEventWindow::Setup`（4007287）：先取 "Title"，再 `CInstantTextBox::ChangeString`（4007409–4007411）。
  - `CDiplomaticEventWindow::Setup`（3934771）逐个按名字取控件：`focus_button`、`close`、`tts_button`、`heading`、`action_title`、`action_desc`、`empire_info_bg`、`empire_name`、`empire_government_type`、`empire_personality_type`、`empire_flag`、`empire_ethics_icons`、`portrait`、`portrait_background`、`leader_*`、`event_picture`、`option_list`、`confirm_button`。✅
- C++ 侧还有一层"视图模型"缓存，`CGuiCacheManager`（4328158 起，余额合计、扇区表、殖民地表、舰船数量、路径缓存）。它只被 C++ 使用，.gui/脚本看不到。✅
- `datacontext`（0x381）、`datamodel`（0x3ea）、`visible`（0x306）、`enabled`（0x305）、`onclick`（0x307）这些 token 在 Clausewitz 的 token 表里存在（来自别的 Paradox 游戏），但 Stellaris 的 GUI 读取器里：
  - `0x381` 只出现在两个 `case` 长列表里（8216477、8228206），没有处理分支。
  - `0x3ea` 在 GUI 读取代码里无出现。
  - `0x306`（`visible`）在整个反编译里只有 `CStandaloneServerConfig` 读写（"服务器是否出现在全局浏览器"，8163888、8164134），与控件无关。✅
  - `CFactory::Create` 的基类实现就是 `ReportUnexpected`（8103657）。
  - 结论：不生效。⚠ 属于读代码后的推断，没做加载实验。

### 3.2 脚本 → UI 的三条通道

| 通道 | 内容 | 刷新时机 | 证据 |
|---|---|---|---|
| 事件窗口 | 标题、描述、选项名、`response_text`、事件图、选项的 trigger/effect 图标，用事件 scope 本地化 | 窗口创建时一次（`Setup`）；选项"可用状态"约每秒重算一次 | `CEvent::GetTitle`；`PostEventOptionSelection` 里 `CGameText::SetEventScope`（3936586）；`CEventWindow::Update`（4008127）里 `1.0 <= 时钟差` 才重算每个选项的 `IsAllowedIgnoreExclusive`/`IsAllowedSkipPotential`（4008217–4008255） ✅ |
| `effectButtonType` | `potential`（显隐）、`allow`（可用）、按钮文字、悬停 tooltip | **每帧** | `CEffectButton::PerFrameUpdate`（3967090）；`UpdateAllInstances`（3967199）被 `CGameIdler::Idle`（400540）每帧调用 ✅ |
| 本地化命令 | 任何经 `CGameText` 带 scope 本地化的字符串 | 取决于调用者 | `CGameText::ProcessWithScope`（396399）；`GenerateString`（391523） ✅ |

`CEffectButton::PerFrameUpdate` 的流程（3967090–3967167）：✅
1. `BuildEventScope` 构造一对 scope。
2. `CButtonEffect::IsPotential`：为假则 `Hide`。
3. 为真则 `Show`，再 `IsAllowed`：为假则置为禁用外观。
4. 创建 `CGameText`，`SetEventScope`，对 `GetLocalizedButtonText` 的结果调用 `CTextBase::ProcessString`，把结果设给按钮。

所以 effectButton 是 mod 里唯一"逐帧动态"的文字、图片载体。AE 的做法：1900 个 effectButton 里约 1357 个根本没有 `text`，纯粹当"状态贴图"用；526 个有文字。✅

### 3.3 本地化命令与 scope

- 命令文档：`logs/script_documentation/localizations.log`（游戏生成）。
  - 说明里写着：硬编码命令，`[Scope.my_variable]` 可读变量，`[Scope.my_date_flag]` 读保存的日期，`[[` 转义。
  - 另有无 scope 命令 `GetDate`、`GetMidGameDate`、`GetLateGameDate`、`GetYear`、`LastKilledCountryName`。
  - `$@变量名$` 引用 `common/scripted_variables`。✅
- 可提升的 scope 词：`This/Root/From/Prev`（大小写皆可）；国家有 `Capital/Ruler/Heir/Species/Federation` 等。文档共列出约 40 种 scope 段（Country 64 条属性、Leader 53、Species 31、Planet 17、Fleet 18、Ship 19、System 11 …）。✅
- 按钮效果里的 scope：
  - 主 scope（This/Root）= 当前选中对象（舰船、舰队、星球、星系、环境对象、巨型建筑、星渊），否则是玩家国家。
  - From = 本地观察的国家。
  - 证据：`CEffectButton::BuildEventScope`（3966966–3966998）、`CSelectable::DetermineScope`（1250254）。✅
  - 如果 scope 仍是国家（类型 4），再按"当前打开的原版窗口"覆盖，依次检查联邦、首次接触、考古、星渊、间谍行动、外交、局势日志（`NButtonEffectUtils::DetermineScopeFromInterface`，3965997–3966117）。✅
  - 所以 This 会随玩家当前选择/打开的窗口变化。`common/button_effects/example.txt` 的注释也警告 "possible to confuse the scopes if multiple interfaces are open"。AE 因此几乎只用 `from = { ... }`（始终是玩家国家）。✅

`CGameText::GenerateString`（391523）解析一个 `[…]` token 的顺序：✅
1. 当前指针类型（国家、星球、舰船…约 28 种）的硬编码属性表：名字与索引的数组，逐项 `strncmp`，再调虚函数取值（391874–391937）。
2. 基类 scope（case 0）调用 `PostProcessEventScopeVariables`（2481395）：只有 `GetDate`、`GetMidGameDate`、`GetLateGameDate`。
3. `GetScriptedLoc`（391154）：在 `CScriptableLocalization` 哈希表按名字查（先去掉 "Persistent" 后缀）；命中后用新的 `CGameText` + 同一个 scope 递归 `ProcessString`（392310–392317）。
4. 变量：`ProcessVariables<T>`（392492 起）。
   - 对 23 种对象类型各有一份：CCountry、CPlanet、CShip、CPopGroup、CGalacticObject、CFleet、CLeader、CArmy、CSpecies、CWar、CPopFaction、CFederation、CFirstContact、CMegaStructure、CSector、CAmbientObject、CArchaeologicalSite、CEspionageOperation、CEspionageAsset、CSpyNetwork、CDeposit、CAgreement、CSituation、CAstralRift。
   - 若变量存在，则把定点数格式化成字符串；也支持保存的日期标志（`GetFlagDate` → `CGameDate::GetGameString`）。
5. 都不命中：写日志 ``Unknown property `%s` in text: %s``（392418）。

`scripted_loc`（`common/scripted_loc`）：✅
- `defined_text = { name text = { localization_key trigger weight } default random }`。
- 也可用 `value = value:<script_value>` 把脚本数值直接输出为文字（`000_example.txt` 末尾注释）。
- 这是 mod 把"算出来的值"显示出来的主要手段，因为 `[Scope.变量]` 只能读已经存下来的变量。
- 剖析标签 `scripted_loc.<名>.evaluate` / `.generate_string`（391212、391812）。

### 3.4 UI → 游戏状态

| 来源 | 机制 | 证据 |
|---|---|---|
| `effectButtonType` 点击 | `CEffectButton::ExecuteEffect`（3966606）造 `CExecuteButtonEffectCommand(scope, CButtonEffect*)`，`PostCommandToSession`。命令序列化 `scope`（token 0x2c8d）和效果名（token 0x59）；`Execute` 先用随机数种子初始化再 `CButtonEffect::ExecuteEffect`（2691628–2691645）→ `SafeExecuteEffect`，所以可以是任意脚本 effect；`IsValid` 在各客户端重新检查 `potential` 与 `allow`（2691653–2691690） | ✅ |
| 事件选项点击 | `CDiplomaticEventWindow::PostEventOptionSelection`（3936513）→ `COpenPlayerEvent::PostEventOptionSelection`（3936550）→ 选项的 effect | ✅ |
| 滚动条、勾选框、下拉框、输入框、分页器 | C++ 观察者（`CScrollbarObserver`、`CCheckBoxObserver`…）直接改 C++ 状态，没有进脚本的通道 | ⚠ 未见脚本 token；已确认脚本 token 表里没有 input/prompt 类 token（仅 `prompt_country`） |
| 文本输入 | 重命名走专门的 C++ 对话框 + 专用命令（`CRenameCountryCommand`、`CRenameSpeciesCommand`、`CRenameAllianceCommand`、`CRenameSpeciesEffect`） | ✅ 类存在；脚本不可读输入 |

数值输入的变通：AE 的"手动难度"窗口只有三个按钮 `ag_difficulty_settings_manual_minus` / `_plus` / `_step`，操作变量 `ag_scaled_difficulty_manual_temp`，范围用 `..._temp_min`，步长用 `..._step`。✅（`ag_forced_triggers_button.txt:1789-1814`）

### 3.5 数据类型的流向

| 方向 | 能传的 | 不能传的 |
|---|---|---|
| 游戏 → UI | 字符串（本地化）、国家/星球/舰船等对象的硬编码属性、已存变量（定点数）、保存的日期、`scripted_loc` 与 `value:` 的计算结果、布尔（显隐/可用）、tooltip 文本（来自 `allow` 失败原因或 effect 描述，可用 `custom_tooltip`） | 任意表达式、列表/聚合、颜色、精灵帧/进度值、控件数量、位置 |
| UI → 游戏 | 点击（带 This/From scope）→ 任意脚本 effect；事件选项 → effect | 滑条值、文本、勾选状态、拖拽、悬停事件、窗口开/关通知 |

---

## 4. `custom_gui`、`custom_gui_option`、`button_effects`

### 4.1 `custom_gui` 的绑定

读取位置（token 0x374f `custom_gui`，0x3764 `custom_gui_option`）：✅
- `CEvent::ReadMember`（2432487）：`custom_gui` 存到事件对象，`custom_gui_option` 存到事件的另一个字段（2432839–2432845）。
- `CEventOption::ReadMember`（2461003）：每个选项自己的 `custom_gui`（2461180）。
- `CDistrictType::ReadMember`（2345039）：区划的 `custom_gui`（`common/districts/00_DOCUMENTATION.txt` 说明只用来选择 `planet_district_entry_width_N`，N=1/2/3）。
- `CMegaStructureType` 里 0x374f 只是长 `case` 列表中的一个，无独立处理。

事件窗口的选择在 `CInGameIdler::AddEventWindow`（719581）：✅
```
switch(事件类型字段)
  case 1: LeaderRecruitmentEventWindow（默认名） / custom_gui 非空则用它
  case 2: LeaderConversationEventWindow
  case 3: CrisisConversationEventWindow
  case 4: LeaderStoryEventWindow
  default:
     非 diplomatic 事件 → 固定的 "EventWindow"，custom_gui 被忽略
     diplomatic = yes  → CDiplomaticEventWindow，窗口名 = custom_gui（为空则 "DiplomaticEventWindow"）
```
- 所以普通事件的 `custom_gui` 无效。`custom_gui` 想生效，必须 `diplomatic = yes`，或属于 case 1–4 的专用事件。AE 全部事件都写 `diplomatic = yes`。✅
- case 1–4 对应的事件类型含义，⚠ 由类名推断。

必需的子控件：
- `CDiplomaticEventWindow::Setup` 按名字取 `heading`、`tts_button`、`close`、`focus_button`、`confirm_button`、`option_list` 等（3934845 起）。
- AE 的窗口里专门有 `####### HIDDEN #######` 段，把用不到但必须存在的控件放到 `@ag_invisible_position = 23333`（屏幕外）。✅ 这是 C++ 按名字取控件、缺少就写日志的直接后果。缺失时是否崩溃，⚠ 未确认。

选项条目（`CDiplomaticEventWindow::Setup` 里的 lambda，3937168）：✅
- 模板名优先级：`option.custom_gui`（3937219）→ `event.custom_gui_option`（3937211）→ 默认 `"event_option_entry"`（3937198）。
- 条目里必须有 `option_button`（文字 = 选项名，`CEventOption::GetName(scope)`）。
- 可有 `trigger_icons`、`effect_icons`（`OverlappingElementsBoxType`）：`NEventWindowUtil::AddTriggeredByIcons` / `AddEffectRewardIcons`（3937236–3937239）自动根据选项的 trigger/effect 填图标。
- 条目通过虚函数加入 `option_list`（`listboxType`）。
- 第一个选项的 `option_button` 绑定 RETURN 快捷键。

对话式选项：`is_dialog_only = yes` 要求有 `response_text`，否则写日志 "…tagged as dialog only but has no response text."（3936556–3936564）；有 `response_text` 时，窗口不关闭，把响应文字（带事件 scope 本地化）放进 `alien_message`（`SetAlienMessage`，3936627）。✅

### 4.2 `button_effects` 脚本格式

`common/button_effects/*.txt`：✅
```
my_button_effect = {
    potential = { ... }   # token 0x2d21：假 → 按钮隐藏
    allow     = { ... }   # token 0x2d50：假 → 按钮灰；失败原因进 tooltip
    effect    = { ... }   # token 0x59：  点击执行
}
```
- 读取：`CButtonEffect::ReadMember`（1840832），数据库 `CButtonEffectDatabase`（1840411，目录 `"common/button_effects"`）。
- 内部：两个 `CRootTrigger`（potential、allow）+ 一个 `CRootEffect`。
- tooltip：`allow` 失败 → 失败原因树；成功 → `CEffect::GetCompleteDesc`（1840745–1840750）。mod 常在 effect 里写 `custom_tooltip = "..."` 当 tooltip（AE 的 `ag_set_difficulty_group`）。✅
- 原版自己只有两个例子：`behemoth_effects.txt` 与 `example.txt`；`.gui` 里只有 `fleet_view.gui` 用了两处 `effectbuttonType`（707、1784），用于 Behemoth 的 "grow" 按钮。✅ 说明这是一个附加得很晚、用得很少的机制。
- 模板化：VoE 用 `common/inline_scripts/UI/*.txt`（含 `$TARGET$` 参数）批量生成 button_effect：30 次 `inline_script` 调用生成 83 个按钮效果定义。✅

### 4.3 覆盖原版窗口

- 机制：名字相同、后加载者整体替换（§1.5）。没有 merge，没有 `gui_overrides`，没有"扩展点"。
- 想往 `fleet_view` 加按钮，只能复制整个 `fleet_view.gui` 再改。AE 的副本是 1936 行，4.5 原版 `fleet_view.gui` 是 2354 行，差异包含新的舰队自动化菜单布局（`@auto_win_*` 等）。✅
  - AE 因此把副本改名为 `.bak`。
- 冲突后果：
  - 同名顶层控件被无声替换（无日志）。
  - 若被替换的窗口是 C++ 视图的窗口，缺少 C++ 要找的子控件会触发"Could not find …"日志，每帧一次（见 §1.6）。
  - 原版升级后副本变旧，新功能丢失，但不会报错。

### 4.4 原版里不用覆盖就能挂接的位置

- 没有通用的"面板扩展点"。⚠ 没发现 `interface` 合并规则。
- 数据驱动的原生入口（脚本定义条目，原生窗口自己渲染）：
  - `common/decisions`：决议列表，含图标、资源消耗、`potential/allow/effect`。✅ 文件存在
  - `common/scripted_actions`：舰队右键菜单动作（`icon`、`tooltip`、`context_menu_name`）。✅
  - `common/artifact_actions`、`common/astral_actions`。✅ 目录存在
  - `common/alerts.txt`：只定义图标、类别、优先级；警报触发条件在 C++（`CAlertManager::Update` 按 66 类轮转，见 `01_ui_per_frame.md`）。⚠ 因此只加 key 不会产生警报。
  - 区划 `custom_gui`：只选宽度。
  - 脚本效果：`create_message`（消息/toast，类型来自 `common/message_types`）、`force_show_diplomacy`、`force_show_espionage`、`open_shroud_tab`、`play_sound`、`advisor_active`、`set_tutorial_level`（效果类存在）。✅
  - `common/edicts|policies|buildings|technology|traditions` 等：由原生窗口按定义渲染。⚠

---

## 5. 限制与痛点

下表"证据"列给出出处；"变通"列是真实 mod 的做法。

| # | 限制 | 证据 | 变通 / 代价 |
|---|---|---|---|
| 1 | 无法不借助事件就打开自定义窗口 | 窗口只能由 `AddEventWindow` 按事件创建（719581）；`custom_gui` 仅 `diplomatic` 事件等生效 | AE 所有窗口都挂在 `diplomatic = yes` 事件上；窗口开关靠全局 flag（`ag_seetings_opened`）防重复打开，关窗靠 `after = { clear_variable… }` ✅ |
| 2 | 事件窗口是"外壳"，一堆必需控件，且位置/行为固定 | `CDiplomaticEventWindow::Setup`（3934771）按名字取控件 | 把不用的控件挪到 `x/y = 23333`（AE ag_mod_settings_ui.gui） ✅ |
| 3 | 只有 effectButton 能由脚本控制显隐/文字/可用性 | `PerFrameUpdate`（3967090）；其他控件无此路径 | 用 effectButton 当"状态灯"：AE 约 1900 个，1357 个无文字纯作贴图 ✅ |
| 4 | 进度条/滑条/帧/颜色不可由脚本驱动 | `CProgressbarSprite::SetState`（8293997）由 C++ 设；`frame` 是 `.gui` 静态属性 | AE 难度滑条一行 30 个 effectButton：18 个 `*_button` 各配一个 `*_bar` 效果，用 `check_variable` 区间做 `potential`，再加 ± 按钮、8 个刻度标记 ✅ |
| 5 | 无动态列表 | 只有事件 `option_list` 由 C++ 逐项添加；其余列表/网格由 C++ 填 | 固定槽位 + `event_target`：AE 国家选择器 12 槽，每槽一个 option + 一个选中遮罩 effectButton（49 行 effect 文件）；`ag_auto_starbase.2` 99 个 option；`ag_ship_special.1` 60 个 ✅ |
| 6 | 无文本输入、无拖拽、无滚轮取值 | §3.4；token 表没有脚本可读的输入 | ± 与"步长"按钮改变量（`ag_difficulty_settings_manual_*`） ✅ |
| 7 | 事件窗口文字只在创建时生成，不随状态刷新；选项可用状态约每秒重算 | `CEventWindow::Setup`、`Update`（4008217） | 把会变的信息放在 effectButton 的 `text`/tooltip 里（每帧重算） ✅ |
| 8 | 每帧成本 | `CGameIdler::Idle` 每帧 `UpdateAllInstances`（400540）；每个可见 effectButton 都要建 scope、求 `potential`/`allow`、做文本本地化 | AE 一个设置窗口行含 30 个按钮，12 个选项就是 360 个按钮每帧求值（⚠ 数量由 gui 与事件结构推算）。窗口关闭时不求值（`CheckIsParentsRendered`）。`01_ui_per_frame.md` 已有同类提示 |
| 9 | 只能读"已存的变量"或 `scripted_loc/value:` 的结果 | §3.3 的 `GenerateString` 顺序；`ProcessVariables<T>` 仅 23 种对象 | 提前在 effect 里把需要显示的值算好存入变量，再在本地化里 `[This.变量]`；AE 的 "Page [This.…marked_page] / […total]" ✅ |
| 10 | UI 状态必须是游戏状态 | effectButton 点击 = 命令（3966635）；AE 的 `ag_settings_page` 是国家变量 | 状态进存档、进多人命令流、进 checksum；翻页要等命令往返（⚠ 多人延迟由命令机制推断）；窗口关闭时要手动清理变量 ✅ |
| 11 | 多人：UI 文件不被校验，脚本被校验 | `checksum_manifest.txt` | 玩家可以有不同的 `.gui`；按钮效果与事件必须一致，否则 OOS ✅ |
| 12 | `potential/allow` 在本地逐帧求值、随选择变化 | `BuildEventScope`、`DetermineScopeFromInterface` | `This` 不可靠；统一用 `from`（玩家国家）；在 `allow` 里检查 `is_scope_type` ✅ |
| 13 | 热重载 | `reload_gui` 只重载 `.gui`（7072499、3807925）；`button_effects`/事件属于 `common/` 数据库 | 改脚本需要重启游戏（⚠ 没找到 `common/` 的重载命令；`filewatcher` 的实际作用没验证） |
| 14 | 调试 | 错误只写日志；`guibounds`；script profiler 标签 | 没有控件树检查器；没有 schema 校验；未知属性只写 `Unexpected token` ✅ |
| 15 | 无复用机制 | 无 include/继承（§1.1） | AE 40 个 `.gui` / 38149 行，698 个容器，1168 个 button_effect（17031 行，22 个文件），196 次 `inline_script`；VoE 用 `inline_script` 模板生成 ✅ |
| 16 | 分辨率适配手工翻倍 | `if_scaled_resolution`（8266487） | AE 381 个块、221 个 `_scaled` 变量 ✅ |
| 17 | 覆盖原版是整窗替换，且会过期 | §4.3 | AE 的 `fleet_view` 副本 1936 行 vs 现行 2354 行，已停用 ✅ |
| 18 | `effectButtonType` 位置受限 | `ReadGameSpecificGuiTypeMember`（4254601）只接受 `containerWindowType` 的直接子项 | ⚠ 推论，没做加载实验 |
| 19 | 静态 tooltip 无 scope | `pdx_tooltip` 是本地化 key（原版 `CLOSE_TITLE`、`FOCUS_ON_SYSTEM` 等） | 要动态 tooltip 只能用 effectButton ⚠ |
| 20 | 全局命名空间 | 顶层名进全局树（§1.5） | 不同 mod 同名窗口无声互相覆盖；AE 用 `ag_` 前缀避免 |

### 5.1 两个真实 mod 的规模对比

| 指标 | AE | VoE |
|---|---|---|
| `.gui` 文件 / 行数 | 40 / 38149 | 2 / 2166 |
| `containerWindowType` | 698 | 28 |
| `effectButtonType` | ≈1900（正则；解析器数得 1883） | 224 |
| `buttonType` / `iconType` / `instantTextBoxType` | 686 / 1022 / 838 | 39 / 27 / 46 |
| `button_effects` 文件 / 行数 / 定义数 | 22 / 17031 / 1168 | 2 / 773 / 83 |
| `inline_script` 调用（按钮效果里） | 196 | 30 |
| 事件总数 / `hide_window` 事件 | 3275 / 844 | 622 / 186 |
| 有 `custom_gui` 的事件 / 有 `custom_gui` 的选项 | 1923 / 392（共 4102 个选项） | 4 / 23（共 1018 个选项） |
| 单事件最多选项 | 99（`ag_auto_starbase.2`） | 8 |
| `.gfx` / 精灵数 | 18 / 3250 | 17 / 1173 |
| 最大单窗口 | `ag_auto_starbase_settings_ui.gui` 474 个 effectButton / 3869 行（只有 132 个按钮效果定义，说明按钮复用效果） | — |

说明：AE 有 1923 个事件带 `custom_gui`，其中大量是叙事事件的"换皮窗口"，不都是设置界面。

---

## 6. 原生插件（DLL）的扩展点评估（⚠ 全部是评估）

仓库现状：SDK 里已有 `execute_button_effect` 命令规格（`kToken=0x36C4`，`scope` 在 `+0x20`，`effect` 指针在 `+0x190`），以及 `PdxLocalize`、`CEventWindow_Setup`、`CEventWindow_AddOptionButton`、`CEventWindow_PostEventOptionSelection` 等。✅ 见 `stellaris_sdk.hpp`。下面的函数名是 Linux 符号，Windows RVA 需要用 `tools/sdk_dumper/functions.py` 的指纹法另外定位。

| # | 想做的事 | 钩子 / 函数 | 做法 | 难度 |
|---|---|---|---|---|
| 1 | 不借助 UI，直接触发按钮效果 | `CExecuteButtonEffectCommand`（2691558）；`CButtonEffectDatabase`（1840411） | 用现有 `CommandBuilder` 以 `execute_button_effect` spec 构造命令：填 `scope`（This/From）和 `effect`（数据库里的按钮效果对象），`IsValid` 后 `Post`。可顺带枚举 `CButtonEffectDatabase` 暴露给 MCP | 低（SDK 已有） |
| 2 | 运行期加载/重载 `.gui` | `CGui::ReloadFile(path)`（8258105）；`CGui::ReloadFolder`（8258141）；`OnExecute_ReloadGUI`（3807925） | 把生成的 `.gui` 写到临时文件，在 TaskQueue（主线程）里调用 `ReloadFile`，`CGui::_pGui` 是单例。新类型立刻能被 `GetGuiType(name)` 找到。可用来做 MCP 的"热更新 UI" | 中（要定位 RVA；线程必须在主线程） |
| 3 | 直接开窗口（不经事件） | `CGui::CreateContainerWindow`（8255940）、`CWindowLayer::CreateContainerWindow`（4007278 的调用方式）、`CGuiView`（7130411） | 构造一个最小的 `CGuiView` 子类对象需要虚表与大小，难；更现实的是借 `CInGameIdler::AddEventWindow`（719581）开一个 `diplomatic` 事件窗口，再配合 2 注入窗口模板 | 高（自建视图）/ 中（借事件窗口） |
| 4 | 给已有窗口推数据 | `CContainerWindow::GetInstantTextBoxRecursiveNonAssertive`（8201077）、`CInstantTextBox::ChangeString(CPdxStringView)`（8268704） | 挂一个每帧回调（例如 `CGuiView::Update` 或 `CGameIdler::Idle` 之后），按名字写文字。要用 `NonAssertive` 变体避免 §1.6 的日志风暴 | 中 |
| 5 | 读 UI 状态 | `CInstantTextBox::GetString`（8268809）；容器里的子控件数组 | 供 MCP 读取"当前窗口上显示了什么"。需要先定位容器内部的名字索引布局，且各版本偏移不同 | 中 |
| 6 | 新增本地化命令（如 `[Root.McpFoo]`） | `GetScriptedLoc`（391154）或 `CGameText::GenerateString`（391523）；exe 中有定位用字符串："Failed to return a result for scripted localization"（0x2389008）、"Unknown property `%s` in text"（0x2388e00） | 在"Unknown property"之前插入插件表：命中名字则返回插件字符串。注意返回的是 `CString`，且 `GetScriptedLoc` 签名是 `(SStringToken const&, CEventScope, bool&, bool&)` 自由函数 | 中 |
| 6b | 零钩子替代 | `ProcessVariables<CCountry>`（392492）读国家变量 | 把值写进国家变量（单机可行），本地化里写 `[Root.my_var]`；多人会 OOS，因为变量属于游戏状态 | 低（仅单机） |
| 7 | 新按钮效果 | `CButtonEffect` 构造（1840468）+ `CButtonEffectDatabase` | 让 mod 带一个 `common/button_effects/*.txt` 最简单；运行期往数据库塞对象需要自己构造 `CRootTrigger`/`CRootEffect`，等于重写解析器 | 中（带文件）/ 高（运行期） |
| 8 | 新脚本 effect/trigger 原语 | `CEffectEntry<T>` 静态注册（如 `CLockBypassEffect`） | 要注册新 token、序列化、`IsValid`、`Execute`，并让所有客户端一致 | 高 |
| 9 | 新控件类型 | `CGameGraphics::ReadGameSpecificGuiTypeMember`（4254601），`CGuiType::ReadMember` 末尾的 `_pReadGameSpecificFunc`（8266550） | 可以把函数指针换成自己的，在 `containerWindowType` 里识别新关键字；但新控件需要完整的 `CGuiObject` 子类（虚表、渲染、输入） | 高 |
| 10 | 完全独立的 UI（含文本输入） | exe 内置 Dear ImGui：`CImGuiController`（5538194 起）、`CGameIdler::RenderImGui`（401186）；控制台里有一组 ImGui 子命令（`SubCommands_ImGui_ShowHide`，3834572；exe 字符串 `debug_view`、`imnodes_demo`、`event_graph`；具体命令名与语法没核对 ⚠） | 挂 `CImGuiController::UpdateViews`（5538396）在其中调用 ImGui 函数画自己的窗口；或在桥已有的 `Present` 钩子里初始化自带的 ImGui。能拿到输入框、滑条、表格、图表 | 中（复用引擎 ImGui 需定位 ImGui 函数 RVA；自带 ImGui 需处理输入捕获、DPI） |
| 11 | 降低 effectButton 每帧成本 | `CEffectButton::UpdateAllInstances`（3967199） | 钩住并改为隔 N 帧求值、或只在父窗口真正可见时求值（它已检查 `CheckIsParentsRendered`） | 中 |
| 12 | 开发回路自动化 | 控制台命令 `reload_gui`；日志 `logs/error.log` | MCP 工具：写 `.gui` → 触发 2 或 `reload_gui` → 读取日志新增的 `Unexpected token`/`Could not find` | 低–中 |
| 13 | 修改输入/点击 | `CMessage::LeftClick`、`CAlertManager::Click` 已在 SDK | 点击模拟比构造命令脆弱，优先用 1 | — |

补充：第 10 行的 ImGui 路线，同一个 worktree 里另有一份独立调研 `docs/gui_imgui_feasibility.md`（ImGui 1.85 + DX11 后端、4.5.2 的 RVA、在运行中的游戏里的实测）。该文件不是我写的，本文没有核对它的内容；它引用本文作为"原生 GUI 系统"的另一路调研。

建议的优先级：先做 1（事实上 MCP 已具备命令投递能力）→ 12 → 4/5 → 2 → 10。9 与 8 不值得。

---

## 7. 没找到 / 不确定的事项（以及我找过的地方）

1. Windows 版函数 RVA：没有定位。只确认了 exe 里存在相应字符串（"Failed to return a result for scripted localization"、"Unknown property `%s` in text"、"button_effect."、"Found duplicate smoothListBoxType"、"…dialog only but has no response text"、"event_option_entry"、"Dear ImGui Demo"、"reload_gui" 描述文本）。Linux 构建与 Windows 构建的函数划分可能不同。
2. VFS 排序是否区分大小写：`SLessThan` 没读。AE 的 `!!!_`、`zzy_` 命名暗示它是按字节/字母序，但这是推断。
3. `@变量` 的作用域（文件内还是全局）：没读 `CReader::ParseAdvancedStatement`。AE 没有跨文件引用，30 个同名变量在多个文件里重复定义，更像文件作用域。
4. 必需控件缺失时的后果（只写日志还是断言/崩溃）：`CContainerWindow::Get*RecursiveNonAssertive` 与 Assertive 版本的区别已读（日志对静默），但 `CDiplomaticEventWindow::Setup` 具体用了哪一种是虚函数调用，没逐一区分。
5. `effectButtonType` 是否只能出现在 `containerWindowType` 直接子项：由 `ReadGameSpecificGuiTypeMember` 推断，没做加载实验。
6. `datacontext` 等 token 是否完全无效：只确认了 GUI 读取代码里没有处理分支。
7. `filewatcher` 对 `.gui` 的实际作用：`InitFileWatchers` 函数体为空（194824），Windows 上可能不同。
8. 多人下"翻页类命令"的延迟与暂停时的处理：由命令机制推断，没实测。
9. 警报（alerts）的触发条件是否全部硬编码：来自 `01_ui_per_frame.md` 的结论，没有再读 `CAlertManager`。
10. 原版面板里是否存在其他脚本可挂接点（例如 `situation_log`、`event_chains` 的展示）：只列了存在的数据目录，没有逐个核对渲染代码。
11. 我没有运行游戏做实验（本任务只读）。凡写"推断"的地方，建议用 `reload_gui` + 一个最小 mod 验证。

---

## 附录 A：关键函数索引（`stellaris_4.5_source.cpp` 行号）

| 主题 | 函数 | 行 |
|---|---|---|
| 加载 | `CGameApplication::InitGUI` | 194016 |
| 加载 | `CGui::ReadMember` / `CGui::Create` / `CGui::InitGUI` | 8257098 / 8257217 / 8257142 |
| 加载 | `CReader::Read<CGuiType*>` | 8258785 |
| 加载 | `CTernary<CGuiType*>::Add` / `AddLocked` | 8217568 / 8218106 |
| 加载 | `VFSEnumerateFiles`（排序）/ `VFSGetEnumeratedFiles` | 8009801 / 8006424 |
| 重载 | `CGui::ReloadFile` / `ReloadFolder` | 8258105 / 8258141 |
| 重载 | `OnExecute_ReloadGUI` / `CClausewitzReloadManager::ReloadSingleTarget` | 3807925 / 7072499 |
| 属性 | `CGuiType::ReadMember` | 8266446 |
| 属性 | `CWindowType::ReadMember` / `CContainerWindowType::ReadMember` / `CButtonType::ReadMember` / `CIconType::ReadMember` | 8343727 / 8215492 / 8190235 / 8169176 |
| 属性 | `CGameGraphics::ReadGameSpecificGuiTypeMember` | 4254601 |
| 视图 | `CGuiView::CGuiView` / `CEventWindow::ConstructWindow` | 7130411 / 4007269 |
| 查找 | `CContainerWindow::GetButtonRecursive` / `CreateContainerWindow` | 8200354 / 8204832 |
| 创建 | `CGui::CreateContainerWindow` | 8255940 |
| 精灵 | `CGuiGraphics::AddSpriteType` / `GetSpriteType` | 8262142 / 8262263 |
| 按钮效果 | `CButtonEffect::IsPotential` / `IsAllowed` / `GetTooltip` / `ExecuteEffect` / `ReadMember` | 1840545 / 1840615 / 1840685 / 1840763 / 1840832 |
| 按钮效果 | `CButtonEffectDatabase::CButtonEffectDatabase` | 1840411 |
| 按钮效果 | `CEffectButton::ExecuteEffect` / `BuildEventScope` / `GetToolTip` / `PerFrameUpdate` / `UpdateAllInstances` | 3966606 / 3966966 / 3967005 / 3967090 / 3967199 |
| 按钮效果 | `NButtonEffectUtils::DetermineScopeFromInterface` / `CSelectable::DetermineScope` | 3965997 / 1250254 |
| 按钮效果 | `CExecuteButtonEffectCommand::Execute` / `IsValid` / `WriteCommandMembers` | 2691628 / 2691653 / 2691711 |
| 帧循环 | `CGameIdler::Idle`（调用 `UpdateAllInstances` 于 400540） | 400193 |
| 事件 | `CInGameIdler::AddEventWindow` | 719581 |
| 事件 | `CEvent::ReadMember` / `CEventOption::ReadMember` / `CDistrictType::ReadMember` | 2432487 / 2461003 / 2345039 |
| 事件 | `CDiplomaticEventWindow::Setup` / 选项 lambda / `PostEventOptionSelection` | 3934771 / 3937168 / 3936513 |
| 事件 | `CEventWindow::Setup` / `Update` | 4007287 / 4008127 |
| 本地化 | `CGameText::GenerateString` / `GetTarget` / `ProcessWithScope` | 391523 / 392438 / 396399 |
| 本地化 | `GetScriptedLoc` / `ProcessVariables<CCountry>` / `PostProcessEventScopeVariables` | 391154 / 392492 / 2481395 |
| 缓存 | `CGuiCacheManager` | 4328158 起 |
| 调试 | `CDebugViewImGui::UpdateCategoryQuickAccess` / `CImGuiController::UpdateViews` / `CGameIdler::RenderImGui` | 5543734 / 5538396 / 401186 |
| 调试 | `UpdateGuiboundsTooltip`（`CGameIdler`） | 400962 附近 |

## 附录 B：引用的游戏/mod 文件

- 游戏：`interface/*.gui|*.gfx`（扫描 178 个 `.gui`、131 个 `.gfx`）、`interface/fleet_view.gui`（707、1784 行 `effectbuttonType`）、`interface/diplomacy_formless_event_view.gui`、`common/button_effects/example.txt`、`common/button_effects/behemoth_effects.txt`、`common/scripted_loc/000_example.txt`、`common/districts/00_DOCUMENTATION.txt`、`common/alerts.txt`、`common/decisions/00_resource_decisions.txt`、`common/scripted_actions/00_paragons.txt`、`unchecked_defines/00_interface.txt`、`checksum_manifest.txt`。
- 日志：`Documents/Paradox Interactive/Stellaris/logs/script_documentation/localizations.log`（运行游戏时生成；本任务读取时是已生成的版本）。
- AE：`interface/ag_mod_settings_ui.gui`、`interface/ag_sliders_ui.gui`、`interface/zzy_overwrite_fleet_view.gui.bak`、`common/button_effects/ag_settings_buttons.txt`、`ag_country_selector_buttons.txt`、`ag_forced_triggers_button.txt`、`events/ag_settings_events.txt`、`events/ag_country_selector_events.txt`、`List of Modified Vanilla Contents.txt`。
- VoE：`interface/KZ_VOY_settings_ui.gui`、`common/button_effects/KZ_VOY_mod_setting_effect.txt`、`common/inline_scripts/UI/*.txt`。
- 仓库：`tools/sdk_dumper/linux_tokens.json`（token 号→名）、`stellaris_bridge/include/sdk/stellaris_sdk.hpp`、`source/better_stellaris/01_ui_per_frame.md`、`source/better_stellaris/research/mods_ancient_empire.md`。
