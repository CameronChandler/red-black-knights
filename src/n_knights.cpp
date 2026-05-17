#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <windows.h>
#include <wincodec.h>

struct Coord {
    int64_t x = 0;
    int64_t y = 0;
    bool operator==(const Coord& o) const noexcept { return x == o.x && y == o.y; }
};

struct CoordHash {
    size_t operator()(const Coord& c) const noexcept {
        const uint64_t h1 = static_cast<uint64_t>(c.x) * 0x9e3779b185ebca87ULL;
        const uint64_t h2 = static_cast<uint64_t>(c.y) * 0xc2b2ae3d27d4eb4fULL;
        return static_cast<size_t>(h1 ^ (h2 + 0x9e3779b185ebca87ULL + (h1 << 6U) + (h1 >> 2U)));
    }
};

struct Placement {
    uint64_t n = 0;
    Coord c;
    uint32_t player = 0;
};

struct GameState {
    std::string rules;
    uint32_t players = 0;
    uint64_t moves_requested = 0;
    uint64_t moves_completed = 0;
    std::vector<Placement> placements;
};

static constexpr Coord kKnightOffsets[8] = {
    {1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}
};

[[nodiscard]] static Coord n_to_coord(uint64_t n) {
    if (n == 0) throw std::runtime_error("n must be >= 1");
    if (n == 1) return {0, 0};
    const long double root = std::sqrt(static_cast<long double>(n));
    const uint64_t k = static_cast<uint64_t>(std::ceil((root - 1.0L) / 2.0L));
    const uint64_t side = 2ULL * k;
    const uint64_t t = 2ULL * k + 1ULL;
    const uint64_t m = t * t;
    const uint64_t d = m - n;
    const int64_t kk = static_cast<int64_t>(k);
    if (d < side) return {kk - static_cast<int64_t>(d), -kk};
    if (d < 2ULL * side) return {-kk, -kk + static_cast<int64_t>(d - side)};
    if (d < 3ULL * side) return {-kk + static_cast<int64_t>(d - 2ULL * side), kk};
    return {kk, kk - static_cast<int64_t>(d - 3ULL * side)};
}

static void add_attacks(const Coord& from, std::unordered_map<Coord, uint32_t, CoordHash>& attacks) {
    for (const Coord& d : kKnightOffsets) {
        Coord t{from.x + d.x, from.y + d.y};
        auto it = attacks.find(t);
        if (it == attacks.end()) attacks.emplace(t, 1U);
        else ++(it->second);
    }
}

[[nodiscard]] static bool is_legal(
    uint32_t player,
    const Coord& target,
    const std::unordered_set<Coord, CoordHash>& occupied,
    const std::vector<std::unordered_map<Coord, uint32_t, CoordHash>>& attack_maps
) {
    if (occupied.find(target) != occupied.end()) return false;
    for (uint32_t p = 0; p < attack_maps.size(); ++p) {
        if (p == player) continue;
        const auto it = attack_maps[p].find(target);
        if (it != attack_maps[p].end() && it->second > 0) return false;
    }
    return true;
}

struct SimResult {
    GameState state;
    std::vector<std::unordered_map<Coord, uint32_t, CoordHash>> attack_maps;
};

[[nodiscard]] static SimResult simulate_n_knights(uint32_t players, uint64_t moves) {
    if (players < 2) throw std::runtime_error("players must be >= 2");

    SimResult result;
    result.state.rules = "n_knights";
    result.state.players = players;
    result.state.moves_requested = moves;
    result.state.moves_completed = 0;
    result.state.placements.reserve(static_cast<size_t>(moves));
    result.attack_maps.resize(players);

    std::unordered_set<Coord, CoordHash> occupied;
    occupied.reserve(static_cast<size_t>(moves) * 2ULL + 1ULL);
    std::vector<uint64_t> cursors(players, 1ULL);

    for (uint64_t turn = 0; turn < moves; ++turn) {
        const uint32_t p = static_cast<uint32_t>(turn % players);
        uint64_t n = cursors[p];
        while (true) {
            const Coord c = n_to_coord(n);
            if (is_legal(p, c, occupied, result.attack_maps)) {
                occupied.insert(c);
                result.state.placements.push_back(Placement{n, c, p});
                add_attacks(c, result.attack_maps[p]);
                cursors[p] = n + 1ULL;
                ++result.state.moves_completed;
                break;
            }
            ++n;
        }
    }

    return result;
}

