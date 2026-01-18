// ORT headers and LICENSE.onnxruntime are from microsoft/onnxruntime tag v1.30.0.
#include "onnxruntime_c_api.h"
#include <cstring>
#include <cmath>
#include <limits>
#include <dlfcn.h>
#include <memory>
#include <mutex>
#include <node_api.h>
#include <stdexcept>
#include <string>
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
struct State {
  void *lib = nullptr;
  const OrtApi *api = nullptr;
  OrtEnv *env = nullptr;
  OrtSession *session = nullptr;
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
  void release() noexcept {
    if (memory) {
      api->ReleaseMemoryInfo(memory);
      memory = nullptr;
    }
    if (session) {
      api->ReleaseSession(session);
      session = nullptr;
    }
    if (env) {
      api->ReleaseEnv(env);
      env = nullptr;
    }
    if (lib) {
      dlclose(lib);
      lib = nullptr;
    }
  }
  ~State() noexcept { release(); }
};
struct Job;
struct Environment {
  napi_env env;
  napi_async_cleanup_hook_handle hook = nullptr;
  bool shuttingDown = false;
  bool cleanupStarted = false;
  std::unordered_set<Job *> jobs;
  std::vector<std::weak_ptr<State>> states;
#ifdef ONNX_ASYNC_TEST
  std::string fault;
  int32_t *control = nullptr;
#endif
};
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
  bool init = false, accepted = false;
  std::shared_ptr<State> state;
  std::string model, library;
  // Fixed storage makes even the catch path allocation-free.
  char error[1024] = {};
  std::vector<int64_t> x, candidates, result;
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
  if (!context->cleanupStarted || !context->jobs.empty())
    return;
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
    }
  }
  for (auto *job : context.jobs)
    napi_cancel_async_work(context.env, job->work);
}
void cleanup(napi_async_cleanup_hook_handle, void *data) noexcept {
  auto *context = static_cast<Environment *>(data);
  try {
    context->cleanupStarted = true;
#ifdef ONNX_ASYNC_TEST
    count(*context, 2);
#endif
    beginShutdown(*context);
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
void executeWork(napi_env, void *data) noexcept {
  auto &job = *static_cast<Job *>(data);
  auto &s = *job.state;
  try {
#ifdef ONNX_ASYNC_TEST
    waitAtExecute(*job.context);
#endif
    if (job.init) {
      s.lib = dlopen(job.library.c_str(), RTLD_NOW | RTLD_LOCAL);
      if (!s.lib)
        throw std::runtime_error(dlerror());
      auto getApi =
          reinterpret_cast<const OrtApiBase *(ORT_API_CALL *)()>(dlsym(s.lib, "OrtGetApiBase"));
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
      s.check(s.api->CreateSession(s.env, job.model.c_str(), options, &s.session));
      s.check(s.api->SessionGetInputCount(s.session, &s.inputCount));
      if (s.inputCount != 2 && s.inputCount != 3)
        throw std::runtime_error("Expected translation graph with two or three inputs");
      s.check(s.api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &s.memory));
    } else {
      std::vector<std::unique_ptr<OrtValue, void (*)(OrtValue *)>> owned;
      owned.reserve(3);
      const OrtValue *feeds[3];
      int64_t *ptrs[] = {job.x.data(), &job.eos, job.candidates.data()};
      int64_t lengths[] = {static_cast<int64_t>(job.x.size()), 1,
                           static_cast<int64_t>(job.candidates.size())};
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
      job.result.assign(tokens, tokens + n);
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
      } else {
        napi_value buffer = nullptr, value = nullptr;
        void *pointer = nullptr;
        if (!completionStatus(*context,
                              napi_create_arraybuffer(env, job->result.size() * sizeof(int64_t),
                                                      &pointer, &buffer))) {
          // No JS values are created once termination is detected.
        } else {
          if (!job->result.empty())
            std::memcpy(pointer, job->result.data(), job->result.size() * sizeof(int64_t));
          checkNapi(napi_create_typedarray(env, napi_bigint64_array, job->result.size(), buffer, 0,
                                           &value));
          completionStatus(*context, napi_resolve_deferred(env, job->deferred, value));
        }
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
enum class Operation { Init, Run, Close };
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
        job->model = readString(env, args[0]);
        job->library = readString(env, args[1]);
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
        {"close", nullptr, close, nullptr, nullptr, nullptr, napi_default, owned},
#ifdef ONNX_ASYNC_TEST
        {"_testConfigure", nullptr, configureTest, nullptr, nullptr, nullptr, napi_default, owned},
#endif
    };
    checkNapi(napi_define_properties(env, exports, sizeof(props) / sizeof(props[0]), props));
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
