#pragma once

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

  // Executes fn(ctx, tid, nthreads) across 1..min(max_threads, pcoreCount())
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

 private:
  PcorePool();
  ~PcorePool();
  PcorePool(const PcorePool&) = delete;
  PcorePool& operator=(const PcorePool&) = delete;

  struct Impl;
  Impl* impl_;
};

}  // namespace mtran::ssru
