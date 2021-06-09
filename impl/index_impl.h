/*
 * example DRAM implementation
 */
#pragma once

#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <unistd.h>

#include "index.h"
#include "distance.h"

class VectorIndex : public VectorIndexInterface {
  /*
   * We use random projection to build a forest of binary trees of all items.
   * Basically just split the hyperspace into two sides by a hyperplane,
   * then recursively split each of those subtrees etc.
   * We create a tree like this q times.
   */
 public:
  typedef Node* NodePtr;
  using VectorIndexInterface::f_;
  using VectorIndexInterface::path_;

  struct Tree {
    std::vector<NodePtr> nodes;
    int n_items;
    bool built;
    int root;
  };

  VectorIndex(const string& path, int f) :
      VectorIndexInterface(path, f) {
    tree_ = new Tree();
  }

  ~VectorIndex() {
    delete tree_;
  }

  bool add_item(int item, const float* w) override {
    if (tree_->built) {
      log("You can't add an item to an already built index");
      return false;
    }

    NodePtr n = get(item);
    n->left = -1;
    n->right = -1;

    for (int z = 0; z < f_; z++)
      n->v[z] = *(w + z);

    if (item >= tree_->n_items)
      tree_->n_items = item + 1;

    return true;
  }

  bool build_index() override {
    if (tree_->built) {
      log("You can't build a built index\n");
      return false;
    }
    if (tree_->n_items == 0){
      log("Tree is empty\n");
      return false;
    }

    n_nodes_ = tree_->n_items;

    std::vector<int> indices;
    for (int i = 0; i < tree_->n_items; i++) {
      indices.push_back(i);
    }
    tree_->root = make_tree(indices);
    tree_->built = true;
    // log("num of total nodes = %ld\n", n_nodes_);

    return true;
  }

  int search_top1(const float* target) override {
    std::vector<pair<float, int>> res;
    int node = tree_->root;
    Node* nd = get(node);
    while (nd->left != -1 && nd->right != -1) {
      float margin = dist_.margin(nd, target, f_);
      if (nd->left == -1) {
        node = nd->right;
      } else if (nd->right == -1) {
        node = nd->left;
      } else if (margin < 0) {
        node = nd->left;
      } else {
        node = nd->right;
      }
      nd = get(node);
    }

    return node;
  }

  int get_n_items() const override {
    return tree_->n_items;
  }

  void get_item(int item, float* v) override {
    NodePtr m = get(item);
    memcpy(v, m->v, (f_) * sizeof(float));
  }

  bool is_built() const {
    return tree_->built;
  }

 private:
  int n_nodes_ = 0;
  Distance dist_;
  Tree* tree_ = nullptr;

  NodePtr get(const int i) {
    if (i < tree_->nodes.size()) {
      return tree_->nodes.at(i);
    } else {
      auto n = new Node();
      n->v = new float[f_];
      tree_->nodes.emplace_back(n);
      return n;
    }
  }

  NodePtr get(const int i) const {
    return tree_->nodes.at(i);
  }

  int make_tree(const std::vector<int >& indices) {
    if (indices.size() == 1)
      return indices[0];

    std::vector<NodePtr> children;
    for (size_t i = 0; i < indices.size(); i++) {
      int j = indices[i];
      NodePtr n = get(j);
      if (n)
        children.push_back(n);
    }

    std::vector<int> children_indices[2];
    VNode m;
    m.v = (float*)alloc_stack(sizeof(float) * f_);
    dist_.create_hyperplane(children, f_,  &m);

    for (size_t i = 0; i < indices.size(); i++) {
      int j = indices[i];
      NodePtr n = get(j);
      if (n) {
        bool side = dist_.side(&m, n->v, f_);
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

    int item = n_nodes_++;
    NodePtr node = get(item);
    node->alpha = m.alpha;
    node->left = m.left;
    node->right = m.right;
    memcpy(node->v, m.v, f_ * sizeof(float));

    return item;
  }
};
