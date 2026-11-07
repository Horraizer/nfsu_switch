#include "nfsu/audio_output.h"

namespace nfsu::audio {

struct Output::Impl {};
Output::Output() : impl_(std::make_unique<Impl>()) {}
Output::~Output() = default;
bool Output::start(Render, std::string& error) {
    error = "audio output unavailable on this platform";
    return false;
}
void Output::stop() {}
std::string Output::error() const { return {}; }
Output::Stats Output::stats() const noexcept { return {}; }

} // namespace nfsu::audio
