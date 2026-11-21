#pragma once
#include <expected>
#include <string>
#include <utility>
namespace vortex {
struct Error { std::string code; std::string message; };
template <typename T> using Result = std::expected<T, Error>;
inline Error make_error(std::string code, std::string message) {
    return Error{std::move(code), std::move(message)};
}
} // namespace vortex
