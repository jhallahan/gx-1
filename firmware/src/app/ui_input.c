/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ-4 UI: the panel's keys, buttons and knobs, step recording and the lights. Part of ui.c. */

static void open_menu(int m)
{
    ui.menu = (uint8_t)m;
    ui.page = 0;
    ui.menu_undo = 0;
    ui.help = 0;
    ui.learn_in = 0;
}
static void close_menu(void)
{
    ui.menu = MN_NONE;
    ui.learn_in = 0;
}

/* ---------------------------------------------------------------------------- the keyboard --- */
static const uint8_t ROLL_DV[8] = {0, DV_4, DV_6, DV_8, DV_12, DV_16, DV_24, DV_32};
static int key_note(int k) { return 53 + k + 12 * proj.koct[sel()]; }   /* key 0 is F3 */
static void chord_off(void)
{
    int i;
    for (i = 0; i < ui.kc_n; i++)
        s4_live(sel(), 0, ui.kc_notes[i], 0);
    ui.kc_n = 0;
    ui.kc_key = 0;
    s4_kchord.vel = 0;
}
static void keys_off(void)                       /* every key the keyboard is sounding let go */
{
    int k;
    for (k = 0; k < NKEYS; k++)
        if (ui.key_note[k]) {
            s4_live(ui.key_seq[k], ui.key_trk[k], ui.key_note[k] - 1, 0);
            ui.key_note[k] = 0;
        }
    chord_off();
    if (ui.roll) {
        s4_roll(sel(), 0);
        ui.roll = 0;
    }
}
static void chord_play(int k, int deg)           /* the chord keyboard: a chord on this degree */
{
    s4_cstep_t *c = &s4_kchord;
    uint8_t n[16];
    int i, cnt;
    chord_off();
    cstep_init(c, deg);
    c->type = (uint8_t)clampi(ui.kb_type, 0, s4_chord_types(sel(), deg) - 1);
    c->voi = (uint8_t)ui.kb_voi;
    c->spread = (uint8_t)ui.kb_spread;
    c->vel = (uint8_t)(proj.kvel ? proj.kvel : 100);
    cnt = s4_chord_notes(sel(), c, n);
    for (i = 0; i < cnt && i < 8; i++) {
        ui.kc_notes[i] = n[i];
        s4_live(sel(), 0, n[i], c->vel);
    }
    ui.kc_n = (uint8_t)(cnt < 8 ? cnt : 8);
    ui.kc_key = (uint8_t)(k + 1);
    ui.hand_chord = *c;
    ui.hand_chord_ok = 1;
    {
        char b[24];
        say(s4_chord_name(sel(), c, b), 0);
    }
}
static void keys_down(int k)
{
    int s = sel(), w = white_of(k), bk = black_of(k);
    int vel = proj.kvel ? proj.kvel : 100;
    if (mode() == M_CHORD) {
        if (w >= 0) {
            chord_play(k, w + 7 * proj.koct[s]);
            return;
        }
        switch (bk) {                            /* the black keys: what the chords are */
        case 0: ui.kb_type = (int8_t)(ui.kb_type > 0 ? ui.kb_type - 1 : 0); break;
        case 1: ui.kb_type = (int8_t)(ui.kb_type + 1 < 24 ? ui.kb_type + 1 : 23); break;
        case 2: ui.kb_voi = (int8_t)((ui.kb_voi + 1) % 6); break;
        case 3: ui.kb_spread = (int8_t)((ui.kb_spread + 1) % 8); break;
        case 4: sq()->ch_bass ^= 1u; dirty(); break;
        case 5: sq()->ch_bank ^= 1u; dirty(); break;
        case 6: proj.koct[s] = (int8_t)clampi(proj.koct[s] - 1, -3, 3); dirty_g(); break;
        case 7: proj.koct[s] = (int8_t)clampi(proj.koct[s] + 1, -3, 3); dirty_g(); break;
        default: return;
        }
        if (bk <= 3 && ui.kc_key) {              /* the chord held: again, as it is now */
            int key = ui.kc_key - 1;
            chord_play(key, s4_kchord.deg);
        } else {
            static const char *const W[8] = {"TYPE", "TYPE", "VOICING", "SPREAD", "BASS", "BANK", "OCTAVE", "OCTAVE"};
            char t[12];
            switch (bk) {
            case 2: say(W[bk], VOI_N[ui.kb_voi]); break;
            case 3: say(W[bk], itoa_u((uint32_t)ui.kb_spread, t)); break;
            case 4: say(W[bk], sq()->ch_bass ? "On" : "Off"); break;
            case 5: say(W[bk], sq()->ch_bank ? "B" : "A"); break;
            case 6:
            case 7: say(W[bk], itoa_s(proj.koct[s], t)); break;
            default: say(W[bk], itoa_u((uint32_t)ui.kb_type + 1u, t)); break;
            }
        }
        return;
    }
    if (mode() == M_MULTI) {                     /* the drum pads: whites 1-8 the tracks, 9-16 accented */
        int t;
        if (w < 0) {
            if (bk >= 0 && bk < 8)
                set_trk(bk);
            return;
        }
        t = w & 7;
        if (w >= 8)
            vel = 127;
        ui.key_note[k] = (uint8_t)(sq()->d.multi.t[t].note + 1);
        ui.key_seq[k] = (uint8_t)s;
        ui.key_trk[k] = (uint8_t)t;
        s4_live(s, t, sq()->d.multi.t[t].note, vel);
        return;
    }
    ui.key_note[k] = (uint8_t)(key_note(k) + 1);
    ui.key_seq[k] = (uint8_t)s;
    ui.key_trk[k] = (uint8_t)trk();
    s4_live(s, trk(), key_note(k), vel);
}
static void keys_up(int k)
{
    if (ui.key_note[k]) {
        s4_live(ui.key_seq[k], ui.key_trk[k], ui.key_note[k] - 1, 0);
        ui.key_note[k] = 0;
    }
    if (ui.kc_key == k + 1)
        chord_off();
}

/* ------------------------------------------------------------------------ the selections --- */
static void set_trk(int t)
{
    int n = mode() == M_MATRIX ? S4_NMX : S4_NTRK;
    proj.trk[sel()] = (uint8_t)clampi(t, 0, n - 1);
    s4_rec_track(trk());
    dirty_g();
}
static void set_sel(int s)
{
    char t[8];
    s = clampi(s, 0, S4_NSEQ - 1);
    if (s == sel())
        return;
    if (ui.view == V_KEYS)
        keys_off();
    proj.sel = (uint8_t)s;
    s4_select(s);
    s4_rec_track(trk());
    dirty_g();
    if (ui.menu == MN_TRACK && mode() != M_MULTI)
        close_menu();
    ui.held = 0;
    ui.first_abs = -1;
    scopy(t, "SEQ ", 8);
    t[4] = (char)('1' + s);
    t[5] = 0;
    say(t, MODE_N[mode()]);
}
static void set_view(int v)
{
    if (ui.view == V_KEYS && v != V_KEYS)
        keys_off();
    if (v != V_SONG && (ui.menu == MN_SLOT || ui.menu == MN_MASTER))
        close_menu();
    ui.view = (uint8_t)v;
    ui.help = 0;
}
static int last_page(void)                       /* the sequence's last page */
{
    int e = mode() == M_MULTI ? sq()->d.multi.t[trk()].end : sq()->end, i0 = mode() == M_MULTI ? sq()->d.multi.t[trk()].init : sq()->init;
    if (mode() == M_MATRIX)
        return 0;
    return (e > i0 ? e : i0) / 16;
}
static void set_page(int p)
{
    p = clampi(p, 0, mode() == M_MATRIX || mode() == M_STOCH ? 0 : S4_NPAGE - 1);
    proj.page[sel()] = (uint8_t)p;
}

