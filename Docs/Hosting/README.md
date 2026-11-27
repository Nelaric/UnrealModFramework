<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# Organization Documentation Hosting

Nelaric project documentation is served under `https://docs.nelaric.com/<repository>/<section>/`. For UnrealModFramework, the API reference lives at `https://docs.nelaric.com/UnrealModFramework/API/` (`API` is case-sensitive), where all generated HTML pages and static assets are deployed together.

## Active publishing workflow

This repository publishes its Pages site through GitHub Actions ([pages.yml](../../.github/workflows/pages.yml)). Once `API_PAGES_ENABLED=true` is set, pushes to `main` and manual workflow dispatches build and deploy the site:

1. **Build job:** Downloads the pinned Doxygen archive (retrying transient network errors and falling back to a mirror if needed), verifies its SHA-256 digest before extraction, builds the site, and uploads a Pages artifact named after the current run attempt.
2. **Deploy job:** Confirms artifact visibility by ID—waiting 15 seconds for one retry if needed—before deploying the site by artifact name.

## Preview locally

Install the Doxygen version pinned in `pages.yml` and run `python Scripts/run_doxygen.py` from the repository root, then open `.tools/api-docs/html/index.html` to inspect the [API entry page](../API/README.md), coding standards, and generated C++ reference. Local builds run in strict mode and fail on any documentation warning.

## GitHub Pages domain topology

The organization site repository `Nelaric/nelaric.github.io` owns the custom domain `docs.nelaric.com`. Individual project repositories inherit that custom domain automatically under their `/<repository>/` subpath, so leave the custom domain setting on `Nelaric/UnrealModFramework` blank.

During packaging, the workflow places the complete Doxygen HTML output under `API/` inside the Pages artifact and writes a lightweight redirect page at the project root pointing to `API/`. That keeps the root project URL (`https://docs.nelaric.com/UnrealModFramework/`) working as a clean entry point, although old deep links to individual HTML files directly under the root are not redirected.

## Initial setup

1. Prepare the public organization repository `Nelaric/nelaric.github.io` (create it only if it does not exist yet). On a brand-new site, copy everything in `Setup/OrganizationPages/`—including `.nojekyll`—to its root directory; on an existing site, merge our route into the existing index without overwriting other projects. Note that GitHub requires `CNAME` to contain only the bare domain name with no copyright comment.
2. Enable GitHub Pages on `Nelaric/nelaric.github.io` from the root of its default branch, and configure `docs.nelaric.com` as its custom domain *before* creating the DNS record. The template landing page indexes both UnrealModFramework and Nelaric Unreal Gameplay.
3. Add the `CNAME` record below at the authoritative DNS provider for `nelaric.com` (for example, in the Alibaba Cloud Public DNS console if Alibaba Cloud hosts the zone—remember that the domain registrar and authoritative DNS host may be different providers).
4. In `Nelaric/UnrealModFramework`, set the GitHub Pages source to **GitHub Actions**, leave its custom domain empty, set the Actions variable `API_PAGES_ENABLED=true`, and trigger the API Pages workflow on `main`.
5. After DNS propagation and TLS certificate provisioning finish, turn on **Enforce HTTPS** on the organization site. Verify the organization root, project root redirect, `API/` landing page, an individual class/header page, and CSS/JS assets over HTTPS.

| DNS field | Value |
| --- | --- |
| Zone | `nelaric.com` |
| Type | `CNAME` |
| Host record | `docs` |
| Value | `nelaric.github.io` |
| Request source | Default |
| TTL | 600 seconds (or provider default) |

Enter only `nelaric.github.io` as the `CNAME` target; GitHub Pages routes `/<repository>/<section>/` paths automatically. Future repositories should follow the same pattern, publishing their own section subdirectories while inheriting the organization domain.

## References

- [GitHub Docs: Using a custom domain across multiple repositories](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/about-custom-domains-and-github-pages#using-a-custom-domain-across-multiple-repositories)
- [GitHub Docs: Managing a custom domain for your GitHub Pages site](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/managing-a-custom-domain-for-your-github-pages-site)
- [Alibaba Cloud: Adding public DNS records](https://help.aliyun.com/zh/dns/pubz-add-parsing-record)
