#include "hidp_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct hidp_globals {
    uint32_t usage_page;
    int32_t logical_min;
    int32_t logical_max;
    int32_t physical_min;
    int32_t physical_max;
    int32_t unit_exponent;
    uint32_t unit;
    uint32_t report_size;
    uint32_t report_count;
    uint32_t report_id;
} hidp_globals_t;

typedef struct hidp_parser {
    hidp_desc_t *desc;
    unsigned flags;
    size_t offset;
    hidp_globals_t g;
    hidp_globals_t stack[HIDP_TOLERANT_GLOBAL_STACK];
    unsigned stack_len;
    hidp_vec_t small;
    hidp_vec_t open;
    hidp_vec_t starts;
    size_t local_start;
    size_t local_usages;
    uint32_t usage_min;
    int have_usage_min;
    unsigned delimiter_depth;
    unsigned delimiter_branch;
    int saw_id;
    int saw_no_id;
} hidp_parser_t;

static void *hidp_default_alloc(void *ctx, size_t size)
{
    (void)ctx;
    return malloc(size);
}

static void hidp_default_free(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

int hidp_vec_push(hidp_desc_t *desc, hidp_vec_t *vec, size_t elem_size, const void *elem)
{
    if (vec->len == vec->cap) {
        size_t cap = vec->cap ? vec->cap * 2u : 8u;
        void *data;

        if (cap > (size_t)-1 / elem_size)
            return HIDP_ERR_NOMEM;
        data = desc->alloc_fn(desc->alloc_ctx, cap * elem_size);
        if (data == NULL)
            return HIDP_ERR_NOMEM;
        if (vec->len > 0u)
            memcpy(data, vec->data, vec->len * elem_size);
        if (vec->data != NULL)
            desc->free_fn(desc->alloc_ctx, vec->data);
        vec->data = data;
        vec->cap = cap;
    }
    memcpy((unsigned char *)vec->data + vec->len * elem_size, elem, elem_size);
    vec->len++;
    return HIDP_OK;
}

void hidp_vec_free(hidp_desc_t *desc, hidp_vec_t *vec)
{
    if (vec->data != NULL)
        desc->free_fn(desc->alloc_ctx, vec->data);
    vec->data = NULL;
    vec->len = 0;
    vec->cap = 0;
}

size_t hidp_report_bytes(size_t bit_len, unsigned id)
{
    return (bit_len + 7u) / 8u + (id != 0u ? 1u : 0u);
}

static int hidp_warn(hidp_parser_t *p, int code)
{
    hidp_warning_t w;

    w.offset = p->offset;
    w.code = code;
    return hidp_vec_push(p->desc, &p->desc->warnings, sizeof(w), &w);
}

static int hidp_recover(hidp_parser_t *p, int code)
{
    if (!(p->flags & HIDP_PARSE_TOLERANT))
        return code;
    return hidp_warn(p, code);
}

static hidp_usage_range_t *hidp_ranges(hidp_parser_t *p)
{
    return (hidp_usage_range_t *)p->desc->ranges.data;
}

static int hidp_push_range(hidp_parser_t *p, uint32_t min, uint32_t max, unsigned char small)
{
    hidp_usage_range_t r;
    int status;

    r.min = min;
    r.max = max;
    status = hidp_vec_push(p->desc, &p->desc->ranges, sizeof(r), &r);
    if (status == HIDP_OK)
        status = hidp_vec_push(p->desc, &p->small, 1u, &small);
    return status;
}

static int hidp_add_usages(hidp_parser_t *p, uint32_t lo, uint32_t hi, unsigned data_len)
{
    uint32_t page = (p->g.usage_page & 0xffffu) << 16;
    int status;

    if (data_len > 2u)
        return hidp_push_range(p, lo, hi, 0);
    while (1) {
        uint32_t block_end = lo | 0xffffu;
        uint32_t end = hi < block_end ? hi : block_end;

        status = hidp_push_range(p, page | (lo & 0xffffu), page | (end & 0xffffu), 1);
        if (status != HIDP_OK || end == hi)
            return status;
        lo = end + 1u;
    }
}

static int hidp_local_usage(hidp_parser_t *p, const hidp_item_t *item)
{
    int status;

    if (p->local_usages >= HIDP_MAX_USAGES)
        return hidp_recover(p, HIDP_ERR_USAGES);
    status = hidp_add_usages(p, item->udata, item->udata, item->data_len);
    if (status == HIDP_OK)
        p->local_usages++;
    return status;
}

static int hidp_clamp_usage_max(hidp_parser_t *p, uint32_t *hi)
{
    uint32_t index = (uint32_t)p->local_usages;
    int status;

    if ((uint32_t)(*hi - p->usage_min + index) < HIDP_MAX_USAGES)
        return HIDP_OK;
    status = hidp_warn(p, HIDP_ERR_USAGES);
    if (status != HIDP_OK)
        return status;
    *hi = HIDP_MAX_USAGES - index + p->usage_min - 1u;
    if (*hi != 0u)
        return HIDP_OK;
    status = hidp_recover(p, HIDP_ERR_USAGES);
    return status != HIDP_OK ? status : -1;
}

static int hidp_local_usage_max(hidp_parser_t *p, const hidp_item_t *item)
{
    uint32_t hi = item->udata;
    uint64_t count;
    int status;

    if ((p->flags & HIDP_PARSE_STRICT) && (!p->have_usage_min || p->usage_min > hi))
        return HIDP_ERR_VALUE;
    p->have_usage_min = 0;
    status = hidp_clamp_usage_max(p, &hi);
    if (status < 0)
        return HIDP_OK;
    if (status != HIDP_OK || p->usage_min > hi)
        return status;
    count = (uint64_t)hi - p->usage_min + 1u;
    if (count > HIDP_MAX_USAGES - p->local_usages) {
        status = hidp_recover(p, HIDP_ERR_USAGES);
        if (status != HIDP_OK || p->local_usages == HIDP_MAX_USAGES)
            return status;
        hi = p->usage_min + (uint32_t)(HIDP_MAX_USAGES - p->local_usages) - 1u;
        count = (uint64_t)hi - p->usage_min + 1u;
    }
    status = hidp_add_usages(p, p->usage_min, hi, item->data_len);
    if (status == HIDP_OK)
        p->local_usages += (size_t)count;
    return status;
}

static int hidp_local_delimiter(hidp_parser_t *p, const hidp_item_t *item)
{
    if (item->udata != 0u) {
        if (p->delimiter_depth != 0u)
            return hidp_recover(p, HIDP_ERR_DELIMITER);
        p->delimiter_depth++;
        p->delimiter_branch++;
        return HIDP_OK;
    }
    if (p->delimiter_depth < 1u)
        return hidp_recover(p, HIDP_ERR_DELIMITER);
    p->delimiter_depth--;
    return HIDP_OK;
}

static int hidp_parse_local(hidp_parser_t *p, const hidp_item_t *item)
{
    if (item->tag == HIDP_LOCAL_DELIMITER)
        return hidp_local_delimiter(p, item);
    if (item->tag != HIDP_LOCAL_USAGE && item->tag != HIDP_LOCAL_USAGE_MIN
        && item->tag != HIDP_LOCAL_USAGE_MAX)
        return HIDP_OK;
    if (p->delimiter_branch > 1u)
        return HIDP_OK;
    if (item->tag == HIDP_LOCAL_USAGE)
        return hidp_local_usage(p, item);
    if (item->tag == HIDP_LOCAL_USAGE_MIN) {
        p->usage_min = item->udata;
        p->have_usage_min = 1;
        return HIDP_OK;
    }
    return hidp_local_usage_max(p, item);
}

static int hidp_signed_max(hidp_parser_t *p, const hidp_item_t *item, int32_t min, int32_t *out)
{
    if (min < 0) {
        *out = item->sdata;
        return HIDP_OK;
    }
    if ((p->flags & HIDP_PARSE_STRICT) && item->sdata < 0)
        return HIDP_ERR_VALUE;
    *out = (int32_t)item->udata;
    return HIDP_OK;
}

static int hidp_global_stack(hidp_parser_t *p, unsigned tag)
{
    unsigned limit = (p->flags & HIDP_PARSE_TOLERANT) ? HIDP_TOLERANT_GLOBAL_STACK
                                                      : HIDP_GLOBAL_STACK;

    if (tag == HIDP_GLOBAL_PUSH) {
        if (p->stack_len == limit)
            return HIDP_ERR_STACK;
        p->stack[p->stack_len++] = p->g;
        return HIDP_OK;
    }
    if (p->stack_len == 0u)
        return hidp_recover(p, HIDP_ERR_STACK);
    p->g = p->stack[--p->stack_len];
    return HIDP_OK;
}

static int hidp_global_limit(hidp_parser_t *p, uint32_t value, uint32_t limit, uint32_t *out)
{
    int status;

    if (value > limit) {
        if (!(p->flags & HIDP_PARSE_TOLERANT) || value > HIDP_TOLERANT_LIMIT)
            return HIDP_ERR_VALUE;
        status = hidp_warn(p, HIDP_ERR_VALUE);
        if (status != HIDP_OK)
            return status;
    }
    *out = value;
    return HIDP_OK;
}

static int hidp_global_report_id(hidp_parser_t *p, uint32_t id)
{
    if (id == 0u || id > HIDP_MAX_REPORT_ID)
        return hidp_recover(p, HIDP_ERR_VALUE);
    p->g.report_id = id;
    return HIDP_OK;
}

static int32_t hidp_unit_exponent(const hidp_item_t *item)
{
    int32_t raw = item->sdata;

    if (((uint32_t)raw & 0xfffffff0u) == 0u)
        return (raw & 0x8) ? raw - 16 : raw;
    return raw;
}

static int hidp_parse_global(hidp_parser_t *p, const hidp_item_t *item)
{
    switch (item->tag) {
    case HIDP_GLOBAL_PUSH:
    case HIDP_GLOBAL_POP:
        return hidp_global_stack(p, item->tag);
    case HIDP_GLOBAL_USAGE_PAGE:
        p->g.usage_page = item->udata;
        return HIDP_OK;
    case HIDP_GLOBAL_LOGICAL_MIN:
        p->g.logical_min = item->sdata;
        return HIDP_OK;
    case HIDP_GLOBAL_LOGICAL_MAX:
        return hidp_signed_max(p, item, p->g.logical_min, &p->g.logical_max);
    case HIDP_GLOBAL_PHYSICAL_MIN:
        p->g.physical_min = item->sdata;
        return HIDP_OK;
    case HIDP_GLOBAL_PHYSICAL_MAX:
        return hidp_signed_max(p, item, p->g.physical_min, &p->g.physical_max);
    case HIDP_GLOBAL_UNIT_EXPONENT:
        p->g.unit_exponent = hidp_unit_exponent(item);
        return HIDP_OK;
    case HIDP_GLOBAL_UNIT:
        p->g.unit = item->udata;
        return HIDP_OK;
    case HIDP_GLOBAL_REPORT_SIZE:
        return hidp_global_limit(p, item->udata, HIDP_MAX_REPORT_SIZE, &p->g.report_size);
    case HIDP_GLOBAL_REPORT_COUNT:
        return hidp_global_limit(p, item->udata, HIDP_MAX_USAGES, &p->g.report_count);
    case HIDP_GLOBAL_REPORT_ID:
        return hidp_global_report_id(p, item->udata);
    default:
        return hidp_recover(p, HIDP_ERR_UNKNOWN_ITEM);
    }
}

static void hidp_concatenate_usages(hidp_parser_t *p)
{
    hidp_usage_range_t *r = hidp_ranges(p);
    const unsigned char *small = (const unsigned char *)p->small.data;
    uint32_t page = p->g.usage_page;
    size_t i = p->desc->ranges.len;

    while (i > p->local_start) {
        i--;
        if (!small[i])
            continue;
        if ((r[i].min >> 16) == page)
            break;
        r[i].min = (r[i].min & 0xffffu) | ((page & 0xffffu) << 16);
        r[i].max = (r[i].max & 0xffffu) | ((page & 0xffffu) << 16);
    }
}

static size_t hidp_open_top(const hidp_parser_t *p)
{
    if (p->open.len == 0u)
        return HIDP_NONE;
    return ((const size_t *)p->open.data)[p->open.len - 1u];
}

static int hidp_open_collection(hidp_parser_t *p, uint32_t data)
{
    hidp_collection_t c;
    size_t index = p->desc->collections.len;
    int status;

    c.type = data & 0xffu;
    c.usage = p->desc->ranges.len > p->local_start ? hidp_ranges(p)[p->local_start].min : 0u;
    c.parent = hidp_open_top(p);
    c.depth = (unsigned)p->open.len;
    c.desc_offset = p->offset;
    status = hidp_vec_push(p->desc, &p->desc->collections, sizeof(c), &c);
    if (status == HIDP_OK)
        status = hidp_vec_push(p->desc, &p->open, sizeof(index), &index);
    return status;
}

static int hidp_close_collection(hidp_parser_t *p)
{
    if (p->open.len == 0u)
        return hidp_recover(p, HIDP_ERR_COLLECTION);
    p->open.len--;
    return HIDP_OK;
}

static hidp_report_t *hidp_get_report(hidp_parser_t *p, unsigned type, unsigned id)
{
    hidp_report_t *reports = (hidp_report_t *)p->desc->reports.data;
    hidp_report_t r;
    size_t i;

    for (i = 0; i < p->desc->reports.len; i++)
        if (reports[i].type == type && reports[i].id == id)
            return &reports[i];
    r.type = type;
    r.id = id;
    r.bit_len = 0;
    r.byte_len = 0;
    if (hidp_vec_push(p->desc, &p->desc->reports, sizeof(r), &r) != HIDP_OK)
        return NULL;
    return (hidp_report_t *)p->desc->reports.data + p->desc->reports.len - 1u;
}

static int hidp_check_logical(hidp_parser_t *p)
{
    int32_t min = p->g.logical_min;
    int32_t max = p->g.logical_max;

    if ((min < 0 && max < min) || (min >= 0 && (uint32_t)max < (uint32_t)min))
        return hidp_recover(p, HIDP_ERR_RANGE);
    return HIDP_OK;
}

static int hidp_grow_report(hidp_parser_t *p, hidp_report_t *report, size_t *out_offset)
{
    uint64_t add = (uint64_t)p->g.report_size * p->g.report_count;
    uint64_t total = (uint64_t)report->bit_len + add;

    if (total > (uint64_t)((size_t)-1 / 16u))
        return HIDP_ERR_TOO_LONG;
    *out_offset = report->bit_len;
    report->bit_len = (size_t)total;
    if (total > (uint64_t)(HIDP_MAX_REPORT_BYTES - 1u) * 8u)
        return hidp_recover(p, HIDP_ERR_TOO_LONG);
    return HIDP_OK;
}

static void hidp_fill_field(const hidp_parser_t *p, unsigned type, unsigned flags,
                            hidp_field_t *f)
{
    memset(f, 0, sizeof(*f));
    f->report_type = type;
    f->report_id = p->g.report_id;
    f->report_size = p->g.report_size;
    f->report_count = p->g.report_count;
    f->flags = flags;
    f->logical_min = p->g.logical_min;
    f->logical_max = p->g.logical_max;
    f->physical_min = p->g.physical_min;
    f->physical_max = p->g.physical_max;
    f->unit_exponent = p->g.unit_exponent;
    f->unit = p->g.unit;
    f->usage_page = p->g.usage_page;
    f->usages_len = p->desc->ranges.len - p->local_start;
    f->usage_count = p->local_usages;
    f->collection = hidp_open_top(p);
    f->desc_offset = p->offset;
}

static int hidp_add_field(hidp_parser_t *p, unsigned type, uint32_t data)
{
    hidp_report_t *report = hidp_get_report(p, type, p->g.report_id);
    hidp_field_t f;
    size_t offset;
    int status;

    if (report == NULL)
        return HIDP_ERR_NOMEM;
    status = hidp_check_logical(p);
    if (status == HIDP_OK)
        status = hidp_grow_report(p, report, &offset);
    if (status != HIDP_OK)
        return status;
    hidp_fill_field(p, type, data & 0x1ffu, &f);
    f.bit_offset = offset + (p->g.report_id != 0u ? 8u : 0u);
    if (p->g.report_id != 0u)
        p->saw_id = 1;
    else
        p->saw_no_id = 1;
    status = hidp_vec_push(p->desc, &p->desc->fields, sizeof(f), &f);
    if (status == HIDP_OK)
        status = hidp_vec_push(p->desc, &p->starts, sizeof(p->local_start), &p->local_start);
    if (status == HIDP_OK)
        p->local_start = p->desc->ranges.len;
    return status;
}

static int hidp_main_item(hidp_parser_t *p, const hidp_item_t *item)
{
    switch (item->tag) {
    case HIDP_MAIN_COLLECTION:
        return hidp_open_collection(p, item->udata);
    case HIDP_MAIN_END_COLLECTION:
        return hidp_close_collection(p);
    case HIDP_MAIN_INPUT:
        return hidp_add_field(p, HIDP_INPUT, item->udata);
    case HIDP_MAIN_OUTPUT:
        return hidp_add_field(p, HIDP_OUTPUT, item->udata);
    case HIDP_MAIN_FEATURE:
        return hidp_add_field(p, HIDP_FEATURE, item->udata);
    default:
        return hidp_warn(p, HIDP_ERR_UNKNOWN_ITEM);
    }
}

static void hidp_reset_locals(hidp_parser_t *p)
{
    p->desc->ranges.len = p->local_start;
    p->small.len = p->local_start;
    p->local_usages = 0;
    p->usage_min = 0;
    p->have_usage_min = 0;
    p->delimiter_depth = 0;
    p->delimiter_branch = 0;
}

static int hidp_parse_main(hidp_parser_t *p, const hidp_item_t *item)
{
    int status;

    if ((p->flags & HIDP_PARSE_STRICT) && p->have_usage_min)
        return HIDP_ERR_VALUE;
    if (p->local_usages > 0u)
        hidp_concatenate_usages(p);
    status = hidp_main_item(p, item);
    hidp_reset_locals(p);
    return status;
}

static int hidp_parse_item(hidp_parser_t *p, const hidp_item_t *item)
{
    switch (item->type) {
    case HIDP_ITEM_MAIN:
        return hidp_parse_main(p, item);
    case HIDP_ITEM_GLOBAL:
        return hidp_parse_global(p, item);
    case HIDP_ITEM_LOCAL:
        return hidp_parse_local(p, item);
    case HIDP_ITEM_LONG:
        return hidp_recover(p, HIDP_ERR_LONG_ITEM);
    default:
        return HIDP_OK;
    }
}

static int hidp_parse_items(hidp_parser_t *p, const unsigned char *data, size_t data_len)
{
    hidp_item_t item;
    int status;

    while (p->offset < data_len) {
        status = hidp_next_item(data, data_len, p->offset, &item);
        if (status != HIDP_OK)
            return hidp_recover(p, status) != HIDP_OK ? status : HIDP_OK;
        status = hidp_parse_item(p, &item);
        if (status != HIDP_OK)
            return status;
        p->offset += item.len;
    }
    return HIDP_OK;
}

static int hidp_parse_end(hidp_parser_t *p)
{
    int status = HIDP_OK;

    if (p->open.len > 0u)
        status = hidp_recover(p, HIDP_ERR_COLLECTION);
    if (status == HIDP_OK && p->delimiter_depth > 0u)
        status = hidp_recover(p, HIDP_ERR_DELIMITER);
    if (status == HIDP_OK && (p->flags & HIDP_PARSE_STRICT) && p->have_usage_min)
        status = HIDP_ERR_VALUE;
    if (status == HIDP_OK && (p->flags & HIDP_PARSE_STRICT) && p->saw_id && p->saw_no_id)
        status = HIDP_ERR_VALUE;
    return status;
}

static void hidp_finish(hidp_parser_t *p)
{
    hidp_desc_t *desc = p->desc;
    hidp_field_t *fields = (hidp_field_t *)desc->fields.data;
    const size_t *starts = (const size_t *)p->starts.data;
    hidp_report_t *reports = (hidp_report_t *)desc->reports.data;
    size_t i;

    for (i = 0; i < desc->fields.len; i++)
        fields[i].usages = fields[i].usages_len > 0u ? hidp_ranges(p) + starts[i] : NULL;
    for (i = 0; i < desc->reports.len; i++)
        reports[i].byte_len = hidp_report_bytes(reports[i].bit_len, reports[i].id);
}

static int hidp_parse_all(hidp_parser_t *p, const unsigned char *data, size_t data_len)
{
    int status;

    if (data_len == 0u)
        return HIDP_ERR_EMPTY;
    status = hidp_parse_items(p, data, data_len);
    if (status != HIDP_OK)
        return status;
    p->offset = data_len;
    status = hidp_parse_end(p);
    hidp_reset_locals(p);
    if (status == HIDP_OK)
        hidp_finish(p);
    return status;
}

static int hidp_desc_new(hidp_alloc_fn alloc_fn, hidp_free_fn free_fn, void *alloc_ctx,
                         hidp_desc_t **out_desc)
{
    hidp_desc_t *desc;

    if (alloc_fn == NULL) {
        alloc_fn = hidp_default_alloc;
        free_fn = hidp_default_free;
    }
    desc = alloc_fn(alloc_ctx, sizeof(*desc));
    if (desc == NULL)
        return HIDP_ERR_NOMEM;
    memset(desc, 0, sizeof(*desc));
    desc->alloc_fn = alloc_fn;
    desc->free_fn = free_fn;
    desc->alloc_ctx = alloc_ctx;
    *out_desc = desc;
    return HIDP_OK;
}

int hidp_desc_parse(const unsigned char *data, size_t data_len,
                    const hidp_parse_options_t *options, hidp_desc_t **out_desc,
                    size_t *out_error_offset)
{
    static const hidp_parse_options_t defaults = {0, NULL, NULL, NULL};
    const hidp_parse_options_t *o = options != NULL ? options : &defaults;
    unsigned flags = o->flags;
    hidp_parser_t p;
    int status;

    if (out_desc == NULL || (data == NULL && data_len > 0u)
        || (o->alloc_fn == NULL) != (o->free_fn == NULL)
        || (flags & ~(HIDP_PARSE_STRICT | HIDP_PARSE_TOLERANT)) != 0u
        || flags == (HIDP_PARSE_STRICT | HIDP_PARSE_TOLERANT))
        return HIDP_ERR_ARG;
    *out_desc = NULL;
    memset(&p, 0, sizeof(p));
    p.flags = flags;
    status = hidp_desc_new(o->alloc_fn, o->free_fn, o->alloc_ctx, &p.desc);
    if (status != HIDP_OK)
        return status;
    status = hidp_parse_all(&p, data, data_len);
    hidp_vec_free(p.desc, &p.small);
    hidp_vec_free(p.desc, &p.open);
    hidp_vec_free(p.desc, &p.starts);
    if (status != HIDP_OK) {
        if (out_error_offset != NULL)
            *out_error_offset = p.offset;
        hidp_desc_destroy(p.desc);
        return status;
    }
    *out_desc = p.desc;
    return HIDP_OK;
}

void hidp_desc_destroy(hidp_desc_t *desc)
{
    if (desc == NULL)
        return;
    hidp_vec_free(desc, &desc->fields);
    hidp_vec_free(desc, &desc->ranges);
    hidp_vec_free(desc, &desc->collections);
    hidp_vec_free(desc, &desc->reports);
    hidp_vec_free(desc, &desc->warnings);
    desc->free_fn(desc->alloc_ctx, desc);
}