/* ------------------------------------------------------------------------ step recording --- */
static int srec_lo(void) { return mode() == M_MULTI ? sq()->d.multi.t[trk()].init : sq()->init; }
static int srec_hi(void) { return mode() == M_MULTI ? sq()->d.multi.t[trk()].end : sq()->end; }
static void srec_advance(void)
{
    int lo = srec_lo(), hi = srec_hi(), c = ui.cursor + 1;
    if (lo > hi) {
        int x = lo;
        lo = hi;
        hi = x;
    }
    if (c > hi || c < lo)
        c = lo;
    ui.cursor = (uint8_t)c;
    set_page(c / 16);
}
static void srec_note(int note, int vel)         /* a note played, stopped, step recording: at the cursor */
{
    int s = sel(), i = ui.cursor, first = !ui.srec_any;
    if (first)
        undo();
    switch (mode()) {
    case M_MONO: {
        s4_step_t *st = nstep(s, 0, i);
        step_init(st, s4_deg_of(s, note));
        st->vel = (uint8_t)vel;
        break;
    }
    case M_MULTI: {
        int t = trk(), k;
        s4_step_t *st;
        for (k = 0; k < S4_NTRK; k++)
            if (sq()->d.multi.t[k].note == note)
                t = k;
        st = nstep(s, t, i);
        step_init(st, note - sq()->d.multi.t[t].note);
        st->vel = (uint8_t)vel;
        break;
    }
    case M_POLY: {
        s4_pnote_t p;
        if (first)
            poly_clear_step(s, i);
        pnote_init(&p, i, s4_deg_of(s, note));
        p.vel = (uint8_t)vel;
        if (s4_poly_find(s, i, p.deg) < 0)
            s4_poly_add(s, &p);
        break;
    }
    case M_CHORD: {
        s4_cstep_t *c = &sq()->d.chord[i];
        if (!first)
            break;
        if (s4_kchord.vel || ui.kc_key) {
            *c = ui.hand_chord;
            c->ofs = 0;
            c->cond = 100;
            c->rtg = 0;
            c->gate = (uint8_t)(sq()->gdef ? sq()->gdef : 90);
        } else
            cstep_init(c, s4_deg_of(s, note));
        c->vel = (uint8_t)vel;
        break;
    }
    default:
        return;
    }
    ui.srec_any = 1;
    dirty();
}
static void srec_tie(void)                       /* SEL: the last step held on over this one */
{
    int s = sel(), i = ui.cursor, p = i > 0 ? i - 1 : srec_hi();
    undo();
    switch (mode()) {
    case M_MONO:
    case M_MULTI: {
        s4_step_t *a = nstep(s, trk(), p);
        if (a->vel) {
            *nstep(s, trk(), i) = *a;
            a->gate = S4_TIE;
        }
        break;
    }
    case M_CHORD:
        if (sq()->d.chord[p].vel) {
            sq()->d.chord[i] = sq()->d.chord[p];
            sq()->d.chord[p].gate = S4_TIE;
        }
        break;
    case M_POLY: {
        int k, n = sq()->d.poly.n;
        for (k = 0; k < n; k++)
            if (sq()->d.poly.note[k].step == p) {
                s4_pnote_t q = sq()->d.poly.note[k];
                sq()->d.poly.note[k].gate = S4_TIE;
                q.step = (uint8_t)i;
                if (s4_poly_find(s, i, q.deg) < 0)
                    s4_poly_add(s, &q);
            }
        break;
    }
    default:
        break;
    }
    dirty();
    srec_advance();
    say("TIE", 0);
}
static void srec_rest(void)
{
    int s = sel(), i = ui.cursor;
    undo();
    switch (mode()) {
    case M_MONO:
    case M_MULTI: nstep(s, trk(), i)->vel = 0; break;
    case M_CHORD: sq()->d.chord[i].vel = 0; break;
    case M_POLY: poly_clear_step(s, i); break;
    default: break;
    }
    dirty();
    srec_advance();
    say("REST", 0);
}

/* the notes played (the keyboard's and MIDI's): the hand pick-and-place uses, and step recording */
static void notes_in(void)
{
    uint32_t n = s4_cap_total(), now = plat_ms();
    uint8_t note, vel, t;
    if (ui.cap_rd > n)                           /* (a capture emptied the list) */
        ui.cap_rd = n;
    if (n - ui.cap_rd > 255u)
        ui.cap_rd = n - 255u;
    for (; ui.cap_rd < n; ui.cap_rd++) {
        if (!s4_cap_get(ui.cap_rd, &note, &vel, &t))
            continue;
        if (now - ui.hand_t > 90u) {             /* a new hand */
            ui.nhand = 0;
            if (ui.srec_any)
                srec_advance(), ui.srec_any = 0;
        }
        if (ui.nhand < 8)
            ui.hand[ui.nhand++] = note;
        ui.hand_t = now;
        if (mode() != M_CHORD || !ui.kc_key) {
            ui.hand_chord_ok = 0;
            if (mode() == M_CHORD && ui.nhand == 1) {
                cstep_init(&ui.hand_chord, s4_deg_of(sel(), note));
                ui.hand_chord_ok = 1;
            }
        }
        if (ui.srec && !s4p.on)
            srec_note(note, vel);
    }
    if (ui.srec_any && now - ui.hand_t > 140u && !ui.keys) {
        ui.srec_any = 0;
        srec_advance();
    }
}

/* ---------------------------------------------------------------------------- the clipboard --- */
static void clip_copy_step(int i)
{
    int s = sel(), t = trk(), k;
    clip_t *c = &ui.clip;
    c->kind = CK_STEPS;
    c->s = (uint8_t)s;
    c->t = (uint8_t)t;
    c->mode = (uint8_t)mode();
    c->n = 0;
    switch (mode()) {
    case M_MONO:
    case M_MULTI: c->st[0] = *nstep(s, t, i); c->n = 1; break;
    case M_CHORD: c->cs[0] = sq()->d.chord[i]; c->n = 1; break;
    case M_POLY:
        for (k = 0; k < sq()->d.poly.n && c->n < 16; k++)
            if (sq()->d.poly.note[k].step == i) {
                const s4_pnote_t *p = &sq()->d.poly.note[k];
                s4_step_t *x = &c->st[c->n++];
                x->deg = p->deg;
                x->vel = p->vel;
                x->gate = p->gate;
                x->oct = p->oct;
                x->cond = p->cond;
                x->rtg = p->rtg;
                x->ofs = p->ofs;
                x->glide = 0;
            }
        break;
    default: c->kind = CK_NONE; return;
    }
    c->mod[0] = sq()->mod[lane_of_knob()][i];
    say("COPIED", "STEP");
}
static void clip_paste_step(int i)
{
    int s = sel(), t = trk(), k;
    clip_t *c = &ui.clip;
    if (c->kind != CK_STEPS || c->mode != mode())
        return;
    undo();
    switch (mode()) {
    case M_MONO:
    case M_MULTI: *nstep(s, t, i) = c->st[0]; break;
    case M_CHORD: sq()->d.chord[i] = c->cs[0]; break;
    case M_POLY:
        poly_clear_step(s, i);
        for (k = 0; k < c->n; k++) {
            s4_pnote_t p;
            pnote_init(&p, i, c->st[k].deg);
            p.vel = c->st[k].vel;
            p.gate = c->st[k].gate;
            p.oct = c->st[k].oct;
            p.cond = c->st[k].cond;
            p.rtg = c->st[k].rtg;
            p.ofs = c->st[k].ofs;
            s4_poly_add(s, &p);
        }
        break;
    default: break;
    }
    sq()->mod[lane_of_knob()][i] = c->mod[0];
    dirty();
    say("PASTED", 0);
}

