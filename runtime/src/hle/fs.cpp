// coreinit FS and nn_save: map Wii U volume paths onto host directories.
//   /vol/content/...  -> <game>/content/...
//   /vol/code/...     -> <game>/code/...
//   /vol/meta/...     -> <game>/meta/...
//   /vol/save/...     -> <save dir>/...
#include "../platform/filesystem.h"

#include <array>
#include <filesystem>
#include <cstdio>
#ifndef _WIN32
#include <strings.h>
#endif
#include <mutex>
#include <string>
#include <unordered_map>

#include "../runtime.h"
#include "../write_watch.h"
#include "../mods/content.h"

namespace {

enum FSStatus : int32_t {
    FS_OK = 0,
    FS_END = -2,
    FS_EXISTS = -5,
    FS_NOT_FOUND = -6,
    FS_NOT_FILE = -7,
    FS_NOT_DIR = -8,
    FS_ACCESS_ERROR = -10,
};

struct OpenFile {
    FILE* f;
    std::string path;  // guest path
    std::string mode;
};
struct OpenDir {
    DIR* d;
    std::string path;  // host path
    std::string gpath;
    uint32_t read = 0;  // entries returned so far
};

std::mutex g_fs_mutex;
std::unordered_map<uint32_t, OpenFile> g_files;
std::unordered_map<uint32_t, OpenDir> g_dirs;
uint32_t g_next_handle = 1;

std::string host_path_exact(const std::string& guest) {
    std::string p = guest;
    auto map = [&](const char* prefix, const std::string& root) -> bool {
        size_t n = strlen(prefix);
        if (p.compare(0, n, prefix) == 0) {
            p = root + p.substr(n);
            return true;
        }
        return false;
    };
    if (map("/vol/content", config::game_dir + "/content")) return p;
    if (map("/vol/code", config::game_dir + "/code")) return p;
    if (map("/vol/meta", config::game_dir + "/meta")) return p;
    if (map("/vol/save", config::save_dir)) return p;
    if (!p.empty() && p[0] != '/') return config::game_dir + "/content/" + p;  // relative to cwd (/vol/content)
    return config::game_dir + p;
}

#ifndef _WIN32
// Wii U volumes are case-insensitive (a request for res/object/... opens the disc's res/Object/...);
// case-sensitive host file systems (Linux, case-sensitive APFS) need the path resolved one component
// at a time. Exact matches cost one stat(); resolved directories are cached. Components that don't
// exist yet (new save files) keep the guest's spelling.
std::string resolve_case(const std::string& p) {
    struct stat st;
    if (stat(p.c_str(), &st) == 0) return p;
    static std::mutex mu;
    static std::unordered_map<std::string, std::string> dirs;  // lower-cased dir path -> host dir path
    std::lock_guard<std::mutex> lk(mu);
    auto lower = [](std::string s) { for (char& ch : s) ch = (char)tolower((unsigned char)ch); return s; };
    std::string cur = p.compare(0, 1, "/") == 0 ? "/" : "";
    size_t pos = cur.size();
    while (pos <= p.size()) {
        size_t e = p.find('/', pos);
        if (e == std::string::npos) e = p.size();
        std::string comp = p.substr(pos, e - pos);
        pos = e + 1;
        if (comp.empty()) { if (e == p.size()) break; continue; }
        std::string base = cur.empty() ? "" : (cur == "/" ? "/" : cur + "/");
        std::string cand = base + comp;
        std::string key = lower(cand);
        if (auto it = dirs.find(key); it != dirs.end()) { cur = it->second; continue; }
        if (stat(cand.c_str(), &st) != 0) {
            std::string found;
            if (DIR* d = opendir(cur.empty() ? "." : cur.c_str())) {
                while (dirent* de = readdir(d))
                    if (!strcasecmp(de->d_name, comp.c_str())) { found = de->d_name; break; }
                closedir(d);
            }
            if (found.empty()) return cand + (e < p.size() ? p.substr(e) : "");  // not there: keep the rest as asked
            cand = base + found;
        }
        if (e < p.size()) dirs[key] = cand;  // only directories are cached
        cur = cand;
    }
    return cur;
}
#endif

std::string host_path(const std::string& guest) {
#ifdef _WIN32
    return host_path_exact(guest);  // Windows file systems are case-insensitive
#else
    return resolve_case(host_path_exact(guest));
#endif
}

// Optional content mods affect read-only content access only: a content mod's file comes first, then the
// installed game.
std::string read_path(const std::string& guest,const std::string& mode="rb") {
    auto override=mods::content::replacement(guest,mode);
    if(!override.empty())if(const char* trace=std::getenv("WWHD_TEST_CONTENT_TRACE");trace&&std::string(trace)=="1")LOG("[content-mod] read %s -> %s",guest.c_str(),override.c_str());
    return override.empty()?host_path(guest):override;
}

void make_parent_dirs(const std::string& path) {
    std::error_code ec; std::filesystem::create_directories(std::filesystem::path(path).parent_path(),ec);
}

void fill_stat(uint32_t out, const struct stat& st) {
    memset(mem::ptr(out), 0, 0x64);
    bool dir = S_ISDIR(st.st_mode);
    st32(out + 0x00, dir ? 0x80000000u : 0x01000000u);
    st32(out + 0x04, 0x666);
    st32(out + 0x10, dir ? 0 : (uint32_t)st.st_size);
    st32(out + 0x14, dir ? 0 : (uint32_t)st.st_size);
}

int32_t open_file(const std::string& gpath, const std::string& mode, uint32_t out_handle) {
    std::string hp = read_path(gpath,mode);
    if (mode.find_first_of("wa") != std::string::npos) make_parent_dirs(hp);
    std::string m = mode;
    if (m.find('b') == std::string::npos) m += "b";
    FILE* f = fopen(hp.c_str(), m.c_str());
    TRACE("[fs] open %s (%s) -> %s", gpath.c_str(), mode.c_str(), f ? "ok" : "not found");
    if (!f) return FS_NOT_FOUND;
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    uint32_t h = g_next_handle++;
    g_files[h] = {f, gpath, m};
    st32(out_handle, h);
    return FS_OK;
}

FILE* file(uint32_t h) {
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    auto it = g_files.find(h);
    return it == g_files.end() ? nullptr : it->second.f;
}

int32_t stat_path(const std::string& gpath, uint32_t out) {
    struct stat st;
    if (stat(read_path(gpath).c_str(), &st) != 0) return FS_NOT_FOUND;
    fill_stat(out, st);
    return FS_OK;
}

int32_t open_dir(const std::string& gpath, uint32_t out_handle) {
    DIR* d = opendir(host_path(gpath).c_str());
    TRACE("[fs] opendir %s -> %s", gpath.c_str(), d ? "ok" : "not found");
    if (!d) return FS_NOT_FOUND;
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    uint32_t h = g_next_handle++;
    g_dirs[h] = {d, host_path(gpath), gpath};
    st32(out_handle, h);
    return FS_OK;
}

}  // namespace

