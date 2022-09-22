"""
This file assembles a toolchain for an amd64 Linux host using the Clang Compiler and glibc.

It downloads the necessary headers, executables, and pre-compiled static/shared libraries to
the external subfolder of the Bazel cache (the same place third party deps are downloaded with
http_archive or similar functions in WORKSPACE.bazel). These will be able to be used via our
custom c++ toolchain configuration (see //toolchain/linux_amd64_toolchain_config.bzl)

Most files are downloaded as .deb files from packages.debian.org (with us acting as the dependency
resolver) and extracted to
  [outputRoot (aka Bazel cache)]/[outputUserRoot]/[outputBase]/external/clang_linux_amd64
  (See https://bazel.build/docs/output_directories#layout-diagram)
which will act as our sysroot.
"""

load("//toolchain:utils.bzl", "gcs_mirror_url")

# From https://github.com/llvm/llvm-project/releases/download/llvmorg-13.0.0/clang+llvm-13.0.0-x86_64-linux-gnu-ubuntu-20.04.tar.xz.sha256
clang_prefix = "clang+llvm-13.0.0-x86_64-linux-gnu-ubuntu-20.04/"
clang_sha256 = "2c2fb857af97f41a5032e9ecadf7f78d3eff389a5cd3c9ec620d24f134ceb3c8"
clang_url = "https://github.com/llvm/llvm-project/releases/download/llvmorg-13.0.0/clang+llvm-13.0.0-x86_64-linux-gnu-ubuntu-20.04.tar.xz"

