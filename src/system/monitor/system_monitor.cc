// Copyright 2026 Loong AI NVR Project

#include "system/monitor/system_monitor.h"

#include "core/event_bus/event_bus.h"
#include "spdlog/spdlog.h"

#include <sys/statvfs.h>
#include <unistd.h>

#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>

namespace loong::system {

SystemMonitor::SystemMonitor() { disk_paths_.emplace_back("/"); }

SystemMonitor::~SystemMonitor() { Stop(); }

void SystemMonitor::Start(int interval_seconds) {
  if (running_) return;
  interval_seconds_ = interval_seconds;
  running_ = true;
  worker_ = std::thread(&SystemMonitor::CollectLoop, this);
  spdlog::info("SystemMonitor: started (interval={}s)", interval_seconds);
}

void SystemMonitor::Stop() {
  if (!running_) return;
  running_ = false;
  if (worker_.joinable()) {
    worker_.join();
  }
  spdlog::info("SystemMonitor: stopped");
}

SystemMetrics SystemMonitor::GetLatest() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_;
}

void SystemMonitor::AddDiskPath(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& p : disk_paths_) {
    if (p == path) return;
  }
  disk_paths_.push_back(path);
}

void SystemMonitor::CollectLoop() {
  // Take an initial CPU sample so the first delta is meaningful.
  {
    SystemMetrics warmup;
    CollectCpu(warmup);
  }

  while (running_) {
    SystemMetrics m;
    CollectCpu(m);
    CollectMemory(m);
    CollectDisk(m);
    CollectGpu(m);
    CollectUptime(m);
    CollectThreads(m);

    {
      std::lock_guard<std::mutex> lock(mutex_);
      latest_ = m;
    }

    // Publish via EventBus for WebSocket consumers.
    core::EventBus::Instance().Publish("system.metrics");

    for (int i = 0; i < interval_seconds_ * 10 && running_; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}

// ========== CPU ==========

void SystemMonitor::CollectCpu(SystemMetrics& m) {
  std::ifstream f("/proc/stat");
  if (!f.is_open()) return;

  std::string line;
  std::getline(f, line);

  // Format: cpu  user nice system idle iowait irq softirq steal
  std::istringstream iss(line);
  std::string label;
  iss >> label;  // "cpu"

  int64_t user = 0;
  int64_t nice = 0;
  int64_t sys = 0;
  int64_t idle = 0;
  int64_t iowait = 0;
  int64_t irq = 0;
  int64_t softirq = 0;
  int64_t steal = 0;
  iss >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal;

  int64_t total = user + nice + sys + idle + iowait + irq + softirq + steal;
  int64_t idle_all = idle + iowait;

  int64_t d_total = total - prev_cpu_total_;
  int64_t d_idle = idle_all - prev_cpu_idle_;
  prev_cpu_total_ = total;
  prev_cpu_idle_ = idle_all;

  if (d_total > 0) {
    m.cpu_usage_percent = 100.0 * static_cast<double>(d_total - d_idle) /
                          static_cast<double>(d_total);
  }
}

// ========== Memory ==========

void SystemMonitor::CollectMemory(SystemMetrics& m) {
  std::ifstream f("/proc/meminfo");
  if (!f.is_open()) return;

  int64_t mem_total_kb = 0;
  int64_t mem_available_kb = 0;
  bool got_total = false;
  bool got_avail = false;

  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind("MemTotal:", 0) == 0) {
      std::istringstream iss(line);
      std::string key;
      iss >> key >> mem_total_kb;
      got_total = true;
    } else if (line.rfind("MemAvailable:", 0) == 0) {
      std::istringstream iss(line);
      std::string key;
      iss >> key >> mem_available_kb;
      got_avail = true;
    }
    if (got_total && got_avail) break;
  }

  m.memory_total_mb = mem_total_kb / 1024;
  m.memory_used_mb = (mem_total_kb - mem_available_kb) / 1024;
  if (mem_total_kb > 0) {
    m.memory_usage_percent =
        100.0 * static_cast<double>(mem_total_kb - mem_available_kb) /
        static_cast<double>(mem_total_kb);
  }
}

// ========== Disk ==========

void SystemMonitor::CollectDisk(SystemMetrics& m) {
  std::vector<std::string> paths;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    paths = disk_paths_;
  }

  for (const auto& path : paths) {
    struct statvfs stat {};
    if (statvfs(path.c_str(), &stat) != 0) continue;

    DiskInfo d;
    d.path = path;
    auto block_size = static_cast<int64_t>(stat.f_frsize);
    d.total_gb = (block_size * static_cast<int64_t>(stat.f_blocks)) /
                 (1024LL * 1024LL * 1024LL);
    d.free_gb = (block_size * static_cast<int64_t>(stat.f_bavail)) /
                (1024LL * 1024LL * 1024LL);
    if (d.total_gb > 0) {
      d.usage_percent = 100.0 * static_cast<double>(d.total_gb - d.free_gb) /
                        static_cast<double>(d.total_gb);
    }
    m.disks.push_back(d);
  }
}

// ========== GPU (stub — requires NVML) ==========

void SystemMonitor::CollectGpu(SystemMetrics& m) {
#ifdef LOONG_HAS_NVML
  // NVML-based GPU monitoring would go here:
  //   nvmlInit();
  //   nvmlDeviceGetUtilizationRates(device, &util);
  //   nvmlDeviceGetMemoryInfo(device, &mem);
  //   nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temp);
  //   nvmlShutdown();
  m.gpu_available = true;
#else
  m.gpu_available = false;
  m.gpu_usage_percent = 0.0;
  m.gpu_memory_used_mb = 0;
  m.gpu_memory_total_mb = 0;
  m.gpu_temp_celsius = 0.0;
#endif
}

// ========== Uptime ==========

void SystemMonitor::CollectUptime(SystemMetrics& m) {
  std::ifstream f("/proc/uptime");
  if (!f.is_open()) return;

  double up = 0.0;
  f >> up;
  m.uptime_seconds = static_cast<int64_t>(up);
}

// ========== Thread count ==========

void SystemMonitor::CollectThreads(SystemMetrics& m) {
  // Count threads of the current process via /proc/self/task
  int count = 0;
  DIR* dir = opendir("/proc/self/task");
  if (dir) {
    while (readdir(dir) != nullptr) {
      ++count;
    }
    closedir(dir);
    count -= 2;  // Subtract "." and ".."
    if (count < 0) count = 0;
  }
  m.process_threads = count;
}

}  // namespace loong::system
