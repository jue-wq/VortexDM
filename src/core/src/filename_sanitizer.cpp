#include "vortex/filename_sanitizer.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace vortex {
std::string sanitize_filename(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    for (const unsigned char ch : input) {
        if (ch < 0x20U || ch == 0x7fU || ch == '/' || ch == '\\' ||
            ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' ||
            ch == '>' || ch == '|') {
            result.push_back('_');
        } else {
            result.push_back(static_cast<char>(ch));
        }
    }
    while (!result.empty() && (result.back() == '.' || result.back() == ' ')) result.pop_back();
    while (!result.empty() && (result.front() == '.' || result.front() == ' ')) result.erase(result.begin());
    if (result.empty()) result = "download";

    std::string upper = result;
    std::transform(upper.begin(), upper.end(), upper.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    const auto dot = upper.find('.');
    const std::string stem = upper.substr(0, dot);
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
        (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
         stem[3] >= '1' && stem[3] <= '9')) {
        result.insert(result.begin(), '_');
    }
    if (result.size() > 240U) result.resize(240U);
    return result;
}
} // namespace vortex
