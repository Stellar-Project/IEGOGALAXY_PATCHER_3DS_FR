#include "worker.h"
#include "network.h"
#include "patcher.h"
#include "versions.h"

#include <stdio.h>
#include <string.h>
#include <3ds.h>

/* ── Stack size du thread worker ─────────────────────────────────────────── */
#define WORKER_STACK_SIZE (256 * 1024)  /* 256 Ko — suffisant pour httpc + minizip */

/* ── Contexte interne ─────────────────────────────────────────────────────── */

typedef struct {
    WorkerShared *shared;
    WorkerParams  params;
    bool          is_install;  /* false = fetch versions, true = install */
} WorkerCtx;

static Thread    s_thread = NULL;
static WorkerCtx s_ctx;

/* ── Helpers thread-safe ──────────────────────────────────────────────────── */

static void _set_state(WorkerShared *s, WorkerState state)
{
    LightLock_Lock(&s->lock);
    s->state = state;
    LightLock_Unlock(&s->lock);
}

static void _set_status(WorkerShared *s, const char *msg)
{
    LightLock_Lock(&s->lock);
    snprintf(s->status, sizeof(s->status), "%s", msg);
    LightLock_Unlock(&s->lock);
}

static void _set_progress(WorkerShared *s, double pct, u32 speed, int retries)
{
    LightLock_Lock(&s->lock);
    s->progress  = pct;
    s->speed_kbs = speed;
    s->retries   = retries;
    LightLock_Unlock(&s->lock);
}

static bool _is_cancelled(WorkerShared *s)
{
    return s->cancel_requested;
}

/* ── Callbacks réseau → worker ────────────────────────────────────────────── */

typedef struct {
    WorkerShared *shared;
} ProgressCtx;

static void _status_cb(const char *msg, void *userdata)
{
    ProgressCtx *ctx = (ProgressCtx *)userdata;
    _set_status(ctx->shared, msg);
}

static void _patcher_progress_cb(double value, int retries,
                                  u32 speed_kbs, void *userdata)
{
    ProgressCtx *ctx = (ProgressCtx *)userdata;
    _set_progress(ctx->shared, value, speed_kbs, retries);
}

/* ── Fonctions worker ─────────────────────────────────────────────────────── */

static void _worker_fetch_versions(WorkerShared *s)
{
    _set_state(s, WORKER_FETCHING_VERSIONS);
    _set_status(s, "Chargement des versions...");
    _set_progress(s, -1.0, 0, 0);

    LightLock_Lock(&s->lock);
    bool ok = versions_fetch(&s->versions);
    LightLock_Unlock(&s->lock);

    if (_is_cancelled(s)) {
        _set_state(s, WORKER_CANCELLED);
        return;
    }

    if (ok) {
        _set_status(s, "Versions chargees");
        _set_state(s, WORKER_DONE);
    } else {
        _set_status(s, "Erreur chargement versions");
        _set_state(s, WORKER_ERROR);
    }
}

static void _worker_install(WorkerShared *s, const WorkerParams *p)
{
    _set_state(s, WORKER_DOWNLOADING);
    _set_status(s, "Preparation...");
    _set_progress(s, -1.0, 0, 0);

    ProgressCtx ctx = { s };

    bool ok = patcher_install(
        p->url,
        p->temp_zip,
        p->dest,
        _status_cb,
        _patcher_progress_cb,
        &ctx
    );

    if (_is_cancelled(s)) {
        _set_state(s, WORKER_CANCELLED);
        return;
    }

    if (ok) {
        _set_status(s, "Installation terminee !");
        _set_progress(s, 100.0, 0, 0);
        _set_state(s, WORKER_DONE);
    } else {
        _set_state(s, WORKER_ERROR);
    }
}

/* ── Point d'entrée du thread ─────────────────────────────────────────────── */

static void _worker_entrypoint(void *arg)
{
    WorkerCtx *ctx = (WorkerCtx *)arg;
    WorkerShared *s = ctx->shared;

    if (ctx->is_install)
        _worker_install(s, &ctx->params);
    else
        _worker_fetch_versions(s);

    threadExit(0);
}

/* ── API publique ─────────────────────────────────────────────────────────── */

void worker_init(WorkerShared *shared)
{
    memset(shared, 0, sizeof(WorkerShared));
    LightLock_Init(&shared->lock);
    shared->state    = WORKER_IDLE;
    shared->progress = -1.0;
}

static void _start_thread(WorkerShared *shared, bool is_install,
                           const WorkerParams *params)
{
    /* Attend que le thread précédent soit terminé */
    worker_join(shared);

    shared->cancel_requested = false;

    LightLock_Lock(&shared->lock);
    shared->state    = WORKER_IDLE;
    shared->progress = -1.0;
    shared->retries  = 0;
    shared->speed_kbs = 0;
    LightLock_Unlock(&shared->lock);

    s_ctx.shared     = shared;
    s_ctx.is_install = is_install;
    if (params)
        s_ctx.params = *params;

    /* Priorité légèrement inférieure au thread principal
     * comme hShop (prio - 1) */
    s32 prio = 0;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);

    s_thread = threadCreate(_worker_entrypoint, &s_ctx,
                            WORKER_STACK_SIZE,
                            prio + 1,   /* +1 = priorité plus basse */
                            -2,         /* affinité auto */
                            false);     /* pas de détachement auto */
}

void worker_start_fetch_versions(WorkerShared *shared)
{
    _start_thread(shared, false, NULL);
}

void worker_start_install(WorkerShared *shared, const WorkerParams *params)
{
    _start_thread(shared, true, params);
}

void worker_cancel(WorkerShared *shared)
{
    shared->cancel_requested = true;
    network_cancel();
    worker_join(shared);
}

void worker_join(WorkerShared *shared)
{
    (void)shared;
    if (s_thread != NULL) {
        threadJoin(s_thread, U64_MAX);
        threadFree(s_thread);
        s_thread = NULL;
    }
}

WorkerState worker_get_state(WorkerShared *shared)
{
    LightLock_Lock(&shared->lock);
    WorkerState s = shared->state;
    LightLock_Unlock(&shared->lock);
    return s;
}

bool worker_is_running(WorkerShared *shared)
{
    WorkerState s = worker_get_state(shared);
    return s == WORKER_FETCHING_VERSIONS
        || s == WORKER_DOWNLOADING
        || s == WORKER_EXTRACTING;
}