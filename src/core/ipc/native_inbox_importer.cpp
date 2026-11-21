#include "native_inbox_importer.hpp"

namespace vortex {

Result<std::size_t> NativeInboxImporter::import_pending(
    SqliteDatabase& inbox, SqliteDatabase& application_database,
    const std::size_t limit
) {
    if (&inbox == &application_database) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument,
            "Native inbox and application database must be different connections"
        ));
    }
    const auto pending = inbox.list_pending_native_requests(limit);
    if (!pending) return std::unexpected(pending.error());

    std::size_t imported = 0;
    for (const auto& entry : *pending) {
        const auto inserted = application_database.enqueue_download_once(
            entry.request_id, entry.download
        );
        if (!inserted) return std::unexpected(inserted.error());

        // If this write fails or the process exits here, the next pass safely
        // replays the same request ID into the destination without duplication.
        const auto marked = inbox.mark_native_request_imported(entry.request_id);
        if (!marked) return std::unexpected(marked.error());
        ++imported;
    }
    return imported;
}

} // namespace vortex
