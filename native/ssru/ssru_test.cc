#include "ssru.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <future>
#include <iostream>
#include <limits>
#include <map>
#include <new>
#include <numeric>
#include <stdexcept>
#include <utility>

// Count core allocations only; each test thread has its own counter.
thread_local bool count_allocations = false;
thread_local std::size_t allocations = 0;
[[gnu::noinline]] void* operator new(std::size_t size) {
  if (count_allocations) ++allocations;
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
[[gnu::noinline]] void operator delete(void* p) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

namespace {
using namespace mtran::ssru;
using Vec = std::vector<double>;

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
  std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

struct Owned {
  std::vector<float> data;
  std::vector<std::int64_t> shape;
  TensorView view() const { return {data.data(), data.size(), shape}; }
};

struct Fixture {
  Config config;
  std::map<std::string, Owned> weights;
  std::vector<Owned> keys, values;
  std::vector<std::int64_t> candidates{8, 3, 1, 6, 2, 5, 10, 4, 9, 7};
  std::size_t source_length;

  Owned make(std::vector<std::int64_t> shape, float scale, float shift = 0) {
    std::size_t size = 1;
    for (auto d : shape) size *= static_cast<std::size_t>(d);
    Owned t{std::vector<float>(size), std::move(shape)};
    const auto seed = weights.size() + keys.size() + values.size() + 1;
    for (std::size_t i = 0; i < size; ++i)
      t.data[i] = shift + scale * static_cast<float>(std::sin((i + 1) * 1.31 + seed * 2.17));
    return t;
  }

  Fixture(Config c = {8, 2, 2, 2}, std::size_t length = 3, bool tied = true)
      : config(c), source_length(length) {
    const auto dim = static_cast<std::int64_t>(c.dim);
    const auto add = [&](const std::string& name, std::vector<std::int64_t> shape,
                          float scale = .13f, float shift = 0) {
      weights.emplace(name, make(std::move(shape), scale, shift));
    };
    const auto norm = [&](const std::string& name) {
      add(name + "_scale", {1, dim}, .07f, 1.0f);
      add(name + "_bias", {dim}, .03f);
    };
    add(tied ? "Wemb" : "decoder_Wemb", {11, dim}, .5f);
    add("decoder_ff_logit_out_b", {1, 11}, .1f);
    add("position_frequency", {dim}, .2f, .3f);
    add("position_offset", {dim}, .3f, .1f);
    for (std::size_t l = 1; l <= c.depth; ++l) {
      const auto p = "decoder_l" + std::to_string(l);
      add(p + "_rnn_Wf", {dim, dim});
      add(p + "_rnn_bf", {dim});
      add(p + "_rnn_W", {dim, dim});
      norm(p + "_rnn_ffn_ln");
      for (const auto* suffix : {"Wq", "Wo"}) add(p + "_context_" + suffix, {dim, dim});
      for (const auto* suffix : {"bq", "bo"}) add(p + "_context_" + suffix, {1, dim});
      norm(p + "_context_Wo_ln");
      std::int64_t width = dim;
      for (std::size_t j = 1; j <= c.ffn_depth; ++j) {
        const auto next = j == c.ffn_depth ? dim : dim + static_cast<std::int64_t>(j * 3);
        add(p + "_ffn_W" + std::to_string(j), {width, next});
        add(p + "_ffn_b" + std::to_string(j), {1, next});
        width = next;
      }
      norm(p + "_ffn_ffn_ln");
      keys.push_back(make({1, static_cast<std::int64_t>(c.heads),
        dim / static_cast<std::int64_t>(c.heads), static_cast<std::int64_t>(length)}, .5f));
      values.push_back(make({1, static_cast<std::int64_t>(c.heads),
        static_cast<std::int64_t>(length), dim / static_cast<std::int64_t>(c.heads)}, .8f));
    }
  }

  std::vector<NamedTensor> named() const {
    std::vector<NamedTensor> out;
    for (const auto& [name, t] : weights) out.push_back({name, t.view()});
    return out;
  }
  Request request() const {
    Request r{source_length, 0, candidates.data(), candidates.size(), {}, {}};
    for (const auto& t : keys) r.keys.push_back(t.view());
    for (const auto& t : values) r.values.push_back(t.view());
    return r;
  }
};

void scalar(void*, const Matrix& w, const float* input, std::size_t rows, float* output) {
  CHECK(input != output);
  for (std::size_t row = 0; row < rows; ++row)
    for (std::size_t col = 0; col < w.cols; ++col) {
      float sum = 0;
      for (std::size_t k = 0; k < w.rows; ++k)
        sum += input[row * w.rows + k] * w.data[k * w.cols + col];
      output[row * w.cols + col] = sum;
    }
}

// Independent double-precision reference; no production math helpers or buffers.
struct Stage { std::string name; Vec input; };
struct Reference {
  const Fixture& fixture;
  std::vector<Stage> stages;
  std::vector<std::int64_t> tokens;

  const Owned& weight(const std::string& name) const { return fixture.weights.at(name); }
  Vec linear(const Vec& x, const std::string& name, const std::string& bias = "") {
    stages.push_back({name, x});
    const auto& w = weight(name);
    Vec y(static_cast<std::size_t>(w.shape[1]));
    for (std::size_t j = 0; j < y.size(); ++j) {
      for (std::size_t k = 0; k < x.size(); ++k) y[j] += x[k] * w.data[k * y.size() + j];
      if (!bias.empty()) y[j] += weight(bias).data[j];
    }
    return y;
  }
  Vec norm(Vec x, const std::string& name) const {
    const double mean = std::accumulate(x.begin(), x.end(), 0.0) / x.size();
    double variance = 0;
    for (auto v : x) variance += (v - mean) * (v - mean);
    const double sd = std::sqrt(variance / x.size() + 1e-6);
    for (std::size_t j = 0; j < x.size(); ++j)
      x[j] = (x[j] - mean) / sd * weight(name + "_scale").data[j] + weight(name + "_bias").data[j];
    return x;
  }
  void run() {
    const auto& f = fixture;
    const auto& c = f.config;
    const auto& emb = weight(f.weights.count("Wemb") ? "Wemb" : "decoder_Wemb");
    std::vector<Vec> cells(c.depth, Vec(c.dim));
    for (std::size_t pos = 0; pos < 1 + 3 * f.source_length; ++pos) {
      Vec x(c.dim);
      for (std::size_t j = 0; j < c.dim; ++j) {
        const float phase = static_cast<float>(pos) * weight("position_frequency").data[j] + weight("position_offset").data[j];
        x[j] = std::sin(static_cast<double>(phase));
        if (pos) x[j] += emb.data[static_cast<std::size_t>(tokens.back()) * c.dim + j] *
          static_cast<float>(std::sqrt(static_cast<double>(c.dim)));
      }
      for (std::size_t l = 0; l < c.depth; ++l) {
        const auto p = "decoder_l" + std::to_string(l + 1);
        const auto gate = linear(x, p + "_rnn_Wf", p + "_rnn_bf");
        const auto update = linear(x, p + "_rnn_W");
        for (std::size_t j = 0; j < c.dim; ++j) {
          const auto g = 1 / (1 + std::exp(-gate[j]));
          cells[l][j] = g * cells[l][j] + (1 - g) * update[j];
          x[j] += std::max(0.0, cells[l][j]);
        }
        x = norm(x, p + "_rnn_ffn_ln");
        const auto q = linear(x, p + "_context_Wq", p + "_context_bq");
        const auto width = c.dim / c.heads;
        Vec context(c.dim);
        for (std::size_t h = 0; h < c.heads; ++h) {
          Vec scores(f.source_length);
          for (std::size_t s = 0; s < f.source_length; ++s) {
            for (std::size_t k = 0; k < width; ++k)
              scores[s] += q[h * width + k] * f.keys[l].data[(h * width + k) * f.source_length + s];
            scores[s] *= static_cast<float>(1 / std::sqrt(static_cast<double>(width)));
          }
          const double maximum = *std::max_element(scores.begin(), scores.end());
          double denominator = 0;
          for (auto& score : scores) { score = std::exp(score - maximum); denominator += score; }
          for (std::size_t k = 0; k < width; ++k)
            for (std::size_t s = 0; s < f.source_length; ++s)
              context[h * width + k] += scores[s] / denominator * f.values[l].data[(h * f.source_length + s) * width + k];
        }
        const auto attention = linear(context, p + "_context_Wo", p + "_context_bo");
        for (std::size_t j = 0; j < c.dim; ++j) x[j] += attention[j];
        x = norm(x, p + "_context_Wo_ln");
        Vec y = x;
        for (std::size_t j = 1; j <= c.ffn_depth; ++j) {
          y = linear(y, p + "_ffn_W" + std::to_string(j), p + "_ffn_b" + std::to_string(j));
          if (j < c.ffn_depth) for (auto& v : y) v = std::max(0.0, v);
        }
        for (std::size_t j = 0; j < c.dim; ++j) x[j] += y[j];
        x = norm(x, p + "_ffn_ffn_ln");
      }
      stages.push_back({"shortlist_projection_T", x});
      Vec logits(f.candidates.size());
      for (std::size_t j = 0; j < logits.size(); ++j) {
        const auto token = static_cast<std::size_t>(f.candidates[j]);
        for (std::size_t k = 0; k < c.dim; ++k) logits[j] += x[k] * emb.data[token * c.dim + k];
        logits[j] += weight("decoder_ff_logit_out_b").data[token];
        if (token == 1) logits[j] = -std::numeric_limits<double>::infinity();
      }
      const auto best = static_cast<std::size_t>(std::max_element(logits.begin(), logits.end()) - logits.begin());
      tokens.push_back(f.candidates[best]);
      if (tokens.back() == 0) break;
    }
  }
};

struct Trace {
  const std::vector<Stage>& expected;
  std::size_t next = 0;
  static void run(void* raw, const Matrix& w, const float* x, std::size_t rows, float* y) {
    auto& self = *static_cast<Trace*>(raw);
    CHECK(rows == 1 && self.next < self.expected.size());
    const auto& stage = self.expected[self.next++];
    CHECK(w.name == stage.name && w.rows == stage.input.size());
    for (std::size_t i = 0; i < w.rows; ++i) {
      if (std::abs(x[i] - stage.input[i]) > 2e-4 * (1 + std::abs(stage.input[i])))
        throw std::runtime_error("stage mismatch: " + w.name + " call=" + std::to_string(self.next - 1) +
          " element=" + std::to_string(i) + " actual=" + std::to_string(x[i]) + " expected=" + std::to_string(stage.input[i]));
    }
    scalar(nullptr, w, x, rows, y);
  }
};

void reference_cases() {
  for (Config c : {Config{4, 1, 1, 1}, Config{8, 2, 3, 2}, Config{12, 3, 2, 3}, Config{384, 8, 4, 2}}) {
    Fixture f(c, c.dim == 384 ? 1 : 3, c.dim != 12);
    Reference ref{f, {}, {}};
    ref.run();
    Decoder decoder(c, f.named());
    Workspace w;
    Trace trace{ref.stages};
    decoder.decode(f.request(), {&trace, Trace::run}, w);
    CHECK(trace.next == ref.stages.size());
    CHECK(w.tokens() == ref.tokens);
    CHECK(w.tokens().size() == 1 + 3 * f.source_length);
    CHECK(!w.ended_with_eos());
  }
  Fixture fallback({8, 4, 1, 2}, 2);
  for (std::size_t j = 0; j < fallback.config.dim; ++j) {
    const auto half = fallback.config.dim / 2;
    fallback.weights.at("position_frequency").data[j] = static_cast<float>(
      std::pow(1e-4, static_cast<double>(j % half) / (half - 1)));
    fallback.weights.at("position_offset").data[j] = static_cast<float>((j / half) * std::acos(-1.0) / 2);
  }
  auto weights = fallback.named();
  std::erase_if(weights, [](const auto& w) { return w.name == "position_frequency" || w.name == "position_offset"; });
  Reference ref{fallback, {}, {}};
  ref.run();
  Decoder decoder(fallback.config, weights);
  Trace trace{ref.stages};
  Workspace w;
  decoder.decode(fallback.request(), {&trace, Trace::run}, w);
  CHECK(w.tokens() == ref.tokens && trace.next == ref.stages.size());
}

struct Forced {
  std::size_t step = 0;
  std::size_t eos_step = 0;
  bool fail = false;
  static void run(void* raw, const Matrix& weight, const float* input, std::size_t rows, float* out) {
    auto& self = *static_cast<Forced*>(raw);
    if (self.fail && self.step) throw std::runtime_error("backend failure");
    scalar(nullptr, weight, input, rows, out);
    if (weight.name == "shortlist_projection_T") {
      std::fill_n(out, weight.cols, 0.0f);
      out[self.step++ == self.eos_step ? 1 : 0] = 100;
    }
  }
};

void eos_ties_and_reuse() {
  Fixture f;
  f.candidates = {8, 0, 1, 3};
  std::fill(f.weights.at("decoder_ff_logit_out_b").data.begin(), f.weights.at("decoder_ff_logit_out_b").data.end(), 0);
  Decoder decoder(f.config, f.named());
  Workspace w;
  for (std::size_t step : {0u, 2u, 9u, 100u}) {
    Forced forced{0, step, false};
    decoder.decode(f.request(), {&forced, Forced::run}, w);
    CHECK(w.tokens().size() == std::min(step + 1, std::size_t{10}));
    CHECK(w.ended_with_eos() == (step <= 9));
    if (step <= 9) CHECK(w.tokens().back() == 0);
  }
  Forced failing{0, 100, true};
  try { decoder.decode(f.request(), {&failing, Forced::run}, w); CHECK(false); }
  catch (const std::runtime_error&) { CHECK(w.tokens().empty() && !w.ended_with_eos()); }
  Forced good{0, 1, false};
  decoder.decode(f.request(), {&good, Forced::run}, w);
  CHECK(w.tokens() == std::vector<std::int64_t>({8, 0}));

  // Exact ties retain candidate order; token 1 is masked even with raw finite bias.
  for (auto& [name, t] : f.weights)
    if (name == "Wemb" || name == "decoder_ff_logit_out_b") std::fill(t.data.begin(), t.data.end(), 0);
  f.weights.at("decoder_ff_logit_out_b").data[1] = 1000;
  f.candidates = {1, 8, 3, 0, 8};
  Decoder tied(f.config, f.named());
  tied.decode(f.request(), {nullptr, scalar}, w);
  CHECK(w.tokens() == std::vector<std::int64_t>(10, 8));
  std::swap(f.candidates[1], f.candidates[2]);
  tied.decode(f.request(), {nullptr, scalar}, w);
  CHECK(w.tokens() == std::vector<std::int64_t>(10, 3));
  f.candidates = {0};
  tied.decode(f.request(), {nullptr, scalar}, w);
  CHECK(w.tokens() == std::vector<std::int64_t>({0}) && w.ended_with_eos());
}

template<class F> void rejects(F test) {
  bool rejected = false;
  try { test(); } catch (const std::exception&) { rejected = true; }
  CHECK(rejected);
}

void invalid_inputs() {
  const auto bad_weight = [](auto mutate) {
    Fixture f;
    auto weights = f.named();
    mutate(f, weights);
    rejects([&] { Decoder decoder(f.config, weights); });
  };
  bad_weight([](auto& f, auto&) { f.config.dim = 3; });
  bad_weight([](auto& f, auto&) { f.config.heads = 3; });
  bad_weight([](auto& f, auto&) { f.config.heads = 0; });
  bad_weight([](auto& f, auto&) { f.config.depth = 0; });
  bad_weight([](auto& f, auto&) { f.config.ffn_depth = 0; });
  bad_weight([](auto& f, auto&) { f.config.depth = std::numeric_limits<std::size_t>::max(); });
  bad_weight([](auto&, auto& ws) { ws.pop_back(); });
  bad_weight([](auto&, auto& ws) { ws.push_back(ws.front()); });
  bad_weight([](auto&, auto& ws) { ws.front().tensor.data = nullptr; });
  bad_weight([](auto&, auto& ws) { ws.front().tensor.size--; });
  bad_weight([](auto&, auto& ws) { ws.front().tensor.shape[0] = -1; });
  bad_weight([](auto&, auto& ws) {
    for (auto& w : ws) if (w.name == "decoder_l1_rnn_W") {
      w.tensor.shape = {4, 16};
    }
  });
  bad_weight([](auto&, auto& ws) { ws.front().tensor.shape = {INT64_MAX, INT64_MAX, INT64_MAX}; });
  bad_weight([](auto& f, auto&) { f.weights.at("decoder_l1_rnn_W").data[0] = NAN; });
  bad_weight([](auto& f, auto&) { f.weights.at("decoder_ff_logit_out_b").data[2] = -INFINITY; });

  Fixture f;
  Decoder decoder(f.config, f.named());
  Workspace w;
  const auto bad_request = [&](auto mutate) {
    auto r = f.request();
    mutate(r);
    rejects([&] { decoder.decode(r, {nullptr, scalar}, w); });
    CHECK(w.tokens().empty() && !w.ended_with_eos());
  };
  bad_request([](auto& r) { r.source_length = 0; });
  bad_request([](auto& r) { r.source_length = std::numeric_limits<std::size_t>::max(); });
  bad_request([](auto& r) { r.eos_id = -1; });
  bad_request([](auto& r) { r.eos_id = 11; });
  bad_request([](auto& r) { r.candidate_ids = nullptr; });
  bad_request([](auto& r) { r.candidate_count = 0; });
  bad_request([](auto& r) { r.keys.clear(); });
  bad_request([](auto& r) { r.values.pop_back(); });
  bad_request([](auto& r) { std::swap(r.keys[0].shape[2], r.keys[0].shape[3]); });
  bad_request([](auto& r) { r.values[0].size--; });
  f.candidates[0] = -1;
  bad_request([](auto&) {});
  f.candidates[0] = 11;
  bad_request([](auto&) {});
  f.candidates[0] = 8;
  f.keys[0].data[0] = INFINITY;
  bad_request([](auto&) {});
  f.keys[0].data[0] = 0;
  f.values[0].data[0] = NAN;
  bad_request([](auto&) {});
  f.values[0].data[0] = 0;
  rejects([&] { decoder.decode(f.request(), {}, w); });
  rejects([&] { decoder.decode(f.request(), {nullptr, [](void*, const Matrix& m, const float*, std::size_t, float* y) {
    std::fill_n(y, m.cols, NAN);
  }}, w); });
  CHECK(w.tokens().empty());
  rejects([&] { decoder.decode(f.request(), {nullptr, [](void*, const Matrix& m, const float* x, std::size_t rows, float* y) {
    scalar(nullptr, m, x, rows, y);
    if (m.name == "decoder_l1_rnn_W")
      for (std::size_t j = 0; j < m.cols; ++j) y[j] = j % 2 ? 1e30f : 0;
  }}, w); });
  CHECK(w.tokens().empty());
  decoder.decode(f.request(), {nullptr, scalar}, w);
  CHECK(!w.tokens().empty());
}

void allocation_and_concurrency() {
  Fixture f;
  Decoder decoder(f.config, f.named());
  const auto request = f.request();
  Workspace w;
  decoder.decode(request, {nullptr, scalar}, w);
  const auto expected = w.tokens();
  // With buffers warm, no allocation is allowed inside the token loop.
  struct Counter {
    std::size_t previous = 0, calls = 0;
    static void run(void* raw, const Matrix& m, const float* x, std::size_t rows, float* y) {
      auto& c = *static_cast<Counter*>(raw);
      if (c.calls) CHECK(allocations == c.previous);
      c.previous = allocations;
      ++c.calls;
      scalar(nullptr, m, x, rows, y);
    }
  } counter;
  allocations = 0;
  count_allocations = true;
  try { decoder.decode(request, {&counter, Counter::run}, w); }
  catch (...) { count_allocations = false; throw; }
  count_allocations = false;
  CHECK(counter.calls == expected.size() * (f.config.depth * (4 + f.config.ffn_depth) + 1));
  CHECK(allocations == counter.previous);
  CHECK(w.tokens() == expected);
  Fixture shorter(f.config, 1);
  shorter.candidates = {3, 6, 1};
  const auto other_request = shorter.request();
  Workspace other;
  decoder.decode(other_request, {nullptr, scalar}, other);
  const auto other_expected = other.tokens();
  decoder.decode(other_request, {nullptr, scalar}, w);
  CHECK(w.tokens() == other_expected);
  decoder.decode(request, {nullptr, scalar}, w);
  CHECK(w.tokens() == expected);
  const auto weights_before = f.weights.at("Wemb").data;
  std::vector<std::future<void>> futures;
  for (int i = 0; i < 8; ++i) futures.push_back(std::async(std::launch::async, [&, i] {
    Workspace local;
    for (int repeat = 0; repeat < 10; ++repeat) {
      decoder.decode(i % 2 ? request : other_request, {nullptr, scalar}, local);
      CHECK(local.tokens() == (i % 2 ? expected : other_expected));
    }
  }));
  for (auto& future : futures) future.get();
  CHECK(weights_before == f.weights.at("Wemb").data);
}

struct CallbackMath {
  std::size_t norm_calls = 0, sigmoid_calls = 0, sin_calls = 0, attention_calls = 0;
  int fail_operation = -1;
  std::size_t fail_call = 0;
  bool nonfinite = false;

  void visit(int operation, std::size_t& calls, float* output) {
    ++calls;
    if (operation == fail_operation && calls == fail_call) {
      if (nonfinite) { output[0] = NAN; return; }
      throw std::runtime_error("math callback failure");
    }
  }
  static void norm(void* raw, float* x, std::size_t dim, const float* scale, const float* bias) {
    auto& self = *static_cast<CallbackMath*>(raw);
    float mean = 0, variance = 0;
    for (std::size_t j = 0; j < dim; ++j) mean += x[j];
    mean /= static_cast<float>(dim);
    for (std::size_t j = 0; j < dim; ++j) { const float delta = x[j] - mean; variance += delta * delta; }
    const float inverse = 1.0f / std::sqrt(variance / static_cast<float>(dim) + 1e-6f);
    for (std::size_t j = 0; j < dim; ++j) x[j] = ((x[j] - mean) * inverse) * scale[j] + bias[j];
    self.visit(0, self.norm_calls, x);
  }
  static void sigmoid(void* raw, float* x, std::size_t dim) {
    auto& self = *static_cast<CallbackMath*>(raw);
    for (std::size_t j = 0; j < dim; ++j) x[j] = 1.0f / (1.0f + std::exp(-x[j]));
    self.visit(1, self.sigmoid_calls, x);
  }
  static void sin(void* raw, float* x, std::size_t dim) {
    auto& self = *static_cast<CallbackMath*>(raw);
    for (std::size_t j = 0; j < dim; ++j) x[j] = std::sin(x[j]);
    self.visit(2, self.sin_calls, x);
  }
  static void attention(void* raw, const float* query, const float* keys, const float* values,
                        std::size_t heads, std::size_t width, std::size_t length, float* output) {
    auto& self = *static_cast<CallbackMath*>(raw);
    std::vector<float> scores(length);
    const float scale = static_cast<float>(1.0 / std::sqrt(static_cast<double>(width)));
    for (std::size_t h = 0; h < heads; ++h) {
      for (std::size_t s = 0; s < length; ++s) {
        float sum = 0;
        for (std::size_t k = 0; k < width; ++k) sum += query[h * width + k] * keys[(h * width + k) * length + s];
        scores[s] = sum * scale;
      }
      const auto maximum = *std::max_element(scores.begin(), scores.end());
      float total = 0;
      for (auto& score : scores) { score = std::exp(score - maximum); total += score; }
      for (auto& score : scores) score /= total;
      for (std::size_t k = 0; k < width; ++k) {
        float sum = 0;
        for (std::size_t s = 0; s < length; ++s) sum += scores[s] * values[(h * length + s) * width + k];
        output[h * width + k] = sum;
      }
    }
    self.visit(3, self.attention_calls, output);
  }
  MathBackend backend(unsigned mask = 15) {
    return {this, mask & 1 ? norm : nullptr, mask & 2 ? sigmoid : nullptr,
      mask & 4 ? sin : nullptr, mask & 8 ? attention : nullptr};
  }
};

struct BiasTrace {
  const Fixture& fixture;
  const std::vector<Stage>& expected;
  std::size_t next = 0, step = 0, plain_calls = 0, bias_calls = 0;
  std::vector<std::pair<std::size_t, std::string>> separate;
  int failure = -1;

  BiasTrace(const Fixture& f, const std::vector<Stage>& stages) : fixture(f), expected(stages) {}
  void multiply(const Matrix& weight, const float* input, std::size_t rows, float* output) {
    CHECK(rows == 1 && next < expected.size());
    const auto& stage = expected[next++];
    CHECK(weight.name == stage.name && weight.rows == stage.input.size());
    for (std::size_t i = 0; i < weight.rows; ++i) CHECK(input[i] == stage.input[i]);
    scalar(nullptr, weight, input, rows, output);
  }
  static void plain(void* raw, const Matrix& weight, const float* input, std::size_t rows, float* output) {
    auto& self = *static_cast<BiasTrace*>(raw);
    CHECK(weight.name.ends_with("_rnn_W") || weight.name == "shortlist_projection_T");
    self.multiply(weight, input, rows, output);
    ++self.plain_calls;
    if (weight.name == "shortlist_projection_T") ++self.step;
  }
  static void biased(void* raw, const Matrix& weight, const float* input, std::size_t rows,
                     const float* bias, bool separate_bias, float* output) {
    auto& self = *static_cast<BiasTrace*>(raw);
    const auto split = weight.name.rfind("_W");
    CHECK(split != std::string::npos);
    const auto& expected_bias = self.fixture.weights.at(weight.name.substr(0, split) + "_b" + weight.name.substr(split + 2));
    CHECK(bias == expected_bias.data.data() && expected_bias.data.size() == weight.cols);
    self.multiply(weight, input, rows, output);
    ++self.bias_calls;
    if (separate_bias) self.separate.emplace_back(self.step, weight.name);
    for (std::size_t i = 0; i < weight.cols; ++i) output[i] += bias[i];
    if (self.step == 2 && self.failure >= 0) {
      if (self.failure == 0) throw std::runtime_error("bias callback failure");
      output[0] = self.failure == 1 ? NAN : INFINITY;
    }
  }
  void complete(std::size_t tokens) const {
    CHECK(next == expected.size() && step == tokens);
    CHECK(plain_calls == tokens * (fixture.config.depth + 1));
    CHECK(bias_calls == tokens * fixture.config.depth * (3 + fixture.config.ffn_depth));
    const std::vector<std::pair<std::size_t, std::string>> folded{
      {0, "decoder_l1_rnn_Wf"}, {0, "decoder_l1_context_Wq"}};
    CHECK(separate == folded);
  }
};

void bias_callbacks() {
  for (const Config config : {Config{4, 1, 1, 1}, Config{12, 3, 2, 3}}) {
    Fixture f(config);
    Decoder decoder(config, f.named());
    const auto request = f.request();
    Workspace w;
    std::vector<Stage> stages;
    decoder.decode(request, {&stages, [](void* raw, const Matrix& weight, const float* input,
                                       std::size_t rows, float* output) {
      static_cast<std::vector<Stage>*>(raw)->push_back({weight.name, Vec(input, input + weight.rows)});
      scalar(nullptr, weight, input, rows, output);
    }}, w);
    const auto expected = w.tokens();
    CHECK(expected.size() == 1 + 3 * f.source_length);
    // Match every scalar input, so missing or duplicate bias cannot hide behind the same argmax.
    for (unsigned mask : {0u, 1u, 2u, 4u, 8u, 15u}) {
      BiasTrace trace(f, stages);
      CallbackMath math;
      decoder.decode(request, {&trace, BiasTrace::plain, BiasTrace::biased}, w, math.backend(mask));
      trace.complete(expected.size());
      CHECK(w.tokens() == expected && !w.ended_with_eos());
      CHECK(math.norm_calls == (mask & 1 ? expected.size() * config.depth * 3 : 0));
      CHECK(math.sigmoid_calls == (mask & 2 ? expected.size() * config.depth : 0));
      CHECK(math.sin_calls == (mask & 4 ? expected.size() : 0));
      CHECK(math.attention_calls == (mask & 8 ? expected.size() * config.depth : 0));
    }
    for (int failure : {0, 1, 2}) {
      BiasTrace failing(f, stages);
      failing.failure = failure;
      bool rejected = false;
      try { decoder.decode(request, {&failing, BiasTrace::plain, BiasTrace::biased}, w); }
      catch (const std::runtime_error& error) {
        rejected = true;
        CHECK(std::string(error.what()) == (failure == 0 ? "bias callback failure" : "SSRU nonfinite value: linear fused bias"));
      }
      CHECK(rejected && failing.step == 2 && w.tokens().empty() && !w.ended_with_eos());
      BiasTrace good(f, stages);
      decoder.decode(request, {&good, BiasTrace::plain, BiasTrace::biased}, w);
      good.complete(expected.size());
      CHECK(w.tokens() == expected);
      decoder.decode(request, {nullptr, scalar}, w);
      CHECK(w.tokens() == expected && !w.ended_with_eos());
    }
  }
}

void math_callbacks() {
  for (const Config config : {Config{4, 1, 1, 1}, Config{12, 3, 2, 3}}) {
    Fixture f(config);
    Decoder decoder(config, f.named());
    const auto request = f.request();
    Workspace w;
    decoder.decode(request, {nullptr, scalar}, w);
    const auto expected = w.tokens();
    Reference ref{f, {}, {}};
    ref.run();
    for (unsigned mask : {1u, 2u, 4u, 8u, 15u}) {
      CallbackMath math;
      Trace trace{ref.stages};
      decoder.decode(request, {&trace, Trace::run}, w, math.backend(mask));
      CHECK(w.tokens() == expected && trace.next == ref.stages.size());
      CHECK(math.norm_calls == (mask & 1 ? expected.size() * config.depth * 3 : 0));
      CHECK(math.sigmoid_calls == (mask & 2 ? expected.size() * config.depth : 0));
      CHECK(math.sin_calls == (mask & 4 ? expected.size() : 0));
      CHECK(math.attention_calls == (mask & 8 ? expected.size() * config.depth : 0));
    }
    for (int operation = 0; operation < 4; ++operation) {
      const std::size_t per_token[] = {config.depth * 3, config.depth, 1, config.depth};
      for (const bool nonfinite : {false, true}) {
        CallbackMath failing;
        failing.fail_operation = operation;
        failing.fail_call = per_token[operation] + 1;
        failing.nonfinite = nonfinite;
        rejects([&] { decoder.decode(request, {nullptr, scalar}, w, failing.backend()); });
        CHECK(w.tokens().empty() && !w.ended_with_eos());
        CallbackMath good;
        decoder.decode(request, {nullptr, scalar}, w, good.backend());
        CHECK(w.tokens() == expected);
      }
    }
  }
}

}  // namespace

int main() {
  try {
    reference_cases();
    eos_ties_and_reuse();
    invalid_inputs();
    allocation_and_concurrency();
    math_callbacks();
    bias_callbacks();
    std::cout << "PASS: scalar stage reference, EOS/ties, invalid inputs, failure recovery, allocations, concurrency, math callbacks, bias callbacks\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
