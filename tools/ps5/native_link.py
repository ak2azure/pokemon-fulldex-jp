#!/usr/bin/env python3
"""Links a PS5 title's executable (eboot.elf, before it is signed) the way
ps5-native-app-boilerplate does (https://github.com/leolobato/ps5-native-app-boilerplate,
GPL-3.0-or-later; its tools/build.sh): LLVM's linker with the boilerplate's start-up code and
linker script, then the boilerplate's converter, which turns the imports into the console's
module imports. tools/ps5/build.sh uses it for the program (as CMake's link launcher:
everything after "--" is the link command CMake would have run, of which the objects, archives
and the output are kept) and for the test title.

    native_link.py --config FILE -- <link command>
    native_link.py --config FILE --out eboot.elf <objects and archives>

FILE (JSON, written by tools/ps5/build.sh) names the SDK, the boilerplate's checkout and
converter, the platform layer (Mihawk's payload SDK fork, platform/) and the wraps object.
"""
import json
import os
import subprocess
import sys

# libc functions no module a title loads really exports, or that are refused to a title or
# fault there, as the platform layer's include/ps5platform/libc.h lists them (measured on the
# console by Mihawk's titles).
ABSENT = set("""
gmtime_r localtime_r utimensat futimens dirfd clock_nanosleep arc4random arc4random_buf
arc4random_uniform if_nameindex strcasestr memccpy times sockatmark getpwuid getpwnam_r
posix_madvise strsignal gethostbyaddr tmpfile if_nametoindex if_indextoname mkstemp isatty link
symlink readlink fchown openat unlinkat fchmodat fstatat mkdirat renameat statvfs fstatvfs
opendir fdopendir readdir closedir rewinddir getaddrinfo freeaddrinfo getnameinfo gethostbyname
gai_strerror qsort_r mkstemps openlog __xuname regcomp regexec regfree regerror newlocale
freelocale strtod_l strtof_l dladdr popen pclose open_memstream memfd_create accept4
getpagesizes in6addr_any realpath syscall getcwd statfs fstatfs umask fork setsid wait4
""".split())

# Libraries a link command names that the title gets its own way (the modules' stubs, libc.a
# after them, the C++ runtime in the group).
SYSTEM_LIBS = {"c", "m", "pthread", "dl", "rt", "c++", "c++abi", "unwind"}


