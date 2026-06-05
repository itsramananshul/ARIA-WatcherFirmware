/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once

/** Major version number (X.x.x) */
#define BYTETRACK_VERSION_MAJOR 0
/** Minor version number (x.X.x) */
#define BYTETRACK_VERSION_MINOR 0
/** Patch version number (x.x.X) */
#define BYTETRACK_VERSION_PATCH 1

/**
 * Macro to convert version number into an integer
 *
 * To be used in comparisons, such as BYTETRACK_VERSION >= BYTETRACK_VERSION_VAL(4, 0, 0)
 */
#define BYTETRACK_VERSION_VAL(major, minor, patch) ((major << 16) | (minor << 8) | (patch))

/**
 * Current version, as an integer
 *
 * To be used in comparisons, such as BYTETRACK_VERSION >= BYTETRACK_VERSION_VAL(4, 0, 0)
 */
#define BYTETRACK_VERSION BYTETRACK_VERSION_VAL(BYTETRACK_VERSION_MAJOR, \
                                                    BYTETRACK_VERSION_MINOR, \
                                                    BYTETRACK_VERSION_PATCH)
