// Round-trip tests for the .m3u8 reader/writer. No GUI involved.
//   playlist_test                      -> synthetic round trip in a temp folder
//   playlist_test <in.m3u8> <out.m3u8> -> load a real playlist, write it back, print a line-level diff summary
#include "../src/playlist.h"
#include "../src/util.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                               \
        }                                                                             \
    } while (0)

static std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string l;
    while (std::getline(ss, l)) out.push_back(trim(l));
    return out;
}

static void syntheticRoundTrip() {
    std::error_code ec;
    fs::path dir = fs::temp_directory_path() / "mixtape-test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "Library" / "Pop", ec);
    fs::create_directories(dir / "Playlists", ec);
    fs::path song = dir / "Library" / "Pop" / u8p("Björk - Jóga.mp3");
    std::ofstream(song, std::ios::binary) << "x";

    Playlist pl;
    pl.name = "test";
    pl.items.push_back({u8s(song), "Björk - Jóga", 301});
    pl.items.push_back({u8s(dir / "Library" / "missing.flac"), "Nobody - Missing", 0});

    SaveOptions o;
    o.relative = true;
    o.backslashes = true;
    o.crlf = false;
    fs::path f = dir / "Playlists" / "test.m3u8";
    CHECK(pl.save(f, o));
    CHECK(!pl.dirty);
    CHECK(pl.file == f);

    std::string text = slurp(f);
    std::string expect = "#EXTM3U\n#EXTINF:301,Björk - Jóga\n..\\Library\\Pop\\Björk - Jóga.mp3\n"
                         "#EXTINF:0,Nobody - Missing\n..\\Library\\missing.flac\n";
    CHECK(text == expect);
    if (text != expect) std::fprintf(stderr, "got:\n%s", text.c_str());

    auto back = Playlist::load(f);
    CHECK(back.has_value());
    if (back) {
        CHECK(back->name == "test");
        CHECK(back->items.size() == 2);
        if (back->items.size() == 2) {
            CHECK(pathKey(u8p(back->items[0].path)) == pathKey(song));
            CHECK(back->items[0].display == "Björk - Jóga");
            CHECK(back->items[0].duration == 301);
            CHECK(fs::path(u8p(back->items[0].path)).is_absolute());
        }
    }

    // absolute + forward slashes + CRLF
    o.relative = false;
    o.backslashes = false;
    o.crlf = true;
    fs::path f2 = dir / "Playlists" / "abs.m3u8";
    CHECK(pl.save(f2, o));
    std::string t2 = slurp(f2);
    CHECK(t2.find("\r\n") != std::string::npos);
    CHECK(t2.find('\\') == std::string::npos);
    auto back2 = Playlist::load(f2);
    CHECK(back2 && back2->items.size() == 2 && pathKey(u8p(back2->items[0].path)) == pathKey(song));

    fs::remove_all(dir, ec);
}

static void realFileRoundTrip(const fs::path& in, const fs::path& out) {
    auto pl = Playlist::load(in);
    if (!pl) {
        std::fprintf(stderr, "could not load %s\n", u8s(in).c_str());
        failures++;
        return;
    }
    SaveOptions o;
    o.relative = true;
    o.backslashes = true;
    // write next to the original's folder layout: same parent, so relative paths match
    fs::path tmp = in.parent_path() / u8p(".mixtape-roundtrip.m3u8");
    CHECK(pl->save(tmp, o));
    std::string a = slurp(in), b = slurp(tmp);
    std::error_code ec;
    fs::copy_file(tmp, out, fs::copy_options::overwrite_existing, ec);
    fs::remove(tmp, ec);
    auto la = lines(a), lb = lines(b);
    size_t diff = 0;
    for (size_t i = 0; i < std::max(la.size(), lb.size()); i++) {
        std::string x = i < la.size() ? la[i] : "", y = i < lb.size() ? lb[i] : "";
        if (x != y) {
            if (diff < 5) std::fprintf(stderr, "  line %zu\n    orig: %s\n    ours: %s\n", i + 1, x.c_str(), y.c_str());
            diff++;
        }
    }
    std::printf("%s: %zu entries, %zu lines in, %zu lines out, %zu differing lines\n", u8s(in.filename()).c_str(),
                pl->items.size(), la.size(), lb.size(), diff);
    CHECK(diff == 0);
}

int main(int argc, char** argv) {
    if (argc >= 3) realFileRoundTrip(u8p(argv[1]), u8p(argv[2]));
    else syntheticRoundTrip();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
