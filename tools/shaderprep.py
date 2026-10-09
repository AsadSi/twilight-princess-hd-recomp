#!/usr/bin/env python3
"""Extract the game's GX2 shaders and build a shader "head start" for the runtime cache.

Usage:
  shaderprep.py scan     [--game DIR]                           list containers and shader counts
  shaderprep.py coverage RUNTIME_CACHE                           runtime programs found in the game files
  shaderprep.py template RUNTIME_CACHE [OUT] [--merge T]         distill your recorded runtime cache into the
                                                                 state template (default OUT:
                                                                 game/shadercache/template.bin);
                                                                 --merge keeps what template T already has
  shaderprep.py build    [OUT] [--template T] [--max N]          head start from the game files + template
                         [--no-pipelines]                        (default OUT: game/shadercache/headstart.bin)

Where the shaders are (Twilight Princess HD, USA):
  - content/Shaders.pack.gz, a gzipped TMPK archive (big-endian: "TMPK", file count, alignment, 0, then
    {name offset, data offset, size, 0} per file) of 1,567 GFX2 shader files (.gsh), each one vertex and
    one pixel shader: Shaders/rvl_wrap/gx_<hash>.gsh (the Wii version's GX setups as GX2 programs) and
    HD's own aa, bloom, blur, dof and misc. No other file on the disc holds shaders (the other .pack.gz
    hold .gtx textures and .gmx models).
  - GFX2 layout: header ("Gfx2", header size, version 7.1, ...), then blocks {"BLK{", header size,
    version, type, data size, id, index, data[]}: 3 vertex shader header, 5 its program, 6 pixel shader
    header, 7 its program (2 padding, 1 end). A header is the GX2VertexShader / GX2PixelShader structure
    (big-endian, relocated pointers); a program is the microcode byte-for-byte as the game later hands it
    to the GPU (220 of the 222 programs the game used from save state 1 through Ordon Village are found
    this way; the other two are HD's copy shaders in Zelda.rpx's data, 100CF700 and 100CF900).
  - GX2VertexShader: 52 register words (0x00..0xCC), shader size @0xD0.
    GX2PixelShader: 41 register words (0x00..0xA0), shader size @0xA4.

The head start is written in the runtime's recipe format (runtime/src/gfx/metal_draw.mm, cache_write):
records of {u32 type, u32 raw size, u32 compressed size, zlib(raw)}; a shader record is
{u32 vertex, u32 size, u32 fsSize, u32 nregs, microcode, fetch microcode, (u32 reg, u32 value) * nregs};
a pipeline record is the runtime's PipelineRecipe struct as is (u64 vsKey, psKey, fsKey, render target
formats, blend state, vertex strides; 168 bytes).

Register state can't be derived from the archives alone (it depends on render targets, vertex layouts,
textures...). The template (default: game/shadercache/template.bin) holds the states recorded in your
own play sessions: for each program a hash of its microcode and the register states it was translated
with, the vertex fetch shaders (tiny attribute-layout descriptors) and the pipeline recipes recorded in
those sessions. It is derived from the game, so it is not part of this repository; make it from your
own runtime cache after playing for a while (the further you get, the more it covers):
  shaderprep.py template ~/Library/Caches/tphd/shaders.bin
  shaderprep.py build
`build` then writes:
  - type 1 records: programs from the template with their recorded states ("known"); the runtime
    replays these at startup (runtime/src/gfx/shader_headstart.mm)
  - type 2 records: the template's pipeline recipes. They name their shaders by the runtime's shader
    keys (a hash of the microcode, the translation-relevant registers and the fetch shader), which the
    known records reproduce exactly since they carry the recorded register state; the runtime builds
    them in the background once those shaders are compiled and drops recipes whose shaders it lacks
  - type 3 records: every other archive program with up to --max states recorded for programs of the
    same family (identical shader register block). Speculative; only `wwhd --warm-shaders` uses them,
    compiling everything once so the macOS Metal shader cache already has the code later.
"""
import collections
import gzip
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TEMPLATE = os.path.join(ROOT, "game", "shadercache", "template.bin")  # made by `template`, not shipped

REC_SHADER, REC_PIPELINE, REC_SPECULATIVE = 1, 2, 3
VS, PS, GS = 0, 1, 2


# ---------------------------------------------------------------- containers
GSH_HEADERS = {3: (VS, 52, 0xD0), 6: (PS, 41, 0xA4)}  # GFX2 block type: shader type, register words, size offset


