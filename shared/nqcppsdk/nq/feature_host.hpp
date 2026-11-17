#pragma once

#include <dlfcn.h>

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <nq/feature_abi.h>
#include <nq/plugin_error.hpp>

#ifndef NQ_ABI_HASH
#error "NQ_ABI_HASH is not defined: link the target against nqcppsdk::nqcppsdk"
#endif

namespace nq::host {

struct PluginFns {
    nq_describe_fn describe = nullptr;
    nq_init_fn init = nullptr;
    nq_destroy_fn destroy = nullptr;
    nq_on_event_fn on_event = nullptr;
    nq_on_ohlcv_fn on_ohlcv = nullptr;
    nq_read_fn read = nullptr;
    nq_on_control_fn on_control = nullptr;
    nq_cross_describe_fn cross_describe = nullptr;
    nq_cross_init_fn cross_init = nullptr;
    nq_cross_destroy_fn cross_destroy = nullptr;
    nq_cross_apply_fn cross_apply = nullptr;
    nq_cross_bind_fn cross_bind = nullptr;
};

class PluginInstance {
public:
    PluginInstance(void* state, const PluginFns& fns, const nq_schema* schema) noexcept
        : state_(state), fns_(fns), schema_(schema) {}

    ~PluginInstance() { Release(); }

    PluginInstance(const PluginInstance&) = delete;
    PluginInstance& operator=(const PluginInstance&) = delete;

    PluginInstance(PluginInstance&& other) noexcept
        : state_(std::exchange(other.state_, nullptr)), fns_(other.fns_), schema_(other.schema_) {}

    PluginInstance& operator=(PluginInstance&& other) noexcept {
        if (this != &other) {
            Release();
            state_ = std::exchange(other.state_, nullptr);
            fns_ = other.fns_;
            schema_ = other.schema_;
        }
        return *this;
    }

    const nq_schema& Schema() const noexcept { return *schema_; }
    std::uint32_t Ncol() const noexcept { return schema_->ncol; }

    void OnEvent(const nq_event& e, const nq_book_view& book) noexcept {
        fns_.on_event(state_, &e, &book);
    }

    void OnOhlcv(const nq_ohlcv& ohlcv) noexcept { fns_.on_ohlcv(state_, &ohlcv); }

    void Read(std::span<double> out, const nq_book_view* book) {
        if (out.size() < schema_->ncol) throw std::length_error("nq::host::PluginInstance::Read: output too small");
        fns_.read(state_, book, out.data());
    }

    void OnControl(const nq_control& c) noexcept { fns_.on_control(state_, &c); }

    void BindCross(const double* row, std::size_t stride) noexcept {
        if (fns_.cross_bind) fns_.cross_bind(state_, row, stride);
    }

    void* Raw() noexcept { return state_; }

private:
    void Release() noexcept {
        if (state_) {
            fns_.destroy(state_);
            state_ = nullptr;
        }
    }

    void* state_;
    PluginFns fns_;
    const nq_schema* schema_;
};

class CrossInstance {
public:
    CrossInstance(void* state, const PluginFns& fns) noexcept : state_(state), fns_(fns) {}

    ~CrossInstance() { Release(); }

    CrossInstance(const CrossInstance&) = delete;
    CrossInstance& operator=(const CrossInstance&) = delete;

    CrossInstance(CrossInstance&& other) noexcept : state_(std::exchange(other.state_, nullptr)), fns_(other.fns_) {}

    CrossInstance& operator=(CrossInstance&& other) noexcept {
        if (this != &other) {
            Release();
            state_ = std::exchange(other.state_, nullptr);
            fns_ = other.fns_;
        }
        return *this;
    }

    void Apply(const nq_cross_panel& panel) noexcept { fns_.cross_apply(state_, &panel); }

    void* Raw() noexcept { return state_; }

private:
    void Release() noexcept {
        if (state_) {
            fns_.cross_destroy(state_);
            state_ = nullptr;
        }
    }

