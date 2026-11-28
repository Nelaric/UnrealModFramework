<!-- Copyright (c) 2026 Nelaric Contributors -->

# Shared Runtime in a packaged game

Run [Release Validation](../../Docs/DevelopmentWorkflow.md#release-validation) on the reviewed candidate with a complete Win64 x64 UE installation. The default builds the Monolithic Development Game and a Pak base release. An existing full engine with Modular Game build products can select `--game-target UnrealModFrameworkDevModular` and run the same checks.

MDT builds and inspects the released SDK. The verifier checks the installed Host plugin, generated contract, launch image, and link receipt. TestMod and SharedCaller build in separate authoring projects. Their C++ callbacks compile against the published C headers, and each archive contains its cooked assets and one independent DLL. The release folder and Asset Registry digest come from the actual base cook.

The gameplay test verifies that both DLLs query the live host ABI and their separate Hook callbacks transform the character's health to 73. TestMod supplies its cooked Manny mesh, and the SDK allows its writer to apply the replacement while denying the reader. The runner then removes TestMod from the staged installation. Closing the World releases package leases and permits unmounting. In the new World the host mesh resolves again, SharedCaller's callback produces 41, and the revoked writer's old context fails. Automation completion and the gameplay markers are required for `verification.json`.

# 打包游戏中的 Shared Runtime

在具备完整 Win64 x64 UE 的环境中，按[发布验证](../../Docs/DevelopmentWorkflow.zh-CN.md#发布验证)运行已审查的候选提交。默认构建 Monolithic Development Game 并创建 Pak 基础发行。已有 Modular Game 产物的完整引擎可指定 `--game-target UnrealModFrameworkDevModular` 运行同样的检查。

MDT 构建并检查发布 SDK。验证器核对已安装的 Host 插件、生成的契约、启动映像和链接回执。TestMod 与 SharedCaller 在独立作者工程中构建，C++ 回调使用正式 SDK 的 C 头文件编译，各自的归档包含 Cooked 资产和单个独立 DLL。基础版本和 Asset Registry 摘要来自此次实际 Cook。

游戏测试验证两个 DLL 都查询同一宿主 ABI，且两个 Hook 回调将角色生命值变为 73。TestMod 提供自己包中的 Manny 网格。SDK 允许写入方应用替换，并拒绝只读调用方。测试随后从暂存游戏移除 TestMod。World 关闭后释放租约，加载器解除挂载。新 World 恢复选中宿主网格，SharedCaller 的回调将生命值变为 41，被撤销的旧上下文失效。只有自动化测试完成且游戏日志记录这些结果，才生成 `verification.json`。
