#include "ssru_op.cc"
#include <cassert>
#include <dlfcn.h>
#include <iostream>

namespace {
const OrtApi* realApi;
const char* disabledKey;
OrtStatus* ORT_API_CALL disabledConfig(const OrtKernelInfo*, OrtKeyValuePairs** out) noexcept {
  realApi->CreateKeyValuePairs(out);
  realApi->AddKeyValuePair(*out, disabledKey, "1");
  return nullptr;
}
OrtStatus* ORT_API_CALL failedSession(const OrtEnv*, const void*, size_t,
                                    const OrtSessionOptions*, OrtSession**) noexcept {
  return realApi->CreateStatus(ORT_FAIL, "probe creation failed");
}
OrtStatus* ORT_API_CALL unknownResult(OrtSession* session, const OrtRunOptions* options,
    const char* const* names, const OrtValue* const* inputs, size_t inputCount,
    const char* const* outputNames, size_t outputCount, OrtValue** outputs) noexcept {
  auto* status = realApi->Run(session, options, names, inputs, inputCount, outputNames, outputCount, outputs);
  if (!status) *static_cast<float*>(data(realApi, outputs[0])) = 999.f;
  return status;
}
template<class F> void rejects(F run, const char* message) {
  bool failed = false;
  try { run(); } catch (const std::runtime_error& error) {
    failed = true; assert(std::string(error.what()).find(message) != std::string::npos);
  }
  assert(failed);
}
}
int main(int argc, char** argv) {
  assert(argc == 2);
  auto* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  assert(library);
  auto entry = reinterpret_cast<decltype(&OrtGetApiBase)>(dlsym(library, "OrtGetApiBase"));
  assert(entry);
  realApi = entry()->GetApi(ORT_API_VERSION);
  assert(realApi);
  {
    OrtMemoryInfo* rawMemory = nullptr;
    check(realApi, realApi->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &rawMemory));
    std::unique_ptr<OrtMemoryInfo, decltype(realApi->ReleaseMemoryInfo)> memory(rawMemory, realApi->ReleaseMemoryInfo);
    const bool kleidi = probeKleidi(realApi, rawMemory);
    OrtApi fake = *realApi;
    fake.Run = unknownResult;
    rejects([&] { probeKleidi(&fake, rawMemory); }, "Unsupported ORT SSRU quantization arithmetic");
    fake = *realApi;
    fake.CreateSessionFromArray = failedSession;
    rejects([&] { probeKleidi(&fake, rawMemory); }, "probe creation failed");
    fake.KernelInfo_GetConfigEntries = disabledConfig;
    for (const char* key : {"mlas.disable_kleidiai", "session.disable_prepacking"}) {
      disabledKey = key;
      assert(!usesKleidi(&fake, nullptr, rawMemory));
    }
    std::cout << "PASS: quantization probe (KleidiAI=" << kleidi << "), disabled configs and failure rejection\n";
  }
  dlclose(library);
}
