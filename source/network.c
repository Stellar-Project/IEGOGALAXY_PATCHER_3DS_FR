#include "network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <3ds.h>

static bool s_httpc_initialized = false;
static PrintConsole *s_debug_console = NULL;
static FILE *s_logfile = NULL;
static volatile bool s_cancel = false;

void network_set_logfile(FILE *f)                 { s_logfile = f; }
void network_set_debug_console(PrintConsole *con) { s_debug_console = con; }
void network_cancel(void)                         { s_cancel = true; }

#define NETLOG(...) do { \
    if (s_logfile) { fprintf(s_logfile, __VA_ARGS__); fflush(s_logfile); } \
    if (s_debug_console) { consoleSelect(s_debug_console); printf(__VA_ARGS__); } \
} while(0)

#define HTTPC_TIMEOUT_NS   (10000000000ULL)
#define DL_CHUNK_SIZE      (0x20000)           /* 128 Ko */
#define MAX_RETRIES        2000               /* large — une reprise par coupure */

/* ── Init / Exit ─────────────────────────────────────────────────────────── */

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

/* ── Ouvre un contexte httpc ─────────────────────────────────────────────── */
/* Toutes les buffers sont sur le heap via les paramètres               */

static Result _open_ctx(httpcContext *ctx,
                         const char *url,
                         u32 range_start, u32 range_end,
                         u32 *out_status)
{
    Result ret = httpcOpenContext(ctx, HTTPC_METHOD_GET, url, 1);
    if (R_FAILED(ret)) return ret;

    httpcSetSSLOpt(ctx, SSLCOPT_DisableVerify);
    httpcAddRequestHeaderField(ctx, "User-Agent",
        "IEGO-Patcher/" APP_VERSION " (Nintendo 3DS)");
    httpcAddRequestHeaderField(ctx, "Connection", "Keep-Alive");

    /* Range header */
    char *range = (char *)malloc(64);
    if (!range) { httpcCancelConnection(ctx); httpcCloseContext(ctx); return -1; }
    if (range_end > 0)
        snprintf(range, 64, "bytes=%lu-%lu",
                 (unsigned long)range_start, (unsigned long)range_end);
    else
        snprintf(range, 64, "bytes=%lu-", (unsigned long)range_start);
    httpcAddRequestHeaderField(ctx, "Range", range);
    free(range);

    ret = httpcBeginRequest(ctx);
    if (R_FAILED(ret)) { httpcCancelConnection(ctx); httpcCloseContext(ctx); return ret; }

    ret = httpcGetResponseStatusCodeTimeout(ctx, out_status, HTTPC_TIMEOUT_NS);
    if (R_FAILED(ret)) { httpcCancelConnection(ctx); httpcCloseContext(ctx); return ret; }

    return 0;
}

/* ── API publique ────────────────────────────────────────────────────────── */

bool network_download_file(const char *url,
                           const char *dest_path,
                           u32 total_size,
                           DownloadProgressCb progress_cb,
                           void *userdata)
{
    if (!url || !dest_path) return false;
    s_cancel = false;

    /* Tous les buffers alloués sur le heap, pas la stack */
    u8   *chunk    = (u8 *)malloc(DL_CHUNK_SIZE);
    char *url_buf  = (char *)malloc(1024);

    if (!chunk || !url_buf) {
        NETLOG("[net] malloc initial echoue\n");
        free(chunk); free(url_buf);
        return false;
    }
    snprintf(url_buf, 1024, "%s", url);

    FILE *fp = fopen(dest_path, "wb");
    if (!fp) {
        NETLOG("[net] fopen err: %s\n", dest_path);
        free(chunk); free(url_buf);
        return false;
    }

    if (progress_cb) progress_cb(0, total_size, 0, 0, userdata);

    u32 downloaded    = 0;
    int retries_total = 0;
    bool success      = false;

    NETLOG("[net] URL: %s\n", url_buf);

    while (!s_cancel && retries_total < MAX_RETRIES) {

        httpcContext ctx;
        u32 statuscode = 0;

        Result ret = _open_ctx(&ctx, url_buf, downloaded, 0, &statuscode);
        if (R_FAILED(ret)) {
            NETLOG("[net] open_ctx err: 0x%08" PRIx32 "\n", ret);
            retries_total++;
            svcSleepThread(500000000LL);
            continue;
        }

        if (statuscode / 100 == 3) {
            httpcGetResponseHeader(&ctx, "Location", url_buf, 1024);
            NETLOG("[net] Redirect -> %s\n", url_buf);
            httpcCloseContext(&ctx);
            continue;
        }

        if (statuscode != 200 && statuscode != 206) {
            NETLOG("[net] HTTP inattendu: %lu\n", (unsigned long)statuscode);
            httpcCloseContext(&ctx);
            break;
        }

        if (total_size == 0) {
            httpcGetDownloadSizeState(&ctx, NULL, &total_size);
            if (total_size > 0)
                NETLOG("[net] Taille: %luMo\n",
                       (unsigned long)(total_size/(1024*1024)));
        }

        u32 prev_pos    = 0;
        u64 t_start     = svcGetSystemTick();
        u32 dl_at_start = downloaded;
        bool write_ok   = true;

        do {
            if (s_cancel) { write_ok = false; break; }

            u32 pos = 0;
            ret = httpcReceiveDataTimeout(&ctx, chunk,
                                         DL_CHUNK_SIZE, HTTPC_TIMEOUT_NS);
            httpcGetDownloadSizeState(&ctx, &pos, NULL);

            u32 readsize = pos - prev_pos;
            if (readsize > 0) {
                if (fwrite(chunk, 1, readsize, fp) != readsize) {
                    NETLOG("[net] fwrite err\n");
                    write_ok = false; break;
                }
                downloaded += readsize;
                prev_pos    = pos;

                u64 ms = (svcGetSystemTick() - t_start) / CPU_TICKS_PER_MSEC;
                u32 spd = (ms > 200)
                    ? (u32)((u64)(downloaded - dl_at_start) * 1000 / (ms * 1024))
                    : 0;

                if (progress_cb)
                    progress_cb(downloaded, total_size,
                                retries_total, spd, userdata);
            }

        } while (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING);

        /* Ferme proprement — sans CancelConnection qui est stubbed */
        httpcCloseContext(&ctx);

        if (!write_ok || s_cancel) break;

        if (ret == 0) {
            /* Vérifie qu'on a tout */
            if (total_size > 0 && downloaded < total_size) {
                NETLOG("[net] Incomplet %luMo/%luMo, reprise\n",
                       (unsigned long)(downloaded/(1024*1024)),
                       (unsigned long)(total_size/(1024*1024)));
                retries_total++;
                svcSleepThread(200000000LL);
                continue;
            }
            success = true;
            break;
        }

        /* Coupure — reprend depuis downloaded */
        NETLOG("[net] Coupure a %luMo (retry %d)\n",
               (unsigned long)(downloaded/(1024*1024)), retries_total+1);
        retries_total++;
        svcSleepThread(200000000LL);
    }

    fclose(fp);
    free(chunk);
    free(url_buf);

    if (!success) {
        remove(dest_path);
        NETLOG("[net] Echec a %luMo\n",
               (unsigned long)(downloaded/(1024*1024)));
        return false;
    }

    NETLOG("[net] OK: %luMo, %d reconnexion(s)\n",
           (unsigned long)(downloaded/(1024*1024)), retries_total);
    return true;
}