static void save_state_json(const GameState& state, const std::string& out_path) {
    const std::filesystem::path outp(out_path);
    if (outp.has_parent_path()) std::filesystem::create_directories(outp.parent_path());

    std::ofstream out(out_path, std::ios::binary);
    if (!out) throw std::runtime_error("failed to open state output");

    out << "{\n";
    out << "  \"rules\": \"" << state.rules << "\",\n";
    out << "  \"players\": " << state.players << ",\n";
    out << "  \"moves_requested\": " << state.moves_requested << ",\n";
    out << "  \"moves_completed\": " << state.moves_completed << ",\n";
    out << "  \"placements\": [\n";
    for (size_t i = 0; i < state.placements.size(); ++i) {
        const Placement& p = state.placements[i];
        out << "    {\"n\": " << p.n
            << ", \"x\": " << p.c.x
            << ", \"y\": " << p.c.y
            << ", \"player\": " << p.player << "}";
        if (i + 1 < state.placements.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
}

[[nodiscard]] static std::string read_file_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("failed to open file");
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

[[nodiscard]] static uint64_t extract_u64(const std::string& txt, const std::string& key, uint64_t fallback) {
    const std::regex re("\\\"" + key + "\\\"\\s*:\\s*(\\d+)");
    std::smatch m;
    if (std::regex_search(txt, m, re)) return static_cast<uint64_t>(std::stoull(m[1].str()));
    return fallback;
}

[[nodiscard]] static GameState load_state_json(const std::string& path) {
    const std::string txt = read_file_all(path);
    GameState s;
    s.rules = "n_knights";
    s.players = static_cast<uint32_t>(extract_u64(txt, "players", 2));
    s.moves_requested = extract_u64(txt, "moves_requested", 0);
    s.moves_completed = extract_u64(txt, "moves_completed", 0);

    const std::regex re("\\{\\s*\\\"n\\\"\\s*:\\s*(\\d+)\\s*,\\s*\\\"x\\\"\\s*:\\s*(-?\\d+)\\s*,\\s*\\\"y\\\"\\s*:\\s*(-?\\d+)\\s*,\\s*\\\"player\\\"\\s*:\\s*(\\d+)\\s*\\}");
    auto b = std::sregex_iterator(txt.begin(), txt.end(), re);
    auto e = std::sregex_iterator();
    for (auto it = b; it != e; ++it) {
        const std::smatch& m = *it;
        Placement p;
        p.n = static_cast<uint64_t>(std::stoull(m[1].str()));
        p.c.x = std::stoll(m[2].str());
        p.c.y = std::stoll(m[3].str());
        p.player = static_cast<uint32_t>(std::stoul(m[4].str()));
        s.placements.push_back(p);
    }
    if (s.moves_completed == 0) s.moves_completed = static_cast<uint64_t>(s.placements.size());
    if (s.moves_requested == 0) s.moves_requested = s.moves_completed;
    return s;
}

struct RGB { uint8_t r, g, b; };

[[nodiscard]] static RGB hsv_to_rgb(float h, float s, float v) {
    const float c = v * s;
    const float hh = h / 60.0f;
    const float x = c * (1.0f - std::fabs(std::fmod(hh, 2.0f) - 1.0f));
    float r1 = 0, g1 = 0, b1 = 0;
    if (0.0f <= hh && hh < 1.0f) { r1 = c; g1 = x; }
    else if (1.0f <= hh && hh < 2.0f) { r1 = x; g1 = c; }
    else if (2.0f <= hh && hh < 3.0f) { g1 = c; b1 = x; }
    else if (3.0f <= hh && hh < 4.0f) { g1 = x; b1 = c; }
    else if (4.0f <= hh && hh < 5.0f) { r1 = x; b1 = c; }
    else { r1 = c; b1 = x; }
    const float m = v - c;
    return {
        static_cast<uint8_t>(std::clamp((r1 + m) * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp((g1 + m) * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp((b1 + m) * 255.0f, 0.0f, 255.0f))
    };
}

[[nodiscard]] static std::vector<RGB> build_palette(uint32_t players) {
    std::vector<RGB> colors;
    colors.reserve(players);

    const RGB seed[] = {
        {255, 0, 0},   // red
        {0, 0, 0},     // black
        {0, 180, 0},   // green
        {0, 90, 255},  // blue
        {255, 220, 0}, // yellow
        {150, 0, 180}, // purple
        {255, 120, 0}  // orange
    };

    for (uint32_t i = 0; i < players; ++i) {
        if (i < sizeof(seed) / sizeof(seed[0])) colors.push_back(seed[i]);
        else {
            const float hue = std::fmod((i * 137.50776f), 360.0f);
            colors.push_back(hsv_to_rgb(hue, 0.85f, 0.95f));
        }
    }
    return colors;
}

struct Image { uint32_t w = 0, h = 0; std::vector<uint8_t> bgra; };

static void fill_rect(Image& img, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, RGB c) {
    for (uint32_t y = y0; y < y0 + h; ++y) {
        for (uint32_t x = x0; x < x0 + w; ++x) {
            const size_t idx = (static_cast<size_t>(y) * img.w + x) * 4ULL;
            img.bgra[idx + 0] = c.b;
            img.bgra[idx + 1] = c.g;
            img.bgra[idx + 2] = c.r;
            img.bgra[idx + 3] = 255;
        }
    }
}

class ComInitGuard {
public:
    ComInitGuard() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr)) throw std::runtime_error("CoInitializeEx failed");
    }
    ~ComInitGuard() { CoUninitialize(); }
};

struct ComReleaser {
    template <typename T> void operator()(T* p) const { if (p) p->Release(); }
};

template <typename T>
using ComPtr = std::unique_ptr<T, ComReleaser>;

[[nodiscard]] static std::wstring to_wstring_utf16(const std::string& s) {
    const int count = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (count <= 0) throw std::runtime_error("utf8->utf16 failed");
    std::wstring w(static_cast<size_t>(count - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), count);
    return w;
}

static void save_png_wic(const Image& img, const std::string& out_path) {
    const std::filesystem::path outp(out_path);
    if (outp.has_parent_path()) std::filesystem::create_directories(outp.parent_path());

    ComInitGuard com;
    IWICImagingFactory* rf = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&rf));
    if (FAILED(hr) || !rf) throw std::runtime_error("WIC factory failed");
    ComPtr<IWICImagingFactory> f(rf);

    IWICStream* rs = nullptr;
    hr = f->CreateStream(&rs);
    if (FAILED(hr) || !rs) throw std::runtime_error("WIC stream failed");
    ComPtr<IWICStream> s(rs);

    hr = s->InitializeFromFilename(to_wstring_utf16(out_path).c_str(), GENERIC_WRITE);
    if (FAILED(hr)) throw std::runtime_error("open output png failed");

    IWICBitmapEncoder* re = nullptr;
    hr = f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &re);
    if (FAILED(hr) || !re) throw std::runtime_error("png encoder failed");
    ComPtr<IWICBitmapEncoder> e(re);

    hr = e->Initialize(s.get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) throw std::runtime_error("encoder init failed");

    IWICBitmapFrameEncode* rfe = nullptr;
    IPropertyBag2* rpb = nullptr;
    hr = e->CreateNewFrame(&rfe, &rpb);
    if (FAILED(hr) || !rfe) throw std::runtime_error("frame create failed");
    ComPtr<IWICBitmapFrameEncode> fe(rfe);
    ComPtr<IPropertyBag2> pb(rpb);

    hr = fe->Initialize(pb.get());
    if (FAILED(hr)) throw std::runtime_error("frame init failed");
    hr = fe->SetSize(img.w, img.h);
    if (FAILED(hr)) throw std::runtime_error("frame size failed");

    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    hr = fe->SetPixelFormat(&fmt);
    if (FAILED(hr)) throw std::runtime_error("frame fmt failed");

    const UINT stride = img.w * 4U;
    const UINT buf = static_cast<UINT>(img.bgra.size());
    hr = fe->WritePixels(img.h, stride, buf, const_cast<BYTE*>(img.bgra.data()));
    if (FAILED(hr)) throw std::runtime_error("write pixels failed");

    if (FAILED(fe->Commit())) throw std::runtime_error("frame commit failed");
    if (FAILED(e->Commit())) throw std::runtime_error("encoder commit failed");
}

