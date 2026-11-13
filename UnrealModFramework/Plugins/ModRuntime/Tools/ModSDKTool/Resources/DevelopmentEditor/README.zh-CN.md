<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# 模组开发 SDK 编辑器

启用 `ModDevelopmentSDKEditor` 后，关卡编辑器主工具栏在“平台”区域之后提供 **Mod** 下拉按钮。选择 **打包 Mod…**，在详情面板选择内容插件或当前工程，填写版本、目标游戏和发布参数，再选择 **开始打包**。

运行 `ModSDKTool build-editor` 生成制作插件源码，再由 UE 5.6 UAT `BuildPlugin` 独立编译。将构建产物安装到作者工程的 `Plugins` 或对应引擎的 `Engine/Plugins`，再在作者工程中启用。制作模块独立于游戏侧 `ModRuntime`。

## 打包输入

- 内容插件须已在工程中启用，`CanContainContent` 为 `true`，没有 C++ 模块，至少包含一个 `.uasset` 或 `.umap`，并使用独立的 `/<插件名>/` 包根。C++ 工程可选择 **打包当前工程**。
- Mod 版本使用三段数字，例如 `1.0.0`。
- 游戏标识和基础发布版本取自目标游戏 SDK。基础发布目录应包含 `<基础发布版本>/<Cook 平台>/AssetRegistry.bin`；目录留空时使用作者工程的 `Releases`。`Win64` 的 Cook 平台名为 `Windows`。
- 平台、IoStore/Pak 和“允许新增引擎内容”应与目标游戏发布要求一致。作者工程还必须具备对应平台的 UE Cooker、目标 SDK 和实际引用的公共类型/资产。
- 输出目录留空时使用 `Saved/PackagedMods`。目标文件名为 `<插件名>-<Mod版本>-<目标平台>.mod`；已有文件不会覆盖。

这些选项也可以在 **项目设置 → 插件 → Mod 打包** 中修改，保存在本机项目的 EditorPerProjectUserSettings 中。

## 打包当前 C++ 工程

1. 构建 Win64 x64 模块化宿主并生成 Pak 基础发布。在本仓库使用能生成 Game DLL 和导入库的 UE 源码构建，选择 `UnrealModFrameworkDevModular` Game Target，取得宿主实际生成的 UBT `.target` 回执。
2. 打开作者工程，勾选 **打包当前工程**，选择 Win64，关闭 **使用 IoStore**，填入 **宿主 Target 回执**。宿主与作者 Editor 使用不同 Engine 目录时，填写 **宿主 Engine 目录**，并使用与宿主一致的 UE 版本和 Changelist 进行 Cook。回执中的宿主工程和模块清单须在磁盘上可读。
3. 打包器从当前 `.uproject` 读取 Runtime 模块和已启用的插件依赖，用临时插件按宿主 Target 编译，并在 DLC Cook 前将工程资产迁入独立包根。蓝图仍通过原来的模块名解析 `/Script/` 下的父类。
4. 在宿主 `[ModLoader]` 配置中设置 `bAllowNativeModules=True`，使用与打包时一致的基础发布和 Pak 后端，将 `.mod` 放入实际游戏可执行文件同级的 `Mods` 目录。加载器会先注册 DLL 中的类型，再创建生命周期蓝图实例。

Editor 回执用于 Win64 PIE 包，Game 回执用于打包游戏。TestMod 的 `/TestMod/BP_CallbackProbe.BP_CallbackProbe_C` 继承工程中的原生 Mod 实例，C++ 加载回调会写入 `LogTestModCallback`。代码中的固定资源路径需要指向 Cook 后的包路径，也可以使用迁移工具能更新的资产引用。

在 PIE 中验证时，用 `-UsePaks` 启动宿主 Editor，把 Editor 版 `.mod` 放在宿主工程的 `Binaries/Win64/Mods`。如果包包含共享着色器库，带渲染的 PIE 需要把基础发布 Cook 产出的 `ShaderArchive-*.ushaderbytecode` 放到宿主工程的 `Content`，并使用对应的着色器平台。只验证生命周期回调时可以使用 `-NullRHI`。

## 运行行为

