# Stellaris MCP bridge (launcher plugin)

Lets AI agents read and play **Stellaris 4.5.2** (Windows x64) over MCP. `stellaris_bridge.dll` runs inside the game,
reads engine state and posts the game's own commands; the MCP server in `mcp-server\` talks to it over the named pipe
`\\.\pipe\stellaris_mcp_bridge`.

Only for the `stellaris.exe` build listed in `stl-plugin.json` (`game.exe_timestamps`); the launcher does not load it
into any other build.

## Install

The plugin is loaded by the **Stellaris launcher** (`D:\stellaris-Launcher`, `stl`) and nothing else.

1. Install it with the launcher's Plugins page (*Install plugin*), or `stl plugin install <folder>`, or unpack this
   zip into `Documents\Paradox Interactive\Stellaris\plugins\stellaris-mcp\`.
2. Enable it in your playset (`stl plugin enable stellaris-mcp`).
3. Start the game with the launcher: `stl launch` or the Play button. Started from Steam or the Paradox Launcher, the
   game runs without plugins.
4. Install the MCP server's dependencies once (Node.js 20+): run `mcp-server\install.cmd` (it runs
   `npm ci --omit=dev` from the lockfile). They are not shipped in the plugin; run it again after updating the plugin.
5. Register the MCP server (stdio) with your MCP client:
   `node "<Documents>\Paradox Interactive\Stellaris\plugins\stellaris-mcp\mcp-server\dist\index.js"`

## Settings and log

- `config\stellaris_mcp.ini` (made from `defaults\` on install): `[log] enabled` and `level` (`info`, or `debug` for
  every pipe request with its duration). Edit it from the launcher's Plugins page (gear button); the bridge picks up
  changes within a couple of seconds, no restart needed.
- The log is `logs\stellaris_mcp.log` in the plugin folder.

---

## 安装（中文）

插件只由 **Stellaris 启动器**加载。

1. 用启动器插件页的「安装插件」、`stl plugin install <目录>`，或把 zip 解压到
   `文档\Paradox Interactive\Stellaris\plugins\stellaris-mcp\`。
2. 在游戏配置里启用：`stl plugin enable stellaris-mcp`。
3. 用启动器启动游戏：`stl launch` 或「开始游戏」按钮。从 Steam 或 Paradox 启动器启动时不会加载插件。
4. 安装一次 MCP 服务端的依赖（需要 Node.js 20+）：运行 `mcp-server\install.cmd`（按锁文件执行
   `npm ci --omit=dev`）。插件里不附带依赖，更新插件后需要再运行一次。
5. 在 MCP 客户端里注册服务端（stdio）：`node "<插件目录>\mcp-server\dist\index.js"`。

设置在 `config\stellaris_mcp.ini`（日志开关 `enabled`、级别 `level`：`info` 或 `debug`），可在启动器插件页编辑，
保存后几秒内生效，无需重启游戏。日志在插件目录的 `logs\stellaris_mcp.log`。
