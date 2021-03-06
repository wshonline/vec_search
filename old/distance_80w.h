#pragma once

#include <vector>
#include <string>
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

inline float dot(const float* x, const float* y, int f) {
  float s = 0;
  for (int z = 0; z < f; z++) {
    s += x[z] * y[z];
    // x++;
    // y++;
  }
  return s;
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

// DRAM?????????????????????
// ????????????????????????????????????????????????
// sizeof(VNode)=32
struct VNode {
  int left = -1;
  int right = -1;
  // float* v;
  persistent_ptr<float[]> v;
  float alpha; // need an extra constant term to determine the offset of the plane
};

struct MemNode {
  // int left_in_mem_array;
  // int right_in_mem_array;
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
