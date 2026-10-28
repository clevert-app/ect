/*
Copyright 2013 Google Inc. All Rights Reserved.
Copyright 2015 Mr_KrzYch00. All Rights Reserved.

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

#include <cstdio>
#include <climits>
#include <cstdint>
#include <sys/stat.h>

#include "zopfli.h"
#include "zopfli_gzip.h"
#include "zlib.h"
#include "deflate.h"
#include "util.h"
#include "zopfli.h"
#include "zlib_container.h"
#include <ctime>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#ifdef ZOPFLI_APPEND_DATA
#undef ZOPFLI_APPEND_DATA
#endif
#define ZOPFLI_APPEND_DATA(/* T */ value, /* T** */ data, /* size_t* */ size) {\
(*data)[(*size)] = (value);\
(*size)++;\
}

static void ZopfliZipCompress(unsigned mode, unsigned multithreading,
                              const unsigned char* in, size_t insize, time_t time, std::string name,
                              unsigned char** out, size_t* outsize) {
  static const unsigned char filePKh[10]     = { 80, 75,  3,  4, 20,  0,  2,  0,  8,  0};
  static const unsigned char CDIRPKh[12]     = { 80, 75,  1,  2, 20,  0, 20,  0,  2,  0,  8,  0};
  static const unsigned char CDIRPKs[12]     = {  0,  0,  0,  0,  0,  0,  0,  0, 32,  0,  0,  0};
  static const unsigned char EndCDIRPKh[12]  = { 80, 75,  5,  6,  0,  0,  0,  0,  1,  0,  1,  0};

  unsigned long crcvalue = crc32(0, in, insize);
  unsigned long i;
  std::string infilename = name.substr(name.find_last_of('/') + 1);
  size_t max = infilename.size();
  *out = (unsigned char*)realloc(*out, 200);

  struct tm* times = localtime(&time);
  unsigned long dostime = times->tm_year < 80 ? 0x00210000 : times->tm_year > 207 ? 0xFF9FBF7D : (
                            (times->tm_year - 80) << 25 | (times->tm_mon + 1) << 21 | times->tm_mday << 16
                            | times->tm_hour << 11 | times->tm_min << 5 | times->tm_sec >> 1
                          );

  /* File PK STATIC DATA + CM */

  for(i=0;i<sizeof(filePKh);++i) ZOPFLI_APPEND_DATA(filePKh[i],out,outsize);

  /* MS-DOS TIME */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((dostime >> (i*8)) % 256, out, outsize);

  /* CRC */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((crcvalue >> (i*8)) % 256, out, outsize);

  /* OSIZE NOT KNOWN YET - WILL UPDATE AFTER COMPRESSION */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA(0, out, outsize);

  /* ISIZE */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((insize >> (i*8)) % 256, out, outsize);

  /* FNLENGTH */
  for(i=0;i<2;++i) ZOPFLI_APPEND_DATA((max >> (i*8)) % 256, out, outsize);

  /* NO EXTRA FLAGS */
  for(i=0;i<2;++i) ZOPFLI_APPEND_DATA(0, out, outsize);

  /* FILENAME */
  for(i=0;i<max;++i) ZOPFLI_APPEND_DATA(infilename[i], out, outsize);
  unsigned long rawdeflsize = *outsize;

  ZopfliBuffer(mode, multithreading, in, insize, out, outsize);
  *out = (unsigned char*)realloc(*out, 200 + *outsize);

  rawdeflsize = *outsize - rawdeflsize;

  /* C-DIR PK HEADER STATIC DATA */
  unsigned long cdirsize = *outsize;
  for(i=0;i<sizeof(CDIRPKh);++i) ZOPFLI_APPEND_DATA(CDIRPKh[i],out,outsize);

  /* MS-DOS TIME, CRC, OSIZE, ISIZE FROM */

  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((dostime >> (i*8)) % 256, out, outsize);

  /* CRC */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((crcvalue >> (i*8)) % 256,out,outsize);

  /* OSIZE + UPDATE IN PK HEADER */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((rawdeflsize >> (i*8)) % 256,out,outsize);
  for(i=0;i<4;++i) (*out)[18+i]=(rawdeflsize >> (i*8)) % 256;

  /* ISIZE */
  for(i=0;i<25;i+=8) ZOPFLI_APPEND_DATA((insize >> i) % 256,out,outsize);

  /* FILENAME LENGTH */
  for(i=0;i<2;++i) ZOPFLI_APPEND_DATA((max >> (i*8)) % 256,out,outsize);

  /* C-DIR STATIC DATA */
  for(i=0;i<sizeof(CDIRPKs);++i) ZOPFLI_APPEND_DATA(CDIRPKs[i],out,outsize);

  /* FilePK offset in ZIP file */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA(0,out,outsize);
  unsigned long cdiroffset= rawdeflsize + 30 + max;

  /* FILENAME */
  for(i=0; i<max;++i) ZOPFLI_APPEND_DATA(infilename[i],out,outsize);
  cdirsize = *outsize - cdirsize;

  /* END C-DIR PK STATIC DATA + TOTAL FILES (ALWAYS 1) */
  for(i=0;i<sizeof(EndCDIRPKh);++i) ZOPFLI_APPEND_DATA(EndCDIRPKh[i],out,outsize);

  /* C-DIR SIZE */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((cdirsize >> (i*8)) % 256,out, outsize);

  /* C-DIR OFFSET */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((cdiroffset >> (i*8)) % 256,out, outsize);

  /* NO COMMENTS IN END C-DIR */
  for(i=0;i<2;++i) ZOPFLI_APPEND_DATA(0, out, outsize);
}

