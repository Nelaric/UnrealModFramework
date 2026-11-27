<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# 组织文档托管

Nelaric 组织下的项目文档统一托管在 `https://docs.nelaric.com/<仓库名>/<栏目>/`。本项目的 API 站点位于 `https://docs.nelaric.com/UnrealModFramework/API/`（路径中的 `API` 区分大小写），生成的 HTML 页面与静态资源一并部署在该目录中。

## 日常自动发布流程

本仓库以 GitHub Actions 作为 Pages 发布源（见 [pages.yml](../../.github/workflows/pages.yml)）。设置仓库变量 `API_PAGES_ENABLED=true` 后，向 `main` 推送提交或手动触发工作流都会构建并更新站点：

1. **构建阶段**：下载锁定版本的 Doxygen 压缩包（遇临时网络异常会自动重试并回退备用镜像），解压前核验 SHA-256 摘要，生成静态页面后上传按当前运行尝试次数命名的 Pages 产物。
2. **部署阶段**：先按产物 ID 检查可见性（若首次查询未就绪，等待 15 秒重试一次），确认可见后按产物名称发布上线。

## 本地预览

安装 `pages.yml` 中锁定版本的 Doxygen，在仓库根目录执行 `python Scripts/run_doxygen.py`，然后用浏览器打开 `.tools/api-docs/html/index.html`，即可检查 [API 入口页](../API/README.zh-CN.md)、编码规范与公开头文件生成的参考页面。本地构建采用严格模式，遇到任何文档警告均会直接报错退出。

## GitHub Pages 域名结构

自定义域名 `docs.nelaric.com` 绑定在组织级站点仓库 `Nelaric/nelaric.github.io` 上。组织下的各个项目仓库会自动继承这一域名，并挂载在各自的 `/<仓库名>/` 子路径下，因此 `Nelaric/UnrealModFramework` 自身的自定义域名配置应保持留空。

打包站点时，工作流会把完整的 Doxygen 输出放入 Pages 产物的 `API/` 子目录，并在项目根路径放一个跳转到 `API/` 的轻量入口页。这样访问项目根地址（`https://docs.nelaric.com/UnrealModFramework/`）时会自动进入 `API/`，不过旧版直接指向根路径下具体 HTML 文件的深层链接不会再重定向。

## 首次开通步骤

1. 准备公开的组织站点仓库 `Nelaric/nelaric.github.io`（仅在尚未创建时新建）。若是从零搭建，将 `Setup/OrganizationPages/` 下的全部文件（含 `.nojekyll`）复制到该仓库根目录；若组织站点已在运行，请在不覆盖其他项目入口的前提下合并导航。注意：按 GitHub 要求，`CNAME` 文件只能包含纯域名文本，不能添加版权注释。
2. 在 `Nelaric/nelaric.github.io` 中启用 GitHub Pages，发布源选默认分支根目录，并在添加 DNS 解析记录**之前**先将自定义域名填为 `docs.nelaric.com`。仓库提供的模板首页同时收录了 UnrealModFramework 与 Nelaric Unreal Gameplay 的文档入口。
3. 前往 `nelaric.com` 的权威 DNS 服务商控制台添加下方 `CNAME` 记录（例如托管在阿里云时，在“云解析 DNS → 公网权威解析”中添加；注意域名注册商与实际权威 DNS 服务商未必是同一家）。
4. 在 `Nelaric/UnrealModFramework` 仓库中，将 Pages 发布源设为 **GitHub Actions**，自定义域名保持为空；随后新增仓库 Actions 变量 `API_PAGES_ENABLED=true`，并在 `main` 分支运行一次 API Pages 工作流。
5. 等待 DNS 生效且 TLS 证书签发完毕后，在组织站点勾选 **Enforce HTTPS**，最后通过 HTTPS 依次检查组织首页、项目根路径跳转、API 首页、具体头文件/类文档页以及样式与脚本资源是否加载正常。

| DNS 字段 | 配置值 |
| --- | --- |
| 域名（Zone） | `nelaric.com` |
| 记录类型 | `CNAME` |
| 主机记录 | `docs` |
| 记录值 | `nelaric.github.io` |
| 解析请求来源 | 默认 |
| TTL | 600 秒（或服务商默认值） |

记录值只需填写主机名 `nelaric.github.io`，后续的仓库名与栏目子路径由 GitHub Pages 自动路由。今后新增其他项目文档站点时，沿用同一方式发布各自的子目录即可自动继承组织域名。

## 参考资料

- [GitHub 文档：跨多个仓库共用自定义域名](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/about-custom-domains-and-github-pages#using-a-custom-domain-across-multiple-repositories)
- [GitHub 文档：管理 GitHub Pages 自定义域名](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/managing-a-custom-domain-for-your-github-pages-site)
- [阿里云帮助中心：添加公网权威 DNS 解析记录](https://help.aliyun.com/zh/dns/pubz-add-parsing-record)
