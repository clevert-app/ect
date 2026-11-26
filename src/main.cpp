#include "ectjpeg.h"
#include "ectpng.h"
#include "gztools.h"
#include "support.h"
#include "zipalign/ZipAlign.h"
#include "zipalign/ZipFile.h"
#include "zopfli/zopfli_gzip.h"
#include <atomic>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <vector>
#ifdef _WIN32
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

static struct Opt {
  char *output = nullptr; // no output means inplace
  int mode = 0;
  bool strip = false;
  bool zip = false;
  bool gzip = false;
  bool quiet = false;
  bool strict = false;
  bool compress_stored = false;
  bool optimize_inside = false;
  int png_filter = 77;
  int png_pal_sort = 0;
  const char *mt_block = "1";
  const char *mt_zip = "phy";
} opt;

static void show_help(bool is_long_help) {
  // follow date --help
  // clang-format off
  printf(
    "Usage: ect [OPTION...] [FILE]...\n"
    "\n"
    "Examples:\n"
    "  ect -6 inplace.png a.zip b.docx c.gz       # Optimize many files in-place.\n"
    "  ect -o output.zip --zip a.txt b.txt        # Create ZIP with zopfli level 6.\n"
    "  tar -c dir | ect --gzip -o file.tar.gz     # Piped streaming GZIP.\n"
    "  ect --as zipalign ...                      # As zipalign/cjxl/cwebp etc.\n"
    "\n"
    "Options:\n"
    "  --output, -o FILE  Output file path\n"
    "  -2 to -9           Set compression level, default is 3\n"
    "  --strip            Strip metadata\n"
    "  --zip              Create new ZIP with file(s), must use with --output\n"
    "  --gzip             Compress stdin with GZIP, write to --output or stdout\n"
    "  --quiet            Do not print size info\n"
    "  --strict           Enable strict mode in some formats\n"
    "  --compress-stored  Deflate stored ZIP entries (may break formats like APK)\n"
    "  --optimize-inside  Optimize supported entries inside ZIP\n"
    "  --png-filter N     PNG filter strategy, default is 77 (auto), see long help\n"
    "  --png-pal-sort N   Try N different PNG palette strategies (0-120)\n"
    "  --mt-block N       N threads for zopfli blocks, N = 0, phy, 1, 2 and more\n"
    "  --mt-zip N         N threads for ZIP entries, see long help\n"
    "  --help, -h         Print this help document, use --long-help to show more\n"
    "\n"
    "About:\n"
    "  Homepage: https://github.com/clevert-app/ect\n"
    "  Version: 2.0.1-alpha.7\n"
    "  Build date: " __DATE__ " " __TIME__ "\n"
  );
  if (!is_long_help)
    return;
  printf(
    "Details:\n"
    "  Compression level:\n"
    "    JPEG optimizes the original DCT coefficients. Level 3 and above search progressive scans.\n"
    "  PNG filter:\n"
    "    The PNG format allows different filters for each scanline.\n"
    "    In --png-filter N option, 0-4 use fixed filter N for whole image.\n"
    "    N = 6 reuse input image filters.\n"
    "    N = 12 use a smart filter estimator.\n"
    "    N = 77 is default, auto choose 0 or 12, because sometimes 0 is best.\n"
    "    N = 79 try all above and very slow.\n"
    "  Multi threading:\n"
    "    Options --mt-block N and --mt-zip N, accepts N = 0, phy, 1, 2, 3, etc.\n"
    "    N = 0 use all logical cores, N = phy use all physical cores.\n"
    "    The --mt-block N controls block level threading, default is 1.\n"
    "    Using --mt-zip N will not affect the ratio, default is phy.\n"
  );
  // clang-format on
}

static void fmt_bytes_size(char *buf, size_t buf_len, size_t value) {
  const char *u = "KMGTP";
  double d = (double)value / 1024.0;
  for (; d >= 1024.0 && u[1]; u++)
    d /= 1024.0;
  snprintf(buf, buf_len, "%.2f %ciB", d, *u);
}

