#include "lib/extras/include_jpeglib.h"
#include "lib/jpegli/bitstream.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iterator>

#if HWY_ONCE

namespace jpegli {
void ZigZagShuffleBlocks(j_compress_ptr cinfo);

void EstimateScanCosts(j_compress_ptr cinfo, const jpeg_scan_info *scans, size_t count,
                       const size_t *original_blocks, uint8_t **output, size_t *costs) {
  auto *m = cinfo->master;
  ZigZagShuffleBlocks(cinfo);
  // Probe images use identical dimensions for every component. Reuse one
  // scratch buffer across all scans, including alternative scans of a band.
  const size_t blocks =
      size_t(cinfo->comp_info[0].width_in_blocks) * cinfo->comp_info[0].height_in_blocks;
  const size_t capacity = blocks * (DCTSIZE2 + 2) + 1;
  m->token_arrays = Allocate<TokenArray>(cinfo, 1, JPOOL_IMAGE);
  auto &array = m->token_arrays[0];
  array.tokens = Allocate<Token>(cinfo, capacity, JPOOL_IMAGE);
  auto *refs = Allocate<RefToken>(cinfo, capacity, JPOOL_IMAGE);
  auto *refbits = Allocate<uint8_t>(cinfo, capacity, JPOOL_IMAGE);
  m->ac_ctx_offset[0] = 0;
  m->context_map = m->ac_ctx_offset;
  m->coding_tables = Allocate<HuffmanCodeTable>(cinfo, 1, JPOOL_IMAGE);
  cinfo->num_scans = 1;
  for (size_t i = 0; i < count; ++i) {
    auto &info = m->scan_token_info[0];
    info = {};
    cinfo->scan_info = &scans[i];
    array.num_tokens = 0;
    m->num_tokens = capacity;
    m->next_token = array.tokens;
    m->next_refinement_token = refs;
    m->next_refinement_bit = refbits;
    TokenizeScan(cinfo, 0, 0, &info);
    Histogram hist;
    BuildHistograms(cinfo, &hist);
    JHUFF_TBL huff{};
    BuildJpegHuffmanTable(hist, &huff);
    BuildHuffmanCodeTable(huff, m->coding_tables);
    // Include byte stuffing, which depends on the actual bits. Use the same
    // writer as the final JPEG and recycle the memory destination per scan.
    auto *dest = cinfo->dest;
    dest->free_in_buffer += dest->next_output_byte - *output;
    dest->next_output_byte = *output;
    WriteScanData(cinfo, 0);
    size_t entropy_bytes = dest->next_output_byte - *output;
    size_t header = 10 + 4 + 17; // noninterleaved SOS + DHT overhead
    for (int k = 1; k <= 16; ++k)
      header += huff.bits[k];
    const size_t total = original_blocks[scans[i].component_index[0]];
    costs[i] = header + (uint64_t(entropy_bytes) * total + blocks / 2) / blocks;
  }
  cinfo->scan_info = nullptr;
}

// Equal-length Huffman codes are interchangeable. Select the ordering that
// emits fewer stuffed FF bytes, keeping jpegli's lengths and shared tables.
void WriteOptimizedCoefficients(j_compress_ptr cinfo, uint8_t **output) {
  auto *m = cinfo->master;
  ZigZagShuffleBlocks(cinfo);
  TokenizeJpeg(cinfo);
  OptimizeHuffmanCodes(cinfo);
  InitEntropyCoder(cinfo);
  auto *dest = cinfo->dest;
  const size_t start = dest->next_output_byte - *output;
  auto rewind = [&]() {
    size_t bytes = dest->next_output_byte - (*output + start);
    dest->free_in_buffer += bytes;
    dest->next_output_byte = *output + start;
    return bytes;
  };
  for (size_t t = 0; t < m->num_huffman_tables; ++t) {
    if (m->slot_id_map[t] < 16)
      continue;
    auto &table = m->huffman_tables[t];
    JHUFF_TBL original = table, best = table;
    size_t smallest = SIZE_MAX;
    for (int order = 0; order < 3; ++order) {
      table = original;
      uint8_t *symbols = table.huffval;
      for (int d = 1; d <= 16; ++d) {
        auto *end = symbols + table.bits[d];
        if (order == 1)
          std::reverse(symbols, end);
        if (order == 2 && symbols != end)
          std::rotate(symbols, symbols + (end - symbols) / 2, end);
        symbols = end;
      }
      BuildHuffmanCodeTable(table, &m->coding_tables[t]);
      for (int i = 0; i < cinfo->num_scans; ++i)
        if (cinfo->scan_info[i].Ss > 0 && m->context_map[m->ac_ctx_offset[i]] == t)
          WriteScanData(cinfo, i);
      size_t bytes = rewind();
      if (bytes < smallest) {
        smallest = bytes;
        best = table;
      }
    }
    table = best;
    BuildHuffmanCodeTable(table, &m->coding_tables[t]);
  }
  WriteFrameHeader(cinfo);
  for (int i = 0; i < cinfo->num_scans; ++i) {
    WriteScanHeader(cinfo, i);
    WriteScanData(cinfo, i);
  }
  WriteOutput(cinfo, {0xff, 0xd9});
  dest->term_destination(cinfo);
}

} // namespace jpegli

