#include "versions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>
#include <jansson.h>

/* ── Comparaison de versions X.Y.Z ───────────────────────────────────────── */

int versions_compare(const char *a, const char *b)
{
    int a1=0, a2=0, a3=0;
    int b1=0, b2=0, b3=0;

    /* Gère X.Y et X.Y.Z */
    sscanf(a, "%d.%d.%d", &a1, &a2, &a3);
    sscanf(b, "%d.%d.%d", &b1, &b2, &b3);

    if (a1 != b1) return a1 > b1 ? 1 : -1;
    if (a2 != b2) return a2 > b2 ? 1 : -1;
    if (a3 != b3) return a3 > b3 ? 1 : -1;
    return 0;
}

/* ── Téléchargement du JSON ───────────────────────────────────────────────── */

/*
 * Télécharge une URL entièrement en mémoire.
 * Le buffer retourné est alloué avec malloc — à free() après usage.
 * Retourne NULL en cas d'échec.
 */
static char *_download_string(const char *url, u32 *out_size)
{
    httpcContext ctx;
    u32 status = 0;
    char *buf = NULL;
    u32 size = 0;

    if (R_FAILED(httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 1)))
        return NULL;

    httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    httpcAddRequestHeaderField(&ctx, "User-Agent",
                               "IEGO-Patcher/" APP_VERSION " (Nintendo 3DS)");

    if (R_FAILED(httpcBeginRequest(&ctx))) goto fail;

    if (R_FAILED(httpcGetResponseStatusCodeTimeout(
            &ctx, &status, 10000000000ULL))) goto fail;

    if (status != 200) {
        printf("[versions] HTTP %lu\n", (unsigned long)status);
        goto fail;
    }

    /* Télécharge par chunks de 4 Ko */
    buf = (char *)malloc(0x1000);
    if (!buf) goto fail;

    Result ret;
    do {
        u32 chunk_read = 0;
        ret = httpcDownloadData(&ctx, (u8 *)(buf + size), 0x1000, &chunk_read);
        size += chunk_read;

        if (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING) {
            char *tmp = (char *)realloc(buf, size + 0x1000);
            if (!tmp) { free(buf); buf = NULL; goto fail; }
            buf = tmp;
        }
    } while (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING);

    if (R_FAILED(ret)) { free(buf); buf = NULL; goto fail; }

    /* Termine la chaîne */
    char *tmp = (char *)realloc(buf, size + 1);
    if (tmp) buf = tmp;
    buf[size] = '\0';

    if (out_size) *out_size = size;

    httpcCancelConnection(&ctx);
    httpcCloseContext(&ctx);
    return buf;

fail:
    httpcCancelConnection(&ctx);
    httpcCloseContext(&ctx);
    return NULL;
}

/* ── Parse le JSON ────────────────────────────────────────────────────────── */

bool versions_fetch(VersionList *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(VersionList));

    printf("[versions] Telechargement versions.json...\n");

    u32 json_size = 0;
    char *json_str = _download_string(VERSIONS_URL, &json_size);
    if (!json_str) {
        printf("[versions] Echec telechargement\n");
        return false;
    }

    printf("[versions] %lu octets reçus\n", (unsigned long)json_size);

    json_error_t err;
    json_t *root = json_loads(json_str, 0, &err);
    free(json_str);

    if (!root) {
        printf("[versions] Erreur JSON: %s (ligne %d)\n", err.text, err.line);
        return false;
    }

    /* patcher_latest */
    json_t *patcher_latest = json_object_get(root, "patcher_latest");
    if (json_is_string(patcher_latest))
        snprintf(out->patcher_latest, sizeof(out->patcher_latest),
                 "%s", json_string_value(patcher_latest));

    /* patcher_download */
    json_t *patcher_dl = json_object_get(root, "patcher_download");
    if (json_is_string(patcher_dl))
        snprintf(out->patcher_download, sizeof(out->patcher_download),
                 "%s", json_string_value(patcher_dl));

    /* Vérifie si une mise à jour du patcher est disponible */
    if (out->patcher_latest[0] != '\0')
        out->update_available =
            (versions_compare(out->patcher_latest, APP_TAG) > 0);

    /* Liste des versions du patch */
    json_t *versions_arr = json_object_get(root, "versions");
    if (!json_is_array(versions_arr)) {
        json_decref(root);
        return false;
    }

    size_t i;
    json_t *entry;
    json_array_foreach(versions_arr, i, entry) {
        if (out->count >= VERSIONS_MAX) break;
        if (!json_is_object(entry)) continue;

        PatchVersion *v = &out->versions[out->count];

        json_t *j;

        j = json_object_get(entry, "version");
        if (json_is_string(j))
            snprintf(v->version, sizeof(v->version), "%s", json_string_value(j));

        j = json_object_get(entry, "date");
        if (json_is_string(j))
            snprintf(v->date, sizeof(v->date), "%s", json_string_value(j));

        j = json_object_get(entry, "notes");
        if (json_is_string(j))
            snprintf(v->notes, sizeof(v->notes), "%s", json_string_value(j));

        j = json_object_get(entry, "bigbang_url");
        if (json_is_string(j))
            snprintf(v->bigbang_url, sizeof(v->bigbang_url),
                     "%s", json_string_value(j));

        j = json_object_get(entry, "supernova_url");
        if (json_is_string(j))
            snprintf(v->supernova_url, sizeof(v->supernova_url),
                     "%s", json_string_value(j));

        j = json_object_get(entry, "coming_soon");
        v->coming_soon = json_is_true(j);

        /* N'ajoute que si l'entrée est valide */
        if (v->version[0] != '\0')
            out->count++;
    }

    json_decref(root);

    printf("[versions] %d version(s) trouvee(s)\n", out->count);
    return (out->count > 0);
}