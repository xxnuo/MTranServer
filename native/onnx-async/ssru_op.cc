#include "ssru_op.h"
#include "../ssru/ssru.h"
#include "../ssru/kleidi.h"
#ifdef MTRAN_SSRU_PACKED
#include "../ssru/packed.h"
#endif
#include <algorithm>
#include <atomic>
#include <bit>
#if defined(__APPLE__) && defined(__aarch64__)
#include <sys/sysctl.h>
#endif
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#ifdef _MSC_VER
#pragma fp_contract(off)
#endif

namespace {
void check(const OrtApi* api, OrtStatus* status) {
  if (!status) return;
  std::unique_ptr<OrtStatus, decltype(api->ReleaseStatus)> owned(status, api->ReleaseStatus);
  std::string message = api->GetErrorMessage(status);
  throw std::runtime_error(message);
}
struct Value {
  const OrtApi* api;
  OrtValue* value = nullptr;
  explicit Value(const OrtApi* a) : api(a) {}
  Value(const Value&) = delete;
  Value& operator=(const Value&) = delete;
  Value(Value&& other) noexcept : api(other.api), value(other.value) { other.value = nullptr; }
  Value& operator=(Value&&) = delete;
  ~Value() { if (value) api->ReleaseValue(value); }
};
struct Shape {
  ONNXTensorElementDataType type;
  size_t size;
  std::vector<int64_t> dims;
};
Shape shape(const OrtApi* api, const OrtValue* value) {
  OrtTensorTypeAndShapeInfo* raw = nullptr;
  check(api, api->GetTensorTypeAndShape(value, &raw));
  std::unique_ptr<OrtTensorTypeAndShapeInfo, decltype(api->ReleaseTensorTypeAndShapeInfo)> info(raw, api->ReleaseTensorTypeAndShapeInfo);
  Shape result; size_t rank;
  check(api, api->GetTensorElementType(raw, &result.type));
  check(api, api->GetDimensionsCount(raw, &rank));
  result.dims.resize(rank);
  check(api, api->GetDimensions(raw, result.dims.data(), rank));
  check(api, api->GetTensorShapeElementCount(raw, &result.size));
  return result;
}
void* data(const OrtApi* api, const OrtValue* value) {
  void* raw = nullptr;
  check(api, api->GetTensorMutableData(const_cast<OrtValue*>(value), &raw));
  return raw;
}
mtran::ssru::TensorView floatView(const OrtApi* api, const OrtValue* value) {
  auto s = shape(api, value);
  if (s.type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) throw std::runtime_error("SSRU expects float weights and K/V");
  return {static_cast<const float*>(data(api, value)), s.size, std::move(s.dims)};
}
size_t attribute(const OrtApi* api, const OrtKernelInfo* info, const char* name) {
  int64_t value;
  check(api, api->KernelInfoGetAttribute_int64(info, name, &value));
  if (value < 1 || value > 65536) throw std::runtime_error("Invalid SSRU configuration");
  return static_cast<size_t>(value);
}
std::vector<std::string> weightNames(const OrtApi* api, const OrtKernelInfo* info) {
  OrtAllocator* allocator = nullptr;
  check(api, api->GetAllocatorWithDefaultOptions(&allocator));
  char** names = nullptr; size_t count = 0;
  check(api, api->KernelInfoGetAttributeArray_string(info, "weight_names", allocator, &names, &count));
  struct Cleanup {
    OrtAllocator* allocator; char** names; size_t count;
    ~Cleanup() { for (size_t i = 0; i < count; ++i) allocator->Free(allocator, names[i]); if (names) allocator->Free(allocator, names); }
  } cleanup{allocator, names, count};
  if (count == 0 || count > 4096) throw std::runtime_error("Invalid SSRU weight count");
  return {names, names + count};
}

// Constant int8 W=[1,1], scale=.1, bias=.125, X=[-3.3,4.4]. Only prepacked
// KleidiAI uses the reciprocal scale; a standalone InvokeOp cannot detect it.
[[maybe_unused]] bool probeKleidi(const OrtApi* api, OrtMemoryInfo* memory) {
  static constexpr char model[] =
    "\x08\x0a\x3a\xb0\x01\x0a\x38\x0a\x01\x58\x0a\x01\x57\x0a\x01\x53\x0a\x01\x5a\x0a\x01\x42\x12\x01"
    "\x59\x22\x15\x44\x79\x6e\x61\x6d\x69\x63\x51\x75\x61\x6e\x74\x69\x7a\x65\x4d\x61\x74\x4d\x75\x6c"
    "\x3a\x0d\x63\x6f\x6d\x2e\x6d\x69\x63\x72\x6f\x73\x6f\x66\x74\x12\x17\x73\x73\x72\x75\x5f\x71\x75"
    "\x61\x6e\x74\x69\x7a\x61\x74\x69\x6f\x6e\x5f\x70\x72\x6f\x62\x65\x2a\x0d\x08\x02\x08\x01\x10\x03"
    "\x42\x01\x57\x4a\x02\x01\x01\x2a\x0b\x10\x01\x42\x01\x53\x4a\x04\xcd\xcc\xcc\x3d\x2a\x08\x10\x03"
    "\x42\x01\x5a\x4a\x01\x00\x2a\x0b\x10\x01\x42\x01\x42\x4a\x04\x00\x00\x00\x3e\x5a\x13\x0a\x01\x58"
    "\x12\x0e\x0a\x0c\x08\x01\x12\x08\x0a\x02\x08\x01\x0a\x02\x08\x02\x62\x13\x0a\x01\x59\x12\x0e\x0a"
    "\x0c\x08\x01\x12\x08\x0a\x02\x08\x01\x0a\x02\x08\x01\x42\x04\x0a\x00\x10\x0d\x42\x11\x0a\x0d\x63"
    "\x6f\x6d\x2e\x6d\x69\x63\x72\x6f\x73\x6f\x66\x74\x10\x01";
  OrtEnv* rawEnv = nullptr;
  check(api, api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "ssru-quantization-probe", &rawEnv));
  std::unique_ptr<OrtEnv, decltype(api->ReleaseEnv)> env(rawEnv, api->ReleaseEnv);
  OrtSessionOptions* rawOptions = nullptr;
  check(api, api->CreateSessionOptions(&rawOptions));
  std::unique_ptr<OrtSessionOptions, decltype(api->ReleaseSessionOptions)> options(rawOptions, api->ReleaseSessionOptions);
  check(api, api->SetIntraOpNumThreads(rawOptions, 1));
  check(api, api->SetInterOpNumThreads(rawOptions, 1));
  check(api, api->AddSessionConfigEntry(rawOptions, "session.intra_op.allow_spinning", "0"));
  check(api, api->SetSessionGraphOptimizationLevel(rawOptions, ORT_ENABLE_ALL));
  OrtSession* rawSession = nullptr;
  check(api, api->CreateSessionFromArray(rawEnv, model, sizeof(model) - 1, rawOptions, &rawSession));
  std::unique_ptr<OrtSession, decltype(api->ReleaseSession)> session(rawSession, api->ReleaseSession);
  float x[] = {-3.3f, 4.4f};
  const int64_t dims[] = {1, 2};
  Value input(api), output(api);
  check(api, api->CreateTensorWithDataAsOrtValue(memory, x, sizeof(x), dims, 2,
      ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input.value));
  const char* inputs[] = {"X"}; const char* outputs[] = {"Y"};
  const OrtValue* values[] = {input.value};
  check(api, api->Run(rawSession, nullptr, inputs, values, 1, outputs, 1, &output.value));
  const auto resultShape = shape(api, output.value);
  if (resultShape.type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || resultShape.size != 1)
    throw std::runtime_error("Unexpected SSRU quantization probe shape");
  const auto bits = std::bit_cast<uint32_t>(*static_cast<const float*>(data(api, output.value)));
  if (bits == 0x3e72682cu) return true;
  if (bits == 0x3e72682bu) return false;
  throw std::runtime_error("Unsupported ORT SSRU quantization arithmetic");
}
bool usesKleidi(const OrtApi* api, const OrtKernelInfo* info, OrtMemoryInfo* memory) {
#if defined(__APPLE__) && defined(__aarch64__)
  bool sme = false;
  for (const char* key : {"hw.optional.arm.FEAT_SME", "hw.optional.arm.FEAT_SME2"}) {
    int supported = 0; size_t size = sizeof(supported);
    sme |= sysctlbyname(key, &supported, &size, nullptr, 0) == 0 && supported != 0;
  }
  if (!sme) return false;
  OrtKeyValuePairs* raw = nullptr;
  check(api, api->KernelInfo_GetConfigEntries(info, &raw));
  std::unique_ptr<OrtKeyValuePairs, decltype(api->ReleaseKeyValuePairs)> config(raw, api->ReleaseKeyValuePairs);
  for (const char* key : {"mlas.disable_kleidiai", "session.disable_prepacking"}) {
    const char* value = api->GetKeyValue(raw, key);
    if (value && std::strcmp(value, "1") == 0) return false;
  }
  // Failure rejects native initialization; the engine retains the original Fast graph.
  return probeKleidi(api, memory);
#else
  (void)api; (void)info; (void)memory;
  return false;
#endif
}

