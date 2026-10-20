#include <nq/feature_abi.h>

static const nq_column cols[1] = {{"x", "", "", NQ_COLUMN_SNAPSHOT}};
static const nq_schema schema = {1u, NQ_SDK_VERSION, NQ_REQUIRES_L1, 1u, cols};
static int state;

NQ_EXPORT const nq_schema* nq_describe(void) { return &schema; }

NQ_EXPORT void* nq_init(const nq_params* params) {
    (void)params;
    return &state;
}

NQ_EXPORT void nq_destroy(void* s) { (void)s; }

NQ_EXPORT void nq_on_event(void* s, const nq_event* e, const nq_book_view* book) {
    (void)s;
    (void)e;
    (void)book;
}

NQ_EXPORT void nq_read(void* s, const nq_book_view* book, double* out) {
    (void)s;
    (void)book;
    out[0] = 1.0;
}

NQ_EXPORT void nq_on_control(void* s, const nq_control* c) {
    (void)s;
    (void)c;
}
