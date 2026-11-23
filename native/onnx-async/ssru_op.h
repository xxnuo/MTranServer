#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>
#include "onnxruntime_c_api.h"

// The caller owns the domain and releases it after all consuming sessions.
OrtCustomOpDomain* createSsruDomain(const OrtApi* api);

// Preferred host backend; matrices with unsupported dimensions still use InvokeOp.
// Safe to call from baseline code before any AVXVNNI translation unit is entered.
const char* ssruBackend() noexcept;

namespace mtran::onnx_async {

struct ContinuousItem {
  std::size_t id = 0;
  const std::int64_t* input = nullptr;
  std::size_t input_len = 0;
  const std::int64_t* cand = nullptr;
  std::size_t cand_len = 0;
  std::int64_t eos_id = 0;
};

struct ContinuousHook {
  std::function<void(std::size_t id, std::vector<std::int64_t>&& tokens)> on_complete;
  std::function<void(std::size_t id, const char* message)> on_error;
  std::function<void(std::vector<ContinuousItem>& out, std::size_t max_new)> pop_refill;
};

class SsruTranslateKernel {
 public:
  virtual ~SsruTranslateKernel() = default;
  virtual bool hasCandidates() const noexcept = 0;
  virtual void translateBatch(
      const std::int64_t* const* inputs,
      const std::size_t* input_lens,
      const std::int64_t* const* cands,
      const std::size_t* cand_lens,
      std::size_t batch_size,
      std::int64_t eos_id,
      std::vector<std::vector<std::int64_t>>& outputs,
      std::size_t max_threads,
      const std::atomic<std::size_t>* active_submitted = nullptr) = 0;

  virtual void translateContinuous(
      const ContinuousItem* initial_items,
      std::size_t initial_count,
      const ContinuousHook& hook,
      std::size_t max_threads,
      std::size_t max_active_batch = 8,
      const std::atomic<std::size_t>* active_submitted = nullptr) = 0;
};

// Thread-local capture active during OrtApi::CreateSession so addon.cc can
// execute com.mtran::SsruTranslate directly while OrtSession owns kernel lifetime.
class ScopedTranslateCapture {
 public:
  ScopedTranslateCapture() noexcept;
  ~ScopedTranslateCapture() noexcept;
  ScopedTranslateCapture(const ScopedTranslateCapture&) = delete;
  ScopedTranslateCapture& operator=(const ScopedTranslateCapture&) = delete;

  SsruTranslateKernel* captured() const noexcept { return kernel_; }
  void record(SsruTranslateKernel* kernel) noexcept { kernel_ = kernel; }

 private:
  ScopedTranslateCapture* previous_ = nullptr;
  SsruTranslateKernel* kernel_ = nullptr;
};

}  // namespace mtran::onnx_async
