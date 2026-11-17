// labitop tests. Includes main.cpp directly so its file-local helpers are reachable.
#define main labitop_main
#include "../src/main.cpp"
#undef main

static int g_failed = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        g_failed++;
    }
}
#define CHECK(expr) check((expr), #expr)

static const char* kGa = "\xEA\xB0\x80"; // U+AC00, a double-width character

int main() {
    // Canvas: clipping by display columns
    g_color = false;
    {
        Canvas c(6, 1);
        CHECK(c.put(0, 0, "hello world") == 6);
        CHECK(c.row(0, 6, false) == "hello ");
        CHECK(c.put(2, 0, "abc", 0, 2) == 2); // maxw limits the write
        CHECK(c.row(0, 6, false) == "heabo ");
        CHECK(c.put(0, 5, "x") == 0); // outside the canvas
    }
    {
        Canvas c(6, 1);
        c.clip(2, 4); // a panel's inner range: nothing outside it may change
        c.put(0, 0, "abcdef");
        CHECK(c.row(0, 6, false) == "  cd  ");
    }
    {
        Canvas c(4, 1);
        c.put(0, 0, std::string(kGa) + kGa);
        c.put(1, 0, "x"); // overwriting half of a wide character blanks its other half
        CHECK(c.row(0, 4, false) == std::string(" x") + kGa);
        c.put(2, 0, "y");
        CHECK(c.row(0, 4, false) == " xy ");
    }
    {
        Canvas c(5, 1);
        std::string three = std::string(kGa) + kGa + kGa;
        CHECK(c.put(0, 0, three) == 4); // the third wide character does not fit in 5 columns
        CHECK(c.row(0, 5, false) == std::string(kGa) + kGa + " ");
        CHECK(c.row(0, 3, false) == std::string(kGa) + " "); // a wide character cut by the edge becomes a blank
        CHECK(c.row(0, 5, true) == std::string(kGa) + kGa);  // trailing blanks trimmed
        CHECK(text_width(three) == 6);
    }
    {
        Canvas c(4, 1);
        c.putr(4, 0, "ab");
        CHECK(c.row(0, 4, false) == "  ab");
    }
    g_color = true;
    {
        Canvas c(3, 1);
        c.put(0, 0, "a", RED);
        CHECK(c.row(0, 3, true) == "\x1b[0;31ma\x1b[0m");
    }
    g_color = false;

    // Bars and graphs (ASCII glyphs so the expected strings are readable)
    G = &kAscii;
    {
        Canvas c(8, 1);
        hbar(c, 0, 0, 8, 0.5);
        CHECK(c.row(0, 8, false) == "####----");
        hbar(c, 0, 0, 8, 0.001); // any non-zero value shows at least one cell
        CHECK(c.row(0, 8, false) == "#-------");
    }
    {
        Canvas c(4, 2);
        Hist h;
        h.push(0.0f);
        h.push(0.5f);
        h.push(1.0f);
        graph(c, 0, 0, 4, 2, h, 1.0); // 3 samples right-aligned in 4 columns
        CHECK(c.row(0, 4, false) == "   #");
        CHECK(c.row(1, 4, false) == " .##"); // the zero sample leaves a baseline mark
    }
    G = &kUnicode;

    CHECK(short_cpu_name("Intel(R) Core(TM) i7-14700K CPU @ 3.40GHz") == "Intel Core i7-14700K");
    CHECK(short_cpu_name("AMD Ryzen 7 7800X3D 8-Core Processor   ") == "AMD Ryzen 7 7800X3D 8-Core");

    // /proc/<pid>/stat: process name containing spaces and parentheses
    ProcInfo p;
    const char* stat = "1234 (a) (b c) S 1 1 1 0 -1 4194560 100 0 0 0 250 50 0 0 20 0 7 0 12345 1000000 300 99";
    CHECK(parse_proc_stat(stat, p, 100, 4096));
    CHECK(p.pid == 1234);
    CHECK(p.name == "a) (b c");
    CHECK(p.cpu_time == 30000000ULL); // (250 + 50) ticks at 100 Hz = 3 s
    CHECK(p.threads == 7);
    CHECK(p.mem == 300ULL * 4096);
    CHECK(!parse_proc_stat("1234 (x) S 1 1", p, 100, 4096)); // truncated line
    CHECK(!parse_proc_stat("garbage", p, 100, 4096));

    // /proc/mounts escapes
    CHECK(unescape_mount("/mnt/My\\040Disk") == "/mnt/My Disk");
    CHECK(unescape_mount("/plain\\x") == "/plain\\x");

    // SMBIOS: one populated DDR5 module, one empty slot, then the end marker
    {
        std::vector<uint8_t> t;
        auto memory_device = [&](unsigned size_mb, bool with_strings) {
            size_t base = t.size();
            t.resize(base + 0x22, 0);
            t[base] = 17;
            t[base + 1] = 0x22;
            t[base + 0x0C] = (uint8_t)(size_mb & 0xFF);
            t[base + 0x0D] = (uint8_t)(size_mb >> 8);
            t[base + 0x10] = with_strings ? 1 : 0; // locator = string 1
            t[base + 0x12] = 0x22;                 // DDR5
            t[base + 0x15] = 0xC0;                 // 4800 MHz rated
            t[base + 0x16] = 0x12;
            t[base + 0x17] = with_strings ? 2 : 0; // manufacturer = string 2
            t[base + 0x20] = 0xE0;                 // 5600 MHz configured
            t[base + 0x21] = 0x15;
            if (with_strings) {
                for (const char* s : { "DIMM_A1", "ACME  " }) t.insert(t.end(), s, s + strlen(s) + 1);
                t.push_back(0);
            } else {
                t.push_back(0);
                t.push_back(0);
            }
        };
        memory_device(16384, true);
        memory_device(0, false);
        t.insert(t.end(), { 127, 4, 0, 0, 0, 0 });

        std::vector<MemModule> mods;
        parse_smbios_memory(t.data(), t.size(), mods);
        CHECK(mods.size() == 1);
        if (mods.size() == 1) {
            CHECK(mods[0].size == 16384ULL << 20);
            CHECK(mods[0].type == "DDR5");
            CHECK(mods[0].speed == 5600); // configured speed wins over the rated one
            CHECK(mods[0].locator == "DIMM_A1");
            CHECK(mods[0].manufacturer == "ACME");
        }
        mods.clear();
        parse_smbios_memory(t.data(), 40, mods); // cut in the middle of the strings: no crash, no entry
        CHECK(mods.empty());
    }

    if (g_failed) return 1;
    puts("all tests passed");
    return 0;
}
