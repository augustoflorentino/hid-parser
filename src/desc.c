#include "hidp_internal.h"
#include "hidp_bitspan.h"

#include <string.h>

size_t hidp_desc_field_count(const hidp_desc_t *desc)
{
    return desc != NULL ? desc->fields.len : 0u;
}

const hidp_field_t *hidp_desc_field(const hidp_desc_t *desc, size_t index)
{
    if (desc == NULL || index >= desc->fields.len)
        return NULL;
    return (const hidp_field_t *)desc->fields.data + index;
}

size_t hidp_desc_collection_count(const hidp_desc_t *desc)
{
    return desc != NULL ? desc->collections.len : 0u;
}

const hidp_collection_t *hidp_desc_collection(const hidp_desc_t *desc, size_t index)
{
    if (desc == NULL || index >= desc->collections.len)
        return NULL;
    return (const hidp_collection_t *)desc->collections.data + index;
}

size_t hidp_desc_application(const hidp_desc_t *desc, size_t collection)
{
    const hidp_collection_t *c = hidp_desc_collection(desc, collection);
    size_t found = HIDP_NONE;

    for (; c != NULL; c = hidp_desc_collection(desc, c->parent)) {
        if (c->type == HIDP_COLLECTION_APPLICATION)
            found = collection;
        collection = c->parent;
    }
    return found;
}

size_t hidp_desc_report_count(const hidp_desc_t *desc)
{
    return desc != NULL ? desc->reports.len : 0u;
}

const hidp_report_t *hidp_desc_report(const hidp_desc_t *desc, size_t index)
{
    if (desc == NULL || index >= desc->reports.len)
        return NULL;
    return (const hidp_report_t *)desc->reports.data + index;
}

size_t hidp_desc_warning_count(const hidp_desc_t *desc)
{
    return desc != NULL ? desc->warnings.len : 0u;
}

const hidp_warning_t *hidp_desc_warning(const hidp_desc_t *desc, size_t index)
{
    if (desc == NULL || index >= desc->warnings.len)
        return NULL;
    return (const hidp_warning_t *)desc->warnings.data + index;
}

const hidp_report_t *hidp_desc_find_report(const hidp_desc_t *desc, unsigned type, unsigned id)
{
    const hidp_report_t *r;
    size_t i;

    for (i = 0; i < hidp_desc_report_count(desc); i++) {
        r = hidp_desc_report(desc, i);
        if (r->type == type && r->id == id)
            return r;
    }
    return NULL;
}

static int hidp_type_numbered(const hidp_desc_t *desc, unsigned type)
{
    const hidp_report_t *r;
    size_t i;

    for (i = 0; i < hidp_desc_report_count(desc); i++) {
        r = hidp_desc_report(desc, i);
        if (r->type == type && r->id != 0u)
            return 1;
    }
    return 0;
}

const hidp_report_t *hidp_desc_match_report(const hidp_desc_t *desc, unsigned type,
                                            const unsigned char *report, size_t report_len)
{
    if (!hidp_type_numbered(desc, type))
        return hidp_desc_find_report(desc, type, 0u);
    if (report == NULL || report_len == 0u)
        return NULL;
    return hidp_desc_find_report(desc, type, report[0]);
}

int hidp_field_usage(const hidp_field_t *field, size_t index, uint32_t *out_usage)
{
    size_t i;
    size_t span;

    if (field == NULL || out_usage == NULL)
        return HIDP_ERR_ARG;
    if (field->usage_count == 0u)
        return HIDP_ERR_NOT_FOUND;
    if (index >= field->usage_count)
        index = field->usage_count - 1u;
    for (i = 0; i < field->usages_len; i++) {
        span = (size_t)(field->usages[i].max - field->usages[i].min);
        if (index <= span) {
            *out_usage = field->usages[i].min + (uint32_t)index;
            return HIDP_OK;
        }
        index -= span + 1u;
    }
    return HIDP_ERR_NOT_FOUND;
}

