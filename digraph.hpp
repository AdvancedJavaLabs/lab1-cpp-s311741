#pragma once
#include "svo.hpp"
#include <cassert>
#include <span>
#include <vector>

struct digraph {
  std::vector<svo_vector<int>> adj;
  int num_edges = 0;

  explicit digraph(int verts): adj(verts) {}

  int num_verts() const { return std::ssize(adj); }

  bool maybe_add_edge(int from, int to) {
    assert(from >= 0 && from < num_verts());
    assert(to >= 0 && to < num_verts());
    if (from != to) {
      auto& v = adj[from];
      if (std::find(v.begin(), v.end(), to) == v.end()) {
        v.emplace_back(to);
        ++num_edges;
        return true;
      }
    }
    return false;
  }
};

void bfs(const digraph&, std::span<int> depths);
void parallel_bfs(int n_threads, const digraph&, std::span<int> depths);