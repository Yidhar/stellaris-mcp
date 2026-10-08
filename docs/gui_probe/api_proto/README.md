# 插件接口原型

`docs/gui_plugin_api_investigation.md` 的验证代码：宿主的 C 接口、发现用的头文件，以及两个消费方插件。宿主一侧在 `../showcase/showcase.cpp`（`StlGui_GetApi`、面板注册表、故障隔离、声明面板）。**不是产品代码**。

![三种来源的面板](screenshots/three_sources.png)

左：消费方 A（自带 ImGui、`/MT`）；中：消费方 B（纯 C，没有 ImGui）；右：mod 里的一个文本文件声明的面板（`interface/stl_gui/zz_overview.txt`）。

| 文件 | 内容 |
|---|---|
| `stellaris_gui_api.h` | 公共 C 接口：面板、回调上下文、游戏快照、动作、绘制函数表。所有结构体以 `size` 开头，只追加 |
| `stellaris_gui_client.h` | 在进程里找宿主并取函数表（`GetModuleHandle` + `GetProcAddress`） |
| `stellaris_gui_imgui.hpp` | 自带 ImGui 的插件用：绑定共享的上下文与分配器，并核对 ImGui 的版本和类型大小，不一致就拒绝绘制 |
| `consumer_a.cpp` | 自带 ImGui 的消费方。`consumer_a.cmd` 里写 `fault`（回调里空指针访问）或 `leak`（留下没配对的 ImGui 栈）；事件 `Local\gui_consumer_<tag>_unload_<pid>`（注销后卸载）、`..._rude_<pid>`（不注销就卸载） |
| `consumer_b.c` | 纯 C 的消费方，只用 `StlGuiUi` |
| `CMakeLists.txt` | 构建 `consumer_a`、`consumer_b`，以及 `consumer_abad`（`ImDrawIdx` 为 32 位，用来验证布局核对） |

## 构建与运行

```
cmake -S docs/gui_probe/api_proto -B build_proto -G "Visual Studio 17 2022" -A x64 -DIMGUI_DIR=<ImGui v1.85 的路径>
cmake --build build_proto --config Release
```

宿主用 `../showcase` 构建（见它的 README）。运行时**消费方可以先于宿主注入**（它们会轮询）：游戏里先 `stl inject` 消费方的 DLL，再注入宿主；宿主启动 ImGui 后面板出现。声明面板需要安装测试 mod（`../showcase/live/showcase_test.py install-mod`）。宿主的 `gui_showcase.cmd` 支持 `panel list`、`panel <id> 0/1`、`scan`（重新扫描 mod 的声明文件）。

DLL 的日志在各自所在的目录（`consumer_*.log`、`gui_showcase.log`）。
