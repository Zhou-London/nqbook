#pragma once

#include <dlfcn.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <nq/adapter_abi.h>
#include <nq/host.hpp>

#ifndef NQ_ADAPTER_ABI_HASH
#error "NQ_ADAPTER_ABI_HASH is not defined: link the target against nqcppsdk::nqcppsdk"
#endif

namespace nq::host {

struct AdapterFns {
    nq_adapter_describe_fn describe = nullptr;
    nq_adapter_init_fn init = nullptr;
    nq_adapter_start_fn start = nullptr;
    nq_adapter_stop_fn stop = nullptr;
    nq_adapter_destroy_fn destroy = nullptr;
    nq_adapter_instrument_fn instrument = nullptr;
};

class AdapterInstance {
public:
    AdapterInstance(void* state, const AdapterFns& fns, const nq_adapter_schema* schema,
                    std::unique_ptr<nq_sink> sink) noexcept
        : state_(state), fns_(fns), schema_(schema), sink_(std::move(sink)) {}

    ~AdapterInstance() { Release(); }

    AdapterInstance(const AdapterInstance&) = delete;
    AdapterInstance& operator=(const AdapterInstance&) = delete;

    AdapterInstance(AdapterInstance&& other) noexcept
        : state_(std::exchange(other.state_, nullptr)),
          fns_(other.fns_),
          schema_(other.schema_),
          sink_(std::move(other.sink_)),
          started_(std::exchange(other.started_, false)) {}

    AdapterInstance& operator=(AdapterInstance&& other) noexcept {
        if (this != &other) {
            Release();
            state_ = std::exchange(other.state_, nullptr);
            fns_ = other.fns_;
            schema_ = other.schema_;
            sink_ = std::move(other.sink_);
            started_ = std::exchange(other.started_, false);
        }
        return *this;
    }

    const nq_adapter_schema& Schema() const noexcept { return *schema_; }

    void Start() {
        if (started_) return;
        if (fns_.start(state_) != 0) throw PluginError(std::string("adapter start failed: ") + schema_->name);
        started_ = true;
    }

    void Stop() noexcept {
        if (!started_) return;
        fns_.stop(state_);
        started_ = false;
    }

    std::optional<nq_instrument> Instrument(std::uint32_t id) const noexcept {
        nq_instrument out;
        if (fns_.instrument(state_, id, &out) != 0) return std::nullopt;
        return out;
    }

    void* Raw() noexcept { return state_; }

private:
    void Release() noexcept {
        if (state_) {
            Stop();
            fns_.destroy(state_);
            state_ = nullptr;
        }
    }

    void* state_;
    AdapterFns fns_;
    const nq_adapter_schema* schema_;
    std::unique_ptr<nq_sink> sink_;
    bool started_ = false;
};

class AdapterPlugin {
public:
    explicit AdapterPlugin(const std::string& path) : path_(path) {
        handle_ = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle_) throw PluginLoadError("dlopen failed: " + path + ": " + DlError());
        try {
            fns_.describe = Resolve<nq_adapter_describe_fn>(NQ_SYM_ADAPTER_DESCRIBE);
            fns_.init = Resolve<nq_adapter_init_fn>(NQ_SYM_ADAPTER_INIT);
            fns_.start = Resolve<nq_adapter_start_fn>(NQ_SYM_ADAPTER_START);
            fns_.stop = Resolve<nq_adapter_stop_fn>(NQ_SYM_ADAPTER_STOP);
            fns_.destroy = Resolve<nq_adapter_destroy_fn>(NQ_SYM_ADAPTER_DESTROY);
            fns_.instrument = Resolve<nq_adapter_instrument_fn>(NQ_SYM_ADAPTER_INSTRUMENT);
            schema_ = fns_.describe();
            if (!schema_) throw PluginLoadError("nq_adapter_describe returned null: " + path);
            if (schema_->abi_hash != NQ_ADAPTER_ABI_HASH) {
                throw AbiMismatchError("adapter abi hash mismatch: " + path + " built against " +
                                       Hex(schema_->abi_hash) + ", host is " + Hex(NQ_ADAPTER_ABI_HASH));
            }
            if (!schema_->name) throw PluginLoadError("adapter schema has no name: " + path);
        } catch (...) {
            ::dlclose(handle_);
            handle_ = nullptr;
            throw;
        }
    }

    ~AdapterPlugin() {
        if (handle_) ::dlclose(handle_);
    }

    AdapterPlugin(const AdapterPlugin&) = delete;
    AdapterPlugin& operator=(const AdapterPlugin&) = delete;

    AdapterPlugin(AdapterPlugin&& other) noexcept
        : path_(std::move(other.path_)),
          handle_(std::exchange(other.handle_, nullptr)),
          fns_(other.fns_),
          schema_(other.schema_) {}

    AdapterPlugin& operator=(AdapterPlugin&& other) noexcept {
        if (this != &other) {
            if (handle_) ::dlclose(handle_);
            path_ = std::move(other.path_);
            handle_ = std::exchange(other.handle_, nullptr);
            fns_ = other.fns_;
            schema_ = other.schema_;
        }
        return *this;
    }

    const std::string& Path() const noexcept { return path_; }
    const nq_adapter_schema& Schema() const noexcept { return *schema_; }
    const AdapterFns& Fns() const noexcept { return fns_; }

    AdapterInstance Create(const nq_params& params, const nq_sink& sink) const {
        auto owned = std::make_unique<nq_sink>(sink);
        void* state = fns_.init(&params, owned.get());
        if (!state) throw PluginInitError("nq_adapter_init failed: " + path_);
        return AdapterInstance(state, fns_, schema_, std::move(owned));
    }

private:
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
    AdapterFns fns_;
    const nq_adapter_schema* schema_ = nullptr;
};

}  // namespace nq::host
