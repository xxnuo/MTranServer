#include "pcore_pool.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#elif defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#include <sys/sysctl.h>
#endif

namespace mtran::ssru {
namespace {

inline void cpuRelax() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
  _mm_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
#if defined(_MSC_VER) && !defined(__clang__)
  __yield();
#else
  asm volatile("yield" ::: "memory");
#endif
#else
  std::this_thread::yield();
#endif
}

unsigned long long parsePositiveUll(const char* text, const char** next_out) noexcept {
  if (!text) return 0;
  const char* p = text;
  while (*p == ' ' || *p == '\t') ++p;
  if (*p < '0' || *p > '9') return 0;
  errno = 0;
  char* end = nullptr;
  const unsigned long long val = std::strtoull(p, &end, 10);
  if (end == p || errno == ERANGE || val == 0) return 0;
  if (next_out) *next_out = end;
  return val;
}

std::size_t ceilDivQuota(unsigned long long quota, unsigned long long period) noexcept {
  if (quota == 0 || period == 0) return 0;
  const unsigned long long q = quota / period + (quota % period != 0 ? 1ULL : 0ULL);
  const unsigned long long max_sz = static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max());
  return static_cast<std::size_t>(std::min(q, max_sz));
}

#if defined(__linux__)

bool readFileLine(const char* path, char* buf, std::size_t buf_size) noexcept {
  std::FILE* f = std::fopen(path, "r");
  if (!f) return false;
  const bool ok = std::fgets(buf, static_cast<int>(buf_size), f) != nullptr;
  std::fclose(f);
  return ok;
}

std::vector<int> parseCpuList(const char* text) {
  std::vector<int> cpus;
  const char* p = text;
  while (*p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',') ++p;
    if (!*p) break;
    char* end = nullptr;
    long a = std::strtol(p, &end, 10);
    if (end == p) break;
    p = end;
    long b = a;
    if (*p == '-') {
      ++p;
      b = std::strtol(p, &end, 10);
      if (end == p) break;
      p = end;
    }
    if (a >= 0 && b >= a && b < CPU_SETSIZE) {
      for (long c = a; c <= b; ++c) cpus.push_back(static_cast<int>(c));
    }
  }
  return cpus;
}

struct LinuxTopology {
  std::vector<int> all_pcore_cpus;
  std::vector<std::vector<int>> physical_pcore_groups;
  std::vector<int> pcore_ht_siblings;
  std::vector<int> ecore_cpus;
  std::vector<int> worker_cpus;
  std::size_t physical_core_count = 0;
  std::size_t allowed_cpu_count = 0;
};

