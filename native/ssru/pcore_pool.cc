#include "pcore_pool.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
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
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
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
  std::vector<int> worker_cpus;
  std::size_t physical_core_count = 0;
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
  if (readFileLine("/sys/devices/cpu_core/cpus", buf, sizeof(buf))) {
    for (int c : parseCpuList(buf)) {
      if (is_allowed(c)) candidate_cpus.push_back(c);
    }
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

  // Group remaining non-P-core CPUs (e.g. E-cores) by physical core siblings.
  std::vector<std::vector<int>> ecore_groups;
  for (int c : all_allowed_cpus) {
    if (std::binary_search(candidate_cpus.begin(), candidate_cpus.end(), c) || visited[c]) continue;
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
    if (!g.empty()) topo.worker_cpus.push_back(g[0]);
  }
  topo.physical_core_count = topo.worker_cpus.size();
  for (const auto& g : topo.physical_pcore_groups) {
    for (std::size_t i = 1; i < g.size(); ++i) topo.worker_cpus.push_back(g[i]);
  }
  for (const auto& g : ecore_groups) {
    for (std::size_t i = 1; i < g.size(); ++i) topo.worker_cpus.push_back(g[i]);
  }
  return topo;
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

struct PcorePool::Impl {
  struct alignas(64) WorkerState {
    std::atomic<std::uint64_t> target_epoch{0};
  };

  std::size_t pcore_count = 1;
#if defined(__linux__)
  LinuxTopology topo;
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
    if (!topo.physical_pcore_groups.empty()) {
      pcore_count = topo.physical_pcore_groups.size();
    } else {
      pcore_count = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    }
#elif defined(__APPLE__)
    pcore_count = detectApplePcores();
#else
    const unsigned hw = std::thread::hardware_concurrency();
    pcore_count = hw > 0 ? static_cast<std::size_t>(hw) : 1;
#endif
    const std::size_t nworkers = pcore_count > 1 ? pcore_count - 1 : 0;
    if (nworkers == 0) return;

    // Allocate slots before spawning workers so pointers remain immutable.
    slots = std::make_unique<WorkerState[]>(nworkers);
    workers.reserve(nworkers);
    for (std::size_t w = 0; w < nworkers; ++w) {
      workers.emplace_back([this, w] {
#if defined(__linux__)
        if (w + 1 < topo.physical_pcore_groups.size()) {
          pinMask(topo.physical_pcore_groups[w + 1]);
        } else if (!topo.all_pcore_cpus.empty()) {
          pinMask(topo.all_pcore_cpus);
        }
#elif defined(__APPLE__)
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#elif defined(_WIN32)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
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

std::size_t PcorePool::recommendedWorkerCount() noexcept {
  const std::size_t hw = std::max<std::size_t>(1, std::thread::hardware_concurrency());
#if defined(__linux__)
  const auto& topo = instance().impl_->topo;
  if (topo.physical_core_count > 0) return std::min<std::size_t>({12, topo.physical_core_count, hw});
  if (!topo.worker_cpus.empty()) return std::min<std::size_t>({12, topo.worker_cpus.size(), hw});
#endif
  return std::min<std::size_t>(12, hw);
}

void PcorePool::pinCurrentThreadToPcores() noexcept {
#if defined(__linux__)
  const auto& topo = instance().impl_->topo;
  if (!topo.all_pcore_cpus.empty()) pinMask(topo.all_pcore_cpus);
#elif defined(__APPLE__)
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#elif defined(_WIN32)
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
}

void PcorePool::pinWorkerThread(std::size_t worker_idx) noexcept {
#if defined(__linux__)
  const auto& wcpus = instance().impl_->topo.worker_cpus;
  if (!wcpus.empty()) {
    pinMask({wcpus[worker_idx % wcpus.size()]});
  }
#elif defined(__APPLE__)
  (void)worker_idx;
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#elif defined(_WIN32)
  (void)worker_idx;
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#else
  (void)worker_idx;
#endif
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

  while (impl_->remaining.load(std::memory_order_acquire) != 0) {
    cpuRelax();
  }

  if (caller_exc) std::rethrow_exception(caller_exc);
  if (impl_->first_exc) std::rethrow_exception(impl_->first_exc);
}

}  // namespace mtran::ssru
