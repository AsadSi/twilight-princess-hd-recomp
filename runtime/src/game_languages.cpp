// The languages TP HD shows its text in (game_languages.h).
#include "game_languages.h"

#include <cctype>
#include <cstdlib>

#include "overlay/hostui.h"
#include "runtime.h"

namespace game_lang {

const char* region_code(int region) { return region == kJapan ? "jp" : region == kEurope ? "eu" : "us"; }

int region_from_code(const std::string& code) {
    std::string s;
    for (char c : code) s += (char)tolower((unsigned char)c);
    if (s == "jp" || s == "jpn" || s == "japan") return kJapan;
    if (s == "us" || s == "usa") return kUsa;
    if (s == "eu" || s == "eur" || s == "europe") return kEurope;
    return 0;
}

const std::vector<Variant>& variants() {
    static const std::vector<Variant> v = {
        {1, kUsa, "English (North America)"}, {1, kEurope, "English (Europe)"},
        {2, kUsa, "French (North America)"},  {2, kEurope, "French (Europe)"},
        {5, kUsa, "Spanish (Latin America)"}, {5, kEurope, "Spanish (Europe)"},
        {3, kEurope, "German"},               {4, kEurope, "Italian"},
        {0, kJapan, "Japanese"},
    };
    return v;
}

const Variant& choose(int language, const std::string& region) {
    const int r = region_from_code(region);
    const Variant* first = nullptr;
    for (const Variant& v : variants()) {
        if (v.language != language) continue;
        if (v.region == r) return v;
        if (!first) first = &v;
    }
    return first ? *first : variants().front();
}

const Variant& current() {
    static const Variant& v = [&]() -> const Variant& {
        std::string language, region;
        const char* why = "WWHD_LANGUAGE";
        if (const char* e = getenv("WWHD_LANGUAGE"); e && *e) {
            language = e;
            if (const char* r = getenv("WWHD_LANGUAGE_REGION")) region = r;
        } else if (hostui::get("language", language) && !language.empty()) {
            hostui::get("language_region", region);
            why = "saved setting";
        }
        char* end = nullptr;
        const long code = language.empty() ? 1 : strtol(language.c_str(), &end, 10);
        if (!language.empty() && (*end || code < 0 || code >= kLanguages))
            LOG("[config] language %s (%s) is not a language code 0..11; using English", language.c_str(), why);
        const Variant& out = choose(language.empty() || *end || code < 0 || code >= kLanguages ? 1 : (int)code, region);
        LOG("[config] text language: %s (%s)", out.name, language.empty() ? "default" : why);
        return out;
    }();
    return v;
}

}  // namespace game_lang
