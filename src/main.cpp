#include "gztools.h"
#include "pngtools.h"
#include "support.h"
#include "zipalign/ZipAlign.h"
#include "zipalign/ZipFile.h"
#include "zopfli/zopfli_gzip.h"
#include "zopflipng.h"
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
// clang-format off
#define assert_with_msg(v, msg, ...) do { if (!(v)) { fprintf(stderr, "error: " msg "\n", ##__VA_ARGS__); exit(1); } } while (0)
// clang-format on

static struct Opt {
  char *output = nullptr; // no output means inplace
  int mode = 3;
  bool strip = false;
  bool zip = false;
  bool gzip = false;
  bool quiet = false;
  bool strict = false;
  bool compress_stored = false;
  bool optimize_inside = false;
  bool reuse = false;
  bool all_filters = false;
  int pal_sort = false;
  bool mt_file = false;
} opt;

static void show_usage() {
  // follow date --help
  // clang-format off
  printf(
    "Usage: ect [OPTION...] [FILE]...\n"
    "\n"
    "Examples:\n"
    "  ect inplace1.png inplace2.zip more.gz      # Optimize many files in-place.\n"
    "  ect -6 -o output.zip --zip a.txt b.txt     # Create ZIP with zopfli level 6.\n"
    "  ect --as zipalign -z -P 4 4 i.apk o.apk    # Use as zipalign.\n"
    "  ect --as cjxl                              # Use as cjxl.\n"
    "\n"
    "Options:\n"
    "  --output, -o FILE  Output file path\n"
    "  -2 to -9           Set compression level (default: 3)\n"
    "  --strip            Strip metadata\n"
    "  --zip              Create new ZIP with file(s), must use with --output\n"
    "  --gzip             Compress stdin with GZIP, write to --output or stdout\n"
    "  --quiet            Print only error messages\n"
    "  --help, -h         Print only error messages\n"
    "  --strict           Enable strict losslessness\n"
    "  --compress-stored  Deflate stored ZIP entries (may break some formats like APK)\n"
    "  --optimize-inside  Optimize supported entries inside ZIP\n"
    "  --reuse            Keep PNG filter and colortype\n"
    "  --all-filters      Try all PNG filter modes\n"
    "  --pal-sort N       Try N different PNG palette filtering strategies (N <= 120)\n"
    "  --mt-zip N         Use N threads for ZIP entries\n"
    "\n"
    "About:\n"
    "  Homepage: https://github.com/clevert-app/ect\n"
    "  Version: 2.0.1-alpha.2\n"
    "  Build date: " __DATE__ " " __TIME__ "\n"
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
      std::vector<unsigned char> buf_in;
      assert_with_msg(read_file(input_file.c_str(), buf_in) == 0, "read file failed");
      unsigned char *out = nullptr;
      size_t out_size = 0;
      ZopfliBuffer(opt.mode, 0, buf_in.data(), buf_in.size(), &out, &out_size);
      int method = ZipEntry::kCompressDeflated;
      assert_with_msg(out_size < UINT32_MAX, "invalid call");
      uint64_t compressed_info = (uint64_t)crc32(0L, buf_in.data(), buf_in.size()) << 32 | out_size;
      ret = zout.add(out, buf_in.size(), input_file.c_str(), method, nullptr, compressed_info);
      free(out);
      assert_with_msg(ret == OK, "add zip entry failed");
    }
    return 0;
  }
  // want to recompress, use zipalign without align
  assert_with_msg(recompress && list.size() == 1, "invalid call");
  static std::atomic<bool> has_been_called{false};
  assert_with_msg(!has_been_called.exchange(true), "zip recompress only once per process");
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
  unsigned char multithreading = 0;
  return ZopfliGzip(input_file.c_str(), outname, opt.mode, multithreading, false, true, gzip_name);
}

static int png_handler(const std::string &input_file, long long input_size, const char *outname) {
  // origin ect has extra mode support, now removed
  // unsigned _mode = opt.mode;
  // unsigned mode = (opt.mode % 10000) > 9 ? 9 : (opt.mode % 10000);
  int mode = opt.mode;
  int mt_deflate = 0;
  int x = 0;

  std::vector<unsigned char> buf_in;
  assert_with_msg(read_file(input_file.c_str(), buf_in) == 0, "read file failed");

  FILE *fd_out = fopen(outname, "wb");
  assert_with_msg(fd_out, "open output file failed");
  std::unique_ptr<FILE, decltype(&fclose)> fd_out_(fd_out, fclose);
  std::vector<unsigned char> buf_out;

  // mode 9 do extra pre-compress use mode 3
  if (mode == 9 && !opt.reuse && !opt.all_filters) {
    x = Zopflipng(opt.strip, buf_in, buf_out, opt.strict, 3, 0, mt_deflate, opt.quiet);
    assert_with_msg(x == 0, "zopfli png failed with code %d", x);
    if (buf_out.size() < buf_in.size())
      std::swap(buf_in, buf_out);
    buf_out.clear();
  }

  const int strategies[] = {6, 10, 0, 1, 2, 3, 4, 7, 8, 9};
  int strategies_len = 2;
  if (opt.reuse)
    strategies_len = 1;
  else if (opt.all_filters)
    strategies_len = sizeof(strategies) / sizeof(strategies[0]);
  for (int i = 0; i < strategies_len; i++) {
    int filter_val = strategies[i] + opt.pal_sort;
    x = Zopflipng(opt.strip, buf_in, buf_out, opt.strict, mode, filter_val, mt_deflate, opt.quiet);
    assert_with_msg(x == 0, "zopfli png failed with code %d", x);
    if (buf_out.size() < buf_in.size())
      std::swap(buf_in, buf_out);
    buf_out.clear();
  }

  if (opt.strip) {
    assert_with_msg(strip_png(buf_in, buf_out) == 0, "strip PNG failed");
    std::swap(buf_in, buf_out);
    buf_out.clear();
  }

  size_t out_write_size = fwrite(buf_in.data(), 1, buf_in.size(), fd_out); // buf_in already swapped
  assert_with_msg(out_write_size == buf_in.size(), "write size not match");
  assert_with_msg(fclose(fd_out_.release()) == 0, "close output file failed");

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
#endif
}

int main(int argc, char *argv[]) {
  // if match, run then exit(), works like busybox applets
  multi_call(argc, argv);
  // main logic
  if (argc < 2) {
    show_usage();
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
      setenv("ZIPALIGN_ZOPFLI_LEVEL", arg + 1, 1);
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
    } else if (strcmp(arg, "--reuse") == 0) {
      opt.reuse = true;
    } else if (strcmp(arg, "--all-filters") == 0) {
      opt.all_filters = true;
    } else if (strcmp(arg, "--pal-sort") == 0 && i + 1 < argc) {
      opt.pal_sort = atoi(argv[i + 1]) << 8;
      assert_with_msg(opt.pal_sort <= (120 << 8), "out of range --pal-sort");
      i++;
    } else if (strcmp(arg, "--mt-file") == 0 && i + 1 < argc) {
      opt.mt_file = true;
      setenv("ZIPALIGN_THREADS_COUNT", argv[i + 1], 1);
      i++;
    } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
      show_usage();
      return 0;
    } else {
      printf("error on arg %s", arg);
      return 1;
    }
  }
  // switches parsing end
  assert_with_msg(!(opt.reuse && opt.all_filters), "the --reuse and --all-filters are conflicting");
  if (opt.gzip) {
    assert_with_msg(files.size() != 0, "--gzip switch is for stdin");
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
    // stdin, ignore multi thread block support
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
