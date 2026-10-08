// The name font's characters (game_font.h): Yaz0-compressed RARC -> JUTResFont (.bfn) -> MAP1 blocks.
#include "game_font.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "../runtime.h"

namespace game_font {
namespace {

namespace fs = std::filesystem;

struct Reader {  // bounds-checked big-endian reads
    const uint8_t* d;
    size_t n;
    bool ok(size_t at, size_t len) const { return at <= n && len <= n - at; }
    uint16_t u16(size_t at) const { return ok(at, 2) ? d[at] << 8 | d[at + 1] : 0; }
    uint32_t u32(size_t at) const { return ok(at, 4) ? (uint32_t)u16(at) << 16 | u16(at + 2) : 0; }
};

std::vector<uint8_t> yaz0(const std::vector<uint8_t>& in) {
    if (in.size() < 16 || memcmp(in.data(), "Yaz0", 4)) return in;
    const size_t size = (size_t)in[4] << 24 | in[5] << 16 | in[6] << 8 | in[7];
    std::vector<uint8_t> out;
    out.reserve(size);
    size_t i = 16;
    while (out.size() < size && i < in.size()) {
        const uint8_t code = in[i++];
        for (int b = 0; b < 8 && out.size() < size; b++) {
            if (code & (0x80 >> b)) {
                if (i >= in.size()) return {};
                out.push_back(in[i++]);
                continue;
            }
            if (i + 1 >= in.size()) return {};
            const size_t dist = ((in[i] & 15) << 8 | in[i + 1]) + 1;
            size_t len = in[i] >> 4;
            i += 2;
            if (!len) {
                if (i >= in.size()) return {};
                len = in[i++] + 0x12;
            } else {
                len += 2;
            }
            if (dist > out.size()) return {};
            for (size_t k = 0; k < len && out.size() < size; k++) out.push_back(out[out.size() - dist]);
        }
    }
    return out;
}

// a RARC archive's file `name`: offset and size in the archive
bool rarc_find(const std::vector<uint8_t>& a, const char* name, size_t* off, size_t* len) {
    if (a.size() < 0x40 || memcmp(a.data(), "RARC", 4)) return false;
    Reader r{a.data(), a.size()};
    const size_t data = (size_t)r.u32(0x0C) + 0x20, files = (size_t)r.u32(0x2C) + 0x20, names = (size_t)r.u32(0x34) + 0x20;
    for (uint32_t k = 0, n = r.u32(0x28); k < n && k < 65536; k++) {
        const size_t e = files + k * 0x14;
        const uint32_t type_name = r.u32(e + 4), at = names + (type_name & 0xFFFFFF);
        if (type_name >> 24 & 0x02 || !r.ok(at, strlen(name) + 1) || memcmp(a.data() + at, name, strlen(name) + 1)) continue;
        *off = data + r.u32(e + 8);
        *len = r.u32(e + 12);
        return r.ok(*off, *len);
    }
    return false;
}

std::shared_ptr<const Glyphs> load() {
    const fs::path arc = fs::path(config::game_dir) / "content" / "res" / "Fontus" / "fontres.arc";
    std::string why;
    std::shared_ptr<Glyphs> glyphs;
    if (FILE* f = fopen(arc.string().c_str(), "rb")) {
        std::vector<uint8_t> data;
        uint8_t buf[65536];
        for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) data.insert(data.end(), buf, buf + n);
        fclose(f);
        data = yaz0(data);
        size_t off = 0, len = 0;
        if (!rarc_find(data, "rodan_b_24_22.bfn", &off, &len)) {
            why = "no rodan_b_24_22.bfn in " + arc.string();
        } else {
            glyphs = std::make_shared<Glyphs>();
            if (!parse_bfn(data.data() + off, len, *glyphs) || glyphs->empty()) why = "rodan_b_24_22.bfn is no font", glyphs = nullptr;
        }
    } else {
        why = "can't open " + arc.string();
    }
    if (!glyphs) {
        LOG("[text] the game's name font can't be read (%s): the text prompt offers its full character set", why.c_str());
        return nullptr;
    }
    LOG("[text] the game's name font (rodan_b_24_22.bfn) has %zu characters: the text prompt offers those", glyphs->size());
    return glyphs;
}

}  // namespace

bool parse_bfn(const uint8_t* d, size_t n, Glyphs& out) {
    if (n < 0x20 || memcmp(d, "FONTbfn1", 8)) return false;
    // the font's single-byte codes are Windows-1252: 0x80..0x9F differ from Latin-1
    static const uint16_t k1252[32] = {0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                       0x2039, 0x0152, 0, 0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013,
                                       0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};
    auto add = [&](uint32_t c) {
        if (c >= 0x80 && c < 0xA0) c = k1252[c - 0x80];
        if (c) out.insert(c);
    };
    Reader r{d, n};
    size_t p = 0x20;
    for (uint32_t k = 0, blocks = r.u32(12); k < blocks && k < 4096 && r.ok(p, 16); k++) {
        const uint32_t size = r.u32(p + 4);
        if (!memcmp(d + p, "MAP1", 4)) {
            const uint16_t method = r.u16(p + 8), first = r.u16(p + 10), last = r.u16(p + 12), count = r.u16(p + 14);
            if (method == 0) {  // linear: a run of codes
                for (uint32_t c = first; c <= last && c < 0x100; c++) add(c);
            } else if (method == 2) {  // table: one glyph index per code, 0xFFFF for none
                for (uint32_t c = first; c <= last && c < 0x100; c++)
                    if (r.ok(p + 16 + (c - first) * 2, 2) && r.u16(p + 16 + (c - first) * 2) != 0xFFFF) add(c);
            } else if (method == 3) {  // map: (code, glyph index) pairs
                for (uint16_t j = 0; j < count && r.ok(p + 16 + j * 4, 4); j++)
                    if (r.u16(p + 16 + j * 4) < 0x100) add(r.u16(p + 16 + j * 4));
            }
        }
        if (size < 8) return false;
        p += size;
    }
    return true;
}

std::shared_ptr<const Glyphs> name_glyphs(int) {
    static const std::shared_ptr<const Glyphs> glyphs = load();
    return glyphs;
}

}  // namespace game_font