float roundEven(float x) {
  const float lo = std::floor(x), fraction = x - lo;
  return fraction < .5f ? lo : fraction > .5f ? lo + 1.f : std::fmod(lo, 2.f) == 0.f ? lo : lo + 1.f;
}
struct Quantized {
  std::vector<int8_t> bytes;
  float scale = 1;
  int8_t zero = 0;
  size_t rows, cols;
  Value tensor, scaleTensor, zeroTensor;
#ifdef MTRAN_SSRU_PACKED
  std::unique_ptr<const mtran::ssru::packed::Weight> packed;
#endif
  explicit Quantized(const OrtApi* api) : tensor(api), scaleTensor(api), zeroTensor(api) {}
};
struct Kernel {
  const OrtApi* api;
  OrtMemoryInfo* memory = nullptr;
  OrtOp* dqmm = nullptr;
  OrtOp* dqmmBias = nullptr;
  OrtOp* mm = nullptr;
  OrtOp *sigmoidOp = nullptr, *normOp = nullptr, *sinOp = nullptr, *softmaxOp = nullptr, *qkOp = nullptr;
  std::unique_ptr<mtran::ssru::Decoder> decoder;
#ifdef MTRAN_SSRU_PACKED
  std::unique_ptr<const mtran::ssru::packed::ShortlistTable> shortlist;
  std::unique_ptr<const mtran::ssru::packed::FullVocabQ4> fullVocab;
#endif
  std::unordered_map<std::string, std::unique_ptr<Quantized>> matrices;
  size_t inputCount = 0;
  bool kleidi = false;
  std::atomic<uint32_t> activeRuns{0};

