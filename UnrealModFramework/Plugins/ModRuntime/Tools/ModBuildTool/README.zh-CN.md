<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# ModBuildTool（MBT）

ModBuildTool（MBT）用于驱动启用了自动 C++ Hook 的 UnrealBuildTool（UBT）构建：将相关编译动作的源文件替换为改写后的副本，并在链接完成后核验受支持桌面目标的最终二进制映像。与之配合，[ModRuntime UHT 适配层](../../Source/ModRuntimeUht/README.zh-CN.md)负责把 MHT 生成的宿主与编译校验单元注册进所属 UE 模块编译，ModSDKTool（MDT）则负责把生成的 Client 源码编入共享 SDK Runtime（参见 [v4 生成清单 Schema](Protocol/generated-artifacts.schema.json)）。

## 构建与命令行用法

在仓库根目录执行以下命令即可构建 MBT（也可在已配置的 CMake 构建目录中直接编译 `ModBuildTool` 目标）：

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModBuildTool
```

面向 UBT 目标构建的核心命令如下：

```text
ModBuildTool build-ue-target --engine <引擎目录> --project <工程.uproject> --target <目标>
                             --platform <平台> --configuration <配置>
                             --architecture <架构> --mode <normal|rebuild|bootstrap>
ModBuildTool finalize-ue-target --contexts watch.json --ue-target-receipt Target.target
ModBuildTool refresh-ue-watches --contexts watch.json
ModBuildTool hash-file --file <path>
ModBuildTool inspect-binary --file <image> --platform Win64 --architecture x64 --kind Executable
```

`build-ue-target` 调用引擎内置的 .NET 运行时，针对单个目标架构执行 UBT 构建：

- `normal`：先导出待执行的编译动作，核对 MHT 生成的 Hook 源码映射与源文件哈希，将对应编译输入替换为改写副本后再交由 UBT 执行，同时记录目标文件哈希以支持增量复用。当导出快照所需但尚未编译的 Editor 二进制缺失时，`normal` 模式会自动引导构建已配置的 Editor Target。
- `rebuild`：在导出编译动作前先向 UBT 下发完整清理重建请求。
- `bootstrap`：带 `-MhtBootstrap` 参数调用 UBT 构建 Development Editor，跳过 Hook 改写直接编译宿主原函数。

MBT 会把 UBT 后置校验步骤排在所有待执行链接动作之后，确保读取的是链接完毕的最终二进制；Target 规则读取的 `Config/ModHeaderTool.json` 及分目标生成目录与直接运行 UBT 完全一致。在 Windows 上，预构建步骤会直接复用当前正在运行的 `ModBuildTool.exe`，同时增量重编 MHT 与 MST；若切换分支后 MBT 自身源码有更新，请先重新运行一次根目录的 Setup。

## 最终链接核对

对于已完成完整生成的 `Win64`、`Linux`、`LinuxArm64` 与 `Mac` 单架构（`x64` 或 `arm64`）桌面构建，Target 规则会自动调用 `finalize-ue-target`。MBT 根据 UBT 产出的 `.target` 回执与 `.modules` 清单定位各个宿主模块所属的模块化动态库或单体可执行文件，核验其中确实包含本批次专属的 `UMF_HostBinding_<owner>_<generationId>` 符号，随后写出 `UMF.UbtHostLinkReceipt` v1 记录：

```text
Intermediate/ModTools/ModHeaderTool/HostTargets/<Target>/<Platform>/<Configuration>/<Architecture>/host-link.json
```

该文件记录了目标平台组合、生成批次与契约标识、UBT 回执哈希，以及每个宿主映像的路径、二进制类型、SHA-256 摘要和批次符号。桌面端检查覆盖 PE、ELF 与薄 Mach-O 格式（Apple 平台还会调用 Xcode `nm` 额外核对 Hook 链接符号）。正式对外发布前，请务必在 Stage 后的游戏包中实测 SDK 加载与调用（参见 [SDK 安装与验证](../ModSDKTool/README.zh-CN.md#安装与验证)及[共享 Runtime 打包游戏回归](../../../../../Test/SharedRuntime/README.zh-CN.md)）。

在增量构建中，`refresh-ue-watches` 会将当前磁盘输入与变体目录 `watch.json` 指向的 v4 生成清单比对：一旦监视目录的文件集合或已追踪源码发生变动，MBT 就会更新 `.mht-watch.stamp`，促使 UBT 重新收集编译动作（见 [ExportMetadata.Build.cs](../../Source/Runtime/ExportMetadata/ExportMetadata.Build.cs)）。

Apple 与移动端 Hook 测试用例的平台架构列表及接入注意事项详见[自动 Hook 构建](../../Source/ModRuntimeUht/README.zh-CN.md#自动-hook-构建)。Live Coding、Hot Reload、预编译二进制分发及远程 Mac 构建请结合具体项目环境单独验证。

## 旧版归档命令

对于自行提供所属模块编译配方的旧版静态库归档生产者，MBT 保留了以下独立归档命令（返回状态中，`Built` 表示新编译完成，`UpToDate` 表示校验通过并命中缓存，`Linked` 表示与最终目标映像的关联核验通过）：

```text
ModBuildTool plan --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json
ModBuildTool build-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json
ModBuildTool build-planned-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json --input-fingerprint <plan.inputFingerprint>
ModBuildTool verify-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json
ModBuildTool finalize-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json --link-receipt Target.link.json
ModBuildTool hash-context --file HostCompileContext.json
ModBuildTool inspect-archive --file <归档> --platform Win64 --architecture x64 --anchor <符号>
```

其中，`plan` 仅校验输入而不触发编译或建目录；`build-host` 获取独占锁并执行归档编译；`build-planned-host` 在持锁状态下额外核对预期输入指纹；`verify-host` 在输入或产物过期时报错。所有命令将 JSON 结果写入 stdout、工具链诊断写入 stderr；退出码 `0` 代表成功，`1` 代表参数非法、构建/I/O 错误或缓存校验失败。

## 宿主编译上下文

旧版静态库模式必须传入真实所属模块的编译配方并保留生产者导出宏定义（详见 [归档上下文 Schema](Protocol/host-context.schema.json)）：

| 字段 | 契约要求 |
| --- | --- |
| `format` / `schemaVersion` | `"UMF.ModHostContext"` / `2` |
| `ownerModule` | 声明所属的真实 UE 模块名；待编译单元必须归属该模块 |
| `moduleAPIMode` | `"Producer"`，确保保留所属模块的生产者 `*_API` 宏定义 |
| `registrationAnchor` | 未修饰的纯 C 符号名；必须出现在产出静态库的符号索引中 |
| `owningBinary` | 可选的最终二进制信息（`id`、`path`、`kind`: `"Executable"` 或 `"DynamicLibrary"`）；执行 `finalize-host` 时必填 |
| `target` | 包含 `name`、`type`、`configuration`、`linkType`、`platform` 与 `architecture` |
| `workingDirectory` | 启动编译器与归档器子进程时的真实工作目录 |
| `outputLibrary` | 位于托管生成根目录之外的 `.lib` 或 `.a` 路径（相对路径基于上下文文件所在目录解析） |
| `compiler` / `archiver` | 工具可执行文件 `path`、`arguments` 参数数组及 `responseFormat`（`"msvc"` 或 `"gnu"`） |
| `environment` / `environmentMode` | `"Inherit"`（默认叠加）或 `"Explicit"`（纯净替换）环境变量表；仅将有效 `environmentHash` 写入回执而不泄露明文值 |
| `dependencies` | 强制包含头、输入清单及其他工具链相关依赖文件 |
| `lto` / `pch` / `unity` | 当前版本三项均须显式设为 `false` |

编译器 `arguments` 必须包含 `{source}`、`{object}` 和 `{dependencies}` 三个占位符；归档器 `arguments` 必须包含 `{output}` 以及单独作为一个数组元素的 `{objects}`（展开为全部目标文件路径）。MBT 通过响应文件直接启动真实可执行程序，不经过 Shell（不支持 `.bat` 或 Shell 包装脚本）：

- `"msvc"` 响应文件仅支持在 Windows 主机运行，采用带 BOM 的 UTF-16LE 编码与 Windows 引号规则（选项与紧随的参数值保持在同一行），搭配 `dependencyFormat: "msvc-json"`（`/sourceDependencies`）。
- `"gnu"` 响应文件采用 UTF-8 编码与 GNU 引号规则，搭配 `dependencyFormat: "make"`（如 `-MD -MF {dependencies}`，须一并覆盖系统头文件依赖）。

下面给出占位符结构示意（实际接入须使用真实的 UE 模块编译参数）：

```json
{
  "compiler": {
    "path": "<actual-compiler>",
    "responseFormat": "msvc",
    "dependencyFormat": "msvc-json",
    "arguments": ["<actual-owner-options>", "/c", "{source}", "/Fo{object}", "/sourceDependencies", "{dependencies}"]
  },
  "archiver": {
    "path": "<actual-archiver>",
    "responseFormat": "msvc",
    "arguments": ["/NOLOGO", "/OUT:{output}", "{objects}"]
  }
}
```

## 完整生成清单契约

旧版归档命令支持状态为 `"Complete"` 且 `unresolvedTypeCount` 为 `0` 的 **v1–v3** `UMF.GeneratedArtifacts` 清单（其中 `finalize-host` 要求 **v2** 或 **v3**；见 [v1 Schema](Protocol/generated-artifacts-v1.schema.json) 与 [v3 Schema](Protocol/generated-artifacts-v3.schema.json)）。

在 v2 和 v3 清单中，`hostBuildContexts` 将每个编译所属模块映射到其紧凑排序 JSON 的摘要（可用 `hash-context` 计算），v1 则使用单上下文字段 `buildContextHash`。接收清单前，MBT 会逐一复核 `inputFiles` 哈希并重新遍历 `watchedDirectories`；一旦输入已发生变动，便会返回 `GeneratedInputsStale` 指明需要重跑 MHT 的具体路径。`generationId`、`inputHash`、`contractHash`、`projectionHash` 与 `nativeABIHash` 均为小写 SHA-256 字符串。

`generatedFiles` 列出无 `..`、解析符号链接后仍留在清单根目录内的唯一相对路径、`contentHash` 及可为 `null` 的 `ownerModule`。`compilationUnits` 显式列出需要编译的 `.cpp`/`.cc`/`.cxx` 源文件及归属模块——即便某个模块当前没有任何导出项，生成器也必须产出一个定义了 `registrationAnchor` 的空单元，防止误复用删掉导出前的陈旧静态库。

## 缓存、原子发布与静态库校验

v2 与 v3 清单中的每个文件均标有明确的 `role`（`HostBinding`、`HostRegistration`、`CompileVerification`、`HostHeader`、`SdkHeader`、`Contract`、`SdkResource` 或私有 `Diagnostic`）。`plan`、`build-host` 和 `finalize-host` 输出的 `publicArtifacts` 只会暴露当前模块及共享的 `SdkHeader`（`.h`/`.hpp`）、`Contract`（`.json`）与 `SdkResource`；宿主编译单元及 v1 未标注角色的文件一律保持私有。

`requiredSymbols` 登记 `{ "ownerModule", "symbol" }` 数组（未修饰 C 符号名，可附带 `exportId`、`signatureHash` 与 `projectionHash`）。同一所属模块内不允许出现重复符号或重复 `exportId`，但 Host 与 Client 可各自记录同一逻辑 `exportId` 的对端入口。MBT 会并入 `registrationAnchor`，在单遍扫描静态库索引时核验全部必需符号（Mach-O 自动补齐平台 `_` 前缀），缺失时一次性报出完整缺失列表。

输入指纹综合覆盖 MBT 可执行文件与版本、上下文与生成清单、全部生成源码、编译器与归档器、显式依赖、前次成功编译解析出的传递头文件依赖及 `environmentHash`。编译全程持有操作系统独占锁并使用独立临时目录：一旦缓存失效，便完整重编该模块所有单元并新建全新静态库（杜绝残留已删除成员），校验通过后先替换库文件，最后原子写入同目录的 `<library>.mbt.json` 凭证。静态库格式检查覆盖 `x64` 与 `arm64` 的 Win64 COFF、Linux ELF 和 macOS Mach-O，直接拒绝导入库成员、薄归档（thin archive）、LTO/PCH/Unity 产物及 C++20 模块单元。

### 旧版归档的最终链接核对

`finalize-host` 读取可信适配方产出的 `UMF.ModHostLinkReceipt` v1 链接记录（[链接记录 Schema](Protocol/host-link-receipt.schema.json)），将静态库与磁盘上的真实最终二进制逐项核对后输出 `UMF.ModHostLinkedArtifacts`（[关联清单 Schema](Protocol/linked-artifacts.schema.json)）：

| 链接记录字段 | 校验要求 |
| --- | --- |
| `status` | 必须为 `"Succeeded"` |
| `target` | 与编译上下文中的 `target` 对象完全一致 |
| `binary` | 所属二进制的 `id`、相对 `path`、`kind`、实际 `contentHash` 及不重复的 `ownerModules` |
| `hostLibraries` | 按所属模块不重复列出实际参与链接的静态库，每项含 `path`、`contentHash`、`hostReceiptHash`、`contextHash`、`generationId`、`registrationAnchor` 与 `anchorRetained: true` |

MBT 要求当前宿主构建未过期，逐项核实二进制身份、模块归属、库与回执哈希、批次与上下文摘要，并解析检查最终二进制是否为架构匹配的 PE、ELF（严格区分 PIE 可执行文件与共享库）或薄 Mach-O 镜像（不支持 fat Mach-O；校验期间文件若发生变动直接报错）。
