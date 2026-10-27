#include "pngtools.h"
#include "png.h"
#include <cstring>

static void png_strip_error(png_structp, png_const_charp message) { throw message; }

static void png_strip_warning(png_structp, png_const_charp) {}

static void png_strip_write(png_structp writer, png_bytep data, png_size_t size) {
  auto &output = *static_cast<std::vector<unsigned char> *>(png_get_io_ptr(writer));
  output.insert(output.end(), data, data + size);
}

int strip_png(const std::vector<unsigned char> &input, std::vector<unsigned char> &output) {
  if (input.size() < 8 || png_sig_cmp(input.data(), 0, 8))
    return 1;

  std::vector<unsigned char> result;
  png_structp writer = nullptr;
  try {
    writer =
        png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, png_strip_error, png_strip_warning);
    if (!writer)
      return 1;
    png_set_write_fn(writer, &result, png_strip_write, nullptr);
    png_write_sig(writer);

    auto chunk_size = [&](size_t offset) -> size_t {
      if (input.size() - offset < 12)
        throw "Truncated PNG chunk";
      png_uint_32 length = png_get_uint_32(input.data() + offset);
      if (length > PNG_UINT_31_MAX || length > input.size() - offset - 12)
        throw "Invalid PNG chunk length";
      return static_cast<size_t>(length) + 12;
    };

    size_t offset = 8;
    bool have_header = false;
    bool have_data = false;
    bool have_end = false;
    while (offset < input.size()) {
      size_t size = chunk_size(offset);
      const unsigned char *type = input.data() + offset + 4;
      if (!have_header) {
        if (std::memcmp(type, "IHDR", 4) || size != 25)
          throw "Missing PNG header";
        have_header = true;
      } else if (!std::memcmp(type, "IHDR", 4)) {
        throw "Duplicate PNG header";
      }

      if (!std::memcmp(type, "IDAT", 4)) {
        size_t end = offset;
        png_uint_32 total = 0;
        while (end < input.size()) {
          size_t part_size = chunk_size(end);
          if (std::memcmp(input.data() + end + 4, "IDAT", 4))
            break;
          size_t length = part_size - 12;
          if (length > PNG_UINT_31_MAX - total)
            throw "Combined PNG IDAT is too large";
          total += static_cast<png_uint_32>(length);
          end += part_size;
        }

        png_write_chunk_start(writer, type, total);
        while (offset < end) {
          size_t part_size = chunk_size(offset);
          png_write_chunk_data(writer, input.data() + offset + 8, part_size - 12);
          offset += part_size;
        }
        png_write_chunk_end(writer);
        have_data = true;
        continue;
      }

      if (!(type[0] & 0x20) || !std::memcmp(type, "tRNS", 4) || !std::memcmp(type, "acTL", 4) ||
          !std::memcmp(type, "fcTL", 4) || !std::memcmp(type, "fdAT", 4)) {
        png_write_chunk(writer, type, input.data() + offset + 8, size - 12);
      }
      offset += size;
      if (!std::memcmp(type, "IEND", 4)) {
        if (size != 12 || !have_data)
          throw "Invalid PNG end";
        have_end = true;
        break;
      }
    }
    if (!have_end)
      throw "Missing PNG end";
  } catch (...) {
    png_destroy_write_struct(&writer, nullptr);
    return 1;
  }
  png_destroy_write_struct(&writer, nullptr);
  output.swap(result);
  return 0;
}
