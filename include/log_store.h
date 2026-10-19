/*
 * log_store.h - message log in NOR flash: a ring of 256-byte pages with
 * per-page CRCs, grouped into sessions. Portable C: the flash itself is a
 * small backend (log_flash_t) so the same format code runs on the RP2350
 * (QSPI flash, src/log_flash_rp2350.c), in the browser simulator and the
 * host tests (RAM emulation of NOR flash, src/log_flash_ram.c), and later
 * against a microSD archive.
 *
 * Layout
 * ------
 *   region   = N sectors of 4096 bytes (erase unit), 16 pages each
 *   page     = 32-byte header + 224-byte payload, programmed once
 *   header   = magic "ML" (u16 0x4C4D) | kind u8 (1 session start, 2 data;
 *              bit 7 = real time valid) | nrec u8 | page seq u32 |
 *              session u32 | payload length u16 | base real ms u16 |
 *              base uptime us u64 | base real seconds u32 | CRC-32 u32
 *              (little-endian; CRC over header bytes 0..27 + payload)
 *   start page payload begins with 16 bytes of session info:
 *              start uptime us u64 | start real s u32 | start real ms u16 |
 *              flags u8 (bit 0 real valid) | 0xFF
 *   record   = b0 | dt varint | [len u8] | [total u16] | bytes
 *              b0: bit 0 input (0 A, 1 B), bit 1 sysex truncated (u16 total
 *              length follows), bit 2 sysex unterminated, bit 3 error record
 *              (bytes = error code), bits 4-7 byte count (15 = a length byte
 *              follows). dt = zigzag LEB128 microseconds since the previous
 *              record in the page (the first record is at the page base).
 *              bytes = the raw message with its status byte (sysex: the
 *              first 128 bytes, as the monitor keeps).
 *
 * Pages are written in ring order with an ever-increasing page sequence
 * number. At boot every page is checked (magic + CRC); the newest valid page
 * gives the write position. A page cut short by power loss fails its CRC and
 * is skipped, as is any page that is not blank where the writer wants to
 * program (it moves on to the next blank page). The sector after the write
 * position is erased ahead of time, which drops the oldest data: the ring
 * loses the oldest session first, a sector at a time. Records live in a RAM
 * page until it is full or flushed, then wait in a RAM queue until the
 * caller lets log_service() program them.
 */
#ifndef MIDIUM_LOG_STORE_H
#define MIDIUM_LOG_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include "midi_parser.h"

#define LOG_PAGE_SIZE        256u
#define LOG_SECTOR_SIZE      4096u
#define LOG_PAGES_PER_SECTOR (LOG_SECTOR_SIZE / LOG_PAGE_SIZE)
#define LOG_HDR_SIZE         32u
#define LOG_PAYLOAD_SIZE     (LOG_PAGE_SIZE - LOG_HDR_SIZE)
#define LOG_SESS_INFO_SIZE   16u
#define LOG_MAGIC            0x4C4Du
#define LOG_KIND_START       1u
#define LOG_KIND_DATA        2u
#define LOG_KIND_REAL        0x80u
#define LOG_MAX_RECS         80u      /* records per page (min record = 3 bytes) */
#define LOG_MAX_SESSIONS     128      /* sessions kept in the RAM index (newest) */
#define LOG_PENDING_PAGES    16u      /* full pages waiting for flash (4 kB RAM) */
#define LOG_PENDING_FORCE    8u       /* erase even when not quiet at this backlog */
#define LOG_ERASE_AHEAD      8u       /* pre-erase the next sector this many pages early */

/* Flash backend. Offsets are relative to the start of the log region.
 * erase_sector / program_page do the whole operation before returning. */
typedef struct {
    uint32_t size;                    /* bytes, a multiple of LOG_SECTOR_SIZE */
    void *ctx;
    void (*read)(void *ctx, uint32_t off, void *dst, uint32_t len);
    bool (*erase_sector)(void *ctx, uint32_t off);
    bool (*program_page)(void *ctx, uint32_t off, const uint8_t *src);   /* 256 bytes */
} log_flash_t;

typedef struct {
    uint64_t t_us;       /* uptime */
    bool     real;       /* real_s / real_ms valid */
    uint32_t real_s;     /* seconds since 2000-01-01 (timeutil.h) */
    uint16_t real_ms;
} log_time_t;

