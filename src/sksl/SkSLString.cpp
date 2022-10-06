/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/private/SkSLString.h"
#include "include/private/SkStringView.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <locale>
#include <memory>
#include <sstream>
#include <string>

std::string skstd::to_string(float value) {
    return skstd::to_string((double)value);
}

std::string skstd::to_string(double value) {
    std::stringstream buffer;
    buffer.imbue(std::locale::classic());
    buffer.precision(7);
    buffer << value;
    std::string text = buffer.str();

    float roundtripped;
    buffer >> roundtripped;
    if (roundtripped != (float)value) {
        buffer.str({});
        buffer.clear();
        buffer.precision(9);
        buffer << value;
        text = buffer.str();
        // TODO(johnstiles): on older versions of libc++, FLT_MAX will fill the buffer with the
        // correct value, but reading back into `roundtripped` gives INF. Disable the assertion
        // until we figure out a better way to check this.
        //SkASSERTF((buffer >> roundtripped, roundtripped == (float)value), "%.17g", value);
    }

    // We need to emit a decimal point to distinguish floats from ints.
    if (!skstd::contains(text, '.') && !skstd::contains(text, 'e')) {
        text += ".0";
    }

    return text;
}

bool SkSL::stod(std::string_view s, SKSL_FLOAT* value) {
    std::string str(s.data(), s.size());
    std::stringstream buffer(str);
    buffer.imbue(std::locale::classic());
    buffer >> *value;
    return !buffer.fail() && std::isfinite(*value);
}

bool SkSL::stoi(std::string_view s, SKSL_INT* value) {
    if (s.empty()) {
        return false;
    }
    char suffix = s.back();
    if (suffix == 'u' || suffix == 'U') {
        s.remove_suffix(1);
    }
    std::string str(s);  // s is not null-terminated
    const char* strEnd = str.data() + str.length();
    char* p;
    errno = 0;
    unsigned long long result = strtoull(str.data(), &p, /*base=*/0);
    *value = static_cast<SKSL_INT>(result);
    return p == strEnd && errno == 0 && result <= 0xFFFFFFFF;
}

std::string SkSL::String::printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::string result;
    vappendf(&result, fmt, args);
    va_end(args);
    return result;
}

void SkSL::String::appendf(std::string *str, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vappendf(str, fmt, args);
    va_end(args);
}

void SkSL::String::vappendf(std::string *str, const char* fmt, va_list args) {
    #define BUFFER_SIZE 256
    char buffer[BUFFER_SIZE];
    va_list reuse;
    va_copy(reuse, args);
    size_t size = vsnprintf(buffer, BUFFER_SIZE, fmt, args);
    if (BUFFER_SIZE >= size + 1) {
        str->append(buffer, size);
    } else {
        auto newBuffer = std::unique_ptr<char[]>(new char[size + 1]);
        vsnprintf(newBuffer.get(), size + 1, fmt, reuse);
        str->append(newBuffer.get(), size);
    }
    va_end(reuse);
}
