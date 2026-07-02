#include "hid-parser/hid-parser.h"

#include <stdlib.h>
#include <string.h>

static int count_item(void *ctx, const hidp_item_t *item)
{
    (void)item;
    (*(size_t *)ctx)++;
    return 0;
}

static int check_value(void *ctx, const hidp_value_t *v)
{
    size_t found_field;
    size_t found_index;

    if (v->index >= v->field->report_count)
        abort();
    if ((v->field->flags & HIDP_FLAG_VARIABLE)
        && hidp_desc_find_usage(ctx, v->field->report_type, v->usage, 0, &found_field,
                                &found_index) != HIDP_OK)
        abort();
    return 0;
}

static void walk_field(const hidp_desc_t *desc, const hidp_field_t *f, const uint8_t *data,
                       size_t size)
{
    const hidp_report_t *r = hidp_desc_find_report(desc, f->report_type, f->report_id);
    unsigned char *buf;
    uint32_t usage;
    int32_t value;
    size_t i;
    int status;

    if (r == NULL)
        abort();
    if (f->report_size * (size_t)f->report_count + f->bit_offset > r->byte_len * 8u)
        abort();
    if (f->usage_count > 0u && hidp_field_usage(f, f->report_count, &usage) != HIDP_OK)
        abort();
    if (r->byte_len > 4096u || (buf = malloc(r->byte_len + 1u)) == NULL)
        return;
    if (hidp_report_init(r, buf, r->byte_len) != HIDP_OK)
        abort();
    for (i = 0; i < r->byte_len && i < size; i++)
        buf[i] = data[i];
    if (r->id != 0u)
        buf[0] = (unsigned char)r->id;
    for (i = 0; i < f->report_count && i < 64u; i++) {
        if (hidp_field_get(f, buf, r->byte_len, i, &value) != HIDP_OK)
            continue;
        if (hidp_field_set(f, buf, r->byte_len, i, value) != HIDP_OK)
            abort();
        hidp_field_array_usage(f, value, &usage);
    }
    status = hidp_report_decode(desc, r->type, buf, r->byte_len, check_value, (void *)desc);
    if (status != HIDP_OK && r->id != 0u)
        abort();
    free(buf);
}

static void parse(const uint8_t *data, size_t size, unsigned flags)
{
    hidp_desc_t *desc = NULL;
    size_t i;

    if (hidp_desc_parse(data, size, flags, NULL, NULL, NULL, &desc, NULL) != HIDP_OK)
        return;
    for (i = 0; i < hidp_desc_field_count(desc); i++)
        walk_field(desc, hidp_desc_field(desc, i), data, size);
    for (i = 0; i < hidp_desc_collection_count(desc); i++)
        if (hidp_desc_collection(desc, i)->parent != HIDP_NONE
            && hidp_desc_collection(desc, i)->parent >= i)
            abort();
    hidp_desc_destroy(desc);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    size_t items = 0;

    hidp_tokenize(data, size, count_item, &items, NULL);
    if (items > size)
        abort();
    parse(data, size, 0);
    parse(data, size, HIDP_PARSE_STRICT);
    parse(data, size, HIDP_PARSE_TOLERANT);
    return 0;
}
