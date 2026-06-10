#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <sys/stat.h>

#include <3ds.h>
#include <citro2d.h>

#include "network.h"
#include "patcher.h"
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
#define VERSION APP_VERSION
#define TITLE_ID_BIGBANG   "000400000010BA00"
#define TITLE_ID_SUPERNOVA "000400000010BB00"
#define URL_PATCH_BIGBANG \
    "http://iegogalaxy.fr/downloads/patch/latest/patch_bigbang_fr.zip"
#define URL_PATCH_SUPERNOVA \
    "http://iegogalaxy.fr/downloads/patch/latest/patch_supernova_fr.zip"
#define TEMP_ZIP_PATH  "sdmc:/iego_patch_temp.zip"
#define DEST_BASE_PATH "sdmc:/"
#define LOG_PATH       "sdmc:/3ds/1/debug.log"

/* ── Écrans citro2d ───────────────────────────────────────────────────────── */

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
    /* Écran du bas en mode console pour les logs */
    consoleSelect(&s_console_bot);
    va_start(args, fmt); vprintf(fmt, args); va_end(args);
    if (s_logfile) {
        va_start(args, fmt); vfprintf(s_logfile, fmt, args); va_end(args);
        fflush(s_logfile);
    }
}

/* ── État UI ──────────────────────────────────────────────────────────────── */

typedef enum {
    SCREEN_MENU,
    SCREEN_CONFIRM,
    SCREEN_PROGRESS,
    SCREEN_RESULT,
} Screen;

typedef struct {
    Screen screen;
    int    selection;      /* 0=BigBang 1=Supernova */
    char   status_msg[128];
    double progress;
    int    retries;
    u32    speed_kbs;
    bool   install_success;
    bool   cancelled;
} AppState;

static AppState s_state;

/* Timer pour dt */
static u64 s_last_tick = 0;

/* Prototype forward — définie plus bas */
static void _draw_progress_screen(void);

static float get_dt(void)
{
    u64 now = svcGetSystemTick();
    /* CPU_TICKS_PER_MSEC * 1000 = ticks par seconde */
    float dt = (float)(now - s_last_tick) / (float)(CPU_TICKS_PER_MSEC * 1000);
    s_last_tick = now;
    if (dt > 0.1f) dt = 0.1f;
    return dt;
}

/* ── Callbacks ────────────────────────────────────────────────────────────── */

static void _on_status(const char *msg, void *userdata)
{
    (void)userdata;
    snprintf(s_state.status_msg, sizeof(s_state.status_msg), "%s", msg);
    dbg("[status] %s\n", msg);
}

static void _on_progress(double value, int retries, u32 speed_kbs, void *ud)
{
    (void)ud;
    s_state.progress  = value;
    s_state.retries   = retries;
    s_state.speed_kbs = speed_kbs;

    hidScanInput();
    if (hidKeysHeld() & KEY_B) {
        network_cancel();
        snprintf(s_state.status_msg, sizeof(s_state.status_msg), "Annule");
        s_state.cancelled = true;
    }

    /* Met à jour l'affichage pendant le téléchargement */
    float dt = get_dt();
    stars_update(dt);

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    /* Écran haut */
    C2D_TargetClear(s_top, g_theme.bg_top);
    C2D_SceneBegin(s_top);
    stars_draw_top();
    _draw_progress_screen();

    /* Écran bas — console debug */
    C2D_TargetClear(s_bot, g_theme.bg_bot);
    C2D_SceneBegin(s_bot);
    stars_draw_bottom();

    C3D_FrameEnd(0);
}

/* ── Rendu écrans ─────────────────────────────────────────────────────────── */

static void _draw_header_top(void)
{
    /* Bandeau titre */
    u32 band = C2D_Color32(0x00, 0x00, 0x00, 0x99);
    C2D_DrawRectSolid(0, 0, 0, 400, 30, band);
    draw_text_centered(6, 400, "IEGO GALAXY PATCHER FR", 0.52f, COL_WHITE);

    /* Version */
    char ver[16];
    snprintf(ver, sizeof(ver), "v%s", VERSION);
    draw_text(370, 8, ver, 0.38f, COL_GRAY);

    /* Ligne accent */
    C2D_DrawRectSolid(0, 30, 0, 400, 2, g_theme.accent);
}

