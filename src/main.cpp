// labitop: panel-based system monitor (CPU, memory, GPU, storage, network, processes)
// for Windows and Linux.
#include "sys.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <thread>

namespace {

bool g_color = true;
volatile std::sig_atomic_t g_quit = 0;

// ---------------------------------------------------------------- formatting

std::string sf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return buf;
}

std::string fmt_bytes(uint64_t b) {
    const char* units = "BKMGTP";
    double v = (double)b;
    int u = 0;
    while (v >= 1024.0 && u < 5) {
        v /= 1024.0;
        u++;
    }
    if (u == 0) return sf("%lluB", (unsigned long long)b);
    return sf(v < 10 ? "%.2f%c" : v < 100 ? "%.1f%c" : "%.0f%c", v, units[u]);
}

std::string fmt_rate(double bytes_per_s) { return fmt_bytes((uint64_t)std::max(0.0, bytes_per_s)) + "/s"; }

std::string fmt_uptime(uint64_t s) {
    unsigned long long d = s / 86400, h = s % 86400 / 3600, m = s % 3600 / 60, sec = s % 60;
    if (d) return sf("%llud %02llu:%02llu:%02llu", d, h, m, sec);
    return sf("%02llu:%02llu:%02llu", h, m, sec);
}

// "Intel(R) Core(TM) i7-14700K CPU @ 3.40GHz" -> "Intel Core i7-14700K"
std::string short_cpu_name(std::string s) {
    size_t at = s.find(" @ ");
    if (at != std::string::npos) s.erase(at);
    for (const char* junk : { "(R)", "(TM)", "(tm)", " CPU", " Processor" })
        for (size_t p; (p = s.find(junk)) != std::string::npos;) s.erase(p, strlen(junk));
    std::string out;
    for (char c : s)
        if (c != ' ' || (!out.empty() && out.back() != ' ')) out += c;
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// ---------------------------------------------------------------- canvas

// Cell style: low 4 bits = colour, high bits = attributes.
enum : uint8_t { RED = 1, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE, GRAY, BOLD = 0x10, DIM = 0x20, INV = 0x40 };

uint8_t level(double frac) { return frac < 0.5 ? GREEN : frac < 0.8 ? YELLOW : RED; }

int cp_width(uint32_t c) {
    if (c < 0x1100) return 1;
    bool wide = c <= 0x115F || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3) ||
                (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60) ||
                (c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x1F300 && c <= 0x1FAFF) || (c >= 0x20000 && c <= 0x3FFFD);
    return wide ? 2 : 1;
}

// Decodes the UTF-8 sequence at s[i]; returns its byte length (1 for malformed input).
size_t next_cp(const std::string& s, size_t i, uint32_t& cp) {
    unsigned char c = (unsigned char)s[i];
    size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    if (i + len > s.size()) len = 1;
    cp = c;
    if (len == 2) cp = c & 0x1Fu;
    else if (len == 3) cp = c & 0x0Fu;
    else if (len == 4) cp = c & 0x07u;
    for (size_t k = 1; k < len; k++) cp = cp << 6 | ((unsigned char)s[i + k] & 0x3Fu);
    return len;
}

int text_width(const std::string& s) {
    int w = 0;
    uint32_t cp;
    for (size_t i = 0, len; i < s.size(); i += len) {
        len = next_cp(s, i, cp);
        if ((unsigned char)s[i] >= 0x20) w += cp_width(cp);
    }
    return w;
}

// A grid of terminal cells. Panels draw into it, then each row is turned into one output line.
struct Canvas {
    struct Cell {
        char ch[5] = " "; // one UTF-8 character; empty = right half of a double-width character
        uint8_t style = 0;
    };
    int w, h;
    int clip0 = 0, clip1; // put() only touches columns [clip0, clip1), so a panel cannot spill into its neighbour
    std::vector<Cell> cells;

    Canvas(int w_, int h_) : w(std::max(w_, 0)), h(std::max(h_, 0)), clip1(w), cells((size_t)w * (size_t)h) {}

    void clip(int x0, int x1) {
        clip0 = std::max(0, x0);
        clip1 = std::min(w, x1);
    }
    void unclip() { clip(0, w); }

    Cell& at(int x, int y) { return cells[(size_t)y * (size_t)w + (size_t)x]; }

    // Stores one character, keeping double-width characters whole: a half that gets overwritten
    // takes its other half with it.
    void set(int x, int y, const char* bytes, size_t len, uint8_t style, int cw) {
        Cell& c = at(x, y);
        if (!c.ch[0] && x > 0) strcpy(at(x - 1, y).ch, " ");
        int last = x + cw - 1;
        if (last + 1 < w && at(last, y).ch[0] && !at(last + 1, y).ch[0]) strcpy(at(last + 1, y).ch, " ");
        memcpy(c.ch, bytes, len);
        c.ch[len] = '\0';
        c.style = style;
        if (cw == 2) {
            Cell& r = at(x + 1, y);
            r.ch[0] = '\0';
            r.style = style;
        }
    }

