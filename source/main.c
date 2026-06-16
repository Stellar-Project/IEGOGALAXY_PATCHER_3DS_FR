#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <sys/stat.h>

#include <3ds.h>
#include <citro2d.h>

#include "network.h"
#include "patcher.h"
#include "versions.h"
#include "worker.h"
#include "gui/theme.h"
#include "gui/stars.h"
#include "gui/draw.h"

/* ── Config ───────────────────────────────────────────────────────────────── */

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif
#ifndef APP_TAG
#define APP_TAG "0.0.0"
#endif
#define VERSION        APP_VERSION
#define TITLE_ID_BIGBANG   "000400000010BA00"
#define TITLE_ID_SUPERNOVA "000400000010BB00"
#define TEMP_ZIP_PATH  "sdmc:/iego_patch_temp.zip"
#define LOG_PATH       "sdmc:/3ds/1/debug.log"

/* ── Écrans ───────────────────────────────────────────────────────────────── */

static C3D_RenderTarget *s_top;
static C3D_RenderTarget *s_bot;

/* ── Log ──────────────────────────────────────────────────────────────────── */

static PrintConsole s_console_bot;
static FILE        *s_logfile = NULL;

static void log_open(void)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/1", 0777);
    s_logfile = fopen(LOG_PATH, "w");
}

static void log_close(void)
{
    if (s_logfile) { fclose(s_logfile); s_logfile = NULL; }
}

static void dbg(const char *fmt, ...)
{
    va_list args;
    consoleSelect(&s_console_bot);
    va_start(args, fmt); vprintf(fmt, args); va_end(args);
    if (s_logfile) {
        va_start(args, fmt); vfprintf(s_logfile, fmt, args); va_end(args);
        fflush(s_logfile);
    }
}

/* ── Worker ───────────────────────────────────────────────────────────────── */

static WorkerShared s_worker;

/* ── État UI ──────────────────────────────────────────────────────────────── */

typedef enum {
    SCREEN_LOADING,
    SCREEN_MENU,
    SCREEN_VERSION,
    SCREEN_CONFIRM,
    SCREEN_PROGRESS,
    SCREEN_RESULT,
} Screen;

typedef struct {
    Screen screen;
    int    selection;     /* 0=BigBang 1=Supernova */
    int    version_sel;   /* index dans versions[] */
    bool   install_success;
    bool   cancelled;
} AppState;

static AppState s_state;

/* Timer */
static u64 s_last_tick = 0;

/* Prototype forward */
static void _draw_progress_screen(void);

static float get_dt(void)
{
    u64 now = svcGetSystemTick();
    float dt = (float)(now - s_last_tick) / (float)(CPU_TICKS_PER_MSEC * 1000);
    s_last_tick = now;
    if (dt > 0.1f) dt = 0.1f;
    return dt;
}

/* ── Rendu ────────────────────────────────────────────────────────────────── */

static void _draw_header_top(void)
{
    u32 band = C2D_Color32(0x00, 0x00, 0x00, 0x99);
    C2D_DrawRectSolid(0, 0, 0, 400, 30, band);
    draw_text_centered(6, 400, "IEGO GALAXY PATCHER FR", 0.52f, COL_WHITE);
    char ver[32];
    snprintf(ver, sizeof(ver), "v%s", VERSION);
    draw_text(350, 8, ver, 0.38f, COL_GRAY);
    C2D_DrawRectSolid(0, 30, 0, 400, 2, g_theme.accent);
}

static void _draw_loading_screen(void)
{
    _draw_header_top();
    draw_text_centered(90,  400, "Chargement...", 0.60f, COL_WHITE);
    draw_text_centered(115, 400, "Recuperation des versions disponibles",
                       0.42f, COL_GRAY);
    static float pulse = 0.0f;
    pulse += 0.03f;
    if (pulse > 1.0f) pulse = 0.0f;
    float bar_w = 80.0f + 200.0f * pulse;
    float bar_x = (400.0f - bar_w) * 0.5f;
    C2D_DrawRectSolid(bar_x, 148, 0, bar_w, 4, g_theme.accent);
}

