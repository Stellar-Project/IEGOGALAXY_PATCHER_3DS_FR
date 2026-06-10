#pragma once
#include <3ds.h>
#include <citro2d.h>

/* Barre de progression */
void draw_progress_bar(float x, float y, float w, float h,
                       float pct, u32 col_bg, u32 col_fill, u32 col_border);

/* Bouton stylisé */
void draw_button(float x, float y, float w, float h,
                 const char *label, u32 col_bg, u32 col_text,
                 bool selected);

/* Panneau semi-transparent */
void draw_panel(float x, float y, float w, float h, u32 col);

/* Texte centré */
void draw_text_centered(float y, float screen_w, const char *text,
                        float size, u32 col);

/* Texte aligné gauche */
void draw_text(float x, float y, const char *text, float size, u32 col);

/* Ligne séparatrice */
void draw_separator(float y, float screen_w, u32 col);