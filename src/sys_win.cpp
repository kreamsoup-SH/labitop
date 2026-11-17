// labitop: Windows backend. Uses only APIs shipped with Windows (ntdll, psapi, iphlpapi, pdh).
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <dxgi.h>
#include <iphlpapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <winioctl.h>
#include <winternl.h>

#include "sys.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <cwctype>

namespace {

// Layouts of the NtQuerySystemInformation results (stable since Windows 7, x86 and x64).
struct CpuPerfInfo {
    LARGE_INTEGER IdleTime, KernelTime, UserTime, DpcTime, InterruptTime;
    ULONG InterruptCount;
};

struct ProcessInfo {
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    LARGE_INTEGER WorkingSetPrivateSize;
    ULONG HardFaultCount;
    ULONG NumberOfThreadsHighWatermark;
    ULONGLONG CycleTime;
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;
    UNICODE_STRING ImageName;
    LONG BasePriority;
    HANDLE UniqueProcessId;
    HANDLE InheritedFromUniqueProcessId;
    ULONG HandleCount;
    ULONG SessionId;
    ULONG_PTR UniqueProcessKey;
    SIZE_T PeakVirtualSize;
    SIZE_T VirtualSize;
    ULONG PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
};

const ULONG kSystemProcessInformation = 5;
const ULONG kSystemProcessorPerformanceInformation = 8;
const NTSTATUS kStatusInfoLengthMismatch = (NTSTATUS)0xC0000004;

PDH_HQUERY g_query = nullptr;
PDH_HCOUNTER g_gpu_engine = nullptr, g_gpu_proc_mem = nullptr, g_gpu_adapter_mem = nullptr;

struct Adapter {
    uint64_t luid; // matches the "luid_<high>_<low>" part of GPU counter instance names
    std::string name;
    uint64_t vram;
    bool nvidia;
};
std::vector<Adapter> g_adapters;

PDH_HCOUNTER g_cpu_perf = nullptr, g_thermal = nullptr;
SysInfo g_info;

struct Drive {
    HANDLE handle;
    std::string name, model;
};
std::vector<Drive> g_drives;

// Reply layout of IOCTL_STORAGE_QUERY_PROPERTY for StorageDeviceTemperatureProperty.
const int kStorageDeviceTemperatureProperty = 52;
struct TemperatureInfo {
    WORD Index;
    SHORT Temperature; // Celsius
    SHORT OverThreshold, UnderThreshold;
    BOOLEAN Flags[4];
    DWORD Reserved;
};
struct TemperatureDescriptor {
    DWORD Version, Size;
    SHORT CriticalTemperature, WarningTemperature;
    WORD InfoCount;
    BYTE Reserved0[2];
    DWORD Reserved1[2];
    TemperatureInfo Info[8];
};

std::string utf8(const wchar_t* w, int len = -1) {
    if (!w || len == 0) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], n, nullptr, nullptr);
    if (len == -1 && !s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

typedef NTSTATUS(NTAPI* QueryEx)(ULONG, PVOID, ULONG, PVOID, ULONG, PULONG);

// Windows splits more than 64 logical processors into groups; each group is queried separately.
void sample_cpu(Snapshot& s) {
    static const QueryEx query_ex =
        (QueryEx)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformationEx");
    WORD groups = std::max<WORD>(1, GetActiveProcessorGroupCount());
    for (WORD g = 0; g < groups; g++) {
        std::vector<CpuPerfInfo> buf(std::max<DWORD>(1, GetActiveProcessorCount(g)));
        ULONG size = (ULONG)(buf.size() * sizeof(CpuPerfInfo)), ret = 0;
        NTSTATUS st = -1;
        if (query_ex) {
            USHORT group = g;
            st = query_ex(kSystemProcessorPerformanceInformation, &group, sizeof group, buf.data(), size, &ret);
        }
        if (st < 0 && g == 0) // older systems: the plain call covers the current group
            st = NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)kSystemProcessorPerformanceInformation,
                                          buf.data(), size, &ret);
        if (st < 0) continue;
        size_t n = ret / sizeof(CpuPerfInfo);
        for (size_t i = 0; i < n; i++) {
            CpuTimes t;
            t.idle = (uint64_t)buf[i].IdleTime.QuadPart;
            // KernelTime already includes idle time.
            t.total = (uint64_t)buf[i].KernelTime.QuadPart + (uint64_t)buf[i].UserTime.QuadPart;
            s.cores.push_back(t);
            s.cpu_total.idle += t.idle;
            s.cpu_total.total += t.total;
        }
    }
}

