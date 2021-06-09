#include <iostream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <map>
#include <random>
#include <fstream>
#include <stdio.h>

#include "index_impl.h"

int precision(const string& path, int f, int n, int prec_n, bool verbose, bool populate) {
  std::chrono::high_resolution_clock::time_point t_start, t_end;

	std::default_random_engine generator(12345);
  std::normal_distribution<float> distribution(0.0, 1.0);

  //******************************************************
  // Building the tree
  t_start = std::chrono::high_resolution_clock::now();
  VectorIndex t(path, f);
  t_end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start ).count();
  std::cout << "Load done in " << (duration / 1000.0) << " secs." << std::endl;

  if (!populate) {
    n = t.get_n_items();
  } else {
    std::cout << "Building index ... " << std::endl;

    // to keep the data consistent for every run,
    // we always do the data generation, even for the already inserted items
    for (int i = 0; i < t.get_n_items(); i++) {
      for (int z = 0; z < f; ++z){
        distribution(generator);
      }
    }

    t_start = std::chrono::high_resolution_clock::now();
    std::cout << "Adding items [" << t.get_n_items() << " to " << n << ")" << std::endl;
    float *vec = (float*) alloc_stack( f * sizeof(float) );
    for (int i = t.get_n_items(); i < n; ++i){

      for (int z = 0; z < f; ++z){
        vec[z] = distribution(generator);
      }

      t.add_item(i, vec);

      if (verbose)
        std::cout << "Loading objects ...\t object: "<< i+1 << "\tProgress:"<< std::fixed << std::setprecision(2) << (double) i / (double)(n + 1) * 100 << "%\r";

    }

    t_end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start ).count();
    std::cout << "Add items done in " << (duration / 1000.0) << " secs." << std::endl;

    t_start = std::chrono::high_resolution_clock::now();
    t.build_index();
    t_end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start ).count();
    std::cout << "Build done in " << (duration / 1000.0) << " secs." << std::endl;
  }

  //******************************************************
  std::vector<int> topk = {10, 100};

  double prec_sum = 0;
  double time_sum = 0;

  // doing the work
  Random random;
  int vec_size = f * sizeof(float);
  float* vec = (float*)alloc_stack(vec_size);
  float* vec2 = (float*)alloc_stack(vec_size);
  for (int i=0; i < prec_n; ++i){
    // select a random node
    int j = random.rand() % n;

    if (verbose)
      std::cout << "finding nbs for " << j << std::endl;

    t.get_item(j, vec);
    // you can also use distribution generator to generate the vector
    // but in this case, the precision may be not necessarily 100%
    // for (int z = 0; z < f; ++z){
    //   vec[z] = distribution(generator);
    // }

    float min_dist = std::numeric_limits<float>::max();
    int closest = 0;
    for (int k = 0; k < t.get_n_items(); k++) {
      t.get_item(k, vec2);
      float dist = Distance::distance(vec2, vec, f);
      if (dist < min_dist) {
        min_dist = dist;
        closest = k;
      }
    }

    t_start = std::chrono::high_resolution_clock::now();
    int top = t.search_top1(vec);
    t_end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>( t_end - t_start ).count();

    // storing metrics
    double hitrate = closest == top;
    prec_sum += hitrate;
    time_sum += duration;

    // print resulting metrics
    if (verbose) {
      std::cout << "Top1: " << "\tprecision: " << std::fixed << std::setprecision(2)
          << (100.0 * prec_sum / (i + 1)) << "% \tavg. time: "<< std::fixed << std::setprecision(8)
          << (time_sum / (i + 1)) * 1e-03 << " ms"
          << "\tquery/s: " << (i + 1) / (time_sum * 1e-06) << std::endl;
    }
  }

  std::cout << "\nTop1: " << "\tprecision: "<< std::fixed << std::setprecision(2)
      << (100.0 * prec_sum / (prec_n)) << "% \tavg. time: "<< std::fixed << std::setprecision(8)
      << (time_sum / (prec_n)) * 1e-03 << " ms"
      << "\tquery/s: " << (prec_n) / (time_sum * 1e-06) << std::endl;

  return 0;
}

void help() {
  std::cout << "Vector Demo C++ example" << std::endl;
  std::cout << "Usage:" << std::endl;
  std::cout << "./precision [--features num_features] [--nodes num_nodes] [--path index_path] [--test_count num_of_tests] [--populate true/false] [--verbose]" << std::endl;
  std::cout << std::endl;
}

void feedback(const string& path, int f, long long n, int prec_n, bool populate) {
  std::cout <<"Running demo with:" << std::endl;
  std::cout <<"num. features: " << f << std::endl;
  std::cout <<"num. nodes: " << n << std::endl;
  std::cout <<"num. tests: " << prec_n << std::endl;
  std::cout <<"index path: " << path << std::endl;
  std::cout <<"populate data: " << (populate ? "true" : "false") << std::endl;
  std::cout << std::endl;
}

int main(int argc, char **argv) {
  int f = 256, prec_n = 10;
  long long n = 1000;
  string path = "vector.tree";
  bool verbose = false;
  bool populate = true;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--features") == 0) {
      f = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--help") == 0) {
      help();
      return EXIT_FAILURE;
    } else if (strcmp(argv[i], "--nodes") == 0) {
      n = atoll(argv[++i]);
    } else if (strcmp(argv[i], "--path") == 0) {
      path = string(argv[++i]);
    } else if (strcmp(argv[i], "--verbose") == 0) {
      verbose = true;
    } else if (strcmp(argv[i], "--test_count") == 0) {
      prec_n = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--populate") == 0) {
      populate = strcmp(argv[++i], "true") == 0 ? true : false;
    } else {
      // this is for pmreorder which provides the pmem data file as the single parameter
      if (argc == 2) {
        path = string(argv[i]);
        if (remove(path.c_str()) == 0) {
          std::cout << "deleted " << path << std::endl;
        } else {
          std::cout << "not found " << path << std::endl;
        }
      } else {
        std::cout << "Unregonized arguments: " << argv[i] << std::endl;
        help();
        return EXIT_FAILURE;
      }
    }
  }

  feedback(path, f, n, prec_n, populate);
  precision(path, f, n, prec_n, verbose, populate);
  return EXIT_SUCCESS;
}
