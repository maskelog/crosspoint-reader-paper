"""
PlatformIO extra_script: fix_assembler.py

On Windows, the Xtensa GCC passes --longcalls to the assembler, but it may
find the system 'as.exe' (Git for Windows / MSYS) before the Xtensa one.
Fix: force -B <prefix> so GCC uses the right assembler, and ensure the
toolchain bin dir is first on PATH.

Also handles the pioarduino unified-toolchain situation where:
  - toolchain-xtensa-esp32/bin/ has the real xtensa-esp32-elf-*.exe binaries
  - toolchain-xtensa-esp-elf/ is a stub with no bin/ subdir
  - The platform calls xtensa-esp32-elf-g++ but the bin dir is not in PATH
"""

Import("env")  # noqa: F821 - SCons injects 'env'
import os
import sys


def _find_toolchain_bin(platform):
    """Return (toolchain_bin_dir, prefix) or (None, None)."""
    pio_packages = os.path.join(os.path.expanduser("~"), ".platformio", "packages")

    # Candidate packages in priority order.
    # toolchain-xtensa-esp-elf (GCC 14.x, pioarduino) ships chip-specific
    # xtensa-esp32-elf-* symlinks alongside the generic xtensa-esp-elf-* ones.
    # Prefer it over the old toolchain-xtensa-esp32 (GCC 8.x) which lacks
    # support for -std=gnu++2b and -mdisable-hardware-atomics.
    candidates = [
        ("toolchain-xtensa-esp-elf", "xtensa-esp32-elf-"),  # GCC 14.x, preferred
        ("toolchain-xtensa-esp32",   "xtensa-esp32-elf-"),  # GCC 8.x, legacy fallback
        ("toolchain-xtensa32",       "xtensa-esp32-elf-"),
    ]

    for pkg_name, pkg_prefix in candidates:
        # First try via platform API (respects version pinning).
        pkg_dir = None
        try:
            pkg_dir = platform.get_package_dir(pkg_name) or None
        except Exception:
            pass

        # Fall back to direct filesystem lookup — handles globally-installed
        # packages not registered with the current platform.
        if not pkg_dir:
            candidate_dir = os.path.join(pio_packages, pkg_name)
            if os.path.isdir(candidate_dir):
                pkg_dir = candidate_dir

        if not pkg_dir:
            continue

        bin_dir = os.path.join(pkg_dir, "bin")
        probe = os.path.join(bin_dir, pkg_prefix + "g++.exe")
        if os.path.isfile(probe):
            return bin_dir, pkg_prefix

    return None, None


if sys.platform == "win32":
    platform = env.PioPlatform()
    toolchain_bin, found_prefix = _find_toolchain_bin(platform)

    if toolchain_bin:
        # Always use xtensa-esp32-elf-* names when addressing ESP32 classic.
        # If the toolchain ships xtensa-esp-elf-* names we adapt the prefix.
        prefix = "xtensa-esp32-elf-"

        b_prefix     = os.path.join(toolchain_bin, found_prefix).replace("\\", "/")
        assembler    = os.path.join(toolchain_bin, found_prefix + "as.exe").replace("\\", "/")
        compiler_c   = os.path.join(toolchain_bin, found_prefix + "gcc.exe").replace("\\", "/")
        compiler_cxx = os.path.join(toolchain_bin, found_prefix + "g++.exe").replace("\\", "/")
        archiver     = os.path.join(toolchain_bin, found_prefix + "ar.exe").replace("\\", "/")
        ranlib       = os.path.join(toolchain_bin, found_prefix + "ranlib.exe").replace("\\", "/")

        env.PrependENVPath("PATH", toolchain_bin)
        env.Replace(
            AS=assembler,
            CC=compiler_c,
            CXX=compiler_cxx,
            LINK=compiler_cxx,
            AR=archiver,
            RANLIB=ranlib,
        )
        env.Append(CCFLAGS=["-B{}".format(b_prefix)])
        env.Append(CXXFLAGS=["-B{}".format(b_prefix)])
        env.Append(LINKFLAGS=["-B{}".format(b_prefix), "-fuse-ld=bfd"])

        print("fix_assembler: using {} tools from {}".format(
            found_prefix.rstrip("-"), toolchain_bin))
    else:
        print("fix_assembler: no Xtensa toolchain found, skipping.")
