#include "hid-parser/hid-parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const type_names[] = {"Input", "Output", "Feature"};

static unsigned char *read_all(FILE *in, size_t *out_len)
{
    unsigned char *data = NULL;
    size_t len = 0;
    size_t cap = 0;
    size_t got;

    do {
        if (len == cap) {
            unsigned char *grown = realloc(data, cap ? cap * 2u : 4096u);

            if (grown == NULL) {
                free(data);
                return NULL;
            }
            data = grown;
            cap = cap ? cap * 2u : 4096u;
        }
        got = fread(data + len, 1, cap - len, in);
        len += got;
    } while (got > 0);
    *out_len = len;
    return data;
}

static size_t parse_hex(unsigned char *data, size_t len)
{
    size_t out = 0;
    size_t i = 0;
    int high = -1;
    int v;

    while (i < len) {
        unsigned char c = data[i++];

        if (c == '0' && i < len && (data[i] == 'x' || data[i] == 'X')) {
            i++;
            continue;
        }
        v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
            : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (v < 0) {
            high = -1;
            continue;
        }
        if (high < 0) {
            high = v;
            continue;
        }
        data[out++] = (unsigned char)(high << 4 | v);
        high = -1;
    }
    return out;
}

typedef struct {
    int depth;
} tree_t;

static int print_item(void *ctx, const hidp_item_t *item)
{
    tree_t *tree = ctx;
    char hex[64];
    size_t n = 0;
    size_t i;

    if (item->type == HIDP_ITEM_MAIN && item->tag == HIDP_MAIN_END_COLLECTION && tree->depth > 0)
        tree->depth--;
    n += (size_t)snprintf(hex, sizeof(hex), "0x%02x,", item->data[-1 - (item->type == HIDP_ITEM_LONG ? 2 : 0)]);
    for (i = 0; i < item->data_len && i < 4u && n < sizeof(hex) - 8u; i++)
        n += (size_t)snprintf(hex + n, sizeof(hex) - n, " 0x%02x,", item->data[i]);
    printf("%-30s // %*s%s", hex, tree->depth * 2, "", hidp_item_name(item->type, item->tag));
    if (item->type == HIDP_ITEM_GLOBAL && (item->tag == HIDP_GLOBAL_LOGICAL_MIN
                                           || item->tag == HIDP_GLOBAL_PHYSICAL_MIN))
        printf(" (%ld)\n", (long)item->sdata);
    else if (item->type == HIDP_ITEM_GLOBAL && item->tag != HIDP_GLOBAL_USAGE_PAGE
             && item->tag != HIDP_GLOBAL_UNIT && item->data_len > 0u)
        printf(" (%lu)\n", (unsigned long)item->udata);
    else if (item->data_len > 0u && item->type != HIDP_ITEM_LONG)
        printf(" (0x%lx)\n", (unsigned long)item->udata);
    else
        printf("\n");
    if (item->type == HIDP_ITEM_MAIN && item->tag == HIDP_MAIN_COLLECTION)
        tree->depth++;
    return 0;
}

static void print_usages(const hidp_field_t *f)
{
    char lo[96];
    char hi[96];
    size_t i;

    for (i = 0; i < f->usages_len; i++) {
        hidp_usage_format(f->usages[i].min, lo, sizeof(lo));
        hidp_usage_format(f->usages[i].max, hi, sizeof(hi));
        if (f->usages[i].min == f->usages[i].max)
            printf("%s%s", i ? ", " : "", lo);
        else
            printf("%s%s..%s", i ? ", " : "", lo, hi);
    }
}

static void print_field(const hidp_field_t *f)
{
    printf("%-7s id=%-3u bit=%-5zu size=%-3u count=%-4u flags=0x%03x logical=%ld..%ld",
           type_names[f->report_type], f->report_id, f->bit_offset, f->report_size,
           f->report_count, f->flags, (long)f->logical_min, (long)f->logical_max);
    if (f->physical_min != 0 || f->physical_max != 0)
        printf(" physical=%ld..%ld", (long)f->physical_min, (long)f->physical_max);
    if (f->unit != 0)
        printf(" unit=0x%lx exp=%ld", (unsigned long)f->unit, (long)f->unit_exponent);
    printf(" usages=");
    print_usages(f);
    printf("\n");
}

static void print_slot(const hidp_field_t *f, size_t i)
{
    uint32_t usage = 0;

    hidp_field_usage(f, i, &usage);
    printf("%c %u %zu %u 1 %ld 0x%08x\n", type_names[f->report_type][0], f->report_id,
           f->bit_offset + i * f->report_size, f->report_size, (long)f->logical_min,
           (unsigned)usage);
}

static void print_slots(const hidp_field_t *f)
{
    uint32_t usage;
    size_t i;

    if (f->usage_count == 0u) {
        printf("%c %u %zu %lu 1 pad\n", type_names[f->report_type][0], f->report_id,
               f->bit_offset, (unsigned long)f->report_size * f->report_count);
        return;
    }
    if (f->flags & (HIDP_FLAG_CONSTANT | HIDP_FLAG_VARIABLE)) {
        for (i = 0; i < f->report_count; i++)
            print_slot(f, i);
        return;
    }
    printf("%c %u %zu %u %u %ld", type_names[f->report_type][0], f->report_id, f->bit_offset,
           f->report_size, f->report_count, (long)f->logical_min);
    for (i = 0; i < f->usage_count && hidp_field_usage(f, i, &usage) == HIDP_OK; i++)
        printf("%s0x%08x", i ? "," : " ", (unsigned)usage);
    printf("\n");
}

