#include "ops.h"
#include "pcore_pool.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void checkNear(const float* got, const double* ref, std::size_t n, double tol, const char* label) {
  for (std::size_t i = 0; i < n; ++i) {
    const double diff = std::abs(static_cast<double>(got[i]) - ref[i]);
    if (!(diff <= tol)) {
      std::cerr << label << " mismatch at " << i << ": got=" << got[i]
                << " ref=" << ref[i] << " diff=" << diff << " tol=" << tol << "\n";
      std::abort();
    }
  }
}

void testPosTable() {
  for (std::size_t dim : {4, 13, 384}) {
    std::vector<float> freq(dim), off(dim), emb(dim);
    std::vector<std::int8_t> emb_q8(dim);
    for (std::size_t j = 0; j < dim; ++j) {
      freq[j] = std::pow(1e-4f, static_cast<float>(j % (dim / 2)) / std::max<std::size_t>(1, dim / 2 - 1));
      off[j] = static_cast<float>(j / (dim / 2)) * 1.5707963f;
      emb[j] = std::sin(static_cast<float>(j * 3 + 1)) * 0.5f;
      emb_q8[j] = static_cast<std::int8_t>(static_cast<int>((j * 17) % 201) - 100);
    }
    const auto pt = mtran::ssru::ops::PosTable::create(freq.data(), off.data(), dim, 64);
    const float scale = std::sqrt(static_cast<float>(dim));
    const float q_scale = 0.0125f;

    for (std::size_t pos : {std::size_t{0}, std::size_t{1}, std::size_t{63}, std::size_t{64}, std::size_t{99}}) {
      std::vector<float> out(dim);
      std::vector<double> ref(dim);

      pt.addScaledEmbed(nullptr, scale, pos, out.data());
      for (std::size_t j = 0; j < dim; ++j) {
        ref[j] = std::sin(static_cast<float>(pos) * freq[j] + off[j]);
      }
      checkNear(out.data(), ref.data(), dim, 1e-6, "pos_null");

      pt.addScaledEmbed(emb.data(), scale, pos, out.data());
      for (std::size_t j = 0; j < dim; ++j) {
        ref[j] = static_cast<double>(emb[j] * scale) + std::sin(static_cast<float>(pos) * freq[j] + off[j]);
      }
      checkNear(out.data(), ref.data(), dim, 1e-6, "pos_fp32");

      pt.addScaledEmbedInt8(emb_q8.data(), q_scale * scale, pos, out.data());
      for (std::size_t j = 0; j < dim; ++j) {
        ref[j] = static_cast<double>(static_cast<float>(emb_q8[j]) * (q_scale * scale)) +
                 std::sin(static_cast<float>(pos) * freq[j] + off[j]);
      }
      checkNear(out.data(), ref.data(), dim, 1e-6, "pos_int8");

      pt.addScaledEmbedInt8(emb_q8.data(), q_scale, scale, pos, out.data());
      for (std::size_t j = 0; j < dim; ++j) {
        ref[j] = static_cast<double>((static_cast<float>(emb_q8[j]) * q_scale) * scale) +
                 std::sin(static_cast<float>(pos) * freq[j] + off[j]);
      }
      checkNear(out.data(), ref.data(), dim, 1e-6, "pos_int8_2step");
    }
  }
}

