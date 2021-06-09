# 代码结构

这里提供了向量索引实现所需要的头文件、必要的高维空间距离公式、示例代码以及测试代码。代码结构如下：
```bash
include/
    index.h  # 向量索引interface定义
    distance.h  # 索引中用到的距离公式，以及如何获得hyperplane
    random.h  # 随机函数（参赛选手不会直接使用，distance.h会使用）
    util.h  # 一些公共的功能函数定义
impl/
    index_impl.h  # **这里给出了DRAM基础版本实现，选手在这个文件里修改为基于持久内存版本**
test/
    unittest.cpp  # unittest代码，选手可以通过unittest进行正确性的自测
Makefile
demo.cpp  # 如何使用向量索引的示例代码，选手可以通过该程序进行性能的自测
README.md
```

我们提供了一个基于DRAM的代码实现（impl/index_impl.h），选手可以根据pmdk的接口，修改成基于持久内存的实现。

注意：

- 评测程序只会拷贝选手代码目录下的include/*和impl/*，然后和评测程序打包编译，其他文件不会使用。
- 选手可以根据pmdk接口，保证满足include/index.h定义的VectorIndexInterface的接口前提下，自由修改impl/index_impl.h的代码，但请不要修改建树和搜索的算法（即算法逻辑保持不变，代码可以做改动），否则会影响正确性评定。
- 参赛选手可以根据pmdk的接口（比如持久指针），修改include/distance.h的定义（比如Node的定义）。但请保持hyperplane函数和其他距离函数的算法逻辑不变，否则会影响正确性评定。
- 请不要直接使用include/distance.h中的random函数，否则会影响正确性评定。
- demo为简单的单线程示例，如果想要测试多线程的search_top1性能，选手可以自行修改。

# 编译和测试

## 编译
```bash
# make会生成unittest和demo两个可执行文件
make
```

## 测试
### 运行unittest
```bash
# unittest
./unittest
```

### 运行demo
```bash
# ./demo --help可以查看可选输入参数
# 注意：评测程序输入的--path为poolset的配置文件，用户可以调用相同的pool::create接口来创建pmem pool，不过size需要设置成0
./demo_pmem --path /pmem/vector.tree --nodes 1000 --test_count 100
Running demo with:
num. features: 256
num. nodes: 1000
num. tests: 100
index path: /pmem/vector.tree
populate data: true

openning /pmem/vector.tree
create /pmem/vector.tree successfully
Load done in 0.076 secs.
Building index ... 
Adding items [0 to 1000)
Add items done in 0.027 secs.
n_nodes_ = 1999
Build done in 0.494 secs.

Top1:   precision: 100.00%      avg. time: 0.00760000 ms        query/s: 131578.94736842
```

请注意：

- 关注query/s的指标，为search_top1的吞吐性能。实际评测程序会多线程调用search_top1来测试吞吐。
- precision在demo程序中如果不是100%，则说明程序实现有问题，需要选手debug
