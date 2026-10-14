#pragma once
// Minimal test helpers (no external test library).

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace bbtest
{
  struct Case
  {
    const char* name;
    std::function<void()> fn;
  };

  inline std::vector<Case>& cases()
  {
    static std::vector<Case> c;
    return c;
  }

  inline int& failures()
  {
    static int f = 0;
    return f;
  }

  struct Registrar
  {
    Registrar(const char* name, std::function<void()> fn) { cases().push_back({ name, std::move(fn) }); }
  };

  inline int runAll()
  {
    int failedCases = 0;
    for (const auto& c : cases())
    {
      const int before = failures();
      c.fn();
      const bool ok = failures() == before;
      if (!ok)
        ++failedCases;
      std::printf("[%s] %s\n", ok ? " OK " : "FAIL", c.name);
    }
    std::printf("%zu cases, %d failed, %d failed checks\n", cases().size(), failedCases, failures());
    return failures() == 0 ? 0 : 1;
  }
}

#define BB_CONCAT_INNER(a, b) a##b
#define BB_CONCAT(a, b) BB_CONCAT_INNER(a, b)
#define TEST_CASE(name)                                                              \
  static void BB_CONCAT(test_fn_, __LINE__)();                                       \
  static bbtest::Registrar BB_CONCAT(test_reg_, __LINE__)(name, BB_CONCAT(test_fn_, __LINE__)); \
  static void BB_CONCAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                                  \
  do {                                                                               \
    if (!(cond)) {                                                                   \
      ++bbtest::failures();                                                          \
      std::printf("    %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);       \
    }                                                                                \
  } while (0)

#define CHECK_EQ(a, b)                                                               \
  do {                                                                               \
    const auto va = (a);                                                             \
    const auto vb = (b);                                                             \
    if (!(va == vb)) {                                                               \
      ++bbtest::failures();                                                          \
      std::printf("    %s:%d: CHECK_EQ(%s, %s) failed: %s vs %s\n", __FILE__, __LINE__, #a, #b, \
                  std::to_string(va).c_str(), std::to_string(vb).c_str());           \
    }                                                                                \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                        \
  do {                                                                               \
    const double va = (a), vb = (b);                                                 \
    if (std::fabs(va - vb) > (eps)) {                                                \
      ++bbtest::failures();                                                          \
      std::printf("    %s:%d: CHECK_NEAR(%s, %s) failed: %f vs %f\n", __FILE__, __LINE__, #a, #b, va, vb); \
    }                                                                                \
  } while (0)