/* ------------------------------------------------------------------------------ mutes --- */
static void mute_seq(int s, int solo)
{
    int k;
    if (solo) {
        for (k = 0; k < S4_NSEQ; k++)
            s4_mute(k, k != s);
        say("SOLO", 0);
        return;
    }
    s4_mute(s, !s4[s].mute);
}
static void mute_trk(int t, int solo)
{
    s4_trk_t *x = sq()->d.multi.t;
    int k;
    if (mode() != M_MULTI)
        return;
    if (solo) {
        int was = x[t].solo;
        for (k = 0; k < S4_NTRK; k++)
            x[k].solo = 0;
        x[t].solo = (uint8_t)!was;
    } else
        x[t].mute ^= 1u;
    dirty();
}
static void step_mute(void)                      /* a held step + MUTE: the step muted (kept) */
{
    int steps[16], n = held_steps(steps), j, k, s = sel();
    undo();
    for (j = 0; j < n; j++) {
        int i = steps[j];
        switch (mode()) {
        case M_MONO:
        case M_MULTI: nstep(s, trk(), i)->rtg ^= S4_MUTED; break;
        case M_CHORD: sq()->d.chord[i].rtg ^= S4_MUTED; break;
        case M_POLY:
            for (k = 0; k < sq()->d.poly.n; k++)
                if (sq()->d.poly.note[k].step == i)
                    sq()->d.poly.note[k].rtg ^= S4_MUTED;
            break;
        default: break;
        }
    }
    ui.held_used |= ui.held;
    dirty();
    say("STEP MUTE", 0);
}

/* ---------------------------------------------------------------------- the grid's keys --- */
static void grid_white_down(int w)
{
    int i = step_of(w), s = sel();
    if (ui.srec && !s4p.on) {                    /* step recording: the cursor here */
        ui.cursor = (uint8_t)i;
        return;
    }
    if (btn_held(B_SEL)) {                       /* SEL + steps: the range (one: its end; two: start, end) */
        uint8_t *in = mode() == M_MULTI ? &sq()->d.multi.t[trk()].init : &sq()->init;
        uint8_t *en = mode() == M_MULTI ? &sq()->d.multi.t[trk()].end : &sq()->end;
        char t[12];
        ui.btn_used |= 1u << B_SEL;
        if (mode() == M_MATRIX) {
            s4_lane_t *ln = &sq()->d.mx[trk() & 3].lane[ui.mx_lane];
            if (!ui.sel_first) {
                ui.sel_first = (uint8_t)(w + 1);
                ln->end = (uint8_t)w;
            } else {
                ln->init = (uint8_t)(ui.sel_first - 1 < w ? ui.sel_first - 1 : w);
                ln->end = (uint8_t)(ui.sel_first - 1 < w ? w : ui.sel_first - 1);
            }
            dirty();
            return;
        }
        if (!ui.sel_first) {
            ui.sel_first = (uint8_t)(i + 1);
            *en = (uint8_t)i;
            if (*in > i)
                *in = 0;
            itoa_u((uint32_t)(i - *in + 1), t);
            say("LENGTH", t);
        } else {
            int a = ui.sel_first - 1;
            *in = (uint8_t)(a < i ? a : i);
            *en = (uint8_t)(a < i ? i : a);
            itoa_u((uint32_t)*in + 1u, t);
            scat(t, "-", 12);
            {
                char u[6];
                scat(t, itoa_u((uint32_t)*en + 1u, u), 12);
            }
            say("RANGE", t);
        }
        dirty();
        return;
    }
    if (btn_held(B_EDIT)) {                      /* EDIT + steps: the first copied, the next pasted */
        ui.btn_used |= 1u << B_EDIT;
        if (!ui.edit_taken) {
            clip_copy_step(i);
            ui.edit_taken = 1;
        } else
            clip_paste_step(i);
        return;
    }
    if (muting()) {                              /* MUTE + whites: 1-4 the sequencers, 5-12 the tracks */
        ui.keys_used |= 1u << BLACK_KEY[BK_MUTE];
        if (w < 4)
            mute_seq(w, shift());
        else if (w < 12)
            mute_trk(w - 4, shift());
        return;
    }
    if (ui.row >= 0) {                           /* a row held: drawn on it */
        row_step(ui.row, i);
        ui.row_used = 1;
        return;
    }
    if (ui.first_abs >= 0 && ui.held && (mode() <= M_MULTI)) {   /* another step held: a tie */
        tie_steps(ui.first_abs, i);
        ui.held |= (uint16_t)(1u << w);
        ui.held_used |= ui.held;
        return;
    }
    ui.held |= (uint16_t)(1u << w);
    ui.held_used &= (uint16_t)~(1u << w);
    ui.made_mask &= (uint16_t)~(1u << w);
    if (ui.first_abs < 0)
        ui.first_abs = (int16_t)i;
    if (mode() == M_STOCH) {
        ui.stoch_col = (uint8_t)w;
        return;
    }
    if (!step_on(s, trk(), i) && mode() != M_MATRIX) {   /* an empty step: on now (let go: kept) */
        tap_step(i);
        ui.made_mask |= (uint16_t)(1u << w);
    }
}
static void grid_white_up(int w)
{
    uint16_t m = (uint16_t)(1u << w);
    int i = step_of(w);
    if (!(ui.held & m))
        return;
    ui.held &= (uint16_t)~m;
    if (!(ui.held_used & m) && !(ui.made_mask & m) && mode() != M_STOCH)
        tap_step(i);                             /* a tap on a step that was on: off */
    ui.held_used &= (uint16_t)~m;
    ui.made_mask &= (uint16_t)~m;
    if (!ui.held)
        ui.first_abs = -1;
    else if (ui.first_abs == i) {
        int k;
        for (k = 0; k < 16; k++)
            if (ui.held >> k & 1u) {
                ui.first_abs = (int16_t)step_of(k);
                break;
            }
    }
}
static void preview(int r, int on)               /* a row's note heard (stopped) */
{
    int s = sel();
    static int8_t note = -1;
    if (note >= 0) {
        s4_live(s, trk(), note, 0);
        note = -1;
    }
    if (!on || s4p.on)
        return;
    if (mode() == M_MULTI) {
        note = (int8_t)sq()->d.multi.t[r & 7].note;
        s4_live(s, r & 7, note, 100);
    } else if (mode() == M_MONO || mode() == M_POLY) {
        note = (int8_t)s4_note_of(s, row_deg(r), 0);
        s4_live(s, 0, note, 100);
    }
}
static void grid_black_down(int b)
{
    int s = sel();
    if (b < 8) {
        if (ui.held && mode() != M_MATRIX) {     /* held steps + a row: their note (Multitrack: that track) */
            int steps[16], n = held_steps(steps), j;
            for (j = 0; j < n; j++) {
                if (mode() == M_MULTI) {
                    row_step(b, steps[j]);
                } else if (mode() == M_STOCH) {
                    row_step(b, steps[j]);
                } else {
                    int st = steps[j], deg = row_deg(b);
                    undo();
                    if (mode() == M_MONO) {
                        s4_step_t *x = nstep(s, 0, st);
                        if (!x->vel)
                            step_init(x, deg);
                        x->deg = (int8_t)deg;
                        x->oct = 0;
                    } else if (mode() == M_CHORD) {
                        s4_cstep_t *c = &sq()->d.chord[st];
                        if (!c->vel)
                            cstep_init(c, deg);
                        c->deg = (int8_t)deg;
                        c->oct = 0;
                    } else if (mode() == M_POLY) {
                        int k = s4_poly_find(s, st, deg);
                        if (k >= 0)
                            s4_poly_del(s, k);
                        else {
                            s4_pnote_t p;
                            pnote_init(&p, st, deg);
                            s4_poly_add(s, &p);
                        }
                    }
                    dirty();
                }
            }
            ui.held_used |= ui.held;
            ui.cur_deg[s] = (int8_t)row_deg(b);
            return;
        }
        if (muting() && mode() == M_MULTI) {
            ui.keys_used |= 1u << BLACK_KEY[BK_MUTE];
            mute_trk(b, shift());
            return;
        }
        ui.row = (int8_t)b;
        ui.row_used = 0;
        if (mode() == M_MULTI)
            set_trk(b);
        else if (mode() <= M_CHORD)
            ui.cur_deg[s] = (int8_t)row_deg(b);
        preview(b, 1);
        return;
    }
    if (b == BK_PREV || b == BK_NEXT) {
        int d = b == BK_NEXT ? 1 : -1;
        if (shift()) {                           /* GLO + < >: the sequence rotated */
            undo();
            s4_rotate(s, trk(), d);
            dirty();
            ui.btn_used |= 1u << B_GLO;
            say("ROTATE", d > 0 ? ">" : "<");
        } else if (btn_held(B_EDIT)) {           /* EDIT + >: the steps again after themselves */
            ui.btn_used |= 1u << B_EDIT;
            if (d > 0) {
                undo();
                s4_duplicate(s, trk());
                dirty();
                say("DUPLICATED", 0);
            }
        } else if (btn_held(B_SEL)) {            /* SEL + < >: half / twice the steps, the time the same */
            ui.btn_used |= 1u << B_SEL;
            undo();
            if (d > 0)
                s4_expand(s, trk());
            else
                s4_condense(s, trk());
            dirty();
            say(d > 0 ? "EXPANDED" : "CONDENSED", 0);
        } else
            set_page(page() + d);
        return;
    }
    /* MUTE: a held step's mute; GLO: the nudges; else (let go unused) the sequencer's */
    if (ui.held) {
        step_mute();
        ui.keys_used |= 1u << BLACK_KEY[BK_MUTE];
    } else if (shift()) {
        open_menu(MN_NUDGE);
        ui.btn_used |= 1u << B_GLO;
        ui.keys_used |= 1u << BLACK_KEY[BK_MUTE];
    }
}
static void grid_black_up(int b)
{
    if (b < 8 && ui.row == b) {
        ui.row = -1;
        preview(b, 0);
    }
    if (b == BK_MUTE && !(ui.keys_used >> BLACK_KEY[BK_MUTE] & 1u))
        mute_seq(sel(), 0);
}

