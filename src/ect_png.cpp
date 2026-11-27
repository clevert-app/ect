#include "ectpng.h"
#include "png.h"
#include "zlib.h"
#include "zopfli/zlib_container.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

namespace filter_estimator {

class FilterEstimator {
  static constexpr size_t hash_size = 16384;
  static constexpr size_t short_hash_size = 4096;
  static constexpr size_t window_size = 32768;
  static constexpr size_t lookahead = 262;

  struct Counts {
    std::array<unsigned, 286> literal{};
    std::array<unsigned, 30> distance{};
    unsigned symbols = 0, extra_bits = 0, fixed_bits = 0;
    unsigned bytes = 0, recent_distance = 0;
    double completed_bits = 0;

    Counts() { literal[256] = 1; }

    double cost() const {
      static const auto entropy_bits = [] {
        std::array<double, 16385> values{};
        for (size_t count = 1; count < values.size(); count++)
          values[count] = count * std::log2(double(count));
        return values;
      }();
      auto entropy = [](const auto &frequencies) {
        unsigned total = 0, used = 0, maximum = 0;
        double bits = 0;
        for (unsigned count : frequencies) {
          total += count;
          used += count != 0;
          maximum = std::max(maximum, count);
          bits -= entropy_bits[count];
        }
        if (maximum > total / 2)
          return total + entropy_bits[total - maximum] + bits + entropy_bits[maximum] + used * 3;
        return total ? std::max(double(total), entropy_bits[total] + bits) + used * 3 : 0;
      };
      return completed_bits + std::min(double(fixed_bits + 10),
                                       entropy(literal) + entropy(distance) + extra_bits + 60);
    }

    template <bool whole> void finish_symbol(unsigned size) {
      if constexpr (!whole)
        bytes += size;
      if (++symbols == 16383 || (!whole && bytes >= 131072)) {
        double bits = cost();
        unsigned saved_distance = recent_distance;
        *this = Counts();
        recent_distance = saved_distance;
        completed_bits = bits;
      }
    }

    template <bool whole> void add_literal(unsigned value) {
      literal[value]++;
      fixed_bits += value < 144 ? 8 : 9;
      finish_symbol<whole>(1);
    }

    template <bool whole> void add_match(unsigned length, unsigned offset) {
      if constexpr (!whole)
        recent_distance = offset;
      unsigned length_code, length_extra = 0;
      if (length <= 10)
        length_code = length + 254;
      else if (length == 258)
        length_code = 285;
      else {
        length_extra = 29 - __builtin_clz(length - 3);
        length_code = 257 + 4 * (length_extra + 1) + (((length - 3) >> length_extra) & 3);
      }
      unsigned distance_code = offset - 1, distance_extra = 0;
      if (offset > 4) {
        distance_extra = 30 - __builtin_clz(offset - 1);
        distance_code = 2 * (distance_extra + 1) + (((offset - 1) >> distance_extra) & 1);
      }
      literal[length_code]++;
      distance[distance_code]++;
      extra_bits += length_extra + distance_extra;
      fixed_bits += (length_code <= 279 ? 7 : 8) + 5 + length_extra + distance_extra;
      finish_symbol<whole>(length);
    }
  };

  std::vector<size_t> heads, trial_heads, best_heads;
  std::vector<uint16_t> previous, trial_previous, best_previous;
  std::vector<size_t> short_heads, trial_short_heads, best_short_heads;
  Counts counts, candidate_counts, best_counts;
  size_t next_position = 0, candidate_end = 0, best_end = 0, row_stride;

  static size_t hash(const uint8_t *data) {
    uint32_t value;
    memcpy(&value, data, sizeof(value));
    return (value * 0x1e35a7bdU) >> 18;
  }

  static size_t short_hash(const uint8_t *data) {
    uint32_t value = uint32_t(data[0]) | uint32_t(data[1]) << 8 | uint32_t(data[2]) << 16;
    return (value * 0x1e35a7bdU) >> 20;
  }