    // Writes text at (x, y), limited to `maxw` columns and to the clip range. Returns columns advanced.
    int put(int x, int y, const std::string& s, uint8_t style = 0, int maxw = 1 << 20) {
        if (y < 0 || y >= h) return 0;
        int limit = (int)std::min<long long>(clip1, (long long)x + maxw);
        int cx = x;
        uint32_t cp;
        for (size_t i = 0, len; i < s.size(); i += len) {
            len = next_cp(s, i, cp);
            if ((unsigned char)s[i] < 0x20) continue;
            int cw = cp_width(cp);
            if (cx + cw > limit) break;
            if (cx >= clip0) set(cx, y, s.data() + i, len, style, cw);
            cx += cw;
        }
        return cx - x;
    }

    // Right-aligned: the text ends just before column `right`.
    void putr(int right, int y, const std::string& s, uint8_t style = 0) { put(right - text_width(s), y, s, style); }

    static std::string sgr(uint8_t style) {
        std::string s = "\x1b[0";
        if (style & BOLD) s += ";1";
        if (style & DIM) s += ";2";
        if (style & INV) s += ";7";
        int colour = style & 0x0F;
        if (colour == GRAY) s += ";90";
        else if (colour) s += sf(";3%d", colour);
        return s + "m";
    }

    // One output line, limited to `cols` columns. `trim` drops trailing blanks (for --once).
    std::string row(int y, int cols, bool trim) const {
        cols = std::min(cols, w);
        const Cell* line = cells.data() + (size_t)y * (size_t)w;
        int end = cols;
        if (trim)
            while (end > 0 && line[end - 1].style == 0 && strcmp(line[end - 1].ch, " ") == 0) end--;
        std::string out;
        uint8_t cur = 0;
        for (int x = 0; x < end; x++) {
            const Cell& c = line[x];
            if (!c.ch[0]) continue;
            bool cut_wide = x + 1 < w && !line[x + 1].ch[0] && x + 1 >= end; // wide char over the edge
            if (g_color && c.style != cur) {
                out += sgr(c.style);
                cur = c.style;
            }
            out += cut_wide ? " " : c.ch;
        }
        if (cur) out += "\x1b[0m";
        return out;
    }
};

// ---------------------------------------------------------------- widgets

struct Glyphs {
    const char *h, *v, *tl, *tr, *bl, *br;
    const char *fill, *empty; // bar cells; `empty` is drawn dim
    const char* empty_plain;  // empty bar cell when colours are off and dimming cannot tell it apart
    const char* eighths[9];   // vertical fill levels 0..8 for graphs
};
const Glyphs kUnicode = { "─", "│", "╭", "╮", "╰", "╯", "■", "■", "·",
                          { " ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" } };
const Glyphs kAscii = { "-", "|", "+", "+", "+", "+", "#", "-", "-", { " ", ".", ".", ".", ":", ":", ":", "#", "#" } };
const Glyphs* G = &kUnicode;

void box(Canvas& c, int x, int y, int w, int h, const std::string& title, const std::string& info = "") {
    if (w < 4 || h < 2) return;
    const uint8_t line = GRAY;
    for (int i = 1; i < w - 1; i++) {
        c.put(x + i, y, G->h, line);
        c.put(x + i, y + h - 1, G->h, line);
    }
    for (int i = 1; i < h - 1; i++) {
        c.put(x, y + i, G->v, line);
        c.put(x + w - 1, y + i, G->v, line);
    }
    c.put(x, y, G->tl, line);
    c.put(x + w - 1, y, G->tr, line);
    c.put(x, y + h - 1, G->bl, line);
    c.put(x + w - 1, y + h - 1, G->br, line);
    int n = c.put(x + 2, y, " " + title + " ", BOLD | CYAN, w - 4);
    if (!info.empty()) c.put(x + 2 + n, y, info + " ", 0, w - 4 - n);
}

// Horizontal usage bar. colour 0 = by level (green / yellow / red).
void hbar(Canvas& c, int x, int y, int w, double frac, uint8_t colour = 0) {
    if (w <= 0) return;
    frac = std::min(1.0, std::max(0.0, frac));
    int fill = (int)(frac * w + 0.5);
    if (frac > 0 && fill == 0) fill = 1;
    const char* empty = g_color ? G->empty : G->empty_plain;
    for (int i = 0; i < w; i++)
        c.put(x + i, y, i < fill ? G->fill : empty, i < fill ? (colour ? colour : level(frac)) : (GRAY | DIM));
}

// "label ■■■■■■■■      text" across w columns.
void meter(Canvas& c, int x, int y, int w, const std::string& label, double frac, const std::string& text) {
    int lw = c.put(x, y, label, CYAN, w) + 1;
    int tw = text_width(text);
    int bw = w - lw - tw - 1;
    if (bw >= 3) hbar(c, x + lw, y, bw, frac);
    c.putr(x + w, y, text);
}

struct Hist {
    std::deque<float> d;
    void push(float v) {
        d.push_back(v);
        if (d.size() > 600) d.pop_front();
    }
};

