/*
 * Copyright 2012 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkBlitRow.h"
#include "SkUtilsArm.h"

#include "SkBlitRow_opts_arm_neon.h"

static const SkBlitRow::Proc sk_blitrow_platform_565_procs_arm[] = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};

static const SkBlitRow::Proc32 sk_blitrow_platform_32_procs_arm[] = {
    NULL, NULL, NULL, NULL,
};

SkBlitRow::Proc SkBlitRow::PlatformProcs565(unsigned flags) {
    return SK_ARM_NEON_WRAP(sk_blitrow_platform_565_procs_arm)[flags];
}

SkBlitRow::Proc32 SkBlitRow::PlatformProcs32(unsigned flags) {
    return SK_ARM_NEON_WRAP(sk_blitrow_platform_32_procs_arm)[flags];
}

///////////////////////////////////////////////////////////////////////////////
#define Color32_arm  NULL
SkBlitRow::ColorProc SkBlitRow::PlatformColorProc() {
    return SK_ARM_NEON_WRAP(Color32_arm);
}

SkBlitRow::ColorRectProc PlatformColorRectProcFactory() {
    return NULL;
}
