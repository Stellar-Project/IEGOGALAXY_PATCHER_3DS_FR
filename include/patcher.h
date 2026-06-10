#pragma once

#include <stdbool.h>
#include <3ds.h>

typedef void (*PatchStatusCb)(const char *message, void *userdata);

/*
 * value     : 0.0–100.0 (pourcentage), négatif = indéterminé
 * retries   : nombre de reconnexions
 * speed_kbs : débit en Ko/s (0 si inconnu)
 */
typedef void (*PatchProgressCb)(double value, int retries,
                                u32 speed_kbs, void *userdata);

bool patcher_extract_zip(const char *zip_path,
                         const char *dest_dir,
                         PatchStatusCb   status_cb,
                         PatchProgressCb progress_cb,
                         void *userdata);

bool patcher_cleanup(const char *zip_path);

bool patcher_install(const char *url,
                     const char *temp_zip,
                     const char *dest_dir,
                     PatchStatusCb   status_cb,
                     PatchProgressCb progress_cb,
                     void *userdata);