// ---------------------------------------------------------------- FS
HLE(coreinit, FSInit) {}
HLE(coreinit, FSShutdown) {}
HLE(coreinit, FSAddClient) { ret(c, 0); }
HLE(coreinit, FSDelClient) { ret(c, 0); }
HLE(coreinit, FSInitCmdBlock) { memset(mem::ptr(arg(c, 0)), 0, 0xA80); }
HLE(coreinit, FSSetCmdPriority) { ret(c, 0); }
HLE(coreinit, FSSetStateChangeNotification) {}
HLE(coreinit, FSGetVolumeState) { ret(c, 1); }  // FS_VOLSTATE_READY
HLE(coreinit, FSGetLastError) { ret(c, 0); }
HLE(coreinit, FSGetLastErrorCodeForViewer) { ret(c, 0); }
HLE(coreinit, FSGetCwd) { mem::write_cstr(arg(c, 2), "/vol/content", arg(c, 3)); ret(c, FS_OK); }

HLE(coreinit, FSOpenFile) {
    ret(c, open_file(mem::read_cstr(arg(c, 2)), mem::read_cstr(arg(c, 3)), arg(c, 4)));
}

// -> elements read; pos >= 0 reads from there (FSReadFileWithPos)
static int32_t read_file(uint32_t dst, uint32_t size, uint32_t count, uint32_t handle, int64_t pos = -1) {
    FILE* f = file(handle);
    if (!f || size == 0) return 0;
    size_t n;
    {
        BlockingScope b;  // the calling thread waits for the disc; others on its core run
        if (pos >= 0 && host::file_seek(f, pos, SEEK_SET) != 0) return FS_ACCESS_ERROR;
        // the kernel writes guest memory here: write-protected texture pages would fail the read
        // (EFAULT) instead of faulting into the write tracker (write_watch.h)
        wwatch::HostWrite w(dst, (uint32_t)std::min<uint64_t>((uint64_t)size * count, 0x100000000ull - dst));
        n = fread(mem::ptr(dst), 1, (size_t)size * count, f);
    }
    return (int32_t)(n / size);
}

HLE(coreinit, FSReadFile) { ret(c, (uint32_t)read_file(arg(c, 2), arg(c, 3), arg(c, 4), arg(c, 5))); }
// (client, block, dst, size, count, pos, handle, flags, error mask)
HLE(coreinit, FSReadFileWithPos) { ret(c, (uint32_t)read_file(arg(c, 2), arg(c, 3), arg(c, 4), arg(c, 6), arg(c, 5))); }

