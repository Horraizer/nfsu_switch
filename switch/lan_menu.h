#pragma once

#include "nfsu/career.h"
#include <switch.h>
#include <functional>
#include <map>
#include <string>

using LanGarageEditor=std::function<void(const std::string&,unsigned,const std::function<void()>&)>;

// The caller owns the RomFS mount and releases its renderer while LAN runs.
void run_switch_lan(const std::string& root,PadState& pad,
                    const std::map<std::string,nfsu::CareerRide>& rides,const LanGarageEditor& edit);
