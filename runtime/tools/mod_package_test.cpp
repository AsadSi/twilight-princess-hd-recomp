// Standalone host-side tests; no game files, player settings or guest code needed.
#include <cassert>
#include <cstdlib>
#include <string>

namespace {
void env(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value ? value : "");
#else
    if (value) setenv(key,value,1); else unsetenv(key);
#endif
}
}
#include "mods/packages.h"
#include "mods/content.h"
#include "mods/cemu_pack.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
namespace {
mods::packages::View view(const std::string& id) {for(auto v:mods::packages::list())if(v.id==id)return v;return {};}
// Second process on the same storage: a confirmed native package loads after a restart without asking.
int restart_check(const char* storage) {
    using namespace mods::packages;
    env("WWHD_NO_HOST_INPUT","1");env("WWHD_MOD_MANAGER_DIR",storage);env("WWHD_TEST_TRUST_NATIVE_MODS",nullptr);
    initialize();std::string error;
    assert(view("fixture").enabled && view("fixture").native_confirmed && unconfirmed_native("fixture").empty());
    frame(1);assert(view("fixture").active && view("fixture").status=="changed");
    assert(enable("fixture",false,error));frame(2);assert(!view("fixture").active);
    return 0;
}
}
int main(int argc, char** argv) {
    namespace fs=std::filesystem;
    using namespace mods::packages;
    if(argc == 3 && std::string(argv[1]) == "--restart") return restart_check(argv[2]);
    if(argc==2&&(std::string(argv[1])=="--cemu-startup"||std::string(argv[1])=="--cemu-backend")){
        bool backend=std::string(argv[1])=="--cemu-backend";
        auto root=fs::temp_directory_path()/("wwhd-cemu-startup-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        auto storage=root/"storage",pack=storage/"Mods"/"cemu.test";fs::create_directories(pack);
        std::ofstream(pack/"manifest.json")<<R"({"format_version":1,"id":"cemu.test","name":"Test","version":"1.0.0","game_id":"tphd-usa","kind":"cemu","cemu_dir":""})";
        std::ofstream(pack/"rules.txt")<<"[Definition]\nname=Test\ntitleIds=000500001019e500\nversion=4\n[Preset]\nname=Normal\n$scale=1\n[Preset]\nname=Double\n$scale=2\n[TextureRedefine]\nwidth=1280\nheight=720\noverwriteWidth=1280*$scale\n";
        std::ofstream(storage/"profiles.json")<<R"({"format_version":1,"active":"Default","profiles":{"Default":{"enabled":{"cemu.test":true},"config":{"cemu.test":{"preset-0":"Double"}}}}})";
        if(backend){
            std::ofstream(pack/"0000000000000001_0000000000000002_ps.txt")<<"#version 420\nvoid main(){}\n";
            auto content=storage/"Mods"/"content.test";fs::create_directories(content/"content"/"res"/"Object");
            std::ofstream(content/"content"/"res"/"Object"/"test.arc")<<"synthetic content";
            std::ofstream(content/"manifest.json")<<R"({"format_version":1,"id":"content.test","name":"Content","version":"1.0.0","game_id":"tphd-usa","kind":"content","content_dir":"content"})";
            std::ofstream(storage/"profiles.json")<<R"({"format_version":1,"active":"Default","profiles":{"Default":{"enabled":{"cemu.test":true,"content.test":true}}}})";
        }
        env("WWHD_NO_HOST_INPUT","1");env("WWHD_MOD_MANAGER_DIR",storage.string().c_str());initialize();
        if(backend){
            assert(!mods::content::replacement("res/Object/test.arc").empty());
            for(const auto& view:list())if(view.id=="cemu.test")assert(!view.active&&!view.compatible&&view.enabled);
            std::string error;assert(enable("cemu.test",false,error));assert(remove("cemu.test",error));
            assert(!mods::content::replacement("res/Object/test.arc").empty());fs::remove_all(root);
            std::cout<<"Unavailable shader backend preserves content and permits disabling shader packs\n";return 0;
        }
        uint32_t width=0,height=0;assert(mods::cemu::texture_extent(1280,720,0x80e,1,4,width,height)&&width==2560&&height==720);
        std::string error;assert(list().at(0).active&&!list().at(0).pending_restart);
        assert(configure("cemu.test","preset-0","Normal",error));assert(list().at(0).pending_restart);
        assert(mods::cemu::texture_extent(1280,720,0x80e,1,4,width,height)&&width==2560);
        assert(enable("cemu.test",false,error));frame(100);assert(list().at(0).active&&list().at(0).pending_restart);
        assert(!remove("cemu.test",error));assert(!install(pack.string(),error));fs::remove_all(root);
        std::cout<<"Cemu startup presets and restart-only immutable lifecycle passed\n";return 0;
    }
    if(argc==2&&std::string(argv[1])=="--content-startup"){
        auto root=fs::temp_directory_path()/("wwhd-content-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        auto storage=root/"storage";auto pack=storage/"Mods"/"content.test";
        fs::create_directories(pack/"content"/"res"/"Object");
        std::ofstream(pack/"content"/"res"/"Object"/"fixture.arc")<<"synthetic replacement";
        fs::create_directories(pack/"content"/"res"/"Msgus");std::ofstream(pack/"content"/"res"/"Msgus"/"bmgres.arc")<<"RARCsynthetic translation";
        std::ofstream(pack/"manifest.json")<<R"({"format_version":1,"id":"content.test","name":"Test","version":"1.0.0","game_id":"tphd-usa","kind":"content","content_dir":"content"})";
        // a profile saved before 2026-10-09 still has Wind Waker's built-in mod choices: ignored
        std::ofstream(storage/"profiles.json")<<R"({"format_version":1,"active":"Default","profiles":{"Default":{"enabled":{"content.test":true},"builtins":{"quick-doors":true}}}})";
        env("WWHD_NO_HOST_INPUT","1");env("WWHD_MOD_MANAGER_DIR",storage.string().c_str());
        assert(mods::content::replacement("/vol/content/res/Object/fixture.arc").empty());initialize();
        auto file=mods::content::replacement("/vol/content/RES/object/FIXTURE.arc");assert(file==(pack/"content"/"res"/"Object"/"fixture.arc").string());
        assert(mods::content::replacement("res/Object/fixture.arc")==file);
        for(auto path:{"/vol/save/res/Object/fixture.arc","/vol/code/res/Object/fixture.arc","/vol/contentX/res/Object/fixture.arc","/vol/content/../res/Object/fixture.arc","res/../res/Object/fixture.arc","res\\Object\\fixture.arc"})assert(mods::content::replacement(path).empty());
        for(auto mode:{"w","a","r+","r+b","wb"})assert(mods::content::replacement("res/Object/fixture.arc",mode).empty());
        assert(mods::content::replacement("res/Object/absent.arc").empty());
        // a translated message archive replaces that language's archive only
        auto translation=(pack/"content"/"res"/"Msgus"/"bmgres.arc").string();
        assert(mods::content::replacement("/vol/content/res/Msgus/bmgres.arc")==translation);
        for(auto other:{"res/Msgusfr/bmgres.arc","res/Msguk/bmgres.arc","res/Msgus/bmgres1.arc"})
            assert(mods::content::replacement(std::string("/vol/content/")+other).empty());
        assert(mods::content::replacement("/vol/content/res/Msgus/bmgres.arc","wb").empty());
        std::string error;assert(list().at(0).active&&list().at(0).restart_required);assert(enable("content.test",false,error));frame(100);
        assert(list().at(0).active&&!list().at(0).enabled);assert(mods::content::replacement("res/Object/fixture.arc")==file);
        assert(!remove("content.test",error));assert(!install(pack.string(),error));
        // Profile changes also retain the startup content snapshot.
        assert(create_profile("Other",error));assert(select_profile("Other",error));frame(101);assert(mods::content::replacement("res/Object/fixture.arc")==file);
        fs::remove_all(root);std::cout<<"Startup overrides, read-only routing, boundaries, and restart lifecycle passed\n";return 0;
    }
    assert(argc == 3 || argc == 4);
    env("WWHD_TEST_TRUST_NATIVE_MODS",nullptr);
    auto root=fs::path(argv[1])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    assert(!fs::exists(root));
    fs::create_directories(root);
    env("WWHD_NO_HOST_INPUT","1");
    env("WWHD_MOD_MANAGER_DIR",(root/"storage").string().c_str());
    initialize();
    std::string error;
    // the fixture library under other IDs, for the checks that need no particular mod
    auto package=[&](const char* id, const char* extra) {
        auto path=root/id;fs::create_directories(path);fs::copy_file(argv[2],path/"fixture.dylib");
        std::ofstream(path/"manifest.json") << "{\"format_version\":1,\"id\":\"" << id
          << "\",\"name\":\"" << id << "\",\"version\":\"1.0.0\",\"game_id\":\"tphd-usa\",\"kind\":\"native\",\"abi_version\":1,"
          << "\"binaries\":{\"" << platform_key() << "\":\"fixture.dylib\"}" << extra << "}";
        return path.string();
    };
    assert(install(package("basic",""),error));
    assert(list().size()==1 && list()[0].id=="basic" && confirm_native("basic",error));
    assert(enable("basic",true,error));frame(1);assert(list()[0].active);
    assert(!remove("basic",error));
    assert(enable("basic",false,error));frame(2);assert(!list()[0].active);
    assert(create_profile("Adventure",error));
    assert(enable("basic",true,error));frame(20);assert(list()[0].active);
    assert(select_profile("Adventure",error));frame(21);assert(!list()[0].active);
    assert(current_profile()=="Adventure");assert(!delete_profile("Adventure",error));
    assert(select_profile("Default",error));assert(delete_profile("Adventure",error));
    assert(install(package("missing-dep",",\"dependencies\":[{\"id\":\"absent\"}]"),error));
    assert(!enable("missing-dep",true,error));assert(error.find("Missing dependency")!=std::string::npos);
    assert(remove("missing-dep",error));
    assert(install(package("cycle-a",",\"dependencies\":[{\"id\":\"cycle-b\"}]"),error));
    assert(install(package("cycle-b",",\"dependencies\":[{\"id\":\"cycle-a\"}]"),error));
    assert(!enable("cycle-a",true,error));assert(error.find("cycle")!=std::string::npos);
    assert(remove("cycle-a",error));assert(remove("cycle-b",error));
    assert(install(package("conflicting",",\"conflicts\":[\"basic\"]"),error));assert(confirm_native("conflicting",error));
    assert(!enable("conflicting",true,error));assert(error.find("conflicts")!=std::string::npos);assert(remove("conflicting",error));
    assert(enable("basic",false,error));assert(remove("basic",error));
    assert(install(package("typed",R"(,"options":[{"id":"toggle","name":"Toggle","type":"bool","default":false},{"id":"rate","name":"Rate","type":"number","min":1,"max":10,"default":2},{"id":"mode","name":"Mode","type":"enum","choices":["a","b"],"default":"a"}])"),error));
    assert(configure("typed","toggle",true,error));assert(!configure("typed","toggle",1,error));
    assert(configure("typed","rate",5,error));assert(!configure("typed","rate",11,error));
    assert(configure("typed","mode","b",error));assert(!configure("typed","mode","c",error));
    assert(install((root/"typed").string(),error));assert(remove("typed",error));
    auto bad=root/"bad.wwhdmod";std::ofstream(bad)<<"not a package";assert(!install(bad.string(),error));
    auto native=root/"native";fs::create_directories(native);
    fs::copy_file(argv[2],native/"fixture.dylib");
    std::ofstream(native/"manifest.json") << "{\"format_version\":1,\"id\":\"fixture\",\"name\":\"Fixture\",\"version\":\"1.0.0\",\"game_id\":\"tphd-usa\",\"kind\":\"native\",\"abi_version\":1,\"binaries\":{\""
      << platform_key() << "\":\"fixture.dylib\"},\"options\":[{\"id\":\"label\",\"name\":\"Label\",\"type\":\"string\",\"default\":\"initial\"}]}";
    assert(install(native.string(),error));
    auto find=[] {return view("fixture");};
    auto storage=root/"storage";
    auto trusted=[&] {std::ifstream f(storage/"profiles.json");std::string text{std::istreambuf_iterator<char>(f),{}};
        auto value=mods::json::parse(text);return value.get("native_trust").get("fixture").string();};
    // Unconfirmed native code: enable() refuses and nothing loads until the player confirms.
    assert(!find().native_confirmed);
    auto pending=unconfirmed_native("fixture");assert(pending.size()==1 && pending[0].first=="fixture" && pending[0].second=="Fixture");
    assert(!enable("fixture",true,error));assert(error.find("native code")!=std::string::npos);
    frame(3);assert(!find().active && !find().enabled);
    // A package that requires the native package names both in the one confirmation.
    assert(install(package("needs-native",",\"dependencies\":[{\"id\":\"fixture\"}]"),error));
    pending=unconfirmed_native("needs-native");assert(pending.size()==2 && pending[0].first=="fixture" && pending[1].first=="needs-native");
    assert(!enable("needs-native",true,error));assert(remove("needs-native",error));
    // Test aid: pre-confirmed only in isolated test runs, and never written to profiles.json.
    env("WWHD_TEST_TRUST_NATIVE_MODS","other,fixture");assert(find().native_confirmed && unconfirmed_native("fixture").empty());
    env("WWHD_TEST_TRUST_NATIVE_MODS",nullptr);assert(!find().native_confirmed && trusted().empty());
    assert(confirm_native("fixture",error));assert(trusted().size()==64);
    assert(find().native_confirmed && unconfirmed_native("fixture").empty());
    assert(enable("fixture",true,error));frame(6);
    assert(find().active && find().status=="initial");
    assert(configure("fixture","label","changed",error));frame(4);assert(find().status=="changed");
    assert(!configure("fixture","label",42,error));
    assert(enable("fixture",false,error));frame(5);assert(!find().active);
    // Confirmed: a new process loads it from the saved profile without asking again.
    assert(enable("fixture",true,error));frame(7);assert(find().active);
    std::string restart="\""+std::string(argv[0])+"\" --restart \""+storage.string()+"\"";
#ifdef _WIN32
    restart="\""+restart+"\"";  // cmd.exe /c drops the outer quotes of a line that starts with one
#endif
    assert(std::system(restart.c_str())==0);
    // That process disabled it; this one keeps its own view until told, so follow the saved state.
    assert(enable("fixture",false,error));frame(8);assert(!find().active);
    // A changed library under the same ID asks again, including when a profile switch would load it.
    assert(create_profile("Native",error));assert(select_profile("Native",error));
    assert(enable("fixture",true,error));frame(9);assert(find().active);
    assert(select_profile("Default",error));frame(10);assert(!find().active);
    auto before=trusted();
    std::ofstream(native/"fixture.dylib",std::ios::binary|std::ios::app) << "changed build";
    assert(install(native.string(),error));assert(!find().native_confirmed && trusted()==before);
    assert(select_profile("Native",error));frame(11);
    assert(!find().active && !find().enabled && find().reason.find("native code you have not confirmed")!=std::string::npos);
    assert(unconfirmed_native("fixture").size()==1 && !enable("fixture",true,error));
    assert(confirm_native("fixture",error));assert(trusted()!=before);
    assert(enable("fixture",true,error));frame(12);assert(find().active && find().reason.empty());
    assert(enable("fixture",false,error));frame(13);assert(select_profile("Default",error));assert(delete_profile("Native",error));
    // Removing forgets the confirmation; reinstalling asks again.
    assert(remove("fixture",error));assert(trusted().empty());
    assert(install(native.string(),error));assert(!find().native_confirmed);
    assert(remove("fixture",error));
    assert(list().empty());
    if(argc == 4) {
        assert(install(argv[3],error));
        auto id=list().at(0).id;
        if(list()[0].kind=="native") {assert(!enable(id,true,error));assert(confirm_native(id,error));}
        assert(enable(id,true,error));frame(40);assert(list()[0].active);
        assert(configure(id,"label","ZIP works",error));frame(41);
        assert(list()[0].status.starts_with("ZIP works"));
        assert(enable(id,false,error));frame(42);assert(remove(id,error));
    }
    auto graphics=root/"CemuResolution";fs::create_directories(graphics);
    std::ofstream(graphics/"rules.txt")<<"[Definition]\nname=Resolution\ntitleIds=000500001019e500\nversion=4\n[Preset]\nname=Normal\n$scale=1\n[Preset]\nname=Double\n$scale=2\n[TextureRedefine]\nwidth=1280\nheight=720\noverwriteWidth=1280*$scale\noverwriteHeight=720*$scale\n";
    assert(install(graphics.string(),error));auto graphicsView=list().at(0);
    assert(graphicsView.kind=="cemu"&&graphicsView.restart_required&&graphicsView.options.size()==1);
    assert(graphicsView.native_confirmed&&unconfirmed_native(graphicsView.id).empty()&&!confirm_native(graphicsView.id,error)); // no native code: never asks
    assert(configure(graphicsView.id,"preset-0","Double",error));
    assert(!configure(graphicsView.id,"preset-0","Unknown",error));
    assert(enable(graphicsView.id,true,error));frame(45);
    assert(!list().at(0).active&&list().at(0).pending_restart);
    assert(enable(graphicsView.id,false,error));assert(remove(graphicsView.id,error));
    std::ofstream(graphics/"0000000000000001_0000000000000002_ps.txt")<<"#version 420\nvoid main(){}\n";
    assert(install(graphics.string(),error));assert(!list().at(0).compatible);
    assert(!enable(list().at(0).id,true,error));assert(remove(list().at(0).id,error));
    auto legacy=root/"LegacyModel";fs::create_directories(legacy/"content"/"res"/"Object");
    std::ofstream(legacy/"content"/"res"/"Object"/"test.arc")<<"synthetic model archive";
    assert(install(legacy.string(),error));auto imported=list().at(0);assert(imported.id=="content.legacymodel"&&imported.restart_required&&!imported.active);assert(imported.native_confirmed&&unconfirmed_native(imported.id).empty());
    assert(enable(imported.id,true,error));frame(50);assert(!list().at(0).active); // waits for restart
    auto second=root/"OtherModel";fs::create_directories(second/"content"/"res"/"Object");std::ofstream(second/"content"/"res"/"Object"/"test.arc")<<"synthetic conflicting archive";
    assert(install(second.string(),error));assert(!enable("content.othermodel",true,error));assert(error.find("Content file conflict")!=std::string::npos);
    assert(enable(imported.id,false,error));frame(51);assert(remove(imported.id,error));assert(remove("content.othermodel",error));
    // A fan translation's title screen as loose files (names the installed game has once, a read-me): each goes
    // to its game path, the read-me is not used. Its message archive is in every res/Msg<suffix>/: refused loose.
    auto game=root/"game";for(auto dir:{"Msgus","Msguk","Layout"})fs::create_directories(game/"content"/"res"/dir);
    for(auto dir:{"Msgus","Msguk"})std::ofstream(game/"content"/"res"/dir/"bmgres.arc")<<"original";
    std::ofstream(game/"content"/"res"/"Layout"/"Title2D.arc")<<"original";std::ofstream(game/"content"/"res"/"Layout"/"Title2D.pack.gz")<<"original";
    mods::content::set_game_root(game);
    auto translation=root/"FanTranslation";fs::create_directories(translation/"inner");
    std::ofstream(translation/"inner"/"Title2D.arc")<<"RARCsynthetic layout";std::ofstream(translation/"Title2D.pack.gz")<<"synthetic textures";
    std::ofstream(translation/"readme.txt")<<"text";
    assert(install(translation.string(),error));{auto v=list().at(0);assert(v.id=="content.fantranslation");
        assert(v.description.find("res/Layout/Title2D.arc")!=std::string::npos&&v.description.find("res/Layout/Title2D.pack.gz")!=std::string::npos);
        assert(v.description.find("Not used: readme.txt")!=std::string::npos);assert(remove(v.id,error));}
    std::ofstream(translation/"bmgres.arc")<<"RARCsynthetic translation";assert(!install(translation.string(),error));assert(error.find("several game files")!=std::string::npos);
    // the content folder of a mod selected on its own: its game paths, named after the mod
    fs::create_directories(translation/"content"/"res"/"Msgus");fs::rename(translation/"bmgres.arc",translation/"content"/"res"/"Msgus"/"bmgres.arc");
    assert(install((translation/"content").string(),error));assert(list().at(0).id=="content.fantranslation");assert(remove(list().at(0).id,error));
    mods::content::set_game_root({});
    auto invalid=root/"CodeMod";fs::create_directories(invalid/"content");std::ofstream(invalid/"content"/"dummy")<<"fixture";std::ofstream(invalid/"patches.txt")<<"code";assert(!install(invalid.string(),error));
    fs::remove(invalid/"patches.txt");fs::create_directories(invalid/"graphicPacks"/"Patch");std::ofstream(invalid/"graphicPacks"/"Patch"/"rules.txt")<<"[Definition]\ntitleIds = 000500001019e500\n";
    std::ofstream(invalid/"graphicPacks"/"Patch"/"patch_code.asm")<<"[Code]\n";assert(!install(invalid.string(),error));assert(error.find("code patch")!=std::string::npos);
    fs::remove_all(invalid/"graphicPacks");std::ofstream(invalid/"rules.txt")<<"[Definition]\ntitleIds = 0005000010143600\n";assert(!install(invalid.string(),error));
    std::ofstream(invalid/"rules.txt",std::ios::trunc)<<"[Definition]\ntitleIds = 000500001019e500\n[TextureRedefine]\n";assert(!install(invalid.string(),error));
    fs::remove(invalid/"rules.txt");std::ofstream(invalid/"content"/".deleted_dummy")<<"";assert(!install(invalid.string(),error));
    auto duplicate=root/"Duplicate";fs::create_directories(duplicate/"content"/"res"/"Object");fs::create_directories(duplicate/"content"/"res"/"object");
    std::ofstream(duplicate/"content"/"res"/"Object"/"A.arc")<<"fixture";std::ofstream(duplicate/"content"/"res"/"object"/"a.arc")<<"fixture";
    // A case-sensitive volume can represent the conflict; a case-insensitive volume collapses it.
    if(std::distance(fs::directory_iterator(duplicate/"content"/"res"),fs::directory_iterator{})==2)assert(!install(duplicate.string(),error));
    std::cout << "Package install, profiles, dependencies, native load/config/unload passed\n";
}