  template <bool whole> double evaluate(const uint8_t *data, size_t end) {
    if constexpr (!whole) {
      trial_heads = heads;
      trial_short_heads = short_heads;
    }
    auto &active_heads = whole ? heads : trial_heads;
    auto &active_short_heads = whole ? short_heads : trial_short_heads;
    auto &active_previous = whole ? previous : trial_previous;
    auto previous_index = [&](size_t position) {
      return whole ? position & (window_size - 1) : position - next_position;
    };
    Counts trial = counts;
    size_t first = next_position;
    candidate_counts = counts;
    candidate_end = first;
    bool tail = false;
    struct UndoEntry {
      size_t index, short_index, head, short_head;
    };
    std::array<UndoEntry, lookahead> undo;
    size_t undo_count = 0;
    struct MatchHeads {
      size_t match, short_match;
    };
    auto insert = [&](size_t position) {
      size_t index = hash(data + position);
      size_t short_index = short_hash(data + position);
      size_t match = active_heads[index];
      size_t short_match = active_short_heads[short_index];
      if (tail)
        undo[undo_count++] = {index, short_index, match, short_match};
      active_previous[previous_index(position)] =
          match ? std::min(position + 1 - match, window_size + 1) : 0;
      active_heads[index] = position + 1;
      active_short_heads[short_index] = position + 1;
      return MatchHeads{match, short_match};
    };
    size_t position = first;
    while (position < end) {
      if (!whole && !tail && end - position < lookahead) {
        candidate_counts = trial;
        candidate_end = position;
        tail = true;
      }
      MatchHeads matches{};
      if (end - position >= 4)
        matches = insert(position);
      else if (end - position >= 3)
        matches.short_match = active_short_heads[short_hash(data + position)];
      size_t match = matches.match;
      size_t best_length = 3, best_distance = 0;
      size_t limit = std::min(size_t(258), end - position);
      auto match_length = [&](size_t offset) {
        size_t length = 4;
        while (length + 8 <= limit) {
          uint64_t left, right;
          memcpy(&left, data + offset + length, 8);
          memcpy(&right, data + position + length, 8);
          if (left != right) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            length += __builtin_clzll(left ^ right) / 8;
#else
            length += __builtin_ctzll(left ^ right) / 8;
#endif
            break;
          }
          length += 8;
        }
        while (length < limit && data[offset + length] == data[position + length])
          length++;
        return length;
      };
      unsigned probes = 16;
      for (unsigned depth = 0; match && depth < probes; depth++) {
        size_t offset = match - 1;
        if (position - offset > window_size)
          break;
        if (data[offset + best_length] == data[position + best_length] &&
            memcmp(data + offset, data + position, 4) == 0) {
          size_t length = match_length(offset);
          if (length > best_length) {
            best_length = length;
            best_distance = position - offset;
            if (length >= 32)
              probes = 8;
            if (length == limit)
              break;
          }
        }
        size_t step = whole || offset >= first ? active_previous[previous_index(offset)]
                                               : previous[offset & (window_size - 1)];
        match = step && step <= offset ? offset - step + 1 : 0;
      }
      if constexpr (!whole) {
        for (size_t distance : {row_stride, size_t(trial.recent_distance)}) {
          if (!distance || distance > window_size || position < distance ||
              limit < 4 || distance == best_distance ||
              (best_length == limit && distance >= best_distance))
            continue;
          size_t offset = position - distance;
          if (data[offset + best_length - 1] == data[position + best_length - 1] &&
              memcmp(data + offset, data + position, 4) == 0) {
            size_t length = match_length(offset);
            if (length > best_length ||
                (length == best_length && distance < best_distance)) {
              best_length = length;
              best_distance = distance;
            }
          }
        }
      }
      if (!best_distance && matches.short_match && position - (matches.short_match - 1) < 8192 &&
          memcmp(data + position, data + matches.short_match - 1, 3) == 0) {
        best_length = 3;
        best_distance = position - (matches.short_match - 1);
      }
      if (best_distance) {
        trial.add_match<whole>(best_length, best_distance);
        size_t stop = std::min(position + best_length, end - 3);
        uint32_t prefix = 0;
        if (stop - position > 16)
          memcpy(&prefix, data + position, sizeof(prefix));
        if (stop - position > 16 && prefix == data[position] * 0x01010101U &&
            memcmp(data + position, data + position + 1, stop - position + 2) == 0) {
          size_t index = hash(data + position);
          size_t short_index = short_hash(data + position);
          if (tail)
            undo[undo_count++] = {index, short_index, active_heads[index],
                                  active_short_heads[short_index]};
          for (size_t inserted = position + 1; inserted < stop; inserted++)
            active_previous[previous_index(inserted)] = 1;
          active_heads[index] = stop;
          active_short_heads[short_index] = stop;
        } else {
          for (size_t inserted = position + 1; inserted < stop; inserted++)
            insert(inserted);
        }
        position += best_length;
      } else {
        trial.add_literal<whole>(data[position]);
        position++;
      }
    }
    while (undo_count) {
      const auto &entry = undo[--undo_count];
      active_heads[entry.index] = entry.head;
      active_short_heads[entry.short_index] = entry.short_head;
    }
    return trial.cost();
  }

public:
  explicit FilterEstimator(size_t row_size, size_t stride_hint = 0)
      : heads(row_size ? hash_size : 0), trial_heads(heads.size()), best_heads(heads.size()),
        previous(row_size ? window_size : 0), trial_previous(row_size ? row_size + lookahead : 0),
        best_previous(trial_previous.size()), short_heads(row_size ? short_hash_size : 0),
        trial_short_heads(short_heads.size()), best_short_heads(short_heads.size()),
        row_stride(stride_hint ? stride_hint : row_size) {}

  double score(const uint8_t *data, size_t end) { return evaluate<false>(data, end); }

  static double estimate(const uint8_t *data, size_t size) {
    FilterEstimator estimator(0);
    estimator.heads.resize(hash_size);
    estimator.short_heads.resize(short_hash_size);
    estimator.previous.resize(window_size);
    return estimator.evaluate<true>(data, size);
  }

