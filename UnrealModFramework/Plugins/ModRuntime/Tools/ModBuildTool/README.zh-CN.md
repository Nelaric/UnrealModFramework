<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# ModBuildTool

MBT 驱动 UBT 使用生成的自动 Hook 源码副本，并依据目标回执核对桌面原生二进制。宿主与编译校验单元由 UBT 在所属模块编译。

当前实现覆盖静态库构建、多符号校验、供 ModSDKTool（MDT）消费的公共产物清单，以及宿主库与成功链接记录的关联核对。[UHT 适配层](../../Source/ModRuntimeUht/README.zh-CN.md)为示例 UE 5.6 Win64 MSVC 目标调用 MHT 生成与 MBT 独立编译，并通过私有 External 模块规则将宿主库链接到所属模块；UE 5.6 的 PostBuildSteps 随后核对宿主库与实际链接产物并发布链接回执。MHT `scan` 报告不能作为 MBT 输入。`Built` 表示宿主库构建完成，`Linked` 表示其与可信链接记录及实际二进制字节的关联核对通过，二者均不证明游戏运行时行为。

当前宿主 Target 均由 UBT 编译 MHT v4 生成的宿主单元。MBT 准备 Hook 编译动作，并通过 UBT 回执检查桌面目标最终二进制中的生成标记。各变体的 `host-link.json` 记录最终文件与回执的哈希。MDT 将生成的 Client 编入共享 UE Runtime。下文的独立归档命令适用于历史生产者输入。[当前生成格式](Protocol/generated-artifacts.schema.json) · [历史 v3 格式](Protocol/generated-artifacts-v3.schema.json)

## 构建和命令

从仓库根目录构建工具：

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModBuildTool
```

也可在已配置的 CMake 构建目录中直接构建 `ModBuildTool` target。使用具体构建目录中的程序按以下方式调用：

```text
ModBuildTool build-ue-target --engine <引擎目录> --project <工程.uproject> --target <目标>
                             --platform <平台> --configuration <配置>
                               --architecture <x64|arm64> --mode <normal|rebuild>
                               --commit <sha> --engine-commit <sha> --metadata <bundle>
