#pragma once

#include <stdexcept>

namespace nq::host {

class PluginError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class PluginLoadError : public PluginError {
public:
    using PluginError::PluginError;
};

class AbiMismatchError : public PluginError {
public:
    using PluginError::PluginError;
};

class PluginInitError : public PluginError {
public:
    using PluginError::PluginError;
};

}  // namespace nq::host
