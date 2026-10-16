/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4 app: what is saved (the project: its settings, its four sequencers as they are, their 64 patterns,
 * its songs) and the app's entry points. Included by the unity build after gfx.c (drawing helpers in
 * scope) and by the host simulator. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "plat.h"
#include "../seq/seq4.h"

#ifndef EN_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define EN_VERSION "DEV"
#endif
#define PROJ_MAGIC 0x50345153u           /* "SQ4P" */
#define PROJ_FORMAT 1u

/* the log's keys (logstore.c): a pattern each, the sequencers as they are, the project, its songs */
#define KEY_PAT(s, p) ((s) * S4_NPAT + (p))
#define KEY_WORK(s) (64 + (s))
#define KEY_PROJ 68
#define KEY_SONGS 69

/* the project (KEY_PROJ) */
typedef struct {
    uint32_t magic, format;
    s4_glob_t g;
    uint8_t pat[S4_NSEQ];                /* the pattern each sequencer has loaded */
    uint8_t sel;                         /* the sequencer on screen */
    uint8_t song;                        /* the song selected */
    uint8_t trk[S4_NSEQ];                /* the track selected (Multitrack, Matriceal) */
    int8_t koct[S4_NSEQ];                /* the keyboard's octave */
    int8_t row[S4_NSEQ];                 /* the grid's lowest row (a degree) */
    uint8_t page[S4_NSEQ];
    uint8_t kvel;                        /* the keyboard's velocity */
    uint8_t rsv[23];
} proj_t;

extern proj_t proj;

void project_demo(void);                 /* the factory project: four sequencers with something in them */
int project_load(void);                  /* 0: loaded; else the demo */
int project_save_all(void);              /* the project, the sequencers as they are (not their patterns) */
int project_save_pattern(int s, int pat);   /* s4[s] into its slot (and as it is): 0 ok */
int project_load_pattern(int s, int pat);   /* a slot into the staging buffer, queued: 0 ok, 1 empty */
int project_has_pattern(int s, int pat);
int project_clear_pattern(int s, int pat);
int project_copy_pattern(int s, int from, int to_s, int to);
void project_dirty_seq(int s);
void project_dirty_proj(void);
int project_is_dirty(void);
int project_autosave(int stopped);       /* the changes, when it is a good time: 0 nothing to do */
void project_service(void);              /* the arranger's and program changes' requests, the flash's tidying */

void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
int ui_dirty(void);
void ui_say(const char *a, const char *b);
