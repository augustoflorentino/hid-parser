#ifndef HID_PARSER_H
#define HID_PARSER_H

#include <stddef.h>
#include <stdint.h>

#if defined(HIDP_STATIC) && defined(__GNUC__)
#define HIDP_API static __attribute__((unused))
#elif defined(HIDP_STATIC)
#define HIDP_API static
#elif defined(_WIN32) && defined(HIDP_BUILD_SHARED)
#define HIDP_API __declspec(dllexport)
#elif defined(_WIN32) && defined(HIDP_SHARED)
#define HIDP_API __declspec(dllimport)
#elif defined(__GNUC__) && defined(HIDP_BUILD_SHARED)
#define HIDP_API __attribute__((visibility("default")))
#else
#define HIDP_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define HIDP_VERSION_MAJOR 2
#define HIDP_VERSION_MINOR 3
#define HIDP_VERSION_PATCH 0
#define HIDP_VERSION (HIDP_VERSION_MAJOR * 10000 + HIDP_VERSION_MINOR * 100 + HIDP_VERSION_PATCH)

#define HIDP_OK 0
#define HIDP_ERR_ARG 1
#define HIDP_ERR_NOMEM 2
#define HIDP_ERR_TRUNCATED 3
#define HIDP_ERR_LONG_ITEM 4
#define HIDP_ERR_UNKNOWN_ITEM 5
#define HIDP_ERR_STACK 6
#define HIDP_ERR_COLLECTION 7
#define HIDP_ERR_DELIMITER 8
#define HIDP_ERR_VALUE 9
#define HIDP_ERR_RANGE 10
#define HIDP_ERR_TOO_LONG 11
#define HIDP_ERR_USAGES 12
#define HIDP_ERR_EMPTY 13
#define HIDP_ERR_BOUNDS 14
#define HIDP_ERR_NOT_FOUND 15
#define HIDP_ERR_STOPPED 16

#define HIDP_PARSE_STRICT 0x1u
#define HIDP_PARSE_TOLERANT 0x2u

#define HIDP_ITEM_MAIN 0u
#define HIDP_ITEM_GLOBAL 1u
#define HIDP_ITEM_LOCAL 2u
#define HIDP_ITEM_RESERVED 3u
#define HIDP_ITEM_LONG 4u

#define HIDP_MAIN_INPUT 0x8u
#define HIDP_MAIN_OUTPUT 0x9u
#define HIDP_MAIN_COLLECTION 0xau
#define HIDP_MAIN_FEATURE 0xbu
#define HIDP_MAIN_END_COLLECTION 0xcu

#define HIDP_GLOBAL_USAGE_PAGE 0x0u
#define HIDP_GLOBAL_LOGICAL_MIN 0x1u
#define HIDP_GLOBAL_LOGICAL_MAX 0x2u
#define HIDP_GLOBAL_PHYSICAL_MIN 0x3u
#define HIDP_GLOBAL_PHYSICAL_MAX 0x4u
#define HIDP_GLOBAL_UNIT_EXPONENT 0x5u
#define HIDP_GLOBAL_UNIT 0x6u
#define HIDP_GLOBAL_REPORT_SIZE 0x7u
#define HIDP_GLOBAL_REPORT_ID 0x8u
#define HIDP_GLOBAL_REPORT_COUNT 0x9u
#define HIDP_GLOBAL_PUSH 0xau
#define HIDP_GLOBAL_POP 0xbu

#define HIDP_LOCAL_USAGE 0x0u
#define HIDP_LOCAL_USAGE_MIN 0x1u
#define HIDP_LOCAL_USAGE_MAX 0x2u
#define HIDP_LOCAL_DESIGNATOR_INDEX 0x3u
#define HIDP_LOCAL_DESIGNATOR_MIN 0x4u
#define HIDP_LOCAL_DESIGNATOR_MAX 0x5u
#define HIDP_LOCAL_STRING_INDEX 0x7u
#define HIDP_LOCAL_STRING_MIN 0x8u
#define HIDP_LOCAL_STRING_MAX 0x9u
#define HIDP_LOCAL_DELIMITER 0xau

