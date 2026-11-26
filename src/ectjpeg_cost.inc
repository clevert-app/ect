// Included by the pinned jpegli entropy_coding.cc to use its private scan
// tokenizers, Huffman tables and bit writer for both probing and final output.
#if HWY_ONCE
#include "lib/jpegli/bitstream.h"
namespace jpegli {
void ZigZagShuffleBlocks(j_compress_ptr cinfo);

void EstimateScanCosts(j_compress_ptr cinfo, const jpeg_scan_info* scans,
                       size_t count, const size_t* original_blocks, uint8_t** output, size_t* costs) {
  auto* m = cinfo->master;
  ZigZagShuffleBlocks(cinfo);
  // Probe images use identical dimensions for every component. Reuse one
  // scratch buffer across all scans, including alternative scans of a band.
  const size_t blocks = size_t(cinfo->comp_info[0].width_in_blocks) *
                        cinfo->comp_info[0].height_in_blocks;
  const size_t capacity = blocks * (DCTSIZE2 + 2) + 1;
  m->token_arrays = Allocate<TokenArray>(cinfo, 1, JPOOL_IMAGE);
  auto& array = m->token_arrays[0];
  array.tokens = Allocate<Token>(cinfo, capacity, JPOOL_IMAGE);
  auto* refs = Allocate<RefToken>(cinfo, capacity, JPOOL_IMAGE);
  auto* refbits = Allocate<uint8_t>(cinfo, capacity, JPOOL_IMAGE);
  m->ac_ctx_offset[0] = 0;
  m->context_map = m->ac_ctx_offset;
  m->coding_tables = Allocate<HuffmanCodeTable>(cinfo, 1, JPOOL_IMAGE);
  cinfo->num_scans = 1;
  for (size_t i = 0; i < count; ++i) {
    auto& info = m->scan_token_info[0];
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
    auto* dest = cinfo->dest;
    dest->free_in_buffer += dest->next_output_byte - *output;
    dest->next_output_byte = *output;
    WriteScanData(cinfo, 0);
    size_t entropy_bytes = dest->next_output_byte - *output;
    size_t header = 10 + 4 + 17;  // noninterleaved SOS + DHT overhead
    for (int k = 1; k <= 16; ++k) header += huff.bits[k];
    const size_t total = original_blocks[scans[i].component_index[0]];
    costs[i] = header + (uint64_t(entropy_bytes) * total + blocks / 2) / blocks;
  }
  cinfo->scan_info = nullptr;
}

// Equal-length Huffman codes are interchangeable. Select the ordering that
// emits fewer stuffed FF bytes, keeping jpegli's lengths and shared tables.
void WriteOptimizedCoefficients(j_compress_ptr cinfo, uint8_t** output) {
  auto* m = cinfo->master;
  ZigZagShuffleBlocks(cinfo);
  TokenizeJpeg(cinfo);
  OptimizeHuffmanCodes(cinfo);
  InitEntropyCoder(cinfo);
  auto* dest = cinfo->dest;
  const size_t start = dest->next_output_byte - *output;
  auto rewind = [&]() {
    size_t bytes = dest->next_output_byte - (*output + start);
    dest->free_in_buffer += bytes;
    dest->next_output_byte = *output + start;
    return bytes;
  };
  for (size_t t = 0; t < m->num_huffman_tables; ++t) {
    if (m->slot_id_map[t] < 16) continue;
    auto& table = m->huffman_tables[t];
    JHUFF_TBL original = table, best = table;
    size_t smallest = SIZE_MAX;
    for (int order = 0; order < 3; ++order) {
      table = original;
      uint8_t* symbols = table.huffval;
      for (int d = 1; d <= 16; ++d) {
        auto* end = symbols + table.bits[d];
        if (order == 1) std::reverse(symbols, end);
        if (order == 2 && symbols != end) std::rotate(symbols, symbols + (end - symbols) / 2, end);
        symbols = end;
      }
      BuildHuffmanCodeTable(table, &m->coding_tables[t]);
      for (int i = 0; i < cinfo->num_scans; ++i)
        if (cinfo->scan_info[i].Ss > 0 && m->context_map[m->ac_ctx_offset[i]] == t)
          WriteScanData(cinfo, i);
      size_t bytes = rewind();
      if (bytes < smallest) { smallest = bytes; best = table; }
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
}  // namespace jpegli
#endif