void sample_mem(Snapshot& s) {
    PERFORMANCE_INFORMATION pi {};
    pi.cb = sizeof pi;
    if (!GetPerformanceInfo(&pi, sizeof pi)) return;
    uint64_t page = pi.PageSize;
    s.mem_total = pi.PhysicalTotal * page;
    s.mem_used = (pi.PhysicalTotal - pi.PhysicalAvailable) * page;
    s.mem_cached = pi.SystemCache * page;
    s.swap_label = "Cmt"; // Windows reports commit charge rather than swap usage
    s.swap_total = pi.CommitLimit * page;
    s.swap_used = pi.CommitTotal * page;
}

void sample_disks(Snapshot& s) {
    wchar_t drives[512];
    DWORD len = GetLogicalDriveStringsW(511, drives);
    if (len == 0 || len > 511) return;
    for (const wchar_t* d = drives; *d; d += wcslen(d) + 1) {
        UINT type = GetDriveTypeW(d);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE && type != DRIVE_RAMDISK) continue;
        ULARGE_INTEGER avail, total, free;
        if (!GetDiskFreeSpaceExW(d, &avail, &total, &free) || total.QuadPart == 0) continue;
        DiskInfo di;
        di.mount = utf8(d);
        if (!di.mount.empty() && di.mount.back() == '\\') di.mount.pop_back();
        wchar_t fs[64] = L"";
        GetVolumeInformationW(d, nullptr, 0, nullptr, nullptr, nullptr, fs, 64);
        di.fs = utf8(fs);
        di.total = total.QuadPart;
        di.free = free.QuadPart;
        s.disks.push_back(std::move(di));
    }
}

std::string trim(std::string s) {
    size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(' ') - a + 1);
}

// Physical drives are opened with no access rights, which needs no administrator privileges
// and is enough for the property and performance queries used here.
void open_drives() {
    for (int i = 0; i < 32; i++) {
        wchar_t path[40];
        swprintf(path, 40, L"\\\\.\\PhysicalDrive%d", i);
        HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        Drive d;
        d.handle = h;
        d.name = "Disk" + std::to_string(i);
        STORAGE_PROPERTY_QUERY q {};
        q.PropertyId = StorageDeviceProperty;
        q.QueryType = PropertyStandardQuery;
        BYTE buf[1024] = {};
        DWORD got = 0;
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, buf, sizeof buf - 1, &got, nullptr) &&
            got >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
            auto* desc = (STORAGE_DEVICE_DESCRIPTOR*)buf;
            auto text = [&](DWORD off) { return off && off < got ? trim((const char*)buf + off) : std::string(); };
            std::string vendor = text(desc->VendorIdOffset), product = text(desc->ProductIdOffset);
            d.model = vendor.empty() ? product : vendor + " " + product;
        }
        g_drives.push_back(std::move(d));
    }
}

