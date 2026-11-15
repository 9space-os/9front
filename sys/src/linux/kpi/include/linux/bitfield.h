/* Public domain. */

#ifndef _LINUX_BITFIELD_H
#define _LINUX_BITFIELD_H

#include <asm/byteorder.h>
#include <linux/build_bug.h>

#define __bf_shf(x) (__builtin_ffsll(x) - 1)

#define FIELD_GET(_m, _v) \
    ((typeof(_m))(((_v) & (_m)) >> __bf_shf(_m)))

#define FIELD_MAX(_m) \
    ((typeof(_m))((_m) >> __bf_shf(_m)))

#define FIELD_PREP(_m, _v) \
    (((typeof(_m))(_v) << __bf_shf(_m)) & (_m))

/* 9front: what the Linux code uses and OpenBSD's header lacks (OpenBSD changes that code), from our earlier LinuxKPI */
#include <linux/types.h>
#define FIELD_MAX(m)		((typeof(m))((m) >> __bf_shf(m)))
#define FIELD_FIT(m, v)		(!((((typeof(m))(v)) << __bf_shf(m)) & ~(m)))
#define FIELD_PREP_CONST(m, v)	FIELD_PREP(m, v)
#define __kpi_bits(t, sz) \
static inline t t##_encode_bits(t v, t f) { return (v << __bf_shf(f)) & f; } \
static inline t t##_get_bits(t v, t f) { return (v & f) >> __bf_shf(f); } \
static inline t t##_replace_bits(t old, t v, t f) { return (old & ~f) | t##_encode_bits(v, f); } \
static inline void t##p_replace_bits(t *p, t v, t f) { *p = t##_replace_bits(*p, v, f); }
__kpi_bits(u8, 8)
__kpi_bits(u16, 16)
__kpi_bits(u32, 32)
__kpi_bits(u64, 64)
/* 9front: the same on little endian fields (rtw89's descriptors) */
#define __kpi_lebits(sz) \
static inline __le##sz le##sz##_encode_bits(u##sz v, u##sz f) { return cpu_to_le##sz(u##sz##_encode_bits(v, f)); } \
static inline u##sz le##sz##_get_bits(__le##sz v, u##sz f) { return u##sz##_get_bits(le##sz##_to_cpu(v), f); } \
static inline __le##sz le##sz##_replace_bits(__le##sz old, u##sz v, u##sz f) { return cpu_to_le##sz(u##sz##_replace_bits(le##sz##_to_cpu(old), v, f)); } \
static inline void le##sz##p_replace_bits(__le##sz *p, u##sz v, u##sz f) { *p = le##sz##_replace_bits(*p, v, f); }
__kpi_lebits(16)
__kpi_lebits(32)
__kpi_lebits(64)

#endif
