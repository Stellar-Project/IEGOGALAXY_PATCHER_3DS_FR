#include "patcher.h"
#include "network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <inttypes.h>
#include <3ds.h>

#include "../lib/minizip/unzip.h"

#define EXTRACT_BUF_SIZE (64 * 1024)

/* ── Adaptateur réseau → patch ────────────────────────────────────────────── */

typedef struct {
    PatchProgressCb cb;
    void           *userdata;
} DlProgressAdaptor;

static void _dl_progress_adaptor(u32 downloaded, u32 total,
                                  int retries, u32 speed_kbs, void *userdata)
{
    DlProgressAdaptor *a = (DlProgressAdaptor *)userdata;
    if (!a->cb) return;

    if (total > 0)
        a->cb((double)downloaded / (double)total * 100.0,
              retries, speed_kbs, a->userdata);
    else
        a->cb(-1.0, retries, speed_kbs, a->userdata);
}

/* ── Utilitaires ──────────────────────────────────────────────────────────── */

static void _mkdir_recursive(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0777); *p = '/'; }
    }
    mkdir(tmp, 0777);
}

static void _get_parent_dir(const char *filepath, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s", filepath);
    char *slash = strrchr(out, '/');
    if (slash) *slash = '\0'; else out[0] = '\0';
}

/* ── API publique ─────────────────────────────────────────────────────────── */

bool patcher_extract_zip(const char *zip_path,
                         const char *dest_dir,
                         PatchStatusCb   status_cb,
                         PatchProgressCb progress_cb,
                         void *userdata)
{
    if (!zip_path || !dest_dir) return false;

    if (status_cb) status_cb("Ouverture du ZIP...", userdata);

    unzFile zf = unzOpen(zip_path);
    if (!zf) { printf("[patcher] ZIP introuvable: %s\n", zip_path); return false; }

    unz_global_info gi;
    if (unzGetGlobalInfo(zf, &gi) != UNZ_OK) { unzClose(zf); return false; }

    if (status_cb)   status_cb("Extraction...", userdata);
    if (progress_cb) progress_cb(0.0, 0, 0, userdata);

    char *buf = malloc(EXTRACT_BUF_SIZE);
    if (!buf) { unzClose(zf); return false; }

    uLong files_done = 0;
    bool  success    = true;

    /*
     * Détecte le dossier racine du ZIP pour le sauter.
     * Ex: "patch_bigbang_fr/luma/titles/..." → on saute "patch_bigbang_fr/"
     * On cherche le premier '/' dans le premier filename pour trouver le préfixe.
     */
    char zip_prefix[256] = {0};
    {
        unz_file_info fi_tmp;
        char fname_tmp[512];
        if (unzGetCurrentFileInfo(zf, &fi_tmp, fname_tmp, sizeof(fname_tmp),
                                  NULL, 0, NULL, 0) == UNZ_OK) {
            char *slash = strchr(fname_tmp, '/');
            if (slash) {
                size_t len = (size_t)(slash - fname_tmp) + 1;
                strncpy(zip_prefix, fname_tmp, len);
                zip_prefix[len] = '\0';
                printf("[patcher] Prefixe ZIP detecte: '%s'\n", zip_prefix);
            }
        }
    }
    size_t prefix_len = strlen(zip_prefix);

    do {
        unz_file_info fi;
        char filename[512];
        if (unzGetCurrentFileInfo(zf, &fi, filename, sizeof(filename),
                                  NULL, 0, NULL, 0) != UNZ_OK) {
            success = false; break;
        }

        /* Saute le dossier racine du ZIP s'il existe */
        const char *rel = filename;
        if (prefix_len > 0 && strncmp(filename, zip_prefix, prefix_len) == 0)
            rel = filename + prefix_len;

        /* Ignore les entrées vides (le dossier racine lui-même) */
        if (rel[0] == '\0') {
            if (unzGoToNextFile(zf) != UNZ_OK) break;
            continue;
        }

        char out_path[640];
        snprintf(out_path, sizeof(out_path), "%s%s", dest_dir, rel);
        bool is_dir = (rel[strlen(rel) - 1] == '/');

        if (is_dir) {
            _mkdir_recursive(out_path);
        } else {
            char parent[640];
            _get_parent_dir(out_path, parent, sizeof(parent));
            if (parent[0]) _mkdir_recursive(parent);

            if (unzOpenCurrentFile(zf) != UNZ_OK) { success = false; break; }

            FILE *fp = fopen(out_path, "wb");
            if (!fp) {
                printf("[patcher] Impossible de creer: %s\n", out_path);
                unzCloseCurrentFile(zf);
                success = false; break;
            }

            int n;
            while ((n = unzReadCurrentFile(zf, buf, EXTRACT_BUF_SIZE)) > 0) {
                if (fwrite(buf, 1, (size_t)n, fp) != (size_t)n) {
                    success = false; break;
                }
            }
            fclose(fp);
            unzCloseCurrentFile(zf);
            if (!success) break;
        }

        files_done++;
        if (progress_cb && gi.number_entry > 0)
            progress_cb((double)files_done / (double)gi.number_entry * 100.0,
                        0, 0, userdata);

    } while (unzGoToNextFile(zf) == UNZ_OK);

    free(buf);
    unzClose(zf);

    if (success && progress_cb) progress_cb(100.0, 0, 0, userdata);
    return success;
}

bool patcher_cleanup(const char *zip_path)
{
    if (!zip_path) return true;
    if (remove(zip_path) == 0 || errno == ENOENT) return true;
    return false;
}

bool patcher_install(const char *url,
                     const char *temp_zip,
                     const char *dest_dir,
                     const char *version,
                     int selection,
                     PatchStatusCb   status_cb,
                     PatchProgressCb progress_cb,
                     void *userdata)
{
    if (status_cb) status_cb("Telechargement...", userdata);
    if (progress_cb) progress_cb(-1.0, 0, 0, userdata);

    DlProgressAdaptor adaptor = { progress_cb, userdata };

    if (!network_download_file(url, temp_zip, 0,
                               _dl_progress_adaptor, &adaptor)) {
        printf("[patcher] Echec telechargement\n");
        patcher_cleanup(temp_zip);
        return false;
    }

    const char *tid = (selection == 1) ? "000400000010BB00" : "000400000010BA00";
    char romfs_path[256];
    char backup_path[256];
    snprintf(romfs_path, sizeof(romfs_path), "sdmc:/luma/titles/%s/romfs", tid);
    snprintf(backup_path, sizeof(backup_path), "sdmc:/luma/titles/%s/romfs.bak", tid);

    if (status_cb) status_cb("Sauvegarde...", userdata);
    rename(romfs_path, backup_path);

    if (!patcher_extract_zip(temp_zip, dest_dir,
                             status_cb, progress_cb, userdata)) {
        printf("[patcher] Echec extraction\n");
        patcher_cleanup(temp_zip);
        return false;
    }

    if (version && version[0]) {
        char ver_path[256];
        snprintf(ver_path, sizeof(ver_path), "sdmc:/luma/titles/%s/patch_version.txt", tid);
        FILE *vf = fopen(ver_path, "w");
        if (vf) {
            fprintf(vf, "%s\n", version);
            fclose(vf);
        }
    }

    if (status_cb) status_cb("Nettoyage...", userdata);
    patcher_cleanup(temp_zip);

    if (status_cb)   status_cb("Termine !", userdata);
    if (progress_cb) progress_cb(100.0, 0, 0, userdata);
    return true;
}