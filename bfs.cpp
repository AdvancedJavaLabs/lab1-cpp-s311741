#include "digraph.hpp"
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

static void reset_depths(const digraph& g [[maybe_unused]],
                         std::span<int> depths,
                         int start_vert) {
  assert(std::ssize(depths) == g.num_verts());
  std::fill_n(depths.begin(), start_vert, -1);
  depths[start_vert] = 0;
  std::fill(depths.begin() + start_vert + 1, depths.end(), -1);
}

void bfs(const digraph& g, std::span<int> depths) {
  reset_depths(g, depths, 0);
  std::queue<int> q;
  q.push(0);
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
    constexpr static int max_size = 256;
    int verts[max_size];
  };

  int load_depth(int vert) {
    return __atomic_load_n(&depths[vert], __ATOMIC_SEQ_CST);
  }

  bool weak_cas_depth(int vert, int* expected, int desired) {
    return __atomic_compare_exchange_n(
      &depths[vert], expected, desired,
      true, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  }

  struct {
    std::mutex mutex;
    std::condition_variable more;
    std::queue<std::unique_ptr<block>> queue;
    int idle = 0;
    bool done = false;
  } q;

  std::vector<std::jthread> workers;

  explicit parbfs(int n_threads, const digraph& g, std::span<int> depths):
    g(g),
    depths(depths),
    workers(n_threads)
  {
    auto initial = std::make_unique<block>();
    initial->verts[0] = 0;
    initial->verts[1] = -1;
    q.queue.push(std::move(initial));

    for (int i = 0; i < n_threads; ++i) {
      workers[i] = std::jthread(&parbfs::worker, this);
    }
  }

  std::unique_ptr<block> pop_block() {
    std::unique_lock lk(q.mutex);
    if (++q.idle == std::ssize(workers) && q.queue.empty()) {
      q.done = true;
      q.more.notify_all();
      return nullptr;
    }
    q.more.wait(lk, [&] { return !q.queue.empty() || q.done; });
    if (q.done) {
      assert(q.queue.empty());
      return nullptr;
    }
    auto result = std::move(q.queue.front());
    q.queue.pop();
    --q.idle;
    return result;
  }

  void push_block(std::unique_ptr<block> block) {
    std::unique_lock lk(q.mutex);
    q.queue.push(std::move(block));
    q.more.notify_one();
  }

  void worker() {
    int out_size = 0;
    std::unique_ptr<block> out = nullptr;

    auto push_out = [&] {
      if (out) {
        if (out_size != block::max_size) {
          out->verts[out_size] = -1;
        }
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
      out->verts[out_size++] = vert;
      if (out_size == block::max_size) {
        push_out();
      }
    };

    auto process_vert = [&](int src) {
      const int src_depth = load_depth(src);
      const int new_depth = src_depth + 1;
      for (int dst: g.adj[src]) {
        int dst_depth = load_depth(dst);
        if (dst_depth != -1 && dst_depth <= new_depth) {
          continue;
        }
        do {
          if (weak_cas_depth(dst, &dst_depth, new_depth)) {
            push_vert(dst);
            break;
          }
          assert(dst_depth != -1);
        } while (dst_depth > new_depth);
      }
    };

    while (auto in = pop_block()) {
      for (int src: in->verts) {
        if (src == -1) { break; }
        process_vert(src);
      }
      push_out();
    }
  }
};

void parallel_bfs(int n_threads, const digraph& g, std::span<int> depths) {
  reset_depths(g, depths, 0);
  parbfs parbfs(n_threads, g, depths);
}