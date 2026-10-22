//
//  support.cpp
//  Efficient Compression Tool
//
//  Created by Felix Hanau on 09.04.15.
//  Copyright (c) 2015 Felix Hanau.
//

#include "support.h"
#include <sys/stat.h>
#ifdef _WIN32
#include <Windows.h>
#endif
#ifdef _MSC_VER
#include <io.h>
#include <sys/utime.h>
#define access _access
#define W_OK 2
#else
#include <utime.h>
#endif
#include <stdio.h>
#include <time.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <vector>
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(__linux__)
#include <climits>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#else
#error Unsupported platform
#endif

long long filesize(const char *Infile) {
  struct stat stats;
  if (stat(Infile, &stats) != 0) {
    return -1;
  }
  return stats.st_size;
}

bool exists(const char *Infile) {
  struct stat stats;
  return stat(Infile, &stats) == 0;
}

bool writepermission(const char *Infile) { return !access(Infile, W_OK); }

void RenameAndReplace(const char *Infile, const char *Outfile) {
#ifdef _WIN32
  MoveFileExA(Infile, Outfile, MOVEFILE_REPLACE_EXISTING);
#else
  rename(Infile, Outfile);
#endif
}

bool isDirectory(const char *path) {
  struct stat sb;
  if (!stat(path, &sb))
    return (sb.st_mode & S_IFDIR) != 0;
  return false;
}

time_t get_file_time(const char *Infile) {
  struct stat stats;
  if (stat(Infile, &stats)) {
    printf("%s: Could not get time\n", Infile);
    return -1;
  }
  return stats.st_mtime;
}

void set_file_time(const char *Infile, time_t otime) {
  struct utimbuf oldtime;
  oldtime.actime = time(0);
  oldtime.modtime = otime;
  if (utime(Infile, &oldtime)) {
    printf("%s: Could not set time\n", Infile);
  }
}

// cpu phy core count, inspired by https://github.com/pytorch/cpuinfo , License:
// BSD-2-Clause-1-ov-file

#if defined(__linux__)
std::set<int> linux_read_cpu_list(const std::string &path) {
  std::ifstream file(path);
  std::string text;
  if (!(file >> text))
    return {};
  std::istringstream input(text);
  std::set<int> cpus;
  while (input.peek() != EOF) {
    int first, last;
    if (!(input >> first) || first < 0)
      return {};
    last = first;
    if (input.peek() == '-') {
      input.get();
      if (!(input >> last) || last < first)
        return {};
    }
    if (last == INT_MAX)
      return {};
    for (int cpu = first; cpu <= last; ++cpu)
      cpus.insert(cpu);
    if (input.peek() == EOF)
      break;
    if (input.get() != ',' || input.peek() == EOF)
      return {};
  }
  return cpus;
}

int get_linux_phy_core_count(const std::string &root) {
  // cpuinfo/src/x86/linux/init.c and src/arm/linux/init.c select CPUs
  // from present/possible, not online: offline CPUs may still be present.
  auto cpus = linux_read_cpu_list(root + "/present");
  const auto possible = linux_read_cpu_list(root + "/possible");
  if (cpus.empty()) {
    cpus = possible;
  } else if (!possible.empty()) {
    for (auto cpu = cpus.begin(); cpu != cpus.end();) {
      if (possible.count(*cpu) == 0)
        cpu = cpus.erase(cpu);
      else
        ++cpu;
    }
  }
  if (cpus.empty())
    return 1;

  std::set<int> covered;
  int count = 0;
  for (int cpu : cpus) {
    if (covered.count(cpu))
      continue;
    const auto siblings = linux_read_cpu_list(
        root + "/cpu" + std::to_string(cpu) + "/topology/thread_siblings_list");
    if (siblings.count(cpu) == 0)
      continue;
    ++count;
    covered.insert(siblings.begin(), siblings.end());
  }
  // cpuinfo/src/arm/linux/init.c notes that offline CPUs may lack sysfs
  // topology. Another thread's sibling list can still identify their core.
  // Unlike cpuinfo's full architecture-specific detection, this small
  // implementation returns 1 if any present CPU remains unaccounted for.
  for (int cpu : cpus) {
    if (covered.count(cpu) == 0)
      return 1;
  }
  return count > 0 ? count : 1;
}
#endif

int get_phy_core_count() {
#if defined(_WIN32)
  DWORD size = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &size);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
    return 1;
  std::vector<unsigned char> buffer(size);
  auto *info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(
      buffer.data());
  if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &size))
    return 1;
  int count = 0;
  // As in cpuinfo/src/x86/windows/init.c, records have variable sizes.
  // Count core records, not bits in a logical-processor affinity mask.
  for (DWORD offset = 0; offset < size;) {
    const size_t headerSize =
        offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor);
    if (size - offset < headerSize)
      return 1;
    info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(
        buffer.data() + offset);
    if (info->Size < headerSize || info->Size > size - offset)
      return 1;
    if (info->Relationship == RelationProcessorCore)
      ++count;
    offset += info->Size;
  }
  return count > 0 ? count : 1;
#elif defined(__APPLE__)
  // Same key as cpuinfo/src/mach/topology.c; includes all performance levels.
  int count = 0;
  size_t size = sizeof(count);
  if (sysctlbyname("hw.physicalcpu_max", &count, &size, nullptr, 0) != 0)
    return 1;
  return count > 0 ? count : 1;
#elif defined(__linux__)
  return get_linux_phy_core_count("/sys/devices/system/cpu");
#endif
}

// extern "C" int getPhysicalCoreCount()
// int getEnvThreadsCount() {
//     static const int ret = []() {
//         const char* value = std::getenv("ZIPALIGN_THREADS_COUNT");
//         if (value == nullptr) return 1;
//         if (std::strcmp(value, "phy") == 0) return getPhysicalCoreCount();
//         int count = static_cast<int>(std::strtol(value, nullptr, 10));
//         return count == 0 ?
//         static_cast<int>(std::thread::hardware_concurrency()) : count;
//     }();
//     return ret;
// }
//
// int main() {
//     std::cout << getEnvThreadsCount() << '\n';
//     return 0;
// }
