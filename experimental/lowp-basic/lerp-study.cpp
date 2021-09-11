/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>

#include "QMath.h"

struct Stats {
    int diff_8_bits = 0;
    int max_diff = 0;
    int min_diff = 0;
    int64_t total = 0;

    void log(int16_t golden, int16_t candidate) {
        int diff = candidate - golden;
        max_diff = std::max(max_diff, diff);
        min_diff = std::min(min_diff, diff);
        diff_8_bits += candidate != golden;
        total++;
    }

    void print() const {
        printf("8-bit diff: %d - %g%%\n", diff_8_bits, 100.0 * diff_8_bits / total);
        printf("differences min: %d max: %d\n", min_diff, max_diff);
        printf("total: %lld\n", total);
    }
};

static float golden_lerp(float t, int16_t a, int16_t b) {
    return (1.0f - t) * a + t * b;
}

template <int logPixelScale>
static int16_t saturating_lerp(float t, int16_t a, int16_t b) {
    const int16_t half = 1 << (logPixelScale - 1);
    Q15 qt(floor(t * 32768.f + 0.5f));
    Q15 qa(a << logPixelScale);
    Q15 qb(b << logPixelScale);

    Q15 answer = simulate_neon_vqrdmulhq_s16(qt, qb - qa) + qa;
    return (answer[0] + half) >> logPixelScale;
}

template <int logPixelScale>
static int16_t ssse3_lerp(float t, int16_t a, int16_t b) {
    const int16_t half = 1 << (logPixelScale - 1);
    Q15 qt(floor(t * 32768.f + 0.5f));
    Q15 qa(a << logPixelScale);
    Q15 qb(b << logPixelScale);

    Q15 answer = simulate_ssse3_mm_mulhrs_epi16(qt, qb - qa) + qa;
    return (answer[0] + half) >> logPixelScale;
}

template <typename Lerp>
static Stats check_lerp(Lerp lerp) {
    Stats stats;
    for (float t = 0; t < 1.0f; t += 1.0f/32768.0f)
    for (int a = 255; a >= 0; a--)
    for (int b = 255; b >= 0; b--) {
        float l = golden_lerp(t, a, b);
        int16_t golden = floor(l + 0.5f);
        int16_t candidate = lerp(t, a, b);
        stats.log(golden, candidate);
    }
    return stats;
}

// Simulate a scaled intermediate value for bilerp.
template <typename Lerp>
static Stats check_scaled_lerp(Lerp lerp) {
    Stats stats;
    for (float t = 0; t < 1.0f; t += 1.0f/32768.0f)
    for (int a = 255; a >= 0; a--)
    for (int b = 255; b >= 0; b--) {
        int16_t scaledA = a << 6,
                scaledB = b << 6;

        float l = golden_lerp(t, scaledA, scaledB);
        int16_t golden = floor(l + 0.5f);
        int16_t candidate = lerp(t, scaledA, scaledB);
        stats.log(golden, candidate);
    }
    return stats;
}

int main() {
    Stats stats;

    printf("Using vqrdmulhq_s16...\n");
    stats = check_lerp(saturating_lerp<7>);
    stats.print();

    printf("\nUsing mm_mulhrs_epi16...\n");
    stats = check_lerp(ssse3_lerp<7>);
    stats.print();

    printf("\nScaled using vqrdmulhq_s16...\n");
    // Need one bit for rounding.
    stats = check_scaled_lerp(saturating_lerp<1>);
    stats.print();

    printf("\nScaled using mm_mulhrs_epi16...\n");
    // Need one bit for rounding.
    stats = check_scaled_lerp(ssse3_lerp<1>);
    stats.print();

    printf("Done.");
    return 0;
}