  void keep() {
    best_heads.swap(trial_heads);
    best_previous.swap(trial_previous);
    best_short_heads.swap(trial_short_heads);
    best_counts = candidate_counts;
    best_end = candidate_end;
  }

  void commit() {
    heads.swap(best_heads);
    short_heads.swap(best_short_heads);
    counts = best_counts;
    size_t oldest = best_end > window_size ? best_end - window_size : 0;
    for (size_t inserted = std::max(next_position, oldest); inserted < best_end; inserted++)
      previous[inserted & (window_size - 1)] = best_previous[inserted - next_position];
    next_position = best_end;
  }
};

} // namespace filter_estimator

namespace {

using namespace filter_estimator;

struct Buffer {
  uint8_t *data = nullptr;
  size_t size = 0;
  size_t capacity = 0;
};

struct Image {
  uint8_t *pixels;
  unsigned width, height, depth;
};

struct Color {
  unsigned type = 0, depth = 1;
  unsigned count = 0;
  uint8_t palette[256][4] = {};
  bool key = false;
  unsigned key_rgb[3] = {};
};

static void fail(const char *message) {
  fprintf(stderr, "PNG: %s\n", message);
  abort();
}

static uint8_t *alloc_filtered(size_t size) {
  // Zopfli's GetMatch reads up to 15 bytes past the logical input end.
  // LodePNG supplies the same 16-byte tail before calling its compressor.
  uint8_t *data = (uint8_t *)malloc(size + 32);
  memset(data + size, 0, 32);
  return data;
}

static void append(Buffer *buffer, const uint8_t *data, size_t size) {
  if (size > SIZE_MAX - buffer->size)
    fail("buffer too large");
  if (buffer->size + size > buffer->capacity) {
    buffer->capacity = std::max(buffer->size + size, buffer->capacity + buffer->capacity / 2 + 256);
    buffer->data = (uint8_t *)realloc(buffer->data, buffer->capacity);
  }
  if (size)
    memcpy(buffer->data + buffer->size, data, size);
  buffer->size += size;
}

static void png_failure(png_structp, png_const_charp message) { fail(message); }

struct Input {
  const uint8_t *data;
  size_t size;
};

static void read_bytes(png_structp png, png_bytep data, png_size_t size) {
  Input *input = (Input *)png_get_io_ptr(png);
  if (size > input->size)
    png_error(png, "truncated input");
  memcpy(data, input->data, size);
  input->data += size;
  input->size -= size;
}

static void write_bytes(png_structp png, png_bytep data, png_size_t size) {
  append((Buffer *)png_get_io_ptr(png), data, size);
}

static size_t chunk_size(const uint8_t *chunk, const uint8_t *end) {
  if (end - chunk < 12)
    fail("truncated chunk");
  size_t size = png_get_uint_32(chunk) + size_t(12);
  if (size > size_t(end - chunk))
    fail("truncated chunk");
  return size;
}

static bool chunk_is(const uint8_t *chunk, const char *name) {
  return memcmp(chunk + 4, name, 4) == 0;
}

static Image read_image(const uint8_t *data, size_t size) {
  Input input = {data, size};
  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, png_failure, nullptr);
  png_infop info = png_create_info_struct(png);
  png_set_read_fn(png, &input, read_bytes);
  png_read_info(png, info);
  Image image = {};
  image.width = png_get_image_width(png, info);
  image.height = png_get_image_height(png, info);
  image.depth = png_get_bit_depth(png, info) == 16 ? 16 : 8;
  // Expand without gamma correction or 16-bit rounding. All later conversions
  // must preserve samples, including the RGB of transparent pixels.
  png_set_expand(png);
  png_set_gray_to_rgb(png);
  png_set_add_alpha(png, image.depth == 16 ? 65535 : 255, PNG_FILLER_AFTER);
  int passes = png_set_interlace_handling(png); // normal returns 1, adam7 returns 7
  png_read_update_info(png, info);
  size_t row_size = png_get_rowbytes(png, info);
  if (row_size > SIZE_MAX / image.height)
    fail("image too large");
  image.pixels = passes == 1 ? (uint8_t *)malloc(row_size * image.height)
                             : (uint8_t *)calloc(row_size * image.height, 1);
  for (int i = 0; i < passes; i++)
    for (unsigned row = 0; row < image.height; row++)
      png_read_row(png, image.pixels + row * row_size, nullptr);
  png_read_end(png, info);
  png_destroy_read_struct(&png, &info, nullptr);
  return image;
}

static unsigned sample(const uint8_t *pixel, unsigned channel, unsigned depth) {
  return depth == 8 ? pixel[channel] : png_get_uint_16(pixel + channel * 2);
}

static uint32_t color_index(const uint8_t *pixel) {
  return unsigned(pixel[0]) | (unsigned(pixel[1]) << 8) | (unsigned(pixel[2]) << 16) |
         (unsigned(pixel[3]) << 24);
}

