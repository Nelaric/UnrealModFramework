# Copyright (c) 2026 Nelaric Contributors
"""Prepare only the x86-64 cooker runtime on an ARM64 Linux hosted runner."""
import os
from pathlib import Path
import platform
import subprocess

# The cooker uses headless Slate, SDL and graphics loader dependencies even with NullRHI.
PACKAGES = ("libstdc++6", "libasound2", "libdbus-1-3", "libgl1", "libegl1", "libvulkan1",
            "libx11-6", "libxcursor1", "libxi6", "libxinerama1", "libxrandr2", "libxss1",
            "libxkbcommon0", "libnss3", "libpulse0", "libsm6", "libfontconfig1", "zlib1g")


def prepare():
    if (os.environ.get("RUNNER_ENVIRONMENT") != "github-hosted" or platform.system() != "Linux"
            or platform.machine() != "aarch64"):
        raise ValueError("This cooker setup requires a native ARM64 Linux hosted runner")
    temp = Path(os.environ["RUNNER_TEMP"])
    base = temp / "ue-volume" if (temp / "ue-volume").is_dir() else temp
    root = base / "ue-cooker-amd64"
    subprocess.run(["sudo", "apt-get", "-qq", "update"], check=True)
    subprocess.run(["sudo", "apt-get", "-qy", "--no-install-recommends", "install",
                    "qemu-user-static", "binfmt-support", "debootstrap"], check=True)
    subprocess.run(["sudo", "update-binfmts", "--enable", "qemu-x86_64"], check=True)
    subprocess.run(["sudo", "debootstrap", "--download-only", "--arch=amd64", "--variant=minbase",
                    "--include="+",".join(PACKAGES), "jammy", str(root), "https://archive.ubuntu.com/ubuntu/"], check=True)
    archives = sorted((root / "var/cache/apt/archives").glob("*.deb"))
    if not archives:
        raise ValueError("Ubuntu did not download the x64 runtime packages")
    for package in archives:
        subprocess.run(["sudo", "dpkg-deb", "--extract", str(package), str(root)], check=True)
    subprocess.run(["sudo", "chroot", str(root), "/sbin/ldconfig"], check=True)
    for library in ("libX11.so.6", "libasound.so.2", "libvulkan.so.1", "libnss3.so"):
        if not any((root / where / "x86_64-linux-gnu" / library).is_file() for where in ("usr/lib", "lib")):
            raise ValueError("Missing x64 cooker runtime library " + library)
    # QEMU follows the guest interpreter symlink from the host before it can load
    # the guest. Make Ubuntu's absolute /lib64 link relative to this runtime.
    interpreter = root / "lib64/ld-linux-x86-64.so.2"
    loader = root / "lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"
    if not loader.is_file() or not loader.resolve().is_relative_to(root.resolve()):
        raise ValueError("Missing the amd64 dynamic loader inside the cooker runtime")
    parent = interpreter.parent.resolve(strict=True)
    if not parent.is_relative_to(root.resolve()):
        raise ValueError("The amd64 interpreter directory escaped the cooker runtime")
    subprocess.run(["sudo", "ln", "-sfn", os.path.relpath(loader.resolve(), parent), str(interpreter)], check=True)
    environment = dict(os.environ, QEMU_LD_PREFIX=str(root))
    test_program = next(root / name for name in ("bin/true", "usr/bin/true") if (root / name).is_file())
    with test_program.open("rb") as stream:
        header = stream.read(20)
    if header[:6] != b"\x7fELF\x02\x01" or header[18:20] != b"\x3e\x00":
        raise ValueError("Expected an amd64 helper")
    subprocess.run([test_program], env=environment, check=True)
    with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as output:
        output.write(f"QEMU_LD_PREFIX={root}\n")
    print("Verified x86-64 host-tool execution on the native AArch64 runner", flush=True)


if __name__ == "__main__":
    prepare()
