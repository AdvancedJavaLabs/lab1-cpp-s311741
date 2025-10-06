#include "digraph.hpp"
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

void bfs(const digraph& g, std::span<int> depths) {
  assert(std::ssize(depths) == g.num_verts());
  std::queue<int> q;
  q.push(0);
  depths[0] = 0;
  std::fill(depths.begin()+1, depths.end(), -1);
  do {
    int v = q.front();
    q.pop();
    for (int n: g.adj[v]) {
      if (depths[n] == -1) {
        depths[n] = depths[v] + 1;
        q.push(n);
      }
    }
  } while (!q.empty());
}

struct parbfs {
  const digraph& g;
  std::span<int> depths;

  struct block {
    constexpr static int max_size = 128;
    int entries[max_size];

    explicit block() {
      std::fill(std::begin(entries), std::end(entries), -1);
    }

    int size() const {
      return std::count_if(
        std::begin(entries), std::end(entries),
        [](int i) { return i != -1; });
    }
  };

  struct {
    std::mutex mutex;
    std::condition_variable more;
    std::queue<std::unique_ptr<block>> blocks;
    int empty_workers = 0;
    bool done = false;
  } q;

  std::vector<std::jthread> threads;

  explicit parbfs(int n_threads, const digraph& g, std::span<int> depths):
    g(g),
    depths(depths),
    threads(n_threads)
  {
    auto initial = std::make_unique<block>();
    initial->entries[0] = 0;
    q.blocks.push(std::move(initial));

    for (int i = 0; i < n_threads; ++i) {
      threads[i] = std::jthread(&parbfs::worker, this);
    }
  }

  std::unique_ptr<block> pop_block() {
    std::unique_lock lk(q.mutex);
    if (++q.empty_workers == std::ssize(threads) && q.blocks.empty()) {
      q.done = true;
      q.more.notify_all();
      return nullptr;
    }
    q.more.wait(lk, [&] { return !q.blocks.empty() || q.done; });
    if (q.done) {
      return nullptr;
    }
    auto result = std::move(q.blocks.front());
    q.blocks.pop();
    --q.empty_workers;
    return result;
  }

  void push_block(std::unique_ptr<block> block) {
    std::unique_lock lk(q.mutex);
    q.blocks.push(std::move(block));
    q.more.notify_one();
  }

  constexpr static int seqcst = __ATOMIC_SEQ_CST;

  void worker() {
    int out_size = 0;
    std::unique_ptr<block> out = nullptr;

    auto push_out = [&] {
      if (out) {
        push_block(std::move(out));
        out_size = 0;
      }
    };

    auto push_vert = [&](int vert) {
      if (!out) {
        assert(out_size == 0);
        out = std::make_unique<block>();
      }
      assert(out_size < block::max_size);
      out->entries[out_size++] = vert;
      if (out_size == block::max_size) {
        push_out();
      }
    };

    auto process_vert = [&](int src) {
      const int src_depth = __atomic_load_n(&depths[src], seqcst);
      const int new_depth = src_depth + 1;
      for (int dst: g.adj[src]) {
        int dst_depth = __atomic_load_n(&depths[dst], seqcst);
        if (dst_depth != -1 && dst_depth <= new_depth) {
          continue;
        }
      cmpxchg:
        bool updated = __atomic_compare_exchange_n(
          &depths[dst], &dst_depth, new_depth,
          true, seqcst, seqcst);
        if (updated) {
          push_vert(dst);
        } else {
          assert(dst_depth != -1);
          if (dst_depth > new_depth) {
            goto cmpxchg;
          }
        }
      }
    };

    while (auto in = pop_block()) {
      for (int src: in->entries) {
        if (src == -1) { break; }
        process_vert(src);
      }
      push_out();
    }
  }
};

void parallel_bfs(int n_threads, const digraph& g, std::span<int> depths) {
  depths[0] = 0;
  std::fill(depths.begin()+1, depths.end(), -1);
  assert(std::ssize(depths) == g.num_verts());
  parbfs parbfs(n_threads, g, depths);
}