    void* state_;
    PluginFns fns_;
};

class Plugin {
public:
    explicit Plugin(const std::string& path) : path_(path) {
        handle_ = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle_) throw PluginLoadError("dlopen failed: " + path + ": " + DlError());
        try {
            fns_.describe = Resolve<nq_describe_fn>(NQ_SYM_DESCRIBE);
            fns_.init = Resolve<nq_init_fn>(NQ_SYM_INIT);
            fns_.destroy = Resolve<nq_destroy_fn>(NQ_SYM_DESTROY);
            fns_.read = Resolve<nq_read_fn>(NQ_SYM_READ);
            fns_.on_control = Resolve<nq_on_control_fn>(NQ_SYM_ON_CONTROL);
            schema_ = fns_.describe();
            if (!schema_) throw PluginLoadError("nq_describe returned null: " + path);
            if (schema_->abi_hash != NQ_ABI_HASH) {
                throw AbiMismatchError("abi hash mismatch: " + path + " built against " +
                                         Hex(schema_->abi_hash) + ", host is " + Hex(NQ_ABI_HASH));
            }
            if (schema_->ncol > 0 && !schema_->cols) throw PluginLoadError("schema has no columns: " + path);
            if (schema_->feed == NQ_FEED_MBO) {
                fns_.on_event = Resolve<nq_on_event_fn>(NQ_SYM_ON_EVENT);
            } else if (schema_->feed == NQ_FEED_OHLCV) {
                fns_.on_ohlcv = Resolve<nq_on_ohlcv_fn>(NQ_SYM_ON_OHLCV);
            } else {
                throw PluginLoadError("schema has an unknown feed: " + path);
            }
            fns_.cross_describe = Find<nq_cross_describe_fn>(NQ_SYM_CROSS_DESCRIBE);
            if (fns_.cross_describe) {
                fns_.cross_init = Resolve<nq_cross_init_fn>(NQ_SYM_CROSS_INIT);
                fns_.cross_destroy = Resolve<nq_cross_destroy_fn>(NQ_SYM_CROSS_DESTROY);
                fns_.cross_apply = Resolve<nq_cross_apply_fn>(NQ_SYM_CROSS_APPLY);
                fns_.cross_bind = Find<nq_cross_bind_fn>(NQ_SYM_CROSS_BIND);
                cross_schema_ = fns_.cross_describe();
                if (!cross_schema_) throw PluginLoadError("nq_cross_describe returned null: " + path);
                if (cross_schema_->nin == 0 || !cross_schema_->inputs) {
                    throw PluginLoadError("cross schema has no inputs: " + path);
                }
                if (cross_schema_->nout == 0 || !cross_schema_->outs) {
                    throw PluginLoadError("cross schema has no columns: " + path);
                }
            }
        } catch (...) {
            ::dlclose(handle_);
            handle_ = nullptr;
            throw;
        }
    }

    ~Plugin() {
        if (handle_) ::dlclose(handle_);
    }

    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;

    Plugin(Plugin&& other) noexcept
        : path_(std::move(other.path_)),
          handle_(std::exchange(other.handle_, nullptr)),
          fns_(other.fns_),
          schema_(other.schema_),
          cross_schema_(other.cross_schema_) {}

    Plugin& operator=(Plugin&& other) noexcept {
        if (this != &other) {
            if (handle_) ::dlclose(handle_);
            path_ = std::move(other.path_);
            handle_ = std::exchange(other.handle_, nullptr);
            fns_ = other.fns_;
            schema_ = other.schema_;
            cross_schema_ = other.cross_schema_;
        }
        return *this;
    }

    const std::string& Path() const noexcept { return path_; }
    const nq_schema& Schema() const noexcept { return *schema_; }
    const PluginFns& Fns() const noexcept { return fns_; }

    std::span<const nq_column> Columns() const noexcept {
        return std::span<const nq_column>(schema_->cols, schema_->ncol);
    }

    PluginInstance Create(const nq_params& params) const {
        void* state = fns_.init(&params);
        if (!state) throw PluginInitError("nq_init failed: " + path_);
        return PluginInstance(state, fns_, schema_);
    }

    bool HasCross() const noexcept { return cross_schema_ != nullptr; }
    const nq_cross_schema& CrossSchema() const noexcept { return *cross_schema_; }

    std::span<const char* const> CrossInputs() const noexcept {
        return std::span<const char* const>(cross_schema_->inputs, cross_schema_->nin);
    }

    std::span<const nq_column> CrossColumns() const noexcept {
        return std::span<const nq_column>(cross_schema_->outs, cross_schema_->nout);
    }

    CrossInstance CreateCross(const nq_params& params) const {
        if (!cross_schema_) throw PluginInitError("plugin has no cross: " + path_);
        void* state = fns_.cross_init(&params);
        if (!state) throw PluginInitError("nq_cross_init failed: " + path_);
        return CrossInstance(state, fns_);
    }

private:
    template <class Fn>
    Fn Find(const char* name) const noexcept {
        return reinterpret_cast<Fn>(::dlsym(handle_, name));
    }

    template <class Fn>
    Fn Resolve(const char* name) const {
        void* sym = ::dlsym(handle_, name);
        if (!sym) throw PluginLoadError(std::string("missing symbol ") + name + ": " + path_);
        return reinterpret_cast<Fn>(sym);
    }

    static std::string DlError() {
        const char* e = ::dlerror();
        return e ? e : "unknown error";
    }

    static std::string Hex(std::uint64_t v) {
        static constexpr char digits[] = "0123456789abcdef";
        std::string s = "0x";
        for (int shift = 60; shift >= 0; shift -= 4) s += digits[(v >> shift) & 0xf];
        return s;
    }

    std::string path_;
    void* handle_ = nullptr;
    PluginFns fns_;
    const nq_schema* schema_ = nullptr;
    const nq_cross_schema* cross_schema_ = nullptr;
};

}  // namespace nq::host