static void reduce_depth(Image *image) {
  if (image->depth != 16)
    return;
  size_t samples = size_t(image->width) * image->height * 4;
  // A 16-bit sample is reducible only when both bytes match.
  // Compact inplace after checking all channels, just as lodepng_compute_color_stats.
  for (size_t i = 0; i < samples * 2; i += 2)
    if (image->pixels[i] != image->pixels[i + 1])
      return;
  for (size_t i = 0; i < samples; i++)
    image->pixels[i] = image->pixels[i * 2];
  image->depth = 8;
}

static unsigned required_bits(unsigned value) {
  if (value == 0 || value == 255)
    return 1;
  if (value % 85 == 0)
    return 2;
  if (value % 17 == 0)
    return 4;
  return 8;
}

static Color choose_color(const Image &image, unsigned mode) {
  size_t count = size_t(image.width) * image.height;
  Color color;
  bool colored = false, alpha = false;
  unsigned bits = image.depth == 16 ? 16 : 1;
  unsigned opaque = image.depth == 16 ? 65535 : 255;
  unsigned stride = image.depth / 2;
  std::unordered_map<uint32_t, unsigned> colors;
  colors.reserve(257);
  for (size_t i = 0; i < count; i++) {
    const uint8_t *pixel = image.pixels + i * stride;
    if (i && memcmp(pixel, pixel - stride, stride) == 0)
      continue;
    unsigned red = sample(pixel, 0, image.depth);
    unsigned green = sample(pixel, 1, image.depth);
    unsigned blue = sample(pixel, 2, image.depth);
    unsigned opacity = sample(pixel, 3, image.depth);
    colored |= red != green || red != blue;
    if (bits < 8)
      bits = std::max(bits, required_bits(red));
    if (!alpha) {
      bool match = red == color.key_rgb[0] && green == color.key_rgb[1] && blue == color.key_rgb[2];
      if (opacity != opaque && (opacity != 0 || (color.key && !match))) {
        alpha = true;
        color.key = false;
      } else if (opacity == 0 && !color.key) {
        color.key = true;
        color.key_rgb[0] = red;
        color.key_rgb[1] = green;
        color.key_rgb[2] = blue;
      } else if (opacity == opaque && color.key && match) {
        alpha = true;
        color.key = false;
      }
    }
    if (image.depth == 8 && colors.size() < 257) {
      uint32_t index = color_index(pixel);
      if (colors.find(index) == colors.end()) {
        unsigned index_in_palette = colors.size();
        colors.emplace(index, index_in_palette);
        if (index_in_palette < 256)
          memcpy(color.palette[index_in_palette], pixel, 4);
      }
    }
    if (colored && alpha && (image.depth == 16 || colors.size() == 257))
      break;
  }
  if (color.key) {
    for (size_t i = 0; i < count; i++) {
      const uint8_t *pixel = image.pixels + i * stride;
      if (sample(pixel, 3, image.depth) != 0 && // check alpha first
          sample(pixel, 0, image.depth) == color.key_rgb[0] &&
          sample(pixel, 1, image.depth) == color.key_rgb[1] &&
          sample(pixel, 2, image.depth) == color.key_rgb[2]) {
        color.key = false;
        alpha = true;
        break;
      }
    }
  }
  if (color.key && count <= 49) {
    color.key = false;
    alpha = true;
  }
  if (colored || alpha)
    bits = std::max(bits, 8u);
  color.count = colors.size();
  unsigned palette_bits = 8;
  if (color.count <= 2)
    palette_bits = 1;
  else if (color.count <= 4)
    palette_bits = 2;
  else if (color.count <= 16)
    palette_bits = 4;

  unsigned div = mode == 2 ? 6 : mode < 8 ? 3 : 2;
  // Preserve the old white-image exception to preferring grayscale.
  bool white = color.count == 1 && !colored && image.pixels[0] == 255;
  if (white) {
    bool large = (image.width > 225 && image.height > 225) || count > 75000;
    large |= image.width > 250 && count > 40000;
    white = image.width > 20 && image.height > 20 && large;
  }
  bool palette = color.count <= 256 && bits <= 8;
  if (8 + color.count * 4 > count / div)
    palette = false;
  if (!colored && !alpha && bits <= palette_bits && !white)
    palette = false;

  color.depth = bits;
  if (palette) {
    color.type = PNG_COLOR_TYPE_PALETTE;
    color.depth = palette_bits;
    color.key = false;
  } else if (colored) {
    color.type = alpha ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB;
  } else {
    color.type = alpha ? PNG_COLOR_TYPE_GRAY_ALPHA : PNG_COLOR_TYPE_GRAY;
  }
  unsigned mask = (1u << color.depth) - 1;
  for (unsigned channel = 0; channel < 3; channel++)
    color.key_rgb[channel] &= mask;
  return color;
}

static unsigned channels(const Color &color) {
  const unsigned counts[] = {1, 0, 3, 1, 2, 0, 4};
  return counts[color.type];
}

static size_t row_bytes(const Image &image, const Color &color) {
  return (size_t(image.width) * channels(color) * color.depth + 7) / 8;
}

