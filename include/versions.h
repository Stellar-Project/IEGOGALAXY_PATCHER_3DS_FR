#pragma once

#include <stdbool.h>
#include <3ds.h>

#define VERSIONS_URL "http://iegogalaxy.fr/downloads/patch/versions.json"
#define VERSIONS_MAX 16

typedef struct {
    char version[32];
    char date[16];
    char notes[128];
    char bigbang_url[256];
    char supernova_url[256];
    bool coming_soon;
} PatchVersion;

typedef struct {
    PatchVersion versions[VERSIONS_MAX];
    int          count;
    char         patcher_latest[32];   /* dernière version du patcher */
    char         patcher_download[256]; /* URL du .3dsx à télécharger */
    bool         update_available;      /* true si patcher_latest > APP_TAG */
} VersionList;

/*
 * Télécharge et parse versions.json depuis le serveur.
 * Retourne true en cas de succès.
 * La liste est allouée statiquement — pas besoin de free.
 */
bool versions_fetch(VersionList *out);

/*
 * Compare deux versions "X.Y.Z".
 * Retourne  1 si a > b
 *           0 si a == b
 *          -1 si a < b
 */
int versions_compare(const char *a, const char *b);