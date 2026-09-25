# Stellaris MCP Bridge 协作与开发规范

## 1. 游戏启动与进程管理准则 (Process Management)
- **直接启动可执行文件**：启动游戏时必须直接执行：
  `"E:\Program Files (x86)\Steam\steamapps\common\Stellaris\stellaris.exe" -dx11`
  工作目录必须设置为其所在目录 `"E:\Program Files (x86)\Steam\steamapps\common\Stellaris"`。必须附加 `-dx11` 参数以启用 DirectX 11 渲染后端（避免 DX9 在多显示器/新显卡下报错退出）。
- **严禁通过启动器拉起**：严禁调用 `steam://run/281990`，严禁启动 `dowser.exe` 或拉起 Electron 版本的 Paradox Launcher。
  - *原因*：启动器需要人工点击“开始游戏”，会导致后台自动化注入脚本等待超时与死锁。
- **自动化拉起推荐**：直接运行项目中现成的辅助脚本 `python scripts/launch_stellaris.py`。

## 2. 逆向工程第一性准则 (Reverse Engineering Core Principles)
- **严禁跨线程远程调用 UI/逻辑函数**：
  - 严禁通过 `CreateRemoteThread` 或桥接后台线程强行调用游戏内部的 UI 逻辑函数（例如 `0x14d8d00` 等）。
  - *原因*：此类函数强依赖主线程 UI 消息泵、局部/虚表上下文（如 `dummy_ctx`）和控件链表锁，跨线程调用会导致线程死锁（`WAIT_TIMEOUT 0x102`）或游戏静默崩溃。
- **坚守数据结构直读遍历（Data Direct Access）**：
  - 所有游戏状态（如 Outliner、行星、星域、舰队、科技、法令等），必须通过全局 Manager 数组/链表安全解引用读取数据，保持零副作用、零死锁风险、微秒级响应。

## 3. 架构设计规范：渐进式披露 (Progressive Disclosure)
- **Layer 1 (宏观层 / 索引层)**：
  - 仅返回系统或分组的宏观摘要、名称、ID 及轻量指示标记（如 `has_construction: bool`、`status_alerts_count: int`）。
  - 严禁在 L1 返回大量嵌套细节，严防上下文窗口膨胀。
- **Layer 2 (微观层 / 详情展开层)**：
  - 针对指定实体（如特定星域、特定行星、特定舰队）展开完整结构化数据（如详细建造队列进度条、百分比、剩余天数、警报类型枚举与解释）。

## 4. C++ 源码逆向与数据库对照实战守则 (C++ Source Reverse Engineering Methodology)
- **超大源码（300MB+）流式定位**：
  - 严禁尝试一次性加载或通过常规文本/模型工具整读超大源码；
  - 必须使用 Python 流式扫描（或 `mmap`）根据 RTTI 类名、虚表方法名或模板实例化特征（如 `TPdxRef<C...>::_pDatabase`、`C...Command::IsValid`）精确定位行号范围，再做局部切片研读。
- **全局数据库 (`_pDatabase`) 严格类型核对**：
  - 严禁凭直觉或相近 ID 混淆数据库类型（例如：`0x3113148` 是星系库 `CSolarSystem`，而 `0x3113128` 才是行星库 `CPlanet`）；
  - 必须在源码中确认 `TPdxRef<T>::_pDatabase` 的真实模板类型 `T`，防止将星系主恒星的船坞队列误当作地表建造队列。
- **规避“零值陷阱” (The Zero-Value Trap)**：
  - 引擎中 `0` 是合法有效 ID（例如地球的殖民地 ID 为 0，地表建造队列 ID 为 0，玩家国家 ID 为 0）；
  - 引擎内部的空句柄/无效 ID 固定为 `0xFFFFFFFF` (`UINT32_MAX`)。
  - 严禁在任何地方将 `id == 0` 当作无效拦截，必须使用 `id != 0xFFFFFFFF`。
- **指令与执行双向闭环推导**：
  - 派发 Command 之前，必须在源码中逆向该指令的 `IsValid` 与 `ExecuteLocal` 实现，理清栈帧内存排布（vtable, Action Object, Country ID, Queue ID）；
  - 必须确保指令派发的目标 Queue ID 与游戏 UI 界面绑定的展示 Queue ID 严格一致。
- **定点数归一化**：
  - 引擎底层进度与时间多采用定点数（如 360 天存储为 `36000000`，放大 100,000 倍）。
  - 向上层 API 暴露前必须进行归一化除法处理，保证对外语义真实直观。

