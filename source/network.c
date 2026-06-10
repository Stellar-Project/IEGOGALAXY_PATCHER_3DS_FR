#include "network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <3ds.h>

static bool s_httpc_initialized = false;
static PrintConsole *s_debug_console = NULL;
static FILE *s_logfile = NULL;

/* Flag d'annulation — mis à true depuis main.c via network_cancel() */
static volatile bool s_cancel = false;

void network_set_logfile(FILE *f)       { s_logfile = f; }
void network_set_debug_console(PrintConsole *con) { s_debug_console = con; }
void network_cancel(void)               { s_cancel = true; }

#define NETLOG(...) do { \
    if (s_logfile) { fprintf(s_logfile, __VA_ARGS__); fflush(s_logfile); } \
    if (s_debug_console) { consoleSelect(s_debug_console); printf(__VA_ARGS__); } \
} while(0)

#define HTTPC_TIMEOUT_NS   (60000000000ULL)  /* 60s */
#define DL_CHUNK_SIZE      (0x80000)          /* 512 Ko — meilleur débit */
/* Taille max par session httpc — on réinitialise après chaque tranche */
#define SESSION_MAX_BYTES  (350 * 1024 * 1024)  /* 350 Mo */
#define MAX_RETRIES        5    /* tentatives par tranche avant abandon */

bool network_init(void)
{
    if (s_httpc_initialized) return true;
    Result ret = httpcInit(1024 * 1024);
    NETLOG("[net] httpcInit: 0x%08" PRIx32 "\n", ret);
    if (R_FAILED(ret)) return false;
    s_httpc_initialized = true;
    s_cancel = false;
    NETLOG("[net] httpcInit OK\n");
    return true;
}

void network_exit(void)
{
    if (s_httpc_initialized) {
        httpcExit();
        s_httpc_initialized = false;
    }
}

/* Réinitialise httpc pour vider sa mémoire interne */
static bool network_restart(void)
{
    NETLOG("[net] Restart httpc...\n");
    httpcExit();
    s_httpc_initialized = false;
    svcSleepThread(500000000LL); /* 500ms */
    Result ret = httpcInit(1024 * 1024);
    if (R_FAILED(ret)) {
        NETLOG("[net] Restart echoue: 0x%08" PRIx32 "\n", ret);
        return false;
    }
    s_httpc_initialized = true;
    NETLOG("[net] Restart OK\n");
    return true;
}

/* Ouvre un contexte avec Range header */
static Result open_context(httpcContext *ctx, const char *url,
                            u32 range_start, u32 range_end,
                            char *newurl_buf, size_t newurl_size,
                            u32 *out_status)
{
    Result ret = 0;
    int redirect_depth = 0;

retry:
    ret = httpcOpenContext(ctx, HTTPC_METHOD_GET, url, 1);
    if (R_FAILED(ret)) return ret;

    httpcSetSSLOpt(ctx, SSLCOPT_DisableVerify);
    httpcAddRequestHeaderField(ctx, "User-Agent",
                               "IEGO-Patcher/1.0 (Nintendo 3DS)");
    httpcAddRequestHeaderField(ctx, "Connection", "Keep-Alive");

    /* Range : toujours explicite pour contrôler la taille de session */
    char range[64];
    if (range_end > 0)
        snprintf(range, sizeof(range), "bytes=%lu-%lu",
                 (unsigned long)range_start, (unsigned long)range_end);
    else
        snprintf(range, sizeof(range), "bytes=%lu-",
                 (unsigned long)range_start);
    httpcAddRequestHeaderField(ctx, "Range", range);
    NETLOG("[net] Range: %s\n", range);

    ret = httpcBeginRequest(ctx);
    if (R_FAILED(ret)) {
        httpcCancelConnection(ctx);
        httpcCloseContext(ctx);
        return ret;
    }

    ret = httpcGetResponseStatusCodeTimeout(ctx, out_status, HTTPC_TIMEOUT_NS);
    if (R_FAILED(ret)) {
        httpcCancelConnection(ctx);
        httpcCloseContext(ctx);
        return ret;
    }

    NETLOG("[net] HTTP %lu\n", (unsigned long)*out_status);

    if (*out_status / 100 == 3) {
        if (redirect_depth >= 10) return -1;
        httpcGetResponseHeader(ctx, "Location", newurl_buf, newurl_size);
        httpcCancelConnection(ctx);
        httpcCloseContext(ctx);
        url = newurl_buf;
        redirect_depth++;
        goto retry;
    }

    return 0;
}

