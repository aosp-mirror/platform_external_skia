"""
THIS IS THE EXTERNAL-ONLY VERSION OF THIS FILE. G3 HAS ITS OWN.

This file contains general helper macros that make our BUILD.bazel files easier to read.
"""

# https://github.com/bazelbuild/bazel-skylib
load("@bazel_skylib//lib:selects.bzl", _selects = "selects")
load("//bazel:flags.bzl", _bool_flag = "bool_flag", _string_flag_with_values = "string_flag_with_values")
load(
    "//bazel:skia_rules.bzl",
    _exports_files_legacy = "exports_files_legacy",
    _generate_cpp_files_for_header_list = "generate_cpp_files_for_header_list",
    _generate_cpp_files_for_headers = "generate_cpp_files_for_headers",
    _select_multi = "select_multi",
    _skia_cc_binary = "skia_cc_binary",
    _skia_cc_binary_with_flags = "skia_cc_binary_with_flags",
    _skia_cc_deps = "skia_cc_deps",
    _skia_cc_library = "skia_cc_library",
    _skia_defines = "skia_defines",
    _skia_filegroup = "skia_filegroup",
    _skia_objc_library = "skia_objc_library",
    _split_srcs_and_hdrs = "split_srcs_and_hdrs",
)

# re-export symbols that are commonly used or that are not supported in G3
# (and thus we need to stub out)
bool_flag = _bool_flag
selects = _selects
string_flag_with_values = _string_flag_with_values
generate_cpp_files_for_headers = _generate_cpp_files_for_headers
generate_cpp_files_for_header_list = _generate_cpp_files_for_header_list

exports_files_legacy = _exports_files_legacy
select_multi = _select_multi
skia_cc_binary = _skia_cc_binary
skia_cc_binary_with_flags = _skia_cc_binary_with_flags
skia_cc_deps = _skia_cc_deps
skia_cc_library = _skia_cc_library
skia_defines = _skia_defines
skia_filegroup = _skia_filegroup
skia_objc_library = _skia_objc_library
split_srcs_and_hdrs = _split_srcs_and_hdrs
