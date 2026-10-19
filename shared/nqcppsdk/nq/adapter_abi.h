#ifndef NQ_ADAPTER_ABI_H
#define NQ_ADAPTER_ABI_H

#include <stddef.h>
#include <stdint.h>

#include <nq/abi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nq_mbo {
    int64_t ts;
    uint64_t id;
    int64_t price;
    int64_t qty;
    uint32_t instrument;
    uint8_t side;
    uint8_t action;
} nq_mbo;

typedef struct nq_sink {
    void* ctx;
    void (*push)(void* ctx, const nq_mbo* m);
    void (*end)(void* ctx, int status);
} nq_sink;

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

typedef const nq_adapter_schema* (*nq_adapter_describe_fn)(void);
typedef void* (*nq_adapter_init_fn)(const nq_params* params, const nq_sink* sink);
typedef int (*nq_adapter_start_fn)(void* state);
typedef void (*nq_adapter_stop_fn)(void* state);
typedef void (*nq_adapter_destroy_fn)(void* state);
typedef int (*nq_adapter_instrument_fn)(void* state, uint32_t id, nq_instrument* out);

#define NQ_SYM_ADAPTER_DESCRIBE "nq_adapter_describe"
#define NQ_SYM_ADAPTER_INIT "nq_adapter_init"
#define NQ_SYM_ADAPTER_START "nq_adapter_start"
#define NQ_SYM_ADAPTER_STOP "nq_adapter_stop"
#define NQ_SYM_ADAPTER_DESTROY "nq_adapter_destroy"
#define NQ_SYM_ADAPTER_INSTRUMENT "nq_adapter_instrument"

#ifdef __cplusplus
}
#endif

#endif