static void print_savings(size_t input_size, size_t output_size, const char *outname) {
  assert_with_msg(output_size < input_size, "invalid call");
  size_t sub = input_size - output_size;
  double pct = (double)sub / input_size * 100.0;
  char s_in[16], s_out[16], s_sub[16];
  fmt_bytes_size(s_in, sizeof(s_in), input_size);
  fmt_bytes_size(s_out, sizeof(s_out), output_size);
  fmt_bytes_size(s_sub, sizeof(s_sub), sub);
  printf("> Output size %s = %s - %s (%.2f%%) for file %s\n", s_out, s_in, s_sub, pct, outname);
}

static int zip_handler(const std::vector<std::string> &list, bool recompress, const char *outname) {
  using namespace android;
  const int zout_flags = ZipFile::kOpenReadWrite | ZipFile::kOpenCreate | ZipFile::kOpenTruncate;
  int ret = 0;
  // create new zip with files in list
  if (!recompress) {
    ZipFile zout;
    ret = zout.open(outname, zout_flags);
    assert_with_msg(ret == OK, "unable to open zip file %s", outname);
    for (const std::string &input_file : list) {
      // todo: fit --mt-zip
      uint8_t *in_buf = nullptr;
      size_t in_size = 0;
      assert_with_msg(read_file(input_file.c_str(), &in_buf, &in_size) == 0, "read file failed");
      std::unique_ptr<uint8_t, decltype(&free)> in_buf_(in_buf, free);
      uint8_t *out_buf = nullptr;
      size_t out_size = 0;
      ZopfliBuffer(opt.mode, 1, in_buf, in_size, &out_buf, &out_size);
      std::unique_ptr<uint8_t, decltype(&free)> out_buf_(out_buf, free);
      int method = ZipEntry::kCompressDeflated;
      assert_with_msg(out_size < UINT32_MAX, "invalid call");
      uint64_t compressed_info = (uint64_t)crc32(0L, in_buf, in_size) << 32 | out_size;
      ret = zout.add(out_buf, in_size, input_file.c_str(), method, nullptr, compressed_info);
      assert_with_msg(ret == OK, "add zip entry failed");
    }
    return 0;
  }
  // want to recompress, use zipalign without align
  assert_with_msg(recompress && list.size() == 1, "invalid call");
  const char *input_name = list[0].c_str();
  return android::process(input_name, outname, 4, true, true, false, 4096, opt.compress_stored);
}

static int gzip_handler(const std::string &input_file, long long input_size, const char *outname) {
  assert_with_msg(input_size != 0, "compress empty file unsupported");
  char *gzip_name = nullptr; // the name in gzip header
  std::unique_ptr<char, decltype(&free)> gzip_name_(gzip_name, free);
  int is_gz = IsGzip(input_file.c_str(), &gzip_name);
  assert_with_msg(is_gz != 2, "bad gzip file");
  assert_with_msg(!(is_gz == 3 && opt.strict), "strict mode on, gzip has extra field");
  assert_with_msg(!exists(outname), "output file %s already exists", outname);
  static unsigned mt_block = parse_threads_count(nullptr, opt.mt_block);
  return ZopfliGzip(input_file.c_str(), outname, opt.mode, mt_block, false, true, gzip_name);
}

static int png_handler(const std::string &input_file, long long input_size, const char *outname) {
  // origin ect has extra mode support, now removed
  // unsigned _mode = opt.mode;
  // unsigned mode = (opt.mode % 10000) > 9 ? 9 : (opt.mode % 10000);

  uint8_t *in_buf = nullptr;
  size_t in_size = 0;
  assert_with_msg(read_file(input_file.c_str(), &in_buf, &in_size) == 0, "read file failed");
  // std::unique_ptr<uint8_t, decltype(&free)> in_buf_(in_buf, free);

  FILE *fd_out = fopen(outname, "wb");
  assert_with_msg(fd_out, "open output file failed");
  std::unique_ptr<FILE, decltype(&fclose)> fd_out_(fd_out, fclose);
  uint8_t *out_buf = nullptr;
  size_t out_size = 0;

  // opt.png_filter = (0-4, 6, 12, 77, 79)
  // * 77 (default) = auto 0 and 12
  // * 79 = try all
  int strategies[] = {6, 0, 1, 2, 3, 4, 12};
  int strategies_len = 1;
  if (opt.png_filter != 79)
    strategies[0] = opt.png_filter;
  if (opt.png_filter == 79)
    strategies_len = sizeof(strategies) / sizeof(strategies[0]);
  for (int i = 0; i < strategies_len; i++) {
    int filter_opt = strategies[i] | (opt.png_pal_sort << 8);
    int mt_zopfli = 1; // mt = 1 means no multi thread, just 1 thread
    int ect_png_code = ect_png(in_buf, in_size, &out_buf, &out_size, opt.mode, filter_opt,
                               opt.strip, opt.strict, mt_zopfli);
    assert_with_msg(ect_png_code == 0, "ect_png failed with code %d", ect_png_code);
    if (out_size < in_size) {
      free(in_buf);
      in_buf = out_buf, in_size = out_size;
      if (opt.png_filter == 79)
        printf("ect_png found better filter strategy %d\n", strategies[i]);
    } else {
      free(out_buf);
    }
    out_buf = nullptr, out_size = 0;
  }

  size_t out_write_size = fwrite(in_buf, 1, in_size, fd_out); // in_buf already swapped
  free(in_buf);
  assert_with_msg(out_write_size == in_size, "write size not match");
  return 0;
}

