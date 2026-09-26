#pragma once
#include "nfsu/input.h"
#include <functional>
#include <string>
#include <vector>
namespace nfsu {
bool run_race_window(const std::function<void(const GamepadFrame&)>& tick,
                     const std::function<std::vector<unsigned char>()>& render,
                     std::string& error, bool smoke = false);
}