/* the mod lanes' view: rows are levels */
static void mod_white_down(int w)
{
    int i = step_of(w), l = lane_of_knob();
    if (ui.row >= 0) {
        undo();
        sq()->mod[l][i] = (uint8_t)(ui.row * 127 / 7);
        sq()->ml[l].on = 1;
        ui.row_used = 1;
        dirty();
        return;
    }
    ui.held |= (uint16_t)(1u << w);
    ui.held_used |= (uint16_t)(1u << w);
}

/* the patterns' view */
static void pats_white_down(int w)
{
    int s = sel();
    char t[8];
    t[0] = 'P';
    itoa_u((uint32_t)w + 1u, t + 1);
    if (btn_held(B_SAVE)) {
        ui.btn_used |= 1u << B_SAVE;
        say(project_save_pattern(s, w) ? "NOT SAVED" : "SAVED", t);
        return;
    }
    if (btn_held(B_EDIT)) {
        ui.btn_used |= 1u << B_EDIT;
        if (!ui.edit_taken) {
            if (w == proj.pat[s] && (dirty_seq >> s & 1u))
                project_save_pattern(s, w);      /* (what is heard is what is copied) */
            ui.clip.kind = CK_PAT;
            ui.clip.s = (uint8_t)s;
            ui.clip.pat = (uint8_t)w;
            ui.edit_taken = 1;
            say("COPIED", t);
        } else if (ui.clip.kind == CK_PAT) {
            say(project_copy_pattern(ui.clip.s, ui.clip.pat, s, w) ? "EMPTY" : "PASTED", t);
        }
        return;
    }
    if (shift()) {                               /* GLO + a pattern: emptied */
        ui.btn_used |= 1u << B_GLO;
        project_clear_pattern(s, w);
        say("CLEARED", t);
        return;
    }
    say(t, project_load_pattern(s, w) ? "EMPTY" : s4p.on ? "NEXT" : "LOADED");
}

/* the arranger's view: whites the slots, blacks 1-4 the lanes, < > the songs */
static void song_white_down(int w)
{
    s4_song_t *g = &s4song[proj.song % S4_NSONG];
    if (w == 15) {
        open_menu(MN_MASTER);
        return;
    }
    if (shift()) {
        g->slot[ui.song_lane & 3][w].pat = S4_EMPTY;
        ui.btn_used |= 1u << B_GLO;
        project_dirty_songs();
        say("SLOT", "EMPTY");
        return;
    }
    if (btn_held(B_EDIT)) {
        ui.btn_used |= 1u << B_EDIT;
        if (!ui.edit_taken) {
            ui.clip.kind = CK_SLOT;
            ui.clip.slot = g->slot[ui.song_lane & 3][w];
            ui.edit_taken = 1;
            say("COPIED", "SLOT");
        } else if (ui.clip.kind == CK_SLOT) {
            g->slot[ui.song_lane & 3][w] = ui.clip.slot;
            project_dirty_songs();
            say("PASTED", "SLOT");
        }
        return;
    }
    ui.slot = (uint8_t)w;
    if (g->slot[ui.song_lane & 3][w].pat == S4_EMPTY) {   /* a new slot: the pattern loaded, once */
        g->slot[ui.song_lane & 3][w].pat = proj.pat[ui.song_lane & 3];
        g->slot[ui.song_lane & 3][w].rep = 1;
        g->slot[ui.song_lane & 3][w].pc = 0;
        g->slot[ui.song_lane & 3][w].bank = 0;
        project_dirty_songs();
    }
    open_menu(MN_SLOT);
}

