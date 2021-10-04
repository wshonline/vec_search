#include <iostream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <map>
#include <random>
#include <fstream>
#include <stdio.h>
#include <cassert>
#include <gtest/gtest.h>
#include <unordered_map>

#include "index_impl.h"

class TmpFile {
 public:
  TmpFile(const string& parent = "/run/tmp") {
    path_ = parent + "/unittest-index-" + std::to_string(rand());
  }
  // TmpFile(const string& parent = "") {
  //   path_ = parent + "" + std::to_string(rand());
  // }

  ~TmpFile() {
    remove_file(path_);
  }

  string path() const {
    return path_;
  }

  static void remove_file(const string& path) {
    if (file_exists(path)) {
      remove(path.c_str());
    }
  }

 private:
  string path_;
};

TEST(VectorIndex, AddItem) {
  TmpFile tmp_file;
  string path = tmp_file.path();

  int f = 40;
  VectorIndex index(path, f);

  int n_items = 100;
  std::vector<std::vector<float>> items;
  for (int i = 0; i < n_items; i++) {
    items.emplace_back(std::vector<float>(f, i));
  }

  for (int item = 0; item < n_items; item++) {
    EXPECT_TRUE(index.add_item(item, items[item].data()));
  }

  float* ret = (float*)alloc_stack(sizeof(float) * f);
  for (int item = 0; item < n_items; item++) {
    index.get_item(item, ret);

    for (int i = 0; i < f; i++) {
      EXPECT_EQ(items[item][i], ret[i]);
    }
  }

  EXPECT_EQ(index.get_n_items(), n_items);
}

TEST(VectorIndex, BuildNoItem) {
  TmpFile tmp_file;
  string path = tmp_file.path();

  int f = 1;
  VectorIndex index(path, f);
  EXPECT_FALSE(index.build_index());
  EXPECT_FALSE(index.is_built());
}

TEST(VectorIndex, Build) {
  int f = 40;
  std::vector<std::vector<float>> items;
  int max_n_items = 10;
  for (int i = 0; i < max_n_items; i++) {
    items.emplace_back(std::vector<float>(f, i));
  }

  for (int n_items = 1; n_items < max_n_items; n_items++) {
    TmpFile tmp_file;
    string path = tmp_file.path();

    VectorIndex index(path, f);
    for (int i = 0; i < n_items; i++) {
      index.add_item(i, items[i].data());
    }
    EXPECT_TRUE(index.build_index());
    EXPECT_EQ(index.get_n_items(), n_items);

    for (int i = 0; i < n_items; i++) {
      int ret = index.search_top1(items[i].data());
      EXPECT_EQ(ret, i);
    }
  }
}

// only one item
TEST(VectorIndex, Search1Item) {
  TmpFile tmp_file;
  string path = tmp_file.path();

  int f = 40;
  VectorIndex index(path, f);

  float* vec = (float*)alloc_stack(sizeof(float) * f);
  for (int i = 0; i < f; i++) {
    vec[i] = i;
  }
  index.add_item(0, vec);
  EXPECT_EQ(index.get_n_items(), 1);
  EXPECT_EQ(index.build_index(), true);
  EXPECT_EQ(index.search_top1(vec), 0);

  for (int i = 0; i < f; i++) {
    vec[i] = 100;
  }
  EXPECT_EQ(index.search_top1(vec), 0);
}

// only two item
TEST(VectorIndex, Search2Item) {
  TmpFile tmp_file;
  string path = tmp_file.path();

  int f = 40;
  VectorIndex index(path, f);

  float* vec = (float*)alloc_stack(sizeof(float) * f);
  int n_items = 2;
  for (int item = 0; item < n_items; item++) {
    for (int i = 0; i < f; i++) {
      vec[i] = item;
    }
    index.add_item(item, vec);
  }
  EXPECT_EQ(index.build_index(), true);
  EXPECT_EQ(index.get_n_items(), n_items);
  // search existing vec always return the correct result
  EXPECT_EQ(index.search_top1(vec), 1);

  for (int i = 0; i < f; i++) {
    vec[i] = 100;
  }
  // (100, 100, ..., 100) is most similar to (1, 1, ..., 1)
  EXPECT_EQ(index.search_top1(vec), 1);
}


