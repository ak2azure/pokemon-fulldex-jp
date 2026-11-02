#!/usr/bin/env python3
"""Compiles the widescreen view's shaders (melonDS/src/wide_gpu/*.glsl, written for deko3D and
OpenGL) for Vulkan, as SPIR-V built into a header (GPU3D_WideGpu_Vk.cpp). The sources stay as
they are; for Vulkan, which numbers its bindings in one space for every kind, their storage
buffers are moved to bindings 8 and on and their textures to 16 and on, gl_VertexID is Vulkan's
gl_VertexIndex, and the stages' in and out variables get locations by name (--aml).

    python3 tools/vk_shaders.py <glslangValidator> <wide_gpu folder> <out.h>
"""
import os
import re
import subprocess
import sys
import tempfile

SHADERS = [
    # (array name, file, stage, defines)
    ("wide_vsh_spv", "wide_vsh.glsl", "vert", []),
    ("wide_fsh_spv", "wide_fsh.glsl", "frag", []),
    ("wide_fsh_early_spv", "wide_fsh.glsl", "frag", ["WIDE_EARLY"]),
    ("final_vsh_spv", "final_vsh.glsl", "vert", []),
    ("final_fsh_spv", "final_fsh.glsl", "frag", []),
]


def for_vulkan(text, defines):
    def binding(m, shift):
        return m.group(1) + str(int(m.group(2)) + shift)
    out = []
    for line in text.splitlines():
        if re.search(r"\bbuffer\b", line) and "binding" in line:
            line = re.sub(r"(binding\s*=\s*)(\d+)", lambda m: binding(m, 8), line)
        elif re.search(r"\bu?sampler", line) and "binding" in line:
            line = re.sub(r"(binding\s*=\s*)(\d+)", lambda m: binding(m, 16), line)
        out.append(line.replace("gl_VertexID", "gl_VertexIndex"))
        if line.startswith("#version"):
            out += ["#define %s 1" % d for d in defines + ["WIDE_VULKAN"]]
    return "\n".join(out) + "\n"


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    glslang, folder, out = sys.argv[1:4]
    parts = ["// Made by tools/vk_shaders.py from wide_gpu/*.glsl: do not edit.\n#pragma once\n#include <cstdint>\n"]
    with tempfile.TemporaryDirectory() as tmp:
        for name, src, stage, defines in SHADERS:
            text = for_vulkan(open(os.path.join(folder, src)).read(), defines)
            glsl = os.path.join(tmp, name + "." + stage)
            spv = os.path.join(tmp, name + ".spv")
            open(glsl, "w").write(text)
            r = subprocess.run([glslang, "-V", "--aml", "-S", stage, glsl, "-o", spv],
                               capture_output=True, text=True)
            if r.returncode != 0:
                sys.exit("vk_shaders.py: %s (%s) did not compile:\n%s" % (src, name, r.stdout + r.stderr))
            data = open(spv, "rb").read()
            words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]
            rows = [", ".join("0x%08x" % w for w in words[i:i + 8]) for i in range(0, len(words), 8)]
            parts.append("static const uint32_t %s[] = {\n%s\n};\n" % (name, ",\n".join(rows)))
    new = "".join(parts)
    try:
        old = open(out).read()
    except OSError:
        old = ""
    if old != new:
        open(out, "w").write(new)


if __name__ == "__main__":
    main()