void testInstanceNorm() {
  for (std::size_t dim : {4, 13, 384}) {
    for (std::size_t rows : {1, 3, 8}) {
      std::vector<float> x(rows * dim), scale(dim), bias(dim), out(rows * dim);
      std::vector<float> mins(rows), maxs(rows);
      for (std::size_t j = 0; j < dim; ++j) {
        scale[j] = 0.8f + 0.05f * static_cast<float>(j % 9);
        bias[j] = -0.2f + 0.03f * static_cast<float>(j % 13);
      }
      for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t j = 0; j < dim; ++j) {
          x[r * dim + j] = std::sin(static_cast<float>(r * 31 + j * 7)) * 4.0f + static_cast<float>(r);
        }
      }
      std::vector<double> ref(rows * dim);
      std::vector<double> ref_min(rows, 0.0), ref_max(rows, 0.0);
      for (std::size_t r = 0; r < rows; ++r) {
        double mean = 0.0;
        for (std::size_t j = 0; j < dim; ++j) mean += x[r * dim + j];
        mean /= static_cast<double>(dim);
        double var = 0.0;
        for (std::size_t j = 0; j < dim; ++j) {
          const double d = x[r * dim + j] - mean;
          var += d * d;
        }
        const double inv = 1.0 / std::sqrt(var / static_cast<double>(dim) + 1e-6);
        for (std::size_t j = 0; j < dim; ++j) {
          const double y = ((x[r * dim + j] - mean) * inv) * scale[j] + bias[j];
          ref[r * dim + j] = y;
          ref_min[r] = std::min(ref_min[r], y);
          ref_max[r] = std::max(ref_max[r], y);
        }
      }
      mtran::ssru::ops::instanceNormAffine(x.data(), scale.data(), bias.data(), rows, dim,
                                           out.data(), mins.data(), maxs.data());
      checkNear(out.data(), ref.data(), rows * dim, 1e-5, "norm_oop");
      checkNear(mins.data(), ref_min.data(), rows, 1e-5, "norm_min");
      checkNear(maxs.data(), ref_max.data(), rows, 1e-5, "norm_max");

      std::vector<float> inplace = x;
      mtran::ssru::ops::instanceNormAffine(inplace.data(), scale.data(), bias.data(), rows, dim,
                                           inplace.data(), mins.data(), maxs.data());
      checkNear(inplace.data(), ref.data(), rows * dim, 1e-5, "norm_inplace");
    }
  }
}

void testSsruGate() {
  for (std::size_t dim : {4, 13, 384}) {
    for (std::size_t rows : {1, 4, 8}) {
      const std::size_t n = rows * dim;
      std::vector<float> wf(n), w(n), x(n), cell(n), out(n);
      for (std::size_t i = 0; i < n; ++i) {
        wf[i] = (i % 17 == 0) ? -100.0f : (i % 19 == 0) ? 100.0f : std::sin(static_cast<float>(i * 1.13f)) * 12.0f;
        w[i] = std::cos(static_cast<float>(i * 0.71f)) * 3.0f;
        x[i] = std::sin(static_cast<float>(i * 0.37f)) * 2.0f;
        cell[i] = std::cos(static_cast<float>(i * 1.91f)) * 1.5f;
      }
      std::vector<double> ref_cell(n), ref_out(n);
      for (std::size_t i = 0; i < n; ++i) {
        const double g = 1.0 / (1.0 + std::exp(-static_cast<double>(wf[i])));
        ref_cell[i] = g * cell[i] + (1.0 - g) * w[i];
        ref_out[i] = x[i] + std::max(0.0, ref_cell[i]);
      }
      std::vector<float> c1 = cell;
      mtran::ssru::ops::ssruGateUpdate(wf.data(), w.data(), x.data(), rows, dim, c1.data(), out.data());
      checkNear(c1.data(), ref_cell.data(), n, 2e-6, "ssru_cell");
      checkNear(out.data(), ref_out.data(), n, 2e-6, "ssru_out");

      std::vector<float> c2 = cell, x_inplace = x;
      mtran::ssru::ops::ssruGateUpdate(wf.data(), w.data(), x_inplace.data(), rows, dim, c2.data(), x_inplace.data());
      checkNear(x_inplace.data(), ref_out.data(), n, 2e-6, "ssru_inplace");
    }
  }
}

