#include "gui/stars.h"
#include "gui/theme.h"
#include <citro2d.h>
#include <stdlib.h>
#include <math.h>

#define TOP_W  400.0f
#define TOP_H  240.0f
#define BOT_W  320.0f

static Star s_stars[STAR_COUNT];

/* Pseudo-random léger */
static float randf(void)
{
    static u32 seed = 12345;
    seed = seed * 1664525u + 1013904223u;
    return (float)(seed & 0xFFFF) / 65535.0f;
}

void stars_init(void)
{
    for (int i = 0; i < STAR_COUNT; i++) {
        s_stars[i].x            = randf() * TOP_W;
        s_stars[i].y            = randf() * TOP_H;
        s_stars[i].vx           = (randf() - 0.5f) * 8.0f;
        s_stars[i].vy           = (randf() - 0.5f) * 8.0f;
        s_stars[i].size         = 1.0f + randf() * 2.0f;
        s_stars[i].alpha        = 0.4f + randf() * 0.6f;
        s_stars[i].twinkle      = randf() * 6.28f;
        s_stars[i].twinkle_speed = 0.5f + randf() * 2.0f;
    }
}

void stars_update(float dt)
{
    for (int i = 0; i < STAR_COUNT; i++) {
        Star *s = &s_stars[i];

        /* Déplacement */
        s->x += s->vx * dt;
        s->y += s->vy * dt;

        /* Scintillement */
        s->twinkle += s->twinkle_speed * dt;
        if (s->twinkle > 6.28f) s->twinkle -= 6.28f;

        /* Wrap autour de l'écran */
        if (s->x < -5.0f)     s->x = TOP_W + 5.0f;
        if (s->x > TOP_W + 5) s->x = -5.0f;
        if (s->y < -5.0f)     s->y = TOP_H + 5.0f;
        if (s->y > TOP_H + 5) s->y = -5.0f;
    }
}

static void _draw_stars(float screen_w)
{
    for (int i = 0; i < STAR_COUNT; i++) {
        Star *s = &s_stars[i];

        /* Scintillement : alpha varie entre 30% et 100% */
        float twinkle_alpha = 0.3f + 0.7f * ((sinf(s->twinkle) + 1.0f) * 0.5f);
        u8 a = (u8)(s->alpha * twinkle_alpha * 255.0f);

        /* Couleur depuis le thème */
        u32 base = g_theme.star_color;
        u8 r = (base >> 24) & 0xFF;
        u8 g_c = (base >> 16) & 0xFF;
        u8 b = (base >> 8)  & 0xFF;
        u32 col = C2D_Color32(r, g_c, b, a);

        float x = s->x * (screen_w / TOP_W);
        float sz = s->size;

        /* Étoile à 4 branches (2 rectangles croisés) */
        C2D_DrawRectSolid(x - sz * 2, s->y - sz * 0.4f,
                          0, sz * 4, sz * 0.8f, col);
        C2D_DrawRectSolid(x - sz * 0.4f, s->y - sz * 2,
                          0, sz * 0.8f, sz * 4, col);
        /* Point central plus brillant */
        u8 a2 = (u8)fminf(255.0f, a * 1.5f);
        u32 col2 = C2D_Color32(r, g_c, b, a2);
        C2D_DrawRectSolid(x - sz * 0.6f, s->y - sz * 0.6f,
                          0, sz * 1.2f, sz * 1.2f, col2);
    }
}

void stars_draw_top(void)   { _draw_stars(TOP_W); }
void stars_draw_bottom(void) { _draw_stars(BOT_W); }