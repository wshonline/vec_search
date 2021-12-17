/*
 * example DRAM implementation
 */

/**
 *
 * 22层
 * xxh3
 * bytell_hash_map + lock_guard
 * 重排内存节点
 */
// taskset -c 0-3 ./demo --path pool.set --nodes 5000000 --test_count 125000 --random --random_prop 1 --thread 8
// hash 完整的256个float
// 单线程QPS 6.7w左右
// 在此基础上使用AVX2，单线程QPS 15w左右

#pragma once

#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <queue>
// #include <atomic>

#include "index.h"
#include "distance.h"
#include "xxh3.h"
// #include "xxhash64.h"
#include "bytell_hash_map.h"

#define POOLSIZE ((1024LL * 1024 * 1024 * 50))
const uint32_t LEVEL = 22;
const std::string LAYOUT = "";
// uint64_t myseed = 1313;
XXH64_hash_t seed = 1313;

// // 自旋锁的实现
// class spin_lock {
// public:
//     spin_lock() = default;
//     spin_lock(const spin_lock&) = delete; 
//     spin_lock& operator=(const spin_lock) = delete;
//     void lock() {   // acquire spin lock
//         while (flag.test_and_set()) {}
//     }
//     void unlock() {   // release spin lock
//         flag.clear();
//     }
// private:
//     std::atomic_flag flag;
// };

class VectorIndex : public VectorIndexInterface {
  /*
   * We use random projection to build a forest of binary trees of all items.
   * Basically just split the hyperspace into two sides by a hyperplane,
   * then recursively split each of those subtrees etc.
   * We create a tree like this q times.
   */
 public:
  // 需要修改成持久内存的指针
  // typedef persistent_ptr<Node> NodePtr;
  // typedef MemNode* MemNodePtr;
  using VectorIndexInterface::f_;
  using VectorIndexInterface::path_;

  // 需要修改成持久化结构
  struct Tree {
    int n_items;  // leaf num
    bool built;
    int root;
  };

  struct root {
    persistent_ptr<Tree> tree;
    persistent_ptr<Node[]> node_array_space;
    persistent_ptr<float[]> float_array_space;
    p<int> node_total;
  };

  pool<root> pop;
  Node* node_array_start;
  float* float_array_start;

  VectorIndex(const string& path, int f) :
      VectorIndexInterface(path, f) {

    mem_tree_level_ = LEVEL;
    uint32_t element_num = 1 << mem_tree_level_;
    std::cout << "mem_tree_level_ = " << mem_tree_level_ << std::endl;
    std::cout << "element_num = " << element_num << std::endl;
    memnode_array_space = new MemNode[element_num];
    memfloat_array_space = new float[element_num * f_];
    
    if (path.find("pool.set") != string::npos) {
      std::cout << "进入pool.set" << std::endl;
      try {
        pop = pool<root>::create(path, LAYOUT, 0, S_IRWXU);
        proot = pop.root();
        transaction::run(pop, [&] {
          // 初始化时，开辟大块的pmem空间
          proot->tree = make_persistent<Tree>();
          proot->node_array_space = make_persistent<Node[]>(15LL * 1000 * 1000);
          proot->float_array_space = make_persistent<float[]>(15LL * 1000 * 1000 * f);
          node_array_start = proot->node_array_space.get();
          float_array_start = proot->float_array_space.get();
        });
      } catch (const pmem::pool_error &e) {                                                                                                                                                                        
        pop = pool<root>::open(path, LAYOUT);
        proot = pop.root();
        node_array_start = proot->node_array_space.get();
        float_array_start = proot->float_array_space.get();
        if (proot->tree->built) {
          // node_cur_num = proot->tree->n_items;  // 让get函数通过内读取该数值 error
          node_cur_num = proot->node_total;  // 让get函数通过内读取该数值
          // 建立内存索引
          // 建立hash表
          build_tree_index_in_memory_and_relable_memnode();
          build_hash_in_memory();
        }
      }   
    } else {
      if (access(path.c_str(), F_OK) == 0) {
        std::cout << "进入else if" << std::endl;
        pop = pool<root>::open(path, LAYOUT);
        proot = pop.root();
        node_array_start = proot->node_array_space.get();
        float_array_start = proot->float_array_space.get();
        if (proot->tree->built) {
          // node_cur_num = proot->tree->n_items;  // 让get函数通过内读取该数值 error
          node_cur_num = proot->node_total;  // 让get函数通过内读取该数值
          // 建立内存索引
          // 建立hash表
          build_tree_index_in_memory_and_relable_memnode();
          build_hash_in_memory();
        }
      } else {
        std::cout << "进入else else" << std::endl;
        pop = pool<root>::create(path, LAYOUT, POOLSIZE, S_IRWXU);
        proot = pop.root();
        transaction::run(pop, [&] {
          // 初始化时，开辟大块的pmem空间
          proot->tree = make_persistent<Tree>();
          proot->node_array_space = make_persistent<Node[]>(15LL * 1000 * 1000);
          proot->float_array_space = make_persistent<float[]>(15LL * 1000 * 1000 * f);
          node_array_start = proot->node_array_space.get();
          float_array_start = proot->float_array_space.get();
        });
      }
    }
  }

