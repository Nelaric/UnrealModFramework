<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# Organization documentation hosting

Organization documentation is hosted at `https://docs.nelaric.com/<repository>/<section>/`. The API site for this project is served at `https://docs.nelaric.com/UnrealModFramework/API/` (`API` is case-sensitive), with generated pages and assets stored together in that directory.

## Active project publishing

This repository uses GitHub Actions as its Pages source. When `API_PAGES_ENABLED=true`, pushes to `main` and manual workflow runs build and publish the API site. The build job downloads the pinned Doxygen archive (retrying transient failures and falling back to a mirror if needed), verifies its SHA-256 digest before extraction, and uploads a Pages artifact named after the current run attempt. The deployment job checks artifact visibility by ID—retrying once after 15 seconds if the initial check fails—and then deploys by artifact name. [API Pages workflow](../../.github/workflows/pages.yml)

## GitHub Pages topology

The organization site repository `Nelaric/nelaric.github.io` owns the custom domain `docs.nelaric.com`. Project repositories inherit this domain while retaining their repository-name path, so leave the custom domain of `Nelaric/UnrealModFramework` unset.

The project's API Pages workflow builds the API landing pages, current public headers, and coding standards with Doxygen, copies the complete output into `API/` inside the Pages artifact, and adds a project-root redirect to `API/`. As a result, the existing GitHub Pages project-root URL continues to work as an entry point after migration, though direct links to individual HTML files at the former root are not redirected by this entry page.

## Initial setup

1. Use the public organization site repository `Nelaric/nelaric.github.io` (create it only if it does not already exist). For initial setup, copy the contents of `Setup/OrganizationPages/`—including `.nojekyll`—to its root directory. When adding this project to an established site, preserve the existing organization index and other project routes. These files serve as a deployment template; the `CNAME` file must contain only the plain domain name without a copyright comment, as required by GitHub.
2. Publish the organization site with GitHub Pages from the root directory of its default branch, and set its custom domain to `docs.nelaric.com` before adding the DNS record. The provided root page indexes both UnrealModFramework and Nelaric Unreal Gameplay documentation.
3. Add the record below in the authoritative DNS provider for `nelaric.com`. If Alibaba Cloud hosts the DNS zone, configure it in its public DNS resolution console. Note that the domain registrar and the authoritative DNS provider may differ.
4. Set this project's Pages publishing source to GitHub Actions and leave its custom domain unset. Set the repository Actions variable `API_PAGES_ENABLED=true` and run the API Pages workflow from `main` to enable the build and deployment jobs.
5. Once DNS validation and certificate issuance complete, enable Enforce HTTPS for the organization site. Verify the organization root, project root, API index, an individual API page, and its styles and scripts over HTTPS.

| DNS field | Value |
| --- | --- |
| Zone | `nelaric.com` |
| Type | `CNAME` |
| Host record | `docs` |
| Value | `nelaric.github.io` |
| Request source | Default |
| TTL | 600 seconds, or the provider's default |

Enter only the host name as the DNS value; GitHub Pages supplies the repository and API paths. Future project sites should publish their own section directories while inheriting the organization's domain.

## References

- [GitHub: using a custom domain across multiple repositories](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/about-custom-domains-and-github-pages#using-a-custom-domain-across-multiple-repositories)
- [GitHub: managing a custom domain](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/managing-a-custom-domain-for-your-github-pages-site)
- [Alibaba Cloud: adding public DNS records](https://help.aliyun.com/zh/dns/pubz-add-parsing-record)
