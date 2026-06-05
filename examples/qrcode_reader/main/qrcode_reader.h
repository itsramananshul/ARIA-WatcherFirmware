/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once

/** Major version number (X.x.x) */
#define QRCODE_READERVERSION_MAJOR 0
/** Minor version number (x.X.x) */
#define QRCODE_READERVERSION_MINOR 0
/** Patch version number (x.x.X) */
#define QRCODE_READERVERSION_PATCH 1

/**
 * Macro to convert version number into an integer
 *
 * To be used in comparisons, such as QRCODE_READERVERSION >= QRCODE_READERVERSION_VAL(4, 0, 0)
 */
#define QRCODE_READERVERSION_VAL(major, minor, patch) ((major << 16) | (minor << 8) | (patch))

/**
 * Current version, as an integer
 *
 * To be used in comparisons, such as QRCODE_READERVERSION >= QRCODE_READERVERSION_VAL(4, 0, 0)
 */
#define QRCODE_READERVERSION QRCODE_READERVERSION_VAL(QRCODE_READERVERSION_MAJOR, \
                                                      QRCODE_READERVERSION_MINOR, \
                                                      QRCODE_READERVERSION_PATCH)