LinuxTopology detectLinuxTopology() {
  LinuxTopology topo;
  cpu_set_t allowed;
  CPU_ZERO(&allowed);
  const bool have_affinity = (sched_getaffinity(0, sizeof(allowed), &allowed) == 0);

  auto is_allowed = [&](int cpu) {
    return cpu >= 0 && cpu < CPU_SETSIZE && (!have_affinity || CPU_ISSET(cpu, &allowed));
  };

  char buf[4096];
  std::vector<int> candidate_cpus;
  std::vector<int> all_allowed_cpus;
  const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
  const int scan_limit = std::min<int>(CPU_SETSIZE, std::max<int>(static_cast<int>(hw) * 2, 256));
  for (int c = 0; c < scan_limit; ++c) {
    if (is_allowed(c)) all_allowed_cpus.push_back(c);
  }
  const bool have_core_sysfs = readFileLine("/sys/devices/cpu_core/cpus", buf, sizeof(buf));
  if (have_core_sysfs) {
    for (int c : parseCpuList(buf)) {
      if (is_allowed(c)) candidate_cpus.push_back(c);
    }
  }
  std::vector<int> atom_cpus;
  const bool have_atom_sysfs = readFileLine("/sys/devices/cpu_atom/cpus", buf, sizeof(buf));
  if (have_atom_sysfs) {
    for (int c : parseCpuList(buf)) {
      if (is_allowed(c)) atom_cpus.push_back(c);
    }
    std::sort(atom_cpus.begin(), atom_cpus.end());
    atom_cpus.erase(std::unique(atom_cpus.begin(), atom_cpus.end()), atom_cpus.end());
  }
  if (candidate_cpus.empty()) {
    // Non-hybrid CPU (or container without cpu_core sysfs): use allowed CPUs,
    // preferring highest-capacity CPUs if ARM big.LITTLE exposes cpu_capacity.
    std::vector<std::pair<int, int>> cap_cpus;
    int max_cap = -1;
    for (int c : all_allowed_cpus) {
      char cap_path[128];
      std::snprintf(cap_path, sizeof(cap_path), "/sys/devices/system/cpu/cpu%d/cpu_capacity", c);
      int cap = 1024;
      if (readFileLine(cap_path, buf, sizeof(buf))) {
        cap = static_cast<int>(std::strtol(buf, nullptr, 10));
      }
      max_cap = std::max(max_cap, cap);
      cap_cpus.push_back({c, cap});
    }
    for (const auto& [c, cap] : cap_cpus) {
      if (cap >= max_cap) candidate_cpus.push_back(c);
    }
  }

  std::sort(candidate_cpus.begin(), candidate_cpus.end());
  candidate_cpus.erase(std::unique(candidate_cpus.begin(), candidate_cpus.end()), candidate_cpus.end());
  topo.all_pcore_cpus = candidate_cpus;

  // Group logical CPUs by physical core via thread_siblings_list so each
  // P-core receives at most one pool worker before SMT siblings are used.
  std::vector<bool> visited(CPU_SETSIZE, false);
  for (int c : candidate_cpus) {
    if (visited[c]) continue;
    char sib_path[128];
    std::snprintf(sib_path, sizeof(sib_path), "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", c);
    std::vector<int> group;
    if (readFileLine(sib_path, buf, sizeof(buf))) {
      for (int s : parseCpuList(buf)) {
        if (std::binary_search(candidate_cpus.begin(), candidate_cpus.end(), s)) {
          group.push_back(s);
          visited[s] = true;
        }
      }
    }
    if (group.empty()) {
      group.push_back(c);
      visited[c] = true;
    }
    topo.physical_pcore_groups.push_back(std::move(group));
  }

  // Group remaining non-P-core CPUs (verified via cpu_atom sysfs when present).
  std::vector<std::vector<int>> ecore_groups;
  for (int c : all_allowed_cpus) {
    if (std::binary_search(candidate_cpus.begin(), candidate_cpus.end(), c) || visited[c]) continue;
    if (have_core_sysfs && (!have_atom_sysfs || !std::binary_search(atom_cpus.begin(), atom_cpus.end(), c))) {
      continue;
    }
    char sib_path[128];
    std::snprintf(sib_path, sizeof(sib_path), "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", c);
    std::vector<int> group;
    if (readFileLine(sib_path, buf, sizeof(buf))) {
      for (int s : parseCpuList(buf)) {
        if (!std::binary_search(candidate_cpus.begin(), candidate_cpus.end(), s) && is_allowed(s)) {
          group.push_back(s);
          visited[s] = true;
        }
      }
    }
    if (group.empty()) {
      group.push_back(c);
      visited[c] = true;
    }
    ecore_groups.push_back(std::move(group));
  }

  // Ordered worker affinity: primary P-cores first, physical E-cores next,
  // and SMT siblings last so every distinct physical execution core stays busy first.
  for (const auto& g : topo.physical_pcore_groups) {
    if (!g.empty()) topo.worker_cpus.push_back(g[0]);
  }
  for (const auto& g : ecore_groups) {
    if (!g.empty()) {
      topo.ecore_cpus.push_back(g[0]);
      topo.worker_cpus.push_back(g[0]);
    }
  }
  topo.physical_core_count = topo.worker_cpus.size();
  topo.allowed_cpu_count = all_allowed_cpus.size();
  for (const auto& g : topo.physical_pcore_groups) {
    for (std::size_t i = 1; i < g.size(); ++i) {
      topo.pcore_ht_siblings.push_back(g[i]);
      topo.worker_cpus.push_back(g[i]);
    }
  }
  for (const auto& g : ecore_groups) {
    for (std::size_t i = 1; i < g.size(); ++i) topo.worker_cpus.push_back(g[i]);
  }
  return topo;
}