static uint8_t *convert_pixels(const Image &image, const Color &color) {
  size_t row_size = row_bytes(image, color);
  uint8_t *result = (uint8_t *)malloc(row_size * image.height);
  if (color.type == PNG_COLOR_TYPE_RGBA && color.depth == image.depth) {
    memcpy(result, image.pixels, row_size * image.height);
    return result;
  }
  // Only packed rows need zeroing: the final byte may contain padding bits.
  if (color.depth < 8)
    memset(result, 0, row_size * image.height);
  std::unordered_map<uint32_t, unsigned> palette;
  if (color.type == PNG_COLOR_TYPE_PALETTE) {
    palette.reserve(color.count);
    for (unsigned i = 0; i < color.count; i++)
      palette.emplace(color_index(color.palette[i]), i);
  }
  uint32_t previous_color = color_index(image.pixels) + 1;
  unsigned previous_index = 0;
  unsigned count = channels(color);
  for (unsigned row = 0; row < image.height; row++) {
    uint8_t *output = result + row * row_size;
    for (unsigned col = 0; col < image.width; col++) {
      const uint8_t *pixel = image.pixels + (size_t(row) * image.width + col) * (image.depth / 2);
      unsigned values[4];
      if (color.type == PNG_COLOR_TYPE_PALETTE) {
        uint32_t index = color_index(pixel);
        if (index != previous_color) {
          previous_index = palette.find(index)->second;
          previous_color = index;
        }
        values[0] = previous_index;
      } else {
        values[0] = sample(pixel, 0, image.depth);
        if (color.type == PNG_COLOR_TYPE_GRAY_ALPHA) {
          values[1] = sample(pixel, 3, image.depth);
        } else {
          for (unsigned channel = 1; channel < count; channel++)
            values[channel] = sample(pixel, channel, image.depth);
        }
        if (color.depth < 8)
          values[0] >>= 8 - color.depth;
      }
      if (color.depth < 8) {
        size_t bit = size_t(col) * color.depth;
        output[bit >> 3] |= values[0] << (8 - color.depth - (bit & 7));
      } else {
        for (unsigned channel = 0; channel < count; channel++) {
          if (color.depth == 16)
            *output++ = values[channel] >> 8;
          *output++ = values[channel];
        }
      }
    }
  }
  return result;
}

static uint8_t paeth_predictor(short left, short above, short upper_left) {
  short left_distance = abs(above - upper_left);
  short above_distance = abs(left - upper_left);
  short corner_distance = abs(left + above - upper_left - upper_left);
  if (above_distance < left_distance) {
    left = above;
    left_distance = above_distance;
  }
  return corner_distance < left_distance ? upper_left : left;
}

static void filter_row(uint8_t *out, const uint8_t *row, const uint8_t *prev_row, size_t size,
                       size_t byte_width, unsigned type) {
  switch (type) {
  case 0:
    memcpy(out, row, size);
    break;
  case 1:
    memcpy(out, row, byte_width);
    for (size_t i = byte_width; i < size; i++)
      out[i] = row[i] - row[i - byte_width];
    break;
  case 2:
    if (prev_row) {
      for (size_t i = 0; i < size; i++)
        out[i] = row[i] - prev_row[i];
    } else {
      memcpy(out, row, size);
    }
    break;
  case 3:
    for (size_t i = 0; i < byte_width; i++)
      out[i] = row[i] - (prev_row ? prev_row[i] >> 1 : 0);
    for (size_t i = byte_width; i < size; i++)
      out[i] = row[i] - ((row[i - byte_width] + (prev_row ? prev_row[i] : 0)) >> 1);
    break;
  case 4:
    if (!prev_row) {
      filter_row(out, row, nullptr, size, byte_width, 1);
    } else {
      for (size_t i = 0; i < byte_width; i++)
        out[i] = row[i] - prev_row[i];
      for (size_t i = byte_width; i < size; i++) {
        uint8_t prediction =
            paeth_predictor(row[i - byte_width], prev_row[i], prev_row[i - byte_width]);
        out[i] = row[i] - prediction;
      }
    }
    break;
  }
}

static void clean_row(uint8_t *row, const uint8_t *previous, size_t size, unsigned type) {
  // Match filterScanline2: Sub and Paeth leave hidden RGB unchanged.
  if (type == 1 || type == 4)
    return;
  for (size_t i = 0; i < size; i += 4) {
    if (row[i + 3])
      continue;
    for (unsigned channel = 0; channel < 3; channel++) {
      unsigned above = previous ? previous[i + channel] : 0;
      unsigned left = i ? row[i + channel - 4] : 0;
      if (type == 0)
        row[i + channel] = 0;
      else if (type == 2)
        row[i + channel] = above;
      else
        row[i + channel] = (left + above) >> 1;
    }
  }
}

