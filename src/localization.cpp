#include "localization.h"
#include <Windows.h>
#include <atomic>
#include <cwctype>

namespace soundbridge {
namespace {
constexpr const wchar_t* catalog[][static_cast<int>(Language::Count)] = {
    {L"", L"", L"", L"", L""},
#define SB_TEXT(key, en, cn, tw, ko, ja) {en, cn, tw, ko, ja},
#include "translations.inc"
#undef SB_TEXT
};
static_assert(sizeof(catalog) / sizeof(catalog[0]) == static_cast<int>(Text::Count));
std::atomic<Language> selected{systemLanguage()};
int index(Language language) noexcept {
    const int result = static_cast<int>(language);
    return result >= 0 && result < static_cast<int>(Language::Count) ? result : 0;
}
}
Language languageFromCode(const std::wstring& code, Language fallback) {
    std::wstring lower = code;
    for (auto& character : lower) character = static_cast<wchar_t>(std::towlower(character));
    if (lower == L"zh-tw" || lower == L"zh-hk" || lower == L"zh-mo" || lower == L"zh-hant" || lower.rfind(L"zh-hant-", 0) == 0)
        return Language::TraditionalChinese;
    if (lower == L"zh" || lower == L"zh-cn" || lower == L"zh-sg" || lower == L"zh-hans" || lower.rfind(L"zh-hans-", 0) == 0)
        return Language::SimplifiedChinese;
    if (lower == L"en" || lower.rfind(L"en-", 0) == 0) return Language::English;
    if (lower == L"ko" || lower.rfind(L"ko-", 0) == 0) return Language::Korean;
    if (lower == L"ja" || lower.rfind(L"ja-", 0) == 0) return Language::Japanese;
    return fallback;
}
Language systemLanguage() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if (!LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0)) return Language::English;
    return languageFromCode(name);
}
Language currentLanguage() noexcept { return selected.load(std::memory_order_relaxed); }
void setLanguage(Language language) noexcept { selected.store(static_cast<Language>(index(language)), std::memory_order_relaxed); }
const wchar_t* languageCode(Language language) noexcept {
    constexpr const wchar_t* codes[] = {L"en", L"zh-CN", L"zh-TW", L"ko", L"ja"};
    return codes[index(language)];
}
const wchar_t* languageName(Language language) noexcept {
    constexpr const wchar_t* names[] = {L"English", L"简体中文", L"繁體中文", L"한국어", L"日本語"};
    return names[index(language)];
}
const wchar_t* tr(Text key, Language language) noexcept {
    const int row = static_cast<int>(key);
    if (row < 0 || row >= static_cast<int>(Text::Count)) return L"";
    return catalog[row][index(language)];
}
std::wstring format(Text key, const std::vector<std::wstring>& arguments, Language language) {
    const std::wstring pattern = tr(key, language);
    std::wstring result;
    // Substitute in one pass: user-owned executable names may contain braces.
    for (std::size_t i = 0; i < pattern.size();) {
        if (i + 2 < pattern.size() && pattern[i] == L'{' && pattern[i + 1] >= L'0' && pattern[i + 1] <= L'9' && pattern[i + 2] == L'}') {
            const auto argument = static_cast<std::size_t>(pattern[i + 1] - L'0');
            if (argument < arguments.size()) result += arguments[argument];
            else result.append(pattern, i, 3);
            i += 3;
        } else result += pattern[i++];
    }
    return result;
}
std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
}
