
#ifndef DTO_H
#define DTO_H

#define DTO_API_AUTO_ADJUST_KNOBS 1
#define DTO_API_WAIT_BUSYPOLL 2
#define DTO_API_WAIT_UMWAIT 4
#define DTO_API_WAIT_TPAUSE 8
#define DTO_API_WAIT_YIELD 16
#define DTO_API_CACHE_CONTROL 32
#define DTO_API_NUMA_AWARE_BUFFER_CENTRIC 64
#define DTO_API_NUMA_AWARE_CPU_CENTRIC 128
#define DTO_API_OVERLAPPING_MEMMOVE_ACTION_DSA 256

#ifdef __cplusplus
extern "C" {
#endif

typedef void(*callback_t)(void*);

enum wait_options {
        WAIT_BUSYPOLL = 0,
        WAIT_UMWAIT,
        WAIT_YIELD,
        WAIT_TPAUSE
};

enum numa_aware {
        NA_NONE = 0,
        NA_BUFFER_CENTRIC,
        NA_CPU_CENTRIC,
        NA_LAST_ENTRY
};

enum overlapping_memmove_actions {
        OVERLAPPING_CPU = 0,
        OVERLAPPING_DSA,
        OVERLAPPING_LAST_ENTRY
};

enum autotune_type {
    AUTOTUNE_API = 0,
    AUTOTUNE_INTERNAL,
    MAX_AUTOTUNE_TYPE
};

struct dto_call_cfg {
        unsigned char auto_adjust;
        unsigned char cache_control;
        enum autotune_type call_type;
        enum wait_options wait_method;
        enum numa_aware numa_mode;
        enum overlapping_memmove_actions overlapping_action;
};

void dto_memcpy(void *dest, const void *src, size_t n, int flags,
                callback_t cb, void* args);
void dto_memmove(void *dest, const void *src, size_t n, int flags,
                callback_t cb, void* args);

#ifdef __cplusplus
}
#endif

#endif