static void key_down(int k)
{
    int w = white_of(k), b = black_of(k);
    if (ui.help)
        ui.help = 0;
    if (ui.view == V_KEYS) {
        keys_down(k);
        return;
    }
    if (ui.view == V_PATS) {
        if (w >= 0)
            pats_white_down(w);
        else if (b >= 0 && b < 4)
            set_sel(b);
        return;
    }
    if (ui.view == V_SONG) {
        if (w >= 0)
            song_white_down(w);
        else if (b >= 0 && b < 4)
            ui.song_lane = (uint8_t)b;
        else if (b == BK_PREV || b == BK_NEXT) {
            proj.song = (uint8_t)clampi(proj.song + (b == BK_NEXT ? 1 : -1), 0, S4_NSONG - 1);
            dirty_g();
        }
        return;
    }
    if (ui.view == V_MOD) {
        if (w >= 0)
            mod_white_down(w);
        else if (b >= 0 && b < 8) {
            ui.row = (int8_t)b;
            ui.row_used = 0;
        } else if (b == BK_PREV || b == BK_NEXT)
            set_page(page() + (b == BK_NEXT ? 1 : -1));
        return;
    }
    if (w >= 0)
        grid_white_down(w);
    else if (b >= 0)
        grid_black_down(b);
}
static void key_up(int k)
{
    int w = white_of(k), b = black_of(k);
    if (ui.view == V_KEYS) {
        keys_up(k);
        return;
    }
    if (ui.view == V_MOD) {
        if (w >= 0)
            ui.held &= (uint16_t)~(1u << w);
        if (b >= 0 && b < 8 && ui.row == b)
            ui.row = -1;
        return;
    }
    if (ui.view != V_GRID)
        return;
    if (w >= 0)
        grid_white_up(w);
    else if (b >= 0)
        grid_black_up(b);
}

