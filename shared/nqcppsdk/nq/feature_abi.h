#ifndef NQ_FEATURE_ABI_H
#define NQ_FEATURE_ABI_H

#include <stddef.h>
#include <stdint.h>

#include <nq/abi.h>

#ifdef __cplusplus
extern "C" {
#endif

enum nq_column_kind { NQ_COLUMN_SNAPSHOT = 0, NQ_COLUMN_CUMULATIVE = 1 };

enum nq_control_type {
    NQ_CONTROL_RESET = 0,
    NQ_CONTROL_SESSION_START = 1,
    NQ_CONTROL_SESSION_END = 2
};

typedef struct nq_event {
    int64_t ts;
    uint64_t id;
    int64_t price;
    int64_t qty;
    int64_t filled;
    uint8_t side;
    uint8_t action;
} nq_event;

typedef struct nq_level {
    int64_t price;
    int64_t qty;
    uint32_t count;
} nq_level;

typedef struct nq_book_view {
    const void* ctx;
    int (*best)(const void* ctx, uint8_t side, nq_level* out);
    int (*level)(const void* ctx, uint8_t side, uint32_t k, nq_level* out);
    int64_t (*ts)(const void* ctx);
} nq_book_view;

typedef struct nq_control {
    uint8_t type;
    int64_t ts;
} nq_control;

typedef struct nq_column {
    const char* name;
    const char* unit;
    const char* doc;
    uint8_t kind;
} nq_column;

typedef struct nq_schema {
    uint64_t abi_hash;
    const char* sdk_version;
    uint8_t requirement;
    uint32_t ncol;
    const nq_column* cols;
    uint8_t feed;
} nq_schema;

typedef struct nq_cross_panel {
    int64_t ts;
    uint32_t nrow;
    uint32_t stride;
    const double* in;
    double* out;
    const uint64_t* valid;
    const uint32_t* changed;
    uint32_t nchanged;
} nq_cross_panel;

typedef struct nq_cross_schema {
    uint32_t nin;
    const char* const* inputs;
    uint32_t nout;
    const nq_column* outs;
} nq_cross_schema;

typedef const nq_schema* (*nq_describe_fn)(void);
typedef void* (*nq_init_fn)(const nq_params* params);
typedef void (*nq_destroy_fn)(void* state);
typedef void (*nq_on_event_fn)(void* state, const nq_event* event, const nq_book_view* book);
typedef void (*nq_on_ohlcv_fn)(void* state, const nq_ohlcv* ohlcv);
typedef void (*nq_on_warmup_fn)(void* state, const nq_ohlcv* ohlcv);
typedef void (*nq_read_fn)(void* state, const nq_book_view* book, double* out);
typedef void (*nq_on_control_fn)(void* state, const nq_control* control);

typedef const nq_cross_schema* (*nq_cross_describe_fn)(void);
typedef void* (*nq_cross_init_fn)(const nq_params* params);
typedef void (*nq_cross_destroy_fn)(void* cross);
typedef void (*nq_cross_apply_fn)(void* cross, const nq_cross_panel* panel);
typedef void (*nq_cross_bind_fn)(void* state, const double* row, size_t stride);

#define NQ_SYM_DESCRIBE "nq_describe"
#define NQ_SYM_INIT "nq_init"
#define NQ_SYM_DESTROY "nq_destroy"
#define NQ_SYM_ON_EVENT "nq_on_event"
#define NQ_SYM_ON_OHLCV "nq_on_ohlcv"
#define NQ_SYM_ON_WARMUP "nq_on_warmup"
#define NQ_SYM_READ "nq_read"
#define NQ_SYM_ON_CONTROL "nq_on_control"
#define NQ_SYM_CROSS_DESCRIBE "nq_cross_describe"
#define NQ_SYM_CROSS_INIT "nq_cross_init"
#define NQ_SYM_CROSS_DESTROY "nq_cross_destroy"
#define NQ_SYM_CROSS_APPLY "nq_cross_apply"
#define NQ_SYM_CROSS_BIND "nq_cross_bind"

#ifdef __cplusplus
}
#endif

#endif
