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

static void print_usages(const hidp_field_t *f)
{
    size_t i;

    for (i = 0; i < f->usages_len; i++) {
        if (f->usages[i].min == f->usages[i].max)
            printf("%s0x%08x", i ? "," : "", (unsigned)f->usages[i].min);
        else
            printf("%s0x%08x-0x%08x", i ? "," : "", (unsigned)f->usages[i].min,
                   (unsigned)f->usages[i].max);
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

static void dump(const hidp_desc_t *desc, int slots)
{
    const hidp_collection_t *c;
    const hidp_report_t *r;
    size_t i;

    for (i = 0; !slots && i < hidp_desc_collection_count(desc); i++) {
        c = hidp_desc_collection(desc, i);
        printf("%*scollection type=%u usage=0x%08x\n", (int)c->depth * 2, "", c->type,
               (unsigned)c->usage);
    }
    for (i = 0; !slots && i < hidp_desc_report_count(desc); i++) {
        r = hidp_desc_report(desc, i);
        printf("report %-7s id=%-3u bytes=%zu\n", type_names[r->type], r->id, r->byte_len);
    }
    for (i = 0; i < hidp_desc_field_count(desc); i++) {
        if (slots)
            print_slots(hidp_desc_field(desc, i));
        else
            print_field(hidp_desc_field(desc, i));
    }
    for (i = 0; !slots && i < hidp_desc_warning_count(desc); i++)
        printf("warning at %zu: %s\n", hidp_desc_warning(desc, i)->offset,
               hidp_strerror(hidp_desc_warning(desc, i)->code));
}

static int parse_args(int argc, char **argv, unsigned *flags, int *slots, const char **path)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--strict") == 0)
            *flags = HIDP_PARSE_STRICT;
        else if (strcmp(argv[i], "--tolerant") == 0)
            *flags = HIDP_PARSE_TOLERANT;
        else if (strcmp(argv[i], "--slots") == 0)
            *slots = 1;
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
    unsigned flags = 0;
    int slots = 0;
    FILE *in = stdin;
    unsigned char *data;
    size_t len = 0;
    size_t error_offset = 0;
    hidp_desc_t *desc = NULL;
    int status;

    if (parse_args(argc, argv, &flags, &slots, &path) != 0) {
        fprintf(stderr, "usage: hiddump [--strict|--tolerant] [--slots] [descriptor]\n");
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
    status = hidp_desc_parse(data, len, flags, NULL, NULL, NULL, &desc, &error_offset);
    free(data);
    if (status != HIDP_OK) {
        fprintf(stderr, "error at byte %zu: %s\n", error_offset, hidp_strerror(status));
        return 1;
    }
    dump(desc, slots);
    hidp_desc_destroy(desc);
    return 0;
}