void sample_drives(Snapshot& s) {
    for (const Drive& d : g_drives) {
        PhysDisk pd;
        pd.name = d.name;
        pd.model = d.model;
        DWORD got = 0;

        STORAGE_PROPERTY_QUERY q {};
        q.PropertyId = (STORAGE_PROPERTY_ID)kStorageDeviceTemperatureProperty;
        q.QueryType = PropertyStandardQuery;
        TemperatureDescriptor t {};
        if (DeviceIoControl(d.handle, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, &t, sizeof t, &got, nullptr) &&
            got >= offsetof(TemperatureDescriptor, Info) + sizeof(TemperatureInfo) && t.InfoCount > 0 &&
            t.Info[0].Temperature > 0 && t.Info[0].Temperature < 150)
            pd.temp = t.Info[0].Temperature;

        DISK_PERFORMANCE perf {};
        if (DeviceIoControl(d.handle, IOCTL_DISK_PERFORMANCE, nullptr, 0, &perf, sizeof perf, &got, nullptr)) {
            pd.read = (uint64_t)perf.BytesRead.QuadPart;
            pd.write = (uint64_t)perf.BytesWritten.QuadPart;
        }
        s.phys.push_back(std::move(pd));
    }
}

void load_sysinfo() {
    const wchar_t* key = L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
    wchar_t name[256] = L"";
    DWORD size = sizeof name - sizeof(wchar_t);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, name, &size) == ERROR_SUCCESS)
        g_info.cpu_name = trim(utf8(name));
    DWORD mhz = 0;
    size = sizeof mhz;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"~MHz", RRF_RT_REG_DWORD, nullptr, &mhz, &size) == ERROR_SUCCESS)
        g_info.cpu_base_mhz = mhz;

    // Raw SMBIOS table: an 8-byte header followed by the structure table.
    const DWORD rsmb = 0x52534D42; // 'RSMB'
    UINT n = GetSystemFirmwareTable(rsmb, 0, nullptr, 0);
    if (n > 8) {
        std::vector<BYTE> buf(n);
        if (GetSystemFirmwareTable(rsmb, 0, buf.data(), n) == n)
            parse_smbios_memory(buf.data() + 8, n - 8, g_info.modules);
    }
}

void sample_net(Snapshot& s) {
    MIB_IF_TABLE2* table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || !table) return;
    for (ULONG i = 0; i < table->NumEntries; i++) {
        const MIB_IF_ROW2& r = table->Table[i];
        if (r.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if (r.OperStatus != IfOperStatusUp) continue;
        if (r.InterfaceAndOperStatusFlags.FilterInterface) continue; // filter drivers duplicate the adapter
        if (r.InOctets == 0 && r.OutOctets == 0) continue;
        NetInfo ni;
        ni.name = utf8(r.Alias);
        ni.rx = r.InOctets;
        ni.tx = r.OutOctets;
        s.nets.push_back(std::move(ni));
    }
    FreeMibTable(table);
}

void sample_procs(Snapshot& s) {
    static std::vector<BYTE> buf(1 << 20);
    ULONG ret = 0;
    NTSTATUS st;
    for (int tries = 0; tries < 8; tries++) {
        st = NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)kSystemProcessInformation, buf.data(),
                                      (ULONG)buf.size(), &ret);
        if (st != kStatusInfoLengthMismatch) break;
        buf.resize(std::max<size_t>(buf.size() * 2, (size_t)ret + 65536));
    }
    if (st < 0) return;
    const BYTE* p = buf.data();
    for (;;) {
        const ProcessInfo* pi = (const ProcessInfo*)p;
        uint32_t pid = (uint32_t)(ULONG_PTR)pi->UniqueProcessId;
        if (pid != 0) { // pid 0 is the idle pseudo-process
            ProcInfo info;
            info.pid = pid;
            if (pi->ImageName.Buffer && pi->ImageName.Length > 0)
                info.name = utf8(pi->ImageName.Buffer, pi->ImageName.Length / (int)sizeof(wchar_t));
            else
                info.name = pid == 4 ? "System" : "?";
            info.cpu_time = (uint64_t)pi->KernelTime.QuadPart + (uint64_t)pi->UserTime.QuadPart;
            info.mem = pi->WorkingSetPrivateSize.QuadPart > 0 ? (uint64_t)pi->WorkingSetPrivateSize.QuadPart
                                                              : (uint64_t)pi->WorkingSetSize;
            info.threads = pi->NumberOfThreads;
            s.procs.push_back(std::move(info));
        }
        if (pi->NextEntryOffset == 0) break;
        p += pi->NextEntryOffset;
    }
}

