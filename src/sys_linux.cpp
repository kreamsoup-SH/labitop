// labitop: Linux backend. Reads /proc, /sys and statvfs only.
#ifdef __linux__
#include "sys.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <sys/statvfs.h>
#include <unistd.h>

namespace {

long g_hz = 100, g_page = 4096;
SysInfo g_info;

std::string read_line(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    std::getline(f, line);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\n' || line.back() == '\r')) line.pop_back();
    return line;
}

// Names of the entries in `dir` that start with `prefix`.
std::vector<std::string> list_dir(const std::string& dir, const char* prefix = "") {
    std::vector<std::string> out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    size_t n = strlen(prefix);
    while (dirent* e = readdir(d))
        if (e->d_name[0] != '.' && strncmp(e->d_name, prefix, n) == 0) out.push_back(e->d_name);
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

// Highest temp*_input (millidegrees) in a hwmon directory, in Celsius; -1 if none.
int hwmon_max_temp(const std::string& dir) {
    int best = -1;
    for (auto& f : list_dir(dir, "temp")) {
        if (f.size() < 7 || f.compare(f.size() - 6, 6, "_input") != 0) continue;
        std::string v = read_line(dir + "/" + f);
        if (!v.empty()) best = std::max(best, atoi(v.c_str()) / 1000);
    }
    return best;
}

void sample_cpu(Snapshot& s) {
    std::ifstream f("/proc/stat");
    std::string line;
    while (std::getline(f, line)) {
        if (line.compare(0, 3, "cpu") != 0) break;
        std::istringstream is(line);
        std::string tag;
        uint64_t v[8] = {};
        is >> tag;
        for (auto& x : v) is >> x;
        CpuTimes t;
        t.idle = v[3] + v[4]; // idle + iowait
        for (auto x : v) t.total += x;
        if (tag == "cpu")
            s.cpu_total = t;
        else
            s.cores.push_back(t);
    }

    // Current clock: average of the per-core "cpu MHz" lines.
    std::ifstream info("/proc/cpuinfo");
    double sum = 0;
    int count = 0;
    while (std::getline(info, line)) {
        if (line.compare(0, 7, "cpu MHz") != 0) continue;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        sum += atof(line.c_str() + colon + 1);
        count++;
    }
    if (count) s.cpu_mhz = sum / count;

    // CPU temperature from the kernel's hardware monitoring drivers.
    for (auto& h : list_dir("/sys/class/hwmon", "hwmon")) {
        std::string dir = "/sys/class/hwmon/" + h;
        std::string name = read_line(dir + "/name");
        if (name == "coretemp" || name == "k10temp" || name == "zenpower" || name == "cpu_thermal") {
            s.cpu_temp = hwmon_max_temp(dir);
            if (s.cpu_temp >= 0) break;
        }
    }
}

void sample_mem(Snapshot& s) {
    std::ifstream f("/proc/meminfo");
    std::string key;
    uint64_t val;
    std::string rest;
    uint64_t total = 0, avail = 0, cached = 0, stotal = 0, sfree = 0;
    while (f >> key >> val) {
        std::getline(f, rest);
        if (key == "MemTotal:") total = val;
        else if (key == "MemAvailable:") avail = val;
        else if (key == "Cached:") cached = val;
        else if (key == "SwapTotal:") stotal = val;
        else if (key == "SwapFree:") sfree = val;
    }
    s.mem_total = total * 1024;
    s.mem_used = (total > avail ? total - avail : 0) * 1024;
    s.mem_cached = cached * 1024;
    s.swap_total = stotal * 1024;
    s.swap_used = (stotal > sfree ? stotal - sfree : 0) * 1024;
}

void sample_disks(Snapshot& s) {
    std::ifstream f("/proc/mounts");
    std::string dev, mount, fs, rest;
    std::set<std::string> seen;
    while (f >> dev >> mount >> fs) {
        std::getline(f, rest);
        if (dev.compare(0, 5, "/dev/") != 0) continue;
        if (dev.compare(0, 9, "/dev/loop") == 0) continue; // snap images etc.
        if (!seen.insert(dev).second) continue;
        mount = unescape_mount(mount);
        struct statvfs st;
        if (statvfs(mount.c_str(), &st) != 0 || st.f_blocks == 0) continue;
        DiskInfo di;
        di.mount = mount;
        di.fs = fs;
        di.total = (uint64_t)st.f_blocks * st.f_frsize;
        di.free = (uint64_t)st.f_bfree * st.f_frsize;
        s.disks.push_back(std::move(di));
    }

    // Physical drives: model, I/O counters and (when a hwmon driver exposes it) temperature.
    for (auto& name : list_dir("/sys/block")) {
        if (name.compare(0, 4, "loop") == 0 || name.compare(0, 3, "ram") == 0 || name.compare(0, 4, "zram") == 0 ||
            name.compare(0, 3, "dm-") == 0 || name.compare(0, 2, "md") == 0 || name.compare(0, 2, "sr") == 0)
            continue;
        std::string base = "/sys/block/" + name;
        PhysDisk pd;
        pd.name = name;
        pd.model = read_line(base + "/device/model");
        // stat fields: reads, merged, sectors read, ms, writes, merged, sectors written, ...
        std::istringstream is(read_line(base + "/stat"));
        uint64_t v[7] = {};
        for (auto& x : v) is >> x;
        pd.read = v[2] * 512;
        pd.write = v[6] * 512;
        // NVMe: device/hwmonN ; SATA with the drivetemp module: device/hwmon/hwmonN
        for (auto& h : list_dir(base + "/device", "hwmon")) {
            std::string dir = base + "/device/" + h;
            pd.temp = std::max(pd.temp, hwmon_max_temp(dir));
            for (auto& sub : list_dir(dir, "hwmon")) pd.temp = std::max(pd.temp, hwmon_max_temp(dir + "/" + sub));
        }
        s.phys.push_back(std::move(pd));
    }
}

void sample_net(Snapshot& s) {
    std::ifstream f("/proc/net/dev");
    std::string line;
    while (std::getline(f, line)) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        name.erase(0, name.find_first_not_of(' '));
        if (name == "lo") continue;
        std::istringstream is(line.substr(colon + 1));
        uint64_t v[16] = {};
        for (auto& x : v) is >> x;
        if (v[0] == 0 && v[8] == 0) continue;
        NetInfo ni;
        ni.name = name;
        ni.rx = v[0];
        ni.tx = v[8];
        s.nets.push_back(std::move(ni));
    }
}

void sample_procs(Snapshot& s) {
    DIR* d = opendir("/proc");
    if (!d) return;
    char path[300], buf[2048];
    while (dirent* e = readdir(d)) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        snprintf(path, sizeof path, "/proc/%s/stat", e->d_name);
        FILE* f = fopen(path, "r");
        if (!f) continue;
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[n] = '\0';
        ProcInfo info;
        if (parse_proc_stat(buf, info, g_hz, g_page)) s.procs.push_back(std::move(info));
    }
    closedir(d);
}