/*
Compresses the data according to the gzip specification.
*/
static void ZopfliGzipCompress(unsigned mode, unsigned multithreading,
                        const unsigned char* in, size_t insize, time_t time,
                        unsigned char** out, size_t* outsize, std::string name = "") {
  unsigned crcvalue = crc32(0, in, insize);

  unsigned char has_name = name != "";
  std::string infile_str = name.substr(name.find_last_of('/') + 1);
  unsigned name_size = infile_str.length() + 1;
  const char* infilename = infile_str.c_str();

  (*out) = (unsigned char*)malloc(20);
  (*out)[*outsize] = 31; (*outsize)++;            /* ID1 */
  (*out)[*outsize] = 139; (*outsize)++;           /* ID2 */
  (*out)[*outsize] = 8; (*outsize)++;             /* CM  */
  (*out)[*outsize] = has_name << 3; (*outsize)++; /* FLG */

  /* MTIME */
  *(unsigned*)(&(*out)[*outsize]) = time & UINT_MAX; (*outsize) += 4;

  (*out)[*outsize] = 2; (*outsize)++;  /* XFL, 2 indicates best compression. */
  (*out)[*outsize] = 3; (*outsize)++;  /* OS follows Unix conventions. */

  if (has_name) {
    (*out) = (unsigned char*)realloc(*out, 20 + name_size);
    memcpy((&(*out)[*outsize]), infilename, name_size);
    (*outsize) += name_size;
  }

  //Use zlib-based compression
  if (mode == 1) {
    printf("zlib based compression (level = 1) is unsupported");
  } else {
    ZopfliBuffer(mode, multithreading, in, insize, out, outsize);
    (*out) = (unsigned char*)realloc(*out, *outsize + 8);
  }

  /* CRC */
  *(unsigned*)(&(*out)[*outsize]) = crcvalue; (*outsize) += 4;

  /* ISIZE */
  *(unsigned*)(&(*out)[*outsize]) = insize % UINT_MAX; (*outsize) += 4;
}

