#include "gztools.h"
#include "miniz/miniz.h"
#include "optipng/optipng.h"
#include "support.h"
#include "zopfli/zopfli_gzip.h"
#include "zopflipng.h"
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

// clang-format off
#define assert_with_msg(v, msg, ...) do { if (!(v)) { fprintf(stderr, "error: " msg "\n", ##__VA_ARGS__); exit(1); } } while (0)
// unsigned fileHandler(const char * Infile, const ECTOptions& Options, int internal);
// unsigned zipHandler(std::vector<int> args, const char * argv[], int files, const ECTOptions& Options);
// void ReZipFile(const char* file_path, const ECTOptions& Options, size_t* files);
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
  bool reuse = false;
  bool all_filters = false;
  bool all_filters_brute = false;
  bool all_filters_cheap = false;
  int pal_sort = false;
  bool mt_file = false;
} opt;

// static std::atomic<size_t> processedfiles;
// static std::atomic<size_t> bytes;
// static std::atomic<long long> savings;

static void show_usage() {
  // follow date --help
  // clang-format off
  printf(
    "Usage: ect [OPTION...] [FILE]...\n"
    "\n"
    "Examples:\n"
    "  ect inplace1.png inplace2.jpg              # Optimize png and jpeg in-place.\n"
    "  ect -6 -o output.zip input.zip             # Recompress zip with zopfli level 6.\n"
    "  ect --as zipalign -z -P 4 4 i.apk o.apk    # Use as zipalign.\n"
    "  ect --as cjxl                              # Use as cjxl.\n"
    "\n"
    "Options:\n"
    "  -2 to -9           Set compression level (default: 3)\n"
    "  --strip            Strip metadata\n"
    "  --zip              Compress file(s) with ZIP algorithm\n"
    "  --gzip             Compress file(s) with GZIP algorithm, can use stdin stdout\n"
    "  --quiet            Print only error messages\n"
    "  --help, -h         Print only error messages\n"
    "  --strict           Enable strict losslessness\n"
    "  --compress-stored  Deflate stored ZIP entries (may break some formats like APK)\n"
    "  --reuse            Keep PNG filter and colortype\n"
    "  --all-filters      Try all PNG filter modes\n"
    "  --all-filters-b    Try all PNG filter modes, including brute force strategies\n"
    "  --all-filters-c    Try all PNG filter modes, may cheaper\n"
    "  --pal-sort [n]     Try n different PNG palette filtering strategies (up to 120)\n"
    "  --mt-file [n]      Use per file multithreading (with n threads)\n"
    "\n"
    "About:\n"
    "  Homepage: https://github.com/clevert-app/ect\n"
    "  Version: 2.0.1-alpha.1\n"
    "  Build date: " __DATE__ "\n"
  );
  // clang-format on
}

static int gzipHandler(const std::string &input_file, long long input_size, const char *outname) {
  assert_with_msg(input_size != 0, "compress empty file unsupported");
  char *gzip_name = nullptr; // the name in gzip header
  std::unique_ptr<char, decltype(&std::free)> gzip_name_(gzip_name, std::free);
  int is_gz = IsGzip(input_file.c_str(), &gzip_name);
  assert_with_msg(is_gz != 2, "bad gzip file");
  assert_with_msg(!(is_gz == 3 && opt.strict), "strict mode on, gzip has extra field");
  assert_with_msg(!exists(outname), "output file %s already exists", outname);
  unsigned char multithreading = 0;
  return ZopfliGzip(input_file.c_str(), outname, opt.mode, multithreading, false, true, gzip_name);
}