static void _draw_menu_screen(void)
{
    _draw_header_top();

    /* Titre section */
    draw_text_centered(50, 400, "Choisis ton jeu", 0.6f, COL_WHITE);
    draw_separator(72, 400, g_theme.accent);

    /* Bouton Big Bang */
    bool bb_sel = (s_state.selection == 0);
    u32 bb_bg = bb_sel
        ? C2D_Color32(0x1E, 0x6F, 0xD9, 0xCC)
        : C2D_Color32(0x0A, 0x20, 0x40, 0xCC);
    draw_button(30, 85, 340, 50,
                "Big Bang", bb_bg, COL_WHITE, bb_sel);

    /* Sous-titre Big Bang */
    if (bb_sel)
        draw_text(42, 121, "Tsurugi Kyousuke", 0.38f, BB_RED);

    /* Bouton Supernova */
    bool sn_sel = (s_state.selection == 1);
    u32 sn_bg = sn_sel
        ? C2D_Color32(0x6A, 0x1F, 0xD9, 0xCC)
        : C2D_Color32(0x18, 0x0A, 0x30, 0xCC);
    draw_button(30, 145, 340, 50,
                "Supernova", sn_bg, COL_WHITE, sn_sel);

    if (sn_sel)
        draw_text(42, 181, "Fubuki Shirou", 0.38f, SN_BLUE);

    /* Aide touches */
    draw_separator(205, 400, g_theme.accent);
    draw_text_centered(212, 400, "[A] Selectionner  [START] Quitter",
                       0.42f, COL_GRAY);
}

static void _draw_confirm_screen(void)
{
    _draw_header_top();

    const char *name = s_state.selection == 0 ? "Big Bang" : "Supernova";

    draw_text_centered(45, 400, "Confirmer l'installation", 0.58f, COL_WHITE);
    draw_separator(68, 400, g_theme.accent);

    /* Panneau info */
    u32 panel = C2D_Color32(0x00, 0x00, 0x00, 0x88);
    draw_panel(20, 78, 360, 100, panel);
    C2D_DrawRectSolid(20, 78, 0, 4, 100, g_theme.highlight);

    char line[128];
    snprintf(line, sizeof(line), "Jeu : %s", name);
    draw_text(32, 86, line, 0.50f, COL_WHITE);

    draw_text(32, 108, "Destination :", 0.44f, COL_GRAY);
    draw_text(32, 124, "sdmc:/luma/titles/ID/romfs/", 0.38f, g_theme.accent2);
    draw_text(32, 140, "(chemin inclus dans le ZIP)", 0.36f, COL_GRAY);

    draw_text(32, 148, "Le patch sera telecharge puis installe.", 0.40f, COL_GRAY);

    draw_separator(188, 400, g_theme.accent);
    draw_text_centered(196, 400,
                       "[A] Installer   [B] Retour", 0.44f, COL_GRAY);
}