bool network_download_file(const char *url,
                           const char *dest_path,
                           u32 total_size,
                           DownloadProgressCb progress_cb,
                           void *userdata)
{
    if (!url || !dest_path) return false;

    s_cancel = false;

    Result ret = 0;
    httpcContext context;
    char newurl[1024];
    u32 statuscode = 0;
    u32 downloaded = 0;  /* total téléchargé depuis le début */
    u8 *chunk = NULL;
    FILE *fp = NULL;
    bool success = false;

    NETLOG("[net] URL: %s\n", url);

    chunk = (u8 *)malloc(DL_CHUNK_SIZE);
    if (!chunk) { NETLOG("[net] malloc echoue\n"); return false; }

    fp = fopen(dest_path, "wb");
    if (!fp) {
        NETLOG("[net] fopen echoue: %s\n", dest_path);
        free(chunk);
        return false;
    }

    if (progress_cb) progress_cb(0, total_size, 0, 0, userdata);

    /* ── Boucle par tranches de SESSION_MAX_BYTES ── */
    while (downloaded < total_size || total_size == 0) {

        if (s_cancel) {
            NETLOG("[net] Annule par l'utilisateur\n");
            break;
        }

        u32 range_start = downloaded;
        u32 range_end   = 0;

        /* Si on connaît la taille totale, on découpe en tranches */
        if (total_size > 0) {
            u32 remaining = total_size - downloaded;
            if (remaining <= SESSION_MAX_BYTES) {
                /* Dernière tranche — pas de range_end pour tout prendre */
                range_end = 0;
            } else {
                range_end = range_start + SESSION_MAX_BYTES - 1;
            }
        }

        NETLOG("[net] Tranche: %luMo -> %s\n",
               (unsigned long)(range_start / (1024*1024)),
               range_end ? "..." : "fin");

        /* Retry sur cette tranche */
        int retries = 0;
        

        while (retries < MAX_RETRIES) {
            if (s_cancel) break;

            /* Si on a eu une erreur, on repart depuis où on s'est arrêté */
            range_start = downloaded;

            ret = open_context(&context, url, range_start, range_end,
                               newurl, sizeof(newurl), &statuscode);
            if (R_FAILED(ret)) {
                NETLOG("[net] open_context err: 0x%08" PRIx32 "\n", ret);
                retries++;
                network_restart();
                continue;
            }

            if (statuscode != 200 && statuscode != 206) {
                NETLOG("[net] HTTP inattendu: %lu\n", (unsigned long)statuscode);
                httpcCancelConnection(&context);
                httpcCloseContext(&context);
                goto fail;
            }

            /* Télécharge cette tranche chunk par chunk */
            u32 prev_pos = 0;
            u64 t_start = svcGetSystemTick(); /* tick de début pour le débit */
            u32 downloaded_at_start = downloaded;

            do {
                if (s_cancel) {
                    httpcCancelConnection(&context);
                    httpcCloseContext(&context);
                    goto cancelled;
                }

                u32 pos = 0;
                ret = httpcReceiveDataTimeout(&context, chunk,
                                              DL_CHUNK_SIZE, HTTPC_TIMEOUT_NS);

                httpcGetDownloadSizeState(&context, &pos, NULL);
                u32 readsize = pos - prev_pos;

                if (readsize > 0) {
                    if (fwrite(chunk, 1, readsize, fp) != readsize) {
                        NETLOG("[net] fwrite echoue (SD pleine?)\n");
                        httpcCancelConnection(&context);
                        httpcCloseContext(&context);
                        goto fail;
                    }
                    downloaded += readsize;
                    prev_pos = pos;

                    /* Calcul du débit en Ko/s
                     * svcGetSystemTick() = CPU ticks à ~268 MHz sur 3DS */
                    u64 t_now = svcGetSystemTick();
                    u64 elapsed_ticks = t_now - t_start;
                    /* 268111856 ticks/s sur Old3DS, ~536MHz sur New3DS
                     * On utilise CPU_TICKS_PER_MSEC défini dans libctru */
                    u32 elapsed_ms = (u32)(elapsed_ticks / (CPU_TICKS_PER_MSEC));
                    u32 speed_kbs = 0;
                    if (elapsed_ms > 200) { /* mesure fiable après 200ms */
                        u32 bytes_since_start = downloaded - downloaded_at_start;
                        speed_kbs = (u32)((u64)bytes_since_start * 1000
                                          / (elapsed_ms * 1024));
                    }

                    if (progress_cb)
                        progress_cb(downloaded, total_size, retries,
                                    speed_kbs, userdata);
                }

            } while (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING);

            httpcCancelConnection(&context);
            httpcCloseContext(&context);

            if (ret == 0) {
                
                NETLOG("[net] Tranche OK: %luMo total\n",
                       (unsigned long)(downloaded/(1024*1024)));
                break;
            }

            NETLOG("[net] Erreur tranche: 0x%08" PRIx32 " (retry %d)\n",
                   ret, retries + 1);
            retries++;
            network_restart();
        }

        if (s_cancel) break;

        if (retries >= MAX_RETRIES) {
            NETLOG("[net] Echec apres %d retries\n", MAX_RETRIES);
            goto fail;
        }

        /* Restart httpc entre chaque tranche pour vider la mémoire */
        if (total_size > 0 && downloaded < total_size) {
            NETLOG("[net] Restart entre tranches...\n");
            if (!network_restart()) goto fail;
        } else {
            /* Téléchargement complet */
            success = true;
            break;
        }
    }

    if (s_cancel) goto cancelled;
    if (downloaded >= total_size && total_size > 0) success = true;

    goto done;

cancelled:
    NETLOG("[net] Telechargement annule\n");
    goto done;

fail:
    NETLOG("[net] Echec definitif a %luMo\n",
           (unsigned long)(downloaded/(1024*1024)));

done:
    fclose(fp);
    free(chunk);

    if (!success) {
        remove(dest_path);
        return false;
    }

    NETLOG("[net] OK: %luMo\n", (unsigned long)(downloaded/(1024*1024)));
    return true;
}