std::size_t parseCgroupV1Quota(const char* quota_text, const char* period_text) noexcept {
  const unsigned long long quota = parsePositiveUll(quota_text, nullptr);
  const unsigned long long period = parsePositiveUll(period_text, nullptr);
  return ceilDivQuota(quota, period);
}

bool hasCgroupController(const std::string& controllers, const char* target) noexcept {
  const std::size_t tlen = std::strlen(target);
  std::size_t start = 0;
  while (start <= controllers.size()) {
    const std::size_t comma = controllers.find(',', start);
    const std::size_t len = (comma == std::string::npos) ? (controllers.size() - start) : (comma - start);
    if (len == tlen && controllers.compare(start, len, target) == 0) return true;
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return false;
}

std::size_t detectLinuxCgroupCpuQuota() {
  std::size_t best = 0;
  auto update_min = [&](std::size_t q) noexcept {
    if (q > 0 && (best == 0 || q < best)) best = q;
  };

  char buf[256];
  auto check_v2_dir = [&](const std::string& dir) {
    const std::string path = dir + "/cpu.max";
    if (readFileLine(path.c_str(), buf, sizeof(buf))) {
      update_min(detail::parseCgroupCpuMax(buf));
    }
  };
  auto check_v1_dir = [&](const std::string& dir) {
    char qbuf[128], pbuf[128];
    const std::string qpath = dir + "/cpu.cfs_quota_us";
    const std::string ppath = dir + "/cpu.cfs_period_us";
    if (readFileLine(qpath.c_str(), qbuf, sizeof(qbuf)) &&
        readFileLine(ppath.c_str(), pbuf, sizeof(pbuf))) {
      update_min(parseCgroupV1Quota(qbuf, pbuf));
    }
  };
  auto walk_hierarchy = [&](const std::string& base, const std::string& rel, auto&& check_dir) {
    std::string cur = (rel == "/") ? std::string() : rel;
    while (!cur.empty() && cur.back() == '/') cur.pop_back();
    for (;;) {
      check_dir(base + cur);
      if (cur.empty()) break;
      const std::size_t slash = cur.rfind('/');
      if (slash == std::string::npos) {
        cur.clear();
      } else {
        cur.resize(slash);
      }
    }
  };

  if (std::FILE* f = std::fopen("/proc/self/cgroup", "r")) {
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
      std::size_t len = std::strlen(line);
      while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
      const std::string s(line, len);
      const std::size_t c1 = s.find(':');
      if (c1 == std::string::npos) continue;
      const std::size_t c2 = s.find(':', c1 + 1);
      if (c2 == std::string::npos) continue;
      const std::string controllers = s.substr(c1 + 1, c2 - c1 - 1);
      std::string rel = s.substr(c2 + 1);
      if (!rel.empty() && rel[0] != '/') rel.insert(rel.begin(), '/');
      if (controllers.empty()) {
        walk_hierarchy("/sys/fs/cgroup", rel, check_v2_dir);
      } else if (hasCgroupController(controllers, "cpu")) {
        walk_hierarchy("/sys/fs/cgroup/" + controllers, rel, check_v1_dir);
        if (controllers != "cpu") walk_hierarchy("/sys/fs/cgroup/cpu", rel, check_v1_dir);
        if (controllers != "cpu,cpuacct") walk_hierarchy("/sys/fs/cgroup/cpu,cpuacct", rel, check_v1_dir);
      }
    }
    std::fclose(f);
  }

  if (best == 0) {
    check_v2_dir("/sys/fs/cgroup");
    check_v1_dir("/sys/fs/cgroup/cpu");
    check_v1_dir("/sys/fs/cgroup/cpu,cpuacct");
  }
  return best;
}

void pinMask(const std::vector<int>& cpus) noexcept {
  if (cpus.empty()) return;
  cpu_set_t mask;
  CPU_ZERO(&mask);
  for (int c : cpus) {
    if (c >= 0 && c < CPU_SETSIZE) CPU_SET(c, &mask);
  }
  (void)sched_setaffinity(0, sizeof(mask), &mask);
}

#elif defined(__APPLE__)

