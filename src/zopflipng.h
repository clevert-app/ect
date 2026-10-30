#ifndef ZOPFLI_ZOPFLIPNG_H_
#define ZOPFLI_ZOPFLIPNG_H_
#include <vector>
constexpr int ZOPFLIPNG_FILTER_AUTO = 255;
int Zopflipng(bool strip, const std::vector<unsigned char> &buf_in, std::vector<unsigned char> &buf_out, bool strict, unsigned Mode, int filter, unsigned multithreading, unsigned quiet);
#endif
