<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# 模组开发 SDK 编辑器

启用 `ModDevelopmentSDKEditor` 后，关卡编辑器主工具栏在“平台”区域之后提供 **Mod** 下拉按钮。选择 **打包 Mod…**，在原生详情面板中设置内容插件、版本、目标游戏和发布参数，再选择 **开始打包**。

开发宿主工程通过 `AdditionalPluginDirectories` 发现 `SourcePlugins`，仅在 Editor Target 启用制作插件。独立交付仍使用 UE 5.6 UAT `BuildPlugin`；安装到作者工程的 `Plugins` 或对应引擎的 `Engine/Plugins` 后启用插件。制作模块不依赖游戏侧 `ModRuntime`。

## 打包输入

- Mod 必须是已启用的项目侧内容插件，`CanContainContent` 为 `true`，没有 C++ 模块，并至少包含一个 `.uasset` 或 `.umap`。内容保留 `/<插件名>/` 包根，不支持 `RemapPluginContentToGame`。
- Mod 版本使用三段数字，例如 `1.0.0`。
- 游戏标识和基础发布版本取自目标游戏 SDK。基础发布目录应包含 `<基础发布版本>/<Cook 平台>/AssetRegistry.bin`；目录留空时使用作者工程的 `Releases`。`Win64` 的 Cook 平台名为 `Windows`。
- 平台、IoStore/Pak 和“允许新增引擎内容”应与目标游戏发布要求一致。作者工程还必须具备对应平台的 UE Cooker、目标 SDK 和实际引用的公共类型/资产。
- 输出目录留空时使用 `Saved/PackagedMods`。目标文件名为 `<插件名>-<Mod版本>-<目标平台>.mod`；已有文件不会覆盖。

这些选项也可以在 **项目设置 → 插件 → Mod 打包** 中修改，保存在本机项目的 EditorPerProjectUserSettings 中。

## 运行行为

开始前提示保存资产。编辑器创建独立的 `UnrealEditor-Cmd` 任务，任务使用当前引擎附带的原生 dotnet 程序直接运行 `AutomationTool.dll BuildCookRun`，执行完整 DLC Cook/Stage。命令不通过 shell 拼接。每次任务使用独有的 Cook/Stage 目录，不复用旧任务的载荷。

任务仅收集所选 Mod 的 Stage 内容，保留 UE 产生的容器文件名、路径和 IoStore 分片，生成 CookReceipt 后写入 `.mod`。基础发布、项目/插件配置在 Cook 前后核对。载荷通过流式 SHA-256 校验；临时包完整复核后提交最终文件。

编辑器显示忙碌和完成/失败通知，日志进入 Output Log。**Mod → 取消打包** 终止任务进程树；超时或插件关闭也终止任务。**打开输出目录** 可定位产物。任务日志、请求和结果保留在 `Saved/ModPackaging/<任务ID>/`。异常终止时可能残留 `.partial` 文件，该文件不是完成的包。

也可使用相同的版本化请求执行无界面打包：

```text
UnrealEditor-Cmd <作者工程.uproject> -run=ModPackage -Request=<Request.json> -unattended -NullRHI
```

请求格式为 `UMF.ModPackageBuildRequest` v1，包含 `pluginName`、`modVersion`、`gameId`、`baseReleaseVersion`、`baseReleaseDirectory`、`targetPlatform`、`outputFile`、`useIoStore`、`includeEngineContent`、`timeoutMinutes`。编辑器自动生成请求，可从成功任务中取得结构。路径相对作者工程解析；请求所在目录作为本次任务工作目录，不能已有 `Stage` 子目录。

## `.mod` v1 布局

所有整数为无符号小端编码，不直接序列化 C++ 结构体。外层不压缩、不签名。最大文件 64 GiB、清单 1 MiB、索引 64 MiB、条目数 65,536。文件布局为 Header、Manifest UTF-8 JSON、连续载荷、Index UTF-8 JSON。

| Header 偏移 | 长度 | 字段 |
| --- | --- | --- |
| 0 | 8 | `UMFMOD` 后跟两个零字节 |
| 8 | 2 | 主版本 `1` |
| 10 | 2 | 次版本 `0` |
| 12 | 4 | Header 字节数 `64` |
| 16 | 8 | Manifest 偏移 `64` |
| 24 | 8 | Manifest 字节数 |
| 32 | 8 | Index 偏移 |
| 40 | 8 | Index 字节数 |
| 48 | 8 | 文件总字节数 |
| 56 | 8 | Flags，必须为 `0` |

Manifest 格式为 `UMF.ModPackage` v1，记录 ModId、版本、插件与包根、实际构建配置及基础发布指纹。Index 的 `entries` 按区分大小写的路径升序排列，每项有 `path`、`offset`、`size`、`storage: "None"`、精确载荷 `sha256`。条目路径不得绝对、穿越目录或存在大小写冲突。

载荷中的 `Content/<Stage相对路径>` 保留原生 Cook 产物；`Metadata/CookReceipt.json` 记录资源根、Stage 路径、构建配置和发布信息。结果格式为 `UMF.ModPackageBuildResult` v1，状态 `Packaged` 仅表示打包和文件校验完成，包含最终文件的 SHA-256。

当前范围为内容型 Mod 打包，不实现游戏端挂载、原生 DLL、签名、框架内容 ID 注册或导出 API 使用分析。Manifest 和结果明确记录 `shippingValidated: false`，Manifest 中 `exportRequirements: "NotVerified"`；不能据此声明已与某个 Shipping 游戏兼容。Pak 与 IoStore 的游戏加载仍需各自验证。
