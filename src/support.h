//
//  support.h
//  Efficient Compression Tool
//
//  Created by Felix Hanau on 09.04.15.
//  Copyright (c) 2015 Felix Hanau.
//

#ifndef __Efficient_Compression_Tool__support__
#define __Efficient_Compression_Tool__support__

#ifndef _WIN32
#include <unistd.h>
#endif
#include <cstdint>
#include <ctime>

#define assert_with_msg(v, msg, ...) do { if (!(v)) { fprintf(stderr, "error: " msg "\n", ##__VA_ARGS__); exit(1); } } while (0)

// Returns Filesize of Infile
long long filesize (const char * Infile);

bool exists(const char * Infile);

bool writepermission (const char * Infile);

void RenameAndReplace(const char * Infile, const char * Outfile);

bool isDirectory(const char *path);

time_t get_file_time(const char* Infile);

void set_file_time(const char* Infile, time_t otime);

int read_file(const char *name, uint8_t **out_buf, size_t *out_size);

int parse_threads_count(const char *env_name, const char *opt_v);

int get_phy_core_count();

#endif /* defined(__Efficient_Compression_Tool__support__) */
