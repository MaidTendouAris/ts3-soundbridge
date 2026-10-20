#include "localization.h"
#include "settings.h"
#include "update_checker.h"
#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <map>

using namespace soundbridge;
namespace {
int failures = 0;
void check(bool value, const char* label) { if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; } }
std::map<wchar_t, int> placeholders(const std::wstring& text) {
    std::map<wchar_t, int> result;
    for (std::size_t i = 0; i + 2 < text.size(); ++i)
        if (text[i] == L'{' && text[i + 1] >= L'0' && text[i + 1] <= L'9' && text[i + 2] == L'}') ++result[text[i + 1]];
    return result;
}
}
int main() {
    check(languageFromCode(L"en-US") == Language::English && languageFromCode(L"JA-jp") == Language::Japanese &&
        languageFromCode(L"ko-KR") == Language::Korean, "case-insensitive regional UI language matching");
    for (const auto* code : {L"zh-TW", L"zh-HK", L"zh-MO", L"zh-Hant", L"zh-Hant-HK"})
        check(languageFromCode(code) == Language::TraditionalChinese, "traditional Chinese variants");
    for (const auto* code : {L"zh-CN", L"zh-SG", L"zh-Hans", L"zh-Hans-CN"})
        check(languageFromCode(code) == Language::SimplifiedChinese, "simplified Chinese variants");
    check(languageFromCode(L"fr-FR") == Language::English && languageFromCode(L"invalid", Language::Japanese) == Language::Japanese,
        "unsupported system language and corrupt preference use fallback");
    const auto file = (std::filesystem::current_path() / "localization-test-settings.ini").wstring();
    DeleteFileW(file.c_str());
    check(loadSettings(file).language == systemLanguage(), "first launch follows Windows UI language");
    const auto retained = parseReleaseResponse(503, "", "1.1.3");
    const Message appError{Text::CaptureAppError, {L"player {1}.exe", L"80004005"}};
    for (int i = 0; i < static_cast<int>(Language::Count); ++i) {
        const auto language = static_cast<Language>(i); setLanguage(language);
        Settings settings; settings.language = language; settings.microphonePercent = 231; settings.showLevelMeters = true;
        settings.applications = {L"D:\\音楽\\플레이어.exe"};
        check(saveSettings(file, settings), "save language with other preferences");
        const auto loaded = loadSettings(file);
        check(loaded.language == language && loaded.microphonePercent == 231 && loaded.showLevelMeters && loaded.applications == settings.applications,
            "each language survives settings round trip without losing paths/gain/preferences");
        check(languageFromCode(languageCode(language)) == language && *languageName(language), "stable language codes and native names");
        for (int key = 1; key < static_cast<int>(Text::Count); ++key) {
            const auto id = static_cast<Text>(key); const std::wstring value = tr(id);
            check(!value.empty(), "every language has every message");
            check(placeholders(value) == placeholders(tr(id, Language::English)), "translations preserve all placeholder occurrences");
            const auto encoded = utf8(value);
            const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, encoded.data(), static_cast<int>(encoded.size()), nullptr, 0);
            std::wstring roundTrip(static_cast<std::size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, encoded.data(), static_cast<int>(encoded.size()), roundTrip.data(), length);
            check(roundTrip == value, "SDK text has valid lossless UTF-8");
        }
        check(retained.message.str() == format(Text::UpdateHttpError, {L"503"}), "stored update errors retranslate without a network request");
        const auto error = appError.str();
        check(error.find(L"player {1}.exe") != std::wstring::npos && error.find(L"80004005") != std::wstring::npos,
            "formatting preserves executable names with placeholder-like braces");
    }
    WritePrivateProfileStringW(L"SoundBridge", L"Language", L"invalid", file.c_str());
    check(loadSettings(file).language == systemLanguage(), "invalid persisted language follows system default");
    setLanguage(static_cast<Language>(999)); check(currentLanguage() == Language::English, "invalid runtime language falls back to English");
    check(std::wstring(tr(Text::Start, static_cast<Language>(-1))) == tr(Text::Start, Language::English), "invalid explicit language cannot index outside catalog");
    DeleteFileW(file.c_str());
    if (failures) return 1;
    std::cout << "All five-language catalog, encoding, fallback, formatting and persistence tests passed.\n";
}
