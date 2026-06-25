#include "hid-parser/hid-parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                    \
        }                                                               \
    } while (0)

#define PARSE(bytes, flags, out) parse_bytes(bytes, sizeof(bytes), flags, out)

typedef struct {
    int remaining;
    int live;
} failing_alloc_t;

static void *failing_alloc(void *ctx, size_t size)
{
    failing_alloc_t *fa = ctx;

    if (fa->remaining-- == 0)
        return NULL;
    fa->live++;
    return malloc(size);
}

static void failing_free(void *ctx, void *ptr)
{
    failing_alloc_t *fa = ctx;

    fa->live--;
    free(ptr);
}

static int parse_bytes(const unsigned char *bytes, size_t len, unsigned flags,
                       hidp_desc_t **out)
{
    *out = NULL;
    return hidp_desc_parse(bytes, len, flags, NULL, NULL, NULL, out, NULL);
}

static uint32_t usage_at(const hidp_field_t *f, size_t index)
{
    uint32_t usage = 0;

    CHECK(hidp_field_usage(f, index, &usage) == HIDP_OK);
    return usage;
}

static const unsigned char mouse[] = {
    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x03,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7f,
    0x75, 0x08, 0x95, 0x02, 0x81, 0x06,
    0xc0, 0xc0,
};

typedef struct {
    hidp_item_t items[64];
    size_t len;
    size_t stop_at;
} item_log_t;

static int log_item(void *ctx, const hidp_item_t *item)
{
    item_log_t *log = ctx;

    if (log->len == log->stop_at)
        return 1;
    log->items[log->len++] = *item;
    return 0;
}

static void test_tokenize(void)
{
    static const unsigned char bytes[] = {0x15, 0x81, 0x26, 0xff, 0x7f, 0x17, 0x00, 0x00,
                                          0x00, 0x80, 0xc0, 0xfe, 0x02, 0x10, 0xaa, 0xbb};
    item_log_t log;
    size_t err = 0;

    memset(&log, 0, sizeof(log));
    log.stop_at = (size_t)-1;
    CHECK(hidp_tokenize(bytes, sizeof(bytes), log_item, &log, NULL) == HIDP_OK);
    CHECK(log.len == 5);
    CHECK(log.items[0].type == HIDP_ITEM_GLOBAL && log.items[0].tag == HIDP_GLOBAL_LOGICAL_MIN);
    CHECK(log.items[0].sdata == -127 && log.items[0].udata == 0x81);
    CHECK(log.items[1].data_len == 2 && log.items[1].sdata == 32767);
    CHECK(log.items[2].data_len == 4 && log.items[2].sdata == INT32_MIN);
    CHECK(log.items[3].type == HIDP_ITEM_MAIN && log.items[3].data_len == 0);
    CHECK(log.items[4].type == HIDP_ITEM_LONG && log.items[4].tag == 0x10);
    CHECK(log.items[4].data_len == 2 && log.items[4].data[1] == 0xbb && log.items[4].offset == 11);
    CHECK(hidp_tokenize(bytes, 4, log_item, &log, &err) == HIDP_ERR_TRUNCATED && err == 2);
    CHECK(hidp_tokenize(bytes, 13, log_item, &log, &err) == HIDP_ERR_TRUNCATED && err == 11);
    memset(&log, 0, sizeof(log));
    log.stop_at = 2;
    CHECK(hidp_tokenize(bytes, sizeof(bytes), log_item, &log, &err) == HIDP_ERR_STOPPED);
    CHECK(err == 5 && log.len == 2);
    CHECK(hidp_tokenize(NULL, 0, log_item, &log, NULL) == HIDP_OK);
    CHECK(hidp_tokenize(NULL, 1, log_item, &log, NULL) == HIDP_ERR_ARG);
    CHECK(hidp_tokenize(bytes, 1, NULL, NULL, NULL) == HIDP_ERR_ARG);
}