/* --------------------------------------------------------------------------- the buttons --- */
static int is_gen_menu(int m) { return m == MN_RANDG || m == MN_EUCLID || m == MN_DRUMS; }
static void generate(void)                       /* FX on a generator's menu: it writes */
{
    int s = sel();
    switch (ui.menu) {
    case MN_EUCLID:
        undo();
        s4_euclid(s, trk());
        say("EUCLID", 0);
        break;
    case MN_DRUMS: {
        int t, on = !sq()->d.multi.t[0].dg_on;
        for (t = 0; t < 3; t++)
            sq()->d.multi.t[t].dg_on = (uint8_t)on;
        say("DRUM GENERATOR", on ? "On" : "Off");
        break;
    }
    default:
        s4_generate(s, mode() == M_MULTI ? (shift() ? -1 : trk()) : 0);
        say("GENERATED", 0);
        break;
    }
    dirty();
}
static void save_now(void)
{
    char t[16];
    int s = sel(), rc = project_save_pattern(s, proj.pat[s]);
    if (!rc)
        rc = project_save_all();
    scopy(t, "SEQ ", 16);
    t[4] = (char)('1' + s);
    t[5] = ' ';
    t[6] = 'P';
    itoa_u((uint32_t)proj.pat[s] + 1u, t + 7);
    say(rc ? (rc == -1 ? "FULL: STOP TO TIDY" : "NOT SAVED") : "SAVED", t);
}
static void snapshot(void)
{
    if (s4p.snap) {
        s4_snapshot(0);
        say("SNAPSHOT", "BACK AT THE BAR");
    } else {
        int s;
        for (s = 0; s < S4_NSEQ; s++)
            s4_snapbuf[s] = s4[s];
        s4_snapshot(1);
        say("SNAPSHOT", "TAKEN");
    }
}
static void play_down(void)
{
    if (shift()) {                               /* GLO + PLAY: tap tempo */
        s4_tap();
        ui.btn_used |= 1u << B_GLO | 1u << B_PLAY;
        return;
    }
    if (btn_held(B_HOME)) {                      /* HOME + PLAY: stopped, back to the top */
        s4_stop_reset();
        ui.btn_used |= 1u << B_HOME | 1u << B_PLAY;
        return;
    }
    if (btn_held(B_REC)) {                       /* REC + PLAY: recording from the start (the count-in) */
        s4_rec(1);
        ui.btn_used |= 1u << B_REC;
    }
    if (!s4p.on && !s4p.count) {
        ui.srec = 0;
        if (ui.view == V_SONG)
            s4_song(1);
        s4_play(1);
        ui.btn_used |= 1u << B_PLAY;             /* (let go: nothing more) */
    }
}
static void play_up(void)
{
    if (ui.fill) {
        s4_fill(0);
        ui.fill = 0;
        return;
    }
    if (ui.btn_used >> B_PLAY & 1u)
        return;
    if (s4p.on || s4p.count) {
        if (s4g.play_mode == 2 && s4p.on)
            s4_restart(-1);
        else {
            s4_play(0);
            if (s4p.song_on)
                s4_song(0);
        }
    }
}
static void btn_down(int b)
{
    int sh = shift();
    if (b != B_GLO && sh)
        ui.btn_used |= 1u << B_GLO;
    switch (b) {
    case B_PLAY: play_down(); break;
    case B_REC:
        if (sh) {
            open_menu(MN_REC);
            ui.btn_used |= 1u << b;
        } else if (btn_held(B_EDIT)) {           /* EDIT + REC: the track (the sequence) cleared */
            undo();
            s4_clear(sel(), mode() == M_MULTI ? trk() : -1, 0);
            dirty();
            ui.btn_used |= 1u << b | 1u << B_EDIT;
            say("CLEARED", mode() == M_MULTI ? "TRACK" : "SEQUENCE");
        }
        break;
    case B_HOME:
        if (sh) {
            set_view(ui.view == V_SONG ? V_GRID : V_SONG);
            ui.btn_used |= 1u << b;
        } else if (btn_held(B_SAVE)) {
            snapshot();
            ui.btn_used |= 1u << b | 1u << B_SAVE;
        }
        break;
    case B_SAVE:
        if (btn_held(B_HOME)) {
            snapshot();
            ui.btn_used |= 1u << b | 1u << B_HOME;
        }
        break;
    case B_SEQ:
        if (sh) {
            if (ui.menu == MN_SEQ && mode() == M_MULTI)
                open_menu(MN_TRACK);
            else if (ui.menu == MN_SEQ && mode() == M_MATRIX)
                open_menu(MN_LANE);
            else
                open_menu(MN_SEQ);
            ui.btn_used |= 1u << b;
        }
        break;
    case B_FX:
        if (sh) {                                /* GLO + FX: the generators, in turn */
            int m = ui.menu;
            if (mode() == M_MULTI)
                m = m == MN_EUCLID ? MN_DRUMS : m == MN_DRUMS ? MN_RANDG : MN_EUCLID;
            else
                m = MN_RANDG;
            open_menu(m);
            ui.btn_used |= 1u << b;
        }
        break;
    case B_LFO:
        if (sh) {
            open_menu(MN_MAP);
            ui.btn_used |= 1u << b;
        }
        break;
    case B_ENV:
        if (sh) {
            open_menu(MN_MODSET);
            ui.btn_used |= 1u << b;
        }
        break;
    case B_SEL:
        ui.sel_first = 0;
        if (ui.srec && !s4p.on) {
            srec_tie();
            ui.btn_used |= 1u << b;
        } else if (sh) {
            open_menu(MN_LOOP);
            ui.btn_used |= 1u << b;
        }
        break;
    case B_EDIT:
        ui.edit_taken = 0;
        if (ui.srec && !s4p.on) {
            srec_rest();
            ui.btn_used |= 1u << b;
        } else if (sh) {
            say(s4_redo() ? "REDO" : "NOTHING TO REDO", 0);
            ui.btn_used |= 1u << b;
        }
        break;
    case B_ARP:
        if (sh) {                                /* GLO + ARP: the keyboard's hold */
            sq()->karp.hold ^= 1u;
            if (!sq()->karp.hold)
                s4_arp_clear(sel());
            dirty();
            say("HOLD", sq()->karp.hold ? "On" : "Off");
            ui.btn_used |= 1u << b;
        }
        break;
    case B_OCTDN:
    case B_OCTUP: {
        int d = b == B_OCTUP ? 1 : -1;
        ui.btn_used |= 1u << b;
        if (ui.view == V_KEYS) {
            keys_off();
            proj.koct[sel()] = (int8_t)clampi(proj.koct[sel()] + d, -3, 3);
            dirty_g();
            {
                char t[6];
                say("OCTAVE", itoa_s(proj.koct[sel()], t));
            }
        } else if (ui.held && mode() != M_MULTI && mode() != M_MATRIX) {   /* held steps: an octave */
            step_knob(1, d);
        } else if (btn_held(B_EDIT)) {           /* EDIT + OCT: the sequence transposed a degree */
            undo();
            s4_transpose(sel(), trk(), d);
            dirty();
            ui.btn_used |= 1u << B_EDIT;
            say("TRANSPOSE", d > 0 ? "+1" : "-1");
        } else if (mode() == M_MULTI) {
            set_trk(trk() + d);
        } else if (mode() == M_MATRIX) {
            ui.mx_lane = (uint8_t)clampi(ui.mx_lane + d, 0, S4_NLANE - 1);
        } else if (ui.view == V_MOD) {
            ui.lane = (uint8_t)clampi(ui.lane + d, 0, 3);
        } else {
            int by = sh ? s4_scale_len(sq()->scale) : 1;
            proj.row[sel()] = (int8_t)clampi(proj.row[sel()] + d * by, -48, 60);
            dirty_g();
        }
        break;
    }
    default:
        break;
    }
}
static void btn_tap(int b)                       /* let go, unused */
{
    uint32_t now = plat_ms();
    int dtap = now - ui.up_t[b] < DTAP_MS;
    switch (b) {
    case B_REC:
        if (dtap && ui.rec_tap) {                /* a double tap: the notes played captured */
            if (ui.rec_tap == 1)
                s4_rec(!s4p.rec);
            else
                ui.srec = !ui.srec;
            ui.rec_tap = 0;
            if (s4_cap_count) {
                undo();
                s4_capture(sel());
                dirty();
                say("CAPTURED", 0);
            } else
                say("NOTHING", "TO CAPTURE");
            break;
        }
        if (s4p.on || s4p.count) {
            s4_rec(!s4p.rec);
            ui.rec_tap = 1;
        } else {
            ui.srec = !ui.srec;
            ui.srec_any = 0;
            if (ui.srec) {
                ui.cursor = (uint8_t)srec_lo();
                set_page(ui.cursor / 16);
                say("STEP RECORD", "SEL TIE  EDIT REST");
            }
            ui.rec_tap = 2;
        }
        break;
    case B_HOME:
        if (ui.help)
            ui.help = 0;
        else if (ui.menu)
            close_menu();
        else if (ui.srec)
            ui.srec = 0;
        else if (ui.view != V_GRID)
            set_view(V_GRID);
        else
            set_view(V_PATS);
        break;
    case B_SAVE: save_now(); break;
    case B_SEQ: set_view(ui.view == V_KEYS ? V_GRID : V_KEYS); break;
    case B_ARP:
        if (ui.menu == MN_ARP)
            close_menu();
        else
            open_menu(MN_ARP);
        break;
    case B_FX:
        if (is_gen_menu(ui.menu))
            generate();
        else if (ui.menu == MN_RANDP)
            close_menu();
        else
            open_menu(MN_RANDP);
        break;
    case B_LFO:
        if (ui.menu == MN_LFO && ui.page < 3)
            ui.page = 3;
        else if (ui.menu == MN_LFO)
            close_menu();
        else
            open_menu(MN_LFO);
        break;
    case B_ENV: set_view(ui.view == V_MOD ? V_GRID : V_MOD); break;
    case B_SEL:
        if (dtap && ui.menu == MN_DIV)
            open_menu(MN_LOOP);
        else if (ui.menu == MN_DIV || ui.menu == MN_LOOP)
            close_menu();
        else
            open_menu(MN_DIV);
        break;
    case B_EDIT: say(s4_undo() ? "UNDO" : "NOTHING TO UNDO", 0); break;
    default: break;
    }
    if (b != B_REC)
        ui.rec_tap = 0;
    ui.up_t[b] = now;
}

/* help cards: a button held alone shows what it does; GLO held alone long: the setup */
static void hold_tick(void)
{
    uint32_t now = plat_ms(), b = ui.btn;
    int i;
    if (ui.help) {
        if (b != 1u << (ui.help - 1) || ui.keys || (int32_t)(ui.turn_t - ui.down_t[ui.help - 1]) > 0)
            ui.help = 0;
        return;
    }
    if (!b || (b & (b - 1u)) || ui.keys)
        return;
    for (i = 0; i < NB; i++) {
        if (b != 1u << i || (ui.btn_used & b) || (int32_t)(ui.turn_t - ui.down_t[i]) > 0)
            continue;
        if (i == B_GLO && now - ui.down_t[i] >= SETUP_MS) {
            open_menu(MN_SETUP);
            ui.btn_used |= b;
        } else if ((i == B_HOME || i == B_SAVE) && now - ui.down_t[i] >= HOLD_MS && ui.view != V_PATS) {
            ui.pats_prev = ui.view;              /* HOME / SAVE held: the patterns, for as long */
            ui.pats_mom = (uint8_t)(i + 1);
            set_view(V_PATS);
            ui.btn_used |= b;
        } else if (i == B_PLAY && (s4p.on || s4p.count) && now - ui.down_t[i] >= HOLD_MS && !ui.fill) {
            s4_fill(1);                          /* PLAY held, playing: the fill */
            ui.fill = 1;
            ui.btn_used |= b;
        } else if (i != B_PLAY && i != B_GLO && i != B_HOME && i != B_SAVE && now - ui.down_t[i] >= HELP_MS) {
            ui.help = (uint8_t)(i + 1);
            ui.btn_used |= b;
        }
    }
}