static int file_handler(const std::string &input_file) {
  std::string e = input_file.substr(input_file.find_last_of(".") + 1); // extension name
  for (char &c : e)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  long long input_size = filesize(input_file.c_str());
  assert_with_msg(input_size >= 0, "bad file %s", input_file.c_str());
  assert_with_msg(input_size < 1200000000, "input size too big"); // just random number
  std::string output_file;
  const char *outname = opt.output;
  if (opt.output == nullptr) {
    output_file = input_file + ".tmp";
    outname = output_file.c_str();
  }
  int ret = 0;

  if (e == "zip" || e == "apk" || e == "docx" || e == "xlsx" || e == "pptx" || e == "dotx" ||
      e == "xltx" || e == "potx" || e == "odt" || e == "ods" || e == "odp" || e == "odg" ||
      e == "epub" || e == "3mf" || e == "jar" || e == "ipa") {
    ret = zip_handler(std::vector<std::string>{input_file}, true, outname);
  } else if (e == "gzip" || e == "gz" || e == "tgz") {
    ret = gzip_handler(input_file, input_size, outname);
  } else if (e == "png") {
    ret = png_handler(input_file, input_size, outname);
#ifdef ECT_LIBJXL_SUPPORT
  } else if (e == "jpg" || e == "jpeg") {
    uint8_t *buf;
    size_t size;
    assert_with_msg(read_file(input_file.c_str(), &buf, &size) == 0, "read file failed");
    std::unique_ptr<uint8_t, decltype(&free)> input(buf, free);
    std::vector<uint8_t> best;
    ret = ect_jpeg(buf, size, opt.mode, opt.strip, best);
    if (ret == 0 && best.empty()) {
      if (!opt.quiet)
        printf("> Keep original JPEG %s\n", input_file.c_str());
      return 0;
    }
    if (ret == 0) {
      FILE *out = fopen(outname, "wbx");
      assert_with_msg(out, "cannot create output %s", outname);
      size_t written = fwrite(best.data(), 1, best.size(), out);
      if (fclose(out) != 0 || written != best.size()) {
        unlink(outname);
        return 1;
      }
    }
#endif
  } else {
    printf("unsupported file extension %s", e.c_str());
    return 0;
  }
  assert_with_msg(ret == 0, "failed file %s", input_file.c_str());
  long long output_size = filesize(outname);
  if (output_size < input_size) {
    if (opt.output == nullptr) {
      struct stat input_stat;
      assert_with_msg(stat(input_file.c_str(), &input_stat) == 0, "read input permissions failed");
#ifdef _WIN32
      int r = _chmod(outname, input_stat.st_mode & (_S_IREAD | _S_IWRITE));
#else
      int r = chmod(outname, input_stat.st_mode & 07777);
#endif
      assert_with_msg(r == 0, "set permissions failed");
      RenameAndReplace(outname, input_file.c_str());
    }
    if (!opt.quiet)
      print_savings(input_size, output_size, opt.output ? opt.output : input_file.c_str());
  } else {
    unlink(outname);
    if (!opt.quiet)
      printf("> Not smaller, keep origin file %s\n", input_file.c_str());
  }
  return 0;
}

