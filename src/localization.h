#pragma once
#include <initializer_list>
#include <string>
#include <vector>

namespace soundbridge {
enum class Language { English, SimplifiedChinese, TraditionalChinese, Korean, Japanese, Count };
enum class Text {
    None,
#define SB_TEXT(key, en, cn, tw, ko, ja) key,
#include "translations.inc"
#undef SB_TEXT
    Count
};
Language languageFromCode(const std::wstring& code, Language fallback = Language::English);
Language systemLanguage();
Language currentLanguage() noexcept;
void setLanguage(Language language) noexcept;
const wchar_t* languageCode(Language language) noexcept;
const wchar_t* languageName(Language language) noexcept;
const wchar_t* tr(Text key, Language language = currentLanguage()) noexcept;
std::wstring format(Text key, const std::vector<std::wstring>& arguments, Language language = currentLanguage());
std::string utf8(const std::wstring& text);

// Keep message IDs and arguments, so existing errors/results change language
// without retrying capture or making another network request.
struct Message {
    Text key = Text::None;
    std::vector<std::wstring> arguments;
    std::wstring literal;
    Message() = default;
    Message(Text id, std::initializer_list<std::wstring> values = {}) : key(id), arguments(values) {}
    Message(const wchar_t* value) : literal(value) {}
    Message(std::wstring value) : literal(std::move(value)) {}
    bool empty() const noexcept { return key == Text::None && literal.empty(); }
    void clear() { key = Text::None; arguments.clear(); literal.clear(); }
    std::wstring str(Language language = currentLanguage()) const { return key == Text::None ? literal : format(key, arguments, language); }
};
}