static void LoadGzip(const char *filename, unsigned char **out, long long *outsize_p) {
  *out = nullptr;
  *outsize_p = -1;
  FILE *file = fopen(filename, "rb");
  std::unique_ptr<FILE, decltype(&fclose)> file_(file, fclose);
  if (!file)
    return;

  unsigned char input[65536];
  z_stream strm = {};
  std::unique_ptr<z_stream, decltype(&inflateEnd)> strm_(&strm, inflateEnd);
  std::unique_ptr<unsigned char, decltype(&free)> output(nullptr, free);

  if (inflateInit2(&strm, -15) != Z_OK)
    return;
  auto refill = [&]() {
    if (!strm.avail_in) {
      strm.next_in = input;
      strm.avail_in = fread(input, 1, sizeof(input), file);
    }
    return strm.avail_in != 0;
  };
  auto read_byte = [&]() -> int {
    if (!refill())
      return EOF;
    --strm.avail_in;
    return *strm.next_in++;
  };
  auto read_number = [&](unsigned bytes, uint32_t *value) {
    *value = 0;
    for (unsigned byte = 0; byte < bytes; ++byte) {
      int next = read_byte();
      if (next == EOF)
        return false;
      *value |= (uint32_t)next << (8 * byte);
    }
    return true;
  };

  size_t alloc_size = 0;
  size_t out_size = 0;
  bool have_member = false;
  for (;;) {
    unsigned char header[10];
    int first = read_byte();
    if (first != 31)
      break;
    int second = read_byte();
    if (second != 139)
      break;
    header[0] = first;
    header[1] = second;
    for (unsigned byte = 2; byte < sizeof(header); ++byte) {
      int next = read_byte();
      if (next == EOF)
        goto inf_leave;
      header[byte] = next;
    }
    unsigned flags = header[3];
    if (header[2] != Z_DEFLATED || (flags & 0xe0))
      goto inf_leave;
    uint32_t check = crc32(0, header, sizeof(header));
    auto read_header_byte = [&]() {
      int next = read_byte();
      if (next != EOF && (flags & 2)) {
        unsigned char value = next;
        check = crc32(check, &value, 1);
      }
      return next;
    };
    if (flags & 4) {
      int low = read_header_byte();
      int high = read_header_byte();
      if (low == EOF || high == EOF)
        goto inf_leave;
      for (unsigned length = low + (high << 8); length; --length) {
        if (read_header_byte() == EOF)
          goto inf_leave;
      }
    }
    for (unsigned flag = 8; flag <= 16; flag <<= 1) {
      if (flags & flag) {
        int next;
        do {
          next = read_header_byte();
          if (next == EOF)
            goto inf_leave;
        } while (next);
      }
    }
    uint32_t hold;
    if ((flags & 2) && (!read_number(2, &hold) || hold != (check & 0xffff)))
      goto inf_leave;

    if (inflateReset(&strm) != Z_OK)
      goto inf_leave;
    check = 0;
    uint32_t total = 0;
    int ret;
    do {
      if (alloc_size - out_size < sizeof(input)) {
        size_t next_size = alloc_size ? alloc_size * 2 : sizeof(input);
        if (next_size <= alloc_size || next_size > SIZE_MAX - 16)
          goto inf_leave;
        unsigned char *resized = (unsigned char *)realloc(output.get(), next_size + 16);
        if (!resized)
          goto inf_leave;
        std::ignore = output.release(); // it is safe here
        output.reset(resized);
        alloc_size = next_size;
      }
      if (!refill() || ferror(file))
        goto inf_leave;
      unsigned have = strm.avail_in;
      strm.next_out = output.get() + out_size;
      strm.avail_out = sizeof(input);
      unsigned had = strm.avail_out;
      ret = inflate(&strm, Z_NO_FLUSH);
      size_t produced = had - strm.avail_out;
      check = crc32(check, output.get() + out_size, produced);
      total += produced;
      out_size += produced;
      if (ret != Z_OK && ret != Z_STREAM_END)
        goto inf_leave;
      if (ret != Z_STREAM_END && !produced && have == strm.avail_in)
        goto inf_leave;
    } while (ret != Z_STREAM_END);
    if (!read_number(4, &hold) || hold != check || !read_number(4, &hold) || hold != total)
      goto inf_leave;
    have_member = true;
  }
  if (!have_member || ferror(file) || out_size > (unsigned long long)LLONG_MAX)
    goto inf_leave;
  if (fclose(file_.release()) != 0)
    goto inf_leave;
  *outsize_p = (long long)out_size;
  *out = output.release();
  return;

inf_leave:
  fprintf(stderr, "%s: gzip decompression error\n", filename);
}

/*
 Loads a file into a memory array.
 */
static void LoadFile(const char* filename,
                     unsigned char** out, long long* outsize) {
  FILE* file = fopen(filename, "rb");
  if (!file) {*outsize = -1; return;}

  fseek(file , 0 , SEEK_END);
  *outsize = ftell(file);
  rewind(file);

  *out = (unsigned char*)malloc(*outsize + 16);
  if (!*out && *outsize){
    exit(1);
  }
  if (*outsize) {
    size_t testsize = fread(*out, 1, *outsize, file);
    if ((long long)testsize != *outsize) {
      /* It could be a directory */
      free(*out);
      *out = 0;
      *outsize = -1;
    }
  }

  fclose(file);
}

/*
 Saves a file from a memory array, overwriting the file if it existed.
 */