static void test_mouse(void)
{
    const hidp_field_t *f;
    const hidp_report_t *r;
    const hidp_collection_t *c;
    hidp_desc_t *d;

    CHECK(PARSE(mouse, 0, &d) == HIDP_OK);
    CHECK(hidp_desc_field_count(d) == 3 && hidp_desc_report_count(d) == 1);
    CHECK(hidp_desc_warning_count(d) == 0);
    r = hidp_desc_report(d, 0);
    CHECK(r->type == HIDP_INPUT && r->id == 0 && r->bit_len == 24 && r->byte_len == 3);
    f = hidp_desc_field(d, 0);
    CHECK(f->bit_offset == 0 && f->report_size == 1 && f->report_count == 3);
    CHECK(f->usages_len == 1 && f->usage_count == 3 && f->collection == 1);
    CHECK(usage_at(f, 0) == 0x00090001 && usage_at(f, 2) == 0x00090003);
    f = hidp_desc_field(d, 1);
    CHECK(f->bit_offset == 3 && f->usage_count == 0 && f->usages == NULL);
    CHECK(f->flags == (HIDP_FLAG_CONSTANT | HIDP_FLAG_VARIABLE));
    f = hidp_desc_field(d, 2);
    CHECK(f->bit_offset == 8 && f->logical_min == -127 && f->logical_max == 127);
    CHECK(f->flags == (HIDP_FLAG_VARIABLE | HIDP_FLAG_RELATIVE));
    CHECK(usage_at(f, 1) == 0x00010031);
    CHECK(hidp_desc_collection_count(d) == 2);
    c = hidp_desc_collection(d, 0);
    CHECK(c->type == HIDP_COLLECTION_APPLICATION && c->usage == 0x00010002);
    CHECK(c->parent == HIDP_NONE && c->depth == 0 && c->desc_offset == 4);
    c = hidp_desc_collection(d, 1);
    CHECK(c->type == HIDP_COLLECTION_PHYSICAL && c->usage == 0x00010001 && c->parent == 0);
    CHECK(hidp_desc_field(d, 3) == NULL && hidp_desc_collection(d, 2) == NULL);
    CHECK(hidp_desc_report(d, 1) == NULL && hidp_desc_warning(d, 0) == NULL);
    hidp_desc_destroy(d);
}

static void test_report_ids(void)
{
    static const unsigned char bytes[] = {
        0x05, 0x01, 0x09, 0x04, 0xa1, 0x01,
        0x85, 0x02, 0x09, 0x30, 0x75, 0x08, 0x95, 0x01, 0x26, 0xff, 0x00, 0x81, 0x02,
        0x85, 0x05, 0x09, 0x31, 0x75, 0x10, 0x81, 0x02,
        0x85, 0x02, 0x09, 0x32, 0x75, 0x04, 0x91, 0x02, 0xc0,
    };
    static const unsigned char in5[] = {5, 0x34, 0x12};
    const hidp_report_t *r;
    hidp_desc_t *d;

    CHECK(PARSE(bytes, 0, &d) == HIDP_OK);
    CHECK(hidp_desc_report_count(d) == 3);
    CHECK(hidp_desc_field(d, 0)->report_id == 2 && hidp_desc_field(d, 0)->bit_offset == 8);
    CHECK(hidp_desc_field(d, 1)->report_id == 5 && hidp_desc_field(d, 1)->bit_offset == 8);
    CHECK(hidp_desc_field(d, 1)->logical_max == 255);
    r = hidp_desc_find_report(d, HIDP_INPUT, 5);
    CHECK(r != NULL && r->byte_len == 3 && r->bit_len == 16);
    r = hidp_desc_find_report(d, HIDP_OUTPUT, 2);
    CHECK(r != NULL && r->byte_len == 2);
    CHECK(hidp_desc_find_report(d, HIDP_FEATURE, 2) == NULL);
    CHECK(hidp_desc_match_report(d, HIDP_INPUT, in5, sizeof(in5)) == hidp_desc_find_report(d, HIDP_INPUT, 5));
    CHECK(hidp_desc_match_report(d, HIDP_INPUT, in5, 0) == NULL);
    hidp_desc_destroy(d);
}