  ~VectorIndex() {
    pop.close();
  }

  bool add_item(int item, const float* w) override {
    if (proot->tree->built) {
      log("You can't add an item to an already built index");
      return false;
    }
    // transaction::run(pop, [&] {
      Node* n = get(item);
      n->left = -1;
      n->right = -1;
      
      for (int z = 0; z < f_; z++)
        n->v[z] = *(w + z);

      if (item >= proot->tree->n_items)
        proot->tree->n_items = item + 1;
    // });

    return true;
  }

  bool build_index() override {
    if (proot->tree->built) {
      log("You can't build a built index\n");
      return false;
    }
    if (proot->tree->n_items == 0){
      log("Tree is empty\n");
      return false;
    }

    n_nodes_ = proot->tree->n_items;
    node_cur_num = proot->node_total;

    std::vector<int> indices;
    for (int i = 0; i < proot->tree->n_items; i++) {
      indices.push_back(i);
    }
    transaction::run(pop, [&] {
      proot->tree->root = make_tree(indices);
      proot->tree->built = true;
    });
    // log("num of total nodes = %ld\n", n_nodes_);

    if (proot->tree->built) {
      build_tree_index_in_memory_and_relable_memnode();
      build_hash_in_memory();
    }

    return true;
  }
  
  void build_hash_in_memory() {
    // uint32_t leaf_num = (proot->tree->n_items + 1) / 2;
    uint32_t leaf_num = proot->tree->n_items;
    for (uint32_t i = 0; i < leaf_num; i++) {
      Node* n = get(i);
      // uint64_t result = XXHash64::hash(n->v.get(), sizeof(float) * f_, myseed);
      XXH64_hash_t result = XXH3_64bits_withSeed(n->v.get(), sizeof(float) * f_, seed);
      auto it = hashret_item_map.find(result);
      if (it == hashret_item_map.end()) {
        hashret_item_map.insert({result, i});
      }
    }
    std::cout << "build_hash_in_memory..." << std::endl;
  }

  int build_tree_index_in_memory_and_relable_memnode() {
    if (!proot->tree->built) {
      return 0;
    }

    int node = proot->tree->root;
    Node* nd = get(node);

    uint32_t cur_loc = 0;
    MemNode* mem_nd = get_mem_node(cur_loc);
    // node_arrayidx_hash_map[node] = cur_loc;
    cur_loc++;

    memnode_root = 0;
    memcpy(mem_nd->v, nd->v.get(), sizeof(float) * f_);
    mem_nd->origin = node;
    mem_nd->left = nd->left;
    mem_nd->right = nd->right;
    mem_nd->alpha = nd->alpha;
    
    std::queue <MemNode*> q;
    q.push(mem_nd);

    int currentLevel = 1;
    while (!q.empty() && currentLevel < mem_tree_level_) {
      int currentLevelSize = q.size();
      for (int i = 0; i < currentLevelSize; ++i) {
        auto node = q.front();
        q.pop();
        if (node->left != -1) {
          nd = get(node->left);

          mem_nd = get_mem_node(cur_loc);
          // node_arrayidx_hash_map[node->left] = cur_loc;
          mem_nd->origin = node->left;  // save origin pmem_node id
          node->left = cur_loc;
          cur_loc++;

          memcpy(mem_nd->v, nd->v.get(), sizeof(float) * f_);
          mem_nd->left = nd->left;
          mem_nd->right = nd->right;
          mem_nd->alpha = nd->alpha;
          q.push(mem_nd);
        }
        if (node->right != -1) {
          nd = get(node->right);

          mem_nd = get_mem_node(cur_loc);
          // node_arrayidx_hash_map[node->right] = cur_loc;
          mem_nd->origin = node->right;  // save origin pmem_node id
          node->right = cur_loc;
          cur_loc++;

          memcpy(mem_nd->v, nd->v.get(), sizeof(float) * f_);
          mem_nd->left = nd->left;
          mem_nd->right = nd->right;
          mem_nd->alpha = nd->alpha;
          q.push(mem_nd);
        }
      }
      currentLevel++;
    }
    std::cout << "build_tree_index_in_memory_and_relable_memnode...level is " << currentLevel << std::endl;
    return currentLevel;
  }

