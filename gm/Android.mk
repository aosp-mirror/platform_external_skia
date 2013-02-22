
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
  gm.cpp \
  gmmain.cpp \
  system_preferences_default.cpp \
  ../src/pipe/utils/SamplePipeControllers.cpp

# Slides
LOCAL_SRC_FILES += \
  aaclip.cpp \
  aarectmodes.cpp \
  arithmode.cpp \
  bicubicfilter.cpp \
  bigmatrix.cpp \
  bitmapcopy.cpp \
  bitmapmatrix.cpp \
  bitmapfilters.cpp \
  bitmaprect.cpp \
  bitmapscroll.cpp \
  blend.cpp \
  blurs.cpp \
  blurrect.cpp \
  circles.cpp \
  colorfilterimagefilter.cpp \
  colormatrix.cpp \
  complexclip.cpp \
  complexclip2.cpp \
  composeshader.cpp \
  convexpaths.cpp \
  cubicpaths.cpp \
  cmykjpeg.cpp \
  degeneratesegments.cpp \
  dashcubics.cpp \
  dashing.cpp \
  distantclip.cpp \
  displacement.cpp \
  drawbitmaprect.cpp \
  drawlooper.cpp \
  extractbitmap.cpp \
  emptypath.cpp \
  fatpathfill.cpp \
  factory.cpp \
  filltypes.cpp \
  filltypespersp.cpp \
  fontscaler.cpp \
  gammatext.cpp \
  getpostextpath.cpp \
  giantbitmap.cpp \
  gradients.cpp \
  gradtext.cpp \
  hairmodes.cpp \
  hittestpath.cpp \
  imageblur.cpp \
  imagemagnifier.cpp \
  lighting.cpp \
  image.cpp \
  imagefiltersbase.cpp \
  imagefiltersgraph.cpp \
  lcdtext.cpp \
  linepaths.cpp \
  matrixconvolution.cpp \
  modecolorfilters.cpp \
  morphology.cpp \
  ninepatchstretch.cpp \
  nocolorbleed.cpp \
  patheffects.cpp \
  pathfill.cpp \
  pathinterior.cpp \
  pathreverse.cpp \
  points.cpp \
  poly2poly.cpp \
  quadpaths.cpp \
  rrect.cpp \
  rrects.cpp \
  samplerstress.cpp \
  shaderbounds.cpp \
  shadertext.cpp \
  shadertext2.cpp \
  shadertext3.cpp \
  shadows.cpp \
  simpleaaclip.cpp \
  spritebitmap.cpp \
  srcmode.cpp \
  strokefill.cpp \
  strokerect.cpp \
  strokes.cpp \
  tablecolorfilter.cpp \
  texteffects.cpp \
  testimagefilters.cpp \
  texdata.cpp \
  tilemodes.cpp \
  tinybitmap.cpp \
  twopointradial.cpp \
  typeface.cpp \
  verttext.cpp \
  verttext2.cpp \
  verylargebitmap.cpp \
  xfermodes.cpp

LOCAL_SHARED_LIBRARIES := \
  libcutils \
  libutils \
  libskia \
  libEGL \
  libGLESv2
  
LOCAL_C_INCLUDES := \
  external/skia/include/config \
  external/skia/include/core \
  external/skia/include/effects \
  external/skia/include/gpu \
  external/skia/include/images \
  external/skia/include/pipe \
  external/skia/include/utils \
  external/skia/gm \
  external/skia/src/core \
  external/skia/src/effects \
  external/skia/src/gpu \
  external/skia/src/pipe/utils \
  external/skia/src/utils

LOCAL_MODULE := skia_gm

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
