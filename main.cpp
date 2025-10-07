#include "digraph.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <fmt/chrono.h>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fstream>
#include <numeric>

namespace {
#if 1
struct xorshift128 {
  using result_type = uint64_t;
  constexpr static result_type max() { return UINT64_MAX; }
  constexpr static result_type min() { return 0; }

  explicit xorshift128() = default;
  explicit xorshift128(uint64_t s): a(s), b(s) {}
  explicit xorshift128(uint64_t a, uint64_t b): a(a), b(b) {}

  uint64_t a = 0xfe48ec23c5fb18e0;
  uint64_t b = 0xac5f64acb55eda12;

  result_type operator()() {
    uint64_t x = a, y = b;
    a = b;
    x ^= x << 23;
    b = x ^ y ^ (x >> 17) ^ (y >> 26);
    return b + y;
  }
};
using rng = xorshift128;
#else
#include <random>
using rng = std::mt19937_64;
#endif

template<typename Rng>
[[gnu::noinline]]
digraph make_random_digraph(Rng& rng, int n_verts, int n_edges) {
  assert(n_verts > 1);
  assert(n_edges >= n_verts-1);
  assert(n_edges <= long(n_verts) * (n_verts - 1));

  digraph g(n_verts);

  {
    std::vector<int> perm(n_verts);
    std::iota(perm.begin(), perm.end(), 0);
    std::shuffle(perm.begin(), perm.end(), rng);
    for (int i = 1; i < n_verts; ++i) {
      g.maybe_add_edge(perm[i-1], perm[i]);
    }
    if (n_edges >= n_verts) {
      g.maybe_add_edge(perm[n_verts-1], perm[0]);
    }
  }

  std::uniform_int_distribution<int> dist(0, n_verts-1);

#if 1
  for (int from = 0; from < n_verts; ++from) {
    int wantout = std::min(n_verts-1, n_edges / n_verts);
    while (std::ssize(g.adj[from]) < wantout) {
      g.maybe_add_edge(from, dist(rng));
    }
  }
#endif

  while (g.num_edges < n_edges) {
    g.maybe_add_edge(dist(rng), dist(rng));
  }

  assert(g.num_edges == n_edges);
  return g;
}

struct timer {
  using clock = std::chrono::steady_clock;
  clock::time_point started = clock::now();

  using dmilliseconds = std::chrono::duration<double, std::milli>;

  dmilliseconds measure() const {
    return std::chrono::duration_cast<dmilliseconds>(clock::now() - started);
  };
};
} // namespace

int main() {
  constexpr static struct { int v, e; } configs[] = {
    { 10, 50 },
    { 100, 500 },
    { 1000, 5000 },
    { 10'000, 50'000 },
    { 50'000, 1000'000 },
    { 100'000, 1000'000 },
    { 250'000, 250'000 },
    { 2000'000, 10'000'000 },
    { 20'000'000, 50'000'000 },
    { 20'000'000, 100'000'000 },
    { 20'000'000, 500'000'000 },
  };

  std::vector<int> depths_seq(20'000'000);
  std::vector<int> depths_par(20'000'000);
  constexpr int n_threads = 4;

  std::ofstream csv("out.csv");
  csv << "v,e,buildtime,seqtime,partime,threads\n";

  for (auto [v, e]: configs) {
    rng rng;
    timer build_timer;
    digraph g = make_random_digraph(rng, v, e);
    auto build_time = build_timer.measure();

    auto seq_span = std::span(depths_seq).subspan(0, v);
    auto par_span = std::span(depths_par).subspan(0, v);

    timer seq_timer;
    bfs(g, seq_span);
    auto seq_time = seq_timer.measure();

    timer par_timer;
    parallel_bfs(n_threads, g, par_span);
    auto par_time = par_timer.measure();

    bool equal = std::ranges::equal(seq_span, par_span);

    constexpr auto green = fg(fmt::color::green);
    constexpr auto red = fg(fmt::color::red);
    using namespace std::literals;

    fmt::print(
      "{}v / {}e\tseq bfs: {}\tpar bfs ({} threads): {}.\tresult {}\n",
      v, e,
      styled(seq_time, seq_time < par_time ? green : red),
      n_threads,
      styled(par_time, par_time < seq_time ? green : red),
      equal ? styled("matches"sv, green) : styled("mismatch"sv, red));
    csv << fmt::format("{},{},{},{},{},{}\n",
      v, e, build_time.count(), seq_time.count(), par_time.count(), n_threads);
  }
}