template <class F>
void pdh_each(PDH_HCOUNTER counter, F fn) {
    if (!counter) return;
    DWORD size = 0, count = 0;
    DWORD fmt = PDH_FMT_DOUBLE | PDH_FMT_NOCAP100;
    if (PdhGetFormattedCounterArrayW(counter, fmt, &size, &count, nullptr) != (PDH_STATUS)PDH_MORE_DATA) return;
    std::vector<BYTE> buf(size);
    auto* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
    if (PdhGetFormattedCounterArrayW(counter, fmt, &size, &count, items) != ERROR_SUCCESS) return;
    for (DWORD i = 0; i < count; i++) {
        DWORD cs = items[i].FmtValue.CStatus;
        if (cs == PDH_CSTATUS_VALID_DATA || cs == PDH_CSTATUS_NEW_DATA) fn(items[i].szName, items[i].FmtValue.doubleValue);
    }
}

// Instance names look like "pid_1234_luid_0x00000000_0x0000D3A5_phys_0_eng_0_engtype_3D".
bool parse_pid(const wchar_t* name, uint32_t& pid) {
    const wchar_t* p = wcsstr(name, L"pid_");
    if (!p) return false;
    pid = (uint32_t)wcstoul(p + 4, nullptr, 10);
    return true;
}

uint64_t parse_luid(const wchar_t* name) {
    const wchar_t* p = wcsstr(name, L"luid_");
    if (!p) return 0;
    wchar_t* end = nullptr;
    uint64_t hi = wcstoul(p + 5, &end, 16);
    uint64_t lo = end && *end == L'_' ? wcstoul(end + 1, nullptr, 16) : 0;
    return hi << 32 | lo;
}

// Hardware display adapters, used to show GPUs that NVML does not cover (integrated, AMD, Intel).
void load_adapters() {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)) || !factory) return;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; SUCCEEDED(factory->EnumAdapters1(i, &adapter)) && adapter; i++) {
        DXGI_ADAPTER_DESC1 d;
        if (SUCCEEDED(adapter->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            Adapter a;
            a.luid = (uint64_t)(uint32_t)d.AdapterLuid.HighPart << 32 | d.AdapterLuid.LowPart;
            a.name = utf8(d.Description);
            a.vram = d.DedicatedVideoMemory;
            a.nvidia = d.VendorId == 0x10DE;
            g_adapters.push_back(std::move(a));
        }
        adapter->Release();
        adapter = nullptr;
    }
    factory->Release();
}

// "VideoDecode" -> "Decode", "Compute_1" -> "Compute", ...
std::string engine_label(std::wstring eng) {
    size_t us = eng.find_last_of(L'_');
    if (us != std::wstring::npos && us + 1 < eng.size() && iswdigit(eng[us + 1])) eng.erase(us);
    static const struct { const wchar_t* from; const char* to; } names[] = {
        { L"3D", "3D" }, { L"Compute", "Compute" }, { L"Copy", "Copy" }, { L"VideoDecode", "Decode" },
        { L"VideoEncode", "Encode" }, { L"VideoProcessing", "VideoProc" }, { L"JPEG_Decode", "JPEG" },
    };
    for (auto& n : names)
        if (_wcsicmp(eng.c_str(), n.from) == 0) return n.to;
    return utf8(eng.c_str());
}