static void test_usage_pages(void)
{
    static const unsigned char bytes[] = {
        0x09, 0x30, 0x0b, 0x01, 0x00, 0x09, 0x00, 0x05, 0x01,
        0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
        0x05, 0x09, 0x19, 0xfe, 0x2a, 0x01, 0x01, 0x75, 0x01, 0x95, 0x04, 0x81, 0x02,
        0x29, 0x05, 0x95, 0x01, 0x81, 0x02,
    };
    const hidp_field_t *f;
    hidp_desc_t *d;

    CHECK(PARSE(bytes, 0, &d) == HIDP_OK);
    f = hidp_desc_field(d, 0);
    CHECK(f->usage_count == 2);
    CHECK(usage_at(f, 0) == 0x00010030 && usage_at(f, 1) == 0x00090001);
    CHECK(usage_at(f, 7) == 0x00090001);
    f = hidp_desc_field(d, 1);
    CHECK(f->usage_count == 4 && usage_at(f, 0) == 0x000900fe && usage_at(f, 3) == 0x00090101);
    f = hidp_desc_field(d, 2);
    CHECK(f->usage_count == 6 && usage_at(f, 0) == 0x00090000 && usage_at(f, 5) == 0x00090005);
    hidp_desc_destroy(d);
    CHECK(PARSE(bytes, HIDP_PARSE_STRICT, &d) == HIDP_ERR_VALUE);
}

static void test_globals(void)
{
    static const unsigned char unsigned_max[] = {0x15, 0x00, 0x25, 0xff, 0x35, 0x00,
                                                 0x45, 0xff, 0x55, 0x0e, 0x65, 0x11,
                                                 0x75, 0x08, 0x95, 0x01, 0x81, 0x02};
    static const unsigned char big_exp[] = {0x55, 0x1e, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02};
    static const unsigned char bad_range[] = {0x15, 0x01, 0x25, 0x00, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02};
    static const unsigned char neg_range[] = {0x15, 0xff, 0x25, 0xff, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02};
    const hidp_field_t *f;
    hidp_desc_t *d;

    CHECK(PARSE(unsigned_max, 0, &d) == HIDP_OK);
    f = hidp_desc_field(d, 0);
    CHECK(f->logical_max == 255 && f->physical_max == 255);
    CHECK(f->unit_exponent == -2 && f->unit == 0x11);
    hidp_desc_destroy(d);
    CHECK(PARSE(unsigned_max, HIDP_PARSE_STRICT, &d) == HIDP_ERR_VALUE);
    CHECK(PARSE(big_exp, 0, &d) == HIDP_OK);
    CHECK(hidp_desc_field(d, 0)->unit_exponent == 0x1e);
    hidp_desc_destroy(d);
    CHECK(PARSE(bad_range, 0, &d) == HIDP_ERR_RANGE && d == NULL);
    CHECK(PARSE(bad_range, HIDP_PARSE_TOLERANT, &d) == HIDP_OK);
    CHECK(hidp_desc_warning_count(d) == 1);
    CHECK(hidp_desc_warning(d, 0)->code == HIDP_ERR_RANGE && hidp_desc_warning(d, 0)->offset == 8);
    hidp_desc_destroy(d);
    CHECK(PARSE(neg_range, 0, &d) == HIDP_OK);
    CHECK(hidp_desc_field(d, 0)->logical_max == -1);
    hidp_desc_destroy(d);
}