def tmpk(b):
    """yield (name, bytes) for every file of a TMPK archive"""
    for i in range(struct.unpack_from(">I", b, 4)[0]):
        name, off, size, _ = struct.unpack_from(">IIII", b, 0x10 + 16 * i)
        yield b[name:b.index(b"\0", name)].decode(), b[off:off + size]


def gsh_shaders(b):
    """yield (type, register words, microcode) for the shaders of a GFX2 file"""
    o = struct.unpack_from(">I", b, 4)[0]
    head = None
    while o + 0x20 <= len(b):
        hsize, _, _, typ, dsize = struct.unpack_from(">5I", b, o + 4)
        data = b[o + hsize:o + hsize + dsize]
        o += hsize + dsize
        if typ in GSH_HEADERS:
            t, nregs, sz_off = GSH_HEADERS[typ]
            head = t, list(struct.unpack_from(">%dI" % nregs, data)), struct.unpack_from(">I", data, sz_off)[0]
        elif typ in (5, 7) and head:  # the program block after its header
            yield head[0], head[1], data[:head[2]]


def game_shaders(game):
    """all distinct (type, microcode) -> (register words, shader file)"""
    out = {}
    for name, b in tmpk(gzip.decompress(open(os.path.join(game, "Shaders.pack.gz"), "rb").read())):
        if b[:4] == b"Gfx2":
            for typ, regs, code in gsh_shaders(b):
                out.setdefault((typ, code), (regs, name))
    return out


# ---------------------------------------------------------------- recipe records
def read_records(path):
    d = open(path, "rb").read()
    o = 0
    while o + 12 <= len(d):
        t, raw, comp = struct.unpack_from("<III", d, o)
        o += 12
        if o + comp > len(d):
            break
        yield t, zlib.decompress(d[o:o + comp])
        o += comp


def parse_shader_record(r):
    vertex, size, fs_size, n = struct.unpack_from("<IIII", r, 0)
    o = 16
    code = bytes(r[o:o + size])
    o += size
    fs = bytes(r[o:o + fs_size])
    o += fs_size
    regs = [struct.unpack_from("<II", r, o + 8 * i) for i in range(n)]
    return vertex, code, fs, regs


def write_record(f, t, raw):
    comp = zlib.compress(raw, 6)
    f.write(struct.pack("<III", t, len(raw), len(comp)))
    f.write(comp)


def shader_record(vertex, code, fs, regs):
    raw = struct.pack("<IIII", vertex, len(code), len(fs), len(regs)) + code + fs
    raw += b"".join(struct.pack("<II", r, v) for r, v in regs)
    return raw