static uint8_t *filter_image(const Image &image, const Color &color, const uint8_t *pixels,
                             int strategy, bool strict, bool warm_start = true) {
  size_t row_size = row_bytes(image, color), stride = row_size + 1;
  size_t byte_width = (channels(color) * color.depth + 7) / 8;
  uint8_t *output = alloc_filtered(stride * image.height);
  if (strategy <= 4) {
    for (unsigned row = 0; row < image.height; row++) {
      output[row * stride] = strategy;
      uint8_t *out = output + row * stride + 1;
      const uint8_t *row_p = pixels + row * row_size;
      const uint8_t *prev_row_p = row ? pixels + (row - 1) * row_size : nullptr;
      filter_row(out, row_p, prev_row_p, row_size, byte_width, strategy);
    }
    return output;
  }
  // allow to use clean_row to modified alpha 0 pixels' rgb, for better filter result
  bool clean = !strict && color.type == PNG_COLOR_TYPE_RGBA && color.depth == 8;
  bool estimated = strategy == 12;
  unsigned opening_rows = estimated && warm_start
      ? std::min(size_t(image.height), size_t(32768) / stride) : 0;
  size_t opening_size = opening_rows >= 2 ? opening_rows * stride : 0;
  FilterEstimator estimator(estimated ? stride : 0);
  std::vector<uint8_t> best_row(estimated ? stride : 0);
  uint8_t *attempt = (uint8_t *)malloc(std::max(stride, opening_size));
  uint8_t *current = clean ? (uint8_t *)malloc(row_size) : nullptr;
  uint8_t *previous = clean ? (uint8_t *)malloc(row_size) : nullptr;
  const uint8_t *above = nullptr;
  unsigned first_row = 0;
  if (opening_size) {
    Image opening_image = image;
    opening_image.height = opening_rows;
    uint8_t *seed = filter_image(opening_image, color, pixels, strategy, strict, false);
    memcpy(output, seed, opening_size);
    free(seed);
    FilterEstimator opening(opening_size, stride);
    double smallest = opening.score(output, opening_size);
    opening.keep();
    for (unsigned candidate = 0; candidate < 5; candidate++) {
      const uint8_t *prefix_above = nullptr;
      for (unsigned row = 0; row < opening_rows; row++) {
        const uint8_t *line = pixels + row * row_size;
        unsigned type = row ? 4 - candidate : output[0];
        if (clean) {
          memcpy(current, line, row_size);
          clean_row(current, prefix_above, row_size, type);
          line = current;
        }
        attempt[row * stride] = type;
        filter_row(attempt + row * stride + 1, line, prefix_above, row_size, byte_width, type);
        if (clean)
          memcpy(previous, current, row_size);
        prefix_above = clean ? previous : line;
      }
      double score = opening.score(attempt, opening_size);
      if (score < smallest) {
        smallest = score;
        opening.keep();
        memcpy(output, attempt, opening_size);
      }
    }
    for (unsigned row = 0; row < opening_rows; row++) {
      const uint8_t *line = pixels + row * row_size;
      if (clean) {
        memcpy(current, line, row_size);
        clean_row(current, above, row_size, output[row * stride]);
        memcpy(previous, current, row_size);
      }
      above = clean ? previous : line;
    }
    if (opening_rows < image.height)
      opening.commit();
    estimator = std::move(opening);
    first_row = opening_rows;
  }
  for (unsigned row = first_row; row < image.height; row++) {
    const uint8_t *source = pixels + row * row_size;
    double smallest = HUGE_VAL;
    unsigned best = 0;
    for (unsigned candidate = 0; candidate < 5; candidate++) {
      // Estimate ties prefer Paeth; minsum ties prefer None, as before.
      unsigned type = estimated ? 4 - candidate : candidate;
      const uint8_t *line = source;
      if (clean) {
        memcpy(current, source, row_size);
        clean_row(current, above, row_size, type);
        line = current;
      }
      attempt[0] = type;
      filter_row(attempt + 1, line, above, row_size, byte_width, type);
      double score = 0;
      if (estimated) {
        memcpy(output + row * stride, attempt, stride);
        score = estimator.score(output, (row + 1) * stride);
        if (row && type == output[(row - 1) * stride])
          score -= 4;
      } else {
        // Preserve LodePNG's 255-value heuristic (libpng uses 256-value),
        // and unsigned scoring for filter None.
        for (size_t pos = 1; pos < stride; pos++) {
          unsigned value = attempt[pos];
          score += type == 0 || value < 128 ? value : 255 - value;
        }
      }
      if (score < smallest) {
        smallest = score;
        best = type;
        if (estimated)
          estimator.keep();
        memcpy(estimated ? best_row.data() : output + row * stride, attempt, stride);
      }
    }
    if (estimated)
      memcpy(output + row * stride, best_row.data(), stride);
    if (clean) {
      memcpy(current, source, row_size);
      clean_row(current, above, row_size, best);
      memcpy(previous, current, row_size);
    }
    above = clean ? previous : source;
    if (estimated && row + 1 < image.height)
      estimator.commit();
  }
  free(previous);
  free(current);
  free(attempt);
  return output;
}

static size_t estimate_size(const uint8_t *data, size_t size) {
  return size_t(std::ceil(FilterEstimator::estimate(data, size) / 8));
}

static void write_chunk(png_structp png, const char *name, const uint8_t *data, size_t size) {
  png_write_chunk(png, (const png_byte *)name, data, size);
}