static int32_t close_file(uint32_t handle) {
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    auto it = g_files.find(handle);
    if (it != g_files.end()) {
        fclose(it->second.f);
        g_files.erase(it);
    }
    return FS_OK;
}
HLE(coreinit, FSCloseFile) { ret(c, close_file(arg(c, 2))); }

// Async calls run at once and complete through their FSAsyncParams (callback, context, message
// queue): the FSAsyncResult lives in the command block (the OS's own part of it), and with a queue its
// OSMessage (message = the result) is posted there for FSGetAsyncResult; else the callback runs
// before the call returns.
bool os_send_message(uint32_t queue, const std::array<uint32_t, 4>& msg, uint32_t flags);  // threads.cpp
static constexpr uint32_t kAsyncResult = 0xA40;  // in the 0xA80-byte FSCmdBlock
static void async_done(Cpu* c, uint32_t client, uint32_t block, int32_t status, uint32_t params) {
    uint32_t cb = ld32(params), ctx = ld32(params + 4), queue = ld32(params + 8), r = block + kAsyncResult;
    st32(r + 0x00, cb);
    st32(r + 0x04, ctx);
    st32(r + 0x08, queue);
    st32(r + 0x0C, r);  // ioMsg: message = this result
    st32(r + 0x10, 0);
    st32(r + 0x14, 0);
    st32(r + 0x18, 0);
    st32(r + 0x1C, client);
    st32(r + 0x20, block);
    st32(r + 0x24, (uint32_t)status);
    if (queue) os_send_message(queue, {r, 0, 0, 0}, 1);
    else if (cb) guest_call(c, cb, {client, block, (uint32_t)status, ctx});
    ret(c, FS_OK);
}
HLE(coreinit, FSGetAsyncResult) { ret(c, ld32(arg(c, 0))); }  // (OSMessage*) -> FSAsyncResult*
// (client, block, path, mode, handle*, error mask, params)
HLE(coreinit, FSOpenFileAsync) {
    int32_t r = open_file(mem::read_cstr(arg(c, 2)), mem::read_cstr(arg(c, 3)), arg(c, 4));
    async_done(c, arg(c, 0), arg(c, 1), r, arg(c, 6));
}
// (client, block, dst, size, count, pos, handle, flags; error mask and params on the stack)
HLE(coreinit, FSReadFileWithPosAsync) {
    int32_t r = read_file(arg(c, 2), arg(c, 3), arg(c, 4), arg(c, 6), arg(c, 5));
    async_done(c, arg(c, 0), arg(c, 1), r, ld32(c->r[1] + 0xC));
}
// (client, block, handle, error mask, params)
HLE(coreinit, FSCloseFileAsync) { async_done(c, arg(c, 0), arg(c, 1), close_file(arg(c, 2)), arg(c, 4)); }
HLE(coreinit, FSGetErrorCodeForViewer) { ret(c, 0); }

HLE(coreinit, FSWriteFile) {
    uint32_t src = arg(c, 2), size = arg(c, 3), count = arg(c, 4);
    FILE* f = file(arg(c, 5));
    if (!f || size == 0) { ret(c, 0); return; }
    size_t n;
    {
        BlockingScope b;
        n = fwrite(mem::ptr(src), 1, (size_t)size * count, f);
        fflush(f);
    }
    ret(c, (uint32_t)(n / size));
}

HLE(coreinit, FSSetPosFile) {
    FILE* f = file(arg(c, 2));
    ret(c, f && host::file_seek(f,arg(c,3),SEEK_SET) == 0 ? FS_OK : FS_ACCESS_ERROR);
}

HLE(coreinit, FSGetStat) { ret(c, stat_path(mem::read_cstr(arg(c, 2)), arg(c, 3))); }

HLE(coreinit, FSGetStatFile) {
    FILE* f = file(arg(c, 2));
    struct stat st;
    if (!f || fstat(fileno(f), &st) != 0) { ret(c, FS_NOT_FOUND); return; }
    fill_stat(arg(c, 3), st);
    ret(c, FS_OK);
}

HLE(coreinit, FSOpenDir) { ret(c, open_dir(mem::read_cstr(arg(c, 2)), arg(c, 3))); }