#endif

namespace jpegli {
void WriteOptimizedCoefficients(j_compress_ptr, uint8_t **);
void EstimateScanCosts(j_compress_ptr, const jpeg_scan_info *, size_t, const size_t *, uint8_t **,
                       size_t *);
} // namespace jpegli

namespace {

struct Error {
  jpeg_error_mgr pub;
  jmp_buf jump;

  jpeg_error_mgr *setup() {
    jpeg_std_error(&pub);
    pub.error_exit = [](j_common_ptr c) {
      c->err->output_message(c);
      longjmp(reinterpret_cast<Error *>(c->err)->jump, 1);
    };
    return &pub;
  }
};

struct Decoder {
  jpeg_decompress_struct src{};
  Error error;
  std::vector<JBLOCKROW> rows[MAX_COMPS_IN_SCAN];
  std::vector<jpeg_saved_marker_ptr> markers;
  ~Decoder() { jpeg_destroy_decompress(&src); }

  bool read(const uint8_t *input, size_t size) {
    src.err = error.setup();
    if (setjmp(error.jump))
      return false;
    jpeg_create_decompress(&src);
    jpeg_mem_src(&src, input, size);
    // jpegli normally supplies a synthetic EOI at EOF.
    src.src->fill_input_buffer = [](j_decompress_ptr) -> boolean { return FALSE; };
    for (int i = 0; i < 16; ++i)
      jpeg_save_markers(&src, JPEG_APP0 + i, 65535);
    jpeg_save_markers(&src, JPEG_COM, 65535);
    if (jpeg_read_header(&src, TRUE) != JPEG_HEADER_OK)
      return false;
    if (src.num_components < 1 || src.num_components > MAX_COMPS_IN_SCAN)
      return false;
    auto coeffs = jpeg_read_coefficients(&src);
    if (!coeffs || src.err->num_warnings != 0)
      return false;
    for (int c = 0; c < src.num_components; ++c) {
      for (JDIMENSION y = 0; y < src.comp_info[c].height_in_blocks; ++y)
        rows[c].push_back(*src.mem->access_virt_barray(reinterpret_cast<j_common_ptr>(&src),
                                                       coeffs[c], y, 1, FALSE));
    }
    // This jpegli version prepends saved markers, including multi-part ICC profiles.
    for (auto m = src.marker_list; m; m = m->next)
      markers.push_back(m);
    std::reverse(markers.begin(), markers.end());
    return true;
  }
};

bool tagged(jpeg_saved_marker_ptr m, int app, const char *tag, size_t size) {
  return m->marker == JPEG_APP0 + app && m->data_length >= size && !memcmp(m->data, tag, size);
}

using Scans = std::vector<jpeg_scan_info>;

jpeg_scan_info scan(int component, int start, int end, int ah = 0, int al = 0) {
  jpeg_scan_info s{};
  s.comps_in_scan = 1;
  s.component_index[0] = component;
  s.Ss = start;
  s.Se = end;
  s.Ah = ah;
  s.Al = al;
  return s;
}

Scans dc_scans(int components, int group_from) {
  Scans scans;
  for (int c = 0; c < components;) {
    auto s = scan(c, 0, 0);
    ++c;
    if (c > group_from) {
      while (c < components)
        s.component_index[s.comps_in_scan++] = c++;
    }
    scans.push_back(s);
  }
  return scans;
}

struct Encoder {
  jpeg_compress_struct dst{};
  Error error;
  uint8_t *data = nullptr;
  unsigned long size = 0;
  ~Encoder() {
    jpeg_destroy_compress(&dst);
    free(data);
  }

