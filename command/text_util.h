#pragma once

#include <string>

#include "aced.h"

namespace command_text
{
std::string toUtf8(const ACHAR* text);
std::basic_string<ACHAR> fromUtf8(const std::string& text);
}
