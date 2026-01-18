#include "ssru_op.h"
#include "../ssru/ssru.h"
#ifdef MTRAN_SSRU_PACKED
#include "../ssru/packed.h"
#endif
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

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
#ifdef MTRAN_SSRU_PACKED
  OrtOp* dql = nullptr;
#endif
  OrtOp* mm = nullptr;
  OrtOp *sigmoidOp = nullptr, *normOp = nullptr, *mulOp = nullptr, *addOp = nullptr, *sinOp=nullptr, *softmaxOp=nullptr, *qkOp=nullptr;
  std::unique_ptr<mtran::ssru::Decoder> decoder;
  std::unordered_map<std::string, std::unique_ptr<Quantized>> matrices;
  size_t inputCount = 0;

  explicit Kernel(const OrtApi* a) : api(a) {}
  ~Kernel() {
    decoder.reset(); matrices.clear();
    if (dqmm) api->ReleaseOp(dqmm);
    if (dqmmBias) api->ReleaseOp(dqmmBias);
#ifdef MTRAN_SSRU_PACKED
    if (dql) api->ReleaseOp(dql);
#endif
    if (mm) api->ReleaseOp(mm);
    for (auto* op : {sigmoidOp,normOp,mulOp,addOp,sinOp,softmaxOp,qkOp}) if(op) api->ReleaseOp(op);
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
    const std::regex linearName(R"(^decoder_l[0-9]+_(rnn_Wf?|context_W[qo]|ffn_W[0-9]+)$)");
#ifdef MTRAN_SSRU_PACKED
    const bool usePacked = mtran::ssru::packed::backend() != nullptr;
    bool hasPacked = false;
#endif
    std::vector<mtran::ssru::NamedTensor> weights;
    weights.reserve(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
      int constant = 0; const OrtValue* value = nullptr;
      check(api, api->KernelInfoGetConstantInput_tensor(info, firstWeight + i, &constant, &value));
      if (!constant || !value) throw std::runtime_error("SSRU weights must be immutable initializers");
      auto view = floatView(api, value);
      weights.push_back({names[i], view});
      if (!std::regex_match(names[i], linearName)) continue;
      if (view.shape.size() != 2) throw std::runtime_error("Invalid SSRU matrix rank");
      if (view.shape[0] <= 0 || view.shape[1] <= 0) throw std::runtime_error("Invalid SSRU matrix shape");
      auto q = std::make_unique<Quantized>(api);
      q->rows = static_cast<size_t>(view.shape[0]); q->cols = static_cast<size_t>(view.shape[1]);
      if (q->rows > SIZE_MAX / q->cols || view.size != q->rows * q->cols) throw std::runtime_error("SSRU matrix size mismatch");
#ifdef MTRAN_SSRU_PACKED
      if (usePacked && q->rows % 32 == 0 && q->cols % 8 == 0 && q->rows <= size_t(INT32_MAX) / (255 * 128)) {
        q->packed = std::make_unique<const mtran::ssru::packed::Weight>(view.data, q->rows, q->cols);
        hasPacked = true;
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
    const char* namesDQ[] = {"T1", "T2"};
    ONNXTensorElementDataType typesDQ[] = {ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8};
    // Standalone ORT operators do not prepack constants. Benchmark this backend
    // against the original graph before enabling it as an acceleration path.
    check(api, api->CreateOp(info, "DynamicQuantizeMatMul", "com.microsoft", 1, namesDQ, typesDQ, 2, nullptr, 0, 4, 1, &dqmm));
#if defined(__linux__) && defined(__aarch64__)
    check(api, api->CreateOp(info, "DynamicQuantizeMatMul", "com.microsoft", 1, namesDQ, typesDQ, 2, nullptr, 0, 5, 1, &dqmmBias));
#endif
#ifdef MTRAN_SSRU_PACKED
    if (hasPacked) {
      const char* namesDQL[] = {"T2"};
      ONNXTensorElementDataType typesDQL[] = {ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8};
      check(api, api->CreateOp(info, "DynamicQuantizeLinear", "", 11, namesDQL, typesDQL, 1, nullptr, 0, 1, 3, &dql));
    }
#endif
    const char* namesMM[] = {"T"}; ONNXTensorElementDataType typesMM[] = {ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT};
    check(api, api->CreateOp(info, "MatMul", "", 13, namesMM, typesMM, 1, nullptr, 0, 2, 1, &mm));
    check(api, api->CreateOp(info, "Sigmoid", "", 6, namesMM, typesMM, 1, nullptr, 0, 1, 1, &sigmoidOp));
    check(api, api->CreateOp(info, "Mul", "", 7, namesMM, typesMM, 1, nullptr, 0, 2, 1, &mulOp));
    check(api, api->CreateOp(info, "Add", "", 7, namesMM, typesMM, 1, nullptr, 0, 2, 1, &addOp));
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
  struct Invocation {
    Kernel* kernel; OrtKernelContext* context;
    std::vector<float> alignA, alignB, scaled, probs;
#ifdef MTRAN_SSRU_PACKED
    std::unique_ptr<mtran::ssru::packed::Workspace> workspace;
#endif
  };
  static void linear(void* raw, const mtran::ssru::Matrix& weight, const float* input, size_t rows, float* output) {
    linearImpl(raw, weight, input, rows, output, nullptr);
  }
  static void linearBias(void* raw, const mtran::ssru::Matrix& weight, const float* input,
                         size_t rows, const float* bias, bool separate, float* output) {
    linearImpl(raw, weight, input, rows, output, separate ? nullptr : bias);
    if (separate) for (size_t i = 0; i < rows * weight.cols; ++i) output[i] += bias[i % weight.cols];
  }
  static void linearImpl(void* raw, const mtran::ssru::Matrix& weight, const float* input,
                         size_t rows, float* output, const float* bias) {
    auto& invocation = *static_cast<Invocation*>(raw); auto& k = *invocation.kernel;
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
      const auto found = k.matrices.find(weight.name);
      if (found == k.matrices.end()) throw std::runtime_error("Missing quantized SSRU matrix: " + weight.name);
      const auto& q = *found->second;
      if (q.rows != weight.rows || q.cols != weight.cols) throw std::runtime_error("SSRU matrix dimension mismatch");
#ifdef MTRAN_SSRU_PACKED
      if (q.packed) {
        if (!invocation.workspace) invocation.workspace = std::make_unique<mtran::ssru::packed::Workspace>();
        auto* activation = invocation.workspace->resize(rows * weight.rows);
        float scale = 0; uint8_t zero = 0;
        Value quantized(k.api), scaleTensor(k.api), zeroTensor(k.api);
        k.wrap(quantized, activation, rows * weight.rows, aShape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8);
        // ORT DQL scale and zero point are rank-0 outputs [], never [1].
        k.wrap(scaleTensor, &scale, sizeof(scale), nullptr, 0, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
        k.wrap(zeroTensor, &zero, sizeof(zero), nullptr, 0, ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8);
        const OrtValue* inputs[] = {a.value};
        OrtValue* outputs[] = {quantized.value, scaleTensor.value, zeroTensor.value};
        check(k.api, k.api->InvokeOp(invocation.context, k.dql, inputs, 1, outputs, 3));
        q.packed->multiply(activation, rows, scale, zero, output, bias);
        return;
      }
#endif
      Value y(k.api);
      k.wrap(y, output, rows * weight.cols * sizeof(float), yShape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
      OrtValue* outputs[] = {y.value};
      Value b(k.api);
      const int64_t biasShape[] = {static_cast<int64_t>(weight.cols)};
      if (bias) k.wrap(b, const_cast<float*>(bias), weight.cols * sizeof(float), biasShape, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
      const OrtValue* inputs[] = {a.value, q.tensor.value, q.scaleTensor.value, q.zeroTensor.value, b.value};
      check(k.api, k.api->InvokeOp(invocation.context, bias ? k.dqmmBias : k.dqmm, inputs, bias ? 5 : 4, outputs, 1));
    }
  }
  static void sigmoidAligned(void* raw, float* x, size_t dim) {
    auto& v=*static_cast<Invocation*>(raw); auto& k=*v.kernel;
    v.alignA.resize(dim);
    const int64_t dims[]={1,1,1,static_cast<int64_t>(dim)};
    Value a(k.api),y(k.api);
    k.wrap(a,x,dim*4,dims,4,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    k.wrap(y,v.alignA.data(),dim*4,dims,4,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    const OrtValue* inputs[]={a.value}; OrtValue* outputs[]={y.value};
    check(k.api,k.api->InvokeOp(v.context,k.sigmoidOp,inputs,1,outputs,1));
    std::copy_n(v.alignA.data(),dim,x);
  }
  static void normalizeAligned(void* raw, float* x, size_t dim, const float* gamma, const float* beta) {
    auto& v=*static_cast<Invocation*>(raw); auto& k=*v.kernel;
    v.alignA.resize(dim);v.alignB.resize(dim);
    const int64_t dims[]={1,1,static_cast<int64_t>(dim)},vector[]={static_cast<int64_t>(dim)},oneDim[]={1};
    float one=1,zero=0;
    Value a(k.api),n(k.api),m(k.api),y(k.api),scale(k.api),bias(k.api),g(k.api),b(k.api);
    k.wrap(a,x,dim*4,dims,3,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    k.wrap(n,v.alignA.data(),dim*4,dims,3,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    k.wrap(m,v.alignB.data(),dim*4,dims,3,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    k.wrap(y,x,dim*4,dims,3,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    k.wrap(scale,&one,4,oneDim,1,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    k.wrap(bias,&zero,4,oneDim,1,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    k.wrap(g,const_cast<float*>(gamma),dim*4,vector,1,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    k.wrap(b,const_cast<float*>(beta),dim*4,vector,1,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    const OrtValue* ni[]={a.value,scale.value,bias.value};OrtValue* no[]={n.value};
    check(k.api,k.api->InvokeOp(v.context,k.normOp,ni,3,no,1));
    const OrtValue* mi[]={n.value,g.value};OrtValue* mo[]={m.value};
    check(k.api,k.api->InvokeOp(v.context,k.mulOp,mi,2,mo,1));
    const OrtValue* ai[]={m.value,b.value};OrtValue* ao[]={y.value};
    check(k.api,k.api->InvokeOp(v.context,k.addOp,ai,2,ao,1));
  }
  static void sinAligned(void* raw,float* x,size_t dim) {
    auto& v=*static_cast<Invocation*>(raw);auto& k=*v.kernel;
    v.alignA.resize(dim);const int64_t dims[]={static_cast<int64_t>(dim)};
    Value a(k.api),y(k.api);
    k.wrap(a,x,dim*4,dims,1,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    k.wrap(y,v.alignA.data(),dim*4,dims,1,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    const OrtValue* inputs[]={a.value};OrtValue* outputs[]={y.value};
    check(k.api,k.api->InvokeOp(v.context,k.sinOp,inputs,1,outputs,1));
    std::copy_n(v.alignA.data(),dim,x);
  }
  static void attentionAligned(void* raw,const float* query,const float* keys,const float* values,size_t heads,size_t width,size_t length,float* output){
    auto& v=*static_cast<Invocation*>(raw);auto& k=*v.kernel;
    const size_t count=heads*length;
    v.scaled.resize(count);v.probs.resize(count);
    const int64_t qs[]={1,int64_t(heads),1,int64_t(width)},ks[]={1,int64_t(heads),int64_t(width),int64_t(length)},vs[]={1,int64_t(heads),int64_t(length),int64_t(width)},ss[]={1,int64_t(heads),1,int64_t(length)};
    Value q(k.api),key(k.api),value(k.api),scaled(k.api),probs(k.api),out(k.api);
    k.wrap(q,const_cast<float*>(query),heads*width*4,qs,4,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    k.wrap(key,const_cast<float*>(keys),heads*width*length*4,ks,4,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    k.wrap(value,const_cast<float*>(values),heads*length*width*4,vs,4,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    k.wrap(scaled,v.scaled.data(),count*4,ss,4,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    k.wrap(probs,v.probs.data(),count*4,ss,4,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    k.wrap(out,output,heads*width*4,qs,4,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    const OrtValue* qi[]={q.value,key.value};OrtValue* qo[]={scaled.value};
    check(k.api,k.api->InvokeOp(v.context,k.qkOp,qi,2,qo,1));
    const OrtValue* si[]={scaled.value};OrtValue* so[]={probs.value};
    check(k.api,k.api->InvokeOp(v.context,k.softmaxOp,si,1,so,1));
    const OrtValue* vi[]={probs.value,value.value};OrtValue* vo[]={out.value};
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
        cs.type != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 || cs.dims.size() != 1 || cs.size == 0) throw std::runtime_error("Invalid SSRU token input shape");
    mtran::ssru::Request request;
    request.source_length = xs.size;
    request.eos_id = *static_cast<const int64_t*>(data(api, eos));
    request.candidate_ids = static_cast<const int64_t*>(data(api, candidates)); request.candidate_count = cs.size;
    const auto depth = decoder->config().depth;
    for (size_t i = 0; i < depth; ++i) { request.keys.push_back(floatView(api, get(3 + i))); request.values.push_back(floatView(api, get(3 + depth + i))); }
    mtran::ssru::Workspace workspace;
    Invocation invocation{};
    invocation.kernel = this; invocation.context = context;
    mtran::ssru::LinearBackend backend{&invocation, linear};
#if defined(__linux__) && defined(__aarch64__)
    // ORT's Linux ARM64 runtime fuses quantized bias with FMA; folded l1 gate/Q stay separate.
    backend.matmul_bias = linearBias;
#endif
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