static int SaveFile(const char* filename,
                     const unsigned char* in, size_t insize) {
  FILE* file = fopen(filename, "wb");
  if (!file){
    fprintf(stderr, "Can't write to file %s\n", filename);
    return EXIT_FAILURE;
  }
  int result = fwrite(in, 1, insize, file) == insize ? EXIT_SUCCESS : EXIT_FAILURE;
  if (fclose(file) != 0) {
    result = EXIT_FAILURE;
  }
  if (result != EXIT_SUCCESS) {
    fprintf(stderr, "Can't write to file %s\n", filename);
  }
  return result;
}

/*
 outfilename: filename to write output to, or 0 to write to stdout instead
 */
int ZopfliGzip(const char* infilename, const char* outfilename, unsigned mode, unsigned multithreading, unsigned ZIP, unsigned char isGZ, const char* gzip_name) {
  unsigned char* in;
  long long insize = -1;
  unsigned char* out = 0;
  size_t outsize = 0;

  if (isGZ) {
    LoadGzip(infilename, &in, &insize);
  }
  else {
    LoadFile(infilename, &in, &insize);
  }
  if (insize < 0) {
    fprintf(stderr, "Invalid file: %s\n", infilename);
    return EXIT_FAILURE;
  }

  struct stat st;
  stat(infilename, &st);
  time_t time = st.st_mtime;
  if (!ZIP) {
    ZopfliGzipCompress(mode, multithreading, in, insize, time, &out, &outsize, (std::string)(gzip_name ? gzip_name : ""));
  }
  else {
    ZopfliZipCompress(mode, multithreading, in, insize, time, infilename, &out, &outsize);
  }
  free(in);

  int result = SaveFile(outfilename, out, outsize);

  free(out);
  return result;
}

int ZopfliGzipStream(unsigned mode, FILE *fd_in, FILE *fd_out) {
  const unsigned char header[] = {31, 139, 8, 0, 0, 0, 0, 0, 2, 3};
  if (fwrite(header, 1, sizeof(header), fd_out) != sizeof(header))
    return 1;
  if (mode == 1)
    exit(1); // do not use zlib, always zopfli
  if (mode % 10000 < 2)
    mode += 2 - mode % 10000;
  ZopfliOptions options;
  ZopfliInitOptions(&options, mode, 0, 0);
  size_t blocksize = ZOPFLI_MASTER_BLOCK_SIZE;
  if (options.numiterations == 1) blocksize /= 5;
  std::vector<unsigned char> in(ZOPFLI_WINDOW_SIZE + blocksize + 1 + 16);
  unsigned char* out = nullptr;
  size_t outsize = 0;
  unsigned char bp = 0;
  unsigned char costmodelnotinited = 1;
  size_t history = 0, pending = 0;
  uint32_t crc = 0, total = 0;
  int result = 1;
  while (true) {
    size_t size = pending + fread(in.data() + history + pending, 1, blocksize + 1 - pending, fd_in);
    if (ferror(fd_in))
      break;
    bool final = size <= blocksize;
    if (!final)
      size = blocksize;
    crc = crc32(crc, in.data() + history, size);
    total += size;
    if (size) {
      ZopfliDeflatePart(&options, final, in.data(), history, history + size, &bp, &out, &outsize, &costmodelnotinited);
    } else {
      ZopfliDeflate(&options, 1, in.data(), 0, &bp, &out, &outsize);
    }
    size_t written = outsize - (!final && bp != 0);
    if (fwrite(out, 1, written, fd_out) != written)
      break;
    if (final) {
      unsigned char trailer[8];
      for (unsigned byte = 0; byte < 4; byte++) {
        trailer[byte] = crc >> (byte * 8);
        trailer[byte + 4] = total >> (byte * 8);
      }
      result = fwrite(trailer, 1, sizeof(trailer), fd_out) != sizeof(trailer);
      break;
    }
    if (bp)
      out[0] = out[outsize - 1];
    outsize = bp != 0;
    size_t end = history + size;
    history = end < ZOPFLI_WINDOW_SIZE ? end : ZOPFLI_WINDOW_SIZE;
    memmove(in.data(), in.data() + end - history, history + 1);
    pending = 1;
  }
  free(out);
  return fflush(fd_out) == 0 ? result : 1;
}

void ZopfliBuffer(unsigned mode, unsigned multithreading, const unsigned char* in, size_t insize, unsigned char** out, size_t* outsize) {
  if (mode % 10000 < 2) {
    mode += 2 - mode % 10000;
  }
  ZopfliOptions options;
  ZopfliInitOptions(&options, mode, multithreading, 0);
  unsigned char bp = 0;
  ZopfliDeflate(&options, 1, in, insize, &bp, out, outsize);
}