// History graph, newest sample at the right edge. Values are divided by `scale`.
// colour 0 = each column coloured by its own level.
void graph(Canvas& c, int x, int y, int w, int h, const Hist& hist, double scale, uint8_t colour = 0) {
    if (w <= 0 || h <= 0 || scale <= 0) return;
    int n = (int)hist.d.size();
    if (n < 2) return; // a single sample is not a trend (first frame, --once)
    for (int col = 0; col < w; col++) {
        int idx = n - w + col;
        if (idx < 0) continue;
        double v = std::min(1.0, std::max(0.0, (double)hist.d[(size_t)idx] / scale));
        int e = (int)(v * h * 8 + 0.5);
        if (v > 0 && e == 0) e = 1;
        uint8_t style = colour ? colour : level(v);
        if (e == 0) { // idle samples still leave a faint baseline, so the graph area reads as a graph
            c.put(x + col, y + h - 1, G->eighths[1], GRAY | DIM);
            continue;
        }
        for (int r = 0; r < h; r++) {
            int cell = std::min(8, std::max(0, e - r * 8));
            if (cell) c.put(x + col, y + h - 1 - r, G->eighths[cell], style);
        }
    }
}

// Largest value among the last `w` samples (for auto-scaled graphs).
double recent_max(const Hist& hist, int w, double floor) {
    double m = floor;
    int n = (int)hist.d.size();
    for (int i = std::max(0, n - w); i < n; i++) m = std::max(m, (double)hist.d[(size_t)i]);
    return m;
}

// ---------------------------------------------------------------- view model

enum SortKey { S_CPU, S_MEM, S_GPU, S_VRAM, S_PID, S_NAME };
const char* kSortNames[] = { "CPU%", "MEM", "GPU%", "VRAM", "PID", "NAME" };

struct ProcRow {
    uint32_t pid;
    std::string name;
    double cpu;
    uint64_t mem;
    uint32_t threads;
    float gpu;
    uint64_t vram;
};

struct NetRow {
    std::string name;
    double rx, tx; // bytes per second
};

struct PhysRow {
    std::string name, model;
    int temp;
    double read, write; // bytes per second
};

struct View {
    Snapshot snap;
    std::vector<double> core_frac;
    double cpu_frac = 0;
    std::vector<ProcRow> procs;
    std::vector<NetRow> nets;
    std::vector<PhysRow> phys;
    double net_rx = 0, net_tx = 0, io_read = 0, io_write = 0;
    uint64_t threads = 0;
    // histories survive across samples
    Hist h_cpu, h_mem, h_rx, h_tx, h_io;
    std::vector<Hist> h_gpu;
};

double cpu_frac(const CpuTimes& now, const CpuTimes& prev) {
    if (now.total <= prev.total) return 0;
    double total = (double)(now.total - prev.total);
    double idle = now.idle > prev.idle ? (double)(now.idle - prev.idle) : 0;
    return std::min(1.0, std::max(0.0, 1.0 - idle / total));
}

// `record` = false for the very first frame, which has no interval to measure rates over.
void build_view(View& v, Snapshot&& now, const Snapshot& prev, double dt, bool record) {
    v.core_frac.clear();
    v.procs.clear();
    v.nets.clear();
    v.phys.clear();
    v.threads = 0;
    v.net_rx = v.net_tx = v.io_read = v.io_write = 0;
    v.cpu_frac = cpu_frac(now.cpu_total, prev.cpu_total);
    for (size_t i = 0; i < now.cores.size(); i++)
        v.core_frac.push_back(i < prev.cores.size() ? cpu_frac(now.cores[i], prev.cores[i]) : 0);

    std::unordered_map<uint32_t, uint64_t> prev_cpu;
    for (auto& p : prev.procs) prev_cpu[p.pid] = p.cpu_time;
    double ncores = (double)std::max<size_t>(1, now.cores.size());
    double denom = dt * 1e7 * ncores; // 100 ns units of CPU time available in the interval
    for (auto& p : now.procs) {
        ProcRow r { p.pid, p.name, 0, p.mem, p.threads, -1, 0 };
        auto it = prev_cpu.find(p.pid);
        if (it != prev_cpu.end() && p.cpu_time >= it->second && denom > 0)
            r.cpu = std::min(100.0, (double)(p.cpu_time - it->second) / denom * 100.0);
        auto g = now.proc_gpu.find(p.pid);
        if (g != now.proc_gpu.end()) {
            r.gpu = g->second.util;
            r.vram = g->second.mem;
        }
        v.threads += p.threads;
        v.procs.push_back(std::move(r));
    }

    auto rate = [dt](uint64_t a, uint64_t b) { return a >= b && dt > 0 ? (double)(a - b) / dt : 0.0; };

    std::unordered_map<std::string, const NetInfo*> prev_net;
    for (auto& n : prev.nets) prev_net[n.name] = &n;
    for (auto& n : now.nets) {
        NetRow r { n.name, 0, 0 };
        auto it = prev_net.find(n.name);
        if (it != prev_net.end()) {
            r.rx = rate(n.rx, it->second->rx);
            r.tx = rate(n.tx, it->second->tx);
        }
        v.net_rx += r.rx;
        v.net_tx += r.tx;
        v.nets.push_back(std::move(r));
    }
    std::stable_sort(v.nets.begin(), v.nets.end(),
                     [](const NetRow& a, const NetRow& b) { return a.rx + a.tx > b.rx + b.tx; });

    std::unordered_map<std::string, const PhysDisk*> prev_phys;
    for (auto& d : prev.phys) prev_phys[d.name] = &d;
    for (auto& d : now.phys) {
        PhysRow r { d.name, d.model, d.temp, 0, 0 };
        auto it = prev_phys.find(d.name);
        if (it != prev_phys.end()) {
            r.read = rate(d.read, it->second->read);
            r.write = rate(d.write, it->second->write);
        }
        v.io_read += r.read;
        v.io_write += r.write;
        v.phys.push_back(std::move(r));
    }

    if (record) {
        v.h_cpu.push((float)v.cpu_frac);
        v.h_mem.push(now.mem_total ? (float)((double)now.mem_used / (double)now.mem_total) : 0);
        v.h_rx.push((float)v.net_rx);
        v.h_tx.push((float)v.net_tx);
        v.h_io.push((float)(v.io_read + v.io_write));
        if (v.h_gpu.size() < now.gpus.size()) v.h_gpu.resize(now.gpus.size());
        for (size_t i = 0; i < now.gpus.size(); i++) v.h_gpu[i].push(std::max(0, now.gpus[i].util) / 100.0f);
    }
    v.snap = std::move(now);
}

