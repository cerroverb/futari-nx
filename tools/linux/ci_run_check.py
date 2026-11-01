#!/usr/bin/env python3
"""futari-linux (2026-10-31): checks the log of a test run of a release archive (the AArch64 workflow's test step).

The run passes when the Vulkan renderer started ("Renderer: Vulkan on ..."), it created the game's Direct3D device
("Guest D3D device ... created") and the game ran frames (a periodic "<seconds> s: <n> game frames" line with n > 0).

    tools/linux/ci_run_check.py <last-run.log>
"""
import re
import sys


def main():
    text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    device = re.search(r"Guest D3D device \(type \d+\) created", text) is not None
    vulkan = re.search(r"Renderer: Vulkan on ", text) is not None and "Renderer initialisation failed" not in text
    frames = [int(n) for n in re.findall(r"\[renut\] [\d.]+ s: (\d+) game frames", text)]
    errors = re.findall(r"\[(?:error|critical)\].*", text)
    print(f"Vulkan renderer: {vulkan}; device created: {device}; frame reports: {frames}; error lines: {len(errors)}")
    for line in errors[:10]:
        print("  " + line[:200])
    if not vulkan or not device or not frames or max(frames) == 0:
        print("FAIL: the game did not draw")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