static void print_kernel_field(const hidp_field_t *f)
{
    uint32_t usage;
    size_t i;

    printf("%c %u %zu %u %u %u %ld %ld %ld %ld %ld 0x%lx %zu ", type_names[f->report_type][0],
           f->report_id, f->bit_offset, f->report_size, f->report_count, f->flags,
           (long)f->logical_min, (long)f->logical_max, (long)f->physical_min,
           (long)f->physical_max, (long)f->unit_exponent, (unsigned long)f->unit,
           f->usage_count);
    for (i = 0; i < f->usage_count && hidp_field_usage(f, i, &usage) == HIDP_OK; i++)
        printf("%s0x%08x", i ? "," : "", (unsigned)usage);
    printf("%s\n", f->usage_count ? "" : "-");
}

static int print_value(void *ctx, const hidp_value_t *v)
{
    char name[96];

    (void)ctx;
    hidp_usage_format(v->usage, name, sizeof(name));
    if (!(v->field->flags & HIDP_FLAG_VARIABLE))
        printf("%s\n", name);
    else
        printf("%s = %lld%s\n", name, (long long)v->value,
               hidp_field_in_range(v->field, v->value) ? "" : " (null)");
    return 0;
}

static int decode(const hidp_desc_t *desc, const unsigned char *report, size_t len)
{
    int status = hidp_report_decode(desc, HIDP_INPUT, report, len, print_value, NULL);

    if (status != HIDP_OK)
        fprintf(stderr, "report of %zu bytes: %s\n", len, hidp_strerror(status));
    return status != HIDP_OK;
}

static void dump(const hidp_desc_t *desc, int slots)
{
    const hidp_collection_t *c;
    const hidp_report_t *r;
    size_t i;

    if (!slots)
        printf("kind %u\n", hidp_desc_kind(desc));
    for (i = 0; !slots && i < hidp_desc_collection_count(desc); i++) {
        c = hidp_desc_collection(desc, i);
        printf("%*scollection type=%u usage=0x%08x %s\n", (int)c->depth * 2, "", c->type,
               (unsigned)c->usage, hidp_usage_name(c->usage) ? hidp_usage_name(c->usage) : "");
    }
    for (i = 0; !slots && i < hidp_desc_report_count(desc); i++) {
        r = hidp_desc_report(desc, i);
        printf("report %-7s id=%-3u bytes=%zu\n", type_names[r->type], r->id, r->byte_len);
    }
    for (i = 0; i < hidp_desc_field_count(desc); i++) {
        if (slots == 3)
            print_kernel_field(hidp_desc_field(desc, i));
        else if (slots)
            print_slots(hidp_desc_field(desc, i));
        else
            print_field(hidp_desc_field(desc, i));
    }
    for (i = 0; !slots && i < hidp_desc_warning_count(desc); i++)
        printf("warning at %zu: %s\n", hidp_desc_warning(desc, i)->offset,
               hidp_strerror(hidp_desc_warning(desc, i)->code));
}

static int parse_args(int argc, char **argv, unsigned *flags, int *slots, const char **path,
                      char **report)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--report") == 0 && i + 1 < argc)
            *report = argv[++i];
        else if (strcmp(argv[i], "--strict") == 0)
            *flags = HIDP_PARSE_STRICT;
        else if (strcmp(argv[i], "--tolerant") == 0)
            *flags = HIDP_PARSE_TOLERANT;
        else if (strcmp(argv[i], "--slots") == 0)
            *slots = (*slots & 4) | 1;
        else if (strcmp(argv[i], "--fields") == 0)
            *slots = (*slots & 4) | 3;
        else if (strcmp(argv[i], "--items") == 0)
            *slots = (*slots & 4) | 2;
        else if (strcmp(argv[i], "--hex") == 0)
            *slots |= 4;
        else if (*path == NULL)
            *path = argv[i];
        else
            return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    char *report = NULL;
    unsigned flags = 0;
    int slots = 0;
    FILE *in = stdin;
    unsigned char *data;
    size_t len = 0;
    size_t error_offset = 0;
    hidp_desc_t *desc = NULL;
    hidp_parse_options_t options = {0, NULL, NULL, NULL};
    int status;

    if (parse_args(argc, argv, &flags, &slots, &path, &report) != 0) {
        fprintf(stderr, "usage: hiddump [--strict|--tolerant] [--slots|--fields|--items] [--hex] "
                        "[--report HEX] [descriptor]\n");
        return 2;
    }
    if (path != NULL && (in = fopen(path, "rb")) == NULL) {
        perror(path);
        return 1;
    }
    data = read_all(in, &len);
    if (in != stdin)
        fclose(in);
    if (data == NULL)
        return 1;
    if (slots & 4) {
        len = parse_hex(data, len);
        slots &= 3;
    }
    if (slots == 2) {
        tree_t tree = {0};

        status = hidp_tokenize(data, len, print_item, &tree, &error_offset);
        free(data);
        if (status != HIDP_OK)
            fprintf(stderr, "error at byte %zu: %s\n", error_offset, hidp_strerror(status));
        return status != HIDP_OK;
    }
    options.flags = flags;
    status = hidp_desc_parse(data, len, &options, &desc, &error_offset);
    free(data);
    if (status != HIDP_OK) {
        fprintf(stderr, "error at byte %zu: %s\n", error_offset, hidp_strerror(status));
        return 1;
    }
    if (report != NULL)
        status = decode(desc, (unsigned char *)report,
                        parse_hex((unsigned char *)report, strlen(report)));
    else
        dump(desc, slots);
    hidp_desc_destroy(desc);
    return status;
}