void sort_procs(std::vector<ProcRow>& p, SortKey key) {
    std::stable_sort(p.begin(), p.end(), [key](const ProcRow& a, const ProcRow& b) {
        switch (key) {
        case S_CPU: return a.cpu > b.cpu;
        case S_MEM: return a.mem > b.mem;
        case S_GPU: return a.gpu > b.gpu;
        case S_VRAM: return a.vram > b.vram;
        case S_PID: return a.pid < b.pid;
        case S_NAME: return a.name < b.name;
        }
        return false;
    });
}

struct UiState {
    SortKey sort = S_CPU;
    int scroll = 0;
    double interval = 1.0;
    int page = 10; // visible process rows, updated by render
};

// ---------------------------------------------------------------- panels
// Each panel has pref(view, width, g) = wanted height including the border, and
// draw(canvas, view, x, y, w, h, g). `g` is the height of history graphs in rows (0 = none).

double frac_of(uint64_t used, uint64_t total) { return total ? (double)used / (double)total : 0.0; }
std::string used_of(uint64_t used, uint64_t total) { return fmt_bytes(used) + " / " + fmt_bytes(total); }

// Core grid: at most 4 rows when the cells stay readable, more rows otherwise.
int core_grid(int n, int inner, int& cols) {
    cols = 1;
    if (n <= 0 || inner <= 0) return 0;
    cols = (n + 3) / 4;
    if (inner / cols < 7) cols = std::max(1, inner / 7);
    return (n + cols - 1) / cols;
}

int cpu_pref(const View& v, int w, int g) {
    int cols;
    return 2 + 1 + g + core_grid((int)v.core_frac.size(), w - 2, cols);
}

void cpu_draw(Canvas& c, const View& v, int x, int y, int w, int h, int g) {
    const Snapshot& s = v.snap;
    box(c, x, y, w, h, "CPU", short_cpu_name(sys_info().cpu_name) + sf("  %zu threads", v.core_frac.size()));
    c.clip(x + 1, x + w - 1);
    int ix = x + 1, iw = w - 2, row = y + 1;
    int gh = g + std::max(0, h - cpu_pref(v, w, g)); // spare rows go to the graph

    std::string text = sf("%5.1f%%", v.cpu_frac * 100);
    if (s.cpu_mhz > 0) text += sf("  %.2f GHz", s.cpu_mhz / 1000.0);
    if (s.cpu_temp >= 0) text += sf("  %s%s%d\xC2\xB0""C", s.cpu_temp_label, *s.cpu_temp_label ? " " : "", s.cpu_temp);
    meter(c, ix, row++, iw, "Total", v.cpu_frac, text);

    graph(c, ix, row, iw, gh, v.h_cpu, 1.0);
    row += gh;

    int n = (int)v.core_frac.size(), cols;
    int rows = core_grid(n, iw, cols);
    int cell = iw / cols;
    int lw = n > 100 ? 3 : 2;
    bool pct = cell >= 13;
    for (int r = 0; r < rows && row + r < y + h - 1; r++)
        for (int col = 0; col < cols; col++) {
            int i = col * rows + r;
            if (i >= n) continue;
            int cx = ix + col * cell;
            double f = v.core_frac[(size_t)i];
            c.put(cx, row + r, sf("%*d", lw, i), GRAY);
            int bw = cell - lw - 2 - (pct ? 5 : 0);
            hbar(c, cx + lw + 1, row + r, bw, f);
            if (pct) c.put(cx + lw + 1 + bw, row + r, sf("%4.0f%%", f * 100));
        }
}

