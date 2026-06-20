#define BITF_STATIC
#define BITF_IMPLEMENTATION
#ifndef BITSPAN_H
#define BITSPAN_H

#include <stddef.h>
#include <stdint.h>

#if defined(BITF_STATIC) && defined(__GNUC__)
#define BITF_API static __attribute__((unused))
#elif defined(BITF_STATIC)
#define BITF_API static
#elif defined(_WIN32) && defined(BITF_BUILD_SHARED)
#define BITF_API __declspec(dllexport)
#elif defined(_WIN32) && defined(BITF_SHARED)
#define BITF_API __declspec(dllimport)
#elif defined(__GNUC__) && defined(BITF_BUILD_SHARED)
#define BITF_API __attribute__((visibility("default")))
#else
#define BITF_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define BITF_OK 0
#define BITF_ERR_ARG 1
#define BITF_ERR_BOUNDS 2
#define BITF_ERR_RANGE 3

#define BITF_LE 0x0
#define BITF_BE 0x1
#define BITF_SIGNED 0x2
#define BITF_FLAGS_MASK 0x3

#define BITF_MAX_BITS 64u

typedef struct bitf_field {
    size_t bit_offset;
    unsigned bit_len;
    unsigned flags;
} bitf_field_t;

BITF_API int bitf_get(const unsigned char *data, size_t data_len, size_t bit_offset,
                      unsigned bit_len, unsigned flags, uint64_t *out_value);
BITF_API int bitf_get_signed(const unsigned char *data, size_t data_len, size_t bit_offset,
                             unsigned bit_len, unsigned flags, int64_t *out_value);
BITF_API int bitf_set(unsigned char *data, size_t data_len, size_t bit_offset,
                      unsigned bit_len, unsigned flags, uint64_t value);
BITF_API int bitf_set_signed(unsigned char *data, size_t data_len, size_t bit_offset,
                             unsigned bit_len, unsigned flags, int64_t value);
BITF_API int bitf_check(size_t data_len, size_t bit_offset, unsigned bit_len, unsigned flags);
BITF_API int bitf_fields_check(const bitf_field_t *fields, size_t fields_len,
                               size_t *out_min_data_len);
BITF_API int bitf_get_many(const unsigned char *data, size_t data_len,
                           const bitf_field_t *fields, size_t fields_len, uint64_t *out_values);
BITF_API int bitf_set_many(unsigned char *data, size_t data_len, const bitf_field_t *fields,
                           size_t fields_len, const uint64_t *values);
BITF_API const char *bitf_strerror(int status);

static inline uint64_t bitf_mask_(unsigned bit_len)
{
    return bit_len >= 64u ? UINT64_MAX : (((uint64_t)1 << bit_len) - 1u);
}

static inline uint64_t bitf_extend_(uint64_t value, unsigned bit_len)
{
    uint64_t sign;

    if (bit_len >= 64u)
        return value;
    sign = (uint64_t)1 << (bit_len - 1u);
    return (value ^ sign) - sign;
}

static inline uint64_t bitf_get_le_unchecked(const unsigned char *data, size_t bit_offset,
                                             unsigned bit_len)
{
    const unsigned char *p = data + bit_offset / 8u;
    unsigned shift = (unsigned)(bit_offset % 8u);
    unsigned total = shift + bit_len;
    uint64_t acc = 0;
    unsigned i;

    for (i = 0; i * 8u < total && i < 8u; i++)
        acc |= (uint64_t)p[i] << (8u * i);
    acc >>= shift;
    if (total > 64u)
        acc |= (uint64_t)p[8] << (64u - shift);
    return acc & bitf_mask_(bit_len);
}

static inline uint64_t bitf_get_be_unchecked(const unsigned char *data, size_t bit_offset,
                                             unsigned bit_len)
{
    const unsigned char *p = data + bit_offset / 8u;
    unsigned shift = (unsigned)(bit_offset % 8u);
    unsigned total = shift + bit_len;
    unsigned nbytes = (total + 7u) / 8u;
    uint64_t acc = 0;
    unsigned i;

    for (i = 0; i < nbytes && i < 8u; i++)
        acc = (acc << 8) | p[i];
    if (nbytes > 8u)
        return ((acc << (8u - (72u - total))) | (p[8] >> (72u - total))) & bitf_mask_(bit_len);
    return (acc >> (nbytes * 8u - total)) & bitf_mask_(bit_len);
}

