#pragma once

#include "core/common/result.hpp"
#include "core/storage/sqlite_database.hpp"

#include <cstddef>

namespace vortex {

// Idempotently copies durable native-host inbox entries into the application's
// download database. Replaying after a crash is safe because the destination
// binds the same request ID in the same transaction as the download row.
class NativeInboxImporter final {
public:
    [[nodiscard]] static Result<std::size_t> import_pending(
        SqliteDatabase& inbox, SqliteDatabase& application_database,
        std::size_t limit = 100
    );
};

} // namespace vortex