typedef struct {
    uint32_t   no;                 /* session number */
    uint32_t   first_seq, last_seq;/* page seqs in flash; 0 = none yet */
    uint32_t   first_slot, last_slot;
    uint32_t   nrec;               /* records (live session: including ones still in RAM) */
    log_time_t start;              /* session start */
    log_time_t end;                /* last record */
    bool       has_end;
    bool       partial;            /* start overwritten (start = first remaining record) */
} log_sess_t;

typedef struct {
    uint8_t    payload[LOG_PAYLOAD_SIZE];
    uint16_t   len;
    uint8_t    nrec;
    uint8_t    kind;               /* incl. LOG_KIND_REAL */
    uint32_t   session;
    log_time_t base;               /* first record (real fields: page mapping) */
    uint64_t   last_us;            /* previous record (dt reference) */
    int64_t    real_off_ms;        /* real ms - uptime ms of this page */
} log_page_buf_t;

typedef struct {
    uint32_t slot;
    uint16_t idx;                  /* record index within the page */
} log_pos_t;

typedef struct {
    bool     ok;
    uint32_t slot, seq, session;
    uint8_t  kind, nrec;
    uint8_t  raw[LOG_PAGE_SIZE];
    uint16_t off[LOG_MAX_RECS];
    uint64_t t_us[LOG_MAX_RECS];
} log_cache_t;

typedef struct {
    const log_flash_t *fl;
    bool     enabled;
    uint32_t nslots, nsectors;

    /* index, oldest first */
    log_sess_t sess[LOG_MAX_SESSIONS];
    int      nsess;
    uint32_t next_seq, next_session;
    uint32_t used_pages;           /* valid pages in flash */

    /* writer */
    uint32_t cur_slot;             /* next slot to program */
    int32_t  erased_sector;        /* sector known to be blank, -1 none */
    bool     live;
    uint32_t live_no;
    bool     live_start_done;      /* start page queued */
    bool     open_active;
    log_page_buf_t open;
    log_page_buf_t pend[LOG_PENDING_PAGES];
    uint32_t ph, pt;

    /* wipe (delete all) */
    bool     wiping;
    uint32_t wipe_sector;

    /* stats */
    uint32_t drops;                /* records lost: RAM queue full */
    uint32_t pages_written, erases, bad_pages, prog_errors;

    log_cache_t cache;
} log_t;

/* Scan the region and build the session index. No session is started. */
void log_init(log_t *L, const log_flash_t *fl);
/* Start a new session (closes the current one). */
void log_new_session(log_t *L, const log_time_t *now);
/* Add one message (all messages, unfiltered). false = dropped / not logging. */
bool log_append(log_t *L, const midi_msg_t *m, const log_time_t *t);
/* Move the partly filled RAM page to the flash queue. */
void log_flush(log_t *L);
bool log_open_page(const log_t *L, uint64_t *first_us);   /* RAM page with records? since when */
uint32_t log_pending(const log_t *L);                       /* full pages waiting */

/* One flash operation at most. erase_ok: the caller says this is a good
 * moment for a (slow) sector erase; it happens anyway once the backlog
 * reaches LOG_PENDING_FORCE pages. */
typedef enum { LOG_OP_NONE = 0, LOG_OP_PROGRAM, LOG_OP_ERASE, LOG_OP_WAIT } log_op_t;
log_op_t log_service(log_t *L, bool erase_ok);

/* Delete everything: erases every non-blank sector, one per log_service()
 * call. The live session ends; start a new one when log_wiping() is false. */
void log_wipe_start(log_t *L);
bool log_wiping(const log_t *L, unsigned *pct);

unsigned log_used_pct(const log_t *L);
int      log_session_count(const log_t *L);
/* i = 0 is the newest session. */
const log_sess_t *log_session(const log_t *L, int i);
const log_sess_t *log_find(const log_t *L, uint32_t no);
bool     log_is_live(const log_t *L, uint32_t no);

/* ---- reading (one session at a time; pages are verified as they are read) ---- */
bool log_pos_first(log_t *L, uint32_t no, log_pos_t *p);
bool log_pos_last(log_t *L, uint32_t no, log_pos_t *p);
bool log_pos_next(log_t *L, uint32_t no, log_pos_t *p);
bool log_pos_prev(log_t *L, uint32_t no, log_pos_t *p);
bool log_read(log_t *L, const log_pos_t *p, midi_msg_t *m, log_time_t *t);

uint32_t log_crc32(const uint8_t *d, uint32_t n, uint32_t crc);

#endif /* MIDIUM_LOG_STORE_H */