static inline uint64_t bitf_get_unchecked(const unsigned char *data, size_t bit_offset,
                                          unsigned bit_len, unsigned flags)
{
    uint64_t value = (flags & BITF_BE) ? bitf_get_be_unchecked(data, bit_offset, bit_len)
                                       : bitf_get_le_unchecked(data, bit_offset, bit_len);

    return (flags & BITF_SIGNED) ? bitf_extend_(value, bit_len) : value;
}

#ifdef __cplusplus
}
#endif

#endif

#if defined(BITF_IMPLEMENTATION) && !defined(BITF_IMPLEMENTATION_DONE)
#define BITF_IMPLEMENTATION_DONE

typedef char bitf_field_layout_check[sizeof(bitf_field_t) == sizeof(size_t) + 2 * sizeof(unsigned) ? 1 : -1];

int bitf_check(size_t data_len, size_t bit_offset, unsigned bit_len, unsigned flags)
{
    size_t first_byte = bit_offset / 8u;
    size_t end_bits = bit_offset % 8u + bit_len;

    if (bit_len == 0u || bit_len > BITF_MAX_BITS || (flags & ~(unsigned)BITF_FLAGS_MASK) != 0u)
        return BITF_ERR_ARG;
    if (first_byte >= data_len || (end_bits + 7u) / 8u > data_len - first_byte)
        return BITF_ERR_BOUNDS;
    return BITF_OK;
}

int bitf_get(const unsigned char *data, size_t data_len, size_t bit_offset,
                      unsigned bit_len, unsigned flags, uint64_t *out_value)
{
    int status;

    if (data == NULL || out_value == NULL)
        return BITF_ERR_ARG;
    status = bitf_check(data_len, bit_offset, bit_len, flags);
    if (status != BITF_OK)
        return status;
    *out_value = bitf_get_unchecked(data, bit_offset, bit_len, flags);
    return BITF_OK;
}

int bitf_get_signed(const unsigned char *data, size_t data_len, size_t bit_offset,
                             unsigned bit_len, unsigned flags, int64_t *out_value)
{
    uint64_t value;
    int status;

    if (out_value == NULL)
        return BITF_ERR_ARG;
    status = bitf_get(data, data_len, bit_offset, bit_len, flags | BITF_SIGNED, &value);
    if (status != BITF_OK)
        return status;
    *out_value = value <= (uint64_t)INT64_MAX ? (int64_t)value
                                              : -(int64_t)(~value) - 1;
    return BITF_OK;
}

static void bitf_store_le(unsigned char *data, size_t bit_offset, unsigned bit_len, uint64_t value)
{
    unsigned char *p = data + bit_offset / 8u;
    unsigned pos = (unsigned)(bit_offset % 8u);
    unsigned left = bit_len;

    while (left > 0u) {
        unsigned take = 8u - pos < left ? 8u - pos : left;
        unsigned mask = ((1u << take) - 1u) << pos;

        *p = (unsigned char)((*p & ~mask) | (((unsigned)value << pos) & mask));
        value >>= take;
        left -= take;
        pos = 0;
        p++;
    }
}

static void bitf_store_be(unsigned char *data, size_t bit_offset, unsigned bit_len, uint64_t value)
{
    unsigned char *p = data + bit_offset / 8u;
    unsigned pos = (unsigned)(bit_offset % 8u);
    unsigned left = bit_len;

    while (left > 0u) {
        unsigned take = 8u - pos < left ? 8u - pos : left;
        unsigned shift = 8u - pos - take;
        unsigned mask = ((1u << take) - 1u) << shift;
        unsigned chunk = (unsigned)(value >> (left - take)) & ((1u << take) - 1u);

        *p = (unsigned char)((*p & ~mask) | (chunk << shift));
        left -= take;
        pos = 0;
        p++;
    }
}

static void bitf_store(unsigned char *data, size_t bit_offset, unsigned bit_len, unsigned flags,
                       uint64_t value)
{
    value &= bitf_mask_(bit_len);
    if (flags & BITF_BE)
        bitf_store_be(data, bit_offset, bit_len, value);
    else
        bitf_store_le(data, bit_offset, bit_len, value);
}

