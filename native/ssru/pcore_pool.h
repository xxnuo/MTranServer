#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>

namespace mtran::ssru {

// Persistent low-latency worker pool biased/pinned to physical P-cores.
// Uses bounded intra-sentence cpuRelax spinning before sleeping on a
// condition variable (0% idle CPU). Re-entrant/concurrent callers when a
// team is already active execute synchronously on the calling thread.
class PcorePool {
 public:
  static PcorePool& instance();

  std::size_t pcoreCount() const noexcept;
  // Physical P-cores plus their SMT siblings (equals pcoreCount() on non-SMT/non-Linux).
  std::size_t pcoreLogicalCount() const noexcept;
  // Heterogeneous E-cores on Linux (0 elsewhere or on homogeneous CPUs).
  std::size_t ecoreCount() const noexcept;
  static std::size_t recommendedWorkerCount() noexcept;

  // Executes fn(ctx, tid, nthreads) across 1..min(max_threads, pcoreLogicalCount())
  // threads. Caller executes tid=0. Thread-safe and fallback-safe when busy.
  void runTeam(std::size_t max_threads,
               void (*fn)(void* ctx, std::size_t tid, std::size_t nthreads),
               void* ctx);

  template <typename Fn>
  void parallelFor(std::size_t max_threads, Fn&& fn) {
    using FnType = std::remove_reference_t<Fn>;
    struct Adapter {
      static void call(void* raw, std::size_t tid, std::size_t nthreads) {
        (*static_cast<FnType*>(raw))(tid, nthreads);
      }
    };
    runTeam(max_threads, &Adapter::call, &fn);
  }

  // Pins the calling thread to the detected P-core CPU set (Linux),
  // sets user-interactive QoS (macOS), or raises thread priority (Windows).
  static void pinCurrentThreadToPcores() noexcept;
  // Pins worker `worker_idx` (P-cores first, E-cores next, P-core HT siblings last on Linux).
  static void pinWorkerThread(std::size_t worker_idx) noexcept;
  // Pins a background helper thread to a detected E-core (no-op if ecoreCount() == 0).
  static void pinEcoreThread(std::size_t ecore_idx) noexcept;

 private:
  PcorePool();
  ~PcorePool();
  PcorePool(const PcorePool&) = delete;
  PcorePool& operator=(const PcorePool&) = delete;

  struct Impl;
  Impl* impl_;
};

// Persistent intra-team barrier with cancellation support on exception.
class TeamBarrier {
 public:
  explicit TeamBarrier(std::size_t nthreads) noexcept
      : nthreads_(nthreads < 1 ? 1 : nthreads) {}
  bool arriveAndWait() noexcept {
    if (nthreads_ <= 1) return true;
    return arriveAndWaitSlow();
  }
  void cancel() noexcept { cancelled_.store(true, std::memory_order_release); }

 private:
  bool arriveAndWaitSlow() noexcept;
  std::size_t nthreads_;
  alignas(64) std::atomic<std::size_t> waiting_{0};
  alignas(64) std::atomic<std::size_t> generation_{0};
  alignas(64) std::atomic<bool> cancelled_{false};
};

}  // namespace mtran::ssru