static void write_color(png_structp png, const Color &color) {
  if (color.type == PNG_COLOR_TYPE_PALETTE) {
    uint8_t palette[768], alpha[256];
    unsigned alpha_size = 0;
    for (unsigned i = 0; i < color.count; i++) {
      memcpy(palette + i * 3, color.palette[i], 3);
      alpha[i] = color.palette[i][3];
      if (alpha[i] != 255)
        alpha_size = i + 1;
    }
    write_chunk(png, "PLTE", palette, color.count * 3);
    if (alpha_size)
      write_chunk(png, "tRNS", alpha, alpha_size);
  } else if (color.key) {
    uint8_t key[6];
    for (unsigned channel = 0; channel < 3; channel++) {
      key[channel * 2] = color.key_rgb[channel] >> 8;
      key[channel * 2 + 1] = color.key_rgb[channel];
    }
    write_chunk(png, "tRNS", key, color.type == PNG_COLOR_TYPE_GRAY ? 2 : 6);
  }
}

static size_t png_overhead(const Color &color) {
  size_t size = 57; // Signature, IHDR, IDAT framing and IEND; zlib is counted separately.
  if (color.type == PNG_COLOR_TYPE_PALETTE) {
    size += 12 + color.count * 3;
    unsigned alpha_size = 0;
    for (unsigned i = 0; i < color.count; i++)
      if (color.palette[i][3] != 255)
        alpha_size = i + 1;
    if (alpha_size)
      size += 12 + alpha_size;
  } else if (color.key) {
    size += color.type == PNG_COLOR_TYPE_GRAY ? 14 : 18;
  }
  return size;
}

static bool should_try_without_palette(const Image &image, const Color &color, size_t size,
                                       unsigned mode) {
  // From TryOptimize in zopflipng.cpp: for small images, PLTE/tRNS overhead
  // can outweigh the shorter pixel indices. Keep its original thresholds.
  size_t count = size_t(image.width) * image.height;
  if (color.type != PNG_COLOR_TYPE_PALETTE || size >= 3800 || count >= 100000)
    return false;

  unsigned colors = color.count;
  unsigned pixels_limit = (colors + 2) * 390 + 370;
  unsigned size_limit = (colors + 2) * 40;
  if (pixels_limit <= count && colors <= 170)
    return false;
  if (size_limit <= size && colors <= 180)
    return false;
  if (size_limit / 2 <= size && colors >= 24)
    return false;

  if (mode <= 2) {
    if (size >= 3400 || count >= 20000)
      return false;
    if (size_limit * 7 / 20 <= size && colors >= 64)
      return false;
  }
  return true;
}

static Color without_palette(const Color &color) {
  Color result;
  bool gray = true, alpha = false;
  for (unsigned i = 0; i < color.count; i++) {
    const uint8_t *pixel = color.palette[i];
    gray &= pixel[0] == pixel[1] && pixel[0] == pixel[2];
    alpha |= pixel[3] != 255;
  }
  result.depth = 8;
  if (gray)
    result.type = alpha ? PNG_COLOR_TYPE_GRAY_ALPHA : PNG_COLOR_TYPE_GRAY;
  else
    result.type = alpha ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB;
  return result;
}

static Buffer compress_image(const Image &image, const Color &color, int strategy, bool strict,
                             const ZopfliOptions *options) {
  uint8_t *pixels = convert_pixels(image, color);
  uint8_t *filtered = filter_image(image, color, pixels, strategy, strict);
  free(pixels);
  size_t size = (row_bytes(image, color) + 1) * image.height;
  Buffer compressed;
  if (options)
    ZopfliZlibCompress(options, filtered, size, &compressed.data, &compressed.size);
  else
    compressed.size = estimate_size(filtered, size) + 6; // keep compressed.data as nullptr
  free(filtered);
  return compressed;
}

static Buffer try_compress(const Image &image, Color *color, int strategy, bool strict,
                           unsigned mode, const ZopfliOptions *options) {
  Buffer best = compress_image(image, *color, strategy, strict, options);
  size_t size = best.size + png_overhead(*color);
  if (should_try_without_palette(image, *color, size, mode)) {
    Color other_color = without_palette(*color);
    Buffer other = compress_image(image, other_color, strategy, strict, options);
    if (other.size + png_overhead(other_color) < size) {
      std::swap(best, other);
      *color = other_color;
    }
    free(other.data);
  }
  return best;
}

static uint8_t *read_filtered(const uint8_t *data, size_t size, size_t filtered_size) {
  // Strategy 6 takes the original filtered stream without changing palette
  // indices, row padding or hidden RGB. libpng has already validated the image.
  if (filtered_size > UINT_MAX)
    fail("image too large");
  Buffer idat;
  idat.capacity = size / 2;
  idat.data = (uint8_t *)malloc(idat.capacity);
  for (const uint8_t *chunk = data + 8, *end = data + size; chunk < end;) {
    size_t length = chunk_size(chunk, end);
    if (chunk_is(chunk, "IDAT"))
      append(&idat, chunk + 8, length - 12);
    if (chunk_is(chunk, "IEND"))
      break;
    chunk += length;
  }
  if (idat.size > UINT_MAX)
    fail("IDAT too large");
  uint8_t *output = alloc_filtered(filtered_size);
  z_stream stream = {};
  stream.next_in = idat.data;
  stream.avail_in = idat.size;
  stream.next_out = output;
  stream.avail_out = filtered_size;
  if (inflateInit2(&stream, 15) != Z_OK)
    fail("invalid IDAT");
  if (inflate(&stream, Z_FINISH) != Z_STREAM_END || stream.avail_out)
    fail("invalid IDAT");
  inflateEnd(&stream);
  free(idat.data);
  return output;
}

} // namespace

