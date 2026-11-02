#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "text_util.h"

namespace command_text
{
std::string toUtf8(const ACHAR* text)
{
#ifdef UNICODE
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
    {
        return std::string();
    }

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, &result[0], size, nullptr, nullptr);
    result.pop_back();
    return result;
#else
    return std::string(text);
#endif
}

std::basic_string<ACHAR> fromUtf8(const std::string& text)
{
#ifdef UNICODE
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 1)
    {
        return std::basic_string<ACHAR>();
    }

    std::basic_string<ACHAR> result(static_cast<size_t>(size), ACHAR());
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &result[0], size);
    result.pop_back();
    return result;
#else
    return text;
#endif
}
}