  bool write(Decoder &in, const Scans &scans, bool strip, unsigned sample_rows = 0,
             size_t *costs = nullptr) {
    dst.err = error.setup();
    if (setjmp(error.jump))
      return false;
    jpeg_create_compress(&dst);
    jpeg_mem_dest(&dst, &data, &size);
    jpeg_copy_critical_parameters(&in.src, &dst);
    // A compact coefficient image lets jpegli measure candidate scans with its
    // own tokenizer and bit writer. Rates are scaled by the original block counts.
    constexpr unsigned sample_width = 32;
    if (sample_rows) {
      dst.image_width = sample_width * DCTSIZE;
      dst.image_height = sample_rows * DCTSIZE;
      for (int c = 0; c < dst.num_components; ++c)
        dst.comp_info[c].h_samp_factor = dst.comp_info[c].v_samp_factor = 1;
    }
    dst.optimize_coding = TRUE;
    dst.write_JFIF_header = dst.write_Adobe_marker = FALSE;
    dst.scan_info = sample_rows || scans.empty() ? nullptr : scans.data();
    dst.num_scans = sample_rows ? 0 : static_cast<int>(scans.size());

    // jpegli_finish_compress zigzags these arrays in place. Keep the decoded
    // coefficients in natural order for subsequent candidates.
    auto d = reinterpret_cast<j_common_ptr>(&dst);
    jvirt_barray_ptr copy[MAX_COMPONENTS];
    for (int c = 0; c < dst.num_components; ++c) {
      const auto &comp = in.src.comp_info[c];
      copy[c] = dst.mem->request_virt_barray(
          d, JPOOL_IMAGE, FALSE, sample_rows ? sample_width : comp.width_in_blocks,
          sample_rows ? sample_rows : comp.height_in_blocks, comp.v_samp_factor);
    }
    dst.mem->realize_virt_arrays(d);
    for (int c = 0; c < dst.num_components; ++c) {
      const auto &comp = in.src.comp_info[c];
      if (sample_rows) {
        // Stratify over the whole image, keeping short runs of adjacent blocks
        // together so the probe retains local end-of-band runs.
        constexpr unsigned run = 8;
        size_t total = size_t(comp.width_in_blocks) * comp.height_in_blocks;
        size_t samples = sample_rows * sample_width / run;
        for (unsigned y = 0; y < sample_rows; ++y) {
          auto row = *dst.mem->access_virt_barray(d, copy[c], y, 1, TRUE);
          for (unsigned x = 0; x < sample_width; x += run) {
            size_t i = (y * sample_width + x) / run;
            // Jitter each stratum to avoid aliasing with the block-row width.
            uint32_t jitter = (uint32_t(i) * 2654435761u) >> 16;
            size_t pos = ((uint64_t(i) * 65536 + jitter) * total) / (samples * 65536);
            for (unsigned k = 0; k < run; ++k) {
              size_t j = std::min(pos + k, total - 1);
              memcpy(row[x + k], in.rows[c][j / comp.width_in_blocks][j % comp.width_in_blocks],
                     sizeof(JBLOCK));
            }
          }
        }
      } else {
        for (JDIMENSION y = 0; y < comp.height_in_blocks; ++y) {
          auto to = dst.mem->access_virt_barray(d, copy[c], y, 1, TRUE);
          memcpy(*to, in.rows[c][y], comp.width_in_blocks * sizeof(JBLOCK));
        }
      }
    }
    if (!sample_rows && scans.empty())
      jpeg_simple_progression(&dst);
    jpeg_write_coefficients(&dst, copy);
    if (sample_rows) {
      size_t blocks[MAX_COMPS_IN_SCAN];
      for (int c = 0; c < dst.num_components; ++c)
        blocks[c] =
            size_t(in.src.comp_info[c].width_in_blocks) * in.src.comp_info[c].height_in_blocks;
      jpegli::EstimateScanCosts(&dst, scans.data(), scans.size(), blocks, &data, costs);
      return true;
    }
    for (auto m : in.markers) {
      if (!strip || tagged(m, 0, "JFIF\0", 5) || tagged(m, 14, "Adobe", 5) ||
          tagged(m, 2, "ICC_PROFILE\0", 12))
        jpeg_write_marker(&dst, m->marker, m->data, m->data_length);
    }
    if (dst.progressive_mode && !scans.empty())
      jpegli::WriteOptimizedCoefficients(&dst, &data);
    else
      jpeg_finish_compress(&dst);
    return dst.err->num_warnings == 0;
  }
};
// AC scan boundaries in zigzag order. Probe prefixes, suffixes and adjacent bands.
constexpr int ends[] = {0, 1, 2, 3, 5, 8, 12, 16, 20, 28, 40, 63};
constexpr int n = std::size(ends);
constexpr int max_depth = 4;
struct Costs {
  size_t bytes[MAX_COMPS_IN_SCAN][2][max_depth + 1][n][n]{};