static void _draw_menu_screen(void)
{
    _draw_header_top();
    draw_text_centered(50, 400, "Choisis ton jeu", 0.6f, COL_WHITE);
    draw_separator(72, 400, g_theme.accent);

    bool bb_sel = (s_state.selection == 0);
    u32 bb_bg = bb_sel
        ? C2D_Color32(0x1E, 0x6F, 0xD9, 0xCC)
        : C2D_Color32(0x0A, 0x20, 0x40, 0xCC);
    draw_button(30, 85, 340, 50, "Big Bang", bb_bg, COL_WHITE, bb_sel);
    if (bb_sel)
        draw_text(42, 121, "Tsurugi Kyousuke", 0.38f, BB_RED);

    bool sn_sel = (s_state.selection == 1);
    u32 sn_bg = sn_sel
        ? C2D_Color32(0x6A, 0x1F, 0xD9, 0xCC)
        : C2D_Color32(0x18, 0x0A, 0x30, 0xCC);
    draw_button(30, 145, 340, 50, "Supernova", sn_bg, COL_WHITE, sn_sel);
    if (sn_sel)
        draw_text(42, 181, "Fubuki Shirou", 0.38f, SN_BLUE);

    draw_separator(205, 400, g_theme.accent);
    draw_text_centered(212, 400, "[A] Selectionner  [START] Quitter",
                       0.42f, COL_GRAY);
}

static void _draw_version_screen(void)
{
    _draw_header_top();

    const char *game = s_state.selection == 0 ? "Big Bang" : "Supernova";
    char title[64];
    snprintf(title, sizeof(title), "Versions  %s", game);
    draw_text_centered(42, 400, title, 0.52f, COL_WHITE);
    draw_separator(62, 400, g_theme.accent);

    LightLock_Lock(&s_worker.lock);
    bool has_error = (s_worker.state == WORKER_ERROR);
    int  count     = s_worker.versions.count;
    LightLock_Unlock(&s_worker.lock);

    if (has_error || count == 0) {
        draw_text_centered(100, 400, "Impossible de charger les versions.",
                           0.48f, COL_ERROR);
        draw_text_centered(120, 400, "Verifie ta connexion Wi-Fi.",
                           0.42f, COL_GRAY);
        draw_separator(190, 400, g_theme.accent);
        draw_text_centered(198, 400, "[B] Retour", 0.42f, COL_GRAY);
        return;
    }

    int visible = count < 4 ? count : 4;
    float item_h = 34.0f, start_y = 72.0f;

    for (int i = 0; i < visible; i++) {
        LightLock_Lock(&s_worker.lock);
        PatchVersion v = s_worker.versions.versions[i];
        LightLock_Unlock(&s_worker.lock);

        bool sel = (i == s_state.version_sel);
        u32 bg = sel ? C2D_Color32(0,0,0,0xBB) : C2D_Color32(0,0,0,0x55);
        float y = start_y + i * (item_h + 4);
        draw_panel(20, y, 360, item_h, bg);
        if (sel) C2D_DrawRectSolid(20, y, 0, 4, item_h, g_theme.highlight);

        if (v.coming_soon) {
            C2D_DrawRectSolid(268, y+8, 0, 82, 18,
                              C2D_Color32(0x88,0x44,0x00,0xFF));
            draw_text(274, y+10, "BIENTOT", 0.36f, COL_WHITE);
        } else if (i == 0) {
            C2D_DrawRectSolid(290, y+8, 0, 60, 18,
                              C2D_Color32(0x1E,0x6F,0xD9,0xFF));
            draw_text(296, y+10, "LATEST", 0.36f, COL_WHITE);
        }

        char ver_str[64];
        snprintf(ver_str, sizeof(ver_str), "v%s", v.version);
        u32 col_ver = v.coming_soon ? COL_GRAY : (sel ? COL_WHITE : COL_GRAY);
        draw_text(32, y+6,  ver_str, 0.50f, col_ver);
        draw_text(32, y+20, v.date,  0.36f, COL_GRAY);
        if (v.notes[0]) {
            char notes[48];
            snprintf(notes, sizeof(notes), "%.45s", v.notes);
            draw_text(120, y+13, notes, 0.38f,
                      sel ? g_theme.accent2 : COL_GRAY);
        }
    }

    draw_separator(190, 400, g_theme.accent);
    draw_text_centered(198, 400,
                       "[A] Installer  [UP/DOWN] Naviguer  [B] Retour",
                       0.38f, COL_GRAY);
}

