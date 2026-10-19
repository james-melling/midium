/*
 * log_store.c - flash message log. Format and rules: see log_store.h.
 */
#include <string.h>
#include "log_store.h"
#include "midi_defs.h"

/* ------------------------------------------------------------------------ */
/* helpers                                                                   */
/* ------------------------------------------------------------------------ */
static uint32_t s_crc_tab[256];
static bool s_crc_ready;

static void crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
        s_crc_tab[i] = c;
    }
    s_crc_ready = true;
}

uint32_t log_crc32(const uint8_t *d, uint32_t n, uint32_t crc) {
    if (!s_crc_ready) crc_init();
    crc = ~crc;
    while (n--) crc = s_crc_tab[(crc ^ *d++) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

static inline void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static inline void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }
static inline uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t get32(const uint8_t *p) { return get16(p) | ((uint32_t)get16(p + 2) << 16); }
static inline uint64_t get64(const uint8_t *p) { return get32(p) | ((uint64_t)get32(p + 4) << 32); }

static inline int64_t real_ms_of(const log_time_t *t) { return (int64_t)t->real_s * 1000 + t->real_ms; }
static void set_real_ms(log_time_t *t, int64_t ms) {
    if (ms < 0) ms = 0;
    t->real_s = (uint32_t)(ms / 1000);
    t->real_ms = (uint16_t)(ms % 1000);
}

static inline uint32_t slot_off(uint32_t slot) { return slot * LOG_PAGE_SIZE; }
static inline uint32_t next_slot(const log_t *L, uint32_t s) { return s + 1 >= L->nslots ? 0 : s + 1; }
static inline uint32_t prev_slot(const log_t *L, uint32_t s) { return s == 0 ? L->nslots - 1 : s - 1; }

/* Header + CRC check of a raw page. */
static bool page_valid(const uint8_t *pg) {
    if (get16(pg) != LOG_MAGIC) return false;
    uint8_t kind = pg[2] & 0x7Fu;
    uint16_t len = get16(pg + 12);
    if ((kind != LOG_KIND_START && kind != LOG_KIND_DATA) || len > LOG_PAYLOAD_SIZE) return false;
    if (kind == LOG_KIND_START && len < LOG_SESS_INFO_SIZE) return false;
    uint32_t c = log_crc32(pg, 28, 0);
    c = log_crc32(pg + LOG_HDR_SIZE, len, c);
    return c == get32(pg + 28);
}

static bool blank(const uint8_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (p[i] != 0xFF) return false;
    return true;
}

static bool page_blank(log_t *L, uint32_t slot) {
    uint8_t b[LOG_PAGE_SIZE];
    L->fl->read(L->fl->ctx, slot_off(slot), b, LOG_PAGE_SIZE);
    return blank(b, LOG_PAGE_SIZE);
}

static bool sector_blank(log_t *L, uint32_t sector) {
    for (uint32_t i = 0; i < LOG_PAGES_PER_SECTOR; i++)
        if (!page_blank(L, sector * LOG_PAGES_PER_SECTOR + i)) return false;
    return true;
}

/* ---- varint ---- */
static unsigned put_varint(uint8_t *o, uint32_t v) {
    unsigned n = 0;
    do {
        uint8_t b = v & 0x7Fu;
        v >>= 7;
        o[n++] = (uint8_t)(b | (v ? 0x80u : 0u));
    } while (v);
    return n;
}
static bool get_varint(const uint8_t *p, unsigned end, unsigned *i, uint32_t *v) {
    uint32_t r = 0;
    for (unsigned sh = 0; sh < 35; sh += 7) {
        if (*i >= end) return false;
        uint8_t b = p[(*i)++];
        r |= (uint32_t)(b & 0x7Fu) << sh;
        if (!(b & 0x80u)) { *v = r; return true; }
    }
    return false;
}
static inline uint32_t zz(int32_t v) { return ((uint32_t)v << 1) ^ (uint32_t)(v >> 31); }
static inline int32_t unzz(uint32_t v) { return (int32_t)(v >> 1) ^ -(int32_t)(v & 1u); }

/* ---- record encode / decode ---- */
#define R_SRC    0x01u
#define R_TRUNC  0x02u
#define R_UNTERM 0x04u
#define R_ERR    0x08u
#define R_MAX    (1u + 5u + 1u + 2u + MIDI_SX_STORE)

static unsigned rec_encode(const midi_msg_t *m, int32_t dt, uint8_t *o) {
    uint8_t flags = (uint8_t)(m->src & 1u);
    const uint8_t *data;
    uint8_t tmp[3];
    unsigned n;
    uint16_t total = 0;
    if (m->type == MT_ERROR) {
        flags |= R_ERR;
        tmp[0] = m->d1;
        data = tmp; n = 1;
    } else if (m->type == MT_SYSEX) {
        n = m->sx_len < MIDI_SX_STORE ? m->sx_len : MIDI_SX_STORE;
        if (n == 0) { tmp[0] = MIDI_SYSEX; data = tmp; n = 1; }
        else data = m->sx;
        if (m->flags & MSG_F_SX_TRUNC) { flags |= R_TRUNC; total = m->sx_len; }
        if (m->flags & MSG_F_SX_UNTERM) flags |= R_UNTERM;
    } else {
        tmp[0] = m->status; tmp[1] = m->d1; tmp[2] = m->d2;
        n = 1u + midi_data_len(m->status);
        if (midi_is_realtime(m->status)) n = 1;
        data = tmp;
    }
    unsigned k = 0;
    o[k++] = (uint8_t)(flags | (n < 15 ? n << 4 : 0xF0u));
    k += put_varint(o + k, zz(dt));
    if (n >= 15) o[k++] = (uint8_t)n;
    if (flags & R_TRUNC) { put16(o + k, total); k += 2; }
    memcpy(o + k, data, n);
    return k + n;
}

/* Parse one record at p[*i]. */
static bool rec_decode(const uint8_t *p, unsigned end, unsigned *i, int32_t *dt, midi_msg_t *m) {
    if (*i >= end) return false;
    uint8_t b0 = p[(*i)++];
    uint32_t v;
    if (!get_varint(p, end, i, &v)) return false;
    *dt = unzz(v);
    unsigned n = b0 >> 4;
    if (n == 15) { if (*i >= end) return false; n = p[(*i)++]; }
    uint16_t total = 0;
    if (b0 & R_TRUNC) { if (*i + 2 > end) return false; total = get16(p + *i); *i += 2; }
    if (n == 0 || n > MIDI_SX_STORE || *i + n > end) return false;
    const uint8_t *d = p + *i;
    *i += n;
    if (!m) return true;
    memset(m, 0, sizeof *m);
    m->src = b0 & R_SRC;
    if (b0 & R_ERR) {
        m->type = MT_ERROR;
        m->d1 = d[0];
    } else if (d[0] == MIDI_SYSEX) {
        m->type = MT_SYSEX;
        m->status = MIDI_SYSEX;
        memcpy(m->sx, d, n);
        m->sx_len = (uint16_t)((b0 & R_TRUNC) ? total : n);
        if (b0 & R_TRUNC) m->flags |= MSG_F_SX_TRUNC;
        if (b0 & R_UNTERM) m->flags |= MSG_F_SX_UNTERM;
    } else {
        m->status = d[0];
        m->type = midi_type_of_status(d[0]);
        m->d1 = n > 1 ? d[1] : 0;
        m->d2 = n > 2 ? d[2] : 0;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* index                                                                     */
/* ------------------------------------------------------------------------ */
static log_sess_t *find_mut(log_t *L, uint32_t no) {
    for (int i = 0; i < L->nsess; i++) if (L->sess[i].no == no) return &L->sess[i];
    return NULL;
}
const log_sess_t *log_find(const log_t *L, uint32_t no) { return find_mut((log_t *)L, no); }
int log_session_count(const log_t *L) { return L->nsess; }
const log_sess_t *log_session(const log_t *L, int i) {
    return (i >= 0 && i < L->nsess) ? &L->sess[L->nsess - 1 - i] : NULL;
}
bool log_is_live(const log_t *L, uint32_t no) { return L->live && L->live_no == no; }

static void remove_sess(log_t *L, int i) {
    memmove(&L->sess[i], &L->sess[i + 1], (size_t)(L->nsess - i - 1) * sizeof L->sess[0]);
    L->nsess--;
}

/* New index entry, kept sorted by number; evicts the oldest when full.
 * Returns NULL if no is older than everything kept in a full index. */
static log_sess_t *add_sess(log_t *L, uint32_t no) {
    if (L->nsess == LOG_MAX_SESSIONS) {
        if (no < L->sess[0].no) return NULL;
        remove_sess(L, 0);
    }
    int i = L->nsess;
    while (i > 0 && L->sess[i - 1].no > no) i--;
    memmove(&L->sess[i + 1], &L->sess[i], (size_t)(L->nsess - i) * sizeof L->sess[0]);
    L->nsess++;
    log_sess_t *s = &L->sess[i];
    memset(s, 0, sizeof *s);
    s->no = no;
    return s;
}

static void page_base(const uint8_t *pg, log_time_t *t) {
    t->t_us = get64(pg + 16);
    t->real = (pg[2] & LOG_KIND_REAL) != 0;
    t->real_s = get32(pg + 24);
    t->real_ms = get16(pg + 14);
}

/* Decode a verified page into the cache (offsets and times of records). */
static bool cache_load(log_t *L, uint32_t slot) {
    log_cache_t *c = &L->cache;
    if (c->ok && c->slot == slot) return true;
    c->ok = false;
    L->fl->read(L->fl->ctx, slot_off(slot), c->raw, LOG_PAGE_SIZE);
    if (!page_valid(c->raw)) return false;
    c->slot = slot;
    c->kind = c->raw[2];
    c->seq = get32(c->raw + 4);
    c->session = get32(c->raw + 8);
    unsigned end = LOG_HDR_SIZE + get16(c->raw + 12);
    unsigned i = LOG_HDR_SIZE + ((c->kind & 0x7Fu) == LOG_KIND_START ? LOG_SESS_INFO_SIZE : 0u);
    uint64_t t = get64(c->raw + 16);
    unsigned n = 0, want = c->raw[3];
    while (n < want && n < LOG_MAX_RECS && i < end) {
        int32_t dt;
        unsigned at = i;
        if (!rec_decode(c->raw, end, &i, &dt, NULL)) break;
        t = (uint64_t)((int64_t)t + dt);
        c->off[n] = (uint16_t)at;
        c->t_us[n] = t;
        n++;
    }
    c->nrec = (uint8_t)n;
    c->ok = true;
    return true;
}

static void rec_time(const log_cache_t *c, unsigned idx, log_time_t *t) {
    log_time_t b;
    page_base(c->raw, &b);
    t->t_us = c->t_us[idx];
    t->real = b.real;
    t->real_s = 0; t->real_ms = 0;
    if (b.real) set_real_ms(t, real_ms_of(&b) - (int64_t)(b.t_us / 1000u) + (int64_t)(c->t_us[idx] / 1000u));
}

void log_init(log_t *L, const log_flash_t *fl) {
    memset(L, 0, sizeof *L);
    L->fl = fl;
    L->erased_sector = -1;
    if (!fl || fl->size < 2u * LOG_SECTOR_SIZE) return;
    L->nsectors = fl->size / LOG_SECTOR_SIZE;
    L->nslots = L->nsectors * LOG_PAGES_PER_SECTOR;
    L->enabled = true;

    uint32_t max_seq = 0, newest = 0, max_no = 0;
    uint8_t pg[LOG_PAGE_SIZE];
    for (uint32_t s = 0; s < L->nslots; s++) {
        fl->read(fl->ctx, slot_off(s), pg, LOG_HDR_SIZE);
        if (get16(pg) != LOG_MAGIC) {
            if (!blank(pg, LOG_HDR_SIZE)) L->bad_pages++;
            continue;
        }
        fl->read(fl->ctx, slot_off(s), pg, LOG_PAGE_SIZE);
        if (!page_valid(pg)) { L->bad_pages++; continue; }
        L->used_pages++;
        uint32_t seq = get32(pg + 4), no = get32(pg + 8);
        if (seq > max_seq) { max_seq = seq; newest = s; }
        if (no > max_no) max_no = no;
        log_sess_t *e = find_mut(L, no);
        if (!e && !(e = add_sess(L, no))) continue;
        e->nrec += pg[3];
        if (!e->first_seq || seq < e->first_seq) { e->first_seq = seq; e->first_slot = s; }
        if (seq > e->last_seq) { e->last_seq = seq; e->last_slot = s; }
        if ((pg[2] & 0x7Fu) == LOG_KIND_START) {
            const uint8_t *si = pg + LOG_HDR_SIZE;
            e->start.t_us = get64(si);
            e->start.real_s = get32(si + 8);
            e->start.real_ms = get16(si + 12);
            e->start.real = (si[14] & 1u) != 0;
            e->has_end = true;     /* marks "start known" until the end pass below */
        }
    }
    /* end times (last record of each session); start of partial sessions */
    for (int i = 0; i < L->nsess; i++) {
        log_sess_t *e = &L->sess[i];
        bool start_known = e->has_end;
        e->has_end = false;
        if (cache_load(L, e->last_slot) && L->cache.nrec) {
            rec_time(&L->cache, L->cache.nrec - 1u, &e->end);
            e->has_end = true;
        }
        if (!start_known) {
            e->partial = true;
            if (cache_load(L, e->first_slot) && L->cache.nrec) rec_time(&L->cache, 0, &e->start);
            else page_base(L->cache.raw, &e->start);
        }
    }
    L->next_seq = max_seq + 1;
    L->next_session = max_no + 1;
    L->cur_slot = max_seq ? next_slot(L, newest) : 0;
    L->cache.ok = false;
}

/* ------------------------------------------------------------------------ */
/* writer                                                                    */
/* ------------------------------------------------------------------------ */
uint32_t log_pending(const log_t *L) { return L->ph - L->pt; }

bool log_open_page(const log_t *L, uint64_t *first_us) {
    if (!L->open_active || !L->open.nrec) return false;
    if (first_us) *first_us = L->open.base.t_us;
    return true;
}

void log_flush(log_t *L) {
    if (!L->open_active) return;
    L->open_active = false;
    if (!L->open.nrec && (L->open.kind & 0x7Fu) != LOG_KIND_START) return;
    if (log_pending(L) >= LOG_PENDING_PAGES) {
        L->drops += L->open.nrec;
        log_sess_t *e = find_mut(L, L->open.session);
        if (e && e->nrec >= L->open.nrec) e->nrec -= L->open.nrec;
        if ((L->open.kind & 0x7Fu) == LOG_KIND_START) L->live_start_done = false;
        return;
    }
    L->pend[L->ph % LOG_PENDING_PAGES] = L->open;
    L->ph++;
}

void log_new_session(log_t *L, const log_time_t *now) {
    if (!L->enabled || L->wiping) return;
    log_flush(L);
    uint32_t no = L->next_session++;
    log_sess_t *e = add_sess(L, no);
    if (!e) return;
    e->start = *now;
    L->live = true;
    L->live_no = no;
    L->live_start_done = false;
}

static void open_page(log_t *L, const log_time_t *t) {
    log_page_buf_t *p = &L->open;
    memset(p->payload, 0xFF, sizeof p->payload);
    p->len = 0;
    p->nrec = 0;
    p->session = L->live_no;
    p->base = *t;
    p->last_us = t->t_us;
    p->real_off_ms = t->real ? real_ms_of(t) - (int64_t)(t->t_us / 1000u) : 0;
    p->kind = LOG_KIND_DATA | (t->real ? LOG_KIND_REAL : 0u);
    if (!L->live_start_done) {
        const log_sess_t *e = log_find(L, L->live_no);
        log_time_t st = e ? e->start : *t;
        uint8_t *si = p->payload;
        put64(si, st.t_us);
        put32(si + 8, st.real_s);
        put16(si + 12, st.real_ms);
        si[14] = st.real ? 1u : 0u;
        si[15] = 0xFF;
        p->len = LOG_SESS_INFO_SIZE;
        p->kind = (uint8_t)((p->kind & LOG_KIND_REAL) | LOG_KIND_START);
        L->live_start_done = true;
    }
    L->open_active = true;
}

bool log_append(log_t *L, const midi_msg_t *m, const log_time_t *t) {
    if (!L->enabled || !L->live || L->wiping) return false;
    uint8_t rec[R_MAX];
    if (L->open_active) {
        log_page_buf_t *p = &L->open;
        int64_t dt = (int64_t)t->t_us - (int64_t)p->last_us;
        int64_t from_base = (int64_t)t->t_us - (int64_t)p->base.t_us;
        bool real_changed = t->real != ((p->kind & LOG_KIND_REAL) != 0);
        if (!real_changed && t->real) {
            int64_t off = real_ms_of(t) - (int64_t)(t->t_us / 1000u);
            if (off - p->real_off_ms > 2 || p->real_off_ms - off > 2) real_changed = true;
        }
        unsigned need = rec_encode(m, (int32_t)(dt > INT32_MAX ? 0 : dt < INT32_MIN ? 0 : dt), rec);
        if (real_changed || dt > INT32_MAX || dt < INT32_MIN || from_base > INT32_MAX || from_base < INT32_MIN ||
            p->len + need > LOG_PAYLOAD_SIZE || p->nrec >= LOG_MAX_RECS)
            log_flush(L);
    }
    if (!L->open_active) open_page(L, t);
    log_page_buf_t *p = &L->open;
    unsigned n = rec_encode(m, (int32_t)((int64_t)t->t_us - (int64_t)p->last_us), rec);
    if (p->len + n > LOG_PAYLOAD_SIZE) return false;   /* cannot happen: R_MAX < payload */
    memcpy(p->payload + p->len, rec, n);
    p->len = (uint16_t)(p->len + n);
    p->nrec++;
    p->last_us = t->t_us;
    log_sess_t *e = find_mut(L, L->live_no);
    if (e) {
        e->nrec++;
        log_time_t et;
        et.t_us = t->t_us;
        et.real = (p->kind & LOG_KIND_REAL) != 0;
        et.real_s = 0; et.real_ms = 0;
        if (et.real) set_real_ms(&et, (int64_t)(t->t_us / 1000u) + p->real_off_ms);
        e->end = et;
        e->has_end = true;
    }
    return true;
}

/* Account for the valid pages of a sector about to be erased. */
static void forget_sector(log_t *L, uint32_t sector) {
    uint8_t pg[LOG_PAGE_SIZE];
    for (uint32_t i = 0; i < LOG_PAGES_PER_SECTOR; i++) {
        uint32_t slot = sector * LOG_PAGES_PER_SECTOR + i;
        if (L->cache.ok && L->cache.slot == slot) L->cache.ok = false;
        L->fl->read(L->fl->ctx, slot_off(slot), pg, LOG_PAGE_SIZE);
        if (!page_valid(pg)) continue;
        if (L->used_pages) L->used_pages--;
        uint32_t no = get32(pg + 8), seq = get32(pg + 4);
        log_sess_t *e = find_mut(L, no);
        if (!e) continue;
        e->nrec = e->nrec >= pg[3] ? e->nrec - pg[3] : 0;
        if ((pg[2] & 0x7Fu) == LOG_KIND_START) e->partial = true;
        if (seq >= e->first_seq) {
            if (seq >= e->last_seq) {
                if (log_is_live(L, no)) { e->first_seq = e->last_seq = 0; }
                else { remove_sess(L, (int)(e - L->sess)); continue; }
            } else {
                e->first_seq = seq + 1;
                e->first_slot = next_slot(L, slot);
            }
        }
        if (e->partial && !log_is_live(L, no) && e->first_seq && cache_load(L, e->first_slot) && L->cache.nrec)
            rec_time(&L->cache, 0, &e->start);
    }
}

static bool do_erase(log_t *L, uint32_t sector) {
    forget_sector(L, sector);
    L->cache.ok = false;
    L->erases++;
    bool ok = L->fl->erase_sector(L->fl->ctx, sector * LOG_SECTOR_SIZE);
    if (ok) L->erased_sector = (int32_t)sector;
    return ok;
}

static void program_head(log_t *L, uint32_t slot) {
    log_page_buf_t *p = &L->pend[L->pt % LOG_PENDING_PAGES];
    uint8_t pg[LOG_PAGE_SIZE];
    memset(pg, 0xFF, sizeof pg);
    uint32_t seq = L->next_seq++;
    put16(pg, LOG_MAGIC);
    pg[2] = p->kind;
    pg[3] = p->nrec;
    put32(pg + 4, seq);
    put32(pg + 8, p->session);
    put16(pg + 12, p->len);
    put16(pg + 14, p->base.real ? p->base.real_ms : 0u);
    put64(pg + 16, p->base.t_us);
    put32(pg + 24, p->base.real ? p->base.real_s : 0u);
    memcpy(pg + LOG_HDR_SIZE, p->payload, p->len);
    uint32_t c = log_crc32(pg, 28, 0);
    c = log_crc32(pg + LOG_HDR_SIZE, p->len, c);
    put32(pg + 28, c);
    if (L->cache.ok && L->cache.slot == slot) L->cache.ok = false;
    bool ok = L->fl->program_page(L->fl->ctx, slot_off(slot), pg);
    L->cur_slot = next_slot(L, slot);
    if (L->erased_sector == (int32_t)(slot / LOG_PAGES_PER_SECTOR) && slot % LOG_PAGES_PER_SECTOR == LOG_PAGES_PER_SECTOR - 1)
        L->erased_sector = -1;
    if (!ok) { L->prog_errors++; return; }   /* page stays queued; retried in the next slot */
    L->pt++;
    L->pages_written++;
    L->used_pages++;
    log_sess_t *e = find_mut(L, p->session);
    if (e) {
        if (!e->first_seq) { e->first_seq = seq; e->first_slot = slot; }
        e->last_seq = seq;
        e->last_slot = slot;
    }
}

static log_op_t wipe_step(log_t *L) {
    for (int k = 0; k < 64 && L->wipe_sector < L->nsectors; k++) {
        uint32_t s = L->wipe_sector++;
        if (sector_blank(L, s)) continue;
        L->erases++;
        L->fl->erase_sector(L->fl->ctx, s * LOG_SECTOR_SIZE);
        return LOG_OP_ERASE;
    }
    if (L->wipe_sector >= L->nsectors) {
        L->wiping = false;
        L->cur_slot = 0;
        L->erased_sector = 0;
        L->used_pages = 0;
        L->cache.ok = false;
    }
    return LOG_OP_NONE;
}

log_op_t log_service(log_t *L, bool erase_ok) {
    if (!L->enabled) return LOG_OP_NONE;
    if (L->wiping) return wipe_step(L);
    bool force = log_pending(L) >= LOG_PENDING_FORCE;
    if (log_pending(L) == 0) {
        /* erase ahead while quiet */
        if (!erase_ok) return LOG_OP_NONE;
        uint32_t sec = L->cur_slot / LOG_PAGES_PER_SECTOR;
        uint32_t left = LOG_PAGES_PER_SECTOR - L->cur_slot % LOG_PAGES_PER_SECTOR;
        uint32_t nxt = sec + 1 >= L->nsectors ? 0 : sec + 1;
        if (left > LOG_ERASE_AHEAD || L->erased_sector == (int32_t)nxt) return LOG_OP_NONE;
        if (sector_blank(L, nxt)) { L->erased_sector = (int32_t)nxt; return LOG_OP_NONE; }
        do_erase(L, nxt);
        return LOG_OP_ERASE;
    }
    for (uint32_t guard = 0; guard < L->nslots; guard++) {
        uint32_t s = L->cur_slot;
        uint32_t sec = s / LOG_PAGES_PER_SECTOR;
        if (s % LOG_PAGES_PER_SECTOR == 0 && L->erased_sector != (int32_t)sec) {
            if (sector_blank(L, sec)) {
                L->erased_sector = (int32_t)sec;
            } else {
                if (!erase_ok && !force) return LOG_OP_WAIT;
                do_erase(L, sec);
                return LOG_OP_ERASE;
            }
        }
        if (page_blank(L, s)) {
            program_head(L, s);
            return LOG_OP_PROGRAM;
        }
        L->cur_slot = next_slot(L, s);   /* not blank (power-loss leftover): skip */
    }
    return LOG_OP_NONE;
}

void log_wipe_start(log_t *L) {
    if (!L->enabled) return;
    L->open_active = false;
    L->ph = L->pt = 0;
    L->nsess = 0;
    L->live = false;
    L->wiping = true;
    L->wipe_sector = 0;
    L->cache.ok = false;
}

bool log_wiping(const log_t *L, unsigned *pct) {
    if (pct) *pct = L->nsectors ? L->wipe_sector * 100u / L->nsectors : 100u;
    return L->wiping;
}

unsigned log_used_pct(const log_t *L) {
    if (!L->nslots) return 0;
    unsigned p = (unsigned)((L->used_pages * 100u + L->nslots - 1u) / L->nslots);
    return p > 100 ? 100 : p;
}

/* ------------------------------------------------------------------------ */
/* reader                                                                    */
/* ------------------------------------------------------------------------ */
static bool sess_page(log_t *L, const log_sess_t *e, uint32_t slot) {
    if (!cache_load(L, slot)) return false;
    return L->cache.session == e->no && L->cache.seq >= e->first_seq && L->cache.seq <= e->last_seq;
}

/* First page with records at or after slot (towards last_slot). */
static bool scan_fwd(log_t *L, const log_sess_t *e, uint32_t slot, uint32_t *out) {
    for (uint32_t g = 0; g < L->nslots; g++) {
        if (sess_page(L, e, slot) && L->cache.nrec) { *out = slot; return true; }
        if (slot == e->last_slot) return false;
        slot = next_slot(L, slot);
    }
    return false;
}
static bool scan_back(log_t *L, const log_sess_t *e, uint32_t slot, uint32_t *out) {
    for (uint32_t g = 0; g < L->nslots; g++) {
        if (sess_page(L, e, slot) && L->cache.nrec) { *out = slot; return true; }
        if (slot == e->first_slot) return false;
        slot = prev_slot(L, slot);
    }
    return false;
}

bool log_pos_first(log_t *L, uint32_t no, log_pos_t *p) {
    const log_sess_t *e = log_find(L, no);
    if (!e || !e->first_seq) return false;
    if (!scan_fwd(L, e, e->first_slot, &p->slot)) return false;
    p->idx = 0;
    return true;
}

bool log_pos_last(log_t *L, uint32_t no, log_pos_t *p) {
    const log_sess_t *e = log_find(L, no);
    if (!e || !e->first_seq) return false;
    if (!scan_back(L, e, e->last_slot, &p->slot)) return false;
    p->idx = (uint16_t)(L->cache.nrec - 1u);
    return true;
}

bool log_pos_next(log_t *L, uint32_t no, log_pos_t *p) {
    const log_sess_t *e = log_find(L, no);
    if (!e || !e->first_seq) return false;
    if (sess_page(L, e, p->slot) && p->idx + 1u < L->cache.nrec) { p->idx++; return true; }
    if (p->slot == e->last_slot) return false;
    uint32_t s;
    if (!scan_fwd(L, e, next_slot(L, p->slot), &s)) return false;
    p->slot = s;
    p->idx = 0;
    return true;
}

bool log_pos_prev(log_t *L, uint32_t no, log_pos_t *p) {
    const log_sess_t *e = log_find(L, no);
    if (!e || !e->first_seq) return false;
    if (p->idx > 0 && sess_page(L, e, p->slot)) {
        if (p->idx > L->cache.nrec) p->idx = L->cache.nrec;
        p->idx--;
        return true;
    }
    if (p->slot == e->first_slot) return false;
    uint32_t s;
    if (!scan_back(L, e, prev_slot(L, p->slot), &s)) return false;
    p->slot = s;
    p->idx = (uint16_t)(L->cache.nrec - 1u);
    return true;
}

bool log_read(log_t *L, const log_pos_t *p, midi_msg_t *m, log_time_t *t) {
    if (!cache_load(L, p->slot) || p->idx >= L->cache.nrec) return false;
    log_cache_t *c = &L->cache;
    unsigned i = c->off[p->idx];
    unsigned end = LOG_HDR_SIZE + get16(c->raw + 12);
    int32_t dt;
    if (!rec_decode(c->raw, end, &i, &dt, m)) return false;
    m->t_us = c->t_us[p->idx];
    if (t) rec_time(c, p->idx, t);
    return true;
}