开始前提示保存资产。编辑器创建独立的 `UnrealEditor-Cmd` 任务，任务使用当前引擎附带的原生 dotnet 程序直接运行 `AutomationTool.dll BuildCookRun`，执行完整 DLC Cook/Stage。命令不通过 shell 拼接。每次任务使用独有的 Cook/Stage 目录，不复用旧任务的载荷。

任务仅收集所选 Mod 的 Stage 内容，保留 UE 产生的容器文件名、路径和 IoStore 分片，生成 CookReceipt 后写入 `.mod`。基础发布、项目/插件配置在 Cook 前后核对。载荷通过流式 SHA-256 校验；临时包完整复核后提交最终文件。

编辑器显示忙碌和完成/失败通知，日志进入 Output Log。**Mod → 取消打包** 终止任务进程树；超时或插件关闭也终止任务。**打开输出目录** 可定位产物。任务日志、请求和结果保留在 `Saved/ModPackaging/<任务ID>/`。异常终止时可能残留 `.partial` 文件，该文件不是完成的包。

也可使用相同的版本化请求执行无界面打包：

```text
UnrealEditor-Cmd <作者工程.uproject> -run=ModPackage -Request=<Request.json> -unattended -NullRHI
```

请求格式为 `UMF.ModPackageBuildRequest` v1，包含 `pluginName`、`modVersion`、`gameId`、`baseReleaseVersion`、`baseReleaseDirectory`、`targetPlatform`、`outputFile`、`useIoStore`、`includeEngineContent`、`timeoutMinutes`。编辑器自动生成请求，可从成功任务中取得结构。路径相对作者工程解析；请求所在目录作为本次任务工作目录，不能已有 `Stage` 子目录。

打包当前工程时，使用 `schemaVersion: 2`，将 `pluginName` 换为 `hostTargetReceipt` 和按需提供的 `hostEngineDirectory`。每次运行使用新的任务目录。

## `.mod` 布局

所有整数为无符号小端编码，不直接序列化 C++ 结构体。外层不压缩、不签名。最大文件 64 GiB、清单 1 MiB、索引 64 MiB、条目数 65,536。文件布局为 Header、Manifest UTF-8 JSON、连续载荷、Index UTF-8 JSON。

| Header 偏移 | 长度 | 字段 |
| --- | --- | --- |
| 0 | 8 | `UMFMOD` 后跟两个零字节 |
| 8 | 2 | 内容包为主版本 `1`，含原生代码的内容包为主版本 `2` |
| 10 | 2 | 次版本 `0` |
| 12 | 4 | Header 字节数 `64` |
| 16 | 8 | Manifest 偏移 `64` |
| 24 | 8 | Manifest 字节数 |
| 32 | 8 | Index 偏移 |
| 40 | 8 | Index 字节数 |
| 48 | 8 | 文件总字节数 |
| 56 | 8 | Flags，必须为 `0` |

Manifest 格式为 `UMF.ModPackage`，其版本与 Header 主版本一致，记录 ModId、版本、插件与包根、实际构建配置及基础发布指纹。Index 的 `entries` 按区分大小写的路径升序排列，每项有 `path`、`offset`、`size`、`storage: "None"`、精确载荷 `sha256`。条目路径不得绝对、穿越目录或存在大小写冲突。

作者 SDK 提供 `ModInstance` 和 `ModInstanceSubSystem` 时，打包器会把所选内容插件内的具体派生蓝图类写入可选的 `lifecycleClasses` 字段。游戏挂载后按这些类路径建立会话入口。

载荷中的 `Content/<Stage相对路径>` 保留原生 Cook 产物；`Metadata/CookReceipt.json` 记录资源根、Stage 路径、构建配置和发布信息。结果格式为 `UMF.ModPackageBuildResult` v1，状态 `Packaged` 仅表示打包和文件校验完成，包含最终文件的 SHA-256。

v2 在清单中增加 `nativeCode`，记录宿主 Target、配置、架构、BuildId 和模块名。打包器用 UBT 编译所选模块，并根据宿主回执生成对应的 `.modules`。模块清单和 DLL 写入 `Native/`，使用同一份索引校验。已加载的原生模块驻留到进程退出，替换原生模块版本需要重启宿主。

Manifest 和结果记录 `shippingValidated: false`，Manifest 中 `exportRequirements` 为 `NotVerified`。