std::size_t detectApplePcores() noexcept {
  int pcores = 0;
  std::size_t size = sizeof(pcores);
  if (sysctlbyname("hw.perflevel0.physicalcpu", &pcores, &size, nullptr, 0) == 0 && pcores > 0) {
    return static_cast<std::size_t>(pcores);
  }
  int physical = 0;
  size = sizeof(physical);
  if (sysctlbyname("hw.physicalcpu", &physical, &size, nullptr, 0) == 0 && physical > 0) {
    return static_cast<std::size_t>(physical);
  }
  return std::max<std::size_t>(1, std::thread::hardware_concurrency());
}

#endif

}  // namespace

namespace detail {

std::size_t parseCgroupCpuMax(const char* text) noexcept {
  if (!text) return 0;
  const char* p = text;
  while (*p == ' ' || *p == '\t') ++p;
  if (std::strncmp(p, "max", 3) == 0 &&
      (p[3] == '\0' || p[3] == ' ' || p[3] == '\t' || p[3] == '\n' || p[3] == '\r')) {
    return 0;
  }
  const char* next = nullptr;
  const unsigned long long quota = parsePositiveUll(p, &next);
  if (quota == 0 || !next) return 0;
  const unsigned long long period = parsePositiveUll(next, nullptr);
  return ceilDivQuota(quota, period);
}

}  // namespace detail

struct PcorePool::Impl {
  struct alignas(64) WorkerState {
    std::atomic<std::uint64_t> target_epoch{0};
  };

  std::size_t pcore_count = 1;
  std::size_t pcore_logical_count = 1;
  std::size_t ecore_count = 0;
#if defined(__linux__)
  LinuxTopology topo;
  std::size_t cgroup_cpu_quota = 0;
  bool pin_individual_cpus = true;
#endif
  std::atomic<bool> team_busy{false};
  std::mutex mu;
  std::condition_variable cv;
  std::atomic<int> sleeping{0};
  std::atomic<bool> stop{false};
  std::atomic<std::size_t> remaining{0};
  std::atomic<std::size_t> active_nthreads{1};
  void (*task_fn)(void*, std::size_t, std::size_t) = nullptr;
  void* task_ctx = nullptr;
  std::mutex exc_mu;
  std::exception_ptr first_exc;
  std::unique_ptr<WorkerState[]> slots;
  std::vector<std::thread> workers;