  int search_top1(const float* target) override {
    /*************** search in hash ***************/
    // uint64_t result = XXHash64::hash(target, sizeof(float) * f_, myseed);
    XXH64_hash_t result = XXH3_64bits_withSeed(target, sizeof(float) * f_, seed);
    auto it = hashret_item_map.find(result);
    if (it != hashret_item_map.end()) {
      // go_hash++;
      return it->second;
    }

    /********* search in mem tree index *********/
    // go_tree++;
    int node = memnode_root;
    // MemNode* mem_nd = get_mem_node(node);
    MemNode* mem_nd = memnode_array_space;
    int currentLevel = 1;
    float margin;

    while (mem_nd->left != -1) {
      margin = dist_.margin_mem(mem_nd, target, f_);
      if (margin <= 0) {
        node = mem_nd->left;
      } else {
        node = mem_nd->right;
      }
      ++currentLevel;
      if (currentLevel > mem_tree_level_) {
        break;
      }
      // mem_nd = get_mem_node(node);
      mem_nd = memnode_array_space + node;
    }

    /*** 如果是target在内存索引树中，加入hash ***/
    if (currentLevel <= mem_tree_level_) {
      std::lock_guard<std::mutex> latch(mutex_[0]);
      hashret_item_map.insert({result, mem_nd->origin});
      return mem_nd->origin;
    }

    /******* search in pmem tree index *******/
    // Node* nd = get(node);
    Node* nd = node_array_start + node;
    while (nd->left != -1) {
      margin = dist_.margin(nd, target, f_);
      if (margin <= 0) {
        node = nd->left;
      } else {
        node = nd->right;
      }
      // node为叶子节点，即可返回
      if (node < n_nodes_) {
        break;
      }
      // nd = get(node);
      nd = node_array_start + node;
    }

    /************** add to hash **************/
    std::lock_guard<std::mutex> latch(mutex_[1]);
    hashret_item_map.insert({result, node});
    return node;
  }

  int get_n_items() const override {
    return proot->tree->n_items;
  }

  void get_item(int item, float* v) override {
    Node* m = get(item);
    memcpy(v, m->v.get(), (f_) * sizeof(float));
  }

  bool is_built() const {
    return proot->tree->built;
  }
  
  void print_hit_status() {
    std::cout << "Hit count: " << hit_count << std::endl;
    std::cout << "Miss count: " << miss_count << std::endl;
    std::cout << "Hit prop: " << hit_count / float(hit_count + miss_count) << std::endl;
  }

 private:
  int n_nodes_ = 0;
  Distance dist_;
  pmem::obj::persistent_ptr<VectorIndex::root> proot;
  uint32_t node_cur_num = 0;

  // MemTree
  MemNode* memnode_array_space;
  float* memfloat_array_space;
  uint32_t memnode_cur_num = 0;
  int mem_tree_level_ = 0;
  int memnode_root = 0;

  // ska::bytell_hash_map<int, uint32_t> node_arrayidx_hash_map;  // relable后, 就不需要查表, node可以直接作为array idx
  ska::bytell_hash_map<uint64_t, int> hashret_item_map;

  std::mutex mutex_[2];  // 互斥锁
  // spin_lock splock[2];  // 自旋锁

  // debug
  int hit_count = 0;
  int miss_count = 0;
  int go_tree = 0;
  int go_hash = 0;

public:
  // 需要在持久内存上新建节点
  Node* get(const int i) {
    if (i < node_cur_num) {
      return node_array_start + i;
    }
    else {
      Node* node = node_array_start + node_cur_num;
      node->v = float_array_start + (uint32_t)node_cur_num * f_;  // 注意 boundary error
      // proot->tree->n_items++;  // no need, n_item is leaf num.
      proot->node_total++;
      node_cur_num++;
      return node;
    }
  }

  Node* get(const int i) const {
    return node_array_start + i;
  }

  MemNode* get_mem_node(const int i) {
    if (i < memnode_cur_num) {
      return memnode_array_space + i;
    }
    else {
      MemNode* n = memnode_array_space + memnode_cur_num;
      n->v = memfloat_array_space + memnode_cur_num * f_;
      memnode_cur_num++;
      return n;
    }
  }

  MemNode* get_mem_node(const int i) const {
    return memnode_array_space + i;
  }

  int make_tree(const std::vector<int >& indices) {
    if (indices.size() == 1)
      return indices[0];

    std::vector<Node*> children;
    for (size_t i = 0; i < indices.size(); i++) {
      int j = indices[i];
      Node* n = get(j);
      if (n)
        children.push_back(n);
    }

    std::vector<int> children_indices[2];
    VNode m;
    m.v = make_persistent<float[]>(f_);  // should free
    dist_.create_hyperplane(children, f_, &m);
    
    for (size_t i = 0; i < indices.size(); i++) {
      int j = indices[i];
      Node* n = get(j);
      if (n) {
        bool side = dist_.side(&m, n->v.get(), f_);
        children_indices[side].push_back(j);
      } else {
        log("No node for index %d?\n", j);
      }
    }

    // to be simple, we do not consider randomize this case
    if (children_indices[0].size() == 0 || children_indices[1].size() == 0) {
      log("trees not balanced. left children num. = %ld, right children num. = %ld\n", children_indices[0].size(), children_indices[1].size());
    }

    if (children_indices[0].size() > 0) {
      m.left = make_tree(children_indices[0]);
    }

    if (children_indices[1].size() > 0) {
      m.right = make_tree(children_indices[1]);
    }

    int item = n_nodes_++;  // 不能使用n_nodes_直接当get内的偏移
    Node* node = get(item);
    node->alpha = m.alpha;
    node->left = m.left;
    node->right = m.right;
    memcpy(node->v.get(), m.v.get(), f_ * sizeof(float));
    delete_persistent<float[]>(m.v, f_);  // free

    return item;
  }
};
