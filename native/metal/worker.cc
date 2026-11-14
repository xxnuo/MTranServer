#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <mlx/mlx.h>
#include <numeric>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>
namespace mx = mlx::core;
using mx::array;
using Arrays = std::vector<array>;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
uint32_t u32(std::istream &in) {
  uint32_t n;
  require(bool(in.read(reinterpret_cast<char *>(&n), 4)), "Truncated input");
  return n;
}
void put(uint32_t n) { std::cout.write(reinterpret_cast<const char *>(&n), 4); }
std::vector<int> ints(std::istream &in, size_t n) {
  require(n <= 1 << 20, "Input vector too large");
  std::vector<int> v(n);
  require(bool(in.read(reinterpret_cast<char *>(v.data()), n * 4)),
          "Truncated vector");
  return v;
}
struct Row {
  std::vector<int> ids, candidates;
};
class Model {
  int dim, heads, enc, depth, ffn, vocab, eos, sourceEos;
  std::string emb, encemb;
  std::map<std::string, array> w;
  std::map<std::string, Arrays> q;
  Arrays projection;
  array frequency{0.0f}, offset{0.0f};
  std::function<Arrays(const Arrays &)> encoder, contexts, chunk;
  array quantized(const array &x, const Arrays &p) {
    return mx::quantized_matmul(x, p[0], p[1], p[2], true, 64, 8);
  }
  array linear(const array &x, const std::string &p,
               const std::string &bias = "") {
    auto y = q.contains(p) ? quantized(x, q.at(p)) : mx::matmul(x, w.at(p));
    return bias.empty() ? y : y + w.at(bias);
  }
  array norm(const array &x, const std::string &p) {
    return mx::fast::layer_norm(x, mx::reshape(w.at(p + "_scale"), {-1}),
                                mx::reshape(w.at(p + "_bias"), {-1}), 1e-6f);
  }
  array head(const array &x) {
    return mx::transpose(
        mx::reshape(x, {x.shape(0), x.shape(1), heads, dim / heads}),
        {0, 2, 1, 3});
  }
  array position(const array &x, const array &pos) {
    return x * array(std::sqrt(float(dim))) + mx::sin(pos * frequency + offset);
  }
  array attend(const array &x, const std::string &p, const array &k,
               const array &v, const array &mask) {
    auto query = head(linear(x, p + "_Wq", p + "_bq"));
    auto y = mx::fast::scaled_dot_product_attention(
        query, k, v, 1 / std::sqrt(float(dim / heads)), "", mask);
    y = mx::reshape(mx::transpose(y, {0, 2, 1, 3}), {x.shape(0), -1, dim});
    return norm(linear(y, p + "_Wo", p + "_bo") + x, p + "_Wo_ln");
  }
  array feed(const array &x, const std::string &p) {
    auto y = x;
    for (int i = 1; i <= ffn; i++) {
      y = linear(y, p + "_W" + std::to_string(i), p + "_b" + std::to_string(i));
      if (i < ffn)
        y = mx::maximum(y, array(0.0f));
    }
    return norm(y + x, p + "_ffn_ln");
  }
  Arrays encode(const Arrays &a) {
    auto x =
        position(mx::take(w.at(encemb), a[0], 0),
                 mx::reshape(mx::arange(a[0].shape(1), mx::float32), {-1, 1}));
    for (int i = 1; i <= enc; i++) {
      std::string p = "encoder_l" + std::to_string(i) + "_self";
      auto k = head(linear(x, p + "_Wk", p + "_bk")),
           v = head(linear(x, p + "_Wv", p + "_bv"));
      x = feed(attend(x, p, k, v, a[1]),
               "encoder_l" + std::to_string(i) + "_ffn");
    }
    return {x};
  }
  Arrays context(const Arrays &a) {
    Arrays out;
    for (int i = 1; i <= depth; i++) {
      std::string p = "decoder_l" + std::to_string(i) + "_context";
      out.push_back(head(linear(a[0], p + "_Wk", p + "_bk")));
      out.push_back(head(linear(a[0], p + "_Wv", p + "_bv")));
    }
    return out;
  }
  Arrays fourSteps(const Arrays &a) {
    auto x = a[0], finished = a[5];
    Arrays states(a.begin() + 6, a.begin() + 6 + depth), tokens;
    const int projectionStart = 6 + depth * 3;
    Arrays p(a.begin() + projectionStart, a.end());
    for (int step = 0; step < 4; step++) {
      x = position(x, a[1] + array(float(step)));
      Arrays next;
      for (int i = 1; i <= depth; i++) {
        std::string base = "decoder_l" + std::to_string(i), r = base + "_rnn";
        auto f = mx::sigmoid(linear(x, r + "_Wf", r + "_bf"));
        auto cell = f * states[i - 1] + (array(1.0f) - f) * linear(x, r + "_W");
        next.push_back(cell);
        x = norm(mx::maximum(cell, array(0.0f)) + x, r + "_ffn_ln");
        x = feed(attend(x, base + "_context", a[6 + depth + (i - 1) * 2],
                        a[6 + depth + (i - 1) * 2 + 1], a[2]),
                 base + "_ffn");
      }
      auto logits = quantized(x, p) + a[3];
      auto token = mx::take(a[4], mx::argmax(logits, -1), 0);
      token = mx::where(finished, array(eos, mx::int32), token);
      finished =
          mx::logical_or(finished, mx::equal(token, array(eos, mx::int32)));
      tokens.push_back(token);
      states = std::move(next);
      x = mx::take(w.at(emb), token, 0);
    }
    Arrays out{mx::concatenate(tokens, 1), x, finished};
    out.insert(out.end(), states.begin(), states.end());
    return out;
  }

public:
  explicit Model(const std::string &filename) {
    std::ifstream in(filename, std::ios::binary);
    char magic[8];
    require(bool(in.read(magic, 8)) && std::string(magic, 8) == "MTRANMX2",
            "Invalid model magic");
    dim = u32(in);
    heads = u32(in);
    enc = u32(in);
    depth = u32(in);
    ffn = u32(in);
    vocab = u32(in);
    eos = u32(in);
    bool tied = u32(in);
    sourceEos = u32(in);
    auto count = u32(in);
    require(dim >= 4 && dim <= 4096 && dim % 64 == 0 && heads > 0 &&
                heads <= dim && dim % heads == 0 && enc > 0 && enc <= 64 &&
                depth > 0 && depth <= 64 && ffn > 0 && ffn <= 16 && vocab > 1 &&
                vocab <= 262144 && eos >= 0 && eos < vocab && count < 4096,
            "Unsupported model dimensions");
    emb = tied ? "Wemb" : "decoder_Wemb";
    encemb = tied ? emb : "encoder_Wemb";
    size_t totalValues = 0;
    for (uint32_t i = 0; i < count; i++) {
      auto nameLength = u32(in);
      require(nameLength > 0 && nameLength <= 256, "Invalid weight name");
      std::string name(nameLength, '\0');
      require(bool(in.read(name.data(), nameLength)), "Truncated weight name");
      auto rank = u32(in);
      require(rank > 0 && rank <= 4, "Invalid weight rank");
      mx::Shape shape;
      size_t length = 1;
      for (uint32_t j = 0; j < rank; j++) {
        auto d = u32(in);
        require(d > 0 && d <= 262144 && length <= (1 << 26) / d,
                "Invalid weight shape");
        shape.push_back(d);
        length *= d;
      }
      totalValues += length;
      require(totalValues <= size_t(1) << 28, "Model exceeds memory budget");
      std::vector<float> values(length);
      require(
          bool(in.read(reinterpret_cast<char *>(values.data()), length * 4)),
          "Truncated weight data");
      if (name == "decoder_ff_logit_out_b") {
        require(length == size_t(vocab), "Invalid vocabulary bias");
        values[1] = -INFINITY;
      }
      require(w.emplace(name, array(values.data(), shape, mx::float32)).second,
              "Duplicate weight");
    }
    require(in.peek() == std::char_traits<char>::eof(), "Trailing model data");
    for (auto &[name, value] : w)
      if (std::regex_search(name, std::regex("_W([0-9]+|[qkvfo])?$")))
        q.emplace(name, mx::quantize(mx::transpose(value), 64, 8));
    projection = mx::quantize(w.at(emb), 64, 8);
    std::vector<float> freq(dim), off(dim);
    for (int i = 0; i < dim; i++) {
      freq[i] =
          float(std::pow(1e-4, double(i % (dim / 2)) / double(dim / 2 - 1)));
      off[i] = float(i / (dim / 2)) * float(std::acos(-1) / 2);
    }
    frequency = array(freq.data(), {dim}, mx::float32);
    offset = array(off.data(), {dim}, mx::float32);
    Arrays weights;
    for (auto &[name, value] : w)
      weights.push_back(value);
    for (auto &[name, value] : q)
      weights.insert(weights.end(), value.begin(), value.end());
    weights.insert(weights.end(), projection.begin(), projection.end());
    weights.push_back(frequency);
    weights.push_back(offset);
    mx::eval(weights);
    for (auto &[name, value] : q)
      w.erase(name);
    mx::clear_cache();
    mx::set_cache_limit(128 << 20);
    encoder = mx::compile([this](const Arrays &a) { return encode(a); });
    contexts = mx::compile([this](const Arrays &a) { return context(a); });
    chunk = mx::compile([this](const Arrays &a) { return fourSteps(a); });
  }
  std::vector<std::vector<int>> translate(const std::vector<Row> &rows,
                                          bool fast) {
    int B = rows.size(), N = 0;
    const int sourceVocab = w.at(encemb).shape(0);
    require(B > 0 && B <= 32, "Invalid batch size");
    std::vector<int> candidates;
    for (auto &row : rows) {
      require(!row.ids.empty() && row.ids.size() <= 512,
              "Invalid input length");
      N = std::max(N, int(row.ids.size()));
      for (int id : row.ids)
        require(id >= 0 && id < sourceVocab, "Invalid source token");
      if (fast) {
        require(!row.candidates.empty() &&
                    row.candidates.size() <= size_t(vocab),
                "Invalid candidate count");
        for (int id : row.candidates)
          require(id >= 0 && id < vocab, "Invalid candidate token");
        candidates.insert(candidates.end(), row.candidates.begin(),
                          row.candidates.end());
      }
    }
    if (fast) {
      std::sort(candidates.begin(), candidates.end());
      candidates.erase(std::unique(candidates.begin(), candidates.end()),
                       candidates.end());
    } else {
      candidates.resize(vocab);
      std::iota(candidates.begin(), candidates.end(), 0);
    }
    int K = candidates.size();
    require(sourceEos >= 0 && sourceEos < sourceVocab, "Invalid source EOS");
    require(B * N <= 4096, "Padded token budget exceeded");
    std::vector<int> ids(B * N, sourceEos);
    std::vector<float> masks(B * N, -INFINITY),
        permitted(B * K, fast ? -INFINITY : 0.0f);
    std::vector<uint8_t> finished(B, 0);
    for (int i = 0; i < B; i++) {
      std::copy(rows[i].ids.begin(), rows[i].ids.end(), ids.begin() + i * N);
      std::fill(masks.begin() + i * N,
                masks.begin() + i * N + rows[i].ids.size(), 0);
      if (fast)
        for (int id : rows[i].candidates)
          permitted[i * K + (std::lower_bound(candidates.begin(),
                                              candidates.end(), id) -
                             candidates.begin())] = 0;
    }
    auto cand = array(candidates.data(), {K}, mx::int32),
         mask = array(masks.data(), {B, 1, 1, N}, mx::float32),
         source = array(ids.data(), {B, N}, mx::int32);
    auto ctx = contexts(encoder({source, mask}));
    Arrays proj;
    for (auto &p : projection)
      proj.push_back(mx::take(p, cand, 0));
    auto bias = mx::reshape(mx::take(w.at("decoder_ff_logit_out_b"), cand, 1),
                            {1, 1, K}) +
                array(permitted.data(), {B, 1, K}, mx::float32);
    Arrays states(depth, mx::zeros({B, 1, dim}, mx::float32));
    auto x = mx::zeros({B, 1, dim}, mx::float32);
    std::vector<std::vector<int>> result(B);
    for (int pos = 0; pos < N * 3 + 1; pos += 4) {
      Arrays args{x,    array(float(pos)),
                  mask, bias,
                  cand, array(finished.data(), {B, 1}, mx::bool_)};
      args.insert(args.end(), states.begin(), states.end());
      args.insert(args.end(), ctx.begin(), ctx.end());
      args.insert(args.end(), proj.begin(), proj.end());
      auto out = chunk(args);
      mx::eval(out);
      auto *data = out[0].data<int32_t>();
      for (int i = 0; i < B; i++)
        for (int j = 0; j < 4; j++) {
          if (finished[i])
            break;
          require(pos + j < int(rows[i].ids.size() * 3 + 1),
                  "Per-row generation limit exceeded");
          int token = data[i * 4 + j];
          if (token == eos)
            finished[i] = 1;
          else {
            require(token >= 0 && token < vocab, "Invalid generated token");
            result[i].push_back(token);
          }
        }
      if (std::all_of(finished.begin(), finished.end(),
                      [](auto b) { return b != 0; }))
        return result;
      x = out[1];
      states = Arrays(out.begin() + 3, out.end());
    }
    throw std::runtime_error("Generation limit exceeded");
  }
};
int main(int argc, char **argv) {
  try {
    require(argc == 3, "Expected model and metallib paths");
    mx::metal::set_metallib_path(argv[2]);
    mx::set_default_device(mx::Device::gpu);
    mx::set_memory_limit(size_t(2) << 30);
    Model model(argv[1]);
    put(0);
    std::cout.flush();
    while (std::cin.peek() != std::char_traits<char>::eof()) {
      auto batch = u32(std::cin), mode = u32(std::cin);
      require(batch > 0 && batch <= 32 && mode <= 1, "Invalid request");
      std::vector<Row> rows;
      for (uint32_t i = 0; i < batch; i++) {
        auto n = u32(std::cin);
        require(n > 0 && n <= 512, "Invalid input length");
        auto ids = ints(std::cin, n);
        auto k = u32(std::cin);
        require(k <= 262144, "Invalid shortlist length");
        rows.push_back({std::move(ids), ints(std::cin, k)});
      }
      auto out = model.translate(rows, mode != 0);
      put(0);
      put(out.size());
      for (auto &row : out) {
        put(row.size());
        std::cout.write(reinterpret_cast<const char *>(row.data()),
                        row.size() * 4);
      }
      std::cout.flush();
    }
  } catch (const std::exception &error) {
    std::string message = error.what();
    put(1);
    put(message.size());
    std::cout << message;
    std::cout.flush();
    std::cerr << message << '\n';
    return 1;
  }
}