debs_to_install = [
    # These three comprise glibc. libc6 has the shared libraries, like libc itself, the math library
    # (libm), etc. linux-libc-dev has the header files specific to linux. libc6-dev has the libc
    # system headers (e.g. malloc.h, math.h).
    {
        # From https://packages.debian.org/bullseye/amd64/libc6/download
        "sha256": "a6263062b476cee1052972621d473b159debec6e424f661eda88248b00331d79",
        "url": "https://ftp.debian.org/debian/pool/main/g/glibc/libc6_2.31-13+deb11u4_amd64.deb",
    },
    {
        # From https://packages.debian.org/bullseye/amd64/linux-libc-dev/download
        "sha256": "e89023a5fc58c30ebb8cbb82de77f872baeafe7a5449f574b03cea478f7e9e6d",
        "url": "https://ftp.debian.org/debian/pool/main/l/linux/linux-libc-dev_5.10.140-1_amd64.deb",
    },
    {
        # From https://packages.debian.org/bullseye/amd64/libc6-dev/download
        "sha256": "5f368eb89d102ccd23529a02fb17aaa1c15e7612506e22ef0c559b71f5049a91",
        "url": "https://ftp.debian.org/debian/pool/main/g/glibc/libc6-dev_2.31-13+deb11u4_amd64.deb",
    },
    # These two put the X11 include files in ${PWD}/usr/include/X11
    # libx11-dev puts libX11.a in ${PWD}/usr/lib/x86_64-linux-gnu
    {
        # From https://packages.debian.org/bullseye/amd64/libx11-dev/download
        "sha256": "11e5f9dcded1a1226b3ee02847b86edce525240367b3989274a891a43dc49f5f",
        "url": "https://ftp.debian.org/debian/pool/main/libx/libx11/libx11-dev_1.7.2-1_amd64.deb",
    },
    {
        # From https://packages.debian.org/bullseye/amd64/libx11-6/download
        "sha256": "086bd667fc07369472a923da015d182bb0c15a72228a5c0e6ddbcbeaab70acd2",
        "url": "https://ftp.debian.org/debian/pool/main/libx/libx11/libx11-6_1.7.2-1_amd64.deb",
    },
    {
        # From https://packages.debian.org/bullseye/all/x11proto-dev/download
        "sha256": "d5568d587d9ad2664c34c14b0ac538ccb3c567e126ee5291085a8de704a565f5",
        "url": "https://ftp.debian.org/debian/pool/main/x/xorgproto/x11proto-dev_2020.1-1_all.deb",
    },
    # xcb is a dep of X11
    {
        # From https://packages.debian.org/bullseye/amd64/libxcb1-dev/download
        "sha256": "b75544f334c8963b8b7b0e8a88f8a7cde95a714dddbcda076d4beb669a961b58",
        "url": "https://ftp.debian.org/debian/pool/main/libx/libxcb/libxcb1-dev_1.14-3_amd64.deb",
    },
    {
        # From https://packages.debian.org/bullseye/amd64/libxcb1/download
        "sha256": "d5e0f047ed766f45eb7473947b70f9e8fddbe45ef22ecfd92ab712c0671a93ac",
        "url": "https://ftp.debian.org/debian/pool/main/libx/libxcb/libxcb1_1.14-3_amd64.deb",
    },
    # Xau is a dep of xcb
    {
        # From https://packages.debian.org/bullseye/amd64/libxau-dev/download
        "sha256": "d1a7f5d484e0879b3b2e8d512894744505e53d078712ce65903fef2ecfd824bb",
        "url": "https://ftp.debian.org/debian/pool/main/libx/libxau/libxau-dev_1.0.9-1_amd64.deb",
    },
    {
        # From https://packages.debian.org/bullseye/amd64/libxau6/download
        "sha256": "679db1c4579ec7c61079adeaae8528adeb2e4bf5465baa6c56233b995d714750",
        "url": "https://ftp.debian.org/debian/pool/main/libx/libxau/libxau6_1.0.9-1_amd64.deb",
    },

    # Xdmcp is a dep of xcb. libxdmcp-dev provides the the libXdmcp.so symlink (and the
    # .a if we want to statically include it). libxdmcp6 actually provides the .so file
    {
        # https://packages.debian.org/bullseye/amd64/libxdmcp-dev/download
        "sha256": "c6733e5f6463afd261998e408be6eb37f24ce0a64b63bed50a87ddb18ebc1699",
        "url": "https://ftp.debian.org/debian/pool/main/libx/libxdmcp/libxdmcp-dev_1.1.2-3_amd64.deb",
    },
    {
        # https://packages.debian.org/bullseye/amd64/libxdmcp6/download
        "sha256": "ecb8536f5fb34543b55bb9dc5f5b14c9dbb4150a7bddb3f2287b7cab6e9d25ef",
        "url": "https://ftp.debian.org/debian/pool/main/libx/libxdmcp/libxdmcp6_1.1.2-3_amd64.deb",
    },
    # These two put GL include files in ${PWD}/usr/include/GL
    {
        # From https://packages.debian.org/bullseye/amd64/libgl-dev/download
        "sha256": "a6487873f2706bbabf9346cdb190f47f23a1464f31cecf92c363bac37c342f2f",
        "url": "https://ftp.debian.org/debian/pool/main/libg/libglvnd/libgl-dev_1.3.2-1_amd64.deb",
    },
    {
        # From https://packages.debian.org/bullseye/amd64/libglx-dev/download
        "sha256": "5a50549948bc4363eab32b1083dad2165402c3628f2ee85e9a32563228cc61c1",
        "url": "https://ftp.debian.org/debian/pool/main/libg/libglvnd/libglx-dev_1.3.2-1_amd64.deb",
    },
    {
        # From https://packages.debian.org/bullseye/amd64/libglx0/download
        "sha256": "cb642200f7e28e6dbb4075110a0b441880eeec35c8a00a2198c59c53309e5e17",
        "url": "https://ftp.debian.org/debian/pool/main/libg/libglvnd/libglx0_1.3.2-1_amd64.deb",
    },
    # This provides libGL.so for us to link against.
    {
        # From https://packages.debian.org/bullseye/amd64/libgl1/download
        "sha256": "f300f9610b5f05f1ce566c4095f1bf2170e512ac5d201c40d895b8fce29dec98",
        "url": "https://ftp.debian.org/debian/pool/main/libg/libglvnd/libgl1_1.3.2-1_amd64.deb",
    },
    # This is used by sk_app for Vulkan and Dawn on Unix.
    {
        # From https://packages.debian.org/bullseye/amd64/libx11-xcb-dev/download
        "sha256": "80a2413ace2a0a073f2472059b9e589737cbf8a336fb6862684a5811bf640aa3",
        "url": "https://ftp.debian.org/debian/pool/main/libx/libx11/libx11-xcb-dev_1.7.2-1_amd64.deb",
    },
    {
        # From https://packages.debian.org/bullseye/amd64/libx11-xcb1/download
        "sha256": "1f9f2dbe7744a2bb7f855d819f43167df095fe7d5291546bec12865aed045e0c",
        "url": "https://ftp.debian.org/debian/pool/main/libx/libx11/libx11-xcb1_1.7.2-1_amd64.deb",
    },
    # This is used to make sure we include only the headers we need. This corresponds to
    # IWYU version 0.17, which uses Clang 13, like we compile with.
    {
        # From https://packages.debian.org/sid/amd64/iwyu/download
        "sha256": "9fd6932a7609e89364f7edc5f9613892c98c21c88a3931e51cf1a0f8744759bd",
        "url": "https://ftp.debian.org/debian/pool/main/i/iwyu/iwyu_8.17-1_amd64.deb",
    },
    {
        # This is a requirement of iwyu
        # https://packages.debian.org/sid/amd64/libclang-cpp13/download
        "sha256": "c6e2471de8f3ec06e40c8e006e06bbd251dd0c8000dee820a4b6dca3d3290c0d",
        "url": "https://ftp.debian.org/debian/pool/main/l/llvm-toolchain-13/libclang-cpp13_13.0.1-3+b1_amd64.deb",
    },
    {
        # This is a requirement of libclang-cpp13
        # https://packages.debian.org/sid/amd64/libstdc++6/download
        "sha256": "f37e5954423955938c5309a8d0e475f7e84e92b56b8301487fb885192dee8085",
        "url": "https://ftp.debian.org/debian/pool/main/g/gcc-12/libstdc++6_12-20220319-1_amd64.deb",
    },
    {
        # This is a requirement of iwyu
        # https://packages.debian.org/sid/amd64/libllvm13/download
        "sha256": "49f29a6c9fbc3097077931529e7fe1c032b1d04a984d971aa1e6990a5133556e",
        "url": "https://ftp.debian.org/debian/pool/main/l/llvm-toolchain-13/libllvm13_13.0.1-3+b1_amd64.deb",
    },
    {
        # This is a requirement of libllvm13
        # https://packages.debian.org/sid/amd64/libffi8/download
        "sha256": "87c55b36951aed18ef2c357683e15c365713bda6090f15386998b57df433b387",
        "url": "https://ftp.debian.org/debian/pool/main/libf/libffi/libffi8_3.4.2-4_amd64.deb",
    },
    {
        # This is a requirement of libllvm13
        # https://packages.debian.org/sid/libz3-4/download
        "sha256": "b415b863678625dee3f3c75bd48b1b9e3b6e11279ebec337904d7f09630d107f",
        "url": "https://ftp.debian.org/debian/pool/main/z/z3/libz3-4_4.8.12-1+b1_amd64.deb",
    },
    {
        # https://packages.debian.org/bullseye/libfontconfig-dev/download
        "sha256": "7655d4238ee7e6ced13501006d20986cbf9ff08454a4e502d5aa399f83e28876",
        "url": "https://ftp.debian.org/debian/pool/main/f/fontconfig/libfontconfig-dev_2.13.1-4.2_amd64.deb",
    },
    {
        # https://packages.debian.org/bullseye/amd64/libfontconfig1/download
        "sha256": "b92861827627a76e74d6f447a5577d039ef2f95da18af1f29aa98fb96baea4c1",
        "url": "https://ftp.debian.org/debian/pool/main/f/fontconfig/libfontconfig1_2.13.1-4.2_amd64.deb",
    },
    {
        # https://packages.debian.org/bullseye/libglu1-mesa-dev/download
        "sha256": "5df6abeedb1f6986cec4b17810ef1a2773a5cd3291544abacc2bf602a9520893",
        "url": "https://ftp.debian.org/debian/pool/main/libg/libglu/libglu1-mesa-dev_9.0.1-1_amd64.deb",
    },
    {
        # https://packages.debian.org/bullseye/amd64/libglu1-mesa/download
        "sha256": "479736c235af0537c1af8df4befc32e638a4e979961fdb02f366501298c50526",
        "url": "https://ftp.debian.org/debian/pool/main/libg/libglu/libglu1-mesa_9.0.1-1_amd64.deb",
    },
]