void load_sysinfo() {
    std::ifstream info("/proc/cpuinfo");
    std::string line;
    while (std::getline(info, line)) {
        if (line.compare(0, 10, "model name") != 0) continue;
        size_t colon = line.find(':');
        if (colon != std::string::npos) g_info.cpu_name = line.substr(line.find_first_not_of(' ', colon + 1));
        break;
    }
    std::string max_khz = read_line("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
    if (!max_khz.empty()) g_info.cpu_base_mhz = atof(max_khz.c_str()) / 1000.0;

    // Memory module details live in the firmware table, which is normally readable by root only.
    std::ifstream dmi("/sys/firmware/dmi/tables/DMI", std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(dmi)), std::istreambuf_iterator<char>());
    if (!raw.empty()) parse_smbios_memory((const uint8_t*)raw.data(), raw.size(), g_info.modules);
}

} // namespace

void sys_init() {
    long hz = sysconf(_SC_CLK_TCK);
    if (hz > 0) g_hz = hz;
    long page = sysconf(_SC_PAGESIZE);
    if (page > 0) g_page = page;
    load_sysinfo();
}

void sys_sample(Snapshot& s) {
    std::ifstream up("/proc/uptime");
    double secs = 0;
    up >> secs;
    s.uptime_s = (uint64_t)secs;
    sample_cpu(s);
    sample_mem(s);
    sample_disks(s);
    sample_net(s);
    sample_procs(s);
    nvml_sample(s.gpus, s.proc_gpu);
}

std::string sys_hostname() {
    char name[256] = "";
    if (gethostname(name, sizeof name - 1) != 0 || !name[0]) return "localhost";
    return name;
}

const SysInfo& sys_info() { return g_info; }

#endif // __linux__