static void render_state_png(
    const GameState& state,
    int64_t xmin,
    int64_t xmax,
    int64_t ymin,
    int64_t ymax,
    uint32_t cell,
    const std::string& out_png
) {
    if (xmin > xmax || ymin > ymax || cell == 0) throw std::runtime_error("invalid render args");
    auto palette = build_palette(state.players);

    std::unordered_map<Coord, uint32_t, CoordHash> owner;
    owner.reserve(state.placements.size() * 2ULL + 1ULL);
    for (const Placement& p : state.placements) owner[p.c] = p.player;

    const uint64_t cw = static_cast<uint64_t>(xmax - xmin + 1);
    const uint64_t ch = static_cast<uint64_t>(ymax - ymin + 1);
    const uint64_t iw = cw * cell;
    const uint64_t ih = ch * cell;
    if (iw > std::numeric_limits<uint32_t>::max() || ih > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("image too large");
    }

    Image img;
    img.w = static_cast<uint32_t>(iw);
    img.h = static_cast<uint32_t>(ih);
    img.bgra.assign(static_cast<size_t>(img.w) * img.h * 4ULL, 0);
    fill_rect(img, 0, 0, img.w, img.h, RGB{245, 245, 240});

    for (int64_t y = ymax; y >= ymin; --y) {
        const uint32_t row = static_cast<uint32_t>(ymax - y);
        for (int64_t x = xmin; x <= xmax; ++x) {
            const uint32_t col = static_cast<uint32_t>(x - xmin);
            const Coord c{x, y};
            const auto it = owner.find(c);
            if (it == owner.end()) continue;
            const uint32_t px = col * cell;
            const uint32_t py = row * cell;
            const uint32_t p = it->second;
            fill_rect(img, px, py, cell, cell, palette[p % palette.size()]);
        }
    }

    save_png_wic(img, out_png);
}