static void test_stack(void)
{
    static const unsigned char push_pop[] = {0x75, 0x08, 0x95, 0x01, 0x85, 0x01, 0xa4, 0x75, 0x10,
                                             0x85, 0x02, 0x81, 0x02, 0xb4, 0x81, 0x02};
    static const unsigned char overflow[] = {0xa4, 0xa4, 0xa4, 0xa4, 0xa4};
    static const unsigned char underflow[] = {0xb4};
    hidp_desc_t *d;
    size_t err = 0;

    CHECK(PARSE(push_pop, 0, &d) == HIDP_OK);
    CHECK(hidp_desc_field(d, 0)->report_size == 16 && hidp_desc_field(d, 0)->report_id == 2);
    CHECK(hidp_desc_field(d, 1)->report_size == 8 && hidp_desc_field(d, 1)->report_id == 1);
    hidp_desc_destroy(d);
    CHECK(hidp_desc_parse(overflow, sizeof(overflow), 0, NULL, NULL, NULL, &d, &err) == HIDP_ERR_STACK);
    CHECK(err == 4);
    CHECK(PARSE(overflow, HIDP_PARSE_TOLERANT, &d) == HIDP_OK);
    hidp_desc_destroy(d);
    CHECK(PARSE(underflow, 0, &d) == HIDP_ERR_STACK);
    CHECK(PARSE(underflow, HIDP_PARSE_TOLERANT, &d) == HIDP_OK);
    CHECK(hidp_desc_warning(d, 0)->code == HIDP_ERR_STACK);
    hidp_desc_destroy(d);
}

static void test_limits(void)
{
    static const unsigned char id0[] = {0x85, 0x00};
    static const unsigned char big_size[] = {0x76, 0x01, 0x01};
    static const unsigned char big_count[] = {0x96, 0x01, 0x30};
    static const unsigned char huge_count[] = {0x97, 0x00, 0x00, 0x01, 0x00};
    static const unsigned char too_long[] = {0x75, 0x80, 0x96, 0x00, 0x04, 0x09, 0x01, 0x81, 0x02};
    static const unsigned char mixed_ids[] = {0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
                                              0x85, 0x01, 0x81, 0x02};
    static const unsigned char long_item[] = {0xfe, 0x00, 0x01};
    static const unsigned char unknown_global[] = {0xc4};
    static const unsigned char unknown_main[] = {0x00, 0xd0, 0x08};
    hidp_desc_t *d;

    CHECK(PARSE(id0, 0, &d) == HIDP_ERR_VALUE);
    CHECK(PARSE(big_size, 0, &d) == HIDP_ERR_VALUE);
    CHECK(PARSE(big_size, HIDP_PARSE_TOLERANT, &d) == HIDP_OK);
    hidp_desc_destroy(d);
    CHECK(PARSE(big_count, 0, &d) == HIDP_ERR_VALUE);
    CHECK(PARSE(huge_count, HIDP_PARSE_TOLERANT, &d) == HIDP_ERR_VALUE);
    CHECK(PARSE(too_long, 0, &d) == HIDP_ERR_TOO_LONG);
    CHECK(PARSE(mixed_ids, 0, &d) == HIDP_OK);
    hidp_desc_destroy(d);
    CHECK(PARSE(mixed_ids, HIDP_PARSE_STRICT, &d) == HIDP_ERR_VALUE);
    CHECK(PARSE(long_item, 0, &d) == HIDP_ERR_LONG_ITEM);
    CHECK(PARSE(unknown_global, 0, &d) == HIDP_ERR_UNKNOWN_ITEM);
    CHECK(PARSE(unknown_main, 0, &d) == HIDP_OK);
    CHECK(hidp_desc_warning_count(d) == 2 && hidp_desc_field_count(d) == 0);
    hidp_desc_destroy(d);
    CHECK(parse_bytes(mouse, 0, 0, &d) == HIDP_ERR_EMPTY);
}

static void test_usage_limit(void)
{
    static const unsigned char range[] = {0x05, 0x09, 0x19, 0x00, 0x2a, 0xff, 0xff,
                                          0x75, 0x01, 0x95, 0x01, 0x81, 0x00};
    hidp_desc_t *d;

    CHECK(PARSE(range, 0, &d) == HIDP_OK);
    CHECK(hidp_desc_field(d, 0)->usage_count == 12288);
    CHECK(hidp_desc_warning_count(d) == 1);
    CHECK(hidp_desc_warning(d, 0)->code == HIDP_ERR_USAGES);
    hidp_desc_destroy(d);
}

