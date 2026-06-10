#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <3ds.h>

/*
 * downloaded : octets téléchargés au total
 * total      : taille totale du fichier
 * retries    : nombre de reconnexions
 * speed_kbs  : débit instantané en Ko/s
 */
typedef void (*DownloadProgressCb)(u32 downloaded, u32 total,
                                   int retries, u32 speed_kbs,
                                   void *userdata);

void network_set_debug_console(PrintConsole *con);
void network_set_logfile(FILE *f);
void network_cancel(void);

bool network_init(void);
void network_exit(void);

bool network_download_file(const char *url,
                           const char *dest_path,
                           u32 total_size,
                           DownloadProgressCb progress_cb,
                           void *userdata);