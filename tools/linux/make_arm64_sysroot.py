#!/usr/bin/env python3
"""futari-linux (2026-11-01): an AArch64 sysroot for cross-compiling on an x86-64 Linux machine.

Downloads Ubuntu 24.04's arm64 packages from its official port mirror (the same distribution, and the same development
packages, as the AArch64 GitHub workflow), with their dependencies, and unpacks them into one folder. Nothing is
installed on the build machine and no package script runs. Release builds made against it need glibc 2.38 or newer,
as the GitHub-built ones do.

    tools/linux/make_arm64_sysroot.py [--out out/sysroot-arm64] [--extra PACKAGE...]
"""
import argparse
import hashlib
import io
import lzma
import os
import re
import subprocess
import sys
import tarfile
import urllib.request

MIRROR = "http://ports.ubuntu.com/ubuntu-ports"
SUITES = ["noble", "noble-updates", "noble-security"]
COMPONENTS = ["main", "universe"]

# The development packages of .github/workflows/build-linux-arm64.yml, and the C/C++ runtime clang links against.
WANTED = """
libc6-dev libstdc++-14-dev libgcc-14-dev linux-libc-dev
libvulkan-dev libasound2-dev libpulse-dev libpipewire-0.3-dev libx11-dev libx11-xcb-dev libxext-dev libxrandr-dev
libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libdrm-dev libgbm-dev
libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev libudev-dev libwayland-dev libdecor-0-dev liburing-dev libgtk-3-dev
""".split()

# Packages never needed in a sysroot (they only matter on a running system).
SKIP = re.compile(r"^(debconf|dpkg|perl|perl-base|perl-modules.*|python3.*|init-system-helpers|adduser|passwd|"
                  r"systemd.*|libsystemd-shared|dbus-.*|ucf|install-info|sensible-utils|tzdata|coreutils|bash|"
                  r"dash|sed|grep|gzip|tar|util-linux|mount|login|base-files|ca-certificates|openssl|"
                  r"debianutils|lsb-base|procps|readline-common|.*-bin|.*-tools|.*-utils)$")


def fetch(url):
    with urllib.request.urlopen(url, timeout=120) as response:
        return response.read()


def read_index():
    """Every package of the suites, the newest suite's version winning (later suites override earlier ones)."""
    packages, provides = {}, {}
    for suite in SUITES:
        for component in COMPONENTS:
            text = lzma.decompress(fetch(f"{MIRROR}/dists/{suite}/{component}/binary-arm64/Packages.xz")).decode()
            for block in text.split("\n\n"):
                fields = dict(re.findall(r"^([A-Za-z0-9-]+): (.*)$", block, re.M))
                if "Package" not in fields:
                    continue
                packages[fields["Package"]] = fields
                for provided in re.split(r",\s*", fields.get("Provides", "")):
                    if provided:
                        provides.setdefault(provided.split()[0], fields["Package"])
    return packages, provides


def resolve(packages, provides):
    """The wanted packages and all their dependencies (the first choice of each alternative)."""
    chosen, todo = set(), list(WANTED)
    while todo:
        name = todo.pop()
        name = name if name in packages else provides.get(name, name)
        if name in chosen or SKIP.match(name):
            continue
        if name not in packages:
            print(f"not found, skipped: {name}", file=sys.stderr)
            continue
        chosen.add(name)
        fields = packages[name]
        for field in ("Pre-Depends", "Depends"):
            for dependency in re.split(r",\s*", fields.get(field, "")):
                if dependency:
                    todo.append(re.split(r"[\s(:|]", dependency.strip())[0])
    return sorted(chosen)


def unpack(deb, out):
    """A .deb is an ar archive; its data.tar.* holds the files."""
    position = 8
    while position < len(deb):
        name = deb[position:position + 16].decode().strip().rstrip("/")
        size = int(deb[position + 48:position + 58])
        body = deb[position + 60:position + 60 + size]
        if name.startswith("data.tar"):
            if name.endswith(".zst"):
                body = subprocess.run(["zstd", "-dc"], input=body, capture_output=True, check=True).stdout
                mode = "r:"
            else:
                mode = "r:*"
            with tarfile.open(fileobj=io.BytesIO(body), mode=mode) as tar:
                for member in tar.getmembers():
                    if member.isdev():
                        continue
                    tar.extract(member, out, filter="tar")
            return
        position += 60 + size + (size & 1)
    raise ValueError("no data archive in the package")


def fix_links(out):
    """Absolute symbolic links (/lib/...) point into the sysroot instead of the build machine."""
    for folder, _, files in os.walk(out):
        for name in files:
            path = os.path.join(folder, name)
            if os.path.islink(path):
                target = os.readlink(path)
                if target.startswith("/"):
                    os.remove(path)
                    os.symlink(os.path.relpath(out + target, folder), path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default="out/sysroot-arm64")
    parser.add_argument("--extra", nargs="*", default=[],
                        help="more packages, e.g. mesa-vulkan-drivers for a root that runs the build under qemu-aarch64")
    args = parser.parse_args()
    out = os.path.abspath(args.out)
    WANTED.extend(args.extra)
    os.makedirs(out, exist_ok=True)
    packages, provides = read_index()
    names = resolve(packages, provides)
    print(f"{len(names)} packages")
    for name in names:
        fields = packages[name]
        deb = fetch(f"{MIRROR}/{fields['Filename']}")
        if hashlib.sha256(deb).hexdigest() != fields["SHA256"]:
            raise ValueError(f"checksum mismatch: {name}")
        unpack(deb, out)
    # Ubuntu keeps /lib, /bin and /sbin as links into /usr; a few packages still ship files under the top-level ones.
    for link in ("lib", "bin", "sbin"):
        top, usr = os.path.join(out, link), os.path.join(out, "usr", link)
        if os.path.isdir(top) and not os.path.islink(top):
            subprocess.run(["cp", "-a", top + "/.", usr + "/"], check=True)
            subprocess.run(["rm", "-rf", top], check=True)
        if not os.path.lexists(top):
            os.symlink(f"usr/{link}", top)
    fix_links(out)
    print(f"done: {out}")


if __name__ == "__main__":
    main()