static void test_usage_overflow(void)
{
    static const unsigned char full_then_more[] = {
        0x05, 0x09, 0x19, 0x00, 0x2a, 0xff, 0x2f, 0x19, 0x00, 0x29, 0x05,
        0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
    };
    static const unsigned char max_below_min[] = {0x05, 0x09, 0x19, 0x05, 0x29, 0x03, 0x15, 0x00,
                                                  0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02};
    static const unsigned char after_usages[] = {0x05, 0x09, 0x09, 0x01, 0x09, 0x02, 0x19, 0x05,
                                                 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
                                                 0x95, 0x01, 0x81, 0x02};
    static const unsigned char no_index_left[] = {
        0x05, 0x09, 0x19, 0x01, 0x2a, 0xff, 0x2f, 0x19, 0x00, 0x29, 0x05,
        0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
    };
    static const unsigned char open_min[] = {0x09, 0x30, 0x19, 0x01, 0x29, 0x02, 0x75, 0x01,
                                             0x95, 0x01, 0x81, 0x02, 0x19, 0x01};
    const hidp_field_t *f;
    hidp_desc_t *d;

    CHECK(PARSE(full_then_more, 0, &d) == HIDP_ERR_USAGES && d == NULL);
    CHECK(PARSE(full_then_more, HIDP_PARSE_TOLERANT, &d) == HIDP_OK);
    CHECK(hidp_desc_field(d, 0)->usage_count == 12288);
    CHECK(hidp_desc_field(d, 0)->usages_len == 1);
    hidp_desc_destroy(d);
    CHECK(PARSE(max_below_min, 0, &d) == HIDP_OK);
    f = hidp_desc_field(d, 0);
    CHECK(f->usage_count == 12288 && usage_at(f, 0) == 0x00090005);
    CHECK(usage_at(f, 12287) == 0x00093004);
    hidp_desc_destroy(d);
    CHECK(PARSE(max_below_min, HIDP_PARSE_STRICT, &d) == HIDP_ERR_VALUE);
    CHECK(PARSE(after_usages, 0, &d) == HIDP_OK);
    CHECK(hidp_desc_field(d, 0)->usage_count == 2);
    hidp_desc_destroy(d);
    CHECK(PARSE(after_usages, HIDP_PARSE_STRICT, &d) == HIDP_ERR_VALUE);
    CHECK(PARSE(no_index_left, 0, &d) == HIDP_ERR_USAGES);
    CHECK(PARSE(no_index_left, HIDP_PARSE_TOLERANT, &d) == HIDP_OK);
    CHECK(hidp_desc_field(d, 0)->usage_count == 12287);
    hidp_desc_destroy(d);
    CHECK(PARSE(open_min, 0, &d) == HIDP_OK);
    hidp_desc_destroy(d);
    CHECK(PARSE(open_min, HIDP_PARSE_STRICT, &d) == HIDP_ERR_VALUE);
}

static void test_collections_and_delimiters(void)
{
    static const unsigned char open[] = {0xa1, 0x01};
    static const unsigned char close[] = {0xc0};
    static const unsigned char delim[] = {0x05, 0x01, 0xa9, 0x01, 0x09, 0x30, 0x09, 0x31,
                                          0xa9, 0x00, 0xa9, 0x01, 0x09, 0x32, 0xa9, 0x00,
                                          0x75, 0x08, 0x95, 0x02, 0x81, 0x02};
    static const unsigned char nested[] = {0xa9, 0x01, 0xa9, 0x01};
    static const unsigned char unclosed[] = {0xa9, 0x01, 0x81, 0x02};
    static const unsigned char trailing[] = {0x09, 0x30, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0xa9, 0x01};
    size_t err = 0;
    hidp_desc_t *d;

    CHECK(PARSE(open, 0, &d) == HIDP_ERR_COLLECTION);
    CHECK(PARSE(close, 0, &d) == HIDP_ERR_COLLECTION);
    CHECK(PARSE(open, HIDP_PARSE_TOLERANT, &d) == HIDP_OK);
    CHECK(hidp_desc_warning(d, 0)->code == HIDP_ERR_COLLECTION);
    hidp_desc_destroy(d);
    CHECK(PARSE(delim, 0, &d) == HIDP_OK);
    CHECK(hidp_desc_field(d, 0)->usage_count == 2);
    CHECK(usage_at(hidp_desc_field(d, 0), 1) == 0x00010031);
    hidp_desc_destroy(d);
    CHECK(PARSE(nested, 0, &d) == HIDP_ERR_DELIMITER);
    CHECK(PARSE(unclosed, 0, &d) == HIDP_OK);
    hidp_desc_destroy(d);
    CHECK(hidp_desc_parse(trailing, sizeof(trailing), 0, NULL, NULL, NULL, &d, &err)
          == HIDP_ERR_DELIMITER);
    CHECK(err == sizeof(trailing));
    CHECK(PARSE(trailing, HIDP_PARSE_TOLERANT, &d) == HIDP_OK);
    CHECK(hidp_desc_warning(d, 0)->code == HIDP_ERR_DELIMITER);
    hidp_desc_destroy(d);
}