int ect_png(const uint8_t *in_buf, size_t in_size, uint8_t **out_buf, size_t *out_size,
            unsigned mode, int filter_opt, bool strip, bool strict, unsigned mt) {
  *out_buf = nullptr, *out_size = 0;
  unsigned palette_n = unsigned(filter_opt) >> 8;
  int strategy = filter_opt & 0xff;
  if (palette_n) {
    fprintf(stderr, "PNG palette sorting is not implemented yet\n");
    return 1;
  }
  // can be: 0 (none), 1 2 3 4 (png native), 6 (reuse), 12 (estimate), 77 (auto)
  if (strategy > 4 && strategy != 6 && strategy != 12 && strategy != 77)
    return 1;
  if (in_size < 33 || png_sig_cmp(in_buf, 0, 8))
    return 1;
  bool is_adam7 = in_buf[28] == PNG_INTERLACE_ADAM7;
  for (const uint8_t *chunk = in_buf + 8, *end = in_buf + in_size; chunk < end;) {
    size_t size = chunk_size(chunk, end);
    if (chunk_is(chunk, "acTL"))
      return 1; // is animated
    chunk += size;
  }
  ZopfliOptions options;
  if (is_adam7 && strategy == 6)
    strategy = 77; // adam7 can not reuse filter
  ZopfliInitOptions(&options, mode, mt, 1);
  Image image = read_image(in_buf, in_size);
  Color color;
  Buffer compressed;
  bool reuse = strategy == 6;
  if (reuse) {
    color.type = in_buf[25];
    color.depth = in_buf[24];
    size_t filtered_size = (row_bytes(image, color) + 1) * image.height;
    uint8_t *filtered = read_filtered(in_buf, in_size, filtered_size);
    ZopfliZlibCompress(&options, filtered, filtered_size, &compressed.data, &compressed.size);
    free(filtered);
  } else {
    reduce_depth(&image);
    color = choose_color(image, mode);
    if (strategy == 77) {
      // Compare 0 and 10; if 10 is better, choose 12, otherwise choose 0.
      Color c_0 = color;
      Buffer b_0 = try_compress(image, &c_0, 0, strict, mode, nullptr);
      size_t size_0 = b_0.size + png_overhead(c_0);
      Color c_10 = color;
      Buffer b_10 = try_compress(image, &c_10, 10, strict, mode, nullptr);
      size_t size_10 = b_10.size + png_overhead(c_10);
      strategy = size_10 < size_0 ? 12 : 0;
    }
    compressed = try_compress(image, &color, strategy, strict, mode, &options);
  }

  Buffer result;
  // Reserve the known image size, with half the input size as a lower bound.
  result.capacity = compressed.size + png_overhead(color);
  result.data = (uint8_t *)malloc(result.capacity);
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, png_failure, nullptr);
  png_set_write_fn(png, &result, write_bytes, nullptr);
  png_write_sig(png);
  bool color_written = reuse;
  bool data_written = false;
  for (const uint8_t *chunk = in_buf + 8, *end = in_buf + in_size; chunk < end;) {
    size_t length = chunk_size(chunk, end);
    if (chunk_is(chunk, "IHDR") && !reuse) {
      uint8_t header[13];
      memcpy(header, chunk + 8, sizeof(header));
      header[8] = color.depth;
      header[9] = color.type;
      header[12] = PNG_INTERLACE_NONE;
      write_chunk(png, "IHDR", header, sizeof(header));
    } else if (chunk_is(chunk, "PLTE") && !reuse) {
      write_color(png, color);
      color_written = true;
    } else if (chunk_is(chunk, "IDAT")) {
      if (!color_written) {
        write_color(png, color);
        color_written = true;
      }
      if (!data_written) {
        write_chunk(png, "IDAT", compressed.data, compressed.size);
        data_written = true;
      }
    } else {
      bool keep = !strip || !(chunk[4] & 32) || chunk_is(chunk, "tRNS");
      // Like getChunks/insertChunks, replace tRNS and omit sBIT/bKGD
      // when the color representation can change. Reuse keeps them intact.
      if (!reuse && (chunk_is(chunk, "tRNS") || chunk_is(chunk, "sBIT") || chunk_is(chunk, "bKGD")))
        keep = false;
      if (keep)
        png_write_chunk(png, chunk + 4, chunk + 8, length - 12);
    }
    if (chunk_is(chunk, "IEND"))
      break;
    chunk += length;
  }
  png_destroy_write_struct(&png, nullptr);

  free(compressed.data);
  free(image.pixels);
  *out_buf = result.data;
  *out_size = result.size;
  return 0;
}
