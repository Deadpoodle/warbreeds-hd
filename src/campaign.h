#pragma once
// Campaign progress kept between sessions: recorded at each briefing, offered
// as "Continue" on the campaign screen.
#include <string>

namespace campaign {

void Install(const std::string& dir, void (*log)(const char*, ...));

}  // namespace campaign
