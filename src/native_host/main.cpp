#include "core/ipc/native_message_dispatcher.hpp"
#include "core/ipc/native_messaging_stream.hpp"

#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include <iostream>
#include <string_view>

namespace {

constexpr std::string_view allowed_firefox_extension = "browser-bridge@vortexdm.app";

[[nodiscard]] bool authorized_origin(const int argc, char** argv) {
    // Firefox passes the extension ID as argv[1]. Chrome's origin format is
    // intentionally not accepted until a stable Chrome extension ID is pinned.
    return argc >= 2 && std::string_view(argv[1]) == allowed_firefox_extension;
}

[[nodiscard]] std::optional<std::filesystem::path> resolve_database_path() {
    if (const char* override_path = std::getenv("VORTEXDM_DB_PATH");
        override_path != nullptr && *override_path != '\0') {
        return std::filesystem::path(override_path);
    }

#ifdef _WIN32
    const char* data_root = std::getenv("LOCALAPPDATA");
    if (data_root == nullptr || *data_root == '\0') return std::nullopt;
    return std::filesystem::path(data_root) / "VortexDM" / "native-inbox.sqlite";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') return std::nullopt;
    return std::filesystem::path(home) / "Library" / "Application Support" /
           "VortexDM" / "native-inbox.sqlite";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / "vortexdm" / "native-inbox.sqlite";
    }
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') return std::nullopt;
    return std::filesystem::path(home) / ".local" / "share" / "vortexdm" / "native-inbox.sqlite";
#endif
}

[[nodiscard]] int fail(const char* message, const int code) {
    std::cerr << "VortexDM native host: " << message << '\n';
    return code;
}

} // namespace

int main(const int argc, char** argv) {
#ifdef _WIN32
    // Native Messaging frames are binary even on Windows; disable CRT newline translation.
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 ||
        _setmode(_fileno(stdout), _O_BINARY) == -1) {
        return fail("could not switch standard streams to binary mode", 1);
    }
#endif
    if (!authorized_origin(argc, argv)) {
        return fail("unauthorized or unpinned browser origin", 2);
    }

    const auto database_path = resolve_database_path();
    if (!database_path) {
        return fail("could not resolve a per-user data directory", 3);
    }
    std::error_code directory_error;
    std::filesystem::create_directories(database_path->parent_path(), directory_error);
    if (directory_error) {
        return fail("could not create the native inbox data directory", 3);
    }

    vortex::SqliteDatabase database;
    if (const auto opened = database.open(*database_path); !opened) {
        return fail("could not open native inbox database", 4);
    }
    if (const auto migrated = database.migrate(); !migrated) {
        (void)database.close();
        return fail("could not initialize application database", 5);
    }

    vortex::NativeMessageDispatcher dispatcher(database);
    bool handshake_accepted = false;

    while (true) {
        auto incoming = vortex::read_native_message(std::cin);
        if (!incoming) {
            (void)database.close();
            return fail("invalid or incomplete Native Messaging frame", 6);
        }
        if (!incoming->has_value()) break;

        auto response = dispatcher.dispatch(**incoming);
        if (!response) {
            (void)database.close();
            return fail("invalid protocol message", 7);
        }

        // A handshake is mandatory and must be the first frame. The dispatcher
        // validates its shape; this loop additionally enforces ordering and
        // rejects duplicate handshakes before accepting subsequent requests.
        bool is_handshake = false;
        try {
            const auto message = nlohmann::json::parse(**incoming);
            is_handshake = message.is_object() && message.value("type", std::string{}) == "handshake";
            if (is_handshake) {
                if (handshake_accepted) {
                    (void)database.close();
                    return fail("duplicate or invalid handshake", 8);
                }
                const auto version = message.value("protocol_version", 0);
                if (version != 1 || !message.contains("extension_version") ||
                    !message["extension_version"].is_string() ||
                    !message.contains("capabilities") || !message["capabilities"].is_array()) {
                    (void)database.close();
                    return fail("unsupported handshake", 8);
                }
                handshake_accepted = true;
                continue;
            }
        } catch (const nlohmann::json::exception&) {
            (void)database.close();
            return fail("invalid JSON message", 7);
        }

        if (!handshake_accepted) {
            (void)database.close();
            return fail("handshake required before requests", 8);
        }
        if (response->has_value()) {
            if (const auto written = vortex::write_native_message(std::cout, **response); !written) {
                (void)database.close();
                return fail("could not write Native Messaging response", 9);
            }
        }
    }

    if (const auto closed = database.close(); !closed) {
        return fail("could not close application database cleanly", 10);
    }
    return 0;
}