// "DDR5 5600 MHz 2x16G"
std::string memory_summary() {
    const auto& m = sys_info().modules;
    if (m.empty()) return "";
    std::string out = m[0].type;
    if (m[0].speed > 0) out += sf("%s%d MHz", out.empty() ? "" : " ", m[0].speed);
    bool same = true;
    for (auto& x : m) same = same && x.size == m[0].size;
    if (same) out += sf(" %zux%s", m.size(), fmt_bytes(m[0].size).c_str());
    else out += sf(" %zu modules", m.size());
    return out;
}

int mem_pref(const View&, int, int g) { return 2 + 3 + g + (int)std::min<size_t>(sys_info().modules.size(), 4); }

void mem_draw(Canvas& c, const View& v, int x, int y, int w, int h, int g) {
    const Snapshot& s = v.snap;
    box(c, x, y, w, h, "Memory", memory_summary());
    c.clip(x + 1, x + w - 1);
    int ix = x + 1, iw = w - 2, row = y + 1;
    int gh = g + std::max(0, h - mem_pref(v, w, g));
    meter(c, ix, row++, iw, "RAM", frac_of(s.mem_used, s.mem_total), used_of(s.mem_used, s.mem_total));
    meter(c, ix, row++, iw, s.swap_label, frac_of(s.swap_used, s.swap_total), used_of(s.swap_used, s.swap_total));
    uint64_t avail = s.mem_total - std::min(s.mem_used, s.mem_total);
    c.put(ix, row++, sf("Available %s   Cached %s   Used %.0f%%", fmt_bytes(avail).c_str(), fmt_bytes(s.mem_cached).c_str(),
                        frac_of(s.mem_used, s.mem_total) * 100), GRAY, iw);
    graph(c, ix, row, iw, gh, v.h_mem, 1.0);
    row += gh;
    const auto& mods = sys_info().modules;
    for (size_t i = 0; i < mods.size() && i < 4 && row < y + h - 1; i++) {
        const MemModule& m = mods[i];
        std::string line = sf("%-8s %5s", m.locator.c_str(), fmt_bytes(m.size).c_str());
        if (!m.type.empty()) line += " " + m.type + (m.speed > 0 ? sf("-%d", m.speed) : std::string());
        if (!m.manufacturer.empty()) line += "  " + m.manufacturer;
        if (!m.part.empty()) line += " " + m.part;
        c.put(ix, row++, line, 0, iw);
    }
}

int gpu_pref(const View& v, int, int g) { return 2 + std::max(1, (int)v.snap.gpus.size() * (3 + g)); }

void gpu_draw(Canvas& c, const View& v, int x, int y, int w, int h, int g) {
    const Snapshot& s = v.snap;
    box(c, x, y, w, h, "GPU");
    c.clip(x + 1, x + w - 1);
    int ix = x + 1, iw = w - 2, row = y + 1;
    if (s.gpus.empty()) {
        c.put(ix, row, "no GPU data", GRAY, iw);
        return;
    }
    int gh = g + std::max(0, h - gpu_pref(v, w, g)) / (int)s.gpus.size();
    for (size_t i = 0; i < s.gpus.size() && row + 2 < y + h - 1; i++) {
        const GpuInfo& gp = s.gpus[i];
        std::string name = gp.name;
        for (const char* prefix : { "NVIDIA GeForce ", "NVIDIA " }) // keep the model number visible
            if (name.rfind(prefix, 0) == 0) {
                name.erase(0, strlen(prefix));
                break;
            }
        std::string stats;
        if (gp.temp >= 0) stats += sf("%d\xC2\xB0""C", gp.temp);
        if (gp.power_mw >= 0) {
            stats += sf("  %dW", (gp.power_mw + 500) / 1000);
            if (gp.power_limit_mw > 0) stats += sf("/%dW", (gp.power_limit_mw + 500) / 1000);
        }
        if (gp.fan >= 0) stats += sf("  fan %d%%", gp.fan);
        c.put(ix, row, name, BOLD, iw - text_width(stats) - 1);
        c.putr(ix + iw, row, stats, gp.temp >= 0 ? level(gp.temp / 100.0) : 0);
        row++;
        meter(c, ix, row++, iw, "Util", gp.util < 0 ? 0 : gp.util / 100.0, gp.util < 0 ? "n/a" : sf("%3d%%", gp.util));
        meter(c, ix, row++, iw, "VRAM", frac_of(gp.mem_used, gp.mem_total),
              gp.mem_total ? used_of(gp.mem_used, gp.mem_total) : fmt_bytes(gp.mem_used));
        if (i < v.h_gpu.size()) graph(c, ix, row, iw, gh, v.h_gpu[i], 1.0);
        row += gh;
    }
}

// In a narrow panel each physical drive gets two lines, so the model name is not squeezed out.
bool phys_two_line(const View& v, int w) { return w - 2 < 56 && v.phys.size() <= 3; }

