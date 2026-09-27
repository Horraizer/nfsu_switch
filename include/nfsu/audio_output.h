#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace nfsu::audio {

// Platform output owns its device thread. The callback fills exactly the
// requested count of 48 kHz stereo signed 16-bit frames.
class Output {
public:
    using Render = std::function<void(std::int16_t*, std::size_t)>;
    Output();
    ~Output();
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    bool start(Render render, std::string& error);
    void stop();
    std::string error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nfsu::audio