// must follow origin define
extern "C" int cmd_zipalign_main(int argc, char *const argv[]);
extern "C" int cmd_webpinfo_main(int argc, const char *argv[]);
extern "C" int cmd_cwebp_main(int argc, const char *argv[]);
extern "C" int cmd_dwebp_main(int argc, const char *argv[]);
extern "C" int cmd_gif2webp_main(int argc, const char *argv[]);
extern "C" int cmd_img2webp_main(int argc, const char *argv[]);
extern "C" int cmd_webpmux_main(int argc, const char *argv[]);
extern "C" int cmd_jxlinfo_main(int argc, char *argv[]);
extern "C" int cmd_cjxl_main(int argc, char **argv);
extern "C" int cmd_djxl_main(int argc, const char *argv[]);
extern "C" int cmd_cjpegli_main(int argc, const char *argv[]);
extern "C" int cmd_djpegli_main(int argc, const char *argv[]);

static void multi_call(int argc, char *argv[]) {
  // supported usages:
  // $ ect --as zipalign -z -P 4 4 input.apk output.apk # use --as switch
  // $ zipalign -z -P 4 4 i.apk o.apk # renamed to zipalign or zipalign.exe)
  if (argc < 1 || argv[0] == NULL)
    return;
  // first check `--as` switch, if not exist, assume argv0 as applet
  char *applet = nullptr; // should contains zipalign/cjxl ... and future more
  if (argc >= 3 && strcmp(argv[1], "--as") == 0) {
    applet = argv[2];
    argc -= 2, argv += 2;
  } else {
    // cuz the msys2 shell may use both slash and bslash in the path
    applet = strrchr(argv[0], '/');
    applet = applet ? applet + 1 : argv[0];
    char *bslash = strrchr(applet, '\\');
    applet = bslash ? bslash + 1 : applet;
    // then, check applet
    size_t len = strlen(applet);
    if (len >= 4) {
      char *dot = applet + len - 4; // upper case `.EXE` is unsupported
      if (strcmp(dot, ".exe") == 0) {
        *dot = '\0';
      }
    }
  }
  if (strcmp(applet, "zipalign") == 0)
    exit(cmd_zipalign_main(argc, argv));
#if defined(ECT_LIBWEBP_SUPPORT)
  if (strcmp(applet, "webpinfo") == 0)
    exit(cmd_webpinfo_main(argc, (const char **)argv));
  if (strcmp(applet, "cwebp") == 0)
    exit(cmd_cwebp_main(argc, (const char **)argv));
  if (strcmp(applet, "dwebp") == 0)
    exit(cmd_dwebp_main(argc, (const char **)argv));
  if (strcmp(applet, "gif2webp") == 0)
    exit(cmd_gif2webp_main(argc, (const char **)argv));
  if (strcmp(applet, "img2webp") == 0)
    exit(cmd_img2webp_main(argc, (const char **)argv));
  if (strcmp(applet, "webpmux") == 0)
    exit(cmd_webpmux_main(argc, (const char **)argv));
#endif
#if defined(ECT_LIBJXL_SUPPORT)
  if (strcmp(applet, "jxlinfo") == 0)
    exit(cmd_jxlinfo_main(argc, argv));
  if (strcmp(applet, "cjxl") == 0)
    exit(cmd_cjxl_main(argc, argv));
  if (strcmp(applet, "djxl") == 0)
    exit(cmd_djxl_main(argc, (const char **)argv));
  if (strcmp(applet, "cjpegli") == 0)
    exit(cmd_cjpegli_main(argc, (const char **)argv));
  if (strcmp(applet, "djpegli") == 0)
    exit(cmd_djpegli_main(argc, (const char **)argv));
#endif
}

int zipalign_get_zopfli_level() {
  static const int ret = []() {
    const char *s = getenv("ZIPALIGN_ZOPFLI_LEVEL");
    int v = s ? (int)strtol(s, NULL, 10) : opt.mode;
    return (2 <= v && v <= 9) ? v : 4; // 2-9 , default = 4, not 3
  }();
  return ret;
}

int zipalign_get_threads_count() {
  static const int ret = []() {
    return parse_threads_count("ZIPALIGN_THREADS_COUNT", opt.mt_zip);
  }();
  return ret;
}

