#pragma once

#include <vector>
#include <string>
#include <immintrin.h>
#include "random.h"

#include <libpmemobj++/make_persistent.hpp>
#include <libpmemobj++/make_persistent_array_atomic.hpp>
#include <libpmemobj++/make_persistent_atomic.hpp>
#include <libpmemobj++/p.hpp>
#include <libpmemobj++/persistent_ptr.hpp>
#include <libpmemobj++/pool.hpp>
#include <libpmemobj++/transaction.hpp>
#include <libpmemobj++/container/vector.hpp>

using namespace pmem::obj;
using std::vector;
using std::pair;
using std::numeric_limits;
using std::make_pair;
using std::string;


// alloc on stack
// no need to free afterwards
#define alloc_stack(x) __builtin_alloca((x))

inline float serial_dot(const float* x, const float* y, int f) {
  float s = 0;
  for (int z = 0; z < f; z++) {
    s += x[z] * y[z];
    // x++;
    // y++;
  }
  return s;
}

template<int offsetRegs>
inline __m256 mul8( const float* p1, const float* p2 )
{
    constexpr int lanes = offsetRegs * 8  ;
    const __m256 a = _mm256_loadu_ps( p1 + lanes );
    const __m256 b = _mm256_loadu_ps( p2 + lanes );
    return _mm256_mul_ps( a, b );
}

template<int offsetRegs>
inline __m256 fma8( __m256 acc, const float* p1, const float* p2 )
{
    constexpr int lanes = offsetRegs * 8;
    const __m256 a = _mm256_loadu_ps( p1 + lanes );
    const __m256 b = _mm256_loadu_ps( p2 + lanes );
    return _mm256_fmadd_ps( a, b, acc );
}

float dot(const float* x, const float* y, int f) {
  // 当维度为32的倍数时，使用AVX2做并行计算
  if (f % 32 != 0) {
    return serial_dot(x, y, f);
  }

  const float* p1 = x;
  const float* const p1End = p1 + f;
  const float* p2 = y;

  __m256 dot0 = mul8<0>( p1, p2 );
  __m256 dot1 = mul8<1>( p1, p2 );
  __m256 dot2 = mul8<2>( p1, p2 );
  __m256 dot3 = mul8<3>( p1, p2 );
  p1 += 8 * 4;
  p2 += 8 * 4;

  while( p1 < p1End )
  {
      dot0 = fma8<0>( dot0, p1, p2 );
      dot1 = fma8<1>( dot1, p1, p2 );
      dot2 = fma8<2>( dot2, p1, p2 );
      dot3 = fma8<3>( dot3, p1, p2 );
      p1 += 8 * 4;
      p2 += 8 * 4;
  }

  const __m256 dot01 = _mm256_add_ps( dot0, dot1 );
  const __m256 dot23 = _mm256_add_ps( dot2, dot3 );
  const __m256 dot0123 = _mm256_add_ps( dot01, dot23 );

  const __m128 r4 = _mm_add_ps( _mm256_castps256_ps128( dot0123 ), _mm256_extractf128_ps( dot0123, 1 ) );
  const __m128 r2 = _mm_add_ps( r4, _mm_movehl_ps( r4, r4 ) );
  const __m128 r1 = _mm_add_ss( r2, _mm_movehdup_ps( r2 ) );
  return _mm_cvtss_f32( r1 );
}

float euclidean_distance(const float* x, const float* y, int f) {
  float d = 0.0;
  for (int i = 0; i < f; ++i) {
    const float tmp = x[i] - y[i];
    d += tmp * tmp;
    // ++x;
    // ++y;
  }
  return d;
}

inline float get_norm(float* v, int f) {
  return sqrt(dot(v, v, f));
}

void normalize(float* v, int f) {
  float norm = get_norm(v, f);
  if (norm > 0) {
    for (int z = 0; z < f; z++)
      v[z] /= norm;
  }
}

// a heuristic to find the two means from list of nodes
template <typename NodePtr, typename Distance>
void two_means(const std::vector<NodePtr>& nodes, int f, Random& random, float* p, float* q) {
  static int iteration_steps = 200;
  size_t count = nodes.size();

  size_t i = random.index(count);
  size_t j = random.index(count-1);
  j += (j >= i); // ensure that i != j

  memcpy(p, nodes[i]->v.get(), f * sizeof(float));
  memcpy(q, nodes[j]->v.get(), f * sizeof(float));

  int ic = 1, jc = 1;
  for (int l = 0; l < iteration_steps; l++) {
    size_t k = random.index(count);
    float di = ic * Distance::distance(p, nodes[k]->v.get(), f);
    float dj = jc * Distance::distance(q, nodes[k]->v.get(), f);
    float norm = 1;
    if (!(norm > float(0))) {
      continue;
    }
    if (di < dj) {
      for (int z = 0; z < f; z++)
        p[z] = (p[z] * ic + nodes[k]->v[z] / norm) / (ic + 1);
      ic++;
    } else if (dj < di) {
      for (int z = 0; z < f; z++)
        q[z] = (q[z] * jc + nodes[k]->v[z] / norm) / (jc + 1);
      jc++;
    }
  }
}

// DRAM版本的节点定义
// 选手需要修改成基于持久内存的定义
// sizeof(VNode)=32
struct VNode {
  int left = -1;
  int right = -1;
  // float* v;
  persistent_ptr<float[]> v;
  float alpha; // need an extra constant term to determine the offset of the plane
};

struct MemNode {
  int origin;  // origin pmem_node id
  int left = -1;
  int right = -1;
  float* v;
  float alpha; // need an extra constant term to determine the offset of the plane
};

typedef VNode Node;

class Euclidean {
 public:
  static float margin(const Node* xn, const float* y, int f) {
    return xn->alpha + dot(xn->v.get(), y, f);
  }

  static float margin_mem(const MemNode* xn, const float* y, int f) {
    return xn->alpha + dot(xn->v, y, f);
  }

  bool side(const Node* xn, const float* y, int f) {
    float dot = margin(xn, y, f);
    return (dot > 0);
  }

  static float distance(const float* x, const float* y, int f) {
    return euclidean_distance(x, y, f);
  }

  void create_hyperplane(const std::vector<Node*>& nodes, int f, Node* hyperplane) {
    float* p = (float*)alloc_stack(f * sizeof(float));
    float* q = (float*)alloc_stack(f * sizeof(float));

    two_means<Node*, Euclidean>(nodes, f, random_, p, q);
    for (int z = 0; z < f; z++) {
      hyperplane->v[z] = p[z] - q[z];
    }
    normalize(hyperplane->v.get(), f);
    hyperplane->alpha = 0.0;
    for (int z = 0; z < f; z++)
      hyperplane->alpha += -hyperplane->v[z] * (p[z] + q[z]) / 2;
  }

  static float normalized_distance(float distance) {
    return sqrt(std::max(distance, float(0)));
  }

 private:
  Random random_;
};

typedef Euclidean Distance;