static void test_values(void)
{
    static const unsigned char bytes[] = {
        0x85, 0x03, 0x75, 0x0c, 0x95, 0x02, 0x15, 0x00, 0x26, 0xff, 0x0f, 0x09, 0x30, 0x81, 0x02,
        0x75, 0x04, 0x95, 0x01, 0x15, 0xf8, 0x25, 0x07, 0x09, 0x31, 0x81, 0x02,
        0x75, 0x04, 0x15, 0x01, 0x25, 0x03, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x81, 0x00,
        0x75, 0x28, 0x81, 0x03,
    };
    unsigned char rep[11];
    const hidp_report_t *r;
    const hidp_field_t *axes;
    const hidp_field_t *wheel;
    const hidp_field_t *arr;
    hidp_desc_t *d;
    uint32_t usage;
    int32_t v = 0;
    size_t fi = 0;
    size_t ui = 0;

    CHECK(PARSE(bytes, 0, &d) == HIDP_OK);
    r = hidp_desc_find_report(d, HIDP_INPUT, 3);
    CHECK(r != NULL && r->byte_len == 10);
    CHECK(hidp_report_init(r, rep, 9) == HIDP_ERR_BOUNDS);
    memset(rep, 0xee, sizeof(rep));
    CHECK(hidp_report_init(r, rep, sizeof(rep)) == HIDP_OK);
    CHECK(rep[0] == 3 && rep[1] == 0 && rep[9] == 0 && rep[10] == 0xee);
    axes = hidp_desc_field(d, 0);
    wheel = hidp_desc_field(d, 1);
    arr = hidp_desc_field(d, 2);
    CHECK(hidp_field_set(axes, rep, 10, 0, 0xabc) == HIDP_OK);
    CHECK(hidp_field_set(axes, rep, 10, 1, 0x123) == HIDP_OK);
    CHECK(rep[1] == 0xbc && rep[2] == 0x3a && rep[3] == 0x12);
    CHECK(hidp_field_set(axes, rep, 10, 2, 1) == HIDP_ERR_BOUNDS);
    CHECK(hidp_field_set(axes, rep, 10, 0, 0x1000) == HIDP_ERR_RANGE);
    CHECK(hidp_field_set(wheel, rep, 10, 0, -3) == HIDP_OK);
    CHECK(hidp_field_get(wheel, rep, 10, 0, &v) == HIDP_OK && v == -3);
    CHECK(hidp_field_get(axes, rep, 10, 1, &v) == HIDP_OK && v == 0x123);
    CHECK(hidp_field_get(axes, rep, 3, 1, &v) == HIDP_ERR_BOUNDS);
    rep[0] = 4;
    CHECK(hidp_field_get(axes, rep, 10, 0, &v) == HIDP_ERR_NOT_FOUND);
    rep[0] = 3;
    CHECK(hidp_field_set(arr, rep, 10, 0, 2) == HIDP_OK);
    CHECK(hidp_field_get(arr, rep, 10, 0, &v) == HIDP_OK && v == 2);
    CHECK(hidp_field_array_usage(arr, v, &usage) == HIDP_OK && usage == 0x00090002);
    CHECK(hidp_field_array_usage(arr, 0, &usage) == HIDP_ERR_NOT_FOUND);
    CHECK(hidp_field_array_usage(axes, 0, &usage) == HIDP_ERR_ARG);
    CHECK(hidp_field_in_range(wheel, -8) && !hidp_field_in_range(wheel, 8));
    CHECK(hidp_field_get(hidp_desc_field(d, 3), rep, 10, 0, &v) == HIDP_ERR_RANGE);
    CHECK(hidp_desc_find_usage(d, HIDP_INPUT, 0x00090003, 0, &fi, &ui) == HIDP_OK);
    CHECK(fi == 2 && ui == 2);
    CHECK(hidp_desc_find_usage(d, HIDP_INPUT, 0x31, 0, &fi, &ui) == HIDP_OK && fi == 1);
    CHECK(hidp_desc_find_usage(d, HIDP_INPUT, 0x31, 2, &fi, &ui) == HIDP_ERR_NOT_FOUND);
    CHECK(hidp_desc_find_usage(d, HIDP_OUTPUT, 0x31, 0, &fi, &ui) == HIDP_ERR_NOT_FOUND);
    hidp_desc_destroy(d);
}

