#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <regex>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <windows.h>
#include <wincodec.h>

struct Coord {
    int64_t x = 0;
    int64_t y = 0;

    bool operator==(const Coord& other) const noexcept {
        return x == other.x && y == other.y;
    }
};

struct CoordHash {
    size_t operator()(const Coord& c) const noexcept {
        const uint64_t h1 = static_cast<uint64_t>(c.x) * 0x9e3779b185ebca87ULL;
        const uint64_t h2 = static_cast<uint64_t>(c.y) * 0xc2b2ae3d27d4eb4fULL;
        return static_cast<size_t>(h1 ^ (h2 + 0x9e3779b185ebca87ULL + (h1 << 6U) + (h1 >> 2U)));
    }
};

enum class Player : uint8_t { Red, Black };
enum class Player3 : uint8_t { Red, Black, Green };

struct Placement {
    uint64_t n = 0;
    Coord c;
    char player = 'R';
};

struct GameState {
    std::string rules = "default";
    uint64_t moves_requested = 0;
    uint64_t moves_completed = 0;
    uint64_t next_n = 1;
    std::vector<Placement> placements;
};

static constexpr Coord kKnightOffsets[8] = {
    {1, 2},  {2, 1},   {2, -1},  {1, -2},
    {-1, -2},{-2, -1}, {-2, 1},  {-1, 2}
};

[[nodiscard]] static inline Player other_player(Player p) {
    return (p == Player::Red) ? Player::Black : Player::Red;
}

[[nodiscard]] static inline char player_char(Player p) {
    return (p == Player::Red) ? 'R' : 'B';
}

[[nodiscard]] static inline char parse_player_char(char ch) {
    if (ch == 'R' || ch == 'B' || ch == 'G') return ch;
    throw std::runtime_error("invalid player char");
}

[[nodiscard]] static inline char player3_char(Player3 p) {
    if (p == Player3::Red) return 'R';
    if (p == Player3::Black) return 'B';
    return 'G';
}

[[nodiscard]] static Coord n_to_coord(uint64_t n) {
    if (n == 0) {
        throw std::runtime_error("n must be >= 1");
    }
    if (n == 1) {
        return {0, 0};
    }

    const long double root = std::sqrt(static_cast<long double>(n));
    const uint64_t k = static_cast<uint64_t>(std::ceil((root - 1.0L) / 2.0L));
    const uint64_t side = 2ULL * k;
    const uint64_t t = 2ULL * k + 1ULL;
    const uint64_t m = t * t;
    const uint64_t d = m - n;

    const int64_t kk = static_cast<int64_t>(k);

    if (d < side) {
        return {kk - static_cast<int64_t>(d), -kk};
    }
    if (d < 2ULL * side) {
        return {-kk, -kk + static_cast<int64_t>(d - side)};
    }
    if (d < 3ULL * side) {
        return {-kk + static_cast<int64_t>(d - 2ULL * side), kk};
    }
    return {kk, kk - static_cast<int64_t>(d - 3ULL * side)};
}

[[nodiscard]] static uint64_t coord_to_n(const Coord& c) {
    const int64_t k = std::max<int64_t>(std::llabs(c.x), std::llabs(c.y));
    if (k == 0) {
        return 1;
    }

    const uint64_t ku = static_cast<uint64_t>(k);
    const uint64_t m = (2ULL * ku + 1ULL) * (2ULL * ku + 1ULL);

    if (c.y == -k) {
        return m - static_cast<uint64_t>(k - c.x);
    }
    if (c.x == -k) {
        return m - 2ULL * ku - static_cast<uint64_t>(c.y + k);
    }
    if (c.y == k) {
        return m - 4ULL * ku - static_cast<uint64_t>(c.x + k);
    }
    return m - 6ULL * ku - static_cast<uint64_t>(k - c.y);
}