def _download_and_extract_deb(ctx, deb, sha256, prefix, output = ""):
    """Downloads a debian file and extracts the data into the provided output directory"""

    # https://bazel.build/rules/lib/repository_ctx#download_and_extract
    # A .deb file has a data.tar.xz and a control.tar.xz, but the important contents
    # (i.e. the headers or libs) are in the data.tar.xz
    ctx.download_and_extract(
        url = gcs_mirror_url(deb, sha256),
        output = "tmp",
        sha256 = sha256,
    )

    # https://bazel.build/rules/lib/repository_ctx#extract
    ctx.extract(
        archive = "tmp/data.tar.xz",
        output = output,
        stripPrefix = prefix,
    )

    # Clean up
    ctx.delete("tmp")

def _download_linux_amd64_toolchain_impl(ctx):
    # Download the clang toolchain (the extraction can take a while)
    # https://bazel.build/rules/lib/repository_ctx#download_and_extract
    ctx.download_and_extract(
        url = gcs_mirror_url(clang_url, clang_sha256),
        output = "",
        stripPrefix = clang_prefix,
        sha256 = clang_sha256,
    )

    # Extract all the debs into our sysroot. This is very similar to installing them, except their
    # dependencies are not installed automatically.
    for deb in debs_to_install:
        _download_and_extract_deb(
            ctx,
            deb["url"],
            deb["sha256"],
            ".",
        )

    # Create a BUILD.bazel file that makes the files downloaded into the toolchain visible.
    # We have separate groups for each task because doing less work (sandboxing fewer files
    # or uploading less data to RBE) makes compiles go faster. We try to strike a balance
    # between minimal specifications and not having to edit this file often with our use
    # of globs.
    # https://bazel.build/rules/lib/repository_ctx#file
    ctx.file(
        "BUILD.bazel",
        content = """
# DO NOT EDIT THIS BAZEL FILE DIRECTLY
# Generated from ctx.file action in download_linux_amd64_toolchain.bzl
filegroup(
    name = "archive_files",
    srcs = [
        "bin/llvm-ar",
    ],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "compile_files",
    srcs = [
        "bin/clang",
        "usr/bin/include-what-you-use",
    ] + glob(
        include = [
            "include/c++/v1/**",
            "usr/include/**",
            "lib/clang/13.0.0/**",
            "usr/include/x86_64-linux-gnu/**",
        ],
        allow_empty = False,
    ),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "link_files",
    srcs = [
        "bin/clang",
        "bin/ld.lld",
        "bin/lld",
        "lib/libc++.a",
        "lib/libc++abi.a",
        "lib/libunwind.a",
        "lib64/ld-linux-x86-64.so.2",
    ] + glob(
        include = [
            "lib/clang/13.0.0/lib/**",
            "lib/x86_64-linux-gnu/**",
            "usr/lib/x86_64-linux-gnu/**",
        ],
        allow_empty = False,
    ),
    visibility = ["//visibility:public"],
)
""",
        executable = False,
    )

# https://bazel.build/rules/repository_rules
download_linux_amd64_toolchain = repository_rule(
    implementation = _download_linux_amd64_toolchain_impl,
    attrs = {},
    doc = "Downloads clang, and all supporting headers, executables, " +
          "and shared libraries required to build Skia on a Linux amd64 host",
)
