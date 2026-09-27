#include "nfsu/arg_path.h"
#include "nfsu/audio.h"
#include "nfsu/sha256.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace audio = nfsu::audio;

namespace {

struct Options {
    fs::path root;
    fs::path out;
    fs::path manifest;
    fs::path verify;
    std::string bank_filter;
    std::string stream_filter;
    unsigned sample = 0;
    unsigned segment = 0;
    bool write = false;
    bool has_sample = false;
    bool has_segment = false;
};

void put16(std::ostream& out, std::uint16_t value) {
    out.put(static_cast<char>(value & 255));
    out.put(static_cast<char>((value >> 8) & 255));
}

void put32(std::ostream& out, std::uint32_t value) {
    put16(out, static_cast<std::uint16_t>(value));
    put16(out, static_cast<std::uint16_t>(value >> 16));
}

void wav_header(std::ostream& out, std::uint32_t frames, std::uint32_t rate,
                std::uint32_t channels) {
    const std::uint64_t size = static_cast<std::uint64_t>(frames) * channels * 2;
    if (size > std::numeric_limits<std::uint32_t>::max() - 36U)
        throw std::runtime_error("WAV exceeds RIFF size limit");
    out.write("RIFF", 4); put32(out, static_cast<std::uint32_t>(36 + size));
    out.write("WAVEfmt ", 8); put32(out, 16); put16(out, 1);
    put16(out, static_cast<std::uint16_t>(channels)); put32(out, rate);
    put32(out, rate * channels * 2); put16(out, static_cast<std::uint16_t>(channels * 2));
    put16(out, 16); out.write("data", 4); put32(out, static_cast<std::uint32_t>(size));
}

std::vector<std::uint8_t> read_bank(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot open bank");
    const auto size = input.tellg();
    if (size < 0 || size > 32 * 1024 * 1024)
        throw std::runtime_error("bank too large");
    input.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), size))
        throw std::runtime_error("cannot read bank");
    return bytes;
}

bool in_repository(fs::path path) {
    path = fs::weakly_canonical(path);
    for (;;) {
        if (fs::exists(path / ".git")) return true;
        const auto parent = path.parent_path();
        if (parent == path) return false;
        path = parent;
    }
}

