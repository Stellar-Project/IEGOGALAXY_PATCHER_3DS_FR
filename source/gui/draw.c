#include "gui/draw.h"
#include "gui/theme.h"
#include <string.h>
#include <stdio.h>

void draw_panel(float x, float y, float w, float h, u32 col)
{
    C2D_DrawRectSolid(x, y, 0, w, h, col);
}

void draw_separator(float y, float screen_w, u32 col)
{
    C2D_DrawRectSolid(20, y, 0, screen_w - 40, 1, col);
}

void draw_text(float x, float y, const char *text, float size, u32 col)
{
    C2D_Text t;
    C2D_TextBuf buf = C2D_TextBufNew(512);
    C2D_TextParse(&t, buf, text);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0, size, size, col);
    C2D_TextBufDelete(buf);
}

void draw_text_centered(float y, float screen_w, const char *text,
                        float size, u32 col)
{
    C2D_Text t;
    C2D_TextBuf buf = C2D_TextBufNew(512);
    C2D_TextParse(&t, buf, text);
    C2D_TextOptimize(&t);

    float tw, th;
    C2D_TextGetDimensions(&t, size, size, &tw, &th);
    float x = (screen_w - tw) * 0.5f;
    C2D_DrawText(&t, C2D_WithColor, x, y, 0, size, size, col);
    C2D_TextBufDelete(buf);
}

void draw_progress_bar(float x, float y, float w, float h,
                       float pct, u32 col_bg, u32 col_fill, u32 col_border)
{
    /* Fond */
    C2D_DrawRectSolid(x, y, 0, w, h, col_bg);

    /* Remplissage */
    if (pct > 0.0f && pct <= 100.0f) {
        float fill_w = w * (pct / 100.0f);
        C2D_DrawRectSolid(x, y, 0, fill_w, h, col_fill);
    }

    /* Bordure — 4 lignes de 1px */
    C2D_DrawRectSolid(x,         y,         0, w,  1, col_border);
    C2D_DrawRectSolid(x,         y + h - 1, 0, w,  1, col_border);
    C2D_DrawRectSolid(x,         y,         0, 1,  h, col_border);
    C2D_DrawRectSolid(x + w - 1, y,         0, 1,  h, col_border);
}

void draw_button(float x, float y, float w, float h,
                 const char *label, u32 col_bg, u32 col_text, bool selected)
{
    /* Fond */
    C2D_DrawRectSolid(x, y, 0, w, h, col_bg);

    /* Bordure plus épaisse si sélectionné */
    u32 border = selected ? g_theme.accent2 : g_theme.accent;
    int bw = selected ? 2 : 1;

    C2D_DrawRectSolid(x,         y,         0, w,   bw,  border);
    C2D_DrawRectSolid(x,         y + h - bw,0, w,   bw,  border);
    C2D_DrawRectSolid(x,         y,         0, bw,  h,   border);
    C2D_DrawRectSolid(x + w - bw,y,         0, bw,  h,   border);

    /* Indicateur sélection — ligne accent à gauche */
    if (selected)
        C2D_DrawRectSolid(x, y + 4, 0, 4, h - 8, g_theme.highlight);

    /* Label centré */
    C2D_Text t;
    C2D_TextBuf buf = C2D_TextBufNew(256);
    C2D_TextParse(&t, buf, label);
    C2D_TextOptimize(&t);
    float tw, th;
    float sz = 0.55f;
    C2D_TextGetDimensions(&t, sz, sz, &tw, &th);
    float tx = x + (w - tw) * 0.5f;
    float ty = y + (h - th) * 0.5f;
    C2D_DrawText(&t, C2D_WithColor, tx, ty, 0, sz, sz, col_text);
    C2D_TextBufDelete(buf);
}