#define HIDP_INPUT 0u
#define HIDP_OUTPUT 1u
#define HIDP_FEATURE 2u

#define HIDP_FLAG_CONSTANT 0x001u
#define HIDP_FLAG_VARIABLE 0x002u
#define HIDP_FLAG_RELATIVE 0x004u
#define HIDP_FLAG_WRAP 0x008u
#define HIDP_FLAG_NONLINEAR 0x010u
#define HIDP_FLAG_NO_PREFERRED 0x020u
#define HIDP_FLAG_NULL_STATE 0x040u
#define HIDP_FLAG_VOLATILE 0x080u
#define HIDP_FLAG_BUFFERED_BYTES 0x100u

#define HIDP_COLLECTION_PHYSICAL 0x00u
#define HIDP_COLLECTION_APPLICATION 0x01u
#define HIDP_COLLECTION_LOGICAL 0x02u
#define HIDP_COLLECTION_REPORT 0x03u
#define HIDP_COLLECTION_NAMED_ARRAY 0x04u
#define HIDP_COLLECTION_USAGE_SWITCH 0x05u
#define HIDP_COLLECTION_USAGE_MODIFIER 0x06u

#define HIDP_KIND_UNKNOWN 0u
#define HIDP_KIND_POINTER 1u
#define HIDP_KIND_MOUSE 2u
#define HIDP_KIND_JOYSTICK 3u
#define HIDP_KIND_GAMEPAD 4u
#define HIDP_KIND_KEYBOARD 5u
#define HIDP_KIND_KEYPAD 6u
#define HIDP_KIND_MULTI_AXIS 7u
#define HIDP_KIND_SYSTEM 8u
#define HIDP_KIND_CONSUMER 9u
#define HIDP_KIND_DIGITIZER 10u
#define HIDP_KIND_PEN 11u
#define HIDP_KIND_TOUCHSCREEN 12u
#define HIDP_KIND_TOUCHPAD 13u
#define HIDP_KIND_SENSOR 14u
#define HIDP_KIND_VENDOR 15u

#define HIDP_ROLE_OTHER 0u
#define HIDP_ROLE_BUTTON 1u
#define HIDP_ROLE_AXIS 2u
#define HIDP_ROLE_HAT 3u
#define HIDP_ROLE_KEY 4u
#define HIDP_ROLE_LED 5u
#define HIDP_ROLE_HAPTIC 6u
#define HIDP_ROLE_VENDOR 7u

#define HIDP_NONE ((size_t)-1)

typedef struct hidp_desc hidp_desc_t;

typedef void *(*hidp_alloc_fn)(void *ctx, size_t size);
typedef void (*hidp_free_fn)(void *ctx, void *ptr);

typedef struct hidp_parse_options {
    unsigned flags;
    hidp_alloc_fn alloc_fn;
    hidp_free_fn free_fn;
    void *alloc_ctx;
} hidp_parse_options_t;

typedef struct hidp_item {
    size_t offset;
    size_t len;
    unsigned type;
    unsigned tag;
    unsigned data_len;
    uint32_t udata;
    int32_t sdata;
    const unsigned char *data;
} hidp_item_t;

typedef int (*hidp_item_fn)(void *ctx, const hidp_item_t *item);

typedef struct hidp_usage_range {
    uint32_t min;
    uint32_t max;
} hidp_usage_range_t;

typedef struct hidp_field {
    unsigned report_type;
    unsigned report_id;
    size_t bit_offset;
    unsigned report_size;
    unsigned report_count;
    unsigned flags;
    int32_t logical_min;
    int32_t logical_max;
    int32_t physical_min;
    int32_t physical_max;
    int32_t unit_exponent;
    uint32_t unit;
    uint32_t usage_page;
    const hidp_usage_range_t *usages;
    size_t usages_len;
    size_t usage_count;
    size_t collection;
    size_t desc_offset;
} hidp_field_t;

typedef struct hidp_collection {
    unsigned type;
    uint32_t usage;
    size_t parent;
    unsigned depth;
    size_t desc_offset;
} hidp_collection_t;

typedef struct hidp_report {
    unsigned type;
    unsigned id;
    size_t bit_len;
    size_t byte_len;
} hidp_report_t;

