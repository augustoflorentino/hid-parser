#include "hidp_internal.h"

static const char *const hidp_main_names[16] = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    "Input", "Output", "Collection", "Feature", "End Collection", NULL, NULL, NULL,
};

static const char *const hidp_global_names[16] = {
    "Usage Page", "Logical Minimum", "Logical Maximum", "Physical Minimum",
    "Physical Maximum", "Unit Exponent", "Unit", "Report Size",
    "Report ID", "Report Count", "Push", "Pop", NULL, NULL, NULL, NULL,
};

static const char *const hidp_local_names[16] = {
    "Usage", "Usage Minimum", "Usage Maximum", "Designator Index",
    "Designator Minimum", "Designator Maximum", NULL, "String Index",
    "String Minimum", "String Maximum", "Delimiter", NULL, NULL, NULL, NULL, NULL,
};

const char *hidp_item_name(unsigned type, unsigned tag)
{
    const char *name = NULL;

    if (type == HIDP_ITEM_LONG)
        return "Long Item";
    if (tag > 15u)
        return "Unknown";
    if (type == HIDP_ITEM_MAIN)
        name = hidp_main_names[tag];
    else if (type == HIDP_ITEM_GLOBAL)
        name = hidp_global_names[tag];
    else if (type == HIDP_ITEM_LOCAL)
        name = hidp_local_names[tag];
    return name != NULL ? name : "Unknown";
}

static const char *const hidp_errors[] = {
    "success",
    "invalid argument",
    "out of memory",
    "descriptor ends in the middle of an item",
    "long items are not supported",
    "unknown item",
    "global stack overflow or underflow",
    "unbalanced collection",
    "unbalanced or nested delimiter",
    "invalid item value",
    "logical minimum is greater than logical maximum",
    "report is too long",
    "too many usages",
    "empty descriptor",
    "out of bounds",
    "not found",
    "stopped by callback",
};

const char *hidp_strerror(int status)
{
    if (status < 0 || (size_t)status >= sizeof(hidp_errors) / sizeof(hidp_errors[0]))
        return "unknown error";
    return hidp_errors[status];
}
