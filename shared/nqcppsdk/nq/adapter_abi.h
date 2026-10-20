#ifndef NQ_ADAPTER_ABI_H
#define NQ_ADAPTER_ABI_H

#include <stddef.h>
#include <stdint.h>

#include <nq/abi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nq_instrument {
    uint32_t id;
    const char* symbol;
    int64_t tick_size;
    int64_t lot_size;
} nq_instrument;

typedef struct nq_adapter_schema {
    uint64_t abi_hash;
    const char* sdk_version;
    const char* name;
    uint8_t provides;
} nq_adapter_schema;

enum nq_next { NQ_NEXT_EVENT = 1, NQ_NEXT_AGAIN = 0, NQ_NEXT_END = -1, NQ_NEXT_ERROR = -2 };

typedef const nq_adapter_schema* (*nq_adapter_describe_fn)(void);
typedef void* (*nq_adapter_init_fn)(const nq_params* params);
typedef int (*nq_adapter_next_fn)(void* state, nq_mbo* out);
typedef int (*nq_adapter_instrument_fn)(void* state, uint32_t id, nq_instrument* out);
typedef void (*nq_adapter_destroy_fn)(void* state);

#define NQ_SYM_ADAPTER_DESCRIBE "nq_adapter_describe"
#define NQ_SYM_ADAPTER_INIT "nq_adapter_init"
#define NQ_SYM_ADAPTER_NEXT "nq_adapter_next"
#define NQ_SYM_ADAPTER_INSTRUMENT "nq_adapter_instrument"
#define NQ_SYM_ADAPTER_DESTROY "nq_adapter_destroy"

#ifdef __cplusplus
}
#endif

#endif
