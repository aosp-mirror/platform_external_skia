use_relative_paths = True

vars = {
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling different
  # dependencies without interference from each other.
  'sk_tool_revision': 'git_revision:bff0baff15ff421160129c70d8af2a91d0e28d4c',
}

deps = {
  "buildtools"                                   : "https://chromium.googlesource.com/chromium/src/buildtools.git@b138e6ce86ae843c42a1a08f37903207bebcca75",
  "third_party/externals/angle2"                 : "https://chromium.googlesource.com/angle/angle.git@c6c7ff3c45bb4cc529bcbb03f1a217310e872ad1",
  "third_party/externals/brotli"                 : "https://skia.googlesource.com/external/github.com/google/brotli.git@e61745a6b7add50d380cfd7d3883dd6c62fc2c71",
  "third_party/externals/d3d12allocator"         : "https://skia.googlesource.com/external/github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator.git@169895d529dfce00390a20e69c2f516066fe7a3b",
  # Dawn requires jinja2 and markupsafe for the code generator, tint for SPIRV compilation, and abseil for string formatting.
  # When the Dawn revision is updated these should be updated from the Dawn DEPS as well.
  "third_party/externals/dawn"                   : "https://dawn.googlesource.com/dawn.git@e0bf254f720e3c921afbd5fcd1df3f2497256332",
  "third_party/externals/jinja2"                 : "https://chromium.googlesource.com/chromium/src/third_party/jinja2@ee69aa00ee8536f61db6a451f3858745cf587de6",
  "third_party/externals/markupsafe"             : "https://chromium.googlesource.com/chromium/src/third_party/markupsafe@0944e71f4b2cb9a871bcbe353f95e889b64a611a",
  "third_party/externals/abseil-cpp"             : "https://skia.googlesource.com/external/github.com/abseil/abseil-cpp.git@c5a424a2a21005660b182516eb7a079cd8021699",
  "third_party/externals/dng_sdk"                : "https://android.googlesource.com/platform/external/dng_sdk.git@c8d0c9b1d16bfda56f15165d39e0ffa360a11123",
  "third_party/externals/egl-registry"           : "https://skia.googlesource.com/external/github.com/KhronosGroup/EGL-Registry@a0bca08de07c7d7651047bedc0b653cfaaa4f2ae",
  "third_party/externals/emsdk"                  : "https://skia.googlesource.com/external/github.com/emscripten-core/emsdk.git@4a48a752e6a8bef6f222622f2b4926d5eb3bdeb3",
  "third_party/externals/expat"                  : "https://chromium.googlesource.com/external/github.com/libexpat/libexpat.git@a28238bdeebc087071777001245df1876a11f5ee",
  "third_party/externals/freetype"               : "https://chromium.googlesource.com/chromium/src/third_party/freetype2.git@7cd3f19f21cc9d600e3b765ef2058474d20233e2",
  "third_party/externals/harfbuzz"               : "https://chromium.googlesource.com/external/github.com/harfbuzz/harfbuzz.git@f1f2be776bcd994fa9262622e1a7098a066e5cf7",
  "third_party/externals/highway"                : "https://chromium.googlesource.com/external/github.com/google/highway.git@424360251cdcfc314cfc528f53c872ecd63af0f0",
  "third_party/externals/icu"                    : "https://chromium.googlesource.com/chromium/deps/icu.git@a0718d4f121727e30b8d52c7a189ebf5ab52421f",
  "third_party/externals/imgui"                  : "https://skia.googlesource.com/external/github.com/ocornut/imgui.git@55d35d8387c15bf0cfd71861df67af8cfbda7456",
  "third_party/externals/libavif"                : "https://github.com/AOMediaCodec/libavif.git@f49462dc93784bf34148715eee36ab6697ca0b35",
  "third_party/externals/libgav1"                : "https://chromium.googlesource.com/codecs/libgav1.git@0fb779c1e169fe6c229cd1fa9cc6ea6feeb441da",
  "third_party/externals/libgifcodec"            : "https://skia.googlesource.com/libgifcodec@fd59fa92a0c86788dcdd84d091e1ce81eda06a77",
  "third_party/externals/libjpeg-turbo"          : "https://chromium.googlesource.com/chromium/deps/libjpeg_turbo.git@22f1a22c99e9dde8cd3c72ead333f425c5a7aa77",
  "third_party/externals/libjxl"                 : "https://chromium.googlesource.com/external/gitlab.com/wg1/jpeg-xl.git@a205468bc5d3a353fb15dae2398a101dff52f2d3",
  "third_party/externals/libpng"                 : "https://skia.googlesource.com/third_party/libpng.git@386707c6d19b974ca2e3db7f5c61873813c6fe44",
  "third_party/externals/libwebp"                : "https://chromium.googlesource.com/webm/libwebp.git@a8e366166ab57bb1b4aaf6739fc775515bc71b51",
  "third_party/externals/libyuv"                 : "https://chromium.googlesource.com/libyuv/libyuv.git@d248929c059ff7629a85333699717d7a677d8d96",
  "third_party/externals/microhttpd"             : "https://android.googlesource.com/platform/external/libmicrohttpd@748945ec6f1c67b7efc934ab0808e1d32f2fb98d",
  "third_party/externals/oboe"                   : "https://chromium.googlesource.com/external/github.com/google/oboe.git@b02a12d1dd821118763debec6b83d00a8a0ee419",
  "third_party/externals/opengl-registry"        : "https://skia.googlesource.com/external/github.com/KhronosGroup/OpenGL-Registry@14b80ebeab022b2c78f84a573f01028c96075553",
  "third_party/externals/perfetto"               : "https://android.googlesource.com/platform/external/perfetto@93885509be1c9240bc55fa515ceb34811e54a394",
  "third_party/externals/piex"                   : "https://android.googlesource.com/platform/external/piex.git@bb217acdca1cc0c16b704669dd6f91a1b509c406",
  "third_party/externals/rive"                   : "https://skia.googlesource.com/external/github.com/rive-app/rive-cpp.git@c9ff7391efb75e81c3670ddca77a5893538a26fa",
  "third_party/externals/sfntly"                 : "https://chromium.googlesource.com/external/github.com/googlei18n/sfntly.git@b55ff303ea2f9e26702b514cf6a3196a2e3e2974",
  "third_party/externals/swiftshader"            : "https://swiftshader.googlesource.com/SwiftShader@1c3dfde53353d5e13ca25eb52aa208d450b5e980",
  "third_party/externals/vulkanmemoryallocator"  : "https://chromium.googlesource.com/external/github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator@7de5cc00de50e71a3aab22dea52fbb7ff4efceb6",
  # vulkan-deps is a meta-repo containing several interdependent Khronos Vulkan repositories.
  # When the vulkan-deps revision is updated, those repos (spirv-*, vulkan-*) should be updated as well.
  "third_party/externals/vulkan-deps"            : "https://chromium.googlesource.com/vulkan-deps@a1ba8ef3ad46339e5f413facc1b84dfbaf088dc4",
  "third_party/externals/spirv-cross"            : "https://chromium.googlesource.com/external/github.com/KhronosGroup/SPIRV-Cross@61c603f3baa5270e04bcfb6acf83c654e3c57679",
  "third_party/externals/spirv-headers"          : "https://skia.googlesource.com/external/github.com/KhronosGroup/SPIRV-Headers.git@0bcc624926a25a2a273d07877fd25a6ff5ba1cfb",
  "third_party/externals/spirv-tools"            : "https://skia.googlesource.com/external/github.com/KhronosGroup/SPIRV-Tools.git@0073a1fa36f7c52ad3d58059cb5d5de8efa825ad",
  "third_party/externals/vulkan-headers"         : "https://chromium.googlesource.com/external/github.com/KhronosGroup/Vulkan-Headers@715673702f5b18ffb8e5832e67cf731468d32ac6",
  "third_party/externals/vulkan-tools"           : "https://chromium.googlesource.com/external/github.com/KhronosGroup/Vulkan-Tools@d55c7aaf041af331bee8c22fb448a6ff4c797f73",
  #"third_party/externals/v8"                     : "https://chromium.googlesource.com/v8/v8.git@5f1ae66d5634e43563b2d25ea652dfb94c31a3b4",
  "third_party/externals/wuffs"                  : "https://skia.googlesource.com/external/github.com/google/wuffs-mirror-release-c.git@600cd96cf47788ee3a74b40a6028b035c9fd6a61",
  "third_party/externals/zlib"                   : "https://chromium.googlesource.com/chromium/src/third_party/zlib@c876c8f87101c5a75f6014b0f832499afeb65b73",

  'bin': {
    'packages': [
      {
        'package': 'skia/tools/sk/${{platform}}',
        'version': Var('sk_tool_revision'),
      }
    ],
    'dep_type': 'cipd',
  },
}
