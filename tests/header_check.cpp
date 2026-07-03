#include "hid-parser/hid-parser.h"

int header_check_cxx(const unsigned char *desc, size_t len)
{
    hidp_desc_t *parsed = NULL;
    int status = hidp_desc_parse(desc, len, NULL, &parsed, NULL);

    hidp_desc_destroy(parsed);
    return status;
}
