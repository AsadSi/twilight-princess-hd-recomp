// SPDX-License-Identifier: MPL-2.0
// The compile indicator's signal: a renderer notes each time a draw waited for a shader or pipeline compile, or was
// left out because one wasn't ready; the overlay shows "Compiling shaders" for a moment after the last one
// (overlay.cpp). Header only, so the renderers need not link the overlay.
#pragma once
#include <atomic>
#include <chrono>

namespace compile_notice {
inline double now_s() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline std::atomic<double> g_last{-1e9};
inline void note() { g_last.store(now_s(), std::memory_order_relaxed); }
// seconds since the last note
inline double since() { return now_s() - g_last.load(std::memory_order_relaxed); }
}  // namespace compile_notice
