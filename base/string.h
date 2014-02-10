#ifndef STRING_H_
#define STRING_H_

#include <string>

std::string StringPrintf(const char* format, ...);

bool HasPrefix(const std::string& haystack, const std::string& needle);

#endif  // !STRING_H_