ModBuildTool plan --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json
ModBuildTool build-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json
ModBuildTool build-planned-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json --input-fingerprint <plan.inputFingerprint>
ModBuildTool verify-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json
ModBuildTool finalize-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json --link-receipt Target.link.json
ModBuildTool finalize-ue-target --contexts TargetContexts.json --ue-target-receipt Target.target
ModBuildTool refresh-ue-watches --contexts TargetContexts.json
ModBuildTool hash-context --file HostCompileContext.json
ModBuildTool hash-file --file <path>
ModBuildTool inspect-archive --file <archive> --platform Win64 --architecture x64 --anchor <symbol>
ModBuildTool inspect-binary --file <image> --platform Win64 --architecture x64 --kind Executable
```

`prepare-metadata` 使用原生 Development Editor 导出 Bundle。调用 `validate-metadata` 可独立核对目标输入。`build-ue-target` 先验证 Bundle，再导出 UBT 动作，核对 Hook 源码和生成副本的哈希，替换编译输入，并执行链接与回执验证。`rebuild` 会传递 UBT 重建请求。`test-ue-target` 对正常 Editor 的链接与自动化报告进行检查。[命令示例](../../../../../Docs/DevelopmentWorkflow.zh-CN.md#本地构建)

`plan` 仅校验输入，不执行编译也不创建目录，并返回当前输入指纹、所属模块、目标、宿主库位置及缓存状态。`build-host` 获取输出锁后检查缓存，命中时返回 `UpToDate`，否则独立编译并归档后返回 `Built`。`build-planned-host` 还会在持锁期间校验调用方传入的计划指纹。`verify-host` 在缓存或产物过期时返回非零退出码。退出码 `0` 表示成功，`1` 表示参数、输入、编译、I/O 或验证失败。JSON 结果写入 stdout，工具链诊断写入 stderr。

## 宿主编译上下文

正式结构见 [host-context.schema.json](Protocol/host-context.schema.json)。UHT 适配层从实际 UBT 编译数据库中转交所属模块的真实 MSVC 配方，并核对生产者定义。其他接入同样必须提供真实的所属模块上下文，不能从系统默认编译器或手写近似 UE 环境推测。

必需字段：

| 字段 | 契约 |
| --- | --- |
| `format` / `schemaVersion` | `UMF.ModHostContext` / `2` |
| `ownerModule` | 真实的声明所属模块；编译单元必须与其一致 |
| `moduleAPIMode` | `Producer`，保留所属模块的生产者宏定义 |
| `registrationAnchor` | 未装饰的 C 标识符；必须写入静态库的符号索引 |
| `owningBinary` | 可选的真实所属二进制 `id`、`path`、`kind`；`finalize-host` 必需 |
| `target` | `name`、`type`、`configuration`、`linkType`、`platform`、`architecture` |
| `workingDirectory` | 实际的工具链工作目录 |
| `outputLibrary` | 位于生成根之外的 `.lib` 或 `.a`；相对路径基于上下文所在目录解析 |
| `compiler` / `archiver` | 实际工具路径、参数数组及响应文件格式 |
| `environment` | 覆盖继承环境的字符串映射；有效环境摘要参与缓存计算，不输出环境值 |
| `dependencies` | 编译配方依赖、强制包含、输入清单及其他工具链相关文件 |
| `lto` / `pch` / `unity` | 当前版本均须显式设为 `false` |

编译器参数数组必须包含 `{source}`、`{object}` 和 `{dependencies}`，分别展开为本次真实路径。归档器参数必须包含 `{output}`，并以一个独立的 `{objects}` 数组元素展开当前全部对象路径。参数通过响应文件直接传递给工具，不经过 shell。

MSVC 编译器配方使用 `dependencyFormat: "msvc-json"`，并显式添加 `/sourceDependencies` 与 `{dependencies}`；GNU/Clang 使用 `dependencyFormat: "make"`，例如通过 `-MD -MF {dependencies}` 输出完整依赖。适配层须保证系统头文件等真实编译依赖均被记录。

`responseFormat: "msvc"` 使用带 BOM 的 UTF-16LE 与 Windows 参数引用规则（选项及其独立值保留在同一行），仅在 Windows 宿主支持；`"gnu"` 使用 UTF-8 与 GNU 响应文件引用规则。工具路径必须指向真实可执行文件，不支持批处理或 shell 配方。

以下参数模板片段仅用于说明占位语法，不能代替实际的 UE 编译配方：

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

新生产者使用 [generated-artifacts.schema.json](Protocol/generated-artifacts.schema.json)，格式为 `UMF.GeneratedArtifacts`、版本 `3`、状态 `Complete`，且 `unresolvedTypeCount` 为零。MBT 仍兼容使用旧哈希字段名的 [v1](Protocol/generated-artifacts-v1.schema.json) 和 v2 清单。最终链接核对要求 v2 或 v3；`Bootstrap` 与扫描报告均会被拒绝构建。

v2 和 v3 的 `hostBuildContexts` 为每个编译所属模块绑定上下文哈希（可通过 `hash-context` 计算已解析 JSON 的紧凑排序摘要），v1 的单上下文回退字段为 `buildContextHash`。MBT 会重验 v2 和 v3 中每项 `inputFiles` 的内容哈希并重新枚举 `watchedDirectories`；输入过期时，`GeneratedInputsStale` 会指出需要重新运行 MHT 的路径。

`environmentMode: Explicit` 仅将声明的 `environment` 作为编译子进程环境；默认的 `Inherit` 模式则将这些值覆盖到继承环境中。计划和构建回执均会提供有效的 `environmentHash`。

`generationId`、`inputHash`、`contractHash`、`projectionHash` 和 `nativeABIHash` 均为小写 SHA-256 摘要。契约、投影和 ABI 摘要由生成器提供，MBT 不重新解析 C++ 类型，也不证明这些语义摘要的正确性。

`generatedFiles` 列出相对于生成清单目录的路径、`contentHash` 与 `ownerModule`（共享契约文件的所属模块可为 `null`）。路径须唯一、不含 `..`，且解析符号链接后不得离开生成根目录；文件内容须逐项匹配摘要。

`compilationUnits` 显式列出 `ownerModule` 和 `source`。MBT 仅编译当前所属模块的 `.cpp`、`.cc` 或 `.cxx` 单元；这些文件必须已列入 `generatedFiles`，MBT 不会通过递归扫描猜测。空导出模块也必须由生成器提供包含稳定注册锚点的单元，以避免误用删除导出前的旧库。

## 缓存、发布与库检查

v2 和 v3 的每个生成文件都必须指定 `role`：`HostBinding`、`HostRegistration`、`CompileVerification`、`HostHeader`、`SdkHeader`、`Contract`、`SdkResource` 或私有 `Diagnostic`。`plan`、`build-host` 和 `finalize-host` 返回的 `publicArtifacts` 仅列出当前模块及共享的 `SdkHeader`、`Contract`、`SdkResource`，包含绝对 `sourcePath`、规范化 `relativePath`、内容哈希、角色和所属模块。SDK 头文件仅接受 `.h` / `.hpp`，契约仅接受 `.json`；宿主编译单元不能同时作为公共产物发布。v1 中未指定或未知的角色均保留为私有，不进入公共清单。

`requiredSymbols` 为 `{ "ownerModule": "...", "symbol": "..." }` 记录数组，可选附带 `exportId`、`signatureHash` 和 `projectionHash`。符号必须使用未装饰的 C 标识符；重复的模块/符号对及同一所属模块内的重复 ExportId 会被拒绝，但 Host 和 Client 所属模块可为相同逻辑 ExportId 各自记录入口。MBT 会并入上下文中的稳定注册锚点，对当前模块的完整必需符号集合执行一次归档检查（macOS 检查时会加上平台 C 符号前缀）。零导出批次可使用空数组，但注册锚点仍必需。v1 清单可省略该数组，此时保留原有的单锚点检查。

输入指纹覆盖 MBT 可执行文件、工具版本、原始上下文和生成清单、全部生成文件、编译器及归档器、显式依赖、上次成功编译报告的传递依赖，以及有效子进程环境。源码或既有头文件的变更会触发重新编译，并借此发现和登记新增的完整 include 依赖。

输出旁的 `<library>.mbt.json` 是提交凭证，记录输入指纹、目标、生成批次、上下文摘要、库哈希、注册锚点和编译器依赖。缓存复用必须同时满足输入指纹匹配、文件摘要一致和归档检查通过；缺失、损坏或版本不符的凭证绝不会成为有效缓存。

编译过程持有独占 OS 锁并使用本次进程专属的暂存目录（由本次进程创建并清理）。每次缓存失效都会重新编译该模块的生成单元，并从完整对象列表创建全新归档，避免已删除的归档成员残留。校验通过后先替换库文件，再原子提交凭证，确保中断导致的不一致无法通过后续验证。

归档检查会核对常规归档容器、目标架构及索引中的全部必需符号，并在缺失时报告完整的缺失集合。支持 Win64 COFF、Linux ELF 和 macOS Mach-O 格式，覆盖 `x64` 和 `arm64` 架构。导入库成员、薄归档、未知对象格式和不匹配的架构均会被拒绝。LTO、PCH、Unity 与 C++ modules/header units 不在当前支持范围内。各平台支持均需在对应平台上实际编译与验证；当前的 Windows 验证结果不代表 Linux 或 macOS 已验收。

## 获取角色与增加生命的清单片段

MHT 输出 v3 清单，包含宿主桥、C ABI 入口、校验与 UE 视图实现四个宿主单元，以及归属 `<game>ModClient` 的一个独立客户端单元；对应的上下文锚点分别为 `UMF_ModHost_<owner>` 和 `UMF_ModClient_<game>`。具体的公共头文件、Host/Client 符号记录及 MDT 契约参见 [MHT ABI 协议](../ModHeaderTool/README.zh-CN.md)。

这两个 API 只是普通清单数据，并未硬编码在 MBT 中。以下简化 v3 片段展示了 `ExportTest` 的记录方式（实际 MHT 符号名从生成描述中读取，MBT 会自动并入上下文的注册锚点）：

```json
{
  "requiredSymbols": [
    { "ownerModule": "ExportTest", "symbol": "UMF_UnrealModFrameworkDev_ExportTest_A_b88dc4c71053a716_Host", "exportId": "api/game.local_player_api/getlocalcharacter" },
    { "ownerModule": "ExportTest", "symbol": "UMF_UnrealModFrameworkDev_ExportTest_A_b846156f4c92057a_Host", "exportId": "api/game.character/addhealth" }
  ]
}
```

MHT 将 `SDK/Public/<game>.modabi.h` 标记为 `SdkHeader`，将生成的 JSON 契约标记为 `Contract`，并将实现作为私有编译单元。Host 与 Client 记录为每个 ExportId 绑定匹配的签名和投影哈希，内部 UE 视图头则使用私有的 `HostHeader` 角色。以上仅为片段，完整输入仍须列出所有内容哈希与编译单元，不可直接运行。

构建结果会一并返回这两个入口、注册锚点、公共文件清单和 `OwnerBinaryOnly` 链接要求。MDT 根据公共清单复制文件并再次核对哈希；MBT 既不发布宿主私有源码，也不构建最终的开发者插件。

## 最终链接核对

`finalize-host` 消费 [host-link-receipt.schema.json](Protocol/host-link-receipt.schema.json)，输出 [linked-artifacts.schema.json](Protocol/linked-artifacts.schema.json)。在 UE 5.6 Win64 x64 适配层中，PostBuildSteps 会调用 `finalize-ue-target`，由 MBT 核对 UBT 目标回执和本次归档的唯一 PE 见证符号，并在宿主库旁写出链接记录与关联清单。其他链接适配层也可以向 `finalize-host` 提供可信链接记录。

上下文须先声明 `owningBinary`，其 `id` 为 UBT 中稳定的二进制身份，`path` 相对上下文目录解析，`kind` 为 `Executable` 或 `DynamicLibrary`，从而明确区分 Editor 模块 DLL 与 monolithic 游戏可执行文件。

链接记录采用 `UMF.ModHostLinkReceipt` v1 格式，包含以下字段：

| 字段 | 要求 |
| --- | --- |
| `status` | `Succeeded` |
| `target` | 与编译上下文中的 `target` 对象完全一致 |
| `binary` | 所属二进制的 `id`、`path`、`kind`、实际 `contentHash` 与唯一的 `ownerModules` 列表 |
| `hostLibraries` | 按所属模块列出实际参与最终链接的宿主库，同一模块不得重复 |
| 每个宿主库记录 | `ownerModule`、`path`、`contentHash`、`hostReceiptHash`、`contextHash`、`generationId`、`registrationAnchor`、`anchorRetained: true` |

记录中的路径均相对于链接记录所在目录解析。MBT 要求当前宿主构建仍然有效，并逐项核对所属二进制身份、模块归属、宿主库路径与哈希、构建凭证哈希、上下文与批次身份、锚点保留声明以及实际二进制哈希。同时还会检查实际二进制镜像的 PE、ELF 或薄 Mach-O 类型与架构（ELF 区分 PIE 可执行文件与共享库，暂不支持 fat Mach-O 镜像）。核对期间若输入发生变化，则直接拒绝。

核对成功后输出 `UMF.ModHostLinkedArtifacts` / `Linked`，包含公开文件、契约摘要、必需符号、实际二进制与库哈希，以及宿主构建凭证和链接记录的路径与哈希，供 MDT 进一步汇总。链接成功与锚点保留的真实性依赖可信适配层；MBT 仅核对关联关系与文件字节，不从任意 JSON 或二进制字符串推断链接成功，也不将 `Linked` 等同于运行时绑定就绪。

## UE 5.6 集成

MBT 驱动 UBT 使用生成的自动 Hook 源码副本，并依据目标回执核对桌面原生二进制。宿主与编译校验单元由 UBT 在所属模块编译。

UBT 为导出器提供目标图和编译动作。在运行 MBT 前准备原生工具，嵌套 UBT 调用复用对应可执行文件。

以下更完整的动作图接入仍是后续契约：

1. 创建 Makefile 时，从真实模块图生成各模块计划与所属二进制映射，在标准 UHT 后获取可用的生成头及真实编译上下文，再调用 MHT 执行完整生成。
2. 复用缓存 Makefile 时，也必须在 UHT 检查后、动作失效判断前检查源码集合与依赖；普通非反射头文件的新增、删除和改动不能仅依赖 UHT 重跑。
3. 调用 `plan`，将当前 `inputFingerprint` 写入 MBT 动作的有效命令或 `CommandVersion`；当 `upToDate=false` 时须明确使该动作失效，包括输入未变但库已损坏的情况。
4. 动作执行 `build-planned-host --input-fingerprint ...`，在 `ProducedItems` 中声明库和凭证，设置 `bProducesImportLibrary=false`，首版禁止远程和 UBA 执行。
5. 在真实所属 `UEBuildBinary` 的私有最终链接环境中加入宿主库和注册锚点的保留要求，避免沿通用 Runtime 公共依赖传播；同时将库作为最终链接动作的 `PrerequisiteItems`，形成明确的生产依赖。
6. 为每个参与模块预留稳定的库路径与动作：无导出时生成空注册单元，首次新增导出与删除最后一项导出均无需改变动作图结构。变体按引擎、工具链、Target、配置、平台和架构隔离。
7. 针对 Windows、Linux 和 macOS 分别在最终链接阶段实现符号保留。不得将宿主静态库标记为导入库，或错误加入仅生成 UE 导入库的动作中。模块输出、Clean、receipt 与插件裁剪也须包含宿主产物。
8. 最终链接成功后，导出当前目标的可信链接记录并调用 `finalize-host`。按模块汇总全部必需的关联结果后，MDT 即可组织完整的 SDK 发布。

在这份 UE 5.6 源码中，`GenerateHeaderFuncs` 仅在 Makefile 重建路径位于 UHT 之后执行，在缓存复用路径不会重新运行，因此不能作为唯一触发点。现有的 `.ubtplugin.csproj` UHT Exporter 入口也不等同于完整的 UBT 动作图扩展。

导出快照需要工程二进制时，生成适配层可引导配置中的 `metadataEditorTarget`。手动准备 Editor 时，先执行 Development Editor 引导构建，再执行正式构建。Live Coding、Hot Reload、预编译分发和远程 Mac 均需要单独适配。