def prog_hash(code):
    h = 0xCBF29CE484222325
    for i in range(0, len(code), 8):
        h = ((h ^ int.from_bytes(code[i:i + 8].ljust(8, b"\0"), "little")) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h ^ len(code)


# ---------------------------------------------------------------- shader register blocks
# registers GX2SetVertexShader / GX2SetPixelShader write from the shader structure
# (same sequence as runtime/src/gx2/gx2_shader_regs.cpp)
mmSQ_PGM_START_PS, mmSQ_PGM_RESOURCES_PS = 0xA210, 0xA214
mmSQ_PGM_START_VS, mmSQ_PGM_RESOURCES_VS, mmSQ_PGM_START_FS = 0xA216, 0xA21A, 0xA225
mmVGT_PRIMITIVEID_EN, mmSPI_VS_OUT_CONFIG, mmPA_CL_VS_OUT_CNTL = 0xA2A1, 0xA1B1, 0xA207
mmSPI_VS_OUT_ID_0, mmSQ_VTX_SEMANTIC_0, mmSQ_VTX_SEMANTIC_CLEAR = 0xA185, 0xA0E0, 0xA238
mmSPI_PS_IN_CONTROL_0, mmSPI_PS_INPUT_CNTL_0 = 0xA1B3, 0xA191
mmCB_SHADER_MASK, mmCB_SHADER_CONTROL, mmDB_SHADER_CONTROL, mmSPI_INPUT_Z = 0xA08F, 0xA1E8, 0xA203, 0xA1B6


def own_block(vertex, w):
    """register -> value pairs a shader structure sets (w = structure register words)"""
    r = {}
    if vertex:
        r[mmSQ_PGM_RESOURCES_VS] = w[0x00 // 4]
        r[mmVGT_PRIMITIVEID_EN] = w[0x04 // 4]
        r[mmSPI_VS_OUT_CONFIG] = w[0x08 // 4]
        r[mmPA_CL_VS_OUT_CNTL] = w[0x38 // 4]
        for i in range(min(w[0x0C // 4], 10)):
            r[mmSPI_VS_OUT_ID_0 + i] = w[0x10 // 4 + i]
        nsem = min(w[0x40 // 4], 32)
        if nsem:
            r[mmSQ_VTX_SEMANTIC_CLEAR] = 0xFFFFFFFF
            for i in range(nsem):
                r[mmSQ_VTX_SEMANTIC_0 + i] = w[0x44 // 4 + i]
    else:
        r[mmSQ_PGM_RESOURCES_PS] = w[0]
        r[mmSPI_PS_IN_CONTROL_0] = w[2]
        r[mmSPI_PS_IN_CONTROL_0 + 1] = w[3]
        for i in range(min(w[4], 0x20)):
            r[mmSPI_PS_INPUT_CNTL_0 + i] = w[5 + i]
        r[mmCB_SHADER_MASK] = w[37]
        r[mmCB_SHADER_CONTROL] = w[38]
        r[mmDB_SHADER_CONTROL] = w[39]
        r[mmSPI_INPUT_Z] = w[40]
    return r


# ---------------------------------------------------------------- template
def cmd_template(cache, out, merge=None):
    """'WWHT' + zlib( u32 nfetch, {u32 len, bytes}*, u32 nstates,
                      {u64 program hash, u32 vertex, i32 fetch index, u32 nregs, (u32 reg, u32 value)*}*,
                      u32 npipelines, {u32 len, pipeline recipe}* )"""
    fetch = {}
    entries = collections.OrderedDict()
    pipelines = collections.OrderedDict()
    if merge:
        old_states, old_pipelines = read_template(merge)
        for h, vertex, fs, regs in old_states:
            fi = fetch.setdefault(fs, len(fetch)) if fs else -1
            entries.setdefault((h, vertex, fi, tuple(regs)), None)
        pipelines.update((p, None) for p in old_pipelines)
    drop = (mmSQ_PGM_START_VS, mmSQ_PGM_START_PS, mmSQ_PGM_START_FS)  # addresses in the recording's memory
    for t, r in read_records(cache):
        if t == REC_PIPELINE:
            pipelines.setdefault(bytes(r), None)
            continue
        if t != REC_SHADER:
            continue
        vertex, code, fs, regs = parse_shader_record(r)
        fi = fetch.setdefault(fs, len(fetch)) if fs else -1
        regs = tuple((a, v) for a, v in regs if a not in drop)
        entries.setdefault((prog_hash(code), vertex, fi, regs), None)
    raw = bytearray(struct.pack("<I", len(fetch)))
    for fs, _ in sorted(fetch.items(), key=lambda x: x[1]):
        raw += struct.pack("<I", len(fs)) + fs
    raw += struct.pack("<I", len(entries))
    for (h, vertex, fi, regs) in entries:
        raw += struct.pack("<QIiI", h, vertex, fi, len(regs))
        raw += b"".join(struct.pack("<II", a, v) for a, v in regs)
    raw += struct.pack("<I", len(pipelines))
    for p in pipelines:
        raw += struct.pack("<I", len(p)) + p
    data = b"WWHT" + zlib.compress(bytes(raw), 9)
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    open(out, "wb").write(data)
    print("template: %d states, %d fetch shaders, %d pipelines, %d bytes -> %s" %
          (len(entries), len(fetch), len(pipelines), len(data), out))


def read_template(path):
    if not os.path.isfile(path):
        sys.exit("no state template at %s\n"
                 "make one from your own runtime cache after playing for a while:\n"
                 "  tools/shaderprep.py template ~/Library/Caches/tphd/shaders.bin" % path)
    d = open(path, "rb").read()
    if d[:4] != b"WWHT":
        sys.exit("not a template: " + path)
    raw = zlib.decompress(d[4:])
    o = 4
    fetch = []
    for _ in range(struct.unpack_from("<I", raw, 0)[0]):
        ln = struct.unpack_from("<I", raw, o)[0]
        fetch.append(raw[o + 4:o + 4 + ln])
        o += 4 + ln
    n = struct.unpack_from("<I", raw, o)[0]
    o += 4
    states = []
    for _ in range(n):
        h, vertex, fi, nr = struct.unpack_from("<QIiI", raw, o)
        o += 20
        regs = [struct.unpack_from("<II", raw, o + 8 * i) for i in range(nr)]
        o += 8 * nr
        states.append((h, vertex, fetch[fi] if fi >= 0 else b"", regs))
    pipelines = []
    if o + 4 <= len(raw):  # older templates end here
        n = struct.unpack_from("<I", raw, o)[0]
        o += 4
        for _ in range(n):
            ln = struct.unpack_from("<I", raw, o)[0]
            pipelines.append(raw[o + 4:o + 4 + ln])
            o += 4 + ln
    return states, pipelines


# ---------------------------------------------------------------- commands
def cmd_scan(game):
    shaders = game_shaders(game)
    per = collections.Counter(os.path.dirname(p) for (_, (_, p)) in shaders.items())
    kinds = collections.Counter(t for t, _ in shaders)
    print("distinct shaders: %d (VS %d, PS %d)" % (len(shaders), kinds[VS], kinds[PS]))
    for k, v in per.most_common(20):
        print("%6d  %s" % (v, k))


def cmd_coverage(game, cache):
    shaders = game_shaders(game)
    progs = set()
    for t, r in read_records(cache):
        if t in (REC_SHADER, REC_SPECULATIVE):
            vertex, code, fs, regs = parse_shader_record(r)
            progs.add((VS if vertex else PS, code))
    have = sum(1 for k in progs if k in shaders)
    print("programs: %d, found in archives: %d (%.1f%%)" % (len(progs), have, 100.0 * have / max(len(progs), 1)))
    missing = [k for k in progs if k not in shaders]
    print("missing by type:", dict(collections.Counter("VS" if t == VS else "PS" for t, _ in missing)))


def cmd_build(game, out, template, limit, with_pipelines=True):
    states, pipelines = read_template(template)
    shaders = game_shaders(game)
    if not with_pipelines:
        pipelines = []
    by_hash = collections.defaultdict(list)
    for h, vertex, fs, regs in states:
        by_hash[(h, bool(vertex))].append((fs, regs))
    # speculative states: those recorded for programs with an identical shader register block
    blocks = {}
    for (typ, code), (w, path) in shaders.items():
        blocks.setdefault((typ == VS, tuple(sorted(own_block(typ == VS, w)))), None)
    by_family = collections.defaultdict(list)
    for h, vertex, fs, regs in states:
        regmap = dict(regs)
        for (tv, regset) in blocks:
            if tv == bool(vertex):
                by_family[(tv, tuple((a, regmap.get(a, 0)) for a in regset))].append((fs, regs))
    pgm = {True: mmSQ_PGM_START_VS, False: mmSQ_PGM_START_PS}
    known = spec = 0
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "wb") as f:
        for (typ, code), (w, path) in shaders.items():
            vertex = typ == VS
            for fs, regs in by_hash.get((prog_hash(code), vertex), []):
                r = dict(regs)
                r[pgm[vertex] + 1] = len(code) >> 3  # the loader supplies the program address
                write_record(f, REC_SHADER, shader_record(1 if vertex else 0, code, fs, sorted(r.items())))
                known += 1
        for p in pipelines:
            write_record(f, REC_PIPELINE, p)
        for (typ, code), (w, path) in shaders.items():
            vertex = typ == VS
            if (prog_hash(code), vertex) in by_hash:
                continue
            blk = own_block(vertex, w)
            for fs, regs in by_family.get((vertex, tuple(sorted(blk.items()))), [])[:limit]:
                r = dict(regs)
                r.update(blk)
                r[pgm[vertex] + 1] = len(code) >> 3
                write_record(f, REC_SPECULATIVE, shader_record(1 if vertex else 0, code, fs, sorted(r.items())))
                spec += 1
    print("archive shaders %d; known records %d; pipeline records %d; speculative records %d -> %s (%d bytes)" %
          (len(shaders), known, len(pipelines), spec, out, os.path.getsize(out)))


def main():
    args = sys.argv[1:]
    opts = {"--game": os.path.join(ROOT, "game", "content"), "--max": "2", "--template": TEMPLATE, "--merge": None}
    no_pipelines = "--no-pipelines" in args
    if no_pipelines:
        args.remove("--no-pipelines")
    for k in list(opts):
        if k in args:
            i = args.index(k)
            opts[k] = args[i + 1]
            del args[i:i + 2]
    if not args:
        print(__doc__)
        return
    cmd, game = args[0], opts["--game"]
    if cmd == "scan":
        cmd_scan(game)
    elif cmd == "coverage":
        cmd_coverage(game, args[1])
    elif cmd == "template":
        cmd_template(args[1], args[2] if len(args) > 2 else TEMPLATE, opts["--merge"])
    elif cmd == "build":
        out = args[1] if len(args) > 1 else os.path.join(ROOT, "game", "shadercache", "headstart.bin")
        cmd_build(game, out, opts["--template"], int(opts["--max"]), not no_pipelines)
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
