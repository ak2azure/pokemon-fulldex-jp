// The menus in Japanese: the game list, its settings, the pause menu and the messages, drawn
// with a 12-dot Japanese font (jpfont.h) beside the 5x7 one (ui.h's Glyph).
//
// The program's own strings stay English: several are compared or stored (a setting's name, a
// value), so they are changed only where they are drawn. Canvas::Text and TextWidth pass every
// string through Tr, which gives the Japanese for a whole string (i18n_ja.h), or for a string
// made of known parts ("< ON >", "SETTINGS: DISPLAY", "A: SELECT   B: BACK"), and leaves anything
// else as it is (a cheat's name, a ROM's).
//
// The language: list_language in the settings file (GAME LIST: LANGUAGE), "ja" (the default) or
// "en". Set by Launcher::LoadLook (the game list) and the game's settings (main.cpp).
#pragma once

#include <cctype>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace I18n
{

inline bool& JapaneseFlag()
{
    static bool on = true;
    return on;
}
inline bool Japanese() { return JapaneseFlag(); }
// A settings file's list_language value ("" when it has none: Japanese).
inline void SetLanguage(const std::string& v) { JapaneseFlag() = v != "en"; }

struct Pair
{
    const char* en;
    const char* ja;
};

} // namespace I18n

#include "i18n_ja.h" // (I18n::kWords, kPhrases)

namespace I18n
{

inline const std::unordered_map<std::string, std::string>& Words()
{
    static const std::unordered_map<std::string, std::string> m = [] {
        std::unordered_map<std::string, std::string> out;
        for (const Pair& p : kWords) out.emplace(p.en, p.ja);
        return out;
    }();
    return m;
}

inline bool AllAscii(const std::string& s)
{
    for (unsigned char c : s)
        if (c >= 0x80) return false;
    return true;
}
inline bool Digits(const std::string& s)
{
    if (s.empty()) return false;
    for (char c : s)
        if (!isdigit((unsigned char)c)) return false;
    return true;
}
inline bool StartsWith(const std::string& s, const char* p) { return s.compare(0, strlen(p), p) == 0; }
inline bool EndsWith(const std::string& s, const char* p)
{
    const size_t n = strlen(p);
    return s.size() >= n && s.compare(s.size() - n, n, p) == 0;
}

// The Japanese for s, or "" when there is none (TrOr keeps s then).
inline std::string Find(const std::string& s, int depth = 0)
{
    if (s.empty() || depth > 3) return "";
    std::string u = s;
    for (char& c : u) c = (char)toupper((unsigned char)c);
    const auto& w = Words();
    auto it = w.find(u);
    if (it != w.end()) return it->second;
    auto part = [&](const std::string& p) {
        const std::string t = Find(p, depth + 1);
        return t.empty() ? p : t;
    };
    // A value between arrows (the menus' chosen value).
    if (StartsWith(u, "< ") && EndsWith(u, " >") && u.size() > 4)
    {
        const std::string t = Find(u.substr(2, u.size() - 4), depth + 1);
        if (!t.empty()) return "< " + t + " >";
    }
    // Composed strings: a known start or end and the rest.
    static const struct { const char* en; const char* ja; bool prefix; } affixes[] = {
        { "SETTINGS: ", "設定: ", true },
        { " (NEXT START)", " (次回)", false },
        { ": COMING SOON", ": 近日対応", false },
        { "SAVE IN ", "セーブあり ", true },
        { "NEW IN ", "はじめから ", true },
        { "UNLOCKED ", "達成 ", true },
        { "SO FAR ", "進行 ", true },
        { "% OF PLAYERS", "%のプレイヤー", false },
    };
    for (const auto& a : affixes)
    {
        if (a.prefix && StartsWith(u, a.en) && u.size() > strlen(a.en))
            return a.ja + part(u.substr(strlen(a.en)));
        if (!a.prefix && EndsWith(u, a.en) && u.size() > strlen(a.en))
            return part(u.substr(0, u.size() - strlen(a.en))) + a.ja;
    }
    // A count and a word: "3 ON" (cheats), "12 PTS", "8 CHARACTERS".
    const size_t sp = u.find(' ');
    if (sp != std::string::npos && Digits(u.substr(0, sp)))
    {
        const std::string rest = u.substr(sp + 1), n = u.substr(0, sp);
        if (rest == "ON") return n + " オン";
        if (rest == "PTS") return n + "P";
        if (rest == "CHARACTERS") return n + "文字";
    }
    // NO DS GAMES YET (the game list, a console with none).
    if (StartsWith(u, "NO ") && EndsWith(u, " GAMES YET") && u.size() > 13)
        return u.substr(3, u.size() - 13) + "のゲームはまだありません";
    // A list of names (the settings groups' lines: "60 FPS / WORLD DETAIL / VOLUME.").
    if (EndsWith(u, ".") && u.size() > 1 && u.find(". ") == std::string::npos)
    {
        std::string out, body = u.substr(0, u.size() - 1);
        size_t at = 0;
        bool any = false;
        while (true)
        {
            const size_t next = body.find(" / ", at);
            const std::string p = body.substr(at, next == std::string::npos ? std::string::npos : next - at);
            const std::string t = Find(p, depth + 1);
            any |= !t.empty();
            out += (out.empty() ? "" : " / ") + (t.empty() ? p : t);
            if (next == std::string::npos) break;
            at = next + 3;
        }
        if (any) return out;
    }
    // The buttons' lines along the bottom ("A: SELECT   B: BACK"): their words.
    if (u.find(':') != std::string::npos)
    {
        std::string out = u;
        bool any = false;
        for (const Pair& p : kPhrases)
        {
            const size_t n = strlen(p.en);
            for (size_t at = out.find(p.en); at != std::string::npos; at = out.find(p.en, at))
            {
                // (Whole words only: the next character is not a letter.)
                const char after = at + n < out.size() ? out[at + n] : ' ';
                if (isalpha((unsigned char)after)) { at += n; continue; }
                out.replace(at, n, p.ja);
                at += strlen(p.ja);
                any = true;
            }
        }
        if (any) return out;
    }
    return "";
}

// s in the menus' language (as it is in English, or with no Japanese known for it).
inline std::string Tr(const std::string& s)
{
    if (!Japanese() || s.empty() || !AllAscii(s)) return s;
    // (Drawn every frame: kept once found. Few different strings are ever drawn.)
    static std::unordered_map<std::string, std::string> cache;
    auto it = cache.find(s);
    if (it != cache.end()) return it->second;
    if (cache.size() > 4096) cache.clear();
    std::string t = Find(s);
    if (t.empty()) t = s;
    cache.emplace(s, t);
    return t;
}

// The next character of a UTF-8 string from `at` (moved past it), or 0 at the end.
inline unsigned Next(const std::string& s, size_t& at)
{
    if (at >= s.size()) return 0;
    const unsigned char c = (unsigned char)s[at];
    int n = c < 0x80 ? 0 : c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    unsigned cp = n == 0 ? c : n == 1 ? (c & 0x1F) : n == 2 ? (c & 0x0F) : (c & 0x07);
    at++;
    for (; n > 0 && at < s.size() && ((unsigned char)s[at] & 0xC0) == 0x80; n--, at++) cp = (cp << 6) | ((unsigned char)s[at] & 0x3F);
    return cp;
}

} // namespace I18n