  Impl() {
#if defined(__linux__)
    topo = detectLinuxTopology();
    cgroup_cpu_quota = detectLinuxCgroupCpuQuota();
    if (!topo.physical_pcore_groups.empty()) {
      pcore_count = topo.physical_pcore_groups.size();
      std::size_t logical = 0;
      for (const auto& g : topo.physical_pcore_groups) logical += g.size();
      pcore_logical_count = std::max(pcore_count, logical);
    } else {
      pcore_count = std::max<std::size_t>(1, std::thread::hardware_concurrency());
      pcore_logical_count = pcore_count;
    }
    ecore_count = topo.ecore_cpus.size();
    if (cgroup_cpu_quota > 0) {
      pcore_count = std::min(pcore_count, cgroup_cpu_quota);
      pcore_logical_count = std::min(pcore_logical_count, cgroup_cpu_quota);
      const std::size_t ecore_budget =
          cgroup_cpu_quota > pcore_count ? (cgroup_cpu_quota - pcore_count) : 0;
      ecore_count = std::min(ecore_count, ecore_budget);
      pin_individual_cpus =
          (topo.allowed_cpu_count == 0) || (cgroup_cpu_quota >= topo.allowed_cpu_count);
    }
#elif defined(__APPLE__)
    pcore_count = detectApplePcores();
    pcore_logical_count = pcore_count;
#else
    const unsigned hw = std::thread::hardware_concurrency();
    pcore_count = hw > 0 ? static_cast<std::size_t>(hw) : 1;
    pcore_logical_count = pcore_count;
#endif
    const std::size_t nworkers = pcore_logical_count > 1 ? pcore_logical_count - 1 : 0;
    if (nworkers == 0) return;

    // Allocate slots before spawning workers so pointers remain immutable.
    slots = std::make_unique<WorkerState[]>(nworkers);
    workers.reserve(nworkers);
    for (std::size_t w = 0; w < nworkers; ++w) {
      workers.emplace_back([this, w] {
#if defined(__linux__)
        if (!pin_individual_cpus) {
          if (!topo.all_pcore_cpus.empty()) pinMask(topo.all_pcore_cpus);
        } else if (w + 1 < topo.physical_pcore_groups.size()) {
          if (!topo.physical_pcore_groups[w + 1].empty()) {
            pinMask({topo.physical_pcore_groups[w + 1][0]});
          }
        } else if (!topo.pcore_ht_siblings.empty()) {
          const std::size_t ht_idx = w + 1 - topo.physical_pcore_groups.size();
          pinMask({topo.pcore_ht_siblings[ht_idx % topo.pcore_ht_siblings.size()]});
        } else if (!topo.all_pcore_cpus.empty()) {
          pinMask(topo.all_pcore_cpus);
        }
#elif defined(__APPLE__)
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
        std::uint64_t seen = 0;
        for (;;) {
          std::uint64_t cur = slots[w].target_epoch.load(std::memory_order_acquire);
          if (cur == seen && !stop.load(std::memory_order_acquire)) {
            for (int spin = 0; spin < 32768; ++spin) {
              cpuRelax();
              cur = slots[w].target_epoch.load(std::memory_order_acquire);
              if (cur != seen || stop.load(std::memory_order_acquire)) break;
            }
          }
          if (cur == seen) {
            std::unique_lock<std::mutex> lk(mu);
            sleeping.fetch_add(1, std::memory_order_seq_cst);
            cv.wait(lk, [&] {
              cur = slots[w].target_epoch.load(std::memory_order_seq_cst);
              return stop.load(std::memory_order_seq_cst) || cur != seen;
            });
            sleeping.fetch_sub(1, std::memory_order_seq_cst);
          }
          if (stop.load(std::memory_order_acquire)) return;
          seen = cur;
          const std::size_t nthreads = active_nthreads.load(std::memory_order_relaxed);
          try {
            task_fn(task_ctx, w + 1, nthreads);
          } catch (...) {
            std::lock_guard<std::mutex> lk(exc_mu);
            if (!first_exc) first_exc = std::current_exception();
          }
          remaining.fetch_sub(1, std::memory_order_release);
        }
      });
    }
  }

  ~Impl() {
    if (!workers.empty()) {
      {
        std::lock_guard<std::mutex> lk(mu);
        stop.store(true, std::memory_order_seq_cst);
      }
      cv.notify_all();
#ifdef _WIN32
      // On Windows, static dtors inside a .node DLL run under DllMain(DLL_PROCESS_DETACH)
      // with the loader lock held after ExitProcess killed threads; join() would deadlock.
      for (auto& t : workers) {
        if (t.joinable()) t.detach();
      }
#else
      for (auto& t : workers) {
        if (t.joinable()) t.join();
      }
#endif
    }
  }
};

PcorePool::PcorePool() : impl_(new Impl()) {}
PcorePool::~PcorePool() { delete impl_; }

PcorePool& PcorePool::instance() {
  static PcorePool pool;
  return pool;
}

std::size_t PcorePool::pcoreCount() const noexcept {
  return impl_->pcore_count;
}

std::size_t PcorePool::pcoreLogicalCount() const noexcept {
  return impl_->pcore_logical_count;
}

std::size_t PcorePool::ecoreCount() const noexcept {
  return impl_->ecore_count;
}

std::size_t PcorePool::recommendedWorkerCount() noexcept {
  const std::size_t hw = std::max<std::size_t>(1, std::thread::hardware_concurrency());
#if defined(__linux__)
  const auto& inst = *instance().impl_;
  const auto& topo = inst.topo;
  std::size_t rec = hw;
  if (topo.physical_core_count > 0) {
    rec = std::min<std::size_t>({12, topo.physical_core_count, hw});
  } else if (!topo.worker_cpus.empty()) {
    rec = std::min<std::size_t>({12, topo.worker_cpus.size(), hw});
  } else {
    rec = std::min<std::size_t>(12, hw);
  }
  if (inst.cgroup_cpu_quota > 0) rec = std::min(rec, inst.cgroup_cpu_quota);
  return std::max<std::size_t>(1, rec);
#else
  return std::min<std::size_t>(12, hw);
#endif
}