void _draw_progress_screen(void)
{
    _draw_header_top();

    /* Statut */
    draw_text_centered(40, 400, s_state.status_msg, 0.50f, COL_WHITE);

    /* Pourcentage */
    char pct_str[32];
    if (s_state.progress < 0)
        snprintf(pct_str, sizeof(pct_str), "...");
    else
        snprintf(pct_str, sizeof(pct_str), "%.1f%%", s_state.progress);
    draw_text_centered(60, 400, pct_str, 0.65f, g_theme.accent2);

    /* Barre de progression */
    float pct = (float)(s_state.progress < 0 ? 0 : s_state.progress);
    u32 bar_bg   = C2D_Color32(0x00, 0x00, 0x00, 0xAA);
    u32 bar_fill = g_theme.accent;
    u32 bar_bord = g_theme.accent2;
    draw_progress_bar(20, 100, 360, 18, pct, bar_bg, bar_fill, bar_bord);

    /* Effet de brillance sur la barre */
    if (pct > 0) {
        float fill_w = 360.0f * (pct / 100.0f);
        u32 shine = C2D_Color32(0xFF, 0xFF, 0xFF, 0x30);
        C2D_DrawRectSolid(20, 100, 0, fill_w, 6, shine);
    }

    /* Débit */
    char speed_str[32];
    if (s_state.speed_kbs >= 1024)
        snprintf(speed_str, sizeof(speed_str), "%.1f Mo/s",
                 (float)s_state.speed_kbs / 1024.0f);
    else if (s_state.speed_kbs > 0)
        snprintf(speed_str, sizeof(speed_str), "%lu Ko/s",
                 (unsigned long)s_state.speed_kbs);
    else
        snprintf(speed_str, sizeof(speed_str), "calcul...");

    draw_text(20, 128, "Debit :", 0.44f, COL_GRAY);
    draw_text(80, 128, speed_str, 0.44f, g_theme.accent2);

    /* Reconnexions */
    if (s_state.retries > 0) {
        char ret_str[32];
        snprintf(ret_str, sizeof(ret_str), "Reconnexions : %d",
                 s_state.retries);
        draw_text(20, 148, ret_str, 0.40f, COL_GRAY);
    }

    draw_text(20, 165, "Tranche en cours (max 350 Mo)", 0.38f, COL_GRAY);

    draw_separator(188, 400, g_theme.accent);
    draw_text_centered(196, 400, "[B] Annuler", 0.44f,
                       C2D_Color32(0xFF, 0x88, 0x88, 0xFF));
}

static void _draw_result_screen(void)
{
    _draw_header_top();

    if (s_state.cancelled) {
        draw_text_centered(70, 400, "Telechargement annule", 0.60f,
                           COL_GRAY);
        draw_text_centered(100, 400, "Le fichier temporaire", 0.44f, COL_GRAY);
        draw_text_centered(118, 400, "a ete supprime.", 0.44f, COL_GRAY);
    } else if (s_state.install_success) {
        /* Icône succès */
        C2D_DrawRectSolid(180, 50, 0, 40, 40,
                          C2D_Color32(0x44, 0xFF, 0x88, 0x33));
        draw_text_centered(58, 400, "OK", 0.80f, COL_SUCCESS);

        draw_text_centered(105, 400, "Patch installe avec succes !", 0.55f,
                           COL_WHITE);
        draw_separator(125, 400, g_theme.accent);
        draw_text_centered(135, 400, "Active LayeredFS dans Luma", 0.44f,
                           COL_GRAY);
        draw_text_centered(153, 400, "(maintenir SELECT au demarrage)", 0.40f,
                           COL_GRAY);
    } else {
        draw_text_centered(58, 400, "ERREUR", 0.70f, COL_ERROR);
        draw_separator(85, 400, g_theme.accent);
        draw_text(20, 95, s_state.status_msg, 0.44f, COL_GRAY);
        draw_text(20, 115, "Voir : sdmc:/3ds/1/debug.log", 0.40f, COL_GRAY);
    }

    draw_separator(190, 400, g_theme.accent);
    draw_text_centered(198, 400,
                       s_state.cancelled ? "[A] Menu" : "[A] Menu  [START] Quitter",
                       0.42f, COL_GRAY);
}

/* ── Boucle principale ────────────────────────────────────────────────────── */