void testCrossAttention() {
  for (std::size_t heads : {2, 8}) {
    const std::size_t head_dim = heads == 8 ? 48 : 12;
    for (std::size_t src_len : {1, 7, 13, 61, 270}) {
      std::vector<float> q(heads * head_dim), k_t(heads * head_dim * src_len), v(heads * src_len * head_dim);
      for (std::size_t i = 0; i < q.size(); ++i) q[i] = std::sin(static_cast<float>(i * 1.7 + 0.3)) * 0.8f;
      for (std::size_t i = 0; i < k_t.size(); ++i) k_t[i] = std::cos(static_cast<float>(i * 0.9 + 0.1)) * 0.6f;
      for (std::size_t i = 0; i < v.size(); ++i) v[i] = std::sin(static_cast<float>(i * 0.5 - 0.2)) * 1.2f;

      const double scale = 1.0 / std::sqrt(static_cast<double>(head_dim));
      std::vector<double> ref(heads * head_dim, 0.0);
      for (std::size_t h = 0; h < heads; ++h) {
        std::vector<double> sc(src_len);
        double mx = -std::numeric_limits<double>::infinity();
        for (std::size_t s = 0; s < src_len; ++s) {
          double dot = 0.0;
          for (std::size_t d = 0; d < head_dim; ++d) {
            dot += q[h * head_dim + d] * k_t[(h * head_dim + d) * src_len + s];
          }
          sc[s] = dot * scale;
          mx = std::max(mx, sc[s]);
        }
        double tot = 0.0;
        for (std::size_t s = 0; s < src_len; ++s) { sc[s] = std::exp(sc[s] - mx); tot += sc[s]; }
        for (std::size_t d = 0; d < head_dim; ++d) {
          double sum = 0.0;
          for (std::size_t s = 0; s < src_len; ++s) {
            sum += (sc[s] / tot) * v[(h * src_len + s) * head_dim + d];
          }
          ref[h * head_dim + d] = sum;
        }
      }

      std::vector<float> out(heads * head_dim), scratch(src_len);
      mtran::ssru::ops::crossAttention(q.data(), k_t.data(), v.data(), heads, head_dim, src_len,
                                       scratch.data(), out.data());
      checkNear(out.data(), ref.data(), heads * head_dim, 1e-5, "cross_attn");

      // Also test PcorePool head slicing.
      std::vector<float> out_par(heads * head_dim, 0.0f);
      mtran::ssru::PcorePool::instance().parallelFor(4, [&](std::size_t tid, std::size_t nthreads) {
        const std::size_t hb = (heads * tid) / nthreads;
        const std::size_t he = (heads * (tid + 1)) / nthreads;
        std::vector<float> local_scratch(src_len);
        mtran::ssru::ops::crossAttention(q.data(), k_t.data(), v.data(), heads, head_dim, src_len,
                                         local_scratch.data(), out_par.data(), hb, he);
      });
      checkNear(out_par.data(), ref.data(), heads * head_dim, 1e-5, "cross_attn_par");
    }
  }
}

