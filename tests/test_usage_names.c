#include "hid-parser/hid-parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                          \
        }                                                                     \
    } while (0)

#define USAGE(page, id) (((uint32_t)(page) << 16) | (uint32_t)(id))

static int same(const char *a, const char *b)
{
    return a != NULL && b != NULL && strcmp(a, b) == 0;
}

static void test_known_names(void)
{
    CHECK(same(hidp_usage_page_name(0x01), "Generic Desktop"));
    CHECK(same(hidp_usage_page_name(0x0C), "Consumer"));
    CHECK(same(hidp_usage_page_name(0xF1D0), "FIDO Alliance"));
    CHECK(same(hidp_usage_name(USAGE(0x01, 0x30)), "X"));
    CHECK(same(hidp_usage_name(USAGE(0x01, 0x05)), "Gamepad"));
    CHECK(same(hidp_usage_name(USAGE(0x07, 0x04)), "Keyboard A"));
    CHECK(same(hidp_usage_name(USAGE(0x0C, 0xE9)), "Volume Increment"));
    CHECK(same(hidp_usage_name(USAGE(0x20, 0x0900)), "VT_NULL"));
    CHECK(same(hidp_usage_name(USAGE(0x0F, 0x21)), "Set Effect Report"));
}

static void test_generated_and_ranges(void)
{
    CHECK(same(hidp_usage_page_name(0x09), "Button"));
    CHECK(same(hidp_usage_name(USAGE(0x09, 1)), "Button"));
    CHECK(same(hidp_usage_name(USAGE(0x09, 0xFFFF)), "Button"));
    CHECK(hidp_usage_name(USAGE(0x09, 0)) == NULL);
    CHECK(same(hidp_usage_name(USAGE(0x0A, 7)), "Instance"));
    CHECK(same(hidp_usage_name(USAGE(0x81, 3)), "Enum"));
    CHECK(same(hidp_usage_page_name(0x10), "Unicode"));
    CHECK(hidp_usage_name(USAGE(0x10, 0x41)) == NULL);
    CHECK(same(hidp_usage_page_name(0x92), "Gaming Device"));
    CHECK(same(hidp_usage_page_name(0xFF00), "Vendor-defined"));
    CHECK(same(hidp_usage_page_name(0xFFFF), "Vendor-defined"));
    CHECK(hidp_usage_name(USAGE(0xFF00, 1)) == NULL);
}

static void test_unknown(void)
{
    CHECK(hidp_usage_page_name(0x00) == NULL);
    CHECK(hidp_usage_page_name(0x13) == NULL);
    CHECK(hidp_usage_page_name(0xF1CF) == NULL);
    CHECK(hidp_usage_page_name(0x10000) == NULL);
    CHECK(hidp_usage_name(USAGE(0x01, 0)) == NULL);
    CHECK(hidp_usage_name(USAGE(0x01, 0x03)) == NULL);
    CHECK(hidp_usage_name(USAGE(0x13, 0x01)) == NULL);
}

static void test_every_value(void)
{
    uint32_t page;
    uint32_t id;
    size_t pages = 0;
    size_t names = 0;

    for (page = 0; page <= 0xFFFF; page++) {
        if (hidp_usage_page_name(page) == NULL)
            continue;
        pages++;
        for (id = 0; id <= 0xFFFF; id++) {
            const char *name = hidp_usage_name(USAGE(page, id));

            if (name != NULL && name[0] != '\0')
                names++;
            else
                CHECK(name == NULL);
        }
    }
    CHECK(pages == 35 + 256);
    CHECK(names >= 2770);
}

static int compare_line(char *line)
{
    char *fields[3];
    const char *ours;
    unsigned long page;
    long id;
    int i;

    line[strcspn(line, "\r\n")] = '\0';
    fields[0] = strtok(line, "\t");
    for (i = 1; i < 3; i++)
        fields[i] = strtok(NULL, "\t");
    if (fields[0] == NULL || fields[1] == NULL || fields[2] == NULL)
        return 0;
    page = strtoul(fields[0], NULL, 16);
    id = strtol(fields[1], NULL, 16);
    ours = id < 0 ? hidp_usage_page_name((uint32_t)page) : hidp_usage_name(USAGE(page, id));
    if (same(ours, fields[2]))
        return 0;
    printf("%04lx\t%s\thid-tools=%s\thid-parser=%s\n", page, id < 0 ? "page" : fields[1], fields[2], ours ? ours : "(null)");
    return 1;
}

static int compare_file(const char *path)
{
    char line[512];
    FILE *file = fopen(path, "r");
    int differences = 0;
    int total = 0;

    CHECK(file != NULL);
    while (fgets(line, sizeof(line), file) != NULL) {
        differences += compare_line(line);
        total++;
    }
    fclose(file);
    printf("%d of %d names differ\n", differences, total);
    return 0;
}

int main(int argc, char **argv)
{
    test_known_names();
    test_generated_and_ranges();
    test_unknown();
    test_every_value();
    if (argc == 2)
        return compare_file(argv[1]);
    printf("usage names: all tests passed\n");
    return 0;
}
