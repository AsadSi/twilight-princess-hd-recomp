// zlib125: inflate on the host's zlib. The game's z_stream (32-bit, big-endian) is mirrored into a
// host z_stream per call: next_in +0x00, avail_in +0x04, total_in +0x08, next_out +0x0C,
// avail_out +0x10, total_out +0x14, msg +0x18, state +0x1C, zalloc +0x20, zfree +0x24, opaque +0x28,
// data_type +0x2C, adler +0x30. The game's allocator callbacks are not used.
#include <zlib.h>

#include <mutex>
#include <unordered_map>

#include "../runtime.h"

namespace {
std::mutex g_mu;
std::unordered_map<uint32_t, z_stream*> g_streams;  // by guest z_stream address

z_stream* stream(uint32_t g) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_streams.find(g);
    return it == g_streams.end() ? nullptr : it->second;
}
void to_host(uint32_t g, z_stream& s) {
    s.next_in = ld32(g + 0x00) ? (Bytef*)mem::ptr(ld32(g + 0x00)) : Z_NULL;
    s.avail_in = ld32(g + 0x04);
    s.next_out = ld32(g + 0x0C) ? (Bytef*)mem::ptr(ld32(g + 0x0C)) : Z_NULL;
    s.avail_out = ld32(g + 0x10);
}
void to_guest(uint32_t g, const z_stream& s, const Bytef* in0, const Bytef* out0) {
    if (in0) st32(g + 0x00, ld32(g + 0x00) + (uint32_t)(s.next_in - in0));
    st32(g + 0x04, s.avail_in);
    st32(g + 0x08, (uint32_t)s.total_in);
    if (out0) st32(g + 0x0C, ld32(g + 0x0C) + (uint32_t)(s.next_out - out0));
    st32(g + 0x10, s.avail_out);
    st32(g + 0x14, (uint32_t)s.total_out);
    st32(g + 0x18, 0);  // msg (the host's text is not guest memory)
    st32(g + 0x2C, (uint32_t)s.data_type);
    st32(g + 0x30, (uint32_t)s.adler);
}
}  // namespace

// (strm, windowBits, version, stream_size)
HLE(zlib125, inflateInit2_) {
    uint32_t g = arg(c, 0);
    auto* s = new z_stream{};
    to_host(g, *s);
    int r = inflateInit2(s, (int)arg(c, 1));
    if (r != Z_OK) {
        delete s;
        ret(c, (uint32_t)r);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (z_stream*& old = g_streams[g]) { inflateEnd(old); delete old; }
        g_streams[g] = s;
    }
    st32(g + 0x1C, g);  // state: non-null while initialised
    to_guest(g, *s, s->next_in, s->next_out);
    ret(c, Z_OK);
}
HLE(zlib125, inflate) {
    uint32_t g = arg(c, 0);
    z_stream* s = stream(g);
    if (!s) { ret(c, (uint32_t)Z_STREAM_ERROR); return; }
    to_host(g, *s);
    const Bytef *in0 = s->next_in, *out0 = s->next_out;
    int r = inflate(s, (int)arg(c, 1));
    to_guest(g, *s, in0, out0);
    ret(c, (uint32_t)r);
}
HLE(zlib125, inflateEnd) {
    uint32_t g = arg(c, 0);
    z_stream* s = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_streams.find(g);
        if (it != g_streams.end()) s = it->second, g_streams.erase(it);
    }
    if (!s) { ret(c, (uint32_t)Z_STREAM_ERROR); return; }
    int r = inflateEnd(s);
    delete s;
    st32(g + 0x1C, 0);
    ret(c, (uint32_t)r);
}
