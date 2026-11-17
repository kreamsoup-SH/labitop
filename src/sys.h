// labitop: shared data model and platform interfaces.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct CpuTimes {
    uint64_t idle = 0, total = 0; // cumulative, arbitrary unit (only ratios are used)
};

struct DiskInfo {
    std::string mount, fs;
    uint64_t total = 0, free = 0; // bytes
};

// A physical drive (as opposed to a mounted volume).
struct PhysDisk {
    std::string name, model;
    int temp = -1;                // Celsius, -1 = unknown
    uint64_t read = 0, write = 0; // cumulative bytes
};

struct NetInfo {
    std::string name;
    uint64_t rx = 0, tx = 0; // cumulative bytes
};

struct ProcInfo {
    uint32_t pid = 0;
    std::string name;
    uint64_t cpu_time = 0; // cumulative kernel+user, 100 ns units
    uint64_t mem = 0;      // bytes
    uint32_t threads = 0;
};

struct GpuInfo {
    std::string name;
    int util = -1; // percent, -1 = unknown
    uint64_t mem_used = 0, mem_total = 0;
    int temp = -1;           // Celsius
    int power_mw = -1;       // milliwatts
    int power_limit_mw = -1; // milliwatts
    int fan = -1;            // percent
};

// How a process uses the GPU (bit flags in ProcGpu::use).
enum GpuUse : uint8_t { GPU_GRAPHICS = 1, GPU_COMPUTE = 2 };

struct ProcGpu {
    float util = -1;    // percent of the busiest engine, -1 = unknown
    uint64_t mem = 0;   // dedicated VRAM, bytes
    uint8_t use = 0;    // GpuUse flags, 0 = unknown
    std::string engine; // busiest engine right now ("3D", "Compute", "Decode", ...), empty when idle
};

// One installed memory module, from the firmware (SMBIOS) tables.
struct MemModule {
    uint64_t size = 0; // bytes
    std::string type;  // "DDR4", "DDR5", ...
    int speed = 0;     // MHz the module is running at (configured speed when known), 0 = unknown
    std::string locator, manufacturer, part;
};

// Hardware facts that do not change while running.
struct SysInfo {
    std::string cpu_name;
    double cpu_base_mhz = 0;
    std::vector<MemModule> modules;
};

struct Snapshot {
    std::vector<CpuTimes> cores;
    CpuTimes cpu_total;
    double cpu_mhz = 0;               // current average clock, 0 = unknown
    int cpu_temp = -1;                // Celsius, -1 = unknown
    const char* cpu_temp_label = "";  // names the sensor when it is not a real CPU core sensor
    uint64_t mem_total = 0, mem_used = 0, mem_cached = 0;
    uint64_t swap_total = 0, swap_used = 0;
    const char* swap_label = "Swp";
    uint64_t uptime_s = 0;
    std::vector<DiskInfo> disks;
    std::vector<PhysDisk> phys;
    std::vector<NetInfo> nets;
    std::vector<ProcInfo> procs;
    std::vector<GpuInfo> gpus;
    std::unordered_map<uint32_t, ProcGpu> proc_gpu;
};

// Platform backend (sys_win.cpp / sys_linux.cpp)
void sys_init();
void sys_sample(Snapshot& s);
std::string sys_hostname();
const SysInfo& sys_info(); // valid after sys_init

// Portable text/binary parsers, compiled on every platform so they can be tested anywhere (parse.cpp).
// Parses one /proc/<pid>/stat line; hz = clock ticks per second, page = page size in bytes.
bool parse_proc_stat(const char* text, ProcInfo& out, long hz, long page);
// Decodes the \ooo octal escapes /proc/mounts uses for spaces and similar characters.
std::string unescape_mount(const std::string& s);
// Reads the populated memory slots from a raw SMBIOS structure table.
void parse_smbios_memory(const uint8_t* data, size_t len, std::vector<MemModule>& out);

// NVIDIA backend (nvml.cpp). Loads the driver's NVML library at runtime; no-op if absent.
void nvml_sample(std::vector<GpuInfo>& gpus, std::unordered_map<uint32_t, ProcGpu>& procs);

// Terminal layer (term.cpp)
enum Key { K_NONE = -1, K_UP = 1000, K_DOWN, K_LEFT, K_RIGHT, K_BACKTAB, K_PGUP, K_PGDN, K_HOME, K_END, K_RESIZE };
bool term_is_tty();
void term_init();    // raw input, alternate screen, hidden cursor
void term_restore(); // safe to call more than once
void term_size(int& w, int& h);
void term_write(const std::string& s);
int term_read_key(int timeout_ms); // returns a Key, an ASCII code, or K_NONE on timeout