// Which GPU APIs a process has loaded. Returns GpuUse flags; 0 when nothing matched or the
// process cannot be read (elevated and protected processes, for example).
uint8_t scan_modules(uint32_t pid) {
    static const wchar_t* const compute[] = { L"nvcuda.dll", L"opencl.dll", L"amdhip64", L"ze_loader.dll", L"directml.dll" };
    static const wchar_t* const graphics[] = { L"d3d9.dll", L"d3d10.dll", L"d3d10_1.dll", L"d3d11.dll",
                                               L"d3d12.dll", L"opengl32.dll", L"vulkan-1.dll" };
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) return 0;
    std::vector<HMODULE> mods(1024);
    DWORD bytes = 0;
    uint8_t use = 0;
    if (EnumProcessModulesEx(h, mods.data(), (DWORD)(mods.size() * sizeof(HMODULE)), &bytes, LIST_MODULES_ALL)) {
        size_t n = std::min(mods.size(), (size_t)bytes / sizeof(HMODULE));
        for (size_t i = 0; i < n && use != (GPU_COMPUTE | GPU_GRAPHICS); i++) {
            wchar_t name[MAX_PATH];
            if (!GetModuleBaseNameW(h, mods[i], name, MAX_PATH)) continue;
            for (const wchar_t* m : compute)
                if (_wcsnicmp(name, m, wcslen(m)) == 0) use |= GPU_COMPUTE;
            for (const wchar_t* m : graphics)
                if (_wcsicmp(name, m) == 0) use |= GPU_GRAPHICS;
        }
    }
    CloseHandle(h);
    return use;
}

// Reading another process's module list is slow, so each result is kept for 10 seconds
// (programs such as PyTorch load CUDA some time after they start).
struct ModuleInfo {
    uint8_t use;
    ULONGLONG checked;
};
std::unordered_map<uint32_t, ModuleInfo> g_modules;

uint8_t module_use(uint32_t pid, ULONGLONG now) {
    auto it = g_modules.find(pid);
    if (it != g_modules.end() && now - it->second.checked < 10000) return it->second.use;
    uint8_t use = scan_modules(pid);
    g_modules[pid] = { use, now };
    return use;
}

// CPU clock and thermal zone, then per-process GPU usage from the WDDM performance counters
// (works for any GPU vendor).
void sample_counters(Snapshot& s) {
    s.cpu_mhz = g_info.cpu_base_mhz;
    if (!g_query || PdhCollectQueryData(g_query) != ERROR_SUCCESS) return;

    // "% Processor Performance" is the current clock relative to the nominal one (Task Manager's speed).
    PDH_FMT_COUNTERVALUE val;
    if (g_cpu_perf && PdhGetFormattedCounterValue(g_cpu_perf, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &val) == ERROR_SUCCESS &&
        val.doubleValue > 0)
        s.cpu_mhz = g_info.cpu_base_mhz * val.doubleValue / 100.0;

    // ACPI thermal zones report Kelvin. This is a board sensor, not the CPU core sensor.
    double kelvin = 0;
    pdh_each(g_thermal, [&](const wchar_t*, double v) { kelvin = std::max(kelvin, v); });
    if (kelvin > 273) {
        s.cpu_temp = (int)(kelvin - 273.15 + 0.5);
        s.cpu_temp_label = "ACPI";
    }

    // Only hardware display adapters count; the software renderer and NPUs also show up here.
    auto hardware = [](uint64_t luid) {
        if (g_adapters.empty()) return true; // adapter list unavailable: keep everything
        for (const Adapter& a : g_adapters)
            if (a.luid == luid) return true;
        return false;
    };

    std::map<std::wstring, double> engine_util; // engine (without pid) -> summed utilization
    std::unordered_map<uint32_t, bool> has_3d;  // pid -> has a context on a 3D engine
    pdh_each(g_gpu_engine, [&](const wchar_t* name, double v) {
        uint32_t pid;
        if (!parse_pid(name, pid) || !hardware(parse_luid(name))) return;
        const wchar_t* et = wcsstr(name, L"engtype_");
        std::wstring eng = et ? et + 8 : L"";
        ProcGpu& pg = s.proc_gpu[pid];
        if (_wcsnicmp(eng.c_str(), L"3D", 2) == 0) has_3d[pid] = true;
        if (_wcsnicmp(eng.c_str(), L"Compute", 7) == 0) pg.use |= GPU_COMPUTE;
        if ((float)v > pg.util) {
            pg.util = (float)v;
            if (v > 0) pg.engine = engine_label(eng);
        }
        if (const wchar_t* luid = wcsstr(name, L"luid_")) engine_util[luid] += v;
    });
    pdh_each(g_gpu_proc_mem, [&](const wchar_t* name, double v) {
        uint32_t pid;
        if (!parse_pid(name, pid) || !hardware(parse_luid(name))) return;
        ProcGpu& pg = s.proc_gpu[pid];
        pg.mem = std::max(pg.mem, (uint64_t)v);
    });

    // Compute or graphics. WDDM gives every GPU process a 3D context (CUDA work also runs there),
    // so the API libraries a process has loaded decide; the 3D context alone counts as graphics
    // unless the process is known to do compute.
    ULONGLONG now = GetTickCount64();
    for (auto& e : s.proc_gpu) {
        uint8_t mods = module_use(e.first, now);
        bool compute = (e.second.use & GPU_COMPUTE) || (mods & GPU_COMPUTE);
        bool graphics = (mods & GPU_GRAPHICS) || (has_3d.count(e.first) && !compute);
        e.second.use = (uint8_t)((compute ? GPU_COMPUTE : 0) | (graphics ? GPU_GRAPHICS : 0));
    }
    for (auto it = g_modules.begin(); it != g_modules.end();) // forget processes that left the GPU
        it = s.proc_gpu.count(it->first) ? std::next(it) : g_modules.erase(it);

    // An adapter's load is that of its busiest engine.
    std::map<uint64_t, double> luid_util, luid_mem;
    for (auto& e : engine_util) {
        double& u = luid_util[parse_luid(e.first.c_str())];
        u = std::max(u, e.second);
    }
    pdh_each(g_gpu_adapter_mem, [&](const wchar_t* name, double v) { luid_mem[parse_luid(name)] += v; });

    bool have_nvml = !s.gpus.empty();
    for (const Adapter& a : g_adapters) {
        if (a.nvidia && have_nvml) continue; // NVML already gave fuller data for these
        GpuInfo g;
        g.name = a.name;
        g.util = (int)std::min(100.0, luid_util[a.luid] + 0.5);
        g.mem_used = (uint64_t)luid_mem[a.luid];
        g.mem_total = a.vram;
        s.gpus.push_back(std::move(g));
    }
}

} // namespace

