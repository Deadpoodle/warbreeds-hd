#pragma once

namespace headless {
// Hidden, non-activating, silent game window for automated test runs.
void Install(void (*log)(const char*, ...));
}
