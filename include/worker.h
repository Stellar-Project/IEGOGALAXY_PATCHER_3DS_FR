#pragma once

#include <stdbool.h>
#include <3ds.h>
#include "versions.h"

/* ── États du worker ──────────────────────────────────────────────────────── */

typedef enum {
    WORKER_IDLE,
    WORKER_FETCHING_VERSIONS,  /* téléchargement versions.json */
    WORKER_DOWNLOADING,        /* téléchargement du patch */
    WORKER_EXTRACTING,         /* extraction du ZIP */
    WORKER_DONE,               /* terminé avec succès */
    WORKER_ERROR,              /* erreur */
    WORKER_CANCELLED,          /* annulé par l'utilisateur */
} WorkerState;

/* ── Données partagées thread principal ↔ worker ──────────────────────────── */

typedef struct {
    /* État courant — lu par le thread principal */
    WorkerState state;

    /* Progression — mis à jour par le worker, lu par le rendu */
    double      progress;       /* 0.0–100.0, négatif = indéterminé */
    u32         speed_kbs;      /* débit en Ko/s */
    int         retries;        /* reconnexions réseau */
    char        status[128];    /* message de statut */

    /* Résultat versions — rempli après WORKER_FETCHING_VERSIONS */
    VersionList versions;

    /* Flag d'annulation — écrit par le thread principal */
    volatile bool cancel_requested;

    /* Verrou pour accès concurrent aux champs ci-dessus */
    LightLock lock;

} WorkerShared;

/* ── Paramètres passés au worker avant lancement ─────────────────────────── */

typedef struct {
    char url[256];
    char dest[256];
    char temp_zip[256];
} WorkerParams;

/* ── API publique ─────────────────────────────────────────────────────────── */

/* Initialise la structure partagée — à appeler une fois au démarrage */
void worker_init(WorkerShared *shared);

/* Lance le worker pour récupérer les versions */
void worker_start_fetch_versions(WorkerShared *shared);

/* Lance le worker pour installer un patch */
void worker_start_install(WorkerShared *shared, const WorkerParams *params);

/* Demande l'annulation et attend la fin du thread */
void worker_cancel(WorkerShared *shared);

/* Attend que le worker ait terminé (join) */
void worker_join(WorkerShared *shared);

/* Lecture thread-safe de l'état */
WorkerState worker_get_state(WorkerShared *shared);

/* Indique si le worker est en train de tourner */
bool worker_is_running(WorkerShared *shared);