#include "native_messaging_stream.hpp"
#include "native_messaging_frame.hpp"
#include <array>
#include <cstddef>
#include <istream>
#include <ostream>
#include <vector>
#include <utility>
namespace vortex {
Result<std::optional<std::string>> read_native_message(std::istream& input) {
 std::array<std::byte,4> header{};
 input.read(reinterpret_cast<char*>(header.data()),4);
 if(input.gcount()==0 && input.eof()) return std::optional<std::string>{};
 if(input.gcount()!=4) return std::unexpected(VortexError::make(ErrorCode::parse_error,"Truncated Native Messaging header"));
 const auto b0=std::to_integer<std::uint32_t>(header[0]), b1=std::to_integer<std::uint32_t>(header[1]), b2=std::to_integer<std::uint32_t>(header[2]), b3=std::to_integer<std::uint32_t>(header[3]);
 const auto length=b0|(b1<<8U)|(b2<<16U)|(b3<<24U);
 if(length==0U || length>NativeMessagingFrame::maximum_payload_bytes) return std::unexpected(VortexError::make(ErrorCode::parse_error,"Invalid Native Messaging payload length"));
 std::vector<std::byte> frame; frame.reserve(static_cast<std::size_t>(length)+4U); frame.insert(frame.end(),header.begin(),header.end());
 const auto offset=frame.size(); frame.resize(offset+static_cast<std::size_t>(length));
 input.read(reinterpret_cast<char*>(frame.data()+offset),static_cast<std::streamsize>(length));
 if(input.gcount()!=static_cast<std::streamsize>(length)) return std::unexpected(VortexError::make(ErrorCode::parse_error,"Truncated Native Messaging payload"));
 auto decoded=NativeMessagingFrame::decode(frame); if(!decoded) return std::unexpected(decoded.error());
 return std::optional<std::string>{std::move(*decoded)};
}
Status write_native_message(std::ostream& output,std::string_view payload) {
 auto encoded=NativeMessagingFrame::encode(payload); if(!encoded) return std::unexpected(encoded.error());
 output.write(reinterpret_cast<const char*>(encoded->data()),static_cast<std::streamsize>(encoded->size())); output.flush();
 if(!output.good()) return std::unexpected(VortexError::make(ErrorCode::io_error,"Unable to write Native Messaging frame"));
 return {};
}
} // namespace vortex