def main():
    args = sys.argv[1:]
    if len(args) < 2 or args[0] != "--config":
        sys.exit(__doc__)
    cfg = json.load(open(args[1]))
    rest = args[2:]
    out = None
    inputs = []
    if rest and rest[0] == "--":
        cmd = rest[1:]
        i = 0
        while i < len(cmd):
            a = cmd[i]
            if a == "-o":
                out = cmd[i + 1]
                i += 2
                continue
            # (Objects and archives CMake built, and libraries named with -l that are archives of
            # the SDK's ports, as SDL's own libraries; the console's modules' stubs and the C
            # and C++ runtimes are given below whatever the command names.)
            if not a.startswith("-") and (a.endswith(".o") or a.endswith(".obj") or a.endswith(".a")):
                inputs.append(a)
            elif a.startswith("-l") and a[2:] not in SYSTEM_LIBS and not a[2:].startswith(("Sce", "kernel")):
                for d in (os.path.join(cfg["sdk"], "target", "user", "homebrew", "lib"),):
                    if os.path.exists(os.path.join(d, "lib%s.a" % a[2:])):
                        inputs.append(os.path.join(d, "lib%s.a" % a[2:]))
                        break
            i += 1
    else:
        if rest[:1] == ["--out"]:
            out = rest[1]
            rest = rest[2:]
        inputs = rest
    if not out:
        sys.exit("native_link.py: no output")

    sdk = cfg["sdk"]
    lib = os.path.join(sdk, "target", "lib")
    hb = os.path.join(sdk, "target", "user", "homebrew", "lib")
    native = os.path.join(cfg["boilerplate"], "tooling", "native")
    pie = out + ".pie"
    # (Not libkernel_sys: titles do not import it, so what only it exports resolves to nothing on
    # the console; wraps.c gives those, and the link fails for any it does not.)
    stubs = sorted(os.path.join(lib, f) for f in os.listdir(lib)
                   if f.endswith(".so") and f not in ("libkernel_sys.so", "libmonosgen-2.0.so"))
    builtins = cfg.get("builtins")
    link = [os.path.join(sdk, "bin", "prospero-lld"),
            "-L", native, "-T", os.path.join(os.path.dirname(os.path.abspath(__file__)), "native", "ps5-pie-unwind.ld"),
            "--eh-frame-hdr", "--error-limit=0",
            # (A weak reference nothing defines stays 0 instead of becoming an import, which the
            # converter could not place: RADV's entry points, thread_local hooks.)
            "--no-dynamic-linker",
            # (out.map: which function each address is in, for the crash reports in game.log.)
            "--Map=" + out + ".map",
            "--version-script", os.path.join(native, "app-symbols.map"), "--exclude-libs=ALL",
            "-e", "_start", "-L", lib, "-o", pie, cfg["crt"], cfg["wraps"]] + cfg.get("objects", [])
    # Archives may need each other in any order (melonDS's core and the frontend's), so they go
    # in one group, with the platform layer and the C++ runtime.
    link += [x for x in inputs if not x.endswith(".a")]
    # RADV (cfg "whole": its archive, tools/ps5/build_radv.sh), linked whole: Mesa's dispatch
    # tables name every command through weak references, which pull no archive member in, so a
    # command no other code needs would be missing (PS5_Vulkan's tools/radv-link.sh).
    if cfg.get("whole"):
        link += ["--whole-archive"] + cfg["whole"] + ["--no-whole-archive"]
    link += ["--start-group"] + [x for x in inputs if x.endswith(".a")] + [cfg["platform"]]
    link += [os.path.join(lib, "libc++.a"), os.path.join(lib, "libc++abi.a"), os.path.join(lib, "libunwind.a")]
    if builtins:
        link.append(builtins)
    link += ["--end-group"]
    link += ["--wrap=" + w for w in cfg["wraps_names"]]
    # Names bound straight to the platform layer's ps5_ functions (cfg "defsyms", for RADV's own
    # use of libc: tools/ps5/build_native.sh lists them, after radv-link.sh), each kept local
    # (a title may export nothing).
    defsyms = cfg.get("defsyms", [])
    if defsyms:
        link += ["--defsym=%s=ps5_%s" % (n, n) for n in defsyms]
        vmap = out + ".local.map"
        with open(vmap, "w") as f:
            f.write("{\n    local:\n" + "".join("        %s;\n" % n for n in defsyms) + "};\n")
        link += ["--version-script", vmap]
    link += cfg.get("flags", [])
    # The console's modules (their stubs), then what the SDK's libc.a adds that none of them
    # has (emulated thread-local storage, among others): a symbol a module exports is taken
    # from the module, since a shared definition seen first is never replaced by an archive's.
    link += ["--as-needed"] + stubs + [os.path.join(lib, "libc.a")]
    subprocess.check_call(link)
    # A thread_local declared in one file and set up in another is reached through a hook
    # (_ZTH...) that is called only if it exists; for one with nothing to set up (NDS::Current)
    # none does, and the converter cannot place the weak reference left behind. Those hooks get
    # empty bodies and the title is linked again.
    nm = os.path.join(sdk, "bin", "prospero-nm")
    hooks = sorted(set(l.split()[-1] for l in subprocess.check_output([nm, "-D", "--undefined-only", pie], text=True)
                       .splitlines() if l.strip() and l.split()[-1].startswith("_ZTH")))
    if hooks:
        src = out + ".tlshooks.c"
        with open(src, "w") as f:
            f.write("".join("void %s(void) {}\n" % h for h in hooks))
        subprocess.check_call([os.path.join(sdk, "bin", "prospero-clang"), "-O2", "-c", src, "-o", src + ".o"])
        link.insert(link.index(cfg["wraps"]) + 1, src + ".o")
        subprocess.check_call(link)
    # What the title imports from the console's modules, kept beside it (out.imports), and
    # none of the functions a title's modules do not really give, which the SDK's stubs list
    # anyway: called, they would jump to nothing. wraps.c gives the program's own.
    imports = sorted(set(l.split()[-1] for l in subprocess.check_output(
        [nm, "-D", "--undefined-only", pie], text=True).splitlines() if l.strip()))
    with open(out + ".imports", "w") as f:
        f.write("\n".join(imports) + "\n")
    bad = sorted(set(imports) & ABSENT)
    if bad:
        sys.exit("native_link.py: the title would import what the console does not give a title: "
                 + " ".join(bad) + " (add them to tools/ps5/native/wraps.c)")
    subprocess.check_call([cfg["tool"], "link", "--in", pie, "--out", out, "--stub-dir", lib,
                           "--module-sdk", "0x02000009", "--companion-sdk", "0x08050001",
                           "--file-name", "eboot.elf"])
    os.remove(pie)


if __name__ == "__main__":
    main()