static int bitf_fits(uint64_t value, unsigned bit_len, unsigned flags)
{
    uint64_t high;

    if (bit_len >= 64u)
        return 1;
    if (!(flags & BITF_SIGNED))
        return value <= bitf_mask_(bit_len);
    high = value >> (bit_len - 1u);
    return high == 0u || high == UINT64_MAX >> (bit_len - 1u);
}

int bitf_set(unsigned char *data, size_t data_len, size_t bit_offset, unsigned bit_len,
                      unsigned flags, uint64_t value)
{
    int status;

    if (data == NULL)
        return BITF_ERR_ARG;
    status = bitf_check(data_len, bit_offset, bit_len, flags);
    if (status != BITF_OK)
        return status;
    if (!bitf_fits(value, bit_len, flags))
        return BITF_ERR_RANGE;
    bitf_store(data, bit_offset, bit_len, flags, value);
    return BITF_OK;
}

int bitf_set_signed(unsigned char *data, size_t data_len, size_t bit_offset,
                             unsigned bit_len, unsigned flags, int64_t value)
{
    return bitf_set(data, data_len, bit_offset, bit_len, flags | BITF_SIGNED, (uint64_t)value);
}

int bitf_fields_check(const bitf_field_t *fields, size_t fields_len,
                               size_t *out_min_data_len)
{
    size_t min_len = 0;
    size_t i;

    if ((fields == NULL && fields_len > 0u) || out_min_data_len == NULL)
        return BITF_ERR_ARG;
    for (i = 0; i < fields_len; i++) {
        const bitf_field_t *f = &fields[i];
        size_t first_byte = f->bit_offset / 8u;
        size_t nbytes = (f->bit_offset % 8u + f->bit_len + 7u) / 8u;

        if (f->bit_len == 0u || f->bit_len > BITF_MAX_BITS
            || (f->flags & ~(unsigned)BITF_FLAGS_MASK) != 0u)
            return BITF_ERR_ARG;
        if (first_byte > SIZE_MAX - nbytes)
            return BITF_ERR_BOUNDS;
        if (first_byte + nbytes > min_len)
            min_len = first_byte + nbytes;
    }
    *out_min_data_len = min_len;
    return BITF_OK;
}

int bitf_get_many(const unsigned char *data, size_t data_len,
                           const bitf_field_t *fields, size_t fields_len, uint64_t *out_values)
{
    size_t min_len;
    size_t i;
    int status;

    if ((data == NULL || out_values == NULL) && fields_len > 0u)
        return BITF_ERR_ARG;
    status = bitf_fields_check(fields, fields_len, &min_len);
    if (status != BITF_OK)
        return status;
    if (min_len > data_len)
        return BITF_ERR_BOUNDS;
    for (i = 0; i < fields_len; i++)
        out_values[i] = bitf_get_unchecked(data, fields[i].bit_offset, fields[i].bit_len,
                                           fields[i].flags);
    return BITF_OK;
}

int bitf_set_many(unsigned char *data, size_t data_len, const bitf_field_t *fields,
                           size_t fields_len, const uint64_t *values)
{
    size_t min_len;
    size_t i;
    int status;

    if ((data == NULL || values == NULL) && fields_len > 0u)
        return BITF_ERR_ARG;
    status = bitf_fields_check(fields, fields_len, &min_len);
    if (status != BITF_OK)
        return status;
    if (min_len > data_len)
        return BITF_ERR_BOUNDS;
    for (i = 0; i < fields_len; i++)
        if (!bitf_fits(values[i], fields[i].bit_len, fields[i].flags))
            return BITF_ERR_RANGE;
    for (i = 0; i < fields_len; i++)
        bitf_store(data, fields[i].bit_offset, fields[i].bit_len, fields[i].flags, values[i]);
    return BITF_OK;
}

const char *bitf_strerror(int status)
{
    switch (status) {
    case BITF_OK:
        return "ok";
    case BITF_ERR_ARG:
        return "invalid argument";
    case BITF_ERR_BOUNDS:
        return "field outside the buffer";
    case BITF_ERR_RANGE:
        return "value does not fit the field";
    default:
        return "unknown status";
    }
}
#endif