[[nodiscard]] static bool parse_u64(const std::string& s, uint64_t& out) {
    try {
        size_t i = 0;
        const auto v = std::stoull(s, &i);
        if (i != s.size()) return false;
        out = static_cast<uint64_t>(v);
        return true;
    } catch (...) { return false; }
}

[[nodiscard]] static bool parse_u32(const std::string& s, uint32_t& out) {
    uint64_t x = 0;
    if (!parse_u64(s, x) || x > std::numeric_limits<uint32_t>::max()) return false;
    out = static_cast<uint32_t>(x);
    return true;
}

[[nodiscard]] static bool parse_i64(const std::string& s, int64_t& out) {
    try {
        size_t i = 0;
        const auto v = std::stoll(s, &i);
        if (i != s.size()) return false;
        out = v;
        return true;
    } catch (...) { return false; }
}

[[nodiscard]] static std::unordered_map<std::string, std::string> parse_args(int argc, char** argv, int start = 2) {
    std::unordered_map<std::string, std::string> m;
    for (int i = start; i < argc; ++i) {
        std::string k = argv[i];
        if (!k.starts_with("--")) throw std::runtime_error("expected --flag");
        if (i + 1 >= argc) throw std::runtime_error("missing value for flag");
        m[k] = argv[++i];
    }
    return m;
}

[[nodiscard]] static std::string required_arg(const std::unordered_map<std::string, std::string>& m, const std::string& k) {
    auto it = m.find(k);
    if (it == m.end()) throw std::runtime_error("missing required arg: " + k);
    return it->second;
}

[[nodiscard]] static std::string default_state_path(uint32_t players, uint64_t moves) {
    return "outputs/n_knights_" + std::to_string(players) + "/states/state_" + std::to_string(moves) + ".json";
}

[[nodiscard]] static std::string default_render_path(uint32_t players, uint64_t moves, uint64_t cells, uint32_t cell_px) {
    return "outputs/n_knights_" + std::to_string(players) + "/renders/board_" + std::to_string(moves) + "_cells_" + std::to_string(cells) + "_px_" + std::to_string(cell_px) + ".png";
}

