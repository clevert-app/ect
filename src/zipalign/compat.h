// zlialign needs android libutils, here is our compat implementation
#ifndef ZIPALIGN_COMPAT_H_
#define ZIPALIGN_COMPAT_H_
#define _FILE_OFFSET_BITS 64
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <errno.h>
#include <unistd.h>
#include <zlib.h>
#include <vector>
#include <algorithm>
#include <future>
#include <thread>
#include <deque>
#include <memory>
#include "support.h"
#if defined(__APPLE__) || !defined(off64_t)
typedef off_t off64_t;
#endif
#define ALOGV(...) ((void)0)
#define ALOGD(...) ((void)0)
#define ALOGW(...) fprintf(stderr, __VA_ARGS__)
#define ALOGE(...) fprintf(stderr, __VA_ARGS__)
#define _Static_assert static_assert
#define assert_or_exit(v) if (!(v)) { ALOGW("Zip: assert failed"); exit(1); }
namespace android {
  typedef int32_t status_t; // from libutils/binder/include/utils/Errors.h
  const status_t OK = 0;
  const status_t UNKNOWN_ERROR = (-2147483647-1); // INT32_MIN value
  const status_t NO_MEMORY = -ENOMEM;
  const status_t INVALID_OPERATION = -ENOSYS;
  const status_t NAME_NOT_FOUND = -ENOENT;
  const status_t PERMISSION_DENIED = -EPERM;
  const status_t ALREADY_EXISTS = -EEXIST;
  template <typename T> using Vector = std::vector<T>;
  inline int ZipInflateFile(FILE* input_file, size_t compressed_length, void* out_buf, size_t uncompressed_length) {
    const size_t kBufSize = 32768; // https://android.googlesource.com/platform/system/core/+/refs/tags/android-11.0.0_r48/libziparchive/zip_archive.cc
    std::vector<uint8_t> read_buf(kBufSize);
    z_stream zstream = {};
    zstream.next_out = static_cast<unsigned char*>(out_buf);
    zstream.avail_out = uncompressed_length;
    int zerr = inflateInit2(&zstream, -MAX_WBITS);
    uint32_t remaining_bytes = compressed_length;
    while (zerr == Z_OK) {
      if (zstream.avail_in == 0 && remaining_bytes != 0) {
        const size_t read_size = (remaining_bytes > kBufSize) ? kBufSize : remaining_bytes;
        if (fread(&read_buf[0], 1, read_size, input_file) != read_size) {
          ALOGW("Zip: inflate read failed, not enough data was read");
          break;
        }
        remaining_bytes -= read_size;
        zstream.next_in = &read_buf[0];
        zstream.avail_in = read_size;
      }
      zerr = inflate(&zstream, Z_NO_FLUSH);
    }
    inflateEnd(&zstream);
    assert_or_exit(zstream.total_out == uncompressed_length && remaining_bytes == 0 && zerr == Z_STREAM_END);
    return 0;
  }
}
namespace zip_archive {
  struct Reader {};
  struct Writer { virtual bool Append(uint8_t* buf, size_t buf_size) = 0; };
}
inline int getEnvZopfliLevel() {
  static const int ret = []() {
    const char* s = getenv("ZIPALIGN_ZOPFLI_LEVEL");
    int v = s ? int(strtol(s, NULL, 10)) : -1;
    return (3 <= v && v <= 9) ? v : 4; // 3-9 , default = 4
  }();
  return ret;
}
inline int getEnvThreadsCount() {
  static const int ret = []() {
    const char* s = getenv("ZIPALIGN_THREADS_COUNT");
    if (s == NULL) return 1;
    if (strcmp(s, "phy") == 0) return get_phy_core_count();
    int v = int(strtol(s, NULL, 10));
    return v == 0 ? int(std::thread::hardware_concurrency()) : v;
  }();
  return ret;
}
#endif