std::vector<fs::path> find_audio_files(const fs::path& root) {
    std::vector<fs::path> paths;
    for (const char* dir : {"Sound", "SNDSTREAMS", "SpeechStreams"}) {
        if (!fs::is_directory(root / dir))
            throw std::runtime_error(std::string("missing retail directory: ") + dir);
        for (const auto& item : fs::recursive_directory_iterator(root / dir)) {
            if (!item.is_regular_file()) continue;
            const auto ext = item.path().extension();
            if (ext == ".abk" || ext == ".ast") paths.push_back(item.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

std::string filename_for(const fs::path& relative, const char* kind, unsigned index) {
    std::ostringstream out;
    out << relative.generic_string() << '.' << kind << std::setfill('0')
        << std::setw(3) << index << ".wav";
    return out.str();
}

template <class Decoder>
void consume_pcm(const fs::path& wav, std::uint32_t frames, std::uint32_t rate,
                 std::uint32_t channels, nfsu::Sha256& sha, Decoder decode) {
    std::ofstream out;
    if (!wav.empty()) {
        fs::create_directories(wav.parent_path());
        out.open(wav, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot create WAV");
        wav_header(out, frames, rate, channels);
    }
    std::uint64_t written = 0;
    const audio::PcmSink sink = [&](const std::uint8_t* data, std::size_t size) {
        written += size;
        sha.update(data, size);
        if (out) out.write(reinterpret_cast<const char*>(data),
                           static_cast<std::streamsize>(size));
        if (out.is_open() && !out) throw std::runtime_error("WAV write failed");
    };
    decode(sink);
    if (written != static_cast<std::uint64_t>(frames) * channels * 2)
        throw std::runtime_error("decoded PCM size differs from header");
    if (out.is_open() && !out) throw std::runtime_error("WAV write failed");
}

unsigned number(const char* text) {
    const std::string s(text);
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("expected decimal index");
    const auto value = std::stoul(s);
    if (value > std::numeric_limits<unsigned>::max())
        throw std::runtime_error("index too large");
    return static_cast<unsigned>(value);
}

Options parse_options(int argc, char** argv) {
    if (argc < 3) throw std::runtime_error(
        "usage: nfsu_audio_dump <retail root> <out dir> [--write] [--bank text] "
        "[--sample index] [--stream text] [--segment index] "
        "[--manifest path | --verify path]");
    std::string reason;
    if (!nfsu::check_arguments(argc, argv, 1, reason))
        throw std::runtime_error(reason);
    Options o;
    o.root = nfsu::argument_path(argv[1]);
    o.out = nfsu::argument_path(argv[2]);
    for (int i = 3; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--write") o.write = true;
        else {
            if (++i == argc) throw std::runtime_error("missing option value");
            if (arg == "--bank") o.bank_filter = argv[i];
            else if (arg == "--stream") o.stream_filter = argv[i];
            else if (arg == "--sample") { o.sample = number(argv[i]); o.has_sample = true; }
            else if (arg == "--segment") { o.segment = number(argv[i]); o.has_segment = true; }
            else if (arg == "--manifest") o.manifest = nfsu::argument_path(argv[i]);
            else if (arg == "--verify") o.verify = nfsu::argument_path(argv[i]);
            else throw std::runtime_error("unknown option: " + arg);
        }
    }
    if (!o.manifest.empty() && !o.verify.empty())
        throw std::runtime_error("choose manifest or verify");
    if (o.write && in_repository(o.out))
        throw std::runtime_error("WAV output must be outside a Git repository");
    if (o.write) {
        const auto relative = fs::weakly_canonical(o.out).lexically_relative(
            fs::weakly_canonical(o.root));
        if (!relative.empty() && *relative.begin() != "..")
            throw std::runtime_error("WAV output must be outside retail root");
    }
    return o;
}

void run(const Options& o) {
    const auto paths = find_audio_files(o.root);
    const bool check_all = !o.manifest.empty() || !o.verify.empty();
    const bool bank_mode = !o.bank_filter.empty() || o.has_sample ||
        (o.stream_filter.empty() && !o.has_segment);
    const bool stream_mode = !o.stream_filter.empty() || o.has_segment ||
        (o.bank_filter.empty() && !o.has_sample);
    std::ostringstream manifest;
    std::size_t banks = 0, streams = 0, samples = 0, segments = 0;
    std::size_t absent_companions = 0;
    for (const auto& path : paths) {
        const auto rel = fs::relative(path, o.root);
        try {
            nfsu::Sha256 sha;
            std::uint64_t frames = 0;
            unsigned bank_count = 0, entry_count = 0, sample_count = 0, segment_count = 0;
            if (path.extension() == ".abk") {
                ++banks;
                const auto bytes = read_bank(path);
                const auto bank = audio::parse_abk(bytes);
                bank_count = 1;
                entry_count = static_cast<unsigned>(bank.entries.size());
                sample_count = static_cast<unsigned>(bank.samples.size());
                samples += sample_count;
                std::cout << rel.generic_string() << " modules=" << bank.modules
                          << " entries=" << bank.entries.size() << " samples="
                          << sample_count << '\n';
                if (bank.samples.empty()) {
                    auto companion = o.root / "SNDSTREAMS" / path.filename();
                    companion.replace_extension(".ast");
                    if (!fs::is_regular_file(companion)) {
                        companion = o.root / "SpeechStreams" / path.filename();
                        companion.replace_extension(".ast");
                    }
                    if (fs::is_regular_file(companion)) {
                        std::ifstream ast(companion, std::ios::binary);
                        const auto indexed = audio::scan_ast(ast);
                        std::set<std::uint64_t> offsets;
                        for (const auto& segment : indexed) offsets.insert(segment.offset);
                        for (const auto& entry : bank.entries) {
                            if (entry.type == 0 || !offsets.count(entry.value) ||
                                (entry.type == 2 && !offsets.count(entry.loop_value)))
                                throw std::runtime_error("stream entry misses companion segment");
                        }
                        std::cout << "  companion=" << fs::relative(companion, o.root).generic_string()
                                  << " linked_segments=" << indexed.size() << '\n';
                    } else {
                        ++absent_companions;
                        std::cout << "  companion AST absent in retail install\n";
                    }
                }
                for (const auto& s : bank.samples) {
                    std::cout << "  sample=" << s.index << " codec=" << static_cast<unsigned>(s.codec)
                              << " rate=" << s.rate << " channels=" << s.channels
                              << " frames=" << s.frames << " loop=" << s.loop_start
                              << ':' << s.loop_end << '\n';
                    const bool selected = o.write && bank_mode &&
                        (o.bank_filter.empty() ||
                         rel.generic_string().find(o.bank_filter) != std::string::npos) &&
                        (!o.has_sample || s.index == o.sample);
                    if (selected || check_all) {
                        fs::path wav;
                        if (selected)
                            wav = o.out / filename_for(rel, "sample", s.index);
                        consume_pcm(wav, s.frames, s.rate, s.channels, sha,
                            [&](const audio::PcmSink& sink) {
                                audio::decode_bank_sample(bytes, s, sink);
                            });
                    }
                    frames += s.frames;
                }
            } else {
                ++streams;
                std::ifstream input(path, std::ios::binary);
                if (!input) throw std::runtime_error("cannot open AST");
                const auto indexed = audio::scan_ast(input);
                segment_count = static_cast<unsigned>(indexed.size());
                segments += segment_count;
                std::cout << rel.generic_string() << " segments=" << segment_count << '\n';
                for (std::size_t i = 0; i < indexed.size(); ++i) {
                    const auto& s = indexed[i];
                    std::cout << "  segment=" << i << " offset=" << s.offset
                              << " codec=" << static_cast<unsigned>(s.codec) << " rate="
                              << s.rate << " channels=" << s.channels << " frames="
                              << s.frames << " blocks=" << s.blocks << '\n';
                    const bool selected = o.write && stream_mode &&
                        (o.stream_filter.empty() ||
                         rel.generic_string().find(o.stream_filter) != std::string::npos) &&
                        (!o.has_segment || i == o.segment);
                    if (selected || check_all) {
                        fs::path wav;
                        if (selected)
                            wav = o.out / filename_for(rel, "segment", static_cast<unsigned>(i));
                        consume_pcm(wav, s.frames, s.rate, s.channels, sha,
                            [&](const audio::PcmSink& sink) {
                                audio::decode_ast_segment(input, s, sink);
                            });
                    }
                    frames += s.frames;
                }
            }
            if (!o.manifest.empty() || !o.verify.empty())
                manifest << rel.generic_string() << ' ' << bank_count << ' ' << entry_count
                         << ' ' << sample_count << ' ' << segment_count << ' ' << frames << ' '
                         << nfsu::sha256_hex(sha.finish()) << '\n';
        } catch (const std::exception& e) {
            throw std::runtime_error(rel.generic_string() + ": " + e.what());
        }
    }
    std::cout << "total banks=" << banks << " samples=" << samples
              << " streams=" << streams << " segments=" << segments
              << " absent_companions=" << absent_companions << '\n';
    if (!o.manifest.empty()) {
        std::ofstream file(o.manifest, std::ios::binary | std::ios::trunc);
        if (!file) throw std::runtime_error("cannot create manifest");
        file << manifest.str();
        if (!file) throw std::runtime_error("manifest write failed");
    }
    if (!o.verify.empty()) {
        std::ifstream file(o.verify, std::ios::binary);
        if (!file) throw std::runtime_error("cannot open expected manifest");
        const std::string expected((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        std::string normalized;
        normalized.reserve(expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (expected[i] == '\r' && i + 1 < expected.size() &&
                expected[i + 1] == '\n') continue;
            normalized.push_back(expected[i]);
        }
        if (normalized != manifest.str())
            throw std::runtime_error("retail decoded PCM manifest differs");
        std::cout << "retail decoded PCM manifest matches\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        run(parse_options(argc, argv));
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "audio dump: " << e.what() << '\n';
        return 1;
    }
}
