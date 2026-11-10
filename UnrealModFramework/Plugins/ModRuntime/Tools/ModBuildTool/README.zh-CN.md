<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# ModBuildTool

MBT 独立编译 MHT 或 ExportCodeGenerator 已生成的宿主连接代码，为一个所属模块产出原生静态库。游戏构建随后将该库链接到所属二进制；MHT 源文件不进入 UBT 的 C++ 编译列表。开发者的目标用法仍是导出宏与原函数声明。

当前实现包含库构建、多符号校验、供 MST 消费的公共产物清单，以及宿主库与成功链接记录的关联核对。[UHT 适配层](../../Source/ModRuntimeUht/README.zh-CN.md) 已为示例 UE 5.6 Win64 MSVC 目标调用 MHT 生成和 MBT 独立编译，私有 External 模块规则将库链接到所属模块。UE 5.6 的 PostBuildSteps 核对宿主库与实际链接产物并发布链接回执。现有 MHT `scan` 报告不能作为 MBT 输入。`Built` 表示宿主库构建完成，`Linked` 表示与可信链接记录及实际二进制的关联核对通过，均不证明游戏运行时行为。

## 构建和命令

从仓库根目录构建工具：

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModBuildTool
```

已配置的 CMake 构建目录也可直接构建 `ModBuildTool` target。使用具体构建目录中的程序调用：

```text
ModBuildTool plan --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json
ModBuildTool build-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json --expected-input <plan.inputFingerprint> --output BuildResult.json
ModBuildTool verify-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json
ModBuildTool finalize-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json --link-receipt Target.link.json --output LinkedArtifacts.json
ModBuildTool hash-context --file HostCompileContext.json
ModBuildTool hash-file --file <path>
ModBuildTool inspect-archive --file <archive> --platform Win64 --architecture x64 --anchor <symbol> --symbol <api-symbol-1> --symbol <api-symbol-2>
ModBuildTool inspect-binary --file <image> --platform Win64 --architecture x64 --kind Executable
```

`plan` 不执行编译或创建输出目录，返回当前输入指纹、所属模块、目标、宿主库位置及缓存状态。`build-host` 获取输出锁后重新校验计划，命中完整缓存时返回 `UpToDate`，否则独立编译并归档后返回 `Built`。`verify-host` 在缓存或产物过期时返回非零。退出码 `0` 为成功，`1` 为参数、输入、编译、I/O 或验证失败。JSON 结果写入 stdout 或 `--output`；工具链诊断写入 stderr。

## 宿主编译上下文

正式结构见 [host-context.schema.json](Protocol/host-context.schema.json)。UHT 适配层已从实际 UBT 编译数据库转交所属模块的真实 MSVC 配方，并核对生产者定义。其他接入同样必须提供真实所属模块上下文，不能从系统默认编译器或手写近似 UE 环境推测。

必需字段：

| 字段 | 合同 |
| --- | --- |
| `format` / `schemaVersion` | `UMF.ModHostContext` / `2` |
| `ownerModule` | 真实声明所属模块；编译单元必须与其一致 |
| `moduleAPIMode` | `Producer`，保留所属模块的生产者宏定义 |
| `registrationAnchor` | 未装饰的 C 标识符；必须进入静态库的符号索引 |
| `owningBinary` | 可选的真实所属二进制 `id`、`path`、`kind`；`finalize-host` 必需 |
| `target` | `name`、`type`、`configuration`、`linkType`、`platform`、`architecture` |
| `workingDirectory` | 实际工具链工作目录 |
| `outputLibrary` | 生成根之外的 `.lib` 或 `.a`；相对路径基于上下文目录 |
| `compiler` / `archiver` | 实际工具路径、参数数组和 response file 格式 |
| `environment` | 覆盖继承环境的字符串映射；有效环境摘要参与缓存，不输出环境值 |
| `dependencies` | 编译配方依赖、强制包含、输入清单及其他工具链相关文件 |
| `lto` / `pch` / `unity` | 首版均须显式为 `false` |

编译器参数数组必须包含 `{source}`、`{object}` 和 `{dependencies}`，分别展开为本次真实路径。归档器参数必须包含 `{output}`，并用一个独立的 `{objects}` 元素展开当前全部对象。参数通过响应文件直接传给工具，不经 shell。

MSVC 编译器配方使用 `dependencyFormat: "msvc-json"`，并显式添加 `/sourceDependencies` 与 `{dependencies}`。GNU/Clang 使用 `dependencyFormat: "make"`，例如通过 `-MD -MF {dependencies}` 输出完整依赖。需要由适配层保证系统头等真实编译依赖也被记录。

`responseFormat: "msvc"` 使用 UTF-16LE BOM 与 Windows 参数引用规则，选项及其独立值保留在同一行；`"gnu"` 使用 UTF-8 与 GNU 响应文件引用规则。MSVC response file 只在 Windows 宿主支持。工具路径必须指向真实可执行文件，不支持批处理或 shell 配方。

例如参数模板可以包含以下片段；这只是占位语法，不能代替实际 UE 编译配方：

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

## 完整生成清单

新生产者使用 [generated-artifacts.schema.json](Protocol/generated-artifacts.schema.json)，格式为 `UMF.GeneratedArtifacts`，版本 `3`，状态 `Complete`，且 `unresolvedTypeCount` 为零。MBT 仍能读取使用旧哈希字段名的 [v1](Protocol/generated-artifacts-v1.schema.json) 和 v2 清单。最终链接核对要求 v2 或 v3。`Bootstrap` 或扫描报告均拒绝构建。

v2 和 v3 的 `hostBuildContexts` 为每个编译所属模块绑定上下文哈希。用 `hash-context` 计算已解析 JSON 的紧凑排序摘要。v1 的单上下文回退字段是 `buildContextHash`。MBT 重验 v2 和 v3 的每项 `inputFiles` 内容哈希，并重新枚举 `watchedDirectories`。输入过期时，`GeneratedInputsStale` 指出需要重新运行 MHT 的路径。

`environmentMode: Explicit` 将声明的 `environment` 作为编译子进程环境。默认的 `Inherit` 模式用这些值覆盖继承环境。计划和构建回执均提供有效的 `environmentHash`。

`generationId`、`inputHash`、`contractHash`、`projectionHash` 和 `nativeABIHash` 均为小写 SHA-256。契约、投影和 ABI 摘要由生成器提供；MBT 不重新解释 C++ 类型或证明这些语义摘要正确。

`generatedFiles` 列出相对生成清单目录的路径、`contentHash` 与 `ownerModule`。共享契约文件的所属模块可以为 `null`。路径须唯一，不含 `..`，解析符号链接后也不得离开生成根。文件内容须逐项匹配摘要。

`compilationUnits` 显式列出 `ownerModule` 和 `source`。MBT 仅编译当前所属模块的 `.cpp`、`.cc` 或 `.cxx` 单元；它们必须已经列入 `generatedFiles`，不能由 MBT 递归扫描猜测。空导出模块也必须由生成器提供含稳定注册锚点的单元，避免复用删除导出前的旧库。

## 缓存、发布与库检查

v2 和 v3 的每个生成文件都必须有 `role`：`HostBinding`、`HostRegistration`、`CompileVerification`、`HostHeader`、`SdkHeader`、`Contract`、`SdkResource` 或私有 `Diagnostic`。`plan`、`build-host` 和 `finalize-host` 的 `publicArtifacts` 仅列出当前模块及共享的 `SdkHeader`、`Contract`、`SdkResource`，包含绝对 `sourcePath`、规范化 `relativePath`、内容哈希、角色和所属模块。SDK 头仅接受 `.h` / `.hpp`，契约接受 `.json`；宿主编译单元不能同时作为公共产物发布。v1 未指定的角色和未知角色不进入公共清单。

`requiredSymbols` 为 `{ "ownerModule": "...", "symbol": "..." }` 记录数组，可附 `exportId`、`signatureHash` 和 `projectionHash`。符号使用未装饰 C 标识符，重复模块/符号记录及同一所属模块内的重复 ExportId 拒绝；Host 和 Client 可为相同逻辑 ExportId 各自记录入口。MBT 合入上下文中的稳定注册锚点，对当前模块的整个必需集合执行一次归档检查；Mac 检查时加平台 C 符号前缀。零导出批次可使用空数组，但注册锚点仍必需。v1 可省略该数组，此时保留原有的单锚点检查。

输入指纹覆盖 MBT 可执行文件、工具版本、原始上下文和生成清单、全部生成文件、编译器及归档器、显式依赖、上次成功编译报告的传递依赖，以及有效子进程环境。新增 include 由生成源或既有依赖的变化触发重编译，再登记新的完整依赖集合。

输出旁的 `<library>.mbt.json` 是提交凭证，记录输入指纹、目标、生成批次、上下文摘要、库哈希、注册锚点和编译器依赖。复用必须同时满足输入指纹、文件摘要和库检查。缺失、损坏或错误版本的凭证不会成为成功缓存。

编译使用独占 OS 锁和本次独有的暂存目录。每次失效都重新编译该模块的生成单元，并从完整对象列表创建新归档，不向旧库追加成员。校验后先替换库，再原子提交凭证；中断导致的不匹配不会通过后续验证。暂存目录由本次进程创建并清理。

MBT 检查普通归档容器、对象架构及全部必需符号的索引，缺失时报告完整缺失集合。识别 Win64 COFF、Linux ELF、Mac Mach-O，架构为 `x64` 和 `arm64`。导入库成员、薄归档、未知对象格式和不匹配架构拒绝。LTO、PCH、Unity 与 C++ modules/header units 不属于当前支持范围。不同平台支持仍需在各平台实际编译和构建验证；当前 Windows 结果不代表 Linux/Mac 已验收。

## 获取角色与增加生命的清单片段

MHT 输出 v3 清单，列出宿主桥、C ABI 入口、校验和 UE 视图实现四个宿主单元，并为 `<game>ModClient` 列出独立客户端单元。上下文锚点分别为 `UMF_ModHost_<owner>` 和 `UMF_ModClient_<game>`。[MHT ABI 协议](../ModHeaderTool/README.zh-CN.md) 给出实际公共头、Host/Client 符号记录及 MST 契约。

这两个 API 不写死在 MBT 中。下列简化 v3 片段展示 `ExportTest` 的记录方式；实际 MHT 符号名从生成描述中读取，MBT 自动加入上下文的注册锚点：

```json
{
  "requiredSymbols": [
    { "ownerModule": "ExportTest", "symbol": "UMF_ExportTest_GetLocalCharacter", "exportId": "Game.Player.GetLocalCharacter" },
    { "ownerModule": "ExportTest", "symbol": "UMF_ExportTest_AddHealth", "exportId": "Game.Character.AddHealth" }
  ]
}
```

MHT 将 `SDK/Public/<game>.modabi.h` 标记为 `SdkHeader`，生成 JSON 契约标记为 `Contract`，实现作为私有编译单元。Host/Client 为每个 ExportId 记录匹配的签名和投影哈希；内部 UE 视图头使用私有 `HostHeader` 角色。完整清单仍须列出所有内容哈希与编译单元，以上不是可直接运行的完整输入。

构建结果同时返回两个入口、注册锚点、公共文件清单和 `OwnerBinaryOnly` 链接要求。MST 根据公共清单复制并再次核对文件哈希；MBT 不发布宿主私有源码，也不构建最终开发者插件。

## 最终链接核对

`finalize-host` 消费 [host-link-receipt.schema.json](Protocol/host-link-receipt.schema.json)，输出 [linked-artifacts.schema.json](Protocol/linked-artifacts.schema.json)。UE 5.6 Win64 x64 适配器在 PostBuildSteps 调用 `finalize-ue-target`。MBT 核对 UBT 目标回执和本次归档的 PE 见证符号，在宿主库旁写出链接记录和关联清单。其他链接适配器可以向 `finalize-host` 提供可信链接记录。

上下文须先声明 `owningBinary`，其 `id` 为 UBT 中稳定的二进制身份，`path` 相对上下文目录解析，`kind` 为 `Executable` 或 `DynamicLibrary`。这样 Editor 模块 DLL 与 monolithic 游戏可执行文件有明确区分。

链接记录使用 `UMF.ModHostLinkReceipt` v1，包含：

| 字段 | 要求 |
| --- | --- |
| `status` | `Succeeded` |
| `target` | 与编译上下文的 Target 对象完全相同 |
| `binary` | 所属二进制 `id`、`path`、`kind`、实际 `contentHash` 和唯一的 `ownerModules` 列表 |
| `hostLibraries` | 按所属模块列出实际参与最终链接的宿主库；同模块不重复 |
| 每个宿主库记录 | `ownerModule`、`path`、`contentHash`、`hostReceiptHash`、`contextHash`、`generationId`、`registrationAnchor`、`anchorRetained: true` |

记录中的路径相对链接记录目录解析。MBT 要求当前宿主构建仍然有效，并检查所属二进制身份、模块归属、宿主库路径及哈希、构建凭证哈希、上下文、批次、锚点保留声明和实际二进制哈希。实际图像再检查 PE/ELF/薄 Mach-O 类型与架构；ELF 区分 PIE 可执行文件与共享库，Mach-O fat 图像暂不支持。检查期间输入变化则拒绝。

成功输出 `UMF.ModHostLinkedArtifacts` / `Linked`，包含公开文件、契约摘要、必需符号、实际二进制及库哈希、宿主构建凭证和链接记录的路径/哈希，供 MST 进一步汇总。记录的真实性和锚点保留事实依赖可信适配器；MBT 不从任意 JSON 或二进制字符串推断链接成功，也不把 `Linked` 当作运行时绑定就绪。

## UE 5.6 集成

当前适配器在标准 CodeGen 后的 UHT 阶段同步运行 MHT 和 MBT。生成头只含声明，全部生成实现以 `.cpp` 列入 MBT 清单。`ExportMetadata.ConfigureHost` 写入真实 Target 描述并添加所属模块私有的 External 依赖；External 模块使用 `PublicPreBuildLibraries` 登记稳定宿主库，即使首次构建开始时库不存在也能接入。正常缓存构建使用已登记外部依赖，强制重复生成时 MBT 可复用有效回执。本机引擎没有补丁；干净检出的引导和编译数据库准备仍是前置步骤。

以下完整动作图接入仍是后续合同：

1. 创建 Makefile 时，从真实模块图生成每模块计划与所属二进制映射，在标准 UHT 后取得可用的生成头及真实编译上下文，再调用 MHT 完整生成。
2. 缓存 Makefile 时，也必须在 UHT 检查后、动作失效判断前检查源码集合与依赖。普通非反射头的新增、删除和改动不能只依赖 UHT 重跑。
3. 调用 `plan`，将当前 `inputFingerprint` 写入 MBT 动作的有效命令或 `CommandVersion`。`upToDate=false` 还须使该动作明确失效，包括库损坏但输入未变的情况。
4. 动作执行 `build-host --expected-input ...`。其 `ProducedItems` 包含库和凭证，`bProducesImportLibrary=false`，首版禁止远程和 UBA 执行。
5. 在真实所属 `UEBuildBinary` 的私有最终链接环境中加入库和注册锚点的保留要求，不沿通用 Runtime 公共依赖传播。库作为最终链接动作的 `PrerequisiteItems`，形成明确生产依赖。
6. 每个参与模块预留稳定库路径与动作。无导出时生成空注册单元，首次新增导出和删除最后一项均无需改变动作图结构。变体区分引擎、工具链、Target、配置、平台和架构。
7. Windows/Linux/Mac 分别实现最终链接的符号保留。不得把宿主静态库标记为导入库，或错误加入仅生成 UE 导入库的动作。模块输出、Clean、receipt 与插件裁剪也要包含宿主产物。
8. 成功链接完成后导出当前目标的可信链接记录，调用 `finalize-host`。按模块汇总所有必需关联结果后，MST 才能组织完整 SDK 发布。

`GenerateHeaderFuncs` 在这份 UE 5.6 的 Makefile 重建路径位于 UHT 后，但缓存路径不会重新执行它，因此不能作为唯一触发点。现有 `.ubtplugin.csproj` 的 UHT Exporter 入口也不等同于完整 UBT 动作扩展。

首次缺少依赖 Editor 的选项目录时，由顶层流程先完成不可发布的 Bootstrap 和选项目录导出，再启动正式构建。MBT 不在目标回调中递归构建当前 Target。Live Coding、Hot Reload、预编译分发和远程 Mac 需要单独适配，不能由普通 UBT 增量推导为已支持。