struct RuleSet {
    virtual ~RuleSet() = default;
    [[nodiscard]] virtual bool is_legal(
        Player player,
        const Coord& target,
        const std::unordered_set<Coord, CoordHash>& occupied,
        const std::unordered_map<Coord, uint32_t, CoordHash>& red_attacks,
        const std::unordered_map<Coord, uint32_t, CoordHash>& black_attacks
    ) const = 0;
};

struct DefaultRuleSet final : RuleSet {
    [[nodiscard]] bool is_legal(
        Player player,
        const Coord& target,
        const std::unordered_set<Coord, CoordHash>& occupied,
        const std::unordered_map<Coord, uint32_t, CoordHash>& red_attacks,
        const std::unordered_map<Coord, uint32_t, CoordHash>& black_attacks
    ) const override {
        if (occupied.find(target) != occupied.end()) {
            return false;
        }
        if (player == Player::Red) {
            const auto it = black_attacks.find(target);
            return (it == black_attacks.end()) || (it->second == 0);
        }
        const auto it = red_attacks.find(target);
        return (it == red_attacks.end()) || (it->second == 0);
    }
};

struct SimResult {
    GameState state;
    std::unordered_set<Coord, CoordHash> occupied;
    std::unordered_map<Coord, uint32_t, CoordHash> red_attacks;
    std::unordered_map<Coord, uint32_t, CoordHash> black_attacks;
    std::unordered_map<Coord, uint32_t, CoordHash> green_attacks;
};

static void add_attacks(
    const Coord& from,
    std::unordered_map<Coord, uint32_t, CoordHash>& attack_map
) {
    for (const Coord& d : kKnightOffsets) {
        Coord t{from.x + d.x, from.y + d.y};
        auto it = attack_map.find(t);
        if (it == attack_map.end()) {
            attack_map.emplace(t, 1U);
        } else {
            ++(it->second);
        }
    }
}

[[nodiscard]] static uint64_t find_next_legal_n(
    Player player,
    uint64_t start_n,
    const RuleSet& rules,
    const std::unordered_set<Coord, CoordHash>& occupied,
    const std::unordered_map<Coord, uint32_t, CoordHash>& red_attacks,
    const std::unordered_map<Coord, uint32_t, CoordHash>& black_attacks
) {
    uint64_t n = start_n;
    while (true) {
        const Coord c = n_to_coord(n);
        if (rules.is_legal(player, c, occupied, red_attacks, black_attacks)) {
            return n;
        }
        ++n;
    }
}

[[nodiscard]] static SimResult simulate_default(uint64_t moves) {
    DefaultRuleSet rules;

    SimResult result;
    result.state.rules = "default";
    result.state.moves_requested = moves;
    result.state.moves_completed = 0;
    result.state.next_n = 1;
    result.state.placements.reserve(static_cast<size_t>(moves));

    uint64_t red_cursor = 1;
    uint64_t black_cursor = 1;

    Player turn = Player::Red;

    for (uint64_t i = 0; i < moves; ++i) {
        uint64_t& cursor = (turn == Player::Red) ? red_cursor : black_cursor;

        const uint64_t n = find_next_legal_n(
            turn,
            cursor,
            rules,
            result.occupied,
            result.red_attacks,
            result.black_attacks
        );

        const Coord c = n_to_coord(n);
        result.occupied.insert(c);

        result.state.placements.push_back(Placement{n, c, player_char(turn)});
        result.state.moves_completed++;

        if (turn == Player::Red) {
            add_attacks(c, result.red_attacks);
        } else {
            add_attacks(c, result.black_attacks);
        }

        cursor = n + 1;
        turn = other_player(turn);
    }

    result.state.next_n = std::max(red_cursor, black_cursor);
    return result;
}