/* --------------------------------------------------------------------------- the knobs --- */
static void grid_knob(int k, int32_t e)
{
    uint32_t now = plat_ms();
    if (now - ui.knob_t > 1500u || ui.knob_k != k) {   /* a new gesture: its undo */
        undo();
        ui.knob_k = (uint8_t)k;
    }
    ui.knob_t = now;
    if (ui.held)
        step_knob(k, (int)(k == 1 ? e : accel(EN_K1 + k, e, 127)));
    else
        global_knob(k, (int)(k == 1 ? e : accel(EN_K1 + k, e, 127)));
}
static void shift_knob(int k, int32_t e)         /* GLO + K1-K4: tempo, swing, scale, root */
{
    int s = sel(), j;
    switch (k) {
    case 0: s4g.tempo = (uint16_t)clampi(s4g.tempo + (int)accel(EN_K1, e, 2700) * 10, 300, 3000); dirty_g(); break;
    case 1: sq()->swing = (uint8_t)clampi((sq()->swing ? sq()->swing : 50) + (int)e, 50, 75); dirty(); break;
    case 2:
        undo();
        for (j = 0; j < S4_NSEQ; j++)
            if (j == s || s4g.lock_scale)
                s4_set_scale(j, clampi(s4[j].scale + (int)e, 0, S4_HARMO), 1), project_dirty_seq(j);
        break;
    default:
        for (j = 0; j < S4_NSEQ; j++)
            if (j == s || s4g.lock_scale) {
                s4[j].root = (uint8_t)((s4[j].root + 12 + (e > 0 ? 1 : -1)) % 12);
                project_dirty_seq(j);
            }
        break;
    }
    ui.btn_used |= 1u << B_GLO;
}
static void keys_knob(int k, int32_t e)          /* the keyboard: velocity, bend, mod wheel, roller */
{
    int s = sel();
    switch (k) {
    case 0: proj.kvel = (uint8_t)clampi((proj.kvel ? proj.kvel : 100) + (int)accel(EN_K1, e, 127), 1, 127); dirty_g(); break;
    case 1: {
        int v;
        ui.bend = (int16_t)clampi(ui.bend + (int)e * 512, -8192, 8191);
        ui.bend_t = plat_ms();
        v = ui.bend + 8192;
        s4_live_cc(s, trk(), 0xE0, (uint32_t)(v & 127), (uint32_t)(v >> 7));
        break;
    }
    case 2:
        ui.modw = (uint8_t)clampi(ui.modw + (int)accel(EN_K3, e, 127), 0, 127);
        s4_live_cc(s, trk(), 0xB0, 1, ui.modw);
        break;
    default:
        ui.roll_i = (uint8_t)clampi(ui.roll_i + (e > 0 ? 1 : -1), 0, 7);
        s4_roll(s, ROLL_DV[ui.roll_i]);
        ui.roll = ui.roll_i != 0;
        break;
    }
}
static void mod_knob(int k, int32_t e)           /* the mod lanes' view */
{
    int l = lane_of_knob(), i;
    s4_modl_t *m = &sq()->ml[l];
    switch (k) {
    case 0:
        if (ui.held)
            step_knob(3, (int)accel(EN_K1, e, 127));
        else
            global_knob(3, (int)accel(EN_K1, e, 127));
        break;
    case 1:
        if (mode() == M_MULTI)
            set_trk(trk() + (e > 0 ? 1 : -1));
        else
            ui.lane = (uint8_t)clampi(ui.lane + (e > 0 ? 1 : -1), 0, 3);
        break;
    case 2:
        if (!m->xtype)
            m->xtype = X_CC;
        m->xnum = (uint8_t)clampi(m->xnum + (int)e, 0, 127);
        m->on = 1;
        dirty();
        break;
    default:
        i = clampi(m->smooth + (int)e, 0, 30);
        m->smooth = (uint8_t)i;
        dirty();
        break;
    }
}
static void knob(int k, int32_t e)
{
    if (ui.menu) {
        menu_knob(k, (int)e);
        return;
    }
    if (shift() && !ui.held) {
        shift_knob(k, e);
        return;
    }
    switch (ui.view) {
    case V_KEYS: keys_knob(k, e); break;
    case V_MOD: mod_knob(k, e); break;
    case V_GRID: grid_knob(k, e); break;
    default: break;
    }
}

/* -------------------------------------------------------------------------------- input --- */
static void midi_learn(void)
{
    if (!ui.learn_in || s4p.in + 1u == ui.learn_in)
        return;
    {
        s4_map_t *mp = &sq()->map[ui.map_i & 15];
        uint8_t st = s4p.last_in[0];
        if ((st & 0xF0) == 0xB0 || (st & 0xF0) == 0x90) {
            mp->type = (uint8_t)((st & 0xF0) == 0xB0 ? 1 : 2);
            mp->ch = (uint8_t)((st & 15) + 1);
            mp->num = s4p.last_in[1];
            ui.learn_in = 0;
            dirty();
            say("LEARNED", (st & 0xF0) == 0xB0 ? "CC" : "NOTE");
        } else
            ui.learn_in = s4p.in + 1u;
    }
}
static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, now = plat_ms();
    int i;
    int32_t e;
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.btn_used &= ~m;
            ui.down_t[i] = now;
            btn_down(i);
        } else {
            if (i == B_PLAY)
                play_up();
            else if (ui.pats_mom == i + 1) {     /* the patterns, held: back */
                ui.pats_mom = 0;
                set_view(ui.pats_prev);
            } else if (i == B_SEL && ui.sel_first)
                ui.sel_first = 0;
            else if (!(ui.btn_used & m))
                btn_tap(i);
            if (i != B_PLAY)
                ui.up_t[i] = now;
        }
    }
    ch = keys ^ ui.keys;
    ui.keys = keys;
    for (i = 0; i < NKEYS; i++)
        if (ch >> i & 1u) {
            if (keys >> i & 1u) {
                ui.keys_used &= ~(1u << i);
                key_down(i);
            } else
                key_up(i);
        }
    if (btn || keys || ch)
        ui.act_t = now;
    if ((e = plat_enc(EN_ALGO)) != 0) {          /* the sequencer (GLO: the track) */
        if (shift()) {
            set_trk(trk() + (e > 0 ? 1 : -1));
            ui.btn_used |= 1u << B_GLO;
        } else
            set_sel(sel() + (e > 0 ? 1 : -1));
        ui.turn_t = ui.act_t = now;
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {        /* its pattern: in when the knob rests */
        int cur = ui.pending_pat ? ui.pending_pat - 1 : (s4p.next[sel()] != 0xFF ? s4p.next[sel()] : proj.pat[sel()]);
        char t[8];
        cur = clampi(cur + (e > 0 ? 1 : -1), 0, S4_NPAT - 1);
        ui.pending_pat = (uint8_t)(cur + 1);
        ui.pending_t = now;
        t[0] = 'P';
        itoa_u((uint32_t)cur + 1u, t + 1);
        say("PATTERN", t);
        ui.turn_t = ui.act_t = now;
    }
    if ((e = plat_enc(EN_SELECT)) != 0) {
        int d = e > 0 ? 1 : -1;
        if (ui.menu) {
            int n = menu_pages(ui.menu);
            ui.page = (uint8_t)((ui.page + n + d) % n);
        } else if (ui.view == V_SONG) {
            ui.slot = (uint8_t)clampi(ui.slot + d, 0, S4_NSLOT - 1);
        } else if (ui.view == V_MOD) {
            ui.lane = (uint8_t)clampi(ui.lane + d, 0, mode() == M_MULTI ? 7 : 3);
        } else if (ui.srec && !s4p.on) {
            ui.cursor = (uint8_t)clampi(ui.cursor + d, 0, S4_NSTEP - 1);
            set_page(ui.cursor / 16);
        } else if (mode() == M_MATRIX) {
            ui.mx_lane = (uint8_t)clampi(ui.mx_lane + d, 0, S4_NLANE - 1);
        } else if (mode() == M_MULTI) {
            set_trk(trk() + d);
        } else {
            proj.row[sel()] = (int8_t)clampi(proj.row[sel()] + d, -48, 60);
            dirty_g();
        }
        ui.turn_t = ui.act_t = now;
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + i)) != 0) {
            knob(i, e);
            ui.touched = (uint8_t)(i + 1);
            ui.touch_t = now;
            ui.turn_t = ui.act_t = now;
        }
    hold_tick();
}

