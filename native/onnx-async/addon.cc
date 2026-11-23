// ORT headers and LICENSE.onnxruntime are from microsoft/onnxruntime tag v1.30.0.
#include "onnxruntime_c_api.h"
#ifdef MTRAN_NATIVE_SSRU
#include "ssru_op.h"
#ifdef MTRAN_SSRU_PACKED
#include "../ssru/pcore_pool.h"
#endif
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cmath>
#include <deque>
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef _MSC_VER
#include <delayimp.h>
// Match node-gyp: resolve Node imports from the current host (including Electron).
static FARPROC WINAPI nodeDelayLoad(unsigned event, DelayLoadInfo *info) {
  if (event != dliNotePreLoadLibrary || _stricmp(info->szDll, "node.exe") != 0)
    return nullptr;
  auto host = GetModuleHandleW(L"libnode.dll");
  return reinterpret_cast<FARPROC>(host ? host : GetModuleHandleW(nullptr));
}
decltype(__pfnDliNotifyHook2) __pfnDliNotifyHook2 = nodeDelayLoad;
#endif
#else
#include <dlfcn.h>
#endif
#include <memory>
#include <mutex>
#include <node_api.h>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#ifdef ONNX_ASYNC_TEST
#include <atomic>
#include <chrono>
#include <thread>
#endif