[[nodiscard]] static bool is_legal_three_knights(
    Player3 turn,
    const Coord& target,
    const std::unordered_set<Coord, CoordHash>& occupied,
    const std::unordered_map<Coord, uint32_t, CoordHash>& red_attacks,
    const std::unordered_map<Coord, uint32_t, CoordHash>& black_attacks,
    const std::unordered_map<Coord, uint32_t, CoordHash>& green_attacks
) {
    if (occupied.find(target) != occupied.end()) return false;
    const auto attacked = [&](const std::unordered_map<Coord, uint32_t, CoordHash>& m) {
        const auto it = m.find(target);
        return it != m.end() && it->second > 0;
    };
    if (turn == Player3::Red) return !attacked(black_attacks) && !attacked(green_attacks);
    if (turn == Player3::Black) return !attacked(red_attacks) && !attacked(green_attacks);
    return !attacked(red_attacks) && !attacked(black_attacks);
}

[[nodiscard]] static SimResult simulate_three_knights(uint64_t moves) {
    SimResult result;
    result.state.rules = "three_knights";
    result.state.moves_requested = moves;
    result.state.moves_completed = 0;
    result.state.next_n = 1;
    result.state.placements.reserve(static_cast<size_t>(moves));

    uint64_t r_cursor = 1, b_cursor = 1, g_cursor = 1;
    Player3 turn = Player3::Red;

    for (uint64_t i = 0; i < moves; ++i) {
        uint64_t* cursor = &r_cursor;
        if (turn == Player3::Black) cursor = &b_cursor;
        if (turn == Player3::Green) cursor = &g_cursor;

        uint64_t n = *cursor;
        while (true) {
            const Coord c = n_to_coord(n);
            if (is_legal_three_knights(turn, c, result.occupied, result.red_attacks, result.black_attacks, result.green_attacks)) {
                result.occupied.insert(c);
                result.state.placements.push_back(Placement{n, c, player3_char(turn)});
                result.state.moves_completed++;
                if (turn == Player3::Red) add_attacks(c, result.red_attacks);
                else if (turn == Player3::Black) add_attacks(c, result.black_attacks);
                else add_attacks(c, result.green_attacks);
                *cursor = n + 1;
                break;
            }
            ++n;
        }

        if (turn == Player3::Red) turn = Player3::Black;
        else if (turn == Player3::Black) turn = Player3::Green;
        else turn = Player3::Red;
    }

    result.state.next_n = std::max(r_cursor, std::max(b_cursor, g_cursor));
    return result;
}

static void save_state_json(const GameState& state, const std::string& out_path) {
    const std::filesystem::path outp(out_path);
    if (outp.has_parent_path()) {
        std::filesystem::create_directories(outp.parent_path());
    }
    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to open output state file: " + out_path);
    }

    out << "{\n";
    out << "  \"rules\": \"" << state.rules << "\",\n";
    out << "  \"moves_requested\": " << state.moves_requested << ",\n";
    out << "  \"moves_completed\": " << state.moves_completed << ",\n";
    out << "  \"next_n\": " << state.next_n << ",\n";
    out << "  \"placements\": [\n";

    for (size_t i = 0; i < state.placements.size(); ++i) {
        const Placement& p = state.placements[i];
        out << "    {\"n\": " << p.n
            << ", \"x\": " << p.c.x
            << ", \"y\": " << p.c.y
            << ", \"player\": \"" << p.player << "\"}";
        if (i + 1 < state.placements.size()) {
            out << ",";
        }
        out << "\n";
    }

    out << "  ]\n";
    out << "}\n";
}

[[nodiscard]] static std::string read_file_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open file: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

[[nodiscard]] static uint64_t extract_uint_field(const std::string& text, const std::string& key, uint64_t fallback) {
    const std::regex re("\\\"" + key + "\\\"\\s*:\\s*(\\d+)");
    std::smatch m;
    if (std::regex_search(text, m, re)) {
        return static_cast<uint64_t>(std::stoull(m[1].str()));
    }
    return fallback;
}