TEST(VectorIndex, SearchExistingItem) {
  TmpFile tmp_file;
  string path = tmp_file.path();

  int f = 40;
  int n_items = 100;
  std::default_random_engine generator(114514);
  std::normal_distribution<float> distribution(0.0, 1.0);
  std::vector<std::vector<float>> items(n_items, std::vector<float>(f, 0));
  for (int i = 0; i < n_items; i++) {
    for (int j = 0; j < f; j++) {
      items[i][j] = distribution(generator);
    }
  }

  VectorIndex index(path, f);
  for (int item = 0; item < n_items; item++) {
    index.add_item(item, items[item].data());
  }
  EXPECT_TRUE(index.build_index());

  float* rec = (float* )alloc_stack(sizeof(float) * f);
  for (int item = 0; item < n_items; item++) {
    // search on existing items always return the correct result
    int index_search = index.search_top1(items[item].data());
    EXPECT_EQ(item, index_search);
  }
}

TEST(VectorIndex, SearchSimilarItem) {
  TmpFile tmp_file;
  string path = tmp_file.path();

  int f = 40;
  int n_items = 100;
  std::default_random_engine generator(114514);
  std::normal_distribution<float> distribution(0.0, 1.0);
  std::vector<std::vector<float>> items(n_items, std::vector<float>(f, 0));
  for (int i = 0; i < n_items; i++) {
    for (int j = 0; j < f; j++) {
      items[i][j] = distribution(generator);
    }
  }

  VectorIndex index(path, f);
  for (int item = 0; item < n_items; item++) {
    index.add_item(item, items[item].data());
  }
  EXPECT_TRUE(index.build_index());

  float* rec = (float*)alloc_stack(sizeof(float) * f);
  for (int item = 0; item < n_items; item++) {
    float* vec_revised = (float* )alloc_stack(sizeof(float) * f);
    for (int dimension =0; dimension<f;dimension++) {
      vec_revised[dimension] = items[item][dimension] * 0.99;
    }
    int index_search = index.search_top1(vec_revised);
    EXPECT_EQ(item, index_search);
  }
}

TEST(VectorIndex, SearchNonExistingItem) {
  TmpFile tmp_file;
  string path = tmp_file.path();

  int f = 2;
  int n_items = 4;
  float items[n_items][f] = {
    {0, 1},
    {0, 2},
    {0, 3},
    {0, 4}
  };
  VectorIndex index(path, f);
  for (int i = 0; i < n_items; i++) {
    index.add_item(i, items[i]);
  }
  EXPECT_TRUE(index.build_index());

  auto get_true_neighbor = [&](float* vec) {
    float min_dist = std::numeric_limits<float>::max();
    int neighbor = 0;
    auto item_vec = (float*)alloc_stack(sizeof(float) * f);
    for (int i = 0; i < n_items; i++) {
      index.get_item(i, item_vec);
      float d = Distance::distance(item_vec, vec, f);
      if (min_dist > d) {
        min_dist = d;
        neighbor = i;
      }
    }

    return neighbor;
  };

  float vec_test_0[] = {0, 1.4};
  EXPECT_EQ(get_true_neighbor(vec_test_0), 0);
  EXPECT_EQ(index.search_top1(vec_test_0), 0);

  float vec_test_1[] = {0, 1.6};
  EXPECT_EQ(get_true_neighbor(vec_test_1), 1);
  EXPECT_EQ(index.search_top1(vec_test_1), 1);

  float vec_test_2[] = {0, 2.6};
  EXPECT_EQ(get_true_neighbor(vec_test_2), 2);
  EXPECT_EQ(index.search_top1(vec_test_2), 2);

  float vec_test_3[] = {0, 3.9};
  EXPECT_EQ(get_true_neighbor(vec_test_3), 3);
  EXPECT_EQ(index.search_top1(vec_test_3), 3);
}

TEST(VectorIndex, MultiThreadSearch) {
  TmpFile tmp_file;
  string path = tmp_file.path();

  int f = 40;
  int n_items = 100;
  std::default_random_engine generator(114514);
  std::normal_distribution<float> distribution(0.0, 1.0);
  std::vector<std::vector<float>> items(n_items, std::vector<float>(f, 0));
  for (int i = 0; i < n_items; i++) {
    for (int j = 0; j < f; j++) {
      items[i][j] = distribution(generator);
    }
  }

  VectorIndex index(path, f);
  for (int item = 0; item < 10; item++) {
    index.add_item(item, items[item].data());
  }
  EXPECT_TRUE(index.build_index());

  std::vector<std::thread> threads;
  int n_threads = 10;
  for (int i = 0; i < n_threads; i++) {
    int index_found;
    threads.emplace_back([&, i=i] {
      index_found = index.search_top1(items[i].data());
      EXPECT_EQ(index_found, i);
    });
  }

  for (int i = 0; i < n_threads; i++) {
    threads[i].join();
  }
}
