<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# Packaged Game Regression for the Shared SDK Runtime

This end-to-end regression verifies that two independently authored and packaged mods—`TestMod` and `SharedCaller`—can mount side by side in a staged Win64 x64 modular Development Game, call into the same shared `UMFDevSDKRuntime` DLL with isolated caller contexts, and cleanly revoke one mod's access upon unload without disrupting the other.

Running the full suite requires a UE 5.6 build capable of compiling and staging a modular Win64 Development Game with Pak containers.

## 1. Prepare the host and author workspaces

1. **Publish the SDK bundle:** In the source repository, build `UnrealModFrameworkDevModular` and `UnrealModFrameworkDevEditor`. Reference both v4 generated manifests in an MDT v3 build request and run `plan`, `build`, and `inspect` (see [Build a sample SDK](../../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.md#build-a-sample-sdk)). The published bundle provides `Host/Plugins/UMFDevSDK` for the game and two plugins under `Plugins/` for mod authors.
2. **Generate isolated project copies:** Run `prepare_projects.py` to create three clean workspaces under `<fresh-workspace>`: `H` (the host game wired with the regression test module), `A` (`TestMod`), and `B` (`SharedCaller`):

   ```text
   python Test/SharedRuntime/prepare_projects.py --work <fresh-workspace> --sdk <published-SDK>
   ```

   Point `UMF_MHT_EXECUTABLE`, `UMF_MBT_EXECUTABLE`, `UMF_MOD_SOURCE_TOOL_EXECUTABLE`, `UMF_MHT_LIBCLANG`, and `UMF_MHT_CLANG_RESOURCE_DIR` at your local tool binaries and Clang resource headers before building.
3. **Build, cook, and stage the host (`H`):** Use MBT to build `UnrealModFrameworkDevEditor` and `UnrealModFrameworkDevModular` inside `H`, then run UAT `BuildCookRun` to cook and stage a Pak base release containing `/Game/Levels/Basic/L_Basic` and `/Engine/Maps/Entry`. Keep three artifacts from this final Game build for verification:
   - The UBT `.target` receipt
   - The v4 generated manifest (`*.generated-artifacts.json`)
   - `Intermediate/ModTools/ModHeaderTool/HostTargets/UnrealModFrameworkDevModular/Win64/Development/x64/host-link.json`

   *(If you use prebuilt modular engine binaries, make sure their runtime DLL dependencies are included in the staged directory.)*
4. **Build and package both mods (`A` and `B`):** Compile the Editor target for both author projects. `TestMod` (`A`) already contains its lifecycle Blueprint; in `SharedCaller` (`B`), generate its entry-point Blueprint first:

   ```text
   UnrealEditor-Cmd <SharedCaller.uproject> -run=pythonscript -script=<absolute path to SharedCaller/create_entry_point.py> -unattended
   ```

   Finally, run the headless `ModPackage` commandlet in `A` and `B` against the same host base release and Game `.target` receipt, using a fresh task directory for each package (see [Build behavior](../../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.md#build-behavior)).

## 2. Verify the staged game

Run `verify_game.py` against the clean staged game directory using the final host manifest, link record, and receipt from `H`:

```text
python Test/SharedRuntime/verify_game.py --sdk <SDK> --stage <Stage/Windows> --receipt <Game.target> --link <host-link.json> --host-project <H/UnrealModFrameworkDev.uproject> --host-manifest <final-game.generated-artifacts.json> --registry <Releases/Version/Windows/AssetRegistry.bin> --test-mod <TestMod.mod> --other-mod <SharedCaller.mod> --report <report-directory>
```

The verifier cross-checks the staged binaries against `host-link.json` and the published SDK, inspects the native module payloads inside both `.mod` archives, installs both packages into the staged game, deletes `TestMod.mod` mid-run to trigger hot unload, and cleans up both test packages before exiting. Detailed outputs are written to `verification.json`, `game.log`, and `console.log` inside `<report-directory>`.

### What the runtime assertion checks

1. On startup, the regression `GameMode` connects the shared host ABI and issues separate caller tokens to `TestMod` and `SharedCaller`.
2. `TestMod` applies its packaged Manny skeletal mesh to the exported player character, while `SharedCaller` calls the shared `UMFDevSDKRuntime` to read current health (`73`).
3. When `TestMod.mod` is removed, the loader unmounts `TestMod` and revokes its caller token.
4. Afterward, `SharedCaller` must still read updated health (`41`) using its existing `UModInstance`, caller context, and shared `UMFDevSDKRuntime`, whereas any call using `TestMod`'s revoked context must fail validation.

*(Note: While an active World extension session can hold a package lease until the World closes, this regression exercises the standard ModInstance lifecycle path to verify container unmounting, unload callbacks, and per-caller token revocation. On Win64, loaded native UE module DLLs remain mapped in memory until process exit.)*