typedef struct hidp_value {
    const hidp_field_t *field;
    size_t field_index;
    size_t index;
    uint32_t usage;
    int64_t value;
} hidp_value_t;

typedef int (*hidp_value_fn)(void *ctx, const hidp_value_t *value);

typedef struct hidp_warning {
    size_t offset;
    int code;
} hidp_warning_t;

HIDP_API int hidp_tokenize(const unsigned char *desc, size_t desc_len, hidp_item_fn item_fn,
                           void *item_ctx, size_t *out_error_offset);

HIDP_API int hidp_item_write(unsigned char *buf, size_t buf_len, unsigned type, unsigned tag,
                             int64_t value, size_t *out_len);
HIDP_API int hidp_desc_parse(const unsigned char *desc, size_t desc_len,
                             const hidp_parse_options_t *options, hidp_desc_t **out_desc,
                             size_t *out_error_offset);
HIDP_API void hidp_desc_destroy(hidp_desc_t *desc);

HIDP_API size_t hidp_desc_field_count(const hidp_desc_t *desc);
HIDP_API const hidp_field_t *hidp_desc_field(const hidp_desc_t *desc, size_t index);
HIDP_API size_t hidp_desc_collection_count(const hidp_desc_t *desc);
HIDP_API const hidp_collection_t *hidp_desc_collection(const hidp_desc_t *desc, size_t index);
HIDP_API size_t hidp_desc_report_count(const hidp_desc_t *desc);
HIDP_API const hidp_report_t *hidp_desc_report(const hidp_desc_t *desc, size_t index);
HIDP_API const hidp_report_t *hidp_desc_find_report(const hidp_desc_t *desc, unsigned type,
                                                     unsigned id);
HIDP_API const hidp_report_t *hidp_desc_match_report(const hidp_desc_t *desc, unsigned type,
                                                      const unsigned char *report,
                                                      size_t report_len);
HIDP_API size_t hidp_desc_application(const hidp_desc_t *desc, size_t collection);
HIDP_API size_t hidp_desc_warning_count(const hidp_desc_t *desc);
HIDP_API const hidp_warning_t *hidp_desc_warning(const hidp_desc_t *desc, size_t index);
HIDP_API int hidp_desc_find_usage(const hidp_desc_t *desc, unsigned type, uint32_t usage,
                                  size_t start_field, size_t *out_field, size_t *out_index);

HIDP_API int hidp_field_usage(const hidp_field_t *field, size_t index, uint32_t *out_usage);
HIDP_API int hidp_field_read(const hidp_field_t *field, const unsigned char *report,
                             size_t report_len, size_t index, int64_t *out_value);
HIDP_API int hidp_field_write(const hidp_field_t *field, unsigned char *report, size_t report_len,
                              size_t index, int64_t value);
HIDP_API int hidp_field_array_usage(const hidp_field_t *field, int64_t value,
                                    uint32_t *out_usage);
HIDP_API int hidp_field_in_range(const hidp_field_t *field, int64_t value);
HIDP_API int hidp_report_decode(const hidp_desc_t *desc, unsigned type, const unsigned char *report,
                                size_t report_len, hidp_value_fn value_fn, void *ctx);
HIDP_API int hidp_report_init(const hidp_report_t *report, unsigned char *buf, size_t buf_len);

HIDP_API const char *hidp_usage_page_name(uint32_t page);
HIDP_API const char *hidp_usage_name(uint32_t usage);
HIDP_API size_t hidp_usage_print(uint32_t usage, char *buf, size_t buf_len);
HIDP_API int hidp_usage_format(uint32_t usage, char *buf, size_t buf_len);
HIDP_API unsigned hidp_usage_kind(uint32_t usage);
HIDP_API unsigned hidp_desc_kind(const hidp_desc_t *desc);
HIDP_API unsigned hidp_usage_role(uint32_t usage);

HIDP_API const char *hidp_item_name(unsigned type, unsigned tag);
HIDP_API const char *hidp_strerror(int status);
HIDP_API int hidp_version(void);

#ifdef __cplusplus
}
#endif

#endif
