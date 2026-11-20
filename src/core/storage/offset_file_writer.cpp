#include "offset_file_writer.hpp"

#include <limits>
#include <string>

namespace vortex {

OffsetFileWriter::~OffsetFileWriter() { (void)close(); }

Status OffsetFileWriter::open(const std::filesystem::path& path, const std::uint64_t size) {
    std::scoped_lock lock(mutex_);
    if (file_.is_open()) return std::unexpected(VortexError::make(
        ErrorCode::io_error, "Partial file writer is already open"
    ));
    if (size > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Requested partial file is too large"
        ));
    }

    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return std::unexpected(VortexError::make(
            ErrorCode::io_error, "Unable to create partial file directory", ec
        ));
    }
    file_.open(path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    if (!file_) return std::unexpected(VortexError::make(
        ErrorCode::io_error, "Unable to open partial file: " + path.string()
    ));

    if (size > 0) {
        file_.seekp(static_cast<std::streamoff>(size - 1));
        file_.put('\0');
        file_.flush();
        if (!file_) {
            file_.close();
            return std::unexpected(VortexError::make(
                ErrorCode::io_error, "Unable to preallocate partial file"
            ));
        }
    }
    size_ = size;
    return {};
}

Status OffsetFileWriter::write_at(
    const std::uint64_t offset, const std::span<const std::byte> data
) {
    std::scoped_lock lock(mutex_);
    if (!file_.is_open()) return std::unexpected(VortexError::make(
        ErrorCode::io_error, "Partial file writer is not open"
    ));
    const auto length = static_cast<std::uint64_t>(data.size());
    if (offset > size_ || length > size_ - offset ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        length > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Write range is outside the partial file"
        ));
    }
    if (data.empty()) return {};
    file_.clear();
    file_.seekp(static_cast<std::streamoff>(offset));
    file_.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
    if (!file_) return std::unexpected(VortexError::make(
        ErrorCode::io_error, "Partial file write failed"
    ));
    return {};
}

Status OffsetFileWriter::flush() {
    std::scoped_lock lock(mutex_);
    if (!file_.is_open()) return std::unexpected(VortexError::make(
        ErrorCode::io_error, "Partial file writer is not open"
    ));
    file_.flush();
    if (!file_) return std::unexpected(VortexError::make(
        ErrorCode::io_error, "Partial file flush failed"
    ));
    return {};
}

Status OffsetFileWriter::close() {
    std::scoped_lock lock(mutex_);
    if (!file_.is_open()) return {};
    file_.flush();
    const bool okay = static_cast<bool>(file_);
    file_.close();
    size_ = 0;
    if (!okay) return std::unexpected(VortexError::make(
        ErrorCode::io_error, "Partial file flush failed during close"
    ));
    return {};
}

} // namespace vortex