static void _draw_confirm_screen(void)
{
    _draw_header_top();

    const char *name = s_state.selection == 0 ? "Big Bang" : "Supernova";
    draw_text_centered(42, 400, "Confirmer l'installation", 0.56f, COL_WHITE);
    draw_separator(62, 400, g_theme.accent);

    draw_panel(20, 72, 360, 112, C2D_Color32(0,0,0,0x88));
    C2D_DrawRectSolid(20, 72, 0, 4, 112, g_theme.highlight);

    char line[128];
    snprintf(line, sizeof(line), "Jeu     : %s", name);
    draw_text(32, 80, line, 0.50f, COL_WHITE);

    LightLock_Lock(&s_worker.lock);
    int count = s_worker.versions.count;
    char ver_str[64] = "latest";
    char notes_str[128] = "";
    if (count > 0 && s_state.version_sel < count) {
        PatchVersion *v = &s_worker.versions.versions[s_state.version_sel];
        snprintf(ver_str,   sizeof(ver_str),   "v%s  (%s)", v->version, v->date);
        snprintf(notes_str, sizeof(notes_str), "%s", v->notes);
    }
    LightLock_Unlock(&s_worker.lock);

    char ver_line[128];
    snprintf(ver_line, sizeof(ver_line), "Version : %s", ver_str);
    draw_text(32, 100, ver_line, 0.44f, g_theme.accent2);
    if (notes_str[0])
        draw_text(32, 118, notes_str, 0.38f, COL_GRAY);

    draw_text(32, 138, "Dest: sdmc:/luma/titles/ID/romfs/", 0.38f, COL_GRAY);
    draw_text(32, 154, "(chemin inclus dans le ZIP)", 0.36f, COL_GRAY);

    draw_separator(190, 400, g_theme.accent);
    draw_text_centered(198, 400, "[A] Installer   [B] Retour", 0.44f, COL_GRAY);
}

void _draw_progress_screen(void)
{
    _draw_header_top();

    LightLock_Lock(&s_worker.lock);
    char   status[128];
    double progress  = s_worker.progress;
    u32    speed_kbs = s_worker.speed_kbs;
    int    retries   = s_worker.retries;
    snprintf(status, sizeof(status), "%s", s_worker.status);
    LightLock_Unlock(&s_worker.lock);

    draw_text_centered(40, 400, status, 0.50f, COL_WHITE);

    char pct_str[32];
    if (progress < 0) snprintf(pct_str, sizeof(pct_str), "...");
    else              snprintf(pct_str, sizeof(pct_str), "%.1f%%", progress);
    draw_text_centered(60, 400, pct_str, 0.65f, g_theme.accent2);

    float pct = (float)(progress < 0 ? 0 : progress);
    draw_progress_bar(20, 100, 360, 18, pct,
                      C2D_Color32(0,0,0,0xAA),
                      g_theme.accent,
                      g_theme.accent2);
    if (pct > 0) {
        C2D_DrawRectSolid(20, 100, 0, 360.0f*(pct/100.0f), 6,
                          C2D_Color32(0xFF,0xFF,0xFF,0x30));
    }

    char speed_str[32];
    if (speed_kbs >= 1024)
        snprintf(speed_str, sizeof(speed_str), "%.1f Mo/s",
                 (float)speed_kbs/1024.0f);
    else if (speed_kbs > 0)
        snprintf(speed_str, sizeof(speed_str), "%lu Ko/s",
                 (unsigned long)speed_kbs);
    else
        snprintf(speed_str, sizeof(speed_str), "calcul...");

    draw_text(20, 128, "Debit :", 0.44f, COL_GRAY);
    draw_text(80, 128, speed_str, 0.44f, g_theme.accent2);

    if (retries > 0) {
        char ret_str[32];
        snprintf(ret_str, sizeof(ret_str), "Reconnexions : %d", retries);
        draw_text(20, 148, ret_str, 0.40f, COL_GRAY);
    }
    draw_separator(188, 400, g_theme.accent);
    draw_text_centered(196, 400, "[B] Annuler", 0.44f,
                       C2D_Color32(0xFF,0x88,0x88,0xFF));
}