static int hidp_field_bits(const hidp_field_t *field, size_t report_len, size_t index,
                           size_t *out_bit)
{
    if (field->report_size == 0u || field->report_size > 32u)
        return HIDP_ERR_RANGE;
    if (index >= field->report_count)
        return HIDP_ERR_BOUNDS;
    *out_bit = field->bit_offset + index * field->report_size;
    if (bitf_check(report_len, *out_bit, field->report_size, BITF_LE) != BITF_OK)
        return HIDP_ERR_BOUNDS;
    return HIDP_OK;
}

static int hidp_id_matches(const hidp_field_t *field, const unsigned char *report)
{
    return field->report_id == 0u || report[0] == field->report_id;
}

int hidp_field_read(const hidp_field_t *field, const unsigned char *report, size_t report_len,
                    size_t index, int64_t *out_value)
{
    size_t bit;
    uint64_t uv;
    int status;

    if (field == NULL || report == NULL || out_value == NULL)
        return HIDP_ERR_ARG;
    status = hidp_field_bits(field, report_len, index, &bit);
    if (status != HIDP_OK)
        return status;
    if (!hidp_id_matches(field, report))
        return HIDP_ERR_NOT_FOUND;
    if (field->logical_min < 0)
        return bitf_get_signed(report, report_len, bit, field->report_size, BITF_LE, out_value);
    bitf_get(report, report_len, bit, field->report_size, BITF_LE, &uv);
    *out_value = (int64_t)uv;
    return HIDP_OK;
}

int hidp_field_write(const hidp_field_t *field, unsigned char *report, size_t report_len,
                     size_t index, int64_t value)
{
    size_t bit;
    int status;

    if (field == NULL || report == NULL)
        return HIDP_ERR_ARG;
    status = hidp_field_bits(field, report_len, index, &bit);
    if (status != HIDP_OK)
        return status;
    if (!hidp_id_matches(field, report))
        return HIDP_ERR_NOT_FOUND;
    if (field->logical_min < 0)
        status = bitf_set_signed(report, report_len, bit, field->report_size, BITF_LE, value);
    else if (value < 0)
        status = BITF_ERR_RANGE;
    else
        status = bitf_set(report, report_len, bit, field->report_size, BITF_LE, (uint64_t)value);
    return status == BITF_OK ? HIDP_OK : HIDP_ERR_RANGE;
}

int hidp_field_in_range(const hidp_field_t *field, int64_t value)
{
    if (field == NULL)
        return 0;
    if (field->logical_min < 0)
        return value >= field->logical_min && value <= field->logical_max;
    return value >= (int64_t)(uint32_t)field->logical_min
           && value <= (int64_t)(uint32_t)field->logical_max;
}

int hidp_field_array_usage(const hidp_field_t *field, int64_t value, uint32_t *out_usage)
{
    if (field == NULL || out_usage == NULL || (field->flags & HIDP_FLAG_VARIABLE))
        return HIDP_ERR_ARG;
    if (!hidp_field_in_range(field, value))
        return HIDP_ERR_NOT_FOUND;
    value -= field->logical_min < 0 ? field->logical_min : (int64_t)(uint32_t)field->logical_min;
    if ((uint64_t)value >= field->usage_count)
        return HIDP_ERR_NOT_FOUND;
    return hidp_field_usage(field, (size_t)value, out_usage);
}

static int hidp_decode_field(const hidp_field_t *f, size_t field_index,
                             const unsigned char *report, size_t len, hidp_value_fn fn,
                             void *ctx)
{
    hidp_value_t v;
    int status;

    if (f->report_size == 0u || f->report_size > 32u)
        return HIDP_OK;
    v.field = f;
    v.field_index = field_index;
    for (v.index = 0; v.index < f->report_count; v.index++) {
        if (hidp_field_read(f, report, len, v.index, &v.value) != HIDP_OK)
            return HIDP_ERR_BOUNDS;
        v.usage = 0;
        if (f->flags & HIDP_FLAG_VARIABLE)
            hidp_field_usage(f, v.index, &v.usage);
        else if (hidp_field_array_usage(f, v.value, &v.usage) != HIDP_OK
                 || (v.usage & 0xffffu) == 0u)
            continue;
        status = fn(ctx, &v);
        if (status != 0)
            return HIDP_ERR_STOPPED;
    }
    return HIDP_OK;
}

