/*
Copyright 2011 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Author: lode.vandevenne@gmail.com (Lode Vandevenne)
Author: jyrki.alakuijala@gmail.com (Jyrki Alakuijala)
*/

/*
Bounded package merge algorithm, based on the paper
"A Fast and Space-Economical Algorithm for Length-Limited Coding
Jyrki Katajainen, Alistair Moffat, Andrew Turpin".
*/

/*Modified by Felix Hanau*/

#include "katajainen.h"
#include "util.h"

#include <stdlib.h>
#include <assert.h>
#include <algorithm>

/* Sorting these keys orders leaves by frequency, then by symbol number. */
static size_t LeafWeight(size_t key) { return key >> 9; }
static unsigned LeafSymbol(size_t key) { return key & 511; }

static void SortLeaves(size_t* leaves, int n) {
  if (n < 64) {
    std::sort(leaves, leaves + n);
    return;
  }

  // Splitter probes often have many small counts. Place those directly in
  // frequency order. Stability preserves symbol order for equal counts.
  constexpr unsigned overflow = 64;
  unsigned short offsets[overflow + 1] = {};
  size_t sorted[288];
  for (int i = 0; i < n; ++i) {
    ++offsets[std::min<size_t>(LeafWeight(leaves[i]), overflow)];
  }
  unsigned start = 0;
  for (auto& offset : offsets) {
    unsigned count = offset;
    offset = start;
    start += count;
  }
  for (int i = 0; i < n; ++i) {
    unsigned bucket = std::min<size_t>(LeafWeight(leaves[i]), overflow);
    sorted[offsets[bucket]++] = leaves[i];
  }
  std::sort(sorted + offsets[overflow - 1], sorted + n);
  memcpy(leaves, sorted, n * sizeof(*leaves));
}

static bool UnboundedLengths(const size_t* leaves, int numsymbols, int maxbits,
                             unsigned* bitlengths) {
  size_t weights[288];
  unsigned short leaves_before[288];
  int leaf = 0, branch = 0;
  for (int next = 0; next < numsymbols - 1; ++next) {
    leaves_before[next] = leaf;
    weights[next] = SIZE_MAX;  // End of the queue of available branches.
    size_t weight = 0;
    for (int child = 0; child < 2; ++child) {
      // Take a branch on ties, as in package-merge.
      if (leaf < numsymbols && LeafWeight(leaves[leaf]) < weights[branch]) {
        weight += LeafWeight(leaves[leaf++]);
      } else {
        weight += weights[branch++];
      }
    }
    weights[next] = weight;
  }

  // Each depth consumes contiguous ranges from both queues. At the start
  // of merge i, 2*i children have been taken, of which leaves_before[i]
  // are leaves. The rest are branches, giving the next level's start.
  int first = numsymbols - 2;  // Root merge.
  int end_leaf = numsymbols;
  for (int depth = 1; ; ++depth) {
    int begin_leaf = leaves_before[first];
    for (int i = begin_leaf; i < end_leaf; ++i) {
      bitlengths[LeafSymbol(leaves[i])] = depth;
    }
    if (first == 0) return true;
    if (depth == maxbits) return false;  // Package-merge replaces these lengths.
    first = 2 * first - begin_leaf;
    end_leaf = begin_leaf;
  }
}

// The lowest list in package-merge contains just leaves. To form the next
// list, merge the leaves with sorted packages (pairs from the previous list).
// Only pair weights and the number of leaves in each even prefix are needed.
static void BoundedLengths(const size_t* leaves, int n, int maxbits,
                           unsigned* bitlengths) {
  size_t leaf_weights[289];
  for (int i = 0; i < n; ++i) leaf_weights[i] = LeafWeight(leaves[i]);
  leaf_weights[n] = SIZE_MAX;

  size_t buffers[2][289];
  size_t* packages = buffers[0];
  size_t* next = buffers[1];
  int pairs = n / 2;
  for (int i = 0; i < pairs; ++i) {
    packages[i] = leaf_weights[2 * i] + leaf_weights[2 * i + 1];
  }
  unsigned short prefix_leaves[15][288];
  for (int level = 1; level < maxbits; ++level) {
    packages[pairs] = SIZE_MAX;
    pairs = (n + pairs) / 2;
    int leaf = 0, package = 0;
    for (int i = 0; i < pairs; ++i) {
      size_t weight = 0;
      for (int child = 0; child < 2; ++child) {
        if (leaf_weights[leaf] < packages[package]) {
          weight += leaf_weights[leaf++];
        } else {
          weight += packages[package++];
        }
      }
      next[i] = weight;
      prefix_leaves[level][i] = leaf;
    }
    std::swap(packages, next);
  }

  // Select the cheapest 2*n-2 items of the top list. Each selected package
  // selects two items one level down. Leaves selected at a given level are
  // a prefix in weight order and each selection adds one bit of code length.
  int selected_pairs = n - 1;
  unsigned ends[289] = {};
  for (int level = maxbits - 1; level > 0 && selected_pairs; --level) {
    int leaf = prefix_leaves[level][selected_pairs - 1];
    ++ends[leaf];
    selected_pairs = 2 * selected_pairs - leaf;
  }
  ++ends[2 * selected_pairs];  // Bottom list contains only leaves.
  unsigned depth = 0;
  for (int i = n; i > 0; --i) {
    depth += ends[i];
    bitlengths[LeafSymbol(leaves[i - 1])] = depth;
  }
}

void ZopfliLengthLimitedCodeLengths(const size_t* frequencies, int n, int maxbits, unsigned* bitlengths) {
  int i;
  int numsymbols = 0;  /* Amount of symbols with frequency > 0. */

  /* One leaf per symbol. Only numsymbols leaves will be used. */
  size_t leaves[288];

  /* Initialize all bitlengths at 0. */
  memset(bitlengths, 0, n * sizeof(unsigned));

  /* Count used symbols and place them in the leaves. */
  for (i = 0; i < n; i++) {
    if (frequencies[i]) {
      leaves[numsymbols++] = (frequencies[i] << 9) | i;
    }
  }

  /* Check special cases and error conditions. */
  assert((1 << maxbits) >= numsymbols); /* Error, too few maxbits to represent symbols. */
  if (numsymbols == 0) {
    return;  /* No symbols at all. OK. */
  }
  if (numsymbols == 1) {
    bitlengths[LeafSymbol(leaves[0])] = 1;
    return;  /* Only one symbol, give it bitlength 1, not 0. OK. */
  }
  if (numsymbols == 2){
    bitlengths[LeafSymbol(leaves[0])]++;
    bitlengths[LeafSymbol(leaves[1])]++;
    return;
  }

  SortLeaves(leaves, numsymbols);

  if (UnboundedLengths(leaves, numsymbols, maxbits, bitlengths)) return;

  BoundedLengths(leaves, numsymbols, maxbits, bitlengths);
}