int main(void)
{
    /* Init 3DS */
    gfxInitDefault();
    gfxSet3D(false);

    /* citro2d */
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    s_top = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
    s_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    /* Console de debug sur l'écran du bas */
    consoleInit(GFX_BOTTOM, &s_console_bot);

    /* Log */
    log_open();
    dbg("=== IEGO Patcher v%s ===\n", VERSION);

    /* Réseau */
    network_set_logfile(s_logfile);

    /* Thème par défaut : Big Bang */
    theme_set_bigbang();
    stars_init();

    /* Anti-veille */
    aptSetSleepAllowed(false);

    memset(&s_state, 0, sizeof(s_state));
    s_state.screen    = SCREEN_MENU;
    s_state.selection = 0;
    s_last_tick = svcGetSystemTick();

    dbg("httpcInit...\n");
    if (!network_init()) {
        dbg("ERREUR httpcInit\n");
        /* Affiche l'erreur à l'écran */
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(s_top, BB_BG_TOP);
        C2D_SceneBegin(s_top);
        draw_text_centered(100, 400, "ERREUR: Wi-Fi non disponible", 0.55f, COL_ERROR);
        draw_text_centered(125, 400, "Voir sdmc:/3ds/1/debug.log", 0.44f, COL_GRAY);
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

    /* ── Boucle principale ─────────────────────────────────────────────────── */
    while (aptMainLoop()) {
        hidScanInput();
        u32 keys = hidKeysDown();

        float dt = get_dt();
        stars_update(dt);

        /* Input */
        switch (s_state.screen) {
        case SCREEN_MENU:
            if (keys & KEY_A) {
                /* Applique le thème selon la sélection */
                if (s_state.selection == 0) theme_set_bigbang();
                else                         theme_set_supernova();
                s_state.screen = SCREEN_CONFIRM;
            } else if (keys & KEY_UP || keys & KEY_LEFT) {
                s_state.selection = 0;
                theme_set_bigbang();
            } else if (keys & KEY_DOWN || keys & KEY_RIGHT) {
                s_state.selection = 1;
                theme_set_supernova();
            } else if (keys & KEY_START) {
                goto shutdown;
            }
            break;

        case SCREEN_CONFIRM:
            if (keys & KEY_A) {
                s_state.screen    = SCREEN_PROGRESS;
                s_state.cancelled = false;
                snprintf(s_state.status_msg, sizeof(s_state.status_msg),
                         "Initialisation...");

                const char *url = s_state.selection == 0
                    ? URL_PATCH_BIGBANG : URL_PATCH_SUPERNOVA;

                char dest[256];
                snprintf(dest, sizeof(dest), "sdmc:/");

                dbg("URL: %s\n", url);
                dbg("Dest: %s (ZIP contient le chemin complet)\n", dest);

                s_state.install_success = patcher_install(
                    url, TEMP_ZIP_PATH, dest,
                    _on_status, _on_progress, NULL
                );

                if (!s_state.install_success &&
                    strcmp(s_state.status_msg, "Annule") == 0)
                    s_state.cancelled = true;

                dbg("install_success=%d cancelled=%d\n",
                    s_state.install_success, s_state.cancelled);

                s_state.screen = SCREEN_RESULT;
            } else if (keys & KEY_B) {
                s_state.screen = SCREEN_MENU;
            }
            break;

        case SCREEN_PROGRESS:
            /* Géré dans le callback _on_progress */
            break;

        case SCREEN_RESULT:
            if (keys & KEY_A) {
                s_state.screen = SCREEN_MENU;
                s_state.progress = 0;
                s_state.retries  = 0;
                s_state.speed_kbs = 0;
            } else if (keys & KEY_START && !s_state.cancelled) {
                goto shutdown;
            }
            break;
        }

        /* ── Rendu ─────────────────────────────────────────────────────────── */
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        /* Écran haut */
        C2D_TargetClear(s_top, g_theme.bg_top);
        C2D_SceneBegin(s_top);
        stars_draw_top();

        switch (s_state.screen) {
        case SCREEN_MENU:     _draw_menu_screen();     break;
        case SCREEN_CONFIRM:  _draw_confirm_screen();  break;
        case SCREEN_PROGRESS: _draw_progress_screen(); break;
        case SCREEN_RESULT:   _draw_result_screen();   break;
        }

        /* Écran bas — fond + étoiles + console debug */
        C2D_TargetClear(s_bot, g_theme.bg_bot);
        C2D_SceneBegin(s_bot);
        stars_draw_bottom();

        C3D_FrameEnd(0);
        gspWaitForVBlank();
    }

shutdown:
    dbg("=== Fin ===\n");
    log_close();
    aptSetSleepAllowed(true);
    network_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}