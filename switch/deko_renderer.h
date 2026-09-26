#pragma once

#include "nfsu/draw_commands.h"
#include <memory>

namespace nfsu {

// Switch-only owner of the device, queue, resident uploads and swapchain.
// The borrowed commands and their scene must stay alive through close().
// All work uses one queue; presentation waits for each frame's fence before
// reusing command memory. This deliberately starts with a static stock frame.
class DekoRenderer {
public:
    DekoRenderer();
    ~DekoRenderer();
    DekoRenderer(const DekoRenderer&) = delete;
    DekoRenderer& operator=(const DekoRenderer&) = delete;
    bool open(const RecordedCommands& commands, std::string& error);
    bool present(std::string& error);
    bool close(std::string& error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nfsu