[[nodiscard]] static std::string extract_string_field(const std::string& text, const std::string& key, const std::string& fallback) {
    const std::regex re("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch m;
    if (std::regex_search(text, m, re)) {
        return m[1].str();
    }
    return fallback;
}

[[nodiscard]] static GameState load_state_json(const std::string& path) {
    const std::string text = read_file_all(path);
    GameState state;

    state.rules = extract_string_field(text, "rules", "default");
    state.moves_requested = extract_uint_field(text, "moves_requested", 0);
    state.moves_completed = extract_uint_field(text, "moves_completed", 0);
    state.next_n = extract_uint_field(text, "next_n", 1);

    const std::regex placement_re(
        "\\{\\s*\\\"n\\\"\\s*:\\s*(\\d+)\\s*,\\s*\\\"x\\\"\\s*:\\s*(-?\\d+)\\s*,\\s*\\\"y\\\"\\s*:\\s*(-?\\d+)\\s*,\\s*\\\"player\\\"\\s*:\\s*\\\"([RBG])\\\"\\s*\\}"
    );

    auto begin = std::sregex_iterator(text.begin(), text.end(), placement_re);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        const std::smatch& m = *it;
        Placement p;
        p.n = static_cast<uint64_t>(std::stoull(m[1].str()));
        p.c.x = std::stoll(m[2].str());
        p.c.y = std::stoll(m[3].str());
        p.player = parse_player_char(m[4].str()[0]);
        state.placements.push_back(p);
    }

    if (state.moves_completed == 0) {
        state.moves_completed = static_cast<uint64_t>(state.placements.size());
    }
    if (state.moves_requested == 0) {
        state.moves_requested = state.moves_completed;
    }

    return state;
}

struct RebuiltState {
    std::unordered_set<Coord, CoordHash> occupied;
    std::unordered_map<Coord, uint32_t, CoordHash> red_attacks;
    std::unordered_map<Coord, uint32_t, CoordHash> black_attacks;
    std::unordered_map<Coord, uint32_t, CoordHash> green_attacks;
    std::unordered_map<Coord, char, CoordHash> piece_owner;
};

[[nodiscard]] static RebuiltState rebuild_maps_from_placements(const std::vector<Placement>& placements) {
    RebuiltState rs;
    rs.occupied.reserve(placements.size() * 2ULL + 1ULL);
    rs.piece_owner.reserve(placements.size() * 2ULL + 1ULL);

    for (const Placement& p : placements) {
        rs.occupied.insert(p.c);
        rs.piece_owner[p.c] = p.player;
        if (p.player == 'R') {
            add_attacks(p.c, rs.red_attacks);
        } else if (p.player == 'B') {
            add_attacks(p.c, rs.black_attacks);
        } else {
            add_attacks(p.c, rs.green_attacks);
        }
    }

    return rs;
}

struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};

static void fill_rect(Image& img, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    for (uint32_t y = y0; y < y0 + h; ++y) {
        for (uint32_t x = x0; x < x0 + w; ++x) {
            const size_t idx = (static_cast<size_t>(y) * img.width + x) * 4ULL;
            img.rgba[idx + 0] = b;
            img.rgba[idx + 1] = g;
            img.rgba[idx + 2] = r;
            img.rgba[idx + 3] = a;
        }
    }
}

static void blend_rect(Image& img, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
    const float af = static_cast<float>(alpha) / 255.0F;
    for (uint32_t y = y0; y < y0 + h; ++y) {
        for (uint32_t x = x0; x < x0 + w; ++x) {
            const size_t idx = (static_cast<size_t>(y) * img.width + x) * 4ULL;
            const float bb = static_cast<float>(img.rgba[idx + 0]);
            const float bg = static_cast<float>(img.rgba[idx + 1]);
            const float br = static_cast<float>(img.rgba[idx + 2]);
            img.rgba[idx + 0] = static_cast<uint8_t>(std::clamp(bb * (1.0F - af) + static_cast<float>(b) * af, 0.0F, 255.0F));
            img.rgba[idx + 1] = static_cast<uint8_t>(std::clamp(bg * (1.0F - af) + static_cast<float>(g) * af, 0.0F, 255.0F));
            img.rgba[idx + 2] = static_cast<uint8_t>(std::clamp(br * (1.0F - af) + static_cast<float>(r) * af, 0.0F, 255.0F));
            img.rgba[idx + 3] = 255;
        }
    }
}

