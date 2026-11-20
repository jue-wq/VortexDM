#pragma once

#include "vortex_error.hpp"

#include <expected>

namespace vortex {

template <typename T>
using Result = std::expected<T, VortexError>;

using Status = Result<void>;

} // namespace vortex
