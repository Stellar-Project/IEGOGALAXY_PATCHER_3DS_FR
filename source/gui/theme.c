#include "gui/theme.h"

Theme g_theme;

void theme_set_bigbang(void)
{
    g_theme.bg_top     = BB_BG_TOP;
    g_theme.bg_bot     = BB_BG_BOT;
    g_theme.accent     = BB_ACCENT;
    g_theme.accent2    = BB_ACCENT2;
    g_theme.highlight  = BB_RED;
    g_theme.star_color = BB_STAR;
    g_theme.name       = "Big Bang";
}

void theme_set_supernova(void)
{
    g_theme.bg_top     = SN_BG_TOP;
    g_theme.bg_bot     = SN_BG_BOT;
    g_theme.accent     = SN_ACCENT;
    g_theme.accent2    = SN_ACCENT2;
    g_theme.highlight  = SN_BLUE;
    g_theme.star_color = SN_STAR;
    g_theme.name       = "Supernova";
}