int main(int argc, char *argv[]) {
  // if match, run then exit(), works like busybox applets
  multi_call(argc, argv);
  // main logic
  if (argc < 2) {
    show_help(false);
    return 0;
  }
  std::vector<char *> files;
  for (int i = 1; i < argc; i++) {
    char *arg = argv[i];
    int len_least = strnlen(arg, 32); // real len can be more than this
    if (strcmp(arg, "--") == 0) {
      // assume remains are not switches
      for (int j = i + 1; j < argc; j++)
        files.push_back(argv[j]);
      break;
    } else if (len_least >= 1 && arg[0] != '-') {
      files.push_back(arg);
    } else if ((strcmp(arg, "--output") == 0 || strcmp(arg, "-o") == 0) && i + 1 < argc) {
      opt.output = argv[i + 1];
      i++;
    } else if (len_least == 2 && arg[0] == '-' && ('1' <= arg[1] && arg[1] <= '9')) {
      opt.mode = 1 + (arg[1] - '1');
      assert_with_msg(opt.mode != 1, "mode 1 is unsupported");
    } else if (strcmp(arg, "--strip") == 0) {
      opt.strip = true;
    } else if (strcmp(arg, "--zip") == 0) {
      opt.zip = true;
    } else if (strcmp(arg, "--gzip") == 0) {
      opt.gzip = true;
    } else if (strcmp(arg, "--quiet") == 0) {
      opt.quiet = true;
    } else if (strcmp(arg, "--strict") == 0) {
      opt.strict = true;
    } else if (strcmp(arg, "--compress-stored") == 0) {
      opt.compress_stored = true;
    } else if (strcmp(arg, "--optimize-inside") == 0) {
      opt.optimize_inside = true;
      assert_with_msg(false, "switch --optimize-inside is not implemented yet");
    } else if (strcmp(arg, "--png-filter") == 0 && i + 1 < argc) {
      opt.png_filter = (int)strtol(argv[i + 1], NULL, 10);
      i++;
    } else if (strcmp(arg, "--png-pal-sort") == 0 && i + 1 < argc) {
      opt.png_pal_sort = (int)strtol(argv[i + 1], NULL, 10);
      i++;
    } else if (strcmp(arg, "--mt-block") == 0 && i + 1 < argc) {
      opt.mt_block = argv[i + 1];
      i++;
    } else if (strcmp(arg, "--mt-zip") == 0 && i + 1 < argc) {
      opt.mt_zip = argv[i + 1];
      i++;
    } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
      show_help(false);
      return 0;
    } else if (strcmp(arg, "--long-help") == 0) {
      show_help(true);
      return 0;
    } else {
      printf("error on arg %s", arg);
      return 1;
    }
  }
  // switches parsing end
  if (opt.mode == 0)
    opt.mode = 3; // default = 3
  if (opt.gzip) {
    assert_with_msg(files.size() == 0, "--gzip switch is for stdin");
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 || _setmode(_fileno(stdout), _O_BINARY) == -1)
      return 1;
#endif
    std::unique_ptr<FILE, decltype(&fclose)> fd_out_(nullptr, fclose);
    FILE *fd_out = stdout;
    if (opt.output != nullptr) {
      fd_out = fopen(opt.output, "wb");
      assert_with_msg(fd_out, "open output file failed");
      fd_out_.reset(fd_out);
    }
    // todo: --mt-block
    return ZopfliGzipStream(opt.mode, stdin, fd_out);
  }
  if (opt.output != nullptr && !opt.zip) {
    assert_with_msg(files.size() == 1, "to use --output, expected exactly 1 input file");
    return file_handler(files[0]);
  }
  std::vector<std::string> files_list;
  for (char *name : files) {
    if (std::filesystem::is_regular_file(name)) {
      files_list.push_back(std::string(name));
    } else if (std::filesystem::is_directory(name)) {
      for (const auto &entry : std::filesystem::recursive_directory_iterator(name)) {
        if (entry.is_regular_file() && !entry.is_symlink()) {
          files_list.push_back(entry.path().string());
        }
      }
    }
  }
  // zip is the only "n in 1 out" format, others are "1 in 1 out", be careful
  if (opt.zip) {
    assert_with_msg(opt.output != nullptr, "--zip must be used with --output");
    return zip_handler(files_list, false, opt.output);
  }
  for (const auto &file : files_list) {
    int ret = file_handler(file);
    if (ret != 0)
      return ret;
  }
  return 0;
}
