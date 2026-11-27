# 共享 Runtime 的打包游戏回归测试

这个测试使用模块化 Win64 x64 Development 游戏、Pak 基础发行版、独立作者工程 TestMod 和 SharedCaller。它需要能构建该模块化 Game 的 UE 工具链。两个 Mod 都调用实际游戏中的 `UMFDevSDKRuntime`。

1. 以游戏及 Editor 的 v4 生成清单运行 MDT `plan`、`build`、`inspect`。发布包包含游戏用的 `Host/Plugins/UMFDevSDK` 和作者工程用的两个插件。
2. 运行 `python Test/SharedRuntime/prepare_projects.py --work <空工作区> --sdk <发布目录>`。它将宿主与 `Test/ReleaseFixtures` 的发布资产按原包名暂存到 `H`，把两个作者工程分别放入 `A` 和 `B`。宿主启用测试模块。将 `UMF_MHT_EXECUTABLE`、`UMF_MBT_EXECUTABLE`、`UMF_MOD_SOURCE_TOOL_EXECUTABLE` 和 `UMF_MHT_LIBCLANG` 指向当前构建的工具。
3. 先对 `H` 调用 MBT 的 `prepare-metadata`，再用同一 Bundle 调用 `build-ue-target` 构建 `UnrealModFrameworkDevEditor` 和 `UnrealModFrameworkDevModular`。使用 UAT `BuildCookRun` 为后者 Cook、Stage 并创建基础发布版本，其中包含 `/Game/Levels/Basic/L_Basic` 和 `/Engine/Maps/Entry`。保留本次 Game 的 `.target` 与 `Intermediate/ModTools/ModHeaderTool/HostTargets/<Target>/Win64/Development/x64/host-link.json`。从实际 Cook 的 `AssetRegistry.bin` 计算摘要。完整运行入口见[发布验证](../../Docs/DevelopmentWorkflow.zh-CN.md#发布验证)。
4. 分别构建两个作者 Editor。用 `UnrealEditor-Cmd <SharedCaller.uproject> -run=pythonscript -script=<SharedCaller/create_entry_point.py 的绝对路径> -unattended` 在 B 中创建生命周期蓝图。A 自带样例蓝图。用两个工程自己的 `ModPackage` commandlet、同一个基础版本和 Game 回执分别打出 `TestMod.mod`、`SharedCaller.mod`。每次打包使用新的任务目录。
5. 用下列命令启动真实暂存游戏。验证器校验 DLL 和链接记录，检查两个包的原生文件索引，并在两个 Mod 调用成功后移走暂存安装的 TestMod。成功时在指定目录生成 `verification.json` 和 `game.log`。

```text
python Test/SharedRuntime/verify_game.py --sdk <SDK> --stage <Stage/Windows> --receipt <Game.target> --link <host-link.json> --host-project <H/UnrealModFrameworkDev.uproject> --host-manifest <游戏本次构建的生成清单> --registry <Releases/版本/Windows/AssetRegistry.bin> --test-mod <TestMod.mod> --other-mod <SharedCaller.mod> --report <报告目录>
```

测试 GameMode 使用样例游戏导出的玩家角色和普通 Mod 生命周期。游戏进程只绑定一次宿主 ABI，分别给两个 Mod 授权。A 必须用自己包内的 Manny 网格改变玩家，B 必须读到 73。加载器通知卸载 A 时，宿主收回 A 的授权。A 的挂载点消失后，B 必须以原实例、原上下文和同一个 Runtime 读到更新后的值 41。测试还检查 A 的旧上下文已经失效。

样例的 World 扩展模式会在选定会话内持有包租约，直到会话释放。它有独立的卸载时机。Windows 原生 UE 模块按现有加载器设计驻留到进程退出，此处验证包卸载、生命周期回调和调用权回收。
