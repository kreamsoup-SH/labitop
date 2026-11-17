// labitop: NVIDIA GPU data through NVML, loaded at runtime from the installed driver.
// No NVIDIA SDK is needed to build, and the program runs fine without an NVIDIA GPU.
#include "sys.h"

#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
typedef HMODULE LibHandle;
static LibHandle lib_open() {
    // System32 only, so a planted nvml.dll next to the exe is never loaded.
    LibHandle h = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) h = LoadLibraryExW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll", nullptr,
                               LOAD_WITH_ALTERED_SEARCH_PATH);
    return h;
}
static void* lib_sym(LibHandle h, const char* name) { return (void*)GetProcAddress(h, name); }
#else
#include <dlfcn.h>
typedef void* LibHandle;
static LibHandle lib_open() {
    LibHandle h = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
    if (!h) h = dlopen("libnvidia-ml.so", RTLD_LAZY);
    return h;
}
static void* lib_sym(LibHandle h, const char* name) { return dlsym(h, name); }
#endif

namespace {

typedef void* Device;
struct Utilization { unsigned gpu, memory; };
struct Memory { unsigned long long total, free, used; };
struct ProcessInfoV3 {
    unsigned pid;
    unsigned long long usedGpuMemory;
    unsigned gpuInstanceId, computeInstanceId;
};
struct ProcessUtilSample {
    unsigned pid;
    unsigned long long timeStamp; // microseconds
    unsigned smUtil, memUtil, encUtil, decUtil;
};

const int kSuccess = 0, kNotSupported = 3, kInsufficientSize = 7;
const unsigned long long kMemUnavailable = ~0ULL;

int (*p_init)();
int (*p_count)(unsigned*);
int (*p_handle)(unsigned, Device*);
int (*p_name)(Device, char*, unsigned);
int (*p_util)(Device, Utilization*);
int (*p_mem)(Device, Memory*);
int (*p_temp)(Device, int, unsigned*);
int (*p_power)(Device, unsigned*);
int (*p_power_limit)(Device, unsigned*);
int (*p_fan)(Device, unsigned*);
int (*p_compute_procs)(Device, unsigned*, ProcessInfoV3*);
int (*p_graphics_procs)(Device, unsigned*, ProcessInfoV3*);
int (*p_proc_util)(Device, ProcessUtilSample*, unsigned*, unsigned long long);

int g_state = 0; // 0 = not tried, 1 = ready, -1 = unavailable
std::vector<unsigned long long> g_last_util_ts; // per device: newest per-process sample already read

template <class T>
void bind(LibHandle h, T& fn, const char* name) { fn = (T)lib_sym(h, name); }

bool init() {
    if (g_state != 0) return g_state > 0;
    g_state = -1;
    LibHandle h = lib_open();
    if (!h) return false;
    bind(h, p_init, "nvmlInit_v2");
    bind(h, p_count, "nvmlDeviceGetCount_v2");
    bind(h, p_handle, "nvmlDeviceGetHandleByIndex_v2");
    bind(h, p_name, "nvmlDeviceGetName");
    bind(h, p_util, "nvmlDeviceGetUtilizationRates");
    bind(h, p_mem, "nvmlDeviceGetMemoryInfo");
    bind(h, p_temp, "nvmlDeviceGetTemperature");
    bind(h, p_power, "nvmlDeviceGetPowerUsage");
    bind(h, p_power_limit, "nvmlDeviceGetEnforcedPowerLimit");
    bind(h, p_fan, "nvmlDeviceGetFanSpeed");
    bind(h, p_compute_procs, "nvmlDeviceGetComputeRunningProcesses_v3");
    bind(h, p_graphics_procs, "nvmlDeviceGetGraphicsRunningProcesses_v3");
    bind(h, p_proc_util, "nvmlDeviceGetProcessUtilization");
    if (!p_init || !p_count || !p_handle) return false;
    if (p_init() != kSuccess) return false;
    g_state = 1;
    return true;
}

void add_procs(int (*fn)(Device, unsigned*, ProcessInfoV3*), Device dev, uint8_t use,
               std::unordered_map<uint32_t, ProcGpu>& out) {
    if (!fn) return;
    unsigned count = 0;
    int r = fn(dev, &count, nullptr);
    if (r != kInsufficientSize || count == 0) return;
    std::vector<ProcessInfoV3> list(count + 16);
    count = (unsigned)list.size();
    if (fn(dev, &count, list.data()) != kSuccess) return;
    for (unsigned i = 0; i < count && i < list.size(); i++) {
        ProcGpu& pg = out[list[i].pid];
        // Under Windows WDDM the per-process figure is reported as unavailable.
        if (list[i].usedGpuMemory != kMemUnavailable) pg.mem = std::max<uint64_t>(pg.mem, list[i].usedGpuMemory);
#ifndef _WIN32
        // Under WDDM every process with a GPU context is in both lists, so they say nothing
        // about compute vs graphics there; sys_win.cpp classifies processes itself.
        pg.use |= use;
#else
        (void)use;
#endif
    }
}

// Per-process engine load (SM = shader/compute cores, Enc/Dec = video codec units).
void add_proc_util(Device dev, unsigned long long& last, std::unordered_map<uint32_t, ProcGpu>& out) {
    if (!p_proc_util) return;
    unsigned count = 0;
    int r = p_proc_util(dev, nullptr, &count, last);
    if (r == kNotSupported) p_proc_util = nullptr; // e.g. Windows WDDM: stop asking
    if (r != kInsufficientSize || count == 0) return;
    std::vector<ProcessUtilSample> samples(count);
    if (p_proc_util(dev, samples.data(), &count, last) != kSuccess) return;
    // The driver keeps several samples per process; use the newest of each.
    std::unordered_map<unsigned, const ProcessUtilSample*> newest;
    for (unsigned i = 0; i < count && i < samples.size(); i++) {
        const ProcessUtilSample*& n = newest[samples[i].pid];
        if (!n || samples[i].timeStamp > n->timeStamp) n = &samples[i];
        last = std::max(last, samples[i].timeStamp);
    }
    for (auto& e : newest) {
        const ProcessUtilSample& s = *e.second;
        unsigned top = std::max(s.smUtil, std::max(s.encUtil, s.decUtil));
        ProcGpu& pg = out[s.pid];
        if ((float)top > pg.util) {
            pg.util = (float)top;
            pg.engine = top == 0 ? "" : s.smUtil == top ? "SM" : s.encUtil == top ? "Encode" : "Decode";
        }
    }
}

} // namespace

