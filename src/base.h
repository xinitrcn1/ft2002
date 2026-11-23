/* ft2002 - common types and small helpers. C11. */
#ifndef FT_BASE_H
#define FT_BASE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;

#define FT_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define FT_MIN(a, b) ((a) < (b) ? (a) : (b))
#define FT_MAX(a, b) ((a) > (b) ? (a) : (b))
#define FT_CLAMP(v, lo, hi) FT_MIN(FT_MAX(v, lo), hi)

/* Abort with a message; used for allocation failure and broken invariants. */
_Noreturn void ft_die(const char *fmt, ...);
void *ft_alloc(size_t n);              /* zeroed */
void *ft_realloc(void *p, size_t n);
char *ft_strdup(const char *s);
char *ft_strndup(const char *s, size_t n);

/* Grow a dynamic array: ensures cap >= need. elem = sizeof element. */
void ft_grow(void **p, u32 *cap, u32 need, size_t elem);
#define FT_GROW(p, cap, need) ft_grow((void **)&(p), &(cap), (need), sizeof(*(p)))

/* Coalesced struct-of-arrays. All columns of a table share one heap block
 * (one allocation, one free, columns 8-byte aligned, in the order given);
 * the first column's pointer is the block. Rows are addressed by u32
 * index, never by pointer, so growing the table moves nothing anyone
 * holds. Usage:
 *     SoaCol c[] = {SOA(t->addr), SOA(t->len)};
 *     soa_grow(c, 2, &t->cap, t->n + 1, t->n);   ...   soa_free(c, 2);
 */
typedef struct {
	void **p;
	u32 size; /* bytes per element */
} SoaCol;
#define SOA(col) ((SoaCol){(void **)&(col), (u32)sizeof(*(col))})
/* ensure cap >= need (doubling), keeping the first n rows */
void soa_grow(const SoaCol *cols, u32 ncols, u32 *cap, u32 need, u32 n);
/* exactly n rows of capacity (drop the growth slack once a table is built) */
void soa_fit(const SoaCol *cols, u32 ncols, u32 *cap, u32 n);
void soa_free(const SoaCol *cols, u32 ncols);

/* Byte pool: append-only string storage referenced by offsets. */
typedef struct {
	char *b;
	u32 n, cap;
} Pool;
u32 pool_add(Pool *p, const char *s, u32 n); /* stores s + NUL, returns offset */
static inline const char *pool_str(const Pool *p, u32 off) { return p->b + off; }
void pool_clear(Pool *p);
void pool_free(Pool *p);

/* Monotonic clock in nanoseconds. */
u64 ft_now_ns(void);

/* Read a whole file; returns malloc'd buffer (NUL terminated) or NULL. */
char *ft_read_file(const char *path, size_t *len);
int ft_write_file(const char *path, const char *data, size_t len);

/* UTF-8 helpers. */
u32 utf8_decode(const char *s, size_t n, u32 *cp); /* returns bytes used (>=1) */
u32 utf8_encode(u32 cp, char out[4]);
static inline bool utf8_cont(u8 c) { return (c & 0xC0) == 0x80; }


#endif
