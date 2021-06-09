#ifndef VECTORINDEX_H
#define VECTORINDEX_H

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>

#include <cerrno>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <queue>
#include <limits>
#include "util.h"


class VectorIndexInterface {
 public:
  // 说明: 如果 path 上已经存有已经建立好的索引文件，直接使用该索引
  //       否则，path 会被后面 build_index 用到来保存索引文件
  // path: 持久化路径（比如pdmk pool路径），用来持久化保存索引
  // f:    为向量的维度
  VectorIndexInterface(const std::string &path, int f) : path_(path), f_(f) {}
  virtual ~VectorIndexInterface() {};

  // 说明:    插入数据，选手需要自己维护保存的数据，供后面调用 build_index 的时候使用
  //          注意: 插入的总数据量会大于DRAM，所以请保存到持久内存上
  //          假设item id是从0连续递增的
  // item_id: 向量id
  // w:       向量数组，维度为 constructor 中给定的参数f
  // 返回:    true，如果插入成功
  virtual bool add_item(int item_id, const float* w) = 0;

  // 说明: 所有向量 add_item 完成后，会调用该函数建立索引用的二叉查找树
  //       建立完成以后，将索引文件保存到持久化路径上（由 constructor 传入的 path）
  // 返回: true，如果建立成功
  virtual bool build_index() = 0;

  // target: 给定的需要查询的目标向量
  // 返回:   根据搜索算法，返回和目标向量最近的向量的item id
  virtual int search_top1(const float* target) = 0;

  // 返回: 数据库内的总items的数目
  virtual int get_n_items() const = 0;

  // item_id: 输入的 item_id
  // v:       输出结果，根据item id，获得对应的向量信息
  virtual void get_item(int item_id, float* v) = 0;

 protected:
  string path_;  // 索引持久化路径
  int f_;  // 向量维度
};

#endif
