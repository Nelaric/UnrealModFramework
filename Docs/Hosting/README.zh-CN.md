<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# 组织文档托管

组织文档统一托管在 `https://docs.nelaric.com/<仓库名>/<栏目>/`。当前项目的 API 站点地址为 `https://docs.nelaric.com/UnrealModFramework/API/`（其中 `API` 区分大小写），生成的页面与资源统一存放在该目录中。

## 已启用的项目发布

本仓库以 GitHub Actions 作为 Pages 发布源。设置 `API_PAGES_ENABLED=true` 后，推送到 `main` 或手动触发工作流均可构建并发布 API 站点。构建 Job 下载锁定版本的 Doxygen（遇到临时网络故障时会自动重试并切换备用源），在解压前校验 SHA-256 摘要，并上传按当前运行尝试次数命名的 Pages 产物。部署 Job 先按产物 ID 检查可见性（首次检查失败后等待 15 秒重试一次），再按产物名称完成部署。[API Pages 工作流](../../.github/workflows/pages.yml)

## GitHub Pages 结构

组织站点仓库 `Nelaric/nelaric.github.io` 绑定了自定义域名 `docs.nelaric.com`。各项目仓库会自动继承该域名并保留各自的仓库名路径，因此 `Nelaric/UnrealModFramework` 的自定义域名应保持留空。

本项目的 API Pages 工作流使用 Doxygen 构建 API 入口页、当前公开头文件与编码规范，将完整产物复制到 Pages 发布包的 `API/` 目录中，并在项目根目录添加指向 `API/` 的跳转页。因此，迁移后原有的 GitHub Pages 项目根地址仍可作为访问入口，但原根目录下单个 HTML 页面的旧深层链接不会由该入口页重定向。

## 首次配置

1. 使用公开的组织站点仓库 `Nelaric/nelaric.github.io`（仅在尚不存在时创建）。首次配置时，将 `Setup/OrganizationPages/` 中的全部内容（包括 `.nojekyll`）复制到仓库根目录；若为已有站点新增本项目，请保留现有的组织索引与其他项目路由。这些文件属于部署模板，其中 `CNAME` 文件必须仅包含纯域名，不得添加版权注释（这是 GitHub 的要求）。
2. 在组织站点启用 GitHub Pages，发布源选择默认分支的根目录，并在添加 DNS 记录前将自定义域名设为 `docs.nelaric.com`。模板提供的根页面同时索引了 UnrealModFramework 与 Nelaric Unreal Gameplay 两套文档。
3. 在 `nelaric.com` 的权威 DNS 服务商处添加下表所示记录。若 DNS 托管在阿里云，请在云解析 DNS 的公网权威解析控制台中配置。请注意，域名注册商与权威 DNS 服务商可能不同。
4. 将本项目的 Pages 发布源设为 GitHub Actions，并保持自定义域名留空。设置仓库 Actions 变量 `API_PAGES_ENABLED=true` 后，从 `main` 分支运行 API Pages 工作流，以执行构建与部署 Job。
5. 待 DNS 校验与证书签发完成后，在组织站点启用 Enforce HTTPS，并通过 HTTPS 逐一验证组织根地址、项目根地址、API 首页、单个 API 页面及其样式与脚本资源。

| DNS 字段 | 值 |
| --- | --- |
| 域名 | `nelaric.com` |
| 记录类型 | `CNAME` |
| 主机记录 | `docs` |
| 记录值 | `nelaric.github.io` |
| 解析请求来源 | 默认 |
| TTL | 600 秒，或服务商默认值 |

DNS 记录值仅需填写主机名，仓库名与 API 路径由 GitHub Pages 自动提供。后续新增的项目站点也应发布各自的栏目目录，并继承该组织域名。

## 参考资料

- [GitHub：跨仓库使用自定义域名](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/about-custom-domains-and-github-pages#using-a-custom-domain-across-multiple-repositories)
- [GitHub：管理自定义域名](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/managing-a-custom-domain-for-your-github-pages-site)
- [阿里云：添加公网 DNS 解析记录](https://help.aliyun.com/zh/dns/pubz-add-parsing-record)
