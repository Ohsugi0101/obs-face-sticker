/*
Face Sticker Plugin for OBS Studio
Copyright (C) 2026 Manus
SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Register the Face Sticker async-video filter with OBS. Called once at module load. */
void face_sticker_register(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
