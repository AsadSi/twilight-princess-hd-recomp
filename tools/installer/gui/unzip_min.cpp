// See unzip_min.h.
#include "unzip_min.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <zlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <sys/stat.h>
#endif

namespace {

uint16_t u16(const std::string& z, size_t p) { return (uint16_t)((uint8_t)z[p] | (uint8_t)z[p + 1] << 8); }
uint32_t u32(const std::string& z, size_t p) { return (uint32_t)u16(z, p) | (uint32_t)u16(z, p + 2) << 16; }

#ifdef _WIN32
std::wstring wide(const std::string& u) {
    int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, w.data(), n);
    return w;
}
#endif

bool make_dir(const std::string& d) {
#ifdef _WIN32
    return CreateDirectoryW(wide(d).c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return mkdir(d.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

// creates every folder of path (a folder path, '/' or '\\' separated)
bool make_dirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); i++) {
        if (i < path.size() && path[i] != '/' && path[i] != '\\') continue;
        std::string part = path.substr(0, i);
        if (part.size() == 2 && part[1] == ':') continue;  // a drive ("C:")
        if (!part.empty() && part.back() != '/' && part.back() != '\\' && !make_dir(part)) return false;
    }
    return true;
}

bool write_file(const std::string& path, const std::string& data) {
#ifdef _WIN32
    FILE* f = _wfopen(wide(path).c_str(), L"wb");
#else
    FILE* f = fopen(path.c_str(), "wb");
#endif
    if (!f) return false;
    bool ok = data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size();
    return (fclose(f) == 0) && ok;
}

bool safe_name(const std::string& n) {
    if (n.empty() || n[0] == '/' || n[0] == '\\' || n.find(':') != std::string::npos) return false;
    size_t s = 0;
    while (s <= n.size()) {
        size_t e = n.find_first_of("/\\", s);
        if (e == std::string::npos) e = n.size();
        if (n.compare(s, e - s, "..") == 0 && e - s == 2) return false;
        s = e + 1;
    }
    return true;
}

bool same_name(const std::string& a, const char* b) {
    size_t n = 0;
    for (; b[n]; n++)
        if (n >= a.size() || tolower((unsigned char)a[n]) != tolower((unsigned char)b[n])) return false;
    return n == a.size();
}

struct Entry {
    std::string name;
    uint16_t flags, method;
    uint32_t crc, csize, usize, local;
};

}  // namespace

bool unzip_to_folder(const std::string& z, const std::string& dest_in, const char* last_name, std::string& err) {
    // end of central directory: the last 22 bytes, or earlier with an archive comment
    if (z.size() < 22) return err = "not a zip archive (too small)", false;
    size_t eocd = std::string::npos;
    size_t lowest = z.size() > 22 + 65535 ? z.size() - 22 - 65535 : 0;
    for (size_t p = z.size() - 22;; p--) {
        if (u32(z, p) == 0x06054b50) {
            eocd = p;
            break;
        }
        if (p == lowest) break;
    }
    if (eocd == std::string::npos) return err = "not a zip archive (no central directory)", false;
    uint16_t count = u16(z, eocd + 10);
    uint32_t cd_size = u32(z, eocd + 12), cd_off = u32(z, eocd + 16);
    if (count == 0xffff || cd_off == 0xffffffff || (uint64_t)cd_off + cd_size > eocd)
        return err = "unsupported zip archive (Zip64 or damaged)", false;

    std::vector<Entry> entries;
    size_t p = cd_off;
    for (unsigned i = 0; i < count; i++) {
        if (p + 46 > eocd || u32(z, p) != 0x02014b50) return err = "damaged zip archive (central directory)", false;
        Entry e;
        e.flags = u16(z, p + 8);
        e.method = u16(z, p + 10);
        e.crc = u32(z, p + 16);
        e.csize = u32(z, p + 20);
        e.usize = u32(z, p + 24);
        uint16_t nlen = u16(z, p + 28), xlen = u16(z, p + 30), clen = u16(z, p + 32);
        e.local = u32(z, p + 42);
        if (p + 46 + nlen > eocd) return err = "damaged zip archive (entry name)", false;
        e.name = z.substr(p + 46, nlen);
        p += 46 + (size_t)nlen + xlen + clen;
        if (e.flags & 1) return err = "unsupported zip archive (encrypted entry " + e.name + ")", false;
        if (e.csize == 0xffffffff || e.usize == 0xffffffff || e.local == 0xffffffff)
            return err = "unsupported zip archive (Zip64 entry " + e.name + ")", false;
        if (!safe_name(e.name)) return err = "refusing the zip entry name " + e.name, false;
        entries.push_back(e);
    }
    // the marker file last: it exists only when everything before it was written
    if (last_name)
        std::stable_partition(entries.begin(), entries.end(), [&](const Entry& e) { return !same_name(e.name, last_name); });

    std::string dest = dest_in;
    if (!dest.empty() && dest.back() != '/' && dest.back() != '\\') dest += '/';
    if (!make_dirs(dest)) return err = "cannot create the folder " + dest_in, false;
    for (const Entry& e : entries) {
        std::string out = dest + e.name;
        bool is_dir = e.name.back() == '/' || e.name.back() == '\\';
        size_t slash = out.find_last_of("/\\");
        if (!make_dirs(is_dir ? out : out.substr(0, slash + 1))) return err = "cannot create a folder for " + out, false;
        if (is_dir) continue;
        size_t l = e.local;
        if ((uint64_t)l + 30 > z.size() || u32(z, l) != 0x04034b50) return err = "damaged zip archive (" + e.name + ")", false;
        size_t data = l + 30 + u16(z, l + 26) + u16(z, l + 28);
        if ((uint64_t)data + e.csize > z.size()) return err = "damaged zip archive (" + e.name + " is cut off)", false;
        std::string bytes;
        if (e.method == 0) {
            if (e.csize != e.usize) return err = "damaged zip archive (" + e.name + ")", false;
            bytes = z.substr(data, e.csize);
        } else if (e.method == 8) {
            bytes.resize(e.usize);
            z_stream s = {};
            if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return err = "zlib could not start", false;
            s.next_in = (Bytef*)(z.data() + data);
            s.avail_in = e.csize;
            s.next_out = (Bytef*)bytes.data();
            s.avail_out = e.usize;
            int r = inflate(&s, Z_FINISH);
            bool ok = r == Z_STREAM_END && s.total_out == e.usize;
            inflateEnd(&s);
            if (!ok) return err = "damaged zip archive (" + e.name + " does not unpack)", false;
        } else {
            return err = "unsupported zip archive (compression method " + std::to_string(e.method) + ")", false;
        }
        if ((uint32_t)crc32(0, (const Bytef*)bytes.data(), (uInt)bytes.size()) != e.crc)
            return err = "damaged zip archive (" + e.name + ": CRC mismatch)", false;
        if (!write_file(out, bytes)) return err = "cannot write " + out, false;
    }
    return true;
}
