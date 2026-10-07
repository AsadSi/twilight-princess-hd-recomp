// Vulkan snapshot reuse caches (runtime/src/gfx/vulkan/uniform_snapshot.h, vertex_snapshot_history.h):
// reuse decisions must never read mapped upload memory (issue #44: uncached or write-combined on
// discrete GPUs, where CPU reads are ~100x slower). Every slice the fake allocator hands out is
// overwritten with poison right after the snapshot wrote it, and its bytes are counted as written; the
// caches must still find exactly the expected hits, and return the slice whose original bytes equal
// the fresh guest bytes.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "gfx/vulkan/uniform_snapshot.h"
#include "gfx/vulkan/vertex_snapshot_history.h"

using namespace gfxvk;

namespace {

struct Slice {
  uint64_t buffer = 0;  // nonzero: valid
  void* mapped = nullptr;
  size_t size = 0;
};

// Fake upload arena: each slice's "GPU" bytes are recorded on the side (what the GPU would read); the
// mapped bytes are poisoned after the write, so any comparison against mapped memory sees garbage.
struct Arena {
  std::vector<std::unique_ptr<uint8_t[]>> mapped;
  std::vector<std::vector<uint8_t>> gpu;
  uint64_t allocations = 0;
  Slice snapshot(const void* data, size_t size) {
    const size_t n = size < 16 ? 16 : size;
    mapped.emplace_back(new uint8_t[n]);
    uint8_t* m = mapped.back().get();
    if (data && size) {
      std::memcpy(m, data, size);
      std::memset(m + size, 0, n - size);
    } else {
      std::memset(m, 0, n);
    }
    gpu.emplace_back(m, m + n);
    std::memset(m, 0xCD, n);  // poison: the CPU must not read this back
    ++allocations;
    return {allocations, m, n};
  }
  const std::vector<uint8_t>& bytes(const Slice& s) const { return gpu.at(s.buffer - 1); }
};

bool slice_holds(const Arena& a, const Slice& s, const void* data, size_t size) {
  return !size || !std::memcmp(a.bytes(s).data(), data, size);
}

void uniform_cache() {
  Arena arena;
  UniformSnapshotCache<Slice, int> cache;
  auto make = [&](const void* d, size_t n) { return arena.snapshot(d, n); };
  uint8_t a[64], b[64];
  for (int i = 0; i < 64; ++i) a[i] = uint8_t(i * 7 + 1), b[i] = a[i];
  b[40] ^= 0x55;

  Slice s1 = cache.get(1, 1, 3, a, 64, make);  // miss
  Slice s2 = cache.get(1, 1, 3, a, 64, make);  // hit: equal bytes (mapped is poison)
  assert(s1.buffer == s2.buffer && arena.allocations == 1);
  assert(cache.counters.hits == 1 && cache.counters.comparisons == 1);
  Slice s3 = cache.get(1, 1, 3, b, 64, make);  // changed byte: miss
  assert(s3.buffer != s1.buffer && slice_holds(arena, s3, b, 64));
  Slice s4 = cache.get(1, 1, 3, b, 64, make);  // hit on the new copy
  assert(s4.buffer == s3.buffer && cache.counters.hits == 2);
  // the old copy is gone: a, then b again, misses (one entry per slot)
  Slice s5 = cache.get(1, 1, 3, a, 64, make);
  assert(s5.buffer != s1.buffer && slice_holds(arena, s5, a, 64) && cache.counters.hits == 2);
  // a different size never compares
  cache.get(1, 1, 3, a, 32, make);
  assert(cache.counters.checks == 4);
  // other slots are independent
  Slice t1 = cache.get(1, 1, 20, a, 64, make);
  Slice t2 = cache.get(1, 1, 20, a, 64, make);
  assert(t1.buffer == t2.buffer);
  // zero-filled entries (no source bytes): nullptr hits; zero guest bytes hit; others miss
  uint8_t zero[48] = {}, nonzero[48] = {};
  nonzero[47] = 1;
  Slice z1 = cache.get(1, 1, 5, nullptr, 48, make);
  Slice z2 = cache.get(1, 1, 5, nullptr, 48, make);
  Slice z3 = cache.get(1, 1, 5, zero, 48, make);
  assert(z1.buffer == z2.buffer && z1.buffer == z3.buffer);
  Slice z4 = cache.get(1, 1, 5, nonzero, 48, make);
  assert(z4.buffer != z1.buffer && slice_holds(arena, z4, nonzero, 48));
  // a new generation or device forgets everything
  const uint64_t before = arena.allocations;
  cache.get(1, 2, 20, a, 64, make);
  cache.get(2, 2, 20, a, 64, make);
  assert(arena.allocations == before + 2);
  // the factory gets the cache's own copy, not the caller's buffer: a guest write between the
  // comparison copy and the GPU copy cannot make them differ
  const void* seen = nullptr;
  cache.get(2, 2, 7, a, 64, [&](const void* d, size_t n) { seen = d; return arena.snapshot(d, n); });
  assert(seen && seen != a);
  printf("uniform snapshot cache: ok\n");
}

void vertex_history() {
  Arena arena;
  VertexSnapshotHistory<Slice> h;
  auto make = [&](const void* d, size_t n) { return arena.snapshot(d, n); };
  std::vector<uint8_t> p(256), q(256);
  for (size_t i = 0; i < p.size(); ++i) p[i] = uint8_t(i * 3), q[i] = uint8_t(i * 5 + 1);
  uint64_t hits = 0;
  // the draw.cpp lookup: last, then (history on) previous; equal bytes hit
  auto get = [&](uint32_t address, const std::vector<uint8_t>& data, bool keepHistory) {
    const uint32_t size = uint32_t(data.size());
    auto* candidate = VertexSnapshotHistory<Slice>::matches(h.last, address, size) ? &h.last
                      : keepHistory ? h.secondary(address, size) : nullptr;
    if (candidate && VertexSnapshotHistory<Slice>::equal(*candidate, data.data())) {
      ++hits;
      if (candidate != &h.last) h.promote();
      return h.last.slice;
    }
    return h.remember(address, size, data.data(), keepHistory, make);
  };
  Slice a = get(0x1000, p, true);
  Slice b = get(0x1000, p, true);
  assert(a.buffer == b.buffer && hits == 1 && slice_holds(arena, a, p.data(), p.size()));
  Slice c = get(0x2000, q, true);  // new key: 0x1000 moves to previous
  Slice d = get(0x1000, p, true);  // previous hit, promoted
  assert(hits == 2 && d.buffer == a.buffer && c.buffer != a.buffer);
  Slice e = get(0x2000, q, true);  // the promoted-away entry is still there
  assert(hits == 3 && e.buffer == c.buffer);
  // a changed payload under the same key replaces it
  std::vector<uint8_t> q2 = q;
  q2[100] ^= 1;
  Slice f = get(0x2000, q2, true);
  assert(hits == 3 && f.buffer != c.buffer && slice_holds(arena, f, q2.data(), q2.size()));
  Slice g = get(0x2000, q2, true);
  assert(hits == 4 && g.buffer == f.buffer);
  // history off: no previous entry
  h.reset();
  get(0x1000, p, false);
  get(0x2000, q, false);
  get(0x1000, p, false);
  assert(hits == 4);
  // reset keeps capacity but forgets the keys
  h.reset();
  assert(!h.last.slice.buffer && !h.previous.slice.buffer);
  printf("vertex snapshot history: ok\n");
}

void vertex_window() {
  Arena arena;
  VertexWindowEntry<Slice> entry;
  std::vector<uint8_t> data(512);
  for (size_t i = 0; i < data.size(); ++i) data[i] = uint8_t(i * 11);
  auto get = [&](uint32_t begin, uint32_t length, bool& hit) {
    hit = entry.matches(0x4000, 512, begin, length) && entry.equal(data.data() + begin);
    if (hit) return entry.slice;
    const uint8_t* copy = entry.remember(0x4000, 512, begin, length, data.data() + begin);
    std::vector<uint8_t> full(512, 0);
    std::memcpy(full.data() + begin, copy, length);
    entry.slice = arena.snapshot(full.data(), full.size());
    return entry.slice;
  };
  bool hit;
  Slice a = get(64, 128, hit);
  assert(!hit);
  Slice b = get(64, 128, hit);
  assert(hit && a.buffer == b.buffer);
  data[100] ^= 1;  // inside the window
  Slice c = get(64, 128, hit);
  assert(!hit && c.buffer != a.buffer && slice_holds(arena, c, data.data(), 0) &&
         !std::memcmp(arena.bytes(c).data() + 64, data.data() + 64, 128));
  data[300] ^= 1;  // outside the window: still a hit
  get(64, 128, hit);
  assert(hit);
  get(32, 128, hit);  // other window: miss
  assert(!hit);
  entry.clear();
  get(32, 128, hit);
  assert(!hit);
  printf("vertex window entry: ok\n");
}

}  // namespace

int main() {
  uniform_cache();
  vertex_history();
  vertex_window();
  printf("snapshot caches: all ok\n");
  return 0;
}