static void print_usage() {
    std::cerr
        << "Usage:\n"
        << "  n_knights simulate --players N --moves M [--out state.json]\n"
        << "  n_knights render --state state.json --xmin A --xmax B --ymin C --ymax D [--cell P] [--png out.png]\n"
        << "  n_knights simulate-render --players N --moves M --cells C [--cell P] [--state out.json] [--png out.png]\n";
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            print_usage();
            return 1;
        }
        const std::string cmd = argv[1];

        if (cmd == "simulate") {
            const auto a = parse_args(argc, argv);
            uint32_t players = 0;
            uint64_t moves = 0;
            if (!parse_u32(required_arg(a, "--players"), players)) throw std::runtime_error("invalid --players");
            if (!parse_u64(required_arg(a, "--moves"), moves)) throw std::runtime_error("invalid --moves");
            const std::string out = (a.find("--out") != a.end()) ? a.at("--out") : default_state_path(players, moves);
            SimResult r = simulate_n_knights(players, moves);
            save_state_json(r.state, out);
            std::cout << "Simulated " << r.state.moves_completed << " moves and wrote " << out << "\n";
            return 0;
        }

        if (cmd == "render") {
            const auto a = parse_args(argc, argv);
            const std::string state_path = required_arg(a, "--state");
            int64_t xmin = 0, xmax = 0, ymin = 0, ymax = 0;
            if (!parse_i64(required_arg(a, "--xmin"), xmin) || !parse_i64(required_arg(a, "--xmax"), xmax) ||
                !parse_i64(required_arg(a, "--ymin"), ymin) || !parse_i64(required_arg(a, "--ymax"), ymax)) {
                throw std::runtime_error("invalid bounds");
            }
            uint32_t cell_px = 1;
            if (a.find("--cell") != a.end() && !parse_u32(a.at("--cell"), cell_px)) throw std::runtime_error("invalid --cell");

            GameState s = load_state_json(state_path);
            const uint64_t cells = static_cast<uint64_t>(xmax - xmin + 1) * static_cast<uint64_t>(ymax - ymin + 1);
            const std::string png = (a.find("--png") != a.end()) ? a.at("--png") : default_render_path(s.players, s.moves_completed, cells, cell_px);
            render_state_png(s, xmin, xmax, ymin, ymax, cell_px, png);
            std::cout << "Rendered " << png << " from " << state_path << "\n";
            return 0;
        }

        if (cmd == "simulate-render") {
            const auto a = parse_args(argc, argv);
            uint32_t players = 0;
            uint64_t moves = 0, cells = 0;
            if (!parse_u32(required_arg(a, "--players"), players)) throw std::runtime_error("invalid --players");
            if (!parse_u64(required_arg(a, "--moves"), moves)) throw std::runtime_error("invalid --moves");
            if (!parse_u64(required_arg(a, "--cells"), cells)) throw std::runtime_error("invalid --cells");
            uint32_t cell_px = 1;
            if (a.find("--cell") != a.end() && !parse_u32(a.at("--cell"), cell_px)) throw std::runtime_error("invalid --cell");

            const uint64_t side = static_cast<uint64_t>(std::floor(std::sqrt(static_cast<long double>(cells))));
            const int64_t half = static_cast<int64_t>(side / 2ULL);
            const int64_t xmin = -half;
            const int64_t ymin = -half;
            const int64_t xmax = xmin + static_cast<int64_t>(side) - 1;
            const int64_t ymax = ymin + static_cast<int64_t>(side) - 1;

            const std::string state_out = (a.find("--state") != a.end()) ? a.at("--state") : default_state_path(players, moves);
            const std::string png_out = (a.find("--png") != a.end()) ? a.at("--png") : default_render_path(players, moves, side * side, cell_px);

            SimResult r = simulate_n_knights(players, moves);
            save_state_json(r.state, state_out);
            render_state_png(r.state, xmin, xmax, ymin, ymax, cell_px, png_out);
            std::cout << "Simulated " << r.state.moves_completed << " moves and wrote " << state_out << " and " << png_out << "\n";
            return 0;
        }

        print_usage();
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 2;
    }
}