  void record(const jpeg_scan_info &s, size_t rate) {
    auto i = std::lower_bound(ends, ends + n, s.Ss - 1) - ends;
    auto j = std::lower_bound(ends, ends + n, s.Se) - ends;
    bytes[s.component_index[0]][s.Ah != 0][s.Al][i][j] = rate;
  }

  Scans choose(int components) const {
    Scans selected;
    for (int c = 0; c < components; ++c) {
      struct Plan {
        uint64_t cost = UINT64_MAX;
        Scans scans;
        int first_depth = 0, max_initial_depth = 0;
      };
      std::vector<Plan> table((max_depth + 1) * n * n);
      auto plan = [&](int b, int i, int j) -> Plan & { return table[(b * n + i) * n + j]; };
      // Each interval can start at bit b, refine a coarser approximation,
      // or split into two independent intervals at this bit plane.
      for (int b = max_depth; b >= 0; --b) {
        for (int i = n - 2; i >= 0; --i) {
          for (int j = i + 1; j < n; ++j) {
            auto &p = plan(b, i, j);
            int first = ends[i] + 1, last = ends[j];
            size_t base = bytes[c][0][b][i][j];
            if (base)
              p = {base, {scan(c, first, last, 0, b)}, b, b};
            size_t ref = bytes[c][1][b][i][j];
            if (ref && b < max_depth) {
              const auto &coarse = plan(b + 1, i, j);
              // jpegli schedules refinement scratch space by the initial
              // depth of the first frequency. Merge only if it is deepest.
              if (coarse.cost != UINT64_MAX && coarse.first_depth == coarse.max_initial_depth &&
                  coarse.cost + ref < p.cost) {
                p = coarse;
                p.cost += ref;
                p.scans.push_back(scan(c, first, last, b + 1, b));
              }
            }
            for (int k = i + 1; k < j; ++k) {
              const auto &left = plan(b, i, k), &right = plan(b, k, j);
              if (left.cost == UINT64_MAX || right.cost == UINT64_MAX ||
                  left.cost + right.cost >= p.cost)
                continue;
              p = left;
              p.cost += right.cost;
              p.scans.insert(p.scans.end(), right.scans.begin(), right.scans.end());
              p.max_initial_depth = std::max(left.max_initial_depth, right.max_initial_depth);
            }
          }
        }
      }
      const auto &ac = plan(0, 0, n - 1).scans;
      selected.insert(selected.end(), ac.begin(), ac.end());
    }
    // Put all first scans before refinements, then refine all components one
    // bit at a time. This also lets jpegli cluster similar Huffman tables.
    std::stable_sort(selected.begin(), selected.end(), [](const auto &a, const auto &b) {
      if (!a.Ah || !b.Ah)
        return !a.Ah && b.Ah;
      return a.Ah > b.Ah;
    });
    return selected;
  }
};
} // namespace