static void _draw_result_screen(void)
{
    _draw_header_top();

    if (s_state.cancelled) {
        draw_text_centered(70,  400, "Telechargement annule", 0.60f, COL_GRAY);
        draw_text_centered(100, 400, "Le fichier temporaire a ete supprime.",
                           0.44f, COL_GRAY);
    } else if (s_state.install_success) {
        C2D_DrawRectSolid(180, 50, 0, 40, 40,
                          C2D_Color32(0x44,0xFF,0x88,0x33));
        draw_text_centered(58,  400, "OK", 0.80f, COL_SUCCESS);
        draw_text_centered(105, 400, "Patch installe avec succes !", 0.55f,
                           COL_WHITE);
        draw_separator(125, 400, g_theme.accent);
        draw_text_centered(135, 400, "Active LayeredFS dans Luma", 0.44f,
                           COL_GRAY);
        draw_text_centered(153, 400, "(maintenir SELECT au demarrage)",
                           0.40f, COL_GRAY);
    } else {
        LightLock_Lock(&s_worker.lock);
        char err_status[128];
        snprintf(err_status, sizeof(err_status), "%s", s_worker.status);
        LightLock_Unlock(&s_worker.lock);
        draw_text_centered(58,  400, "ERREUR", 0.70f, COL_ERROR);
        draw_separator(85,  400, g_theme.accent);
        draw_text(20, 95,  err_status, 0.44f, COL_GRAY);
        draw_text(20, 115, "Voir : sdmc:/3ds/1/debug.log", 0.40f, COL_GRAY);
    }

    draw_separator(190, 400, g_theme.accent);
    draw_text_centered(198, 400,
        s_state.cancelled ? "[A] Menu" : "[A] Menu  [START] Quitter",
        0.42f, COL_GRAY);
}

/* ── Point d'entrée ───────────────────────────────────────────────────────── */

