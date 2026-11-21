#include "vortex/task.hpp"
#include "vortex/url.hpp"
#include <iostream>
int main() {
    const auto valid = vortex::parse_http_url("https://example.com/video.mp4");
    if (!valid || valid->host != "example.com") return 1;
    if (vortex::parse_http_url("file:///etc/passwd")) return 2;
    if (vortex::parse_http_url("javascript:alert(1)")) return 3;
    if (vortex::parse_http_url("https://example.com/a b")) return 4;
    if (!vortex::can_transition(vortex::TaskState::Added, vortex::TaskState::Queued)) return 5;
    if (vortex::can_transition(vortex::TaskState::Completed, vortex::TaskState::Downloading)) return 6;
    std::cout << "core tests passed\n";
    return 0;
}