void PcorePool::pinCurrentThreadToPcores() noexcept {
#if defined(__linux__)
  const auto& topo = instance().impl_->topo;
  if (!topo.all_pcore_cpus.empty()) pinMask(topo.all_pcore_cpus);
#elif defined(__APPLE__)
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
}

void PcorePool::pinWorkerThread(std::size_t worker_idx) noexcept {
#if defined(__linux__)
  const auto& inst = *instance().impl_;
  const auto& wcpus = inst.topo.worker_cpus;
  if (!inst.pin_individual_cpus) {
    if (!inst.topo.all_pcore_cpus.empty()) pinMask(inst.topo.all_pcore_cpus);
  } else if (!wcpus.empty()) {
    pinMask({wcpus[worker_idx % wcpus.size()]});
  }
#elif defined(__APPLE__)
  (void)worker_idx;
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#else
  (void)worker_idx;
#endif
}

void PcorePool::pinEcoreThread(std::size_t ecore_idx) noexcept {
#if defined(__linux__)
  const auto& inst = *instance().impl_;
  const auto& ecpus = inst.topo.ecore_cpus;
  if (inst.ecore_count > 0 && !ecpus.empty()) {
    if (inst.pin_individual_cpus) {
      pinMask({ecpus[ecore_idx % ecpus.size()]});
    } else {
      pinMask(ecpus);
    }
  }
#else
  (void)ecore_idx;
#endif
}

bool TeamBarrier::arriveAndWaitSlow() noexcept {
  if (cancelled_.load(std::memory_order_acquire)) return false;
  const std::size_t gen = generation_.load(std::memory_order_relaxed);
  if (waiting_.fetch_add(1, std::memory_order_acq_rel) + 1 == nthreads_) {
    waiting_.store(0, std::memory_order_relaxed);
    generation_.store(gen + 1, std::memory_order_release);
    return !cancelled_.load(std::memory_order_acquire);
  }
  std::size_t spins = 0;
  for (;;) {
    if (cancelled_.load(std::memory_order_acquire)) return false;
    if (generation_.load(std::memory_order_acquire) != gen) {
      return !cancelled_.load(std::memory_order_acquire);
    }
    if ((++spins & 4095) == 0) std::this_thread::yield();
    else cpuRelax();
  }
}

void PcorePool::runTeam(std::size_t max_threads,
                        void (*fn)(void* ctx, std::size_t tid, std::size_t nthreads),
                        void* ctx) {
  if (!fn) return;
  const std::size_t cap = std::min(max_threads, 1 + impl_->workers.size());
  if (cap <= 1 || impl_->team_busy.exchange(true, std::memory_order_acquire)) {
    fn(ctx, 0, 1);
    return;
  }

  struct ReleaseGuard {
    std::atomic<bool>& busy;
    ~ReleaseGuard() { busy.store(false, std::memory_order_release); }
  } guard{impl_->team_busy};

  impl_->task_fn = fn;
  impl_->task_ctx = ctx;
  impl_->first_exc = nullptr;
  impl_->active_nthreads.store(cap, std::memory_order_relaxed);
  impl_->remaining.store(cap - 1, std::memory_order_relaxed);

  for (std::size_t w = 0; w + 1 < cap; ++w) {
    impl_->slots[w].target_epoch.fetch_add(1, std::memory_order_seq_cst);
  }
  if (impl_->sleeping.load(std::memory_order_seq_cst) > 0) {
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->cv.notify_all();
  }

  std::exception_ptr caller_exc;
  try {
    fn(ctx, 0, cap);
  } catch (...) {
    caller_exc = std::current_exception();
  }

  std::size_t spins = 0;
  while (impl_->remaining.load(std::memory_order_acquire) != 0) {
    if ((++spins & 4095) == 0) std::this_thread::yield();
    else cpuRelax();
  }

  if (caller_exc) std::rethrow_exception(caller_exc);
  if (impl_->first_exc) std::rethrow_exception(impl_->first_exc);
}

}  // namespace mtran::ssru
