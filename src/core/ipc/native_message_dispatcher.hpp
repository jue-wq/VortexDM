#pragma once

#include "core/common/result.hpp"
#include "core/storage/sqlite_database.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace vortex {

// Validates extension messages and only ACKs downloads after their durable
// SQLite insert has committed. An empty response means the handshake was accepted.
class NativeMessageDispatcher final {
public:
    explicit NativeMessageDispatcher(SqliteDatabase& database) : database_(database) {}
    [[nodiscard]] Result<std::optional<std::string>> dispatch(std::string_view json);

private:
    SqliteDatabase& database_;
};

} // namespace vortex