int disk_pref(const View& v, int w, int g) {
    int vols = (int)v.snap.disks.size();
    return 2 + vols * (vols <= 3 ? 2 : 1) + (int)v.phys.size() * (phys_two_line(v, w) ? 2 : 1) + (v.phys.empty() ? 0 : g);
}

void disk_draw(Canvas& c, const View& v, int x, int y, int w, int h, int g) {
    const Snapshot& s = v.snap;
    box(c, x, y, w, h, "Storage",
        v.phys.empty() ? "" : "read " + fmt_rate(v.io_read) + "  write " + fmt_rate(v.io_write));
    c.clip(x + 1, x + w - 1);
    int ix = x + 1, iw = w - 2, row = y + 1;
    int gh = g + std::max(0, h - disk_pref(v, w, g));
    bool two_line = s.disks.size() <= 3;
    for (auto& d : s.disks) {
        if (row >= y + h - 1) break;
        uint64_t used = d.total - std::min(d.free, d.total);
        double f = frac_of(used, d.total);
        if (two_line) {
            int n = c.put(ix, row, d.mount, BOLD | CYAN, iw);
            c.put(ix + n + 1, row, d.fs, GRAY, iw - n - 1);
            c.putr(ix + iw, row, sf("%s  free %s  %.0f%%", used_of(used, d.total).c_str(), fmt_bytes(d.free).c_str(), f * 100));
            row++;
            hbar(c, ix, row++, iw, f);
        } else {
            meter(c, ix, row++, iw, d.mount, f, sf("%s %3.0f%%", used_of(used, d.total).c_str(), f * 100));
        }
    }
    // Physical drives. The model column gets the same width on every row.
    std::vector<std::string> rights;
    int right_w = 0;
    for (auto& p : v.phys) {
        std::string right;
        if (p.temp >= 0) right += sf("%d\xC2\xB0""C  ", p.temp);
        right += "R " + fmt_rate(p.read) + "  W " + fmt_rate(p.write);
        right_w = std::max(right_w, text_width(right));
        rights.push_back(std::move(right));
    }
    bool two = phys_two_line(v, w);
    for (size_t i = 0; i < v.phys.size() && row < y + h - 1; i++, row++) {
        const PhysRow& p = v.phys[i];
        int n = c.put(ix, row, p.name, CYAN, iw);
        if (!two) {
            c.put(ix + n + 1, row, p.model, 0, iw - n - 2 - right_w);
            c.putr(ix + iw, row, rights[i], GRAY);
            continue;
        }
        std::string temp = p.temp >= 0 ? sf("%d\xC2\xB0""C", p.temp) : "";
        c.put(ix + n + 1, row, p.model, 0, iw - n - 2 - text_width(temp));
        c.putr(ix + iw, row, temp, p.temp >= 0 ? level(p.temp / 80.0) : 0);
        if (++row < y + h - 1)
            c.put(ix + n + 1, row, "read " + fmt_rate(p.read) + "   write " + fmt_rate(p.write), GRAY, iw - n - 1);
    }
    // Total read + write throughput, scaled to the busiest moment on screen.
    if (!v.phys.empty()) graph(c, ix, row, iw, gh, v.h_io, recent_max(v.h_io, iw, 1024.0 * 1024), BLUE);
}

int net_pref(const View& v, int, int g) { return 2 + 2 + 2 * g + (int)std::min<size_t>(v.nets.size(), 3); }

void net_draw(Canvas& c, const View& v, int x, int y, int w, int h, int g) {
    box(c, x, y, w, h, "Network");
    c.clip(x + 1, x + w - 1);
    int ix = x + 1, iw = w - 2, row = y + 1;
    int extra = std::max(0, h - net_pref(v, w, g));
    int gh_rx = g + (extra + 1) / 2, gh_tx = g + extra / 2;
    // Each graph is scaled to its own busiest moment on screen ("scale").
    double rx_max = recent_max(v.h_rx, iw, 10 * 1024.0), tx_max = recent_max(v.h_tx, iw, 10 * 1024.0);
    c.put(ix, row, "Down " + fmt_rate(v.net_rx), GREEN | BOLD, iw);
    c.putr(ix + iw, row++, "scale " + fmt_rate(rx_max), GRAY);
    graph(c, ix, row, iw, gh_rx, v.h_rx, rx_max, GREEN);
    row += gh_rx;
    c.put(ix, row, "Up   " + fmt_rate(v.net_tx), MAGENTA | BOLD, iw);
    c.putr(ix + iw, row++, "scale " + fmt_rate(tx_max), GRAY);
    graph(c, ix, row, iw, gh_tx, v.h_tx, tx_max, MAGENTA);
    row += gh_tx;
    for (size_t i = 0; i < v.nets.size() && i < 3 && row < y + h - 1; i++) {
        const NetRow& r = v.nets[i];
        std::string right = "rx " + fmt_rate(r.rx) + "  tx " + fmt_rate(r.tx);
        c.put(ix, row, r.name, CYAN, iw - text_width(right) - 1);
        c.putr(ix + iw, row++, right, GRAY);
    }
}