class ComInitGuard {
public:
    ComInitGuard() {
        hr_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr_)) {
            throw std::runtime_error("CoInitializeEx failed");
        }
    }

    ~ComInitGuard() {
        if (SUCCEEDED(hr_)) {
            CoUninitialize();
        }
    }

private:
    HRESULT hr_{};
};

struct ComReleaser {
    template <typename T>
    void operator()(T* p) const {
        if (p != nullptr) {
            p->Release();
        }
    }
};

template <typename T>
using ComPtr = std::unique_ptr<T, ComReleaser>;

static void save_png_wic(const Image& img, const std::wstring& path) {
    ComInitGuard com;

    IWICImagingFactory* raw_factory = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&raw_factory)
    );
    if (FAILED(hr) || raw_factory == nullptr) {
        throw std::runtime_error("failed to create WIC factory");
    }
    ComPtr<IWICImagingFactory> factory(raw_factory);

    IWICStream* raw_stream = nullptr;
    hr = factory->CreateStream(&raw_stream);
    if (FAILED(hr) || raw_stream == nullptr) {
        throw std::runtime_error("failed to create WIC stream");
    }
    ComPtr<IWICStream> stream(raw_stream);

    hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) {
        throw std::runtime_error("failed to open output PNG file");
    }

    IWICBitmapEncoder* raw_encoder = nullptr;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &raw_encoder);
    if (FAILED(hr) || raw_encoder == nullptr) {
        throw std::runtime_error("failed to create PNG encoder");
    }
    ComPtr<IWICBitmapEncoder> encoder(raw_encoder);

    hr = encoder->Initialize(stream.get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        throw std::runtime_error("failed to initialize PNG encoder");
    }

    IWICBitmapFrameEncode* raw_frame = nullptr;
    IPropertyBag2* raw_props = nullptr;
    hr = encoder->CreateNewFrame(&raw_frame, &raw_props);
    if (FAILED(hr) || raw_frame == nullptr) {
        throw std::runtime_error("failed to create PNG frame");
    }
    ComPtr<IWICBitmapFrameEncode> frame(raw_frame);
    ComPtr<IPropertyBag2> props(raw_props);

    hr = frame->Initialize(props.get());
    if (FAILED(hr)) {
        throw std::runtime_error("failed to initialize PNG frame");
    }

    hr = frame->SetSize(img.width, img.height);
    if (FAILED(hr)) {
        throw std::runtime_error("failed to set PNG frame size");
    }

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr)) {
        throw std::runtime_error("failed to set PNG pixel format");
    }

    const UINT stride = img.width * 4U;
    const UINT buffer_size = static_cast<UINT>(img.rgba.size());
    hr = frame->WritePixels(img.height, stride, buffer_size, const_cast<BYTE*>(img.rgba.data()));
    if (FAILED(hr)) {
        throw std::runtime_error("failed to write PNG pixels");
    }

    hr = frame->Commit();
    if (FAILED(hr)) {
        throw std::runtime_error("failed to commit PNG frame");
    }

    hr = encoder->Commit();
    if (FAILED(hr)) {
        throw std::runtime_error("failed to finalize PNG");
    }
}

