#ifndef ZOPFLI_ZOPFLI_GZIP_H_
#define ZOPFLI_ZOPFLI_GZIP_H_
int ZopfliGzip(const char* filename, const char* outname, unsigned mode, unsigned multithreading, unsigned ZIP, unsigned char isGZ, const char* gzip_name);
int ZopfliGzipStream(unsigned mode, FILE *fd_in, FILE *fd_out);
void ZopfliBuffer(unsigned mode, unsigned multithreading, const unsigned char* in, size_t insize, unsigned char** out, size_t* outsize);
#endif
