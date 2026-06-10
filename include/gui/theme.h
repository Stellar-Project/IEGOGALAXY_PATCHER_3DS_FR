#pragma once
#include <3ds.h>
#include <citro2d.h>

/* ── Couleurs ─────────────────────────────────────────────────────────────── */

/* Big Bang — bleu électrique / rouge Tsurugi */
#define BB_BG_TOP       C2D_Color32(0x0A, 0x16, 0x28, 0xFF)
#define BB_BG_BOT       C2D_Color32(0x06, 0x0E, 0x1A, 0xFF)
#define BB_ACCENT       C2D_Color32(0x1E, 0x6F, 0xD9, 0xFF)
#define BB_ACCENT2      C2D_Color32(0x4F, 0xC3, 0xF7, 0xFF)
#define BB_RED          C2D_Color32(0xFF, 0x44, 0x44, 0xFF)
#define BB_STAR         C2D_Color32(0x4F, 0xC3, 0xF7, 0xCC)

/* Supernova — violet / bleu glacé Fubuki */
#define SN_BG_TOP       C2D_Color32(0x0D, 0x0A, 0x1E, 0xFF)
#define SN_BG_BOT       C2D_Color32(0x08, 0x06, 0x14, 0xFF)
#define SN_ACCENT       C2D_Color32(0x6A, 0x1F, 0xD9, 0xFF)
#define SN_ACCENT2      C2D_Color32(0xB0, 0x88, 0xFF, 0xFF)
#define SN_BLUE         C2D_Color32(0x88, 0xCC, 0xFF, 0xFF)
#define SN_STAR         C2D_Color32(0xB0, 0x88, 0xFF, 0xCC)

/* Commun */
#define COL_WHITE       C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF)
#define COL_GRAY        C2D_Color32(0xAA, 0xAA, 0xCC, 0xFF)
#define COL_DARK        C2D_Color32(0x22, 0x22, 0x44, 0xFF)
#define COL_BLACK       C2D_Color32(0x00, 0x00, 0x00, 0xFF)
#define COL_SUCCESS     C2D_Color32(0x44, 0xFF, 0x88, 0xFF)
#define COL_ERROR       C2D_Color32(0xFF, 0x44, 0x44, 0xFF)
#define COL_TRANSPARENT C2D_Color32(0x00, 0x00, 0x00, 0x00)

/* ── Thème actif ──────────────────────────────────────────────────────────── */

typedef struct {
    u32 bg_top;
    u32 bg_bot;
    u32 accent;
    u32 accent2;
    u32 highlight;
    u32 star_color;
    const char *name;
} Theme;

extern Theme g_theme;

void theme_set_bigbang(void);
void theme_set_supernova(void);