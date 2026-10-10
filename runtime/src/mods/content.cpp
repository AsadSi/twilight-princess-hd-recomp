#include "content.h"
#include "mod_archive.h"
#include "mod_json.h"
#include <cstdio>
#include <fstream>
#include <vector>
#include <sstream>
#include <atomic>
#include <stdexcept>
namespace mods::content {
namespace fs=std::filesystem;
namespace {
Files active;
std::atomic<bool> present{false};
std::string lower(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c+= 'a'-'A';return s;}
void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
}
namespace {
fs::path game_root;
// the installed game's content files by lower-case file name -> game-relative paths (for loose imports)
std::multimap<std::string,std::string> game_names(){
    std::multimap<std::string,std::string> out;std::error_code ec;
    if(game_root.empty())return out;
    auto content=game_root/"content";if(!fs::is_directory(content,ec))return out;
    for(fs::recursive_directory_iterator it(content,ec),end;!ec&&it!=end;it.increment(ec))
        if(it->is_regular_file(ec))out.emplace(lower(it->path().filename().string()),it->path().lexically_relative(content).generic_string());
    return out;
}
}
void set_game_root(const fs::path& game){game_root=game;}
const fs::path& installed_game(){return game_root;}
void import_legacy(const fs::path& stage,const std::string& source_name){
    std::vector<fs::path> candidates;
    for(const auto& e:fs::recursive_directory_iterator(stage)){
        auto name=lower(e.path().filename().string());
        if(e.is_directory()){
            if(name=="content")candidates.push_back(e.path());
            require(name!="code"&&name!="meta"&&name!="aoc", "Only content replacements are supported; code, meta and DLC folders cannot be imported");
            if(name.size()==16&&name.starts_with("00050000"))require(name=="000500001019e500","This SDCafiine pack targets another game or region (requires Twilight Princess HD USA)");
        }else{
            auto ext=lower(e.path().extension().string());
            // Cemu code patches (patch_*.asm: hooks and code caves in the game's PowerPC code) can't run in the
            // recompiled game; importing only the files beside one would silently drop part of the mod.
            require(ext!=".asm","This mod includes a Cemu code patch (.asm), which this port cannot apply. To use only its content files, select its content folder instead");
            require(name!="patches.txt"&&!name.ends_with("_vs.txt")&&!name.ends_with("_ps.txt")&&ext!=".glsl"&&ext!=".rpx"&&ext!=".rpl", "This mod includes unsupported code patches or shaders; import the complete mod through a future adapter");
            if(name=="rules.txt"){
                require(e.file_size()<=1024*1024,"Oversized Cemu rules file");std::ifstream f(e.path());std::string text{std::istreambuf_iterator<char>(f),{}};auto rules=lower(text);
                // A file-only Cemu pack may have Definition metadata, but no patch/shader/texture rules.
                require(rules.find("[texture")==std::string::npos&&rules.find("[control")==std::string::npos&&rules.find("[preset")==std::string::npos,"Cemu texture/control/preset rules require an adapter; this import supports file-only packs");
                std::istringstream lines(rules);std::string line;
                while(std::getline(lines,line)){auto start=line.find_first_not_of(" \t\r");if(start!=std::string::npos&&line[start]=='['){auto end=line.find(']',start);require(end!=std::string::npos&&line.substr(start,end-start+1)=="[definition]","Only Definition metadata is supported in file-only Cemu packs");}}
                auto title=rules.find("titleids");if(title!=std::string::npos){auto end=rules.find('\n',title);auto line=rules.substr(title,end-title);require(line.find("000500001019e500")!=std::string::npos,"Cemu pack does not target Twilight Princess HD (USA)");}
            }
        }
    }
    // Loose files (a replaced archive such as res/Layout/Title2D.arc): a file whose name the installed game has
    // exactly once goes to that game path; names in several folders (bmgres.arc is in every res/Msg<suffix>/)
    // need a content/ tree. Other files (read-me texts, pictures) are not used.
    std::string mapped,ignored;
    if(candidates.empty()){
        auto names=game_names();std::vector<std::pair<fs::path,std::string>> moves;
        for(const auto& e:fs::recursive_directory_iterator(stage)){
            if(!e.is_regular_file())continue;auto name=lower(e.path().filename().string());
            auto hits=names.equal_range(name);auto n=std::distance(hits.first,hits.second);
            require(n<=1,"A loose file matches several game files; use a content/ tree with its game-relative path");
            if(n==1)moves.emplace_back(e.path(),hits.first->second);else ignored+=(ignored.empty()?"":", ")+e.path().filename().string();
        }
        require(!moves.empty(),"Select a single mod pack folder containing exactly one content/ directory");
        for(const auto& [from,to]:moves){
            auto dest=stage/"content"/fs::path(to);require(!fs::exists(dest),"Duplicate loose file name");
            fs::create_directories(dest.parent_path());fs::rename(from,dest);mapped+=(mapped.empty()?"":", ")+to;
        }
        candidates.push_back(stage/"content");
    }
    require(candidates.size()==1,"Select a single mod pack folder containing exactly one content/ directory");
    // Stable deterministic ID by source name. An explicit manifest can supply a different ID/version.
    auto name=fs::path(source_name).stem().string();require(!name.empty(),"Missing mod name");if(name.size()>128)name.resize(128);
    auto id=lower(name);for(char& c:id)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'))c='-';if(id.size()>55)id.resize(55);id="content."+id;
    json::Value m;m["format_version"]=1;m["id"]=id;m["name"]=name;m["version"]="1.0.0";m["game_id"]="tphd-usa";m["kind"]="content";m["minimum_manager_version"]="1.1.0";
    m["content_dir"]=candidates.front().lexically_relative(stage).generic_string();
    std::string description="Imported local content replacement. Requires restart. Model and archive compatibility must be checked in game.";
    if(!mapped.empty())description+=" Loose files placed at: "+mapped+".";
    if(!ignored.empty())description+=" Not used: "+ignored+".";
    m["description"]=description;
    std::ofstream out(stage/"manifest.json");out<<json::dump(m)<<'\n';out.close();require(bool(out),"Cannot write imported manifest");
}
Files index(const fs::path& directory){
    require(fs::is_directory(directory),"Content folder is missing");
    require(!fs::is_symlink(directory),"Content paths may not use symlinks");
    Files out;uint64_t total=0;size_t count=0;
    for(const auto& e:fs::recursive_directory_iterator(directory)){
        require(++count<=4096,"Too many content entries");require(!e.is_symlink(),"Content files may not use symlinks");
        if(e.is_directory())continue;
        require(e.is_regular_file(),"Unsupported content file type");require(!lower(e.path().filename().string()).starts_with(".deleted_"),"SDCafiine file hiding is not supported yet");auto n=e.file_size();total+=n;
        require(n<=128ull*1024*1024&&total<=512ull*1024*1024,"Content size limit exceeded");
        auto relative=e.path().lexically_relative(directory).generic_string();
        require(archive::relative_path(relative),"Invalid content path");
        require(out.emplace(lower(relative),e.path().string()).second,"Duplicate content path (case insensitive)");
    }
    require(!out.empty(),"Content mod contains no files");return out;
}
namespace {
// TP HD reads a content file only as far as the size its lists give: content/FileSizeList.txt (path, size) and
// DecompressedSizeList.txt (compressed size, size, ratio, "./DVDRoot/" and the path without ".gz"), every string
// ending in 0. With files replaced, the lists are written into `cache` with the replacements' sizes (a .gz's own size
// from its trailer) and served in their place.
void patch_size_lists(Files& files,const fs::path& cache){
    if(files.empty()||cache.empty()||game_root.empty())return;
    std::error_code ec;
    const auto size=[&](const std::string& path){auto n=fs::file_size(path,ec);return ec?std::string():std::to_string(n);};
    const auto gz_size=[&](const std::string& path){std::ifstream in(path,std::ios::binary);unsigned char b[4]={};in.seekg(-4,std::ios::end);
        return in.read((char*)b,4)?std::to_string(b[0]|b[1]<<8|b[2]<<16|(uint32_t)b[3]<<24):std::string();};
    for(const std::string name:{"FileSizeList.txt","DecompressedSizeList.txt"}){
        std::ifstream in(game_root/"content"/name,std::ios::binary);if(!in)continue;
        std::vector<std::string> f;for(std::string field;std::getline(in,field,'\0');)f.push_back(field);
        bool changed=false;
        const auto set=[&](std::string& field,const std::string& v){if(!v.empty()&&v!=field)field=v,changed=true;};
        if(name=="FileSizeList.txt"){
            for(size_t i=0;i+1<f.size();i+=2)if(auto it=files.find(lower(f[i]));it!=files.end())set(f[i+1],size(it->second));
        }else{
            for(size_t i=0;i+3<f.size();i+=4){
                if(!f[i+3].starts_with("./DVDRoot/"))continue;
                const auto path=lower(f[i+3].substr(10));
                if(auto it=files.find(path+".gz");it!=files.end())set(f[i],size(it->second)),set(f[i+1],gz_size(it->second));
                else if(auto raw=files.find(path);raw!=files.end())set(f[i],size(raw->second)),set(f[i+1],size(raw->second));
            }
        }
        if(!changed)continue;
        fs::create_directories(cache,ec);
        std::ofstream out(cache/name,std::ios::binary);for(const auto& field:f)out<<field<<'\0';
        if(out)files[lower(name)]=(cache/name).string();
    }
}
}
void activate(Files files,const fs::path& cache){require(!present.load(),"Content overrides already activated");patch_size_lists(files,cache);active=std::move(files);present.store(!active.empty(),std::memory_order_release);}
std::string replacement(const std::string& guest,const std::string& mode){
    if(!present.load(std::memory_order_acquire))return {};
    if(mode!="r"&&mode!="rb")return {}; // never redirect writers or update handles
    auto relative=guest;
    if(relative.starts_with("/vol/content/"))relative.erase(0,13);
    else if(relative.empty()||relative.front()=='/')return {};
    if(!archive::relative_path(relative))return {};
    auto key=lower(relative);auto it=active.find(key);if(it!=active.end())return it->second;
    return {};
}
}
