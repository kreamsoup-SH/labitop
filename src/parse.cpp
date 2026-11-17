// labitop: portable parsers. No OS calls here, so every platform can build and test them.
#include "sys.h"

#include <cstdlib>
#include <cstring>

bool parse_proc_stat(const char* text, ProcInfo& out, long hz, long page) {
    // "pid (comm) state ppid ..." - comm may contain spaces and parentheses
    const char* lp = strchr(text, '(');
    const char* rp = strrchr(text, ')');
    if (!lp || !rp || rp < lp || hz <= 0) return false;
    unsigned long long utime = 0, stime = 0, threads = 0, rss = 0;
    const char* p = rp + 1;
    for (int field = 3; field <= 24; field++) { // the field after ")" is number 3 (state)
        while (*p == ' ') p++;
        if (!*p) return false;
        if (field == 14) utime = strtoull(p, nullptr, 10);
        else if (field == 15) stime = strtoull(p, nullptr, 10);
        else if (field == 20) threads = strtoull(p, nullptr, 10);
        else if (field == 24) rss = strtoull(p, nullptr, 10);
        while (*p && *p != ' ') p++;
    }
    out.pid = (uint32_t)strtoul(text, nullptr, 10);
    out.name.assign(lp + 1, rp);
    out.cpu_time = (utime + stime) * 10000000ULL / (uint64_t)hz;
    out.mem = rss * (uint64_t)page;
    out.threads = (uint32_t)threads;
    return true;
}

std::string unescape_mount(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        auto oct = [&](size_t k) { return k < s.size() && s[k] >= '0' && s[k] <= '7'; };
        if (s[i] == '\\' && oct(i + 1) && oct(i + 2) && oct(i + 3)) {
            out += (char)((s[i + 1] - '0') * 64 + (s[i + 2] - '0') * 8 + (s[i + 3] - '0'));
            i += 3;
        } else {
            out += s[i];
        }
    }
    return out;
}

static const char* memory_type_name(uint8_t t) {
    switch (t) {
    case 0x12: return "DDR";
    case 0x13: return "DDR2";
    case 0x18: return "DDR3";
    case 0x1A: return "DDR4";
    case 0x1B: return "LPDDR";
    case 0x1C: return "LPDDR2";
    case 0x1D: return "LPDDR3";
    case 0x1E: return "LPDDR4";
    case 0x22: return "DDR5";
    case 0x23: return "LPDDR5";
    }
    return "";
}

static std::string trimmed(const std::string& s) {
    size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(' ');
    return s.substr(a, b - a + 1);
}

// SMBIOS table: a run of structures. Each has a 4-byte header (type, length, handle), `length`
// bytes of fixed fields, then NUL-terminated strings ended by an extra NUL. Type 17 = memory device.
void parse_smbios_memory(const uint8_t* p, size_t len, std::vector<MemModule>& out) {
    size_t i = 0;
    while (i + 4 <= len) {
        uint8_t type = p[i], slen = p[i + 1];
        if (slen < 4 || i + slen > len) break;

        std::vector<std::string> strings;
        size_t j = i + slen;
        while (j < len && p[j] != 0) {
            size_t start = j;
            while (j < len && p[j] != 0) j++;
            if (j >= len) return; // unterminated string: table is cut short
            strings.emplace_back((const char*)p + start, j - start);
            j++;
        }
        size_t next = strings.empty() ? i + slen + 2 : j + 1;

        auto word = [&](size_t off) { return (unsigned)(p[i + off] | p[i + off + 1] << 8); };
        auto str = [&](size_t off) -> std::string {
            if (off >= slen) return {};
            uint8_t idx = p[i + off];
            return idx >= 1 && idx <= strings.size() ? trimmed(strings[idx - 1u]) : std::string();
        };

        if (type == 17 && slen >= 0x15) {
            unsigned size = word(0x0C);
            uint64_t bytes = 0;
            if (size == 0x7FFF && slen >= 0x20) {
                uint32_t mb = (uint32_t)word(0x1C) | (uint32_t)word(0x1E) << 16;
                bytes = (uint64_t)(mb & 0x7FFFFFFFu) << 20;
            } else if (size != 0 && size != 0xFFFF) {
                bytes = size & 0x8000 ? (uint64_t)(size & 0x7FFF) << 10 : (uint64_t)size << 20;
            }
            if (bytes > 0) { // size 0 means an empty slot
                MemModule m;
                m.size = bytes;
                m.type = memory_type_name(p[i + 0x12]);
                if (slen >= 0x17) m.speed = (int)word(0x15);
                if (slen >= 0x22 && word(0x20) != 0 && word(0x20) != 0xFFFF) m.speed = (int)word(0x20);
                if (m.speed == 0xFFFF) m.speed = 0;
                m.locator = str(0x10);
                m.manufacturer = str(0x17);
                m.part = str(0x1A);
                out.push_back(std::move(m));
            }
        }
        if (type == 127) break; // end-of-table marker
        i = next;
    }
}