HLE(coreinit, FSReadDir) {
    uint32_t out = arg(c, 3);
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    auto it = g_dirs.find(arg(c, 2));
    if (it == g_dirs.end()) { ret(c, FS_NOT_DIR); return; }
    struct dirent* de;
    while ((de = readdir(it->second.d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        struct stat st;
        auto path=it->second.path + "/" + de->d_name;
        auto replacement=mods::content::replacement(it->second.gpath + "/" + de->d_name);
        if(!replacement.empty())path=std::move(replacement);
        if (stat(path.c_str(), &st) != 0) continue;
        fill_stat(out, st);
        mem::write_cstr(out + 0x64, de->d_name, 256);
        it->second.read++;
        ret(c, FS_OK);
        return;
    }
    ret(c, FS_END);
}

HLE(coreinit, FSCloseDir) {
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    auto it = g_dirs.find(arg(c, 2));
    if (it != g_dirs.end()) {
        closedir(it->second.d);
        g_dirs.erase(it);
    }
    ret(c, FS_OK);
}

// ---------------------------------------------------------------- nn_save
// Saves live in <save dir>/<account slot or "common">/<path>
static std::string save_path(uint32_t slot, const std::string& p) {
    std::string base = slot == 0xFF ? "/vol/save/common" : "/vol/save/user";
    return base + (p.empty() || p[0] == '/' ? "" : "/") + p;
}

HLE(nn_save, SAVEInit) { ret(c, 0); }
HLE(nn_save, SAVEShutdown) {}
HLE(nn_save, SAVEInitSaveDir) {
    make_parent_dirs(host_path(save_path(arg(c, 0), "x")));
    ret(c, 0);
}
HLE(nn_save, SAVEFlushQuota) { ret(c, 0); }
HLE(nn_save, SAVEOpenFile) {
    ret(c, open_file(save_path(arg(c, 2), mem::read_cstr(arg(c, 3))), mem::read_cstr(arg(c, 4)), arg(c, 5)));
}
// (client, block, slot, path, error mask): a file, or a folder with what is in it
HLE(nn_save, SAVERemove) {
    std::error_code ec;
    std::string hp = host_path(save_path(arg(c, 2), mem::read_cstr(arg(c, 3))));
    auto n = std::filesystem::remove_all(hp, ec);  // (uintmax_t)-1 on error
    ret(c, ec ? FS_ACCESS_ERROR : n > 0 ? FS_OK : FS_NOT_FOUND);
}
HLE(nn_save, SAVERollbackQuota) { ret(c, 0); }
HLE(nn_save, SAVEGetStat) { ret(c, stat_path(save_path(arg(c, 2), mem::read_cstr(arg(c, 3))), arg(c, 4))); }
HLE(nn_save, SAVEOpenDir) {
    std::string gp = save_path(arg(c, 2), mem::read_cstr(arg(c, 3)));
    make_parent_dirs(host_path(gp) + "/");
    ret(c, open_dir(gp, arg(c, 4)));
}

// ---------------------------------------------------------------- save states: open files
#include <algorithm>
#include <vector>

#include "../savestate.h"
void fs_ss_save(ss::Writer& w) {
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    w.u32(g_next_handle);
    std::vector<uint32_t> keys;
    for (auto& [h, f] : g_files) keys.push_back(h);
    std::sort(keys.begin(), keys.end());
    w.u32((uint32_t)keys.size());
    for (uint32_t h : keys) {
        OpenFile& f = g_files[h];
        w.u32(h);
        w.str(f.path);
        w.str(f.mode);
        w.u64((uint64_t)host::file_tell(f.f));
    }
    keys.clear();
    for (auto& [h, d] : g_dirs) keys.push_back(h);
    std::sort(keys.begin(), keys.end());
    w.u32((uint32_t)keys.size());
    for (uint32_t h : keys) {
        w.u32(h);
        w.str(g_dirs[h].gpath);
        w.u32(g_dirs[h].read);
    }
}

void fs_ss_load(ss::Reader& r) {
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    uint32_t next = r.u32();
    for (auto& [h, f] : g_files) fclose(f.f);
    g_files.clear();
    for (auto& [h, d] : g_dirs) closedir(d.d);
    g_dirs.clear();
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n && r.ok; i++) {
        uint32_t h = r.u32();
        std::string gp = r.str(), mode = r.str();
        uint64_t pos = r.u64();
        // reopening must not truncate or create: writers continue in update mode
        std::string m = mode.find_first_of("wa+") != std::string::npos ? "r+b" : "rb";
        FILE* f = fopen(read_path(gp,m).c_str(), m.c_str());
        if (!f) {
            LOG("[savestate] cannot reopen %s", gp.c_str());
            continue;
        }
        host::file_seek(f,pos,SEEK_SET);
        g_files[h] = {f, gp, mode};
    }
    n = r.u32();
    for (uint32_t i = 0; i < n && r.ok; i++) {
        uint32_t h = r.u32();
        std::string gp = r.str();
        uint32_t read = r.u32();
        DIR* d = opendir(host_path(gp).c_str());
        if (!d) continue;
        OpenDir od{d, host_path(gp), gp, 0};
        while (od.read < read) {
            struct dirent* de = readdir(d);
            if (!de) break;
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            od.read++;
        }
        g_dirs[h] = od;
    }
    g_next_handle = std::max(g_next_handle, next);
}