int hidp_report_decode(const hidp_desc_t *desc, unsigned type, const unsigned char *report,
                       size_t report_len, hidp_value_fn value_fn, void *ctx)
{
    const hidp_report_t *r;
    const hidp_field_t *f;
    size_t i;
    int status;

    if (desc == NULL || report == NULL || value_fn == NULL)
        return HIDP_ERR_ARG;
    r = hidp_desc_match_report(desc, type, report, report_len);
    if (r == NULL)
        return HIDP_ERR_NOT_FOUND;
    if (report_len < r->byte_len)
        return HIDP_ERR_BOUNDS;
    for (i = 0; i < desc->fields.len; i++) {
        f = hidp_desc_field(desc, i);
        if (f->report_type != type || f->report_id != r->id || f->usage_count == 0u
            || (f->flags & HIDP_FLAG_CONSTANT))
            continue;
        status = hidp_decode_field(f, i, report, report_len, value_fn, ctx);
        if (status != HIDP_OK)
            return status;
    }
    return HIDP_OK;
}

static int hidp_field_index_of(const hidp_field_t *f, uint32_t usage, size_t *out_index)
{
    size_t base = 0;
    size_t i;

    for (i = 0; i < f->usages_len && base < f->usage_count; i++) {
        if (usage >= f->usages[i].min && usage <= f->usages[i].max) {
            *out_index = base + (size_t)(usage - f->usages[i].min);
            return *out_index < f->usage_count;
        }
        base += (size_t)(f->usages[i].max - f->usages[i].min) + 1u;
    }
    return 0;
}

int hidp_desc_find_usage(const hidp_desc_t *desc, unsigned type, uint32_t usage,
                         size_t start_field, size_t *out_field, size_t *out_index)
{
    const hidp_field_t *f;
    size_t index;
    size_t i;

    if (desc == NULL)
        return HIDP_ERR_ARG;
    for (i = start_field; i < desc->fields.len; i++) {
        f = hidp_desc_field(desc, i);
        if (f->report_type != type || !hidp_field_index_of(f, usage, &index))
            continue;
        if ((f->flags & HIDP_FLAG_VARIABLE) && index >= f->report_count)
            continue;
        if (out_field != NULL)
            *out_field = i;
        if (out_index != NULL)
            *out_index = index;
        return HIDP_OK;
    }
    return HIDP_ERR_NOT_FOUND;
}

int hidp_report_set_usage(const hidp_desc_t *desc, unsigned type, unsigned char *report,
                          size_t report_len, uint32_t usage, int64_t value)
{
    const hidp_report_t *r;
    const hidp_field_t *f;
    size_t field = 0;
    size_t index;

    if (desc == NULL || report == NULL || report_len == 0u)
        return HIDP_ERR_ARG;
    r = hidp_desc_match_report(desc, type, report, report_len);
    if (r == NULL)
        return HIDP_ERR_NOT_FOUND;
    for (; hidp_desc_find_usage(desc, type, usage, field, &field, &index) == HIDP_OK; field++) {
        f = hidp_desc_field(desc, field);
        if (f->report_id == r->id && (f->flags & HIDP_FLAG_VARIABLE))
            return hidp_field_write(f, report, report_len, index, value);
    }
    return HIDP_ERR_NOT_FOUND;
}

int hidp_report_init(const hidp_report_t *report, unsigned char *buf, size_t buf_len)
{
    if (report == NULL || buf == NULL)
        return HIDP_ERR_ARG;
    if (buf_len < report->byte_len)
        return HIDP_ERR_BOUNDS;
    memset(buf, 0, report->byte_len);
    if (report->id != 0u)
        buf[0] = (unsigned char)report->id;
    return HIDP_OK;
}
