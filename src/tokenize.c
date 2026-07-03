#include "hidp_internal.h"

static int hidp_read_short(const unsigned char *p, unsigned data_len, hidp_item_t *item)
{
    uint32_t u = 0;
    unsigned i;

    for (i = 0; i < data_len; i++)
        u |= (uint32_t)p[i] << (8u * i);
    item->udata = u;
    if (data_len == 1u)
        item->sdata = (int32_t)(int8_t)(uint8_t)u;
    else if (data_len == 2u)
        item->sdata = (int32_t)(int16_t)(uint16_t)u;
    else if (data_len == 4u)
        item->sdata = u <= (uint32_t)INT32_MAX ? (int32_t)u : -(int32_t)(~u) - 1;
    else
        item->sdata = 0;
    return HIDP_OK;
}

HIDP_INTERN int hidp_next_item(const unsigned char *desc, size_t desc_len, size_t offset,
                          hidp_item_t *item)
{
    unsigned char b = desc[offset];
    size_t left = desc_len - offset - 1u;

    item->offset = offset;
    item->type = (b >> 2) & 3u;
    item->tag = (unsigned)(b >> 4);
    item->udata = 0;
    item->sdata = 0;
    if (item->tag == 0xfu) {
        if (left < 2u || desc[offset + 1u] > left - 2u)
            return HIDP_ERR_TRUNCATED;
        item->type = HIDP_ITEM_LONG;
        item->data_len = desc[offset + 1u];
        item->tag = desc[offset + 2u];
        item->data = desc + offset + 3u;
        item->len = 3u + item->data_len;
        return HIDP_OK;
    }
    item->data_len = (b & 3u) == 3u ? 4u : (b & 3u);
    if (item->data_len > left)
        return HIDP_ERR_TRUNCATED;
    item->data = desc + offset + 1u;
    item->len = 1u + item->data_len;
    return hidp_read_short(item->data, item->data_len, item);
}

int hidp_tokenize(const unsigned char *desc, size_t desc_len, hidp_item_fn item_fn,
                  void *item_ctx, size_t *out_error_offset)
{
    size_t offset = 0;
    hidp_item_t item;
    int status;

    if ((desc == NULL && desc_len > 0u) || item_fn == NULL)
        return HIDP_ERR_ARG;
    while (offset < desc_len) {
        status = hidp_next_item(desc, desc_len, offset, &item);
        if (status == HIDP_OK && item_fn(item_ctx, &item) != 0)
            status = HIDP_ERR_STOPPED;
        if (status != HIDP_OK) {
            if (out_error_offset != NULL)
                *out_error_offset = offset;
            return status;
        }
        offset += item.len;
    }
    return HIDP_OK;
}

static unsigned hidp_value_size(int64_t value)
{
    if (value == 0)
        return 0u;
    if (value >= -128 && value <= 255)
        return 1u;
    if (value >= -32768 && value <= 65535)
        return 2u;
    return 4u;
}

static int hidp_value_fits(int64_t value, unsigned size)
{
    if (size == 1u)
        return value >= -128 && value <= 127;
    if (size == 2u)
        return value >= -32768 && value <= 32767;
    return value >= INT32_MIN && value <= INT32_MAX;
}

static unsigned hidp_signed_size(int64_t value)
{
    unsigned size = hidp_value_size(value);

    while (size > 0u && size < 4u && !hidp_value_fits(value, size))
        size *= 2u;
    return size;
}

static int hidp_tag_signed(unsigned type, unsigned tag)
{
    return type == HIDP_ITEM_GLOBAL
           && (tag == HIDP_GLOBAL_LOGICAL_MIN || tag == HIDP_GLOBAL_LOGICAL_MAX
               || tag == HIDP_GLOBAL_PHYSICAL_MIN || tag == HIDP_GLOBAL_PHYSICAL_MAX
               || tag == HIDP_GLOBAL_UNIT_EXPONENT);
}

int hidp_item_write(unsigned char *buf, size_t buf_len, unsigned type, unsigned tag,
                    int64_t value, size_t *out_len)
{
    unsigned size;
    unsigned i;

    if (buf == NULL || out_len == NULL || type > HIDP_ITEM_LOCAL || tag > 15u)
        return HIDP_ERR_ARG;
    if (value < INT32_MIN || value > (int64_t)UINT32_MAX
        || (value < 0 && !hidp_tag_signed(type, tag)))
        return HIDP_ERR_RANGE;
    size = hidp_tag_signed(type, tag) ? hidp_signed_size(value) : hidp_value_size(value);
    if (buf_len < 1u + size)
        return HIDP_ERR_BOUNDS;
    buf[0] = (unsigned char)(tag << 4 | type << 2 | (size == 4u ? 3u : size));
    for (i = 0; i < size; i++)
        buf[1u + i] = (unsigned char)((uint64_t)value >> (8u * i));
    *out_len = 1u + size;
    return HIDP_OK;
}
