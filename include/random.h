#ifndef KISSRANDOM_H
#define KISSRANDOM_H

#if defined(_MSC_VER) && _MSC_VER == 1500
typedef unsigned __int32    uint32_t;
typedef unsigned __int64    uint64_t;
#else
#include <stdint.h>
#endif

// KISS = "keep it simple, stupid", but high quality random number generator
// http://www0.cs.ucl.ac.uk/staff/d.jones/GoodPracticeRNG.pdf -> "Use a good RNG and build it into your code"
// http://mathforum.org/kb/message.jspa?messageID=6627731
// https://de.wikipedia.org/wiki/KISS_(Zufallszahlengenerator)

struct Random {
  uint32_t x;
  uint32_t y;
  uint32_t z;
  uint32_t c;

  // seed must be != 0
  Random(uint32_t seed = 123456789) {
    x = seed;
    y = 362436000;
    z = 521288629;
    c = 7654321;
  }

  uint32_t rand() {
    // Linear congruence generator
    x = 69069 * x + 12345;

    // Xor shift
    y ^= y << 13;
    y ^= y >> 17;
    y ^= y << 5;

    // Multiply-with-carry
    uint64_t t = 698769069ULL * z + c;
    c = t >> 32;
    z = (uint32_t) t;

    return x + y + z;
  }
  inline int flip() {
    // Draw random 0 or 1
    return rand() & 1;
  }
  inline size_t index(size_t n) {
    // Draw random integer between 0 and n-1 where n is at most the number of data points you have
    return rand() % n;
  }
  inline void set_seed(uint32_t seed) {
    x = seed;
  }
};

#endif