  explicit Kernel(const OrtApi* a) : api(a) {}
  ~Kernel() {
    decoder.reset();
#ifdef MTRAN_SSRU_PACKED
    shortlist.reset();
    fullVocab.reset();
#endif
    matrices.clear();
    if (dqmm) api->ReleaseOp(dqmm);
    if (dqmmBias) api->ReleaseOp(dqmmBias);
    if (mm) api->ReleaseOp(mm);
    for (auto* op : {sigmoidOp, normOp, sinOp, softmaxOp, qkOp}) if (op) api->ReleaseOp(op);
    if (memory) api->ReleaseMemoryInfo(memory);
  }
  void wrap(Value& value, void* ptr, size_t bytes, const int64_t* dims, size_t rank, ONNXTensorElementDataType type) {
    check(api, api->CreateTensorWithDataAsOrtValue(memory, ptr, bytes, dims, rank, type, &value.value));
  }
  void initialize(const OrtKernelInfo* info) {
    const mtran::ssru::Config config{attribute(api, info, "dim"), attribute(api, info, "heads"), attribute(api, info, "depth"), attribute(api, info, "ffn_depth")};
    const auto names = weightNames(api, info);
    const size_t firstWeight = 3 + 2 * config.depth;
    check(api, api->KernelInfo_GetInputCount(info, &inputCount));
    if (inputCount != firstWeight + names.size()) throw std::runtime_error("SSRU input/weight count mismatch");
    check(api, api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory));
    kleidi = usesKleidi(api, info, memory);
    const std::regex linearName(R"(^decoder_l[0-9]+_(rnn_Wf?|context_W[qo]|ffn_W[0-9]+)$)");
#ifdef MTRAN_SSRU_PACKED
    const bool usePacked = mtran::ssru::packed::backend() != nullptr;
    const float* embData = nullptr;
    const float* outBias = nullptr;
    size_t vocabSize = 0;
#endif
    std::vector<mtran::ssru::NamedTensor> weights;
    weights.reserve(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
      int constant = 0; const OrtValue* value = nullptr;
      check(api, api->KernelInfoGetConstantInput_tensor(info, firstWeight + i, &constant, &value));
      if (!constant || !value) throw std::runtime_error("SSRU weights must be immutable initializers");
      auto view = floatView(api, value);
      weights.push_back({names[i], view});
#ifdef MTRAN_SSRU_PACKED
      if ((names[i] == "Wemb" || (names[i] == "decoder_Wemb" && !embData)) && view.shape.size() == 2 && view.shape[0] >= 2) {
        embData = view.data;
        vocabSize = static_cast<size_t>(view.shape[0]);
      } else if (names[i] == "decoder_ff_logit_out_b") {
        outBias = view.data;
      }
#endif
      if (!std::regex_match(names[i], linearName)) continue;
      if (view.shape.size() != 2) throw std::runtime_error("Invalid SSRU matrix rank");
      if (view.shape[0] <= 0 || view.shape[1] <= 0) throw std::runtime_error("Invalid SSRU matrix shape");
      auto q = std::make_unique<Quantized>(api);
      q->rows = static_cast<size_t>(view.shape[0]); q->cols = static_cast<size_t>(view.shape[1]);
      if (q->rows > SIZE_MAX / q->cols || view.size != q->rows * q->cols) throw std::runtime_error("SSRU matrix size mismatch");
#ifdef MTRAN_SSRU_PACKED
      if (usePacked && q->rows % 32 == 0 && q->cols % 8 == 0 && q->rows <= size_t(INT32_MAX) / (255 * 128)) {
        q->packed = std::make_unique<const mtran::ssru::packed::Weight>(view.data, q->rows, q->cols);
      } else
#endif
      {
        float maximum = 0;
        for (size_t j = 0; j < view.size; ++j) {
          if (!std::isfinite(view.data[j])) throw std::runtime_error("Non-finite SSRU matrix weight");
          maximum = std::max(maximum, std::abs(view.data[j]));
        }
        q->scale = (maximum * 2.f) / 254.f;
        if (q->scale < std::numeric_limits<float>::min()) q->scale = 1.f;
        if (!std::isfinite(q->scale)) throw std::runtime_error("Invalid SSRU quantization scale");
        q->bytes.resize(view.size);
        for (size_t j = 0; j < view.size; ++j) q->bytes[j] = static_cast<int8_t>(std::clamp(roundEven(view.data[j] / q->scale), -128.f, 127.f));
        wrap(q->tensor, q->bytes.data(), q->bytes.size(), view.shape.data(), view.shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8);
        wrap(q->scaleTensor, &q->scale, sizeof(float), nullptr, 0, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
        wrap(q->zeroTensor, &q->zero, 1, nullptr, 0, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8);
      }
      if (!matrices.emplace(names[i], std::move(q)).second) throw std::runtime_error("Duplicate SSRU weight");
    }
    decoder = std::make_unique<mtran::ssru::Decoder>(config, weights);
    int constCand = 0; const OrtValue* candVal = nullptr;
    const bool hasConstCand = api->KernelInfoGetConstantInput_tensor(info, 2, &constCand, &candVal) == nullptr && constCand && candVal;
    const bool isFullVocabGraph = hasConstCand && shape(api, candVal).size == 0;
#ifdef MTRAN_SSRU_PACKED
    if (isFullVocabGraph) {
      if (!usePacked || !embData || !outBias) throw std::runtime_error("Full-vocab SSRU requires packed backend");
      if (vocabSize < 9) throw std::runtime_error("Full-vocab SSRU requires vocabulary >= 9");
      fullVocab = std::make_unique<const mtran::ssru::packed::FullVocabQ4>(embData, outBias, vocabSize, config.dim, kleidi);
    } else if (usePacked && embData && outBias) {
      shortlist = std::make_unique<const mtran::ssru::packed::ShortlistTable>(embData, outBias, vocabSize, config.dim, kleidi);
    }
#else
    if (isFullVocabGraph) throw std::runtime_error("Full-vocab SSRU requires packed backend");
#endif
    const char* namesDQ[] = {"T1", "T2"};
    ONNXTensorElementDataType typesDQ[] = {ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8};
    // Standalone ORT operators do not prepack constants. Benchmark this backend
    // against the original graph before enabling it as an acceleration path.
    check(api, api->CreateOp(info, "DynamicQuantizeMatMul", "com.microsoft", 1, namesDQ, typesDQ, 2, nullptr, 0, 4, 1, &dqmm));
#if defined(__linux__) && defined(__aarch64__)
    check(api, api->CreateOp(info, "DynamicQuantizeMatMul", "com.microsoft", 1, namesDQ, typesDQ, 2, nullptr, 0, 5, 1, &dqmmBias));
#endif
    const char* namesMM[] = {"T"}; ONNXTensorElementDataType typesMM[] = {ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT};
    check(api, api->CreateOp(info, "MatMul", "", 13, namesMM, typesMM, 1, nullptr, 0, 2, 1, &mm));
    check(api, api->CreateOp(info, "Sigmoid", "", 6, namesMM, typesMM, 1, nullptr, 0, 1, 1, &sigmoidOp));
    float epsilon=1e-6f; OrtOpAttr* attr=nullptr;
    check(api,api->CreateOpAttr("epsilon", &epsilon, 1, ORT_OP_ATTR_FLOAT, &attr));
    const OrtOpAttr* attrs[]={attr};
    auto* status=api->CreateOp(info,"InstanceNormalization","",6,namesMM,typesMM,1,attrs,1,3,1,&normOp);
    api->ReleaseOpAttr(attr); check(api,status);
    check(api,api->CreateOp(info,"Sin","",7,namesMM,typesMM,1,nullptr,0,1,1,&sinOp));
    int64_t axis=3;attr=nullptr;
    check(api,api->CreateOpAttr("axis",&axis,1,ORT_OP_ATTR_INT,&attr));
    const OrtOpAttr* softAttrs[]={attr};
    status=api->CreateOp(info,"Softmax","",11,namesMM,typesMM,1,softAttrs,1,1,1,&softmaxOp);
    api->ReleaseOpAttr(attr);check(api,status);
    float alpha=static_cast<float>(1.0/std::sqrt(double(config.dim/config.heads)));attr=nullptr;
    check(api,api->CreateOpAttr("alpha",&alpha,1,ORT_OP_ATTR_FLOAT,&attr));
    const OrtOpAttr* qkAttrs[]={attr};
    status=api->CreateOp(info,"FusedMatMul","com.microsoft",1,namesMM,typesMM,1,qkAttrs,1,2,1,&qkOp);
    api->ReleaseOpAttr(attr);check(api,status);

  }
  struct SentenceTensors {
    const OrtApi* api;
    float oneVal = 1.0f, zeroVal = 0.0f;
    Value oneTensor, zeroTensor, sigIn, sigOut, normIn, normOut, sinIn, sinOut, attnQ, attnOut, attnScaled, attnProbs;
    const float* sigInPtr = nullptr;
    const float* normInPtr = nullptr;
    const float* sinInPtr = nullptr;
    const float* attnQPtr = nullptr;
    float* attnOutPtr = nullptr;
    std::vector<const float*> keyPtrs, valPtrs;
    std::vector<Value> keyVals, valVals;
    explicit SentenceTensors(const OrtApi* a)
        : api(a), oneTensor(a), zeroTensor(a), sigIn(a), sigOut(a), normIn(a), normOut(a), sinIn(a), sinOut(a),
          attnQ(a), attnOut(a), attnScaled(a), attnProbs(a) {}
    static void rewrap(Kernel& k, Value& v, void* ptr, size_t bytes, const int64_t* dims, size_t rank) {
      if (v.value) { k.api->ReleaseValue(v.value); v.value = nullptr; }
      k.wrap(v, ptr, bytes, dims, rank, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    }
  };
  struct Invocation {
    Kernel* kernel; OrtKernelContext* context;
    SentenceTensors* tensors = nullptr;
    std::vector<float> alignA, scaled, probs;
#ifdef MTRAN_SSRU_PACKED
    std::unique_ptr<mtran::ssru::packed::Workspace> workspace;
#endif
  };
  static void linear(void* raw, const mtran::ssru::Matrix& weight, const float* input, size_t rows, float* output) {
    linearImpl(raw, weight, input, rows, output, nullptr);
  }
  static void linearBias(void* raw, const mtran::ssru::Matrix& weight, const float* input,
                         size_t rows, const float* bias, bool separate, float* output) {
    auto& invocation = *static_cast<Invocation*>(raw);
    const bool kleidi = invocation.kernel->kleidi;
    const bool dynamic = kleidi && !separate && weight.name.find("_rnn_") == std::string::npos;
#if defined(__linux__) && defined(__aarch64__)
    const bool fuseFma = !separate;
#else
    const bool fuseFma = false;
#endif
    linearImpl(raw, weight, input, rows, output, fuseFma ? bias : nullptr, dynamic, fuseFma ? nullptr : bias);
  }
  static void linearPair(void* raw, const mtran::ssru::Matrix& w1, const float* b1, bool separate1, float* out1,
                         const mtran::ssru::Matrix& w2, float* out2, const float* input, size_t rows) {
#ifdef MTRAN_SSRU_PACKED
    auto& invocation = *static_cast<Invocation*>(raw); auto& k = *invocation.kernel;
    const auto f1 = k.matrices.find(w1.name), f2 = k.matrices.find(w2.name);
    if ((!k.kleidi || (w1.name.find("_rnn_") != std::string::npos && w2.name.find("_rnn_") != std::string::npos)) &&
        f1 != k.matrices.end() && f2 != k.matrices.end() && f1->second->packed && f2->second->packed &&
        w1.rows == w2.rows && f1->second->rows == w1.rows && f1->second->cols == w1.cols &&
        f2->second->rows == w2.rows && f2->second->cols == w2.cols) {
      if (!invocation.workspace) invocation.workspace = std::make_unique<mtran::ssru::packed::Workspace>();
      auto* activation = invocation.workspace->resize(rows * w1.rows);
      float scale = 0; uint8_t zero = 0;
      mtran::ssru::packed::quantizeLinearU8(input, rows, w1.rows, activation, scale, zero);
#if defined(__linux__) && defined(__aarch64__)
      const bool fuseFma = !separate1 && b1 != nullptr;
#else
      const bool fuseFma = false;
#endif
      f1->second->packed->multiply(activation, rows, scale, zero, out1, fuseFma ? b1 : nullptr, fuseFma ? nullptr : b1);
      f2->second->packed->multiply(activation, rows, scale, zero, out2, nullptr, nullptr);
      return;
    }
#endif
    if (b1) linearBias(raw, w1, input, rows, b1, separate1, out1);
    else linear(raw, w1, input, rows, out1);
    linear(raw, w2, input, rows, out2);
  }
  static void linearImpl(void* raw, const mtran::ssru::Matrix& weight, const float* input,
                         size_t rows, float* output, const float* bias, bool kleidi = false, const float* separateBias = nullptr) {
    if (kleidi && rows != 1) throw std::invalid_argument("SSRU KleidiAI expects one activation row");
    auto& invocation = *static_cast<Invocation*>(raw); auto& k = *invocation.kernel;
    if (weight.name != "shortlist_projection_T") {
      const auto found = k.matrices.find(weight.name);
      if (found == k.matrices.end()) throw std::runtime_error("Missing quantized SSRU matrix: " + weight.name);
      const auto& q = *found->second;
      if (q.rows != weight.rows || q.cols != weight.cols) throw std::runtime_error("SSRU matrix dimension mismatch");
#ifdef MTRAN_SSRU_PACKED
      if (q.packed) {
        if (!invocation.workspace) invocation.workspace = std::make_unique<mtran::ssru::packed::Workspace>();
        auto* activation = invocation.workspace->resize(rows * weight.rows);
        float scale = 0; uint8_t zero = 0;
        if (kleidi) scale = mtran::ssru::kleidi::quantize(input, weight.rows, activation, zero);
        else mtran::ssru::packed::quantizeLinearU8(input, rows, weight.rows, activation, scale, zero);
        q.packed->multiply(activation, rows, scale, zero, output, bias, separateBias);
        return;
      }
#endif
    }
    const int64_t aShape[] = {static_cast<int64_t>(rows), static_cast<int64_t>(weight.rows)};
    const int64_t yShape[] = {static_cast<int64_t>(rows), static_cast<int64_t>(weight.cols)};
    Value a(k.api);
    k.wrap(a, const_cast<float*>(input), rows * weight.rows * sizeof(float), aShape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    if (weight.name == "shortlist_projection_T") {
      const int64_t bShape[] = {static_cast<int64_t>(weight.rows), static_cast<int64_t>(weight.cols)};
      Value b(k.api), y(k.api);
      k.wrap(y, output, rows * weight.cols * sizeof(float), yShape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
      OrtValue* outputs[] = {y.value};
      k.wrap(b, const_cast<float*>(weight.data), weight.rows * weight.cols * sizeof(float), bShape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
      const OrtValue* inputs[] = {a.value, b.value};
      check(k.api, k.api->InvokeOp(invocation.context, k.mm, inputs, 2, outputs, 1));
    } else {
      const auto& q = *k.matrices.find(weight.name)->second;
      if (kleidi) {
        std::vector<uint8_t> activation(weight.rows);
        uint8_t zero = 0;
        const float scale = mtran::ssru::kleidi::quantize(input, weight.rows, activation.data(), zero) * q.scale;
        if (!std::isfinite(scale)) throw std::runtime_error("Invalid SSRU KleidiAI output scale");
        for (size_t col = 0; col < weight.cols; ++col) {
          int64_t sum = 0;
          for (size_t row = 0; row < weight.rows; ++row)
            sum += (int64_t(activation[row]) - zero) * q.bytes[row * weight.cols + col];
          if (sum < INT32_MIN || sum > INT32_MAX) throw std::runtime_error("SSRU KleidiAI accumulation overflow");
          output[col] = float(sum) * scale + (separateBias ? separateBias[col] : 0.0f);
        }
        return;
      }
      Value y(k.api);
      k.wrap(y, output, rows * weight.cols * sizeof(float), yShape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
      OrtValue* outputs[] = {y.value};
      Value b(k.api);
      const int64_t biasShape[] = {static_cast<int64_t>(weight.cols)};
      if (bias) k.wrap(b, const_cast<float*>(bias), weight.cols * sizeof(float), biasShape, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
      const OrtValue* inputs[] = {a.value, q.tensor.value, q.scaleTensor.value, q.zeroTensor.value, b.value};
      check(k.api, k.api->InvokeOp(invocation.context, bias ? k.dqmmBias : k.dqmm, inputs, bias ? 5 : 4, outputs, 1));
      if (separateBias) for (size_t i = 0; i < rows * weight.cols; ++i) output[i] += separateBias[i % weight.cols];
    }
  }
  static void sigmoidAligned(void* raw, float* x, size_t dim) {
    auto& v=*static_cast<Invocation*>(raw); auto& k=*v.kernel; auto& st=*v.tensors;
    if (st.sigInPtr != x) {
      const int64_t dims[]={1,1,1,static_cast<int64_t>(dim)};
      SentenceTensors::rewrap(k, st.sigIn, x, dim * 4, dims, 4);
      st.sigInPtr = x;
    }
    const OrtValue* inputs[]={st.sigIn.value}; OrtValue* outputs[]={st.sigOut.value};
    check(k.api,k.api->InvokeOp(v.context,k.sigmoidOp,inputs,1,outputs,1));
    std::copy_n(v.alignA.data(),dim,x);
  }
  static void normalizeAligned(void* raw, float* x, size_t dim, const float* gamma, const float* beta) {
    auto& v=*static_cast<Invocation*>(raw); auto& k=*v.kernel; auto& st=*v.tensors;
    if (st.normInPtr != x) {
      const int64_t dims[]={1,1,static_cast<int64_t>(dim)};
      SentenceTensors::rewrap(k, st.normIn, x, dim * 4, dims, 3);
      st.normInPtr = x;
    }
    const OrtValue* ni[]={st.normIn.value,st.oneTensor.value,st.zeroTensor.value};OrtValue* no[]={st.normOut.value};
    check(k.api,k.api->InvokeOp(v.context,k.normOp,ni,3,no,1));
    for (size_t i = 0; i < dim; ++i) x[i] = (v.alignA[i] * gamma[i]) + beta[i];
  }
  static void sinAligned(void* raw,float* x,size_t dim) {
    auto& v=*static_cast<Invocation*>(raw);auto& k=*v.kernel; auto& st=*v.tensors;
    if (st.sinInPtr != x) {
      const int64_t dims[]={static_cast<int64_t>(dim)};
      SentenceTensors::rewrap(k, st.sinIn, x, dim * 4, dims, 1);
      st.sinInPtr = x;
    }
    const OrtValue* inputs[]={st.sinIn.value};OrtValue* outputs[]={st.sinOut.value};
    check(k.api,k.api->InvokeOp(v.context,k.sinOp,inputs,1,outputs,1));
    std::copy_n(v.alignA.data(),dim,x);
  }
  static void attentionAligned(void* raw,const float* query,const float* keys,const float* values,size_t heads,size_t width,size_t length,float* output){
    auto& v=*static_cast<Invocation*>(raw);auto& k=*v.kernel; auto& st=*v.tensors;
    if (st.attnQPtr != query) {
      const int64_t qs[]={1,int64_t(heads),1,int64_t(width)};
      SentenceTensors::rewrap(k, st.attnQ, const_cast<float*>(query), heads * width * 4, qs, 4);
      st.attnQPtr = query;
    }
    if (st.attnOutPtr != output) {
      const int64_t qs[]={1,int64_t(heads),1,int64_t(width)};
      SentenceTensors::rewrap(k, st.attnOut, output, heads * width * 4, qs, 4);
      st.attnOutPtr = output;
    }
    const OrtValue* keyVal = nullptr;
    const OrtValue* valVal = nullptr;
    for (size_t i = 0; i < st.keyPtrs.size(); ++i) {
      if (st.keyPtrs[i] == keys && st.valPtrs[i] == values) {
        keyVal = st.keyVals[i].value;
        valVal = st.valVals[i].value;
        break;
      }
    }
    Value fallbackKey(k.api), fallbackVal(k.api);
    if (!keyVal) {
      const int64_t ks[]={1,int64_t(heads),int64_t(width),int64_t(length)}, vs[]={1,int64_t(heads),int64_t(length),int64_t(width)};
      k.wrap(fallbackKey, const_cast<float*>(keys), heads * width * length * 4, ks, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
      k.wrap(fallbackVal, const_cast<float*>(values), heads * length * width * 4, vs, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
      keyVal = fallbackKey.value; valVal = fallbackVal.value;
    }
    const OrtValue* qi[]={st.attnQ.value, keyVal};OrtValue* qo[]={st.attnScaled.value};
    check(k.api,k.api->InvokeOp(v.context,k.qkOp,qi,2,qo,1));
    const OrtValue* si[]={st.attnScaled.value};OrtValue* so[]={st.attnProbs.value};
    check(k.api,k.api->InvokeOp(v.context,k.softmaxOp,si,1,so,1));
    const OrtValue* vi[]={st.attnProbs.value, valVal};OrtValue* vo[]={st.attnOut.value};
    check(k.api,k.api->InvokeOp(v.context,k.mm,vi,2,vo,1));
  }
  void compute(OrtKernelContext* context) {
    size_t count;
    check(api, api->KernelContext_GetInputCount(context, &count));
    if (count != inputCount) throw std::runtime_error("SSRU runtime input count mismatch");
    const auto get = [&](size_t index) { const OrtValue* value = nullptr; check(api, api->KernelContext_GetInput(context, index, &value)); if (!value) throw std::runtime_error("Missing SSRU input"); return value; };
    const auto* x = get(0); const auto* eos = get(1); const auto* candidates = get(2);
    const auto xs = shape(api, x), es = shape(api, eos), cs = shape(api, candidates);
    if (xs.type != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 || xs.dims.size() != 1 || xs.size == 0 ||
        es.type != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 || es.dims != std::vector<int64_t>{1} ||
        cs.type != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 || cs.dims.size() != 1) throw std::runtime_error("Invalid SSRU token input shape");
    mtran::ssru::Request request;
    request.source_length = xs.size;
    request.eos_id = *static_cast<const int64_t*>(data(api, eos));
    request.candidate_ids = cs.size ? static_cast<const int64_t*>(data(api, candidates)) : nullptr; request.candidate_count = cs.size;
    const auto& cfg = decoder->config();
    const auto depth = cfg.depth;
    for (size_t i = 0; i < depth; ++i) { request.keys.push_back(floatView(api, get(3 + i))); request.values.push_back(floatView(api, get(3 + depth + i))); }
    struct ThreadScratch {
      mtran::ssru::Workspace workspace;
      Invocation invocation;
    };
    thread_local ThreadScratch scratch;
    auto& workspace = scratch.workspace;
    auto& invocation = scratch.invocation;

    const size_t dim = cfg.dim, heads = cfg.heads, width = dim / heads, length = request.source_length;
    if (heads > SIZE_MAX / length) throw std::runtime_error("SSRU attention score size overflow");
    const size_t attnCount = heads * length;
    invocation.alignA.resize(dim);
    invocation.scaled.resize(attnCount);
    invocation.probs.resize(attnCount);

    SentenceTensors st(api);
    const int64_t d4[] = {1, 1, 1, static_cast<int64_t>(dim)};
    const int64_t d3[] = {1, 1, static_cast<int64_t>(dim)};
    const int64_t d1[] = {static_cast<int64_t>(dim)}, oneDim[] = {1};
    wrap(st.oneTensor, &st.oneVal, sizeof(float), oneDim, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    wrap(st.zeroTensor, &st.zeroVal, sizeof(float), oneDim, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    const int64_t ss[] = {1, static_cast<int64_t>(heads), 1, static_cast<int64_t>(length)};
    const int64_t ks[] = {1, static_cast<int64_t>(heads), static_cast<int64_t>(width), static_cast<int64_t>(length)};
    const int64_t vs[] = {1, static_cast<int64_t>(heads), static_cast<int64_t>(length), static_cast<int64_t>(width)};
    wrap(st.sigOut, invocation.alignA.data(), dim * sizeof(float), d4, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    wrap(st.normOut, invocation.alignA.data(), dim * sizeof(float), d3, 3, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    wrap(st.sinOut, invocation.alignA.data(), dim * sizeof(float), d1, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    wrap(st.attnScaled, invocation.scaled.data(), attnCount * sizeof(float), ss, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    wrap(st.attnProbs, invocation.probs.data(), attnCount * sizeof(float), ss, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    st.keyPtrs.reserve(depth); st.valPtrs.reserve(depth);
    st.keyVals.reserve(depth); st.valVals.reserve(depth);
    const size_t kvBytes = dim * length * sizeof(float);
    for (size_t i = 0; i < depth; ++i) {
      if (request.keys[i].size == dim * length && request.values[i].size == dim * length &&
          request.keys[i].data && request.values[i].data) {
        st.keyPtrs.push_back(request.keys[i].data);
        st.valPtrs.push_back(request.values[i].data);
        st.keyVals.emplace_back(api);
        st.valVals.emplace_back(api);
        wrap(st.keyVals.back(), const_cast<float*>(request.keys[i].data), kvBytes, ks, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
        wrap(st.valVals.back(), const_cast<float*>(request.values[i].data), kvBytes, vs, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
      }
    }

    activeRuns.fetch_add(1, std::memory_order_relaxed);
    invocation.kernel = this; invocation.context = context; invocation.tensors = &st;
    struct ResetGuard {
      Invocation& inv;
      std::atomic<uint32_t>& active;
      ~ResetGuard() {
        active.fetch_sub(1, std::memory_order_relaxed);
        inv.kernel = nullptr; inv.context = nullptr; inv.tensors = nullptr;
      }
    } guard{invocation, activeRuns};
    mtran::ssru::LinearBackend backend{&invocation, linear};
#ifdef MTRAN_SSRU_PACKED
    if (cs.size > 0 && shortlist) {
      backend.argmax_shortlist = [](void* raw, const float* x_vec, size_t, const float*, const float*,
                                    const int64_t* cids, size_t ccount) {
        auto& inv = *static_cast<Invocation*>(raw);
        if (!inv.workspace) inv.workspace = std::make_unique<mtran::ssru::packed::Workspace>();
        return inv.kernel->shortlist->argmax(x_vec, cids, ccount, *inv.workspace, true);
      };
    } else if (cs.size == 0 && fullVocab) {
      backend.argmax_full = [](void* raw, const float* x_vec, size_t) {
        auto& inv = *static_cast<Invocation*>(raw);
        if (!inv.workspace) inv.workspace = std::make_unique<mtran::ssru::packed::Workspace>();
        return inv.kernel->fullVocab->argmax(x_vec, *inv.workspace, inv.kernel->activeRuns.load(std::memory_order_relaxed) <= 1);
      };
    }
#endif
    backend.matmul_bias = linearBias;
    backend.matmul_pair = linearPair;
    decoder->decode(request, backend, workspace,
                    {&invocation, normalizeAligned, sigmoidAligned, sinAligned, attentionAligned});
    const auto& tokens = workspace.tokens();
    const int64_t dims[] = {static_cast<int64_t>(tokens.size())}; OrtValue* result = nullptr;
    check(api, api->KernelContext_GetOutput(context, 0, dims, 1, &result));
    std::memcpy(data(api, result), tokens.data(), tokens.size() * sizeof(int64_t));
  }
};

const OrtCustomOp& operation() {
  static const OrtCustomOp op = [] {
    OrtCustomOp o{}; o.version = ORT_API_VERSION;
    o.GetName = [](const OrtCustomOp*) { return "SsruDecode"; };
    o.GetExecutionProviderType = [](const OrtCustomOp*) { return "CPUExecutionProvider"; };
    o.GetInputTypeCount = [](const OrtCustomOp*) -> size_t { return 4; };
    o.GetInputType = [](const OrtCustomOp*, size_t i) { return i < 3 ? ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 : ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT; };
    o.GetOutputTypeCount = [](const OrtCustomOp*) -> size_t { return 1; };
    o.GetOutputType = [](const OrtCustomOp*, size_t) { return ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64; };
    o.GetInputCharacteristic = [](const OrtCustomOp*, size_t i) { return i == 3 ? INPUT_OUTPUT_VARIADIC : INPUT_OUTPUT_REQUIRED; };
    o.GetOutputCharacteristic = [](const OrtCustomOp*, size_t) { return INPUT_OUTPUT_REQUIRED; };
    o.GetInputMemoryType = [](const OrtCustomOp*, size_t) { return OrtMemTypeDefault; };
    o.GetVariadicInputMinArity = [](const OrtCustomOp*) { return 1; };
    o.GetVariadicInputHomogeneity = [](const OrtCustomOp*) { return 1; };
    o.GetStartVersion = [](const OrtCustomOp*) { return 1; };
    o.GetEndVersion = [](const OrtCustomOp*) { return 1; };
    o.CreateKernelV2 = [](const OrtCustomOp*, const OrtApi* api, const OrtKernelInfo* info, void** output) -> OrtStatus* {
      try { auto kernel = std::make_unique<Kernel>(api); kernel->initialize(info); *output = kernel.release(); return nullptr; }
      catch (const std::exception& e) { return api->CreateStatus(ORT_FAIL, e.what()); }
      catch (...) { return api->CreateStatus(ORT_FAIL, "SSRU initialization failed"); }
    };
    o.KernelComputeV2 = [](void* raw, OrtKernelContext* context) -> OrtStatus* {
      auto* kernel = static_cast<Kernel*>(raw);
      try { kernel->compute(context); return nullptr; }
      catch (const std::exception& e) { return kernel->api->CreateStatus(ORT_FAIL, e.what()); }
      catch (...) { return kernel->api->CreateStatus(ORT_FAIL, "SSRU execution failed"); }
    };
    o.KernelDestroy = [](void* raw) { delete static_cast<Kernel*>(raw); };
    return o;
  }();
  return op;
}
}  // namespace

const char* ssruBackend() noexcept {
#ifdef MTRAN_SSRU_PACKED
  if (const char* backend = mtran::ssru::packed::backend()) return backend;
#endif
  return "invokeop";
}

OrtCustomOpDomain* createSsruDomain(const OrtApi* api) {
  OrtCustomOpDomain* domain = nullptr;
  check(api, api->CreateCustomOpDomain("com.mtran", &domain));
  const auto status = api->CustomOpDomain_Add(domain, &operation());
  if (status) { api->ReleaseCustomOpDomain(domain); check(api, status); }
  return domain;
}