void procs_draw(Canvas& c, View& v, UiState& ui, int x, int y, int w, int h) {
    box(c, x, y, w, h, "Processes",
        sf("%zu tasks, %llu threads, sorted by %s", v.procs.size(), (unsigned long long)v.threads, kSortNames[ui.sort]));
    c.clip(x + 1, x + w - 1);
    int ix = x + 1, iw = w - 2;
    int rows = h - 3; // borders + header
    ui.page = std::max(1, rows);
    if (rows < 1 || iw < 10) return;
    std::string header = "    PID  CPU%     MEM  THR  GPU%    VRAM  NAME";
    header.resize((size_t)std::max(iw, (int)header.size()), ' ');
    c.put(ix, y + 1, header, INV, iw);
    sort_procs(v.procs, ui.sort);
    int total = (int)v.procs.size();
    ui.scroll = std::max(0, std::min(ui.scroll, total - rows));
    for (int i = 0; i < rows && ui.scroll + i < total; i++) {
        const ProcRow& p = v.procs[(size_t)(ui.scroll + i)];
        int row = y + 2 + i, cx = ix;
        cx += c.put(cx, row, sf("%7u ", p.pid), GRAY);
        cx += c.put(cx, row, sf("%5.1f", p.cpu), p.cpu >= 0.05 ? level(p.cpu / 100.0) : (uint8_t)GRAY);
        cx += c.put(cx, row, sf(" %7s %4u ", fmt_bytes(p.mem).c_str(), p.threads));
        cx += c.put(cx, row, sf("%5s %7s  ", p.gpu < 0 ? "-" : sf("%.1f", p.gpu).c_str(), p.vram ? fmt_bytes(p.vram).c_str() : "-"),
                    p.gpu >= 0.05f || p.vram ? 0 : (uint8_t)GRAY);
        c.put(cx, row, p.name, 0, ix + iw - cx);
    }
}

struct Panel {
    int (*pref)(const View&, int w, int g);
    void (*draw)(Canvas&, const View&, int x, int y, int w, int h, int g);
    int weight;
};
const Panel kCpu = { cpu_pref, cpu_draw, 3 }, kMem = { mem_pref, mem_draw, 2 }, kGpu = { gpu_pref, gpu_draw, 1 },
            kDisk = { disk_pref, disk_draw, 1 }, kNet = { net_pref, net_draw, 1 };

// Panels per row, depending on the terminal width.
std::vector<std::vector<Panel>> layout(int W) {
    if (W >= 120) return { { kCpu, kMem }, { kGpu, kDisk, kNet } };
    if (W >= 80) return { { kCpu }, { kMem, kGpu }, { kDisk, kNet } };
    return { { kCpu }, { kMem }, { kGpu }, { kDisk }, { kNet } };
}

// `full` prints every process with no key help (used by --once); H is ignored then.
std::vector<std::string> render(View& v, UiState& ui, const std::string& host, int W, int H, bool full = false) {
    const int term_w = W; // output is clipped to the real width even when the layout uses the minimum
    W = std::max(W, 40);
    auto rows = layout(W);

    struct Placed {
        const Panel* p;
        int x, w;
    };
    std::vector<std::vector<Placed>> placed;
    for (auto& r : rows) {
        int total = 0, x = 0;
        for (auto& p : r) total += p.weight;
        std::vector<Placed> pr;
        for (size_t i = 0; i < r.size(); i++) {
            int w = i + 1 == r.size() ? W - x : W * r[i].weight / total;
            pr.push_back({ &r[i], x, w });
            x += w;
        }
        placed.push_back(std::move(pr));
    }

    // Shrink the history graphs until the process list has room. A one-shot frame has no history.
    int g = full ? 0 : 4, top = 0;
    std::vector<int> heights;
    for (;; g--) {
        heights.clear();
        top = 1; // title line
        for (auto& pr : placed) {
            int hmax = 0;
            for (auto& pl : pr) hmax = std::max(hmax, pl.p->pref(v, pl.w, g));
            heights.push_back(hmax);
            top += hmax;
        }
        if (full || g == 0 || H - top - 1 >= 8) break;
    }
    int proc_h = full ? (int)v.procs.size() + 3 : H - top - 1;
    if (full) H = top + proc_h;

    Canvas c(W, H);
    char clock[16] = "";
    time_t t = time(nullptr);
    if (tm* lt = localtime(&t)) strftime(clock, sizeof clock, "%H:%M:%S", lt);
    int n = c.put(1, 0, "labitop", BOLD | CYAN);
    c.put(1 + n + 2, 0, host + "  up " + fmt_uptime(v.snap.uptime_s));
    c.putr(W - 1, 0, sf("%s  every %.2gs", clock, ui.interval), GRAY);

    int y = 1;
    for (size_t r = 0; r < placed.size(); r++) {
        for (auto& pl : placed[r]) {
            c.unclip();
            pl.p->draw(c, v, pl.x, y, pl.w, heights[r], g);
        }
        y += heights[r];
    }
    c.unclip();
    if (proc_h >= 4) procs_draw(c, v, ui, 0, y, W, proc_h);
    c.unclip();
    if (!full)
        c.put(1, H - 1, "q quit | sort: c cpu  m mem  g gpu  v vram  p pid  n name | arrows PgUp PgDn scroll | +/- interval", GRAY);

    std::vector<std::string> out;
    for (int i = 0; i < H; i++) out.push_back(c.row(i, term_w, full));
    return out;
}