/* things that wait: the pattern picked, the bend's return, the playhead's page, MIDI learn */
static void service(void)
{
    uint32_t now = plat_ms();
    int s = sel();
    if (ui.pending_pat && now - ui.pending_t > 600u) {
        int p = ui.pending_pat - 1;
        ui.pending_pat = 0;
        if (p != proj.pat[s] || s4p.next[s] != 0xFF) {
            char t[8];
            t[0] = 'P';
            itoa_u((uint32_t)p + 1u, t + 1);
            say(t, project_load_pattern(s, p) ? "EMPTY" : s4p.on ? "NEXT" : "LOADED");
        }
    }
    if (ui.bend && now - ui.bend_t > 300u) {     /* the bend springs back */
        ui.bend = 0;
        s4_live_cc(s, trk(), 0xE0, 0, 64);
    }
    if (sq()->follow && s4p.on && !ui.held && ui.view == V_GRID && mode() != M_MATRIX && mode() != M_STOCH)
        set_page(s4p.step[s][mode() == M_MULTI ? trk() : 0] / 16);
    if (ui.srec && s4p.on)
        ui.srec = 0;
    midi_learn();
    notes_in();
    (void)last_page;
}

/* ------------------------------------------------------------------------------ the lights --- */
static int blink(void) { return (int)((plat_ms() / 160u) & 1u); }
static int beat_blink(void) { return s4p.on && (s4p.pos % S4_U) < S4_U / 4u; }
static void leds(void)
{
    uint32_t b = 0, k = 0;
    int s = sel(), i, bl = blink();
    if (s4p.on || (s4p.count && bl))
        b |= 1u << B_PLAY;
    if (s4p.rec || (ui.srec && bl))
        b |= 1u << B_REC;
    if (ui.view == V_KEYS)
        b |= 1u << B_SEQ;
    if (ui.view == V_MOD)
        b |= 1u << B_ENV;
    if (ui.view == V_PATS || ui.view == V_SONG)
        b |= 1u << B_HOME;
    if (sq()->karp.mode || sq()->karp.hold || ((mode() == M_POLY || mode() == M_CHORD) && sq()->arp.mode))
        b |= 1u << B_ARP;
    if (ui.menu == MN_RANDP || is_gen_menu(ui.menu))
        b |= 1u << B_FX;
    if (ui.menu == MN_LFO || ui.menu == MN_MAP)
        b |= 1u << B_LFO;
    if (ui.menu == MN_DIV || ui.menu == MN_LOOP)
        b |= 1u << B_SEL;
    if (ui_dirty())
        b |= 1u << B_SAVE;
    if (shift())
        b |= 1u << B_GLO;
    if (ui.view == V_KEYS) {
        if (proj.koct[s] > 0)
            b |= 1u << B_OCTUP;
        if (proj.koct[s] < 0)
            b |= 1u << B_OCTDN;
    }
    if (s4g.leds) {
        if (ui.view == V_KEYS) {
            k = ui.keys;
            if (mode() == M_CHORD) {
                if (sq()->ch_bass)
                    k |= 1u << BLACK_KEY[4];
                if (sq()->ch_bank)
                    k |= 1u << BLACK_KEY[5];
            }
        } else if (ui.view == V_PATS) {
            for (i = 0; i < S4_NPAT; i++) {
                int on = project_has_pattern(s, i);
                if (i == s4p.pat[s] && s4p.on)
                    on = beat_blink() ? 0 : 1;
                else if (i == proj.pat[s])
                    on = 1;
                if (s4p.next[s] == i)
                    on = bl;
                if (on)
                    k |= 1u << WHITE_KEY[i];
            }
            k |= 1u << BLACK_KEY[s];
        } else if (ui.view == V_SONG) {
            const s4_song_t *g = &s4song[proj.song % S4_NSONG];
            for (i = 0; i < S4_NSLOT; i++) {
                int on = g->slot[ui.song_lane & 3][i].pat != S4_EMPTY;
                if (s4p.song_on && s4p.slot[ui.song_lane & 3] == i)
                    on = bl;
                if (i == ui.slot && ui.menu == MN_SLOT)
                    on = 1;
                if (on)
                    k |= 1u << WHITE_KEY[i];
            }
            k |= 1u << BLACK_KEY[ui.song_lane & 3];
        } else {
            int t = trk(), ph = s4p.on ? s4p.step[s][mode() == M_MULTI || mode() == M_MATRIX ? t : 0] : -1;
            for (i = 0; i < 16; i++) {
                int st = step_of(i), on;
                if (ui.view == V_MOD)
                    on = sq()->mod[lane_of_knob()][st] >= 64;
                else if (mode() == M_STOCH)
                    on = 0;
                else if (mode() == M_MATRIX)
                    on = sq()->d.mx[t & 3].v[ui.mx_lane][i] != 0;
                else
                    on = step_on(s, t, st);
                if (mode() == M_MATRIX ? i == (ph & 15) : st == ph)
                    on = !on;
                if (ui.srec && !s4p.on && st == ui.cursor)
                    on = bl;
                if (on)
                    k |= 1u << WHITE_KEY[i];
            }
            if (mode() == M_MULTI) {
                for (i = 0; i < 8; i++) {
                    int on = !sq()->d.multi.t[i].mute;
                    if (i == t)
                        on = bl || s4p.lit[s][i];
                    if (on)
                        k |= 1u << BLACK_KEY[i];
                }
            } else if (ui.view == V_GRID && mode() <= M_CHORD) {
                for (i = 0; i < 8; i++)
                    if (row_deg(i) == ui.cur_deg[s])
                        k |= 1u << BLACK_KEY[i];
            }
            if (page() > 0)
                k |= 1u << BLACK_KEY[BK_PREV];
            if (page() < last_page())
                k |= 1u << BLACK_KEY[BK_NEXT];
            if (sq()->mute)
                k |= 1u << BLACK_KEY[BK_MUTE];
        }
    }
    if (s4g.leds == 2)
        b = 0;
    plat_glow(s4g.glow);
    plat_play_red(s4p.rec || s4p.count);
    plat_leds(s4g.leds ? b : 0, k);
}
