# gui_probe

`docs/gui_imgui_feasibility.md` 的探针：可丢弃的验证代码，**不是产品代码**。

| 路径 | 内容 |
|---|---|
| `poc.cpp`、`CMakeLists.txt`、`live/` | 第一个探针（把引擎自带的 ImGui 启动并画一个面板）和它的实时测试脚本。留作记录；RVA 写死，只适用于 4.5.2 |
| `showcase/screenshots/`、`api_proto/screenshots/` | 研究文档引用的截图 |

## 已迁出的代码

展示 DLL 和公共接口的原型已经整理成独立仓库：

| 原来在这里 | 现在在 |
|---|---|
| `showcase/showcase.cpp`（宿主、指挥甲板）、`showcase/gen_ui_glyphs.py` | **`Yidhar/stellaris-guiexpand`**：`src/`、`tools/gen_ui_glyphs.py` |
| `api_proto/stellaris_gui_*.h*`（公共接口） | `Yidhar/stellaris-guiexpand`：`include/stellaris_guiexpand/` |
| `api_proto/consumer_a.cpp`、`consumer_b.c` | `Yidhar/stellaris-guiexpand`：`examples/` |
| `showcase/mod/zz_gui_showcase/` | **`Yidhar/stellaris-guiexpand-test-mod`**（effect 和 loc 键改名为 `guiexpand_test_*`） |
| `showcase/live/showcase_test.py` | `Yidhar/stellaris-guiexpand`：`tools/live/guiexpand_test.py` |

要看迁出前的原型：`git show 6b5aa58:docs/gui_probe/showcase/showcase.cpp`（该提交是迁出前的最后状态）。
