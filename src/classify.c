#include "hidp_internal.h"

static unsigned hidp_desktop_kind(uint32_t id)
{
    switch (id) {
    case 0x01: return HIDP_KIND_POINTER;
    case 0x02: return HIDP_KIND_MOUSE;
    case 0x04: return HIDP_KIND_JOYSTICK;
    case 0x05: return HIDP_KIND_GAMEPAD;
    case 0x06: return HIDP_KIND_KEYBOARD;
    case 0x07: return HIDP_KIND_KEYPAD;
    case 0x08: return HIDP_KIND_MULTI_AXIS;
    case 0x80: return HIDP_KIND_SYSTEM;
    default: return HIDP_KIND_UNKNOWN;
    }
}

static unsigned hidp_digitizer_kind(uint32_t id)
{
    switch (id) {
    case 0x01: return HIDP_KIND_DIGITIZER;
    case 0x02: return HIDP_KIND_PEN;
    case 0x04: return HIDP_KIND_TOUCHSCREEN;
    case 0x05: return HIDP_KIND_TOUCHPAD;
    default: return HIDP_KIND_UNKNOWN;
    }
}

unsigned hidp_usage_kind(uint32_t usage)
{
    uint32_t page = usage >> 16;
    uint32_t id = usage & 0xffffu;

    if (page == 0x01u)
        return hidp_desktop_kind(id);
    if (page == 0x0du)
        return hidp_digitizer_kind(id);
    if (page == 0x0cu && id == 0x01u)
        return HIDP_KIND_CONSUMER;
    if (page == 0x20u)
        return HIDP_KIND_SENSOR;
    if (page >= 0xff00u)
        return HIDP_KIND_VENDOR;
    return HIDP_KIND_UNKNOWN;
}

unsigned hidp_desc_kind(const hidp_desc_t *desc)
{
    const hidp_collection_t *c;
    unsigned kind;
    size_t i;

    for (i = 0; i < hidp_desc_collection_count(desc); i++) {
        c = hidp_desc_collection(desc, i);
        if (c->parent != HIDP_NONE || c->type != HIDP_COLLECTION_APPLICATION)
            continue;
        kind = hidp_usage_kind(c->usage);
        if (kind != HIDP_KIND_UNKNOWN && kind != HIDP_KIND_VENDOR)
            return kind;
    }
    return hidp_desc_collection_count(desc) > 0u ? HIDP_KIND_VENDOR : HIDP_KIND_UNKNOWN;
}

static unsigned hidp_desktop_role(uint32_t id)
{
    if (id >= 0x30u && id <= 0x38u)
        return HIDP_ROLE_AXIS;
    if (id == 0x39u)
        return HIDP_ROLE_HAT;
    if (id >= 0x90u && id <= 0x93u)
        return HIDP_ROLE_BUTTON;
    if (id == 0x40u || id == 0x41u || id == 0x42u || id == 0x43u || id == 0x44u || id == 0x45u)
        return HIDP_ROLE_AXIS;
    return HIDP_ROLE_OTHER;
}

unsigned hidp_usage_role(uint32_t usage)
{
    uint32_t page = usage >> 16;
    uint32_t id = usage & 0xffffu;

    switch (page) {
    case 0x01u: return hidp_desktop_role(id);
    case 0x02u: return HIDP_ROLE_AXIS;
    case 0x07u: return HIDP_ROLE_KEY;
    case 0x08u: return HIDP_ROLE_LED;
    case 0x09u: return HIDP_ROLE_BUTTON;
    case 0x0eu:
    case 0x0fu: return HIDP_ROLE_HAPTIC;
    default: return page >= 0xff00u ? HIDP_ROLE_VENDOR : HIDP_ROLE_OTHER;
    }
}