static void test_classify(void)
{
    static const unsigned char pad[] = {0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0xc0,
                                        0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0xc0};
    static const unsigned char vendor[] = {0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0xc0};
    hidp_desc_t *d;

    CHECK(PARSE(mouse, 0, &d) == HIDP_OK && hidp_desc_kind(d) == HIDP_KIND_MOUSE);
    hidp_desc_destroy(d);
    CHECK(PARSE(pad, 0, &d) == HIDP_OK && hidp_desc_kind(d) == HIDP_KIND_GAMEPAD);
    hidp_desc_destroy(d);
    CHECK(PARSE(vendor, 0, &d) == HIDP_OK && hidp_desc_kind(d) == HIDP_KIND_VENDOR);
    hidp_desc_destroy(d);
    CHECK(hidp_desc_kind(NULL) == HIDP_KIND_UNKNOWN);
    CHECK(hidp_usage_kind(0x000d0005) == HIDP_KIND_TOUCHPAD);
    CHECK(hidp_usage_kind(0x000c0001) == HIDP_KIND_CONSUMER);
    CHECK(hidp_usage_kind(0x00010003) == HIDP_KIND_UNKNOWN);
    CHECK(hidp_usage_role(0x00010031) == HIDP_ROLE_AXIS);
    CHECK(hidp_usage_role(0x00010039) == HIDP_ROLE_HAT);
    CHECK(hidp_usage_role(0x00010090) == HIDP_ROLE_BUTTON);
    CHECK(hidp_usage_role(0x00090010) == HIDP_ROLE_BUTTON);
    CHECK(hidp_usage_role(0x00070004) == HIDP_ROLE_KEY);
    CHECK(hidp_usage_role(0x00080001) == HIDP_ROLE_LED);
    CHECK(hidp_usage_role(0x000f0097) == HIDP_ROLE_HAPTIC);
    CHECK(hidp_usage_role(0xff000001) == HIDP_ROLE_VENDOR);
    CHECK(hidp_usage_role(0x000c00e9) == HIDP_ROLE_OTHER);
}

static void test_format(void)
{
    char buf[64];

    CHECK(hidp_usage_format(0x00090005, buf, sizeof(buf)) == HIDP_OK && strcmp(buf, "Button 5") == 0);
    CHECK(hidp_usage_format(0x00010030, buf, sizeof(buf)) == HIDP_OK && strcmp(buf, "X") == 0);
    CHECK(hidp_usage_format(0xff000001, buf, sizeof(buf)) == HIDP_OK);
    CHECK(strcmp(buf, "Vendor-defined 0x0001") == 0);
    CHECK(hidp_usage_format(0x7f000001, buf, sizeof(buf)) == HIDP_OK && strcmp(buf, "0x7f000001") == 0);
    CHECK(hidp_usage_format(0x00090005, buf, 8) == HIDP_ERR_BOUNDS && strcmp(buf, "Button ") == 0);
    CHECK(hidp_usage_format(0x00090005, NULL, 8) == HIDP_ERR_ARG);
}

