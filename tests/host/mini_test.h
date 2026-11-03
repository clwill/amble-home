// Minimal host test runner for amble's pure logic. No ESPHome, no IDF.
#pragma once
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

namespace mini {
struct Case { const char *name; void (*fn)(); };
inline std::vector<Case> &cases() { static std::vector<Case> c; return c; }
inline int &failures() { static int f = 0; return f; }
struct Reg { Reg(const char *n, void (*f)()) { cases().push_back({n, f}); } };
}  // namespace mini

#define TEST(name) static void name(); static mini::Reg reg_##name(#name, name); static void name()
#define CHECK(cond) do { if (!(cond)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); mini::failures()++; } } while (0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { std::printf("  FAIL %s:%d: %s == %s (got %lld vs %lld)\n", __FILE__, __LINE__, #a, #b, (long long) _a, (long long) _b); mini::failures()++; } } while (0)

#ifdef MINI_TEST_MAIN
int main() {
  int passed = 0;
  for (auto &c : mini::cases()) { int before = mini::failures(); c.fn(); if (mini::failures() == before) passed++; else std::printf("in %s\n", c.name); }
  std::printf("%d passed, %d failed\n", passed, (int) mini::cases().size() - passed);
  return mini::failures() ? 1 : 0;
}
#endif
