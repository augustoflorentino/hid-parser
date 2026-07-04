#define HIDP_IMPLEMENTATION
#include "hid-parser/hid-parser.h"

static const unsigned char desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x09, 0x30, 0x15, 0x81, 0x25, 0x7f,
    0x75, 0x08, 0x95, 0x01, 0x81, 0x02, 0xc0,
};

int main(void)
{
    const unsigned char report[1] = {0xf0};
    hidp_desc_t *d = NULL;
    int64_t value = 0;
    int status;

    status = hidp_desc_parse(desc, sizeof(desc), NULL, &d, NULL);
    if (status == HIDP_OK)
        status = hidp_field_read(hidp_desc_field(d, 0), report, sizeof(report), 0, &value);
    if (hidp_strerror(status) == NULL)
        status = HIDP_ERR_ARG;
    hidp_desc_destroy(d);
    return status == HIDP_OK && value == -16 ? 0 : 1;
}