void testSelfAttention() {
  for (std::size_t heads : {2, 8}) {
    const std::size_t head_dim = heads == 8 ? 48 : 12;
    const std::size_t dim = heads * head_dim;
    for (std::size_t seq_len : {1, 2, 7, 13, 61, 260}) {
      std::vector<float> qkv(seq_len * 3 * dim);
      std::vector<float> q(seq_len * dim), k(seq_len * dim), v(seq_len * dim);
      for (std::size_t t = 0; t < seq_len; ++t) {
        for (std::size_t d = 0; d < dim; ++d) {
          const float qv = std::sin(static_cast<float>(t * 17 + d * 3)) * 0.7f;
          const float kv = std::cos(static_cast<float>(t * 13 + d * 5)) * 0.7f;
          const float vv = std::sin(static_cast<float>(t * 7 - d * 11)) * 1.1f;
          q[t * dim + d] = qv;
          k[t * dim + d] = kv;
          v[t * dim + d] = vv;
          qkv[t * 3 * dim + d] = qv;
          qkv[t * 3 * dim + dim + d] = kv;
          qkv[t * 3 * dim + 2 * dim + d] = vv;
        }
      }

      const double scale = 1.0 / std::sqrt(static_cast<double>(head_dim));
      std::vector<double> ref(seq_len * dim, 0.0);
      for (std::size_t h = 0; h < heads; ++h) {
        const std::size_t h_off = h * head_dim;
        for (std::size_t t = 0; t < seq_len; ++t) {
          std::vector<double> sc(seq_len);
          double mx = -std::numeric_limits<double>::infinity();
          for (std::size_t s = 0; s < seq_len; ++s) {
            double dot = 0.0;
            for (std::size_t d = 0; d < head_dim; ++d) {
              dot += q[t * dim + h_off + d] * k[s * dim + h_off + d];
            }
            sc[s] = dot * scale;
            mx = std::max(mx, sc[s]);
          }
          double tot = 0.0;
          for (std::size_t s = 0; s < seq_len; ++s) { sc[s] = std::exp(sc[s] - mx); tot += sc[s]; }
          for (std::size_t d = 0; d < head_dim; ++d) {
            double sum = 0.0;
            for (std::size_t s = 0; s < seq_len; ++s) {
              sum += (sc[s] / tot) * v[s * dim + h_off + d];
            }
            ref[t * dim + h_off + d] = sum;
          }
        }
      }

      std::vector<float> out(seq_len * dim), scratch(heads * seq_len);
      mtran::ssru::ops::selfAttention(q.data(), k.data(), v.data(), seq_len, heads, head_dim,
                                      scratch.data(), out.data());
      checkNear(out.data(), ref.data(), seq_len * dim, 1e-5, "self_attn");

      std::vector<float> out_strided(seq_len * dim, 0.0f);
      mtran::ssru::PcorePool::instance().parallelFor(4, [&](std::size_t tid, std::size_t nthreads) {
        const std::size_t hb = (heads * tid) / nthreads;
        const std::size_t he = (heads * (tid + 1)) / nthreads;
        mtran::ssru::ops::selfAttentionStrided(qkv.data(), qkv.data() + dim, qkv.data() + 2 * dim,
                                               3 * dim, seq_len, heads, head_dim,
                                               scratch.data(), out_strided.data(), hb, he);
      });
      checkNear(out_strided.data(), ref.data(), seq_len * dim, 1e-5, "self_attn_strided_par");
    }
  }
}

void testPcorePool() {
  auto& pool = mtran::ssru::PcorePool::instance();
  mtran::ssru::PcorePool::pinCurrentThreadToPcores();
  const std::size_t pcores = pool.pcoreCount();
  assert(pcores >= 1);

  for (std::size_t req : {std::size_t{1}, std::size_t{2}, std::size_t{4}, std::size_t{6}, std::size_t{32}}) {
    std::atomic<std::size_t> count{0};
    std::atomic<std::size_t> tid_sum{0};
    std::size_t observed_nthreads = 0;
    pool.parallelFor(req, [&](std::size_t tid, std::size_t nthreads) {
      count.fetch_add(1, std::memory_order_relaxed);
      tid_sum.fetch_add(tid, std::memory_order_relaxed);
      if (tid == 0) observed_nthreads = nthreads;
      // Re-entrant nested call must run synchronously without deadlocking when team is busy.
      if (nthreads > 1) {
        std::size_t inner = 0;
        pool.parallelFor(4, [&](std::size_t itid, std::size_t inthreads) {
          assert(itid == 0 && inthreads == 1);
          ++inner;
        });
        assert(inner == 1);
      }
    });
    const std::size_t expected = std::min(req, pcores);
    assert(observed_nthreads == expected);
    assert(count.load() == expected);
    assert(tid_sum.load() == expected * (expected - 1) / 2);
  }

  bool caught = false;
  try {
    pool.parallelFor(pcores, [&](std::size_t tid, std::size_t) {
      if (tid == (pcores > 1 ? 1 : 0)) throw std::runtime_error("pool-error");
    });
  } catch (const std::runtime_error& e) {
    caught = std::string(e.what()) == "pool-error";
  }
  assert(caught);
}

}  // namespace

int main() {
  testPcorePool();
  testPosTable();
  testInstanceNorm();
  testSsruGate();
  testCrossAttention();
  testSelfAttention();
  std::cout << "PASS: ops_test (pcoreCount=" << mtran::ssru::PcorePool::instance().pcoreCount()
            << ", PosTable, instanceNormAffine, ssruGateUpdate, crossAttention, selfAttention)\n";
  return 0;
}
