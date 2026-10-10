// Tunic colours (tunic.h). Link's Hero's clothes are textures in res/Object/Kmdl.pack.gz: a TMPK (big-endian: file
// count +4; from +0x10, 16 bytes a file: name offset, data offset, size) holding a GTX for each model (Gfx2: header size
// +4, then blocks "BLK{" with their header's size +4, type +0x10, data size +0x14; type 0x0B a texture's GX2Surface,
// format +0x14; 0x0C and 0x0D its image and mipmap data). In the body's and the hat's (al.bmd, al_head.bmd) BC1
// textures (format 0x31), each block's two end colours (RGB565, little-endian) are moved from green to the chosen
// colour where they are green (hue 60-170 degrees, about 95 on average), so the belt, skin, hair and metal keep theirs. A block whose end colours
// change order is reordered, its indices too, to keep its mode (BC1 reads c0 > c1 as four colours, else three and
// transparent). The game reads the written pack in place of its own from the next start (packages.cpp, content.h).
#include "tunic.h"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

namespace mods::tunic {
namespace {
struct Look {
    float hue, sat, val;  // hue < 0: the hue kept (black, white)
};
const Look kLooks[] = {{0, 1.5f, 1}, {220, 1.4f, 1}, {280, 1.4f, 1}, {50, 1.4f, 1.15f}, {-1, 0.1f, 0.3f}, {-1, 0.08f, 1.8f}};

uint32_t be32(const std::vector<uint8_t>& d, size_t at) {
    return at + 4 <= d.size() ? (uint32_t)d[at] << 24 | d[at + 1] << 16 | d[at + 2] << 8 | d[at + 3] : 0;
}

uint16_t recolour(uint16_t c, const Look& l) {
    const float r = (c >> 11) / 31.f, g = ((c >> 5) & 63) / 63.f, b = (c & 31) / 31.f;
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    float s = mx > 0 ? d / mx : 0, v = mx, h = 0;
    if (d > 0) h = mx == r ? 60 * std::fmod((g - b) / d + 6, 6.f) : mx == g ? 60 * ((b - r) / d + 2) : 60 * ((r - g) / d + 4);
    if (h < 60 || h > 170 || s < 0.2f || v < 0.05f) return c;
    if (l.hue >= 0) h = std::fmod(l.hue + (h - 95) * 0.3f + 360, 360.f);  // the shades kept, closer together
    s = std::min(1.f, s * l.sat), v = std::min(1.f, v * l.val);
    const float cc = v * s, x = cc * (1 - std::fabs(std::fmod(h / 60, 2.f) - 1)), m = v - cc;
    const int k = (int)(h / 60) % 6;
    const float rgb[6][3] = {{cc, x, 0}, {x, cc, 0}, {0, cc, x}, {0, x, cc}, {x, 0, cc}, {cc, 0, x}};
    const auto q = [&](float f, int max) { return (uint16_t)std::lround(std::clamp(f + m, 0.f, 1.f) * max); };
    return (uint16_t)(q(rgb[k][0], 31) << 11 | q(rgb[k][1], 63) << 5 | q(rgb[k][2], 31));
}

void recolour_bc1(uint8_t* p, size_t n, const Look& l) {
    for (size_t i = 0; i + 8 <= n; i += 8, p += 8) {
        const uint16_t c0 = p[0] | p[1] << 8, c1 = p[2] | p[3] << 8;
        uint16_t n0 = recolour(c0, l), n1 = recolour(c1, l);
        uint32_t idx = p[4] | p[5] << 8 | p[6] << 16 | (uint32_t)p[7] << 24;
        const bool four = c0 > c1;
        if (four ? n0 < n1 : n0 > n1) {  // swapped: indices 0 <-> 1, and 2 <-> 3 with four colours
            std::swap(n0, n1);
            idx ^= four ? 0x55555555u : ~(idx >> 1) & 0x55555555u;
        } else if (four && n0 == n1) {
            idx = 0;  // one colour: never three and transparent
        }
        p[0] = n0 & 0xFF, p[1] = n0 >> 8, p[2] = n1 & 0xFF, p[3] = n1 >> 8;
        for (int b = 0; b < 4; b++) p[4 + b] = idx >> (8 * b) & 0xFF;
    }
}

void recolour_gtx(std::vector<uint8_t>& d, size_t at, size_t end, const Look& l) {
    if (be32(d, at) != 0x47667832) return;  // "Gfx2"
    uint32_t format = 0;
    for (size_t p = at + be32(d, at + 4); p + 0x20 <= end && be32(d, p) == 0x424C4B7B;) {  // "BLK{"
        const uint32_t type = be32(d, p + 0x10), size = be32(d, p + 0x14);
        const size_t data = p + be32(d, p + 4);
        if (data + size > end) return;
        if (type == 0x0B) format = be32(d, data + 0x14);
        else if ((type == 0x0C || type == 0x0D) && format == 0x31) recolour_bc1(d.data() + data, size, l);
        p = data + size;
    }
}
}  // namespace

std::string pack(const std::filesystem::path& game, const std::filesystem::path& dir, int look) {
    const Look& l = kLooks[std::clamp(look, 0, (int)std::size(kLooks) - 1)];
    const auto out = dir / ("Kmdl-" + std::to_string(look) + ".pack.gz");
    std::error_code ec;
    if (std::filesystem::exists(out, ec)) return out.string();
    gzFile in = gzopen((game / "content/res/Object/Kmdl.pack.gz").c_str(), "rb");
    if (!in) return {};
    std::vector<uint8_t> d;
    for (uint8_t buf[1 << 16];;) {
        const int n = gzread(in, buf, sizeof buf);
        if (n <= 0) break;
        d.insert(d.end(), buf, buf + n);
    }
    gzclose(in);
    if (be32(d, 0) != 0x544D504B) return {};  // "TMPK"
    for (uint32_t i = 0, files = be32(d, 4); i < files; i++) {
        const size_t e = 0x10 + 16 * i, name = be32(d, e), at = be32(d, e + 4), size = be32(d, e + 8);
        if (name >= d.size() || at + size > d.size()) return {};
        const char* s = (const char*)d.data() + name;
        if (!strcmp(s, "tex/Object/Kmdl/archive/bmwr/al.bmd.gtx") || !strcmp(s, "tex/Object/Kmdl/archive/bmwr/al_head.bmd.gtx"))
            recolour_gtx(d, at, at + size, l);
    }
    std::filesystem::create_directories(dir, ec);
    const auto tmp = out.string() + ".tmp";
    gzFile o = gzopen(tmp.c_str(), "wb1");
    const bool ok = o && gzwrite(o, d.data(), (unsigned)d.size()) == (int)d.size();
    if (o) gzclose(o);
    if (!ok || (std::filesystem::rename(tmp, out, ec), ec)) return fprintf(stderr, "[tunic] could not write %s\n", out.c_str()), std::string();
    return out.string();
}
}  // namespace mods::tunic