int ect_jpeg(const uint8_t *input, size_t size, int level, bool strip, std::vector<uint8_t> &best) {
  Decoder in;
  if (!in.read(input, size))
    return 1;
  // Trailers (e.g. motion photos) and MPF indexes can refer to file offsets.
  if (in.src.src->bytes_in_buffer != 0)
    return 0;
  for (auto m : in.markers)
    if (tagged(m, 2, "MPF\0", 4))
      return 0;

  const int nc = in.src.num_components;
  // jpegli normalizes single-component sampling to 1x1 on write.
  if (nc == 1 && (in.src.comp_info[0].h_samp_factor != 1 || in.src.comp_info[0].v_samp_factor != 1))
    return 0;
  // A DQT slot may be redefined after a component has latched its table.
  // jpegli copies the final slots on transcode. Keep such originals intact.
  for (int c = 0; c < nc; ++c) {
    const auto &comp = in.src.comp_info[c];
    const auto *slot = in.src.quant_tbl_ptrs[comp.quant_tbl_no];
    if (!comp.quant_table || !slot ||
        memcmp(comp.quant_table->quantval, slot->quantval, sizeof(slot->quantval)))
      return 0;
  }
  auto trial = [&](const Scans &scans) {
    Encoder out;
    if (!out.write(in, scans, strip))
      return false;
    if (out.size < (best.empty() ? size : best.size()))
      best.assign(out.data, out.data + out.size);
    return true;
  };
  auto blocks_in_group = [&](int from) {
    int blocks = 0;
    for (int c = from; c < nc; ++c)
      blocks += in.src.comp_info[c].h_samp_factor * in.src.comp_info[c].v_samp_factor;
    return blocks;
  };
  const bool small = size_t(in.src.image_width) * in.src.image_height < 65536;
  if ((level < 3 || small) && (nc == 1 || blocks_in_group(0) <= C_MAX_BLOCKS_IN_MCU)) {
    auto seq = dc_scans(nc, 0);
    seq[0].Se = 63;
    if (!trial(seq))
      return 1;
  }
  if (level < 3 || small) {
    // Level zero is sequential in this jpegli build. Ask explicitly for
    // the library's progressive script instead of repeating that candidate.
    return trial({}) ? 0 : 1;
  }

  auto costs = std::make_unique<Costs>();
  // For each cutoff measure the low and high bands, plus narrow bands
  // between consecutive cutoffs. Measure every distinct scan only once.
  Scans probes;
  for (unsigned i = 0; i + 1 < std::size(ends); ++i) {
    for (unsigned j = i + 1; j < std::size(ends); ++j) {
      if (i != 0 && ends[j] != 63 && j != i + 1)
        continue;
      for (int c = 0; c < nc; ++c) {
        for (int b = 0; b <= max_depth; ++b) {
          probes.push_back(scan(c, ends[i] + 1, ends[j], 0, b));
          if (b > 0)
            probes.push_back(scan(c, ends[i] + 1, ends[j], b, b - 1));
        }
      }
    }
  }
  {
    Encoder sample;
    std::vector<size_t> rates(probes.size());
    if (!sample.write(in, probes, true, 48, rates.data()))
      return 1;
    for (size_t i = 0; i < probes.size(); ++i)
      costs->record(probes[i], rates[i]);
  }
  Scans ac = costs->choose(nc);
  int from = blocks_in_group(0) == nc ? 0 : 1;
  if (blocks_in_group(from) > C_MAX_BLOCKS_IN_MCU)
    from = nc;
  Scans scans = dc_scans(nc, from);
  scans.insert(scans.end(), ac.begin(), ac.end());
  return trial(scans) ? 0 : 1;
}
