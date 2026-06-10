#pragma once
#include <3ds.h>

#define STAR_COUNT 80

typedef struct {
    float x, y;       /* position */
    float vx, vy;     /* vitesse */
    float size;       /* taille 1-3 */
    float alpha;      /* transparence */
    float twinkle;    /* phase de scintillement */
    float twinkle_speed;
} Star;

void stars_init(void);
void stars_update(float dt);
void stars_draw_top(void);
void stars_draw_bottom(void);