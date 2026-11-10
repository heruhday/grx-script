#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace selection_event
{
    void install();
    void uninstall();
    uint64_t getSelectionVersion();
    std::vector<std::string> getSelectedHandles();
}
