#pragma once
#include <stdint.h>

namespace SelfTest {
    struct Report {
        uint32_t total;
        uint32_t passed;
        uint32_t failed;
    };

    // Execute internal kernel corroboration tests at boot and report to Serial + EarlyTerm
    Report RunAll();
}