void nvml_sample(std::vector<GpuInfo>& gpus, std::unordered_map<uint32_t, ProcGpu>& procs) {
    if (!init()) return;
    unsigned n = 0;
    if (p_count(&n) != kSuccess) return;
    for (unsigned i = 0; i < n; i++) {
        Device dev = nullptr;
        if (p_handle(i, &dev) != kSuccess) continue;
        GpuInfo g;
        char name[96] = "";
        if (p_name && p_name(dev, name, sizeof name) == kSuccess) g.name = name;
        if (g.name.empty()) g.name = "NVIDIA GPU";
        Utilization u {};
        if (p_util && p_util(dev, &u) == kSuccess) g.util = (int)u.gpu;
        Memory m {};
        if (p_mem && p_mem(dev, &m) == kSuccess) {
            g.mem_used = m.used;
            g.mem_total = m.total;
        }
        unsigned v = 0;
        if (p_temp && p_temp(dev, 0 /* GPU core sensor */, &v) == kSuccess) g.temp = (int)v;
        if (p_power && p_power(dev, &v) == kSuccess) g.power_mw = (int)v;
        if (p_power_limit && p_power_limit(dev, &v) == kSuccess) g.power_limit_mw = (int)v;
        if (p_fan && p_fan(dev, &v) == kSuccess) g.fan = (int)v;
        gpus.push_back(std::move(g));
        add_procs(p_compute_procs, dev, GPU_COMPUTE, procs);
        add_procs(p_graphics_procs, dev, GPU_GRAPHICS, procs);
        if (g_last_util_ts.size() <= i) g_last_util_ts.resize(i + 1, 0);
        add_proc_util(dev, g_last_util_ts[i], procs);
    }
}