static int pngHandler(const std::string &input_file, long long input_size, const char *outname) {
  unsigned _mode = opt.mode;
  unsigned mode = (opt.mode % 10000) > 9 ? 9 : (opt.mode % 10000);
  if (mode == 1 && opt.reuse)
    mode++;
  unsigned mt_deflate = 0;
  int x = 0;

  if (mode == 9 && !opt.reuse && !opt.all_filters) {
    x = Zopflipng(opt.strip, input_file.c_str(), outname, opt.strict, 3, 0, mt_deflate, opt.quiet);
    if (x < 0)
      return 1;
  }

  // Disabled as using this causes libpng warnings
  // int filter = Optipng(Options.Mode, Infile, true, Options.Strict || Options.Mode > 1);
  int filter = 0;
  // if (!opt.all_filters)
  //   filter = opt.reuse ? 6 : Optipng(mode, input_file.c_str(), false, opt.strict || mode > 1);
  if (filter == -1)
    return 1;
  if (filter && !opt.all_filters && opt.all_filters_cheap && !opt.reuse)
    filter = 15;

  if (opt.all_filters) {
    unsigned mt_deflate = 0;
    const int filters[] = {6, 0, 5, 1, 2, 3, 4, 7, 8, 11, 12, 13, 9, 10, 14};
    const int filters_len_brute = sizeof(filters) / sizeof(filters[0]);
    const int filters_len = filters_len_brute - 3; // without last 3 filters
    x = Zopflipng(opt.strip, input_file.c_str(), outname, opt.strict, _mode,
                  filters[0] + opt.pal_sort, mt_deflate, opt.quiet);
    if (x < 0)
      return 1;
    // todo: about overwrite
    // for (int i = 1; i < (opt.all_filters_brute ? filters_len_brute : filters_len); i++) {
    //   Zopflipng(opt.strip, input_file.c_str(), outname, opt.strict, _mode,
    //             filters[i] + opt.pal_sort, mt_deflate, opt.quiet);
    // }
    return 0;
  }

  x = Zopflipng(opt.strip, input_file.c_str(), outname, opt.strict, _mode, filter + opt.pal_sort,
                mt_deflate, opt.quiet);
  if (x < 0)
    return 1;

  // if (opt.strip && x)
  //   Optipng(0, input_file.c_str(), false, 0);

  return 0;
}

static int fileHandler(const std::string &input_file, bool internal) {
  std::string e = input_file.substr(input_file.find_last_of(".") + 1); // extension name
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
  if (e == "GZIP" || e == "gzip" || e == "GZ" || e == "gz") {
    ret = gzipHandler(input_file, input_size, outname);
  } else if (e == "PNG" || e == "png") {
    ret = pngHandler(input_file, input_size, outname);
  }
  assert_with_msg(ret == 0, "failed file %s", input_file.c_str());
  if (opt.output == nullptr) {
    if (filesize(outname) < input_size) {
      RenameAndReplace(outname, input_file.c_str());
    } else {
      unlink(outname);
      printf("not smaller, keep origin");
    }
  }
  return 0;
}

#ifdef ZIPALIGN_SUPPORTED
extern "C" int cmd_zipalign_main(int argc, char *const argv[]);
// extern "C" int cmd_cjxl_main(int argc, char* const argv[]); // todo
#endif

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
  #ifdef ZIPALIGN_SUPPORTED
  if (strcmp(applet, "zipalign") == 0)
    exit(cmd_zipalign_main(argc, argv));
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
      for (int j = i + 1; j < argc; j++) {
        files.push_back(arg);
      }
      break;
    } else if (len_least >= 1 && arg[0] != '-') {
      files.push_back(arg);
    } else if (strcmp(arg, "--output") == 0 && i + 1 < argc) {
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
    } else if (strcmp(arg, "--reuse") == 0) {
      opt.reuse = true;
    } else if (strcmp(arg, "--all-filters") == 0) {
      opt.all_filters = true;
    } else if (strcmp(arg, "--all-filters-b") == 0) {
      opt.all_filters_brute = opt.all_filters = true;
    } else if (strcmp(arg, "--all-filters-c") == 0) {
      opt.all_filters_cheap = true;
    } else if (strcmp(arg, "--pal-sort") == 0 && i + 1 < argc) {
      opt.pal_sort = atoi(argv[i + 1]) << 8;
      assert_with_msg(opt.pal_sort <= (120 << 8), "out of range --pal-sort");
    } else if (strcmp(arg, "--mt-file") == 0) {
      opt.mt_file = true;
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
  if (opt.gzip && files.size() == 0) {
    #ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 || _setmode(_fileno(stdout), _O_BINARY) == -1)
      return 1;
    #endif
    // stdin stdout, ignore multi thread block support
    return ZopfliGzipStream(opt.mode, stdin, stdout);
  }
  if (opt.output != nullptr && !opt.zip) {
    assert_with_msg(files.size() == 1, "expected exactly 1 input file");
    return fileHandler(files[0], 0);
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
  if (opt.zip && files_list.size() != 0) {
    // return zipHandler(files_list);
  }
  for (const auto &file : files_list) {
    int ret = fileHandler(file, true);
    if (ret != 0) {
      return ret;
    }
  }
  return 0;
}