void sys_init() {
    SetErrorMode(SEM_FAILCRITICALERRORS);
    load_adapters();
    load_sysinfo();
    open_drives();
    if (PdhOpenQueryW(nullptr, 0, &g_query) != ERROR_SUCCESS) {
        g_query = nullptr;
        return;
    }
    if (PdhAddEnglishCounterW(g_query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &g_gpu_engine) != ERROR_SUCCESS)
        g_gpu_engine = nullptr;
    if (PdhAddEnglishCounterW(g_query, L"\\GPU Process Memory(*)\\Dedicated Usage", 0, &g_gpu_proc_mem) != ERROR_SUCCESS)
        g_gpu_proc_mem = nullptr;
    if (PdhAddEnglishCounterW(g_query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &g_gpu_adapter_mem) != ERROR_SUCCESS)
        g_gpu_adapter_mem = nullptr;
    if (PdhAddEnglishCounterW(g_query, L"\\Processor Information(_Total)\\% Processor Performance", 0, &g_cpu_perf) != ERROR_SUCCESS)
        g_cpu_perf = nullptr;
    if (PdhAddEnglishCounterW(g_query, L"\\Thermal Zone Information(*)\\Temperature", 0, &g_thermal) != ERROR_SUCCESS)
        g_thermal = nullptr;
    PdhCollectQueryData(g_query); // rate counters need a baseline sample
}

void sys_sample(Snapshot& s) {
    s.uptime_s = GetTickCount64() / 1000;
    sample_cpu(s);
    sample_mem(s);
    sample_disks(s);
    sample_net(s);
    sample_procs(s);
    nvml_sample(s.gpus, s.proc_gpu);
    sample_counters(s);
    sample_drives(s);
}

const SysInfo& sys_info() { return g_info; }

std::string sys_hostname() {
    wchar_t name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(name, &n)) return "localhost";
    return utf8(name, (int)n);
}

#endif // _WIN32