[[nodiscard]] static std::wstring to_wstring_utf16(const std::string& s) {
    if (s.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (count <= 0) {
        throw std::runtime_error("failed utf8->utf16 conversion");
    }
    std::wstring out(static_cast<size_t>(count - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), count);
    return out;
}

[[nodiscard]] static std::string default_state_path(const std::string& rules, uint64_t moves) {
    return "outputs/" + rules + "/states/state_" + std::to_string(moves) + ".json";
}

[[nodiscard]] static std::string default_render_path(const std::string& rules, uint64_t moves, int64_t xmin, int64_t xmax, int64_t ymin, int64_t ymax, uint32_t cell, bool overlay) {
    return "outputs/" + rules + "/renders/board_" +
        std::to_string(moves) + "_" +
        std::to_string(xmin) + "_" + std::to_string(xmax) + "_" +
        std::to_string(ymin) + "_" + std::to_string(ymax) +
        "_c" + std::to_string(cell) +
        (overlay ? "_ov1" : "_ov0") + ".png";
}

static void render_png(
    const GameState& state,
    int64_t xmin,
    int64_t xmax,
    int64_t ymin,
    int64_t ymax,
    uint32_t cell,
    bool overlay_attacks,
    const std::string& out_png
) {
    if (xmin > xmax || ymin > ymax) {
        throw std::runtime_error("invalid bounds");
    }
    if (cell == 0) {
        throw std::runtime_error("cell size must be > 0");
    }

    const RebuiltState rs = rebuild_maps_from_placements(state.placements);

    const uint64_t cells_w = static_cast<uint64_t>(xmax - xmin + 1);
    const uint64_t cells_h = static_cast<uint64_t>(ymax - ymin + 1);
    const uint64_t width_u64 = cells_w * cell;
    const uint64_t height_u64 = cells_h * cell;

    if (width_u64 > std::numeric_limits<uint32_t>::max() || height_u64 > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("image dimensions too large");
    }

    Image img;
    img.width = static_cast<uint32_t>(width_u64);
    img.height = static_cast<uint32_t>(height_u64);
    img.rgba.assign(static_cast<size_t>(img.width) * img.height * 4ULL, 0U);

    fill_rect(img, 0, 0, img.width, img.height, 245, 245, 240, 255);

    for (int64_t by = ymax; by >= ymin; --by) {
        const uint32_t row = static_cast<uint32_t>(ymax - by);
        for (int64_t bx = xmin; bx <= xmax; ++bx) {
            const uint32_t col = static_cast<uint32_t>(bx - xmin);
            const uint32_t px = col * cell;
            const uint32_t py = row * cell;
            const Coord c{bx, by};

            auto piece_it = rs.piece_owner.find(c);
            if (piece_it != rs.piece_owner.end()) {
                if (piece_it->second == 'R') {
                    fill_rect(img, px, py, cell, cell, 255, 0, 0, 255);
                } else if (piece_it->second == 'B') {
                    fill_rect(img, px, py, cell, cell, 0, 0, 0, 255);
                } else {
                    fill_rect(img, px, py, cell, cell, 0, 190, 0, 255);
                }
                continue;
            }

            if (overlay_attacks) {
                const bool attacked_by_red = (rs.red_attacks.find(c) != rs.red_attacks.end());
                const bool attacked_by_black = (rs.black_attacks.find(c) != rs.black_attacks.end());
                const bool attacked_by_green = (rs.green_attacks.find(c) != rs.green_attacks.end());

                if (attacked_by_red && attacked_by_black && attacked_by_green) {
                    blend_rect(img, px, py, cell, cell, 90, 60, 30, 120);
                } else if (attacked_by_red && attacked_by_black) {
                    blend_rect(img, px, py, cell, cell, 128, 0, 0, 120);
                } else if (attacked_by_red && attacked_by_green) {
                    blend_rect(img, px, py, cell, cell, 120, 60, 0, 110);
                } else if (attacked_by_black && attacked_by_green) {
                    blend_rect(img, px, py, cell, cell, 0, 80, 0, 100);
                } else if (attacked_by_red) {
                    blend_rect(img, px, py, cell, cell, 255, 0, 0, 100);
                } else if (attacked_by_black) {
                    blend_rect(img, px, py, cell, cell, 0, 0, 0, 90);
                } else if (attacked_by_green) {
                    blend_rect(img, px, py, cell, cell, 0, 180, 0, 100);
                }
            }

            if (cell >= 6U) {
                blend_rect(img, px, py, cell, 1, 200, 200, 200, 120);
                blend_rect(img, px, py, 1, cell, 200, 200, 200, 120);
            }
        }
    }

    const std::filesystem::path outp(out_png);
    if (outp.has_parent_path()) {
        std::filesystem::create_directories(outp.parent_path());
    }
    save_png_wic(img, to_wstring_utf16(out_png));
}

[[nodiscard]] static bool parse_u64(const std::string& s, uint64_t& out) {
    try {
        size_t idx = 0;
        const auto v = std::stoull(s, &idx);
        if (idx != s.size()) return false;
        out = static_cast<uint64_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] static bool parse_i64(const std::string& s, int64_t& out) {
    try {
        size_t idx = 0;
        const auto v = std::stoll(s, &idx);
        if (idx != s.size()) return false;
        out = static_cast<int64_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] static bool parse_u32(const std::string& s, uint32_t& out) {
    uint64_t tmp = 0;
    if (!parse_u64(s, tmp) || tmp > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    out = static_cast<uint32_t>(tmp);
    return true;
}

[[nodiscard]] static std::unordered_map<std::string, std::string> parse_args_map(int argc, char** argv, int start_idx = 2) {
    std::unordered_map<std::string, std::string> args;
    for (int i = start_idx; i < argc; ++i) {
        std::string key = argv[i];
        if (!key.starts_with("--")) {
            throw std::runtime_error("expected flag starting with --, got: " + key);
        }
        if (i + 1 >= argc) {
            throw std::runtime_error("missing value for flag: " + key);
        }
        std::string value = argv[++i];
        args[key] = value;
    }
    return args;
}

[[nodiscard]] static std::string get_required_arg(const std::unordered_map<std::string, std::string>& args, const std::string& key) {
    auto it = args.find(key);
    if (it == args.end()) {
        throw std::runtime_error("missing required arg: " + key);
    }
    return it->second;
}

[[nodiscard]] static std::string get_optional_arg(const std::unordered_map<std::string, std::string>& args, const std::string& key, const std::string& fallback) {
    auto it = args.find(key);
    return (it == args.end()) ? fallback : it->second;
}

static void print_usage() {
    std::cerr
        << "Usage:\n"
        << "  red_black_knights simulate --moves N [--out state.json] [--rules default|three_knights]\n"
        << "  red_black_knights render --state state.json --xmin A --xmax B --ymin C --ymax D --png out.png [--cell S] [--overlay true|false]\n"
        << "  red_black_knights simulate-render --moves N --xmin A --xmax B --ymin C --ymax D [--png out.png] [--cell S] [--overlay true|false] [--state out.json] [--rules default|three_knights]\n"
        << "  red_black_knights probe --n N\n"
        << "  red_black_knights probe --x X --y Y\n";
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            print_usage();
            return 1;
        }

        const std::string cmd = argv[1];

        if (cmd == "simulate") {
            const auto args = parse_args_map(argc, argv);
            const std::string rules = get_optional_arg(args, "--rules", "default");
            if (rules != "default" && rules != "three_knights") {
                throw std::runtime_error("supported --rules: default, three_knights");
            }

            uint64_t moves = 0;
            if (!parse_u64(get_required_arg(args, "--moves"), moves)) {
                throw std::runtime_error("invalid --moves");
            }

            const std::string out_path =
                (args.find("--out") != args.end())
                ? args.at("--out")
                : default_state_path(rules, moves);

            SimResult sim = (rules == "default") ? simulate_default(moves) : simulate_three_knights(moves);
            save_state_json(sim.state, out_path);

            std::cout << "Simulated " << sim.state.moves_completed << " moves and wrote " << out_path << "\n";
            return 0;
        }

        if (cmd == "render") {
            const auto args = parse_args_map(argc, argv);
            const std::string state_path = get_required_arg(args, "--state");

            int64_t xmin = 0, xmax = 0, ymin = 0, ymax = 0;
            if (!parse_i64(get_required_arg(args, "--xmin"), xmin) ||
                !parse_i64(get_required_arg(args, "--xmax"), xmax) ||
                !parse_i64(get_required_arg(args, "--ymin"), ymin) ||
                !parse_i64(get_required_arg(args, "--ymax"), ymax)) {
                throw std::runtime_error("invalid bounds args");
            }

            uint32_t cell = 8;
            if (args.find("--cell") != args.end() && !parse_u32(args.at("--cell"), cell)) {
                throw std::runtime_error("invalid --cell");
            }

            const std::string overlay_arg = get_optional_arg(args, "--overlay", "true");
            const bool overlay = !(overlay_arg == "false" || overlay_arg == "0");

            const GameState state = load_state_json(state_path);
            uint64_t inferred_moves = state.moves_completed;
            if (inferred_moves == 0) {
                inferred_moves = static_cast<uint64_t>(state.placements.size());
            }
            const std::string out_png =
                (args.find("--png") != args.end())
                ? args.at("--png")
                : default_render_path(state.rules, inferred_moves, xmin, xmax, ymin, ymax, cell, overlay);
            render_png(state, xmin, xmax, ymin, ymax, cell, overlay, out_png);

            std::cout << "Rendered " << out_png << " from " << state_path << "\n";
            return 0;
        }

        if (cmd == "simulate-render") {
            const auto args = parse_args_map(argc, argv);
            uint64_t moves = 0;
            if (!parse_u64(get_required_arg(args, "--moves"), moves)) {
                throw std::runtime_error("invalid --moves");
            }

            int64_t xmin = 0, xmax = 0, ymin = 0, ymax = 0;
            if (!parse_i64(get_required_arg(args, "--xmin"), xmin) ||
                !parse_i64(get_required_arg(args, "--xmax"), xmax) ||
                !parse_i64(get_required_arg(args, "--ymin"), ymin) ||
                !parse_i64(get_required_arg(args, "--ymax"), ymax)) {
                throw std::runtime_error("invalid bounds args");
            }

            uint32_t cell = 8;
            if (args.find("--cell") != args.end() && !parse_u32(args.at("--cell"), cell)) {
                throw std::runtime_error("invalid --cell");
            }

            const std::string overlay_arg = get_optional_arg(args, "--overlay", "true");
            const bool overlay = !(overlay_arg == "false" || overlay_arg == "0");

            const std::string rules = get_optional_arg(args, "--rules", "default");
            if (rules != "default" && rules != "three_knights") {
                throw std::runtime_error("supported --rules: default, three_knights");
            }
            const std::string out_state =
                (args.find("--state") != args.end())
                ? args.at("--state")
                : default_state_path(rules, moves);
            const std::string out_png =
                (args.find("--png") != args.end())
                ? args.at("--png")
                : default_render_path(rules, moves, xmin, xmax, ymin, ymax, cell, overlay);

            SimResult sim = (rules == "default") ? simulate_default(moves) : simulate_three_knights(moves);
            save_state_json(sim.state, out_state);
            render_png(sim.state, xmin, xmax, ymin, ymax, cell, overlay, out_png);

            std::cout << "Simulated " << sim.state.moves_completed << " moves, wrote " << out_state << " and " << out_png << "\n";
            return 0;
        }

        if (cmd == "probe") {
            const auto args = parse_args_map(argc, argv);
            if (args.find("--n") != args.end()) {
                uint64_t n = 0;
                if (!parse_u64(args.at("--n"), n)) {
                    throw std::runtime_error("invalid --n");
                }
                const Coord c = n_to_coord(n);
                std::cout << "n=" << n << " -> (" << c.x << "," << c.y << ")\n";
                return 0;
            }
            if (args.find("--x") != args.end() && args.find("--y") != args.end()) {
                int64_t x = 0, y = 0;
                if (!parse_i64(args.at("--x"), x) || !parse_i64(args.at("--y"), y)) {
                    throw std::runtime_error("invalid --x/--y");
                }
                const uint64_t n = coord_to_n(Coord{x, y});
                std::cout << "(" << x << "," << y << ") -> n=" << n << "\n";
                return 0;
            }
            throw std::runtime_error("probe requires --n or --x and --y");
        }

        print_usage();
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 2;
    }
}
