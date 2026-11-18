#ifndef NQ_ABI_H
#define NQ_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NQ_SDK_VERSION "0.4.0"

#if defined(_WIN32)
#define NQ_EXPORT __declspec(dllexport)
#else
#define NQ_EXPORT __attribute__((visibility("default")))
#endif

enum nq_side { NQ_BID = 0, NQ_ASK = 1 };

enum nq_action { NQ_ADD = 0, NQ_CANCEL = 1, NQ_MODIFY = 2, NQ_TRADE = 3, NQ_CLEAR = 4 };

enum nq_requires { NQ_REQUIRES_NONE = 0, NQ_REQUIRES_L1 = 1, NQ_REQUIRES_MBP = 2, NQ_REQUIRES_MBO = 3 };

enum nq_feed { NQ_FEED_MBO = 0, NQ_FEED_OHLCV = 1 };

#define NQ_PRICE_SCALE 10000000000LL
#define NQ_QTY_SCALE 100000000LL

typedef struct nq_mbo {
    int64_t ts;
    uint64_t id;
    int64_t price;
    int64_t qty;
    uint32_t instrument;
    uint8_t side;
    uint8_t action;
} nq_mbo;

typedef struct nq_ohlcv {
    int64_t ts;
    int64_t open;
    int64_t high;
    int64_t low;
    int64_t close;
    double volume;
    uint32_t instrument;
} nq_ohlcv;

typedef struct nq_kv {
    const char* key;
    const char* value;
} nq_kv;

typedef struct nq_params {
    const char* symbol;
    int64_t tick_size;
    int64_t lot_size;
    int64_t price_scale;
    int64_t qty_scale;
    uint32_t nkv;
    const nq_kv* kv;
} nq_params;

#ifdef __cplusplus
}
#endif

#endif