static void test_bad_args(void)
{
    hidp_desc_t *d = NULL;
    uint32_t usage;
    int32_t v;
    int i;

    CHECK(hidp_desc_parse(mouse, sizeof(mouse), 0, NULL, NULL, NULL, NULL, NULL) == HIDP_ERR_ARG);
    CHECK(hidp_desc_parse(NULL, 1, 0, NULL, NULL, NULL, &d, NULL) == HIDP_ERR_ARG);
    CHECK(hidp_desc_parse(mouse, sizeof(mouse), 0, failing_alloc, NULL, NULL, &d, NULL) == HIDP_ERR_ARG);
    CHECK(hidp_desc_parse(mouse, sizeof(mouse), 4, NULL, NULL, NULL, &d, NULL) == HIDP_ERR_ARG);
    CHECK(hidp_desc_parse(mouse, sizeof(mouse), 3, NULL, NULL, NULL, &d, NULL) == HIDP_ERR_ARG);
    CHECK(hidp_desc_field_count(NULL) == 0 && hidp_desc_field(NULL, 0) == NULL);
    CHECK(hidp_desc_find_report(NULL, 0, 0) == NULL);
    CHECK(hidp_desc_find_usage(NULL, 0, 0, 0, NULL, NULL) == HIDP_ERR_ARG);
    CHECK(hidp_field_usage(NULL, 0, &usage) == HIDP_ERR_ARG);
    CHECK(hidp_field_get(NULL, mouse, 1, 0, &v) == HIDP_ERR_ARG);
    CHECK(hidp_field_set(NULL, NULL, 0, 0, 0) == HIDP_ERR_ARG);
    CHECK(hidp_field_in_range(NULL, 0) == 0);
    CHECK(hidp_report_init(NULL, NULL, 0) == HIDP_ERR_ARG);
    for (i = -1; i <= HIDP_ERR_STOPPED + 1; i++)
        CHECK(hidp_strerror(i) != NULL);
    CHECK(strcmp(hidp_strerror(HIDP_ERR_STOPPED), "unknown error") != 0);
    CHECK(strcmp(hidp_item_name(HIDP_ITEM_MAIN, HIDP_MAIN_INPUT), "Input") == 0);
    CHECK(strcmp(hidp_item_name(HIDP_ITEM_LOCAL, HIDP_LOCAL_DELIMITER), "Delimiter") == 0);
    CHECK(strcmp(hidp_item_name(HIDP_ITEM_LONG, 0), "Long Item") == 0);
    CHECK(strcmp(hidp_item_name(HIDP_ITEM_RESERVED, 0), "Unknown") == 0);
    hidp_desc_destroy(NULL);
}

static void test_alloc_failure(void)
{
    hidp_desc_t *d;
    int status;
    int n;

    for (n = 0;; n++) {
        failing_alloc_t fa = {n, 0};

        d = NULL;
        status = hidp_desc_parse(mouse, sizeof(mouse), 0, failing_alloc, failing_free, &fa, &d, NULL);
        hidp_desc_destroy(d);
        CHECK(fa.live == 0);
        if (status == HIDP_OK)
            break;
        CHECK(status == HIDP_ERR_NOMEM && d == NULL);
    }
    CHECK(n > 5);
}

int main(void)
{
    test_tokenize();
    test_mouse();
    test_report_ids();
    test_usage_pages();
    test_globals();
    test_stack();
    test_limits();
    test_usage_limit();
    test_usage_overflow();
    test_collections_and_delimiters();
    test_values();
    test_classify();
    test_format();
    test_bad_args();
    test_alloc_failure();
    return 0;
}