namespace {
void checkNapi(napi_status status) {
  if (status != napi_ok)
    throw std::runtime_error("N-API call failed");
}
// Preserve any pending JS exception; do not retry a failed Promise settlement.
void reportStatus(napi_env env, napi_status status, const char *message) noexcept {
  if (!env || status == napi_ok)
    return;
  bool pending = false;
  if (napi_is_exception_pending(env, &pending) != napi_ok)
    return;
  if (!pending && status != napi_cannot_run_js && status != napi_pending_exception)
    napi_throw_error(env, nullptr, message);
}
// Failure paths never throw or use a napi_value whose creation failed.
void fail(napi_env env, napi_deferred deferred, const char *message) noexcept {
  if (!env)
    return;
  bool pending = false;
  if (napi_is_exception_pending(env, &pending) != napi_ok)
    return;
  if (!deferred) {
    if (!pending)
      napi_throw_error(env, nullptr, message);
    return;
  }
  napi_value error = nullptr;
  if (pending) {
    if (napi_get_and_clear_last_exception(env, &error) != napi_ok)
      return;
  } else {
    napi_value text = nullptr;
    if (napi_create_string_utf8(env, message, NAPI_AUTO_LENGTH, &text) != napi_ok ||
        napi_create_error(env, nullptr, text, &error) != napi_ok) {
      // Preserve an allocation exception when available; otherwise do not fabricate handles.
      if (napi_is_exception_pending(env, &pending) != napi_ok)
        return;
      if (!pending && napi_throw_error(env, nullptr, message) != napi_ok)
        return;
      if (napi_get_and_clear_last_exception(env, &error) != napi_ok)
        return;
    }
  }
  if (error)
    reportStatus(env, napi_reject_deferred(env, deferred, error), "Cannot reject native promise");
}
struct BatchTask;
struct State;
struct Job;
struct Environment {
  napi_env env;
  napi_async_cleanup_hook_handle hook = nullptr;
  bool shuttingDown = false;
  bool cleanupStarted = false;
  bool cleanupInProgress = false;
  std::unordered_set<Job *> jobs;
  std::vector<std::weak_ptr<State>> states;
#ifdef MTRAN_NATIVE_SSRU
  napi_threadsafe_function tsfn = nullptr;
  bool tsfnRefd = false;
  bool tsfnFinalizePending = false;
  size_t batchInflight = 0;
  std::mutex completedMutex;
  std::deque<std::unique_ptr<BatchTask>> completedTasks;
#endif
#ifdef ONNX_ASYNC_TEST
  std::string fault;
  int32_t *control = nullptr;
#endif
};
#ifdef MTRAN_NATIVE_SSRU
struct BatchTask {
  Environment *context = nullptr;
  napi_deferred deferred = nullptr;
  std::shared_ptr<State> state;
  bool isBatch = false;
  std::vector<std::vector<int64_t>> xs;
  std::vector<std::vector<int64_t>> cs;
  int64_t eos = 0;
  std::vector<std::vector<int64_t>> outputs;
  char error[1024] = {};
  void setError(const char *message) noexcept {
    std::strncpy(error, message, sizeof(error) - 1);
    error[sizeof(error) - 1] = '\0';
  }
};
#endif
struct State {
#ifdef _WIN32
  HMODULE lib = nullptr;
#else
  void *lib = nullptr;
#endif
  const OrtApi *api = nullptr;
  OrtEnv *env = nullptr;
  OrtSession *session = nullptr;
#ifdef MTRAN_NATIVE_SSRU
  struct ContGroup {
    std::unique_ptr<BatchTask> task;
    std::atomic<size_t> remaining{0};
    bool is_batch = false;
    std::mutex err_mutex;
  };
  struct ContUnit {
    std::shared_ptr<ContGroup> group;
    size_t item_idx = 0;
    std::shared_ptr<void> pre_encoded;
    bool pre_failed = false;
    std::string pre_error;
  };
  struct WorkerSlot {
    std::condition_variable cv;
    bool idle = false;
    bool woken = false;
  };
  OrtCustomOpDomain *ssruDomain = nullptr;
  mtran::onnx_async::SsruTranslateKernel *translateKernel = nullptr;
  std::mutex batchMutex;
  std::condition_variable ecoreCv;
  std::vector<std::unique_ptr<WorkerSlot>> workerSlots;
  std::deque<std::unique_ptr<BatchTask>> fastQueue;
  std::deque<ContUnit> contRawQueue;
  std::deque<ContUnit> contEncodedQueue;
  std::vector<std::thread> batchWorkers;
  std::vector<std::thread> ecoreWorkers;
  bool batchStop = false;
  bool contRunning = false;
  bool contHasBatch = false;
  std::atomic<size_t> concurrentExecutors{0};
#endif
  OrtMemoryInfo *memory = nullptr;
  size_t inputCount = 0;
  int cpuThreads = 1;
  // Only the owning JS thread mutates lifecycle fields; jobs retain State.
  bool closing = false;
  size_t inflight = 0;
  std::vector<napi_deferred> closers;
  void check(OrtStatus *status) {
    if (!status)
      return;
    std::unique_ptr<OrtStatus, void (*)(OrtStatus *)> owned(status, api->ReleaseStatus);
    throw std::runtime_error(api->GetErrorMessage(status));
  }
#ifdef MTRAN_NATIVE_SSRU
  void wakeIdleWorkersLocked(size_t count) noexcept {
    for (size_t w = 0; w < workerSlots.size() && count > 0; ++w) {
      auto &slot = *workerSlots[w];
      if (slot.idle && !slot.woken) {
        slot.woken = true;
        slot.cv.notify_one();
        --count;
      }
    }
  }
  void wakeContWorkerLocked() noexcept {
    if (workerSlots.empty())
      return;
    auto &slot = *workerSlots[0];
    if (!slot.woken) {
      slot.woken = true;
      slot.cv.notify_one();
    }
  }
  void stopBatchWorker() noexcept {
    {
      std::lock_guard lock(batchMutex);
      batchStop = true;
      for (auto &slot : workerSlots) {
        slot->woken = true;
        slot->cv.notify_all();
      }
    }
    ecoreCv.notify_all();
    for (auto &t : ecoreWorkers) {
      if (t.joinable())
        t.join();
    }
    ecoreWorkers.clear();
    for (auto &t : batchWorkers) {
      if (t.joinable())
        t.join();
    }
    batchWorkers.clear();
    std::lock_guard lock(batchMutex);
    workerSlots.clear();
    fastQueue.clear();
    contRawQueue.clear();
    contEncodedQueue.clear();
    contRunning = false;
    contHasBatch = false;
    concurrentExecutors.store(0, std::memory_order_relaxed);
  }
  void startBatchWorker();
#endif
  void release() noexcept {
#ifdef MTRAN_NATIVE_SSRU
    stopBatchWorker();
    translateKernel = nullptr;
#endif
    if (memory) {
      api->ReleaseMemoryInfo(memory);
      memory = nullptr;
    }
    if (session) {
      api->ReleaseSession(session);
      session = nullptr;
    }
#ifdef MTRAN_NATIVE_SSRU
    if (ssruDomain) {
      api->ReleaseCustomOpDomain(ssruDomain);
      ssruDomain = nullptr;
    }
#endif
    if (env) {
      api->ReleaseEnv(env);
      env = nullptr;
    }
    if (lib) {
#ifdef _WIN32
      FreeLibrary(lib);
#else
      dlclose(lib);
#endif
      lib = nullptr;
    }
  }
  ~State() noexcept { release(); }
};
#ifdef MTRAN_NATIVE_SSRU
void runEcorePreEncodeLoop(State &s, size_t ecoreIdx) noexcept {
#ifdef MTRAN_SSRU_PACKED
  mtran::ssru::PcorePool::pinEcoreThread(ecoreIdx);
#else
  (void)ecoreIdx;
#endif
  constexpr size_t kEcoreMinQueue = 8;
  for (;;) {
    State::ContUnit unit;
    {
      std::unique_lock lock(s.batchMutex);
      s.ecoreCv.wait(lock, [&] {
        return s.batchStop ||
               (s.contRunning && s.contHasBatch && s.contRawQueue.size() >= kEcoreMinQueue);
      });
      if (s.batchStop)
        return;
      unit = std::move(s.contRawQueue.front());
      s.contRawQueue.pop_front();
    }
    try {
      const auto &xVec = unit.group->task->xs[unit.item_idx];
      unit.pre_encoded = s.translateKernel->encodeSingle(xVec.data(), xVec.size(), 1);
    } catch (const std::exception &ex) {
      unit.pre_failed = true;
      unit.pre_error = ex.what();
    } catch (...) {
      unit.pre_failed = true;
      unit.pre_error = "Unknown native pre-encode failure";
    }
    bool wakePcore = false;
    {
      std::lock_guard lock(s.batchMutex);
      if (s.batchStop)
        return;
      s.contEncodedQueue.push_back(std::move(unit));
      wakePcore = !s.contRunning;
    }
    if (wakePcore) {
      std::lock_guard lock(s.batchMutex);
      if (!s.contRunning && (!s.contEncodedQueue.empty() || !s.contRawQueue.empty()))
        s.wakeContWorkerLocked();
    }
  }
}

void runBatchWorkerLoop(State &s, size_t workerIdx) noexcept {
#ifdef MTRAN_SSRU_PACKED
  mtran::ssru::PcorePool::pinWorkerThread(workerIdx);
  const size_t pcores_phys = std::max<size_t>(1, mtran::ssru::PcorePool::instance().pcoreCount());
  const size_t pcores_logical = std::max<size_t>(1, mtran::ssru::PcorePool::instance().pcoreLogicalCount());
  struct ScopedCore0Pin {
    size_t workerIdx;
    bool active = false;
    ScopedCore0Pin(size_t w, bool enable) noexcept : workerIdx(w), active(enable && w != 0) {
      if (active)
        mtran::ssru::PcorePool::pinWorkerThread(0);
    }
    ~ScopedCore0Pin() noexcept {
      if (active)
        mtran::ssru::PcorePool::pinWorkerThread(workerIdx);
    }
  };
#else
  (void)workerIdx;
  const size_t pcores_phys = 1;
  const size_t pcores_logical = 1;
#endif
  auto notifyOneDone = [&](std::unique_ptr<BatchTask> task) noexcept {
    if (!task)
      return;
    Environment *ctx = task->context;
    napi_threadsafe_function tsfn = nullptr;
    {
      std::lock_guard lock(ctx->completedMutex);
      tsfn = ctx->tsfn;
      ctx->completedTasks.push_back(std::move(task));
    }
    if (tsfn)
      napi_call_threadsafe_function(tsfn, nullptr, napi_tsfn_nonblocking);
  };

  auto finishUnitOk = [&](State::ContUnit &unit, std::vector<int64_t> &&tokens) noexcept {
    auto &grp = *unit.group;
    if (!grp.task->error[0])
      grp.task->outputs[unit.item_idx] = std::move(tokens);
    if (grp.remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      if (grp.task->error[0])
        grp.task->outputs.clear();
      notifyOneDone(std::move(grp.task));
    }
  };

  auto finishUnitErr = [&](State::ContUnit &unit, const char *msg) noexcept {
    auto &grp = *unit.group;
    {
      std::lock_guard el(grp.err_mutex);
      if (!grp.task->error[0])
        grp.task->setError(msg ? msg : "Unknown native execution failure");
    }
    if (grp.remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      grp.task->outputs.clear();
      notifyOneDone(std::move(grp.task));
    }
  };

  for (;;) {
    std::unique_ptr<BatchTask> fastTask;
    bool runCont = false;

    std::unique_lock lock(s.batchMutex);
    auto &slot = *s.workerSlots[workerIdx];
    for (;;) {
      if (s.batchStop)
        break;
      const bool contReady =
          (workerIdx == 0) && !s.contRunning &&
          (!s.contEncodedQueue.empty() || !s.contRawQueue.empty());
      if (!s.fastQueue.empty() || contReady)
        break;
      slot.idle = true;
      slot.woken = false;
      slot.cv.wait(lock, [&] { return s.batchStop || slot.woken; });
      slot.idle = false;
    }
    if (s.batchStop)
      break;

    if (!s.fastQueue.empty()) {
      fastTask = std::move(s.fastQueue.front());
      s.fastQueue.pop_front();
    } else if (workerIdx == 0 && !s.contRunning &&
               (!s.contEncodedQueue.empty() || !s.contRawQueue.empty())) {
      s.contRunning = true;
      runCont = true;
    }

    if (fastTask) {
      std::unique_ptr<BatchTask> fastTask2;
      if (!s.fastQueue.empty() && s.fastQueue.front()->eos == fastTask->eos) {
        fastTask2 = std::move(s.fastQueue.front());
        s.fastQueue.pop_front();
      }
      const bool queueNonEmpty = !s.fastQueue.empty();
      lock.unlock();
      if (fastTask2) {
        try {
          const int64_t *ptrs[2] = {fastTask->xs[0].data(), fastTask2->xs[0].data()};
          const size_t lens[2] = {fastTask->xs[0].size(), fastTask2->xs[0].size()};
          const int64_t *cptrs[2] = {
              fastTask->cs[0].empty() ? nullptr : fastTask->cs[0].data(),
              fastTask2->cs[0].empty() ? nullptr : fastTask2->cs[0].data()};
          const size_t clens[2] = {fastTask->cs[0].size(), fastTask2->cs[0].size()};
          std::vector<std::vector<int64_t>> out;
          s.translateKernel->translateBatch(ptrs, lens, cptrs, clens, 2,
                                            fastTask->eos, out, 1, &s.concurrentExecutors);
          fastTask->outputs.push_back(std::move(out[0]));
          fastTask2->outputs.push_back(std::move(out[1]));
        } catch (...) {
          for (auto *ft : {fastTask.get(), fastTask2.get()}) {
            try {
              const int64_t *xp = ft->xs[0].data();
              const size_t xl = ft->xs[0].size();
              const int64_t *cp = ft->cs[0].empty() ? nullptr : ft->cs[0].data();
              const size_t cl = ft->cs[0].size();
              std::vector<std::vector<int64_t>> out;
              s.translateKernel->translateBatch(&xp, &xl, &cp, &cl, 1, ft->eos, out, 1, &s.concurrentExecutors);
              ft->outputs = std::move(out);
            } catch (const std::exception &ex) {
              ft->outputs.clear();
              ft->setError(ex.what());
            } catch (...) {
              ft->outputs.clear();
              ft->setError("Unknown native execution failure");
            }
          }
        }
        s.concurrentExecutors.fetch_sub(2, std::memory_order_relaxed);
        notifyOneDone(std::move(fastTask));
        notifyOneDone(std::move(fastTask2));
        continue;
      }
      const bool singleExec =
          !queueNonEmpty && (s.concurrentExecutors.load(std::memory_order_relaxed) <= 1);
      const size_t maxThreads = singleExec ? pcores_phys : 1;
#ifdef MTRAN_SSRU_PACKED
      ScopedCore0Pin pinGuard(workerIdx, maxThreads > 1);
#endif
      try {
        const int64_t *xPtr = fastTask->xs[0].data();
        const size_t xLen = fastTask->xs[0].size();
        const int64_t *cPtr = fastTask->cs[0].empty() ? nullptr : fastTask->cs[0].data();
        const size_t cLen = fastTask->cs[0].size();
        std::vector<std::vector<int64_t>> out;
        s.translateKernel->translateBatch(&xPtr, &xLen, &cPtr, &cLen, 1,
                                          fastTask->eos, out, maxThreads,
                                          &s.concurrentExecutors);
        fastTask->outputs = std::move(out);
      } catch (const std::exception &ex) {
        fastTask->outputs.clear();
        fastTask->setError(ex.what());
      } catch (...) {
        fastTask->outputs.clear();
        fastTask->setError("Unknown native execution failure");
      }
      s.concurrentExecutors.fetch_sub(1, std::memory_order_relaxed);
      notifyOneDone(std::move(fastTask));
      continue;
    }

    if (!runCont)
      continue;

    struct ContGuard {
      State &s;
      ~ContGuard() {
        std::lock_guard l(s.batchMutex);
        s.contRunning = false;
        if (!s.contEncodedQueue.empty() || !s.contRawQueue.empty()) {
          s.wakeContWorkerLocked();
        } else {
          s.contHasBatch = false;
        }
      }
    } contGuard{s};

    // Wake E-core pre-encoders only for large batched documents behind this wave.
    if (s.contHasBatch && s.contRawQueue.size() >= 8 && !s.ecoreWorkers.empty())
      s.ecoreCv.notify_all();

    std::unordered_map<size_t, State::ContUnit> activeCont;
    activeCont.reserve(32);
    size_t nextId = 1;
    size_t maxBatch = s.contHasBatch ? 16 : 8;

    auto pushUnitToItems = [&](State::ContUnit &&unit,
                               std::vector<mtran::onnx_async::ContinuousItem> &out) {
      if (unit.pre_failed) {
        finishUnitErr(unit, unit.pre_error.c_str());
        return;
      }
      if (unit.group->is_batch)
        maxBatch = 16;
      const size_t id = nextId++;
      const auto &xs = unit.group->task->xs[unit.item_idx];
      const auto &cs = unit.group->task->cs[unit.item_idx];
      mtran::onnx_async::ContinuousItem item;
      item.id = id;
      item.input = xs.data();
      item.input_len = xs.size();
      item.cand = cs.empty() ? nullptr : cs.data();
      item.cand_len = cs.size();
      item.eos_id = unit.group->task->eos;
      item.pre_encoded = unit.pre_encoded;
      activeCont.emplace(id, std::move(unit));
      out.push_back(std::move(item));
    };

    auto pullLocked = [&](std::vector<mtran::onnx_async::ContinuousItem> &out, size_t max_new) {
      while (!s.contEncodedQueue.empty() && out.size() < max_new) {
        auto unit = std::move(s.contEncodedQueue.front());
        s.contEncodedQueue.pop_front();
        pushUnitToItems(std::move(unit), out);
      }
      while (!s.contRawQueue.empty() && out.size() < max_new) {
        auto unit = std::move(s.contRawQueue.front());
        s.contRawQueue.pop_front();
        pushUnitToItems(std::move(unit), out);
      }
    };

    std::vector<mtran::onnx_async::ContinuousItem> initialItems;
    while (initialItems.empty() && (!s.contEncodedQueue.empty() || !s.contRawQueue.empty())) {
      pullLocked(initialItems, maxBatch);
    }
    lock.unlock();
    if (initialItems.empty())
      continue;
#ifdef MTRAN_SSRU_PACKED
    ScopedCore0Pin pinGuard(workerIdx, pcores_logical > 1);
#endif

    mtran::onnx_async::ContinuousHook hook;
    hook.on_complete = [&](size_t id, std::vector<int64_t> &&tokens) {
      auto it = activeCont.find(id);
      if (it == activeCont.end())
        return;
      auto unit = std::move(it->second);
      activeCont.erase(it);
      finishUnitOk(unit, std::move(tokens));
    };
    hook.on_error = [&](size_t id, const char *msg) {
      auto it = activeCont.find(id);
      if (it == activeCont.end())
        return;
      auto unit = std::move(it->second);
      activeCont.erase(it);
      finishUnitErr(unit, msg);
    };
    hook.pop_refill = [&](std::vector<mtran::onnx_async::ContinuousItem> &out, size_t max_new) {
      std::lock_guard l(s.batchMutex);
      pullLocked(out, max_new);
    };

    try {
      // Single P-core continuous worker owns PcorePool; never throttle via active_submitted.
      s.translateKernel->translateContinuous(
          initialItems.data(), initialItems.size(), hook, pcores_logical, maxBatch, nullptr);
    } catch (const std::exception &ex) {
      for (auto &kv : activeCont)
        finishUnitErr(kv.second, ex.what());
      activeCont.clear();
    } catch (...) {
      for (auto &kv : activeCont)
        finishUnitErr(kv.second, "Unknown native execution failure");
      activeCont.clear();
    }
  }
}

void State::startBatchWorker() {
  if (!translateKernel || !batchWorkers.empty())
    return;
  batchStop = false;
  contRunning = false;
  contHasBatch = false;
#ifdef MTRAN_SSRU_PACKED
  auto &pool = mtran::ssru::PcorePool::instance();
  const size_t nWorkers = translateKernel->hasCandidates()
                              ? std::max<size_t>(1, pool.pcoreCount())
                              : 1;
  const size_t nEcores = std::min<size_t>(4, pool.ecoreCount());
#else
  const size_t nWorkers = 1;
  const size_t nEcores = 0;
#endif
  workerSlots.reserve(nWorkers);
  for (size_t w = 0; w < nWorkers; ++w)
    workerSlots.push_back(std::make_unique<WorkerSlot>());
  batchWorkers.reserve(nWorkers);
  for (size_t w = 0; w < nWorkers; ++w) {
    batchWorkers.emplace_back([this, w] { runBatchWorkerLoop(*this, w); });
  }
  ecoreWorkers.reserve(nEcores);
  for (size_t e = 0; e < nEcores; ++e) {
    ecoreWorkers.emplace_back([this, e] { runEcorePreEncodeLoop(*this, e); });
  }
}
#endif
struct Holder {
  napi_env owner;
  std::shared_ptr<State> state;
};
std::mutex holdersMutex;
std::unordered_set<Holder *> holders;
std::shared_ptr<State> getState(napi_env env, napi_value value) {
  void *pointer = nullptr;
  checkNapi(napi_get_value_external(env, value, &pointer));
  std::lock_guard lock(holdersMutex);
  auto *holder = static_cast<Holder *>(pointer);
  if (!holders.contains(holder) || holder->owner != env)
    throw std::runtime_error("Invalid session handle or owner environment");
  return holder->state;
}
void finalizeHolder(napi_env, void *pointer, void *) noexcept {
  auto *holder = static_cast<Holder *>(pointer);
  try {
    std::lock_guard lock(holdersMutex);
    holders.erase(holder);
  } catch (...) {
  }
  holder->state->closing = true;
  delete holder;
}
void finishClose(Environment &context, State &state) noexcept {
  if (!state.closing || state.inflight)
    return;
  state.release();
  if (!context.shuttingDown) {
    for (auto deferred : state.closers) {
      napi_value value = nullptr;
      const auto status = napi_get_undefined(context.env, &value);
      if (status == napi_ok)
        reportStatus(context.env, napi_resolve_deferred(context.env, deferred, value),
                     "Cannot resolve close promise");
      else
        reportStatus(context.env, status, "Cannot complete close");
    }
  }
  state.closers.clear();
}
std::string readString(napi_env env, napi_value value) {
  size_t length = 0;
  checkNapi(napi_get_value_string_utf8(env, value, nullptr, 0, &length));
  std::vector<char> bytes(length + 1);
  checkNapi(napi_get_value_string_utf8(env, value, bytes.data(), bytes.size(), &length));
  if (std::memchr(bytes.data(), 0, length))
    throw std::runtime_error("Path contains NUL");
  return {bytes.data(), length};
}
std::basic_string<ORTCHAR_T> readPath(napi_env env, napi_value value) {
#ifdef _WIN32
  size_t length = 0;
  checkNapi(napi_get_value_string_utf16(env, value, nullptr, 0, &length));
  std::vector<char16_t> chars(length + 1);
  checkNapi(napi_get_value_string_utf16(env, value, chars.data(), chars.size(), &length));
  std::wstring result(chars.begin(), chars.begin() + length);
  if (result.find(L'\0') != std::wstring::npos)
    throw std::runtime_error("Path contains NUL");
  return result;
#else
  return readString(env, value);
#endif
}
std::vector<int64_t> readTokens(napi_env env, napi_value value) {
  napi_typedarray_type type;
  size_t length = 0, offset = 0;
  void *pointer = nullptr;
  napi_value buffer = nullptr;
  checkNapi(napi_get_typedarray_info(env, value, &type, &length, &pointer, &buffer, &offset));
  bool ordinary = false;
  checkNapi(napi_is_arraybuffer(env, buffer, &ordinary));
  if (!ordinary || type != napi_bigint64_array || !pointer || length == 0 || length > 1000000)
    throw std::runtime_error("Expected nonempty BigInt64Array backed by ArrayBuffer");
  auto *tokens = static_cast<int64_t *>(pointer);
  // Copy on submission so caller mutation/detach cannot race the worker.
  std::vector<int64_t> result(tokens, tokens + length);
  for (auto token : result)
    if (token < 0)
      throw std::runtime_error("Negative token ID");
  return result;
}
struct Job {
  Environment *context = nullptr;
  napi_deferred deferred = nullptr;
  napi_async_work work = nullptr;
  bool init = false, accepted = false, batchFallback = false;
  std::shared_ptr<State> state;
  std::basic_string<ORTCHAR_T> model, library;
  // Fixed storage makes even the catch path allocation-free.
  char error[1024] = {};
  std::vector<int64_t> x, candidates, result;
  std::vector<std::vector<int64_t>> batchX, batchC, batchOutputs;
  int64_t eos = 0;
  void setError(const char *message) noexcept {
    std::strncpy(error, message, sizeof(error) - 1);
    error[sizeof(error) - 1] = '\0';
  }
  // No N-API in destructors: work is deleted explicitly while the cleanup hook holds env alive.
};
#ifdef ONNX_ASYNC_TEST
bool injected(Environment &context, const char *point) noexcept {
  if (context.fault != point)
    return false;
  context.fault.clear();
  return true;
}
void count(Environment &context, size_t index, int32_t delta = 1) noexcept {
  if (context.control)
    std::atomic_ref<int32_t>(context.control[index]).fetch_add(delta);
}
void waitAtExecute(Environment &context) noexcept {
  if (!context.control)
    return;
  count(context, 0);
  while (!std::atomic_ref<int32_t>(context.control[1]).load())
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
#endif
// Called only on the JS thread. The async cleanup hook keeps env valid until every completion.
void finishTeardown(Environment *context) noexcept {
  if (!context->cleanupStarted || context->cleanupInProgress || !context->jobs.empty())
    return;
#ifdef MTRAN_NATIVE_SSRU
  if (context->tsfnFinalizePending || context->batchInflight != 0)
    return;
#endif
  for (auto &weak : context->states) {
    if (auto state = weak.lock()) {
      state->closing = true;
      state->closers.clear();
      state->release();
    }
  }
  auto hook = context->hook;
  context->hook = nullptr;
  // Once removed, Node may finish destroying env; no more N-API calls follow.
  napi_remove_async_cleanup_hook(hook);
  delete context;
}
void beginShutdown(Environment &context) noexcept {
  // libuv cancellation is not idempotent for a cancelled work already awaiting completion.
  if (context.shuttingDown)
    return;
  context.shuttingDown = true;
  for (auto &weak : context.states) {
    if (auto state = weak.lock()) {
      state->closing = true;
      state->closers.clear();
#ifdef MTRAN_NATIVE_SSRU
      state->stopBatchWorker();
#endif
    }
  }
  for (auto *job : context.jobs)
    napi_cancel_async_work(context.env, job->work);
#ifdef MTRAN_NATIVE_SSRU
  {
    std::lock_guard lock(context.completedMutex);
    context.completedTasks.clear();
  }
  context.batchInflight = 0;
  if (context.tsfn) {
    auto tsfn = context.tsfn;
    context.tsfn = nullptr;
    context.tsfnRefd = false;
    context.tsfnFinalizePending = true;
    napi_release_threadsafe_function(tsfn, napi_tsfn_abort);
  }
#endif
}
void cleanup(napi_async_cleanup_hook_handle, void *data) noexcept {
  auto *context = static_cast<Environment *>(data);
  try {
    context->cleanupStarted = true;
    context->cleanupInProgress = true;
#ifdef ONNX_ASYNC_TEST
    count(*context, 2);
#endif
    beginShutdown(*context);
    context->cleanupInProgress = false;
    finishTeardown(context);
  } catch (...) {
  }
}
// Node can drain async_work before invoking cleanup hooks. N-API's allocation
// preamble then reports JS unavailable before allocating any returned JS object.
bool completionStatus(Environment &context, napi_status status) {
  bool pending = false;
  if (status == napi_cannot_run_js ||
      (status == napi_pending_exception &&
       napi_is_exception_pending(context.env, &pending) == napi_ok && !pending)) {
    beginShutdown(context);
    return false;
  }
  checkNapi(status);
  return true;
}
bool buildBigInt64TypedArray(Environment &context, const std::vector<int64_t> &tokens, napi_value *out) {
  napi_value buffer = nullptr;
  void *pointer = nullptr;
  if (!completionStatus(context,
                        napi_create_arraybuffer(context.env, tokens.size() * sizeof(int64_t),
                                                &pointer, &buffer)))
    return false;
  if (!tokens.empty())
    std::memcpy(pointer, tokens.data(), tokens.size() * sizeof(int64_t));
  checkNapi(napi_create_typedarray(context.env, napi_bigint64_array, tokens.size(), buffer, 0, out));
  return true;
}
#ifdef MTRAN_NATIVE_SSRU
void tsfnFinalize(napi_env, void *finalize_data, void *) noexcept {
  auto *context = static_cast<Environment *>(finalize_data);
  context->tsfnFinalizePending = false;
  finishTeardown(context);
}
void flushCompletedBatch(napi_env env, napi_value, void *ctx, void *) noexcept {
  auto *context = static_cast<Environment *>(ctx);
  std::deque<std::unique_ptr<BatchTask>> done;
  {
    std::lock_guard lock(context->completedMutex);
    std::swap(done, context->completedTasks);
  }
  for (auto &task : done) {
    auto &state = *task->state;
    try {
      if (!context->shuttingDown && env) {
        if (task->error[0]) {
          fail(env, task->deferred, task->error);
        } else if (!task->isBatch) {
          napi_value val = nullptr;
          if (buildBigInt64TypedArray(*context, task->outputs[0], &val))
            completionStatus(*context, napi_resolve_deferred(env, task->deferred, val));
        } else {
          napi_value arr = nullptr;
          if (completionStatus(*context, napi_create_array_with_length(env, task->outputs.size(), &arr))) {
            bool ok = true;
            for (size_t i = 0; i < task->outputs.size(); ++i) {
              napi_value item = nullptr;
              if (!buildBigInt64TypedArray(*context, task->outputs[i], &item)) {
                ok = false;
                break;
              }
              checkNapi(napi_set_element(env, arr, static_cast<uint32_t>(i), item));
            }
            if (ok)
              completionStatus(*context, napi_resolve_deferred(env, task->deferred, arr));
          }
        }
      }
    } catch (const std::exception &err) {
      if (!context->shuttingDown && env)
        fail(env, task->deferred, err.what());
    } catch (...) {
      if (!context->shuttingDown && env)
        fail(env, task->deferred, "Unknown batch completion failure");
    }
    if (context->batchInflight > 0)
      context->batchInflight--;
    if (state.inflight > 0)
      state.inflight--;
    finishClose(*context, state);
  }
  if (context->batchInflight == 0 && context->tsfn && context->tsfnRefd) {
    if (napi_unref_threadsafe_function(context->env, context->tsfn) == napi_ok)
      context->tsfnRefd = false;
  }
  finishTeardown(context);
}
void ensureBatchTsfn(Environment &context) {
  if (context.tsfn)
    return;
  napi_value name = nullptr;
  checkNapi(napi_create_string_utf8(context.env, "ort:ssru_batch", NAPI_AUTO_LENGTH, &name));
  napi_threadsafe_function created = nullptr;
  checkNapi(napi_create_threadsafe_function(context.env, nullptr, nullptr, name, 0, 1,
                                           &context, tsfnFinalize, &context,
                                           flushCompletedBatch, &created));
  context.tsfn = created;
  if (napi_unref_threadsafe_function(context.env, context.tsfn) != napi_ok) {
    auto tmp = context.tsfn;
    context.tsfn = nullptr;
    napi_release_threadsafe_function(tmp, napi_tsfn_abort);
    throw std::runtime_error("Cannot unref batch threadsafe function");
  }
  context.tsfnRefd = false;
}
#endif
std::vector<int64_t> runOrtSingle(State &s, std::vector<int64_t> &xVec,
                                  std::vector<int64_t> &cVec, int64_t eos) {
  std::vector<std::unique_ptr<OrtValue, void (*)(OrtValue *)>> owned;
  owned.reserve(3);
  const OrtValue *feeds[3];
  int64_t *ptrs[] = {xVec.data(), &eos, cVec.data()};
  int64_t lengths[] = {static_cast<int64_t>(xVec.size()), 1, static_cast<int64_t>(cVec.size())};
  for (size_t i = 0; i < s.inputCount; i++) {
    OrtValue *v = nullptr;
    s.check(s.api->CreateTensorWithDataAsOrtValue(s.memory, ptrs[i],
                                                  lengths[i] * sizeof(int64_t), &lengths[i], 1,
                                                  ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &v));
    owned.emplace_back(v, s.api->ReleaseValue);
    feeds[i] = v;
  }
  const char *names[] = {"X", "eos_id", "candidate_ids"};
  const char *output = "Y";
  OrtValue *raw = nullptr;
  s.check(s.api->Run(s.session, nullptr, names, feeds, s.inputCount, &output, 1, &raw));
  std::unique_ptr<OrtValue, void (*)(OrtValue *)> out(raw, s.api->ReleaseValue);
  OrtTensorTypeAndShapeInfo *info = nullptr;
  s.check(s.api->GetTensorTypeAndShape(raw, &info));
  std::unique_ptr<OrtTensorTypeAndShapeInfo, void (*)(OrtTensorTypeAndShapeInfo *)> shape(
      info, s.api->ReleaseTensorTypeAndShapeInfo);
  ONNXTensorElementDataType type;
  s.check(s.api->GetTensorElementType(info, &type));
  if (type != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64)
    throw std::runtime_error("Expected int64 Y");
  size_t n;
  s.check(s.api->GetTensorShapeElementCount(info, &n));
  void *p;
  s.check(s.api->GetTensorMutableData(raw, &p));
  auto *tokens = static_cast<int64_t *>(p);
  return {tokens, tokens + n};
}
void executeWork(napi_env, void *data) noexcept {
  auto &job = *static_cast<Job *>(data);
  auto &s = *job.state;
  try {
#ifdef ONNX_ASYNC_TEST
    waitAtExecute(*job.context);
#endif
    if (job.init) {
#ifdef _WIN32
      s.lib = LoadLibraryW(job.library.c_str());
      if (!s.lib)
        throw std::runtime_error("Cannot load ORT library (Windows error " +
                                 std::to_string(GetLastError()) + ")");
      auto getApi = reinterpret_cast<decltype(&OrtGetApiBase)>(GetProcAddress(s.lib, "OrtGetApiBase"));
#else
      s.lib = dlopen(job.library.c_str(), RTLD_NOW | RTLD_LOCAL);
      if (!s.lib)
        throw std::runtime_error(dlerror());
      auto getApi = reinterpret_cast<decltype(&OrtGetApiBase)>(dlsym(s.lib, "OrtGetApiBase"));
#endif
      if (!getApi)
        throw std::runtime_error("Missing OrtGetApiBase");
      auto *base = getApi();
      if (std::string(base->GetVersionString()) != "1.30.0")
        throw std::runtime_error("Expected ORT 1.30.0");
      s.api = base->GetApi(ORT_API_VERSION);
      if (!s.api)
        throw std::runtime_error("Missing ORT API 30");
      s.check(s.api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "shared-addon", &s.env));
      OrtSessionOptions *options = nullptr;
      s.check(s.api->CreateSessionOptions(&options));
      std::unique_ptr<OrtSessionOptions, void (*)(OrtSessionOptions *)> opts(
          options, s.api->ReleaseSessionOptions);
      s.check(s.api->SetIntraOpNumThreads(options, s.cpuThreads));
      s.check(s.api->SetInterOpNumThreads(options, 1));
      s.check(s.api->AddSessionConfigEntry(options, "session.intra_op.allow_spinning", "0"));
      s.check(s.api->AddSessionConfigEntry(options, "session.inter_op.allow_spinning", "0"));
      s.check(s.api->SetSessionExecutionMode(options, ORT_SEQUENTIAL));
      s.check(s.api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL));
#ifdef MTRAN_NATIVE_SSRU
      s.ssruDomain = createSsruDomain(s.api);
      s.check(s.api->AddCustomOpDomain(options, s.ssruDomain));
      {
        mtran::onnx_async::ScopedTranslateCapture capture;
        s.check(s.api->CreateSession(s.env, job.model.c_str(), options, &s.session));
        s.translateKernel = capture.captured();
      }
#else
      s.check(s.api->CreateSession(s.env, job.model.c_str(), options, &s.session));
#endif
      s.check(s.api->SessionGetInputCount(s.session, &s.inputCount));
      if (s.inputCount != 2 && s.inputCount != 3)
        throw std::runtime_error("Expected translation graph with two or three inputs");
      s.check(s.api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &s.memory));
    } else if (job.batchFallback) {
      job.batchOutputs.resize(job.batchX.size());
      for (size_t i = 0; i < job.batchX.size(); ++i)
        job.batchOutputs[i] = runOrtSingle(s, job.batchX[i], job.batchC[i], job.eos);
    } else {
      job.result = runOrtSingle(s, job.x, job.candidates, job.eos);
    }
  } catch (const std::exception &ex) {
    job.setError(ex.what());
  } catch (...) {
    job.setError("Unknown native execution failure");
  }
}
void completeWork(napi_env env, napi_status status, void *data) noexcept {
  std::unique_ptr<Job> job(static_cast<Job *>(data));
  auto *context = job->context;
  // A completion must never cancel/requeue its own work, especially a cancelled work.
  context->jobs.erase(job.get());
  auto &state = *job->state;
  try {
    if (status == napi_cancelled)
      beginShutdown(*context);
    if (!context->shuttingDown && env) {
      if (status != napi_ok && !job->error[0])
        job->setError("Async work cancelled");
      if (job->error[0])
        fail(env, job->deferred, job->error);
      else if (job->init) {
#ifdef MTRAN_NATIVE_SSRU
        if (state.translateKernel) {
          ensureBatchTsfn(*context);
          state.startBatchWorker();
        }
#endif
        auto holder = std::make_unique<Holder>();
        holder->owner = env;
        holder->state = job->state;
        napi_value value = nullptr;
        {
          std::lock_guard lock(holdersMutex);
          holders.insert(holder.get());
        }
        auto result = napi_create_external(env, holder.get(), finalizeHolder, nullptr, &value);
        if (result != napi_ok) {
          {
            std::lock_guard lock(holdersMutex);
            holders.erase(holder.get());
          }
        }
        if (completionStatus(*context, result)) {
          holder.release();
          completionStatus(*context, napi_resolve_deferred(env, job->deferred, value));
        }
      } else if (job->batchFallback) {
        napi_value arr = nullptr;
        if (completionStatus(*context, napi_create_array_with_length(env, job->batchOutputs.size(), &arr))) {
          bool ok = true;
          for (size_t i = 0; i < job->batchOutputs.size(); ++i) {
            napi_value item = nullptr;
            if (!buildBigInt64TypedArray(*context, job->batchOutputs[i], &item)) {
              ok = false;
              break;
            }
            checkNapi(napi_set_element(env, arr, static_cast<uint32_t>(i), item));
          }
          if (ok)
            completionStatus(*context, napi_resolve_deferred(env, job->deferred, arr));
        }
      } else {
        napi_value value = nullptr;
        if (buildBigInt64TypedArray(*context, job->result, &value))
          completionStatus(*context, napi_resolve_deferred(env, job->deferred, value));
      }
    }
  } catch (const std::exception &error) {
    if (!context->shuttingDown && env)
      fail(env, job->deferred, error.what());
  } catch (...) {
    if (!context->shuttingDown && env)
      fail(env, job->deferred, "Unknown completion failure");
  }
  // This tail does not allocate or invoke JS; deleting work precedes removal of the cleanup hook.
  if (job->accepted) {
    state.inflight--;
    finishClose(*context, state);
  }
  napi_delete_async_work(context->env, job->work);
  job->work = nullptr;
#ifdef ONNX_ASYNC_TEST
  count(*context, 3);
  if (status == napi_cancelled)
    count(*context, 4);
  count(*context, 5, -1);
#endif
  job.reset();
  finishTeardown(context);
}
void queueWork(std::unique_ptr<Job> &job) {
  auto &context = *job->context;
  napi_value name = nullptr;
  checkNapi(napi_create_string_utf8(context.env, job->init ? "ort:init" : "ort:run",
                                    NAPI_AUTO_LENGTH, &name));
  napi_status createStatus;
#ifdef ONNX_ASYNC_TEST
  if (injected(context, "async_work"))
    createStatus = napi_generic_failure;
  else
#endif
    createStatus = napi_create_async_work(context.env, nullptr, name, executeWork, completeWork,
                                          job.get(), &job->work);
  checkNapi(createStatus);
  try {
    context.jobs.insert(job.get());
    napi_status queueStatus;
#ifdef ONNX_ASYNC_TEST
    if (injected(context, "queue"))
      queueStatus = napi_generic_failure;
    else
#endif
      queueStatus = napi_queue_async_work(context.env, job->work);
    checkNapi(queueStatus);
  } catch (...) {
    context.jobs.erase(job.get());
    napi_delete_async_work(context.env, job->work);
    job->work = nullptr;
    throw;
  }
#ifdef ONNX_ASYNC_TEST
  count(context, 5);
#endif
  job.release();
}
#ifdef MTRAN_NATIVE_SSRU
void enqueueBatchTask(Environment &context, State &state, std::unique_ptr<BatchTask> task) {
  if (!context.tsfn)
    throw std::runtime_error("Batch threadsafe function unavailable");
  if (!context.tsfnRefd) {
    checkNapi(napi_ref_threadsafe_function(context.env, context.tsfn));
    context.tsfnRefd = true;
  }
  const bool hasCands = state.translateKernel->hasCandidates();
  const bool workerRoute = (hasCands && !task->isBatch);
  std::shared_ptr<State::ContGroup> contGroup;
  if (!workerRoute) {
    const size_t total = task->xs.size();
    contGroup = std::make_shared<State::ContGroup>();
    contGroup->is_batch = task->isBatch;
    contGroup->remaining.store(total, std::memory_order_relaxed);
    task->outputs.resize(total);
    contGroup->task = std::move(task);
  }

  context.batchInflight++;
  state.inflight++;
  try {
    bool notifyEcores = false;
    {
      std::lock_guard lock(state.batchMutex);
      if (workerRoute) {
        state.concurrentExecutors.fetch_add(1, std::memory_order_relaxed);
        state.fastQueue.push_back(std::move(task));
        state.wakeIdleWorkersLocked(1);
      } else {
        const size_t total = contGroup->task->xs.size();
        if (contGroup->is_batch)
          state.contHasBatch = true;
        for (size_t i = 0; i < total; ++i) {
          state.contRawQueue.push_back(State::ContUnit{contGroup, i, {}, false, {}});
        }
        if (!state.ecoreWorkers.empty() && state.contRunning &&
            state.contHasBatch && state.contRawQueue.size() >= 8) {
          notifyEcores = true;
        }
        if (!state.contRunning)
          state.wakeContWorkerLocked();
      }
    }
    if (notifyEcores)
      state.ecoreCv.notify_all();
  } catch (...) {
    state.inflight--;
    context.batchInflight--;
    if (context.batchInflight == 0 && context.tsfnRefd &&
        napi_unref_threadsafe_function(context.env, context.tsfn) == napi_ok)
      context.tsfnRefd = false;
    throw;
  }
}
#endif
enum class Operation { Init, Run, RunBatch, Close };
napi_value invoke(napi_env env, napi_callback_info info, Operation operation) noexcept {
  napi_deferred deferred = nullptr;
  napi_value promise = nullptr;
  Environment *context = nullptr;
  std::shared_ptr<State> state;
  bool accepted = false;
  try {
    size_t count = 4;
    napi_value args[4] = {};
    void *data = nullptr;
    checkNapi(napi_get_cb_info(env, info, &count, args, nullptr, &data));
    context = static_cast<Environment *>(data);
    if (context->shuttingDown)
      return nullptr;
    napi_deferred created = nullptr;
    napi_value value = nullptr;
    napi_status promiseStatus;
#ifdef ONNX_ASYNC_TEST
    if (injected(*context, "promise"))
      promiseStatus = napi_generic_failure;
    else
#endif
      promiseStatus = napi_create_promise(env, &created, &value);
    checkNapi(promiseStatus);
    deferred = created;
    promise = value;
    if (operation == Operation::Close) {
      if (count != 1)
        throw std::runtime_error("close(handle)");
      state = getState(env, args[0]);
      state->closers.push_back(deferred);
      state->closing = true;
      finishClose(*context, *state);
    } else if (operation == Operation::RunBatch) {
      if (count != 4)
        throw std::runtime_error("runBatch(handle, Xs, candidatesList, eos)");
      state = getState(env, args[0]);
      if (state->closing)
        throw std::runtime_error("Session is closing or closed");
      bool xsIsArray = false, csIsArray = false;
      checkNapi(napi_is_array(env, args[1], &xsIsArray));
      checkNapi(napi_is_array(env, args[2], &csIsArray));
      if (!xsIsArray || !csIsArray)
        throw std::runtime_error("Expected array inputs for runBatch");
      uint32_t batchLen = 0, candArrLen = 0;
      checkNapi(napi_get_array_length(env, args[1], &batchLen));
      checkNapi(napi_get_array_length(env, args[2], &candArrLen));
      if (batchLen != candArrLen)
        throw std::runtime_error("Batch inputs and candidates length mismatch");
      bool lossless = false;
      int64_t eos = 0;
      checkNapi(napi_get_value_bigint_int64(env, args[3], &eos, &lossless));
      if (!lossless || eos < 0)
        throw std::runtime_error("Invalid eos int64");
      if (batchLen == 0) {
        napi_value emptyArr = nullptr;
        checkNapi(napi_create_array_with_length(env, 0, &emptyArr));
        checkNapi(napi_resolve_deferred(env, deferred, emptyArr));
        return promise;
      }
      std::vector<std::vector<int64_t>> batchX(batchLen);
      std::vector<std::vector<int64_t>> batchC(batchLen);
      for (uint32_t i = 0; i < batchLen; ++i) {
        napi_value xEl = nullptr, cEl = nullptr;
        checkNapi(napi_get_element(env, args[1], i, &xEl));
        checkNapi(napi_get_element(env, args[2], i, &cEl));
        batchX[i] = readTokens(env, xEl);
        if (state->inputCount == 3) {
          batchC[i] = readTokens(env, cEl);
        } else {
          napi_valuetype type;
          checkNapi(napi_typeof(env, cEl, &type));
          if (type != napi_null)
            throw std::runtime_error("Standard graph requires null candidates");
        }
      }
#ifdef MTRAN_NATIVE_SSRU
      if (state->translateKernel) {
        auto task = std::make_unique<BatchTask>();
        task->context = context;
        task->deferred = deferred;
        task->state = state;
        task->isBatch = true;
        task->xs = std::move(batchX);
        task->cs = std::move(batchC);
        task->eos = eos;
        enqueueBatchTask(*context, *state, std::move(task));
        return promise;
      }
#endif
      auto job = std::make_unique<Job>();
      job->context = context;
      job->deferred = deferred;
      job->state = state;
      job->batchFallback = true;
      job->batchX = std::move(batchX);
      job->batchC = std::move(batchC);
      job->eos = eos;
      state->inflight++;
      accepted = true;
      job->accepted = true;
      queueWork(job);
    } else {
      auto job = std::make_unique<Job>();
      job->context = context;
      job->deferred = deferred;
      if (operation == Operation::Init) {
        if (count != 2 && count != 3)
          throw std::runtime_error("init(modelPath, ortLibraryPath, cpuThreads?)");
        job->init = true;
        job->state = std::make_shared<State>();
        if (count == 3) {
          double threads;
          checkNapi(napi_get_value_double(env, args[2], &threads));
          if (!std::isfinite(threads) || threads < 0 || threads > std::numeric_limits<int>::max() || std::floor(threads) != threads)
            throw std::runtime_error("cpuThreads must be a non-negative integer");
          job->state->cpuThreads = static_cast<int>(threads);
        }
        job->model = readPath(env, args[0]);
        job->library = readPath(env, args[1]);
        std::erase_if(context->states, [](const auto &previous) { return previous.expired(); });
        context->states.emplace_back(job->state);
      } else {
        if (count != 4)
          throw std::runtime_error("run(handle, X, candidates, eos)");
        state = getState(env, args[0]);
        if (state->closing)
          throw std::runtime_error("Session is closing or closed");
        job->state = state;
        job->x = readTokens(env, args[1]);
        if (state->inputCount == 3) job->candidates = readTokens(env, args[2]);
        else {
          napi_valuetype type;
          checkNapi(napi_typeof(env, args[2], &type));
          if (type != napi_null) throw std::runtime_error("Standard graph requires null candidates");
        }
        bool lossless = false;
        checkNapi(napi_get_value_bigint_int64(env, args[3], &job->eos, &lossless));
        if (!lossless || job->eos < 0)
          throw std::runtime_error("Invalid eos int64");
#ifdef MTRAN_NATIVE_SSRU
        if (state->translateKernel) {
          auto task = std::make_unique<BatchTask>();
          task->context = context;
          task->deferred = deferred;
          task->state = state;
          task->isBatch = false;
          task->xs.push_back(std::move(job->x));
          task->cs.push_back(std::move(job->candidates));
          task->eos = job->eos;
          enqueueBatchTask(*context, *state, std::move(task));
          return promise;
        }
#endif
        state->inflight++;
        accepted = true;
        job->accepted = true;
      }
      queueWork(job);
    }
  } catch (const std::exception &error) {
    if (accepted) {
      state->inflight--;
      finishClose(*context, *state);
    }
    if (!context || !context->shuttingDown)
      fail(env, deferred, error.what());
  } catch (...) {
    if (accepted) {
      state->inflight--;
      finishClose(*context, *state);
    }
    if (!context || !context->shuttingDown)
      fail(env, deferred, "Unknown native callback failure");
  }
  return promise;
}
napi_value init(napi_env env, napi_callback_info info) noexcept {
  return invoke(env, info, Operation::Init);
}
napi_value run(napi_env env, napi_callback_info info) noexcept {
  return invoke(env, info, Operation::Run);
}
napi_value runBatch(napi_env env, napi_callback_info info) noexcept {
  return invoke(env, info, Operation::RunBatch);
}
napi_value close(napi_env env, napi_callback_info info) noexcept {
  return invoke(env, info, Operation::Close);
}
#ifdef ONNX_ASYNC_TEST
// Compiled only into the isolated test addon, never into the production exports.
napi_value configureTest(napi_env env, napi_callback_info info) noexcept {
  try {
    size_t length = 2;
    napi_value args[2] = {};
    void *data = nullptr;
    checkNapi(napi_get_cb_info(env, info, &length, args, nullptr, &data));
    auto &context = *static_cast<Environment *>(data);
    context.fault = readString(env, args[0]);
    if (length == 2) {
      napi_typedarray_type type;
      size_t n, offset;
      void *pointer = nullptr;
      napi_value buffer;
      checkNapi(napi_get_typedarray_info(env, args[1], &type, &n, &pointer, &buffer, &offset));
      if (type != napi_int32_array || n < 6 || !pointer)
        throw std::runtime_error("Expected control Int32Array[6]");
      context.control = static_cast<int32_t *>(pointer);
    }
    napi_value result = nullptr;
    checkNapi(napi_get_undefined(env, &result));
    return result;
  } catch (const std::exception &error) {
    fail(env, nullptr, error.what());
  } catch (...) {
    fail(env, nullptr, "Test configuration failure");
  }
  return nullptr;
}
#endif
napi_value registerModule(napi_env env, napi_value exports) noexcept {
  try {
    auto context = std::make_unique<Environment>();
    context->env = env;
    checkNapi(napi_add_async_cleanup_hook(env, cleanup, context.get(), &context->hook));
    auto *owned = context.release(); // Hook owns this until all init/run completions drain.
    napi_property_descriptor props[] = {
        {"init", nullptr, init, nullptr, nullptr, nullptr, napi_default, owned},
        {"run", nullptr, run, nullptr, nullptr, nullptr, napi_default, owned},
        {"runBatch", nullptr, runBatch, nullptr, nullptr, nullptr, napi_default, owned},
        {"close", nullptr, close, nullptr, nullptr, nullptr, napi_default, owned},
#ifdef ONNX_ASYNC_TEST
        {"_testConfigure", nullptr, configureTest, nullptr, nullptr, nullptr, napi_default, owned},
#endif
    };
    checkNapi(napi_define_properties(env, exports, sizeof(props) / sizeof(props[0]), props));
#ifdef MTRAN_NATIVE_SSRU
    napi_value ssruVersion = nullptr;
    checkNapi(napi_create_uint32(env, 1, &ssruVersion));
    checkNapi(napi_set_named_property(env, exports, "ssruVersion", ssruVersion));
    napi_value backend = nullptr;
    checkNapi(napi_create_string_utf8(env, ssruBackend(), NAPI_AUTO_LENGTH, &backend));
    checkNapi(napi_set_named_property(env, exports, "ssruBackend", backend));
#endif
    return exports;
  } catch (const std::exception &error) {
    fail(env, nullptr, error.what());
  } catch (...) {
    fail(env, nullptr, "Native module registration failure");
  }
  return nullptr;
}
} // namespace
NAPI_MODULE(NODE_GYP_MODULE_NAME, registerModule)
