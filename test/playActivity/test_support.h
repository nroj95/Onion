#ifndef PLAY_ACTIVITY_TEST_SUPPORT_H
#define PLAY_ACTIVITY_TEST_SUPPORT_H

#include <stdbool.h>

/* =============================================================================
 * purpose:
 * provide shared helpers for host-side play activity tests.
 *
 * key behavior:
 * - records passed and failed assertions.
 * - creates small fixture files.
 * - runs fixture preparation commands.
 * - exposes the final test counts to the test runner.
 * =============================================================================
 */

void check_condition(
    bool condition,
    const char *test_name
);

bool run_command(const char *command);

bool write_fixture(
    const char *path,
    const char *contents
);

int test_count_run(void);
int test_count_failed(void);

#endif