// Every line spans the full terminal width, so nothing is erased per line: an erase issued right
// after a character lands in the last column would wipe that character on some terminals.
// `clear` wipes the whole screen first (after a resize).
void draw(const std::vector<std::string>& lines, bool clear) {
    std::string frame = clear ? "\x1b[2J\x1b[H" : "\x1b[H";
    for (size_t i = 0; i < lines.size(); i++) {
        frame += lines[i];
        if (i + 1 < lines.size()) frame += "\r\n";
    }
    term_write(frame);
}

void usage() {
    puts("labitop - CPU / memory / GPU / storage / network / process monitor\n"
         "\n"
         "  labitop [-i seconds] [--once] [--ascii]\n"
         "\n"
         "  -i, --interval <s>  refresh interval in seconds (default 1)\n"
         "  --once              print one frame to stdout and exit\n"
         "  --ascii             draw with plain ASCII (for terminals that show broken boxes)\n"
         "  -h, --help          show this help\n"
         "\n"
         "Keys: q quit, c/m/g/v/p/n sort, arrows PgUp PgDn Home End scroll, +/- interval");
}

} // namespace

int main(int argc, char** argv) {
    UiState ui;
    bool once = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if ((a == "-i" || a == "--interval") && i + 1 < argc) {
            ui.interval = atof(argv[++i]);
            if (!(ui.interval >= 0.1)) ui.interval = 0.1;
            if (ui.interval > 60) ui.interval = 60;
        } else if (a == "--once") {
            once = true;
        } else if (a == "--ascii") {
            G = &kAscii;
        } else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else {
            fprintf(stderr, "labitop: unknown option '%s'\n", a.c_str());
            usage();
            return 2;
        }
    }

    g_color = term_is_tty();
    sys_init();
    std::string host = sys_hostname();

    using clock = std::chrono::steady_clock;
    Snapshot prev;
    sys_sample(prev);
    auto prev_time = clock::now();
    View view;
    int W, H;

    if (once) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        Snapshot now;
        sys_sample(now);
        build_view(view, std::move(now), prev, std::chrono::duration<double>(clock::now() - prev_time).count(), true);
        term_size(W, H);
        std::string text;
        for (auto& l : render(view, ui, host, W, H, true)) text += l + "\n";
        term_write(text);
        term_restore();
        return 0;
    }

    atexit(term_restore);
    signal(SIGINT, [](int) { g_quit = 1; });
    signal(SIGTERM, [](int) { g_quit = 1; });
    term_init();

    // Short first interval so real numbers show up quickly.
    auto next = prev_time + std::chrono::milliseconds(300);
    build_view(view, Snapshot(prev), prev, 1.0, false);
    int last_w = 0, last_h = 0;
    while (!g_quit) {
        auto now_time = clock::now();
        if (now_time >= next) {
            Snapshot now;
            sys_sample(now);
            now_time = clock::now();
            Snapshot keep = now;
            build_view(view, std::move(now), prev, std::chrono::duration<double>(now_time - prev_time).count(), true);
            prev = std::move(keep);
            prev_time = now_time;
            next = now_time + std::chrono::milliseconds((int)(ui.interval * 1000));
        }
        term_size(W, H);
        draw(render(view, ui, host, W, H), W != last_w || H != last_h);
        last_w = W;
        last_h = H;

        int wait = (int)std::chrono::duration_cast<std::chrono::milliseconds>(next - clock::now()).count();
        int key = term_read_key(std::max(0, wait));
        switch (key) {
        case 'q': case 'Q': case 27: g_quit = 1; break;
        case 'c': ui.sort = S_CPU; ui.scroll = 0; break;
        case 'm': ui.sort = S_MEM; ui.scroll = 0; break;
        case 'g': ui.sort = S_GPU; ui.scroll = 0; break;
        case 'v': ui.sort = S_VRAM; ui.scroll = 0; break;
        case 'p': ui.sort = S_PID; ui.scroll = 0; break;
        case 'n': ui.sort = S_NAME; ui.scroll = 0; break;
        case K_UP: case 'k': ui.scroll--; break;
        case K_DOWN: case 'j': ui.scroll++; break;
        case K_PGUP: ui.scroll -= ui.page; break;
        case K_PGDN: ui.scroll += ui.page; break;
        case K_HOME: ui.scroll = 0; break;
        case K_END: ui.scroll = 1 << 30; break;
        case '+': case '=':
            ui.interval = std::min(10.0, ui.interval * 2);
            break;
        case '-': case '_':
            ui.interval = std::max(0.25, ui.interval / 2);
            next = clock::now();
            break;
        }
        if (ui.scroll < 0) ui.scroll = 0;
    }
    term_restore();
    return 0;
}