int main(void)
{
    gfxInitDefault();
    gfxSet3D(false);
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    s_top = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
    s_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    consoleInit(GFX_BOTTOM, &s_console_bot);

    log_open();
    dbg("=== IEGO Patcher v%s ===\n", VERSION);

    worker_init(&s_worker);
    network_set_logfile(s_logfile);
    theme_set_bigbang();
    stars_init();
    aptSetSleepAllowed(false);

    memset(&s_state, 0, sizeof(s_state));
    s_state.screen    = SCREEN_LOADING;
    s_state.selection = 0;
    s_last_tick = svcGetSystemTick();

    if (!network_init()) {
        dbg("ERREUR httpcInit\n");
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(s_top, BB_BG_TOP);
        C2D_SceneBegin(s_top);
        draw_text_centered(100, 400, "ERREUR: Wi-Fi non disponible",
                           0.55f, COL_ERROR);
        draw_text_centered(125, 400, "Voir sdmc:/3ds/1/debug.log",
                           0.44f, COL_GRAY);
        draw_text_centered(150, 400, "[START] Quitter", 0.44f, COL_GRAY);
        C3D_FrameEnd(0);
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & KEY_START) break;
            gspWaitForVBlank();
        }
        goto shutdown;
    }
    dbg("httpcInit OK\n");

    /* Lance le chargement des versions en arrière-plan */
    worker_start_fetch_versions(&s_worker);

    /* ── Boucle principale ─────────────────────────────────────────────────── */
    while (aptMainLoop()) {
        hidScanInput();
        u32 keys      = hidKeysDown();
        u32 keys_held = hidKeysHeld();

        float dt = get_dt();
        stars_update(dt);

        WorkerState wstate = worker_get_state(&s_worker);

        /* ── Transitions automatiques ── */

        /* LOADING → MENU quand le worker a fini de charger les versions */
        if (s_state.screen == SCREEN_LOADING &&
            (wstate == WORKER_DONE || wstate == WORKER_ERROR)) {
            LightLock_Lock(&s_worker.lock);
            int count = s_worker.versions.count;
            bool upd  = s_worker.versions.update_available;
            char pver[32];
            snprintf(pver, sizeof(pver), "%s", s_worker.versions.patcher_latest);
            LightLock_Unlock(&s_worker.lock);
            dbg("Versions: %d\n", count);
            if (upd) dbg("Update patcher dispo: %s\n", pver);
            worker_join(&s_worker);
            s_state.screen = SCREEN_MENU;
        }

        /* PROGRESS → RESULT quand le worker a terminé */
        if (s_state.screen == SCREEN_PROGRESS) {
            if (keys_held & KEY_B) {
                dbg("Annulation\n");
                worker_cancel(&s_worker);
                s_state.cancelled       = true;
                s_state.install_success = false;
                s_state.screen = SCREEN_RESULT;
            } else if (wstate == WORKER_DONE) {
                worker_join(&s_worker);
                s_state.install_success = true;
                s_state.cancelled       = false;
                s_state.screen = SCREEN_RESULT;
                dbg("Installation OK\n");
            } else if (wstate == WORKER_ERROR || wstate == WORKER_CANCELLED) {
                worker_join(&s_worker);
                s_state.install_success = false;
                s_state.cancelled       = (wstate == WORKER_CANCELLED);
                s_state.screen = SCREEN_RESULT;
                dbg("Installation %s\n",
                    wstate == WORKER_CANCELLED ? "annulee" : "erreur");
            }
        }

        /* ── Input ── */
        switch (s_state.screen) {

        case SCREEN_LOADING:
            break;

        case SCREEN_MENU:
            if (keys & KEY_A) {
                if (s_state.selection == 0) theme_set_bigbang();
                else                         theme_set_supernova();
                s_state.version_sel = 0;
                s_state.screen = SCREEN_VERSION;
            } else if (keys & (KEY_UP|KEY_LEFT)) {
                s_state.selection = 0; theme_set_bigbang();
            } else if (keys & (KEY_DOWN|KEY_RIGHT)) {
                s_state.selection = 1; theme_set_supernova();
            } else if (keys & KEY_START) {
                goto shutdown;
            }
            break;

        case SCREEN_VERSION: {
            LightLock_Lock(&s_worker.lock);
            int count = s_worker.versions.count;
            bool coming = (count > 0 && s_state.version_sel < count)
                ? s_worker.versions.versions[s_state.version_sel].coming_soon
                : false;
            LightLock_Unlock(&s_worker.lock);

            if (count == 0 || wstate == WORKER_ERROR) {
                if (keys & KEY_B) s_state.screen = SCREEN_MENU;
            } else {
                if      (keys & KEY_UP   && s_state.version_sel > 0)
                    s_state.version_sel--;
                else if (keys & KEY_DOWN && s_state.version_sel < count-1)
                    s_state.version_sel++;
                else if ((keys & KEY_A) && !coming)
                    s_state.screen = SCREEN_CONFIRM;
                else if (keys & KEY_B)
                    s_state.screen = SCREEN_MENU;
            }
            break;
        }

        case SCREEN_CONFIRM: {
            if (keys & KEY_A) {
                WorkerParams params;
                snprintf(params.temp_zip, sizeof(params.temp_zip), TEMP_ZIP_PATH);
                snprintf(params.dest,     sizeof(params.dest),     "sdmc:/");

                LightLock_Lock(&s_worker.lock);
                int count = s_worker.versions.count;
                if (count > 0 && s_state.version_sel < count) {
                    PatchVersion *v = &s_worker.versions.versions[s_state.version_sel];
                    const char *url = (s_state.selection == 0)
                        ? v->bigbang_url : v->supernova_url;
                    snprintf(params.url, sizeof(params.url), "%s", url);
                    dbg("URL: %s  ver: %s\n", params.url, v->version);
                } else {
                    snprintf(params.url, sizeof(params.url), "%s",
                        s_state.selection == 0
                        ? "http://iegogalaxy.fr/downloads/patch/latest/patch_bigbang_fr.zip"
                        : "http://iegogalaxy.fr/downloads/patch/latest/patch_supernova_fr.zip");
                }
                LightLock_Unlock(&s_worker.lock);

                worker_start_install(&s_worker, &params);
                s_state.screen    = SCREEN_PROGRESS;
                s_state.cancelled = false;

            } else if (keys & KEY_B) {
                s_state.screen = SCREEN_VERSION;
            }
            break;
        }

        case SCREEN_PROGRESS:
            break;

        case SCREEN_RESULT:
            if (keys & KEY_A)
                s_state.screen = SCREEN_MENU;
            else if ((keys & KEY_START) && !s_state.cancelled)
                goto shutdown;
            break;
        }

        /* ── Rendu ─────────────────────────────────────────────────────────── */
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        C2D_TargetClear(s_top, g_theme.bg_top);
        C2D_SceneBegin(s_top);
        stars_draw_top();

        switch (s_state.screen) {
        case SCREEN_LOADING:  _draw_loading_screen();  break;
        case SCREEN_MENU:     _draw_menu_screen();     break;
        case SCREEN_VERSION:  _draw_version_screen();  break;
        case SCREEN_CONFIRM:  _draw_confirm_screen();  break;
        case SCREEN_PROGRESS: _draw_progress_screen(); break;
        case SCREEN_RESULT:   _draw_result_screen();   break;
        }

        C2D_TargetClear(s_bot, g_theme.bg_bot);
        C2D_SceneBegin(s_bot);
        stars_draw_bottom();

        C3D_FrameEnd(0);
        gspWaitForVBlank();
    }

shutdown:
    dbg("=== Fin ===\n");
    worker_cancel(&s_worker);
    log_close();
    aptSetSleepAllowed(true);
    network_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}