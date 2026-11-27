<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# 共享 SDK Runtime 的打包游戏回归测试

本端到端回归测试用于在 Stage 后的 Win64 x64 模块化 Development 游戏中，同时挂载两个完全独立制作并打包的 Mod（`TestMod` 与 `SharedCaller`），验证两者能够通过各自隔离的调用方（Caller）上下文安全共用同一个 `UMFDevSDKRuntime` DLL，并且在运行期动态卸载其中一个 Mod 后，其调用权限会被立即吊销，而另一个 Mod 的现有连接与调用完全不受影响。

运行本回归需要一套支持编译模块化 Win64 Development Game 并以 Pak 容器完成 Stage 的 UE 5.6 环境。

## 1. 准备宿主与两个独立作者工程

1. **发布游戏 SDK 包**：先在源仓库中构建 `UnrealModFrameworkDevModular` 与 `UnrealModFrameworkDevEditor`，将两者的 v4 生成清单填入 MDT v3 请求，依次执行 `plan`、`build` 和 `inspect`（参见[构建样例 SDK](../../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md#构建样例-sdk)）。产出的发布包内含供宿主使用的 `Host/Plugins/UMFDevSDK` 以及 `Plugins/` 下的两个作者端插件。
2. **生成隔离的工作区副本**：运行 `prepare_projects.py`，在 `<fresh-workspace>` 下创建三份干净工程副本——`H`（接入了回归验证模块的宿主游戏）、`A`（`TestMod`）与 `B`（`SharedCaller`）：

   ```text
   python Test/SharedRuntime/prepare_projects.py --work <fresh-workspace> --sdk <published-SDK>
   ```

   编译前请将环境变量 `UMF_MHT_EXECUTABLE`、`UMF_MBT_EXECUTABLE`、`UMF_MOD_SOURCE_TOOL_EXECUTABLE`、`UMF_MHT_LIBCLANG` 和 `UMF_MHT_CLANG_RESOURCE_DIR` 指向当前本地构建的工具可执行文件与 Clang 资源头文件目录。
3. **编译、Cook 并 Stage 宿主游戏（`H`）**：在 `H` 中通过 MBT 编译 `UnrealModFrameworkDevEditor` 与 `UnrealModFrameworkDevModular`，随后调用 UAT `BuildCookRun` 对后者执行 Cook 与 Stage，生成包含 `/Game/Levels/Basic/L_Basic` 和 `/Engine/Maps/Entry` 的 Pak 基础发行版。请妥善保留该次最终 Game 构建产出的三份文件供验证器核对：
   - UBT `.target` 回执文件
   - v4 生成清单（`*.generated-artifacts.json`）
   - `Intermediate/ModTools/ModHeaderTool/HostTargets/UnrealModFrameworkDevModular/Win64/Development/x64/host-link.json`

   *（如果使用的是预编译的模块化引擎二进制，请确保其运行期依赖库已一并复制到 Stage 目录中。）*
4. **编译并打包两个作者工程（`A` 与 `B`）**：分别构建两个作者工程的 Editor 目标。`TestMod`（`A`）已自带生命周期蓝图；对于 `SharedCaller`（`B`），请先运行脚本生成入口蓝图：

   ```text
   UnrealEditor-Cmd <SharedCaller.uproject> -run=pythonscript -script=<SharedCaller/create_entry_point.py 的绝对路径> -unattended
   ```

   随后在 `A` 和 `B` 中分别调用 `ModPackage` Commandlet，指定同一份基础发行版、宿主 Game `.target` 回执以及各自全新的任务目录完成无界面打包（见[运行行为](../../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md#运行行为)）。

## 2. 运行实机校验

将干净的 Stage 游戏目录及 `H` 产出的最终清单、链接记录和回执传给验证脚本 `verify_game.py`：

```text
python Test/SharedRuntime/verify_game.py --sdk <SDK> --stage <Stage/Windows> --receipt <Game.target> --link <host-link.json> --host-project <H/UnrealModFrameworkDev.uproject> --host-manifest <final-game.generated-artifacts.json> --registry <Releases/Version/Windows/AssetRegistry.bin> --test-mod <TestMod.mod> --other-mod <SharedCaller.mod> --report <report-directory>
```

验证器会先比对 Stage 目录下的二进制映像、`host-link.json` 与已发布的 SDK，再检查两个 `.mod` 归档内的原生模块载荷并安装到游戏中；游戏运行期间，脚本会主动移走 `TestMod.mod` 以触发热卸载，并在测试结束后清理安装的测试包。最终报告、引擎日志与控制台输出分别保存在 `<report-directory>` 下的 `verification.json`、`game.log` 与 `console.log` 中。

### 运行期核心断言

1. 游戏启动后，测试 `GameMode` 连接共享宿主 ABI，并为 `TestMod` 与 `SharedCaller` 签发相互隔离的调用方令牌。
2. `TestMod` 将包内的 Manny 骨骼网格应用到导出的玩家角色上，同时 `SharedCaller` 通过共享的 `UMFDevSDKRuntime` 成功读取当前生命值 `73`。
3. 移除 `TestMod.mod` 后，加载器卸载 `TestMod` 的容器并吊销其调用方授权。
4. 卸载完成后，`SharedCaller` 必须仍能凭原有的 `UModInstance`、调用方上下文及同一个 `UMFDevSDKRuntime` 正常读出更新后的生命值 `41`；而尝试继续使用 `TestMod` 旧上下文的调用必须被宿主校验拒绝。

*（注：虽然处于活动状态的 World 扩展会话在关闭前会持有包租约，但本回归专门走标准 `ModInstance` 生命周期路径，重点检验容器卸载、卸载前回调及单调用方权限吊销。在 Win64 平台上，已加载的 UE 原生模块 DLL 会常驻内存直至进程退出。）*
