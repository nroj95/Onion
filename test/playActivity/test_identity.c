#include "test_identity_tests.h"
#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>

/* =============================================================================
 * purpose:
 * run the focused host-side play activity test suite.
 *
 * key behavior:
 * - runs each identity and database test group.
 * - prints the combined pass and failure totals.
 * - returns failure when any assertion fails.
 * =============================================================================
 */

int main(void)
{
    printf("play activity identity tests\n\n");

    test_raw_identity();
    test_single_file_zip_identity();
    test_cue_identity();
    test_m3u_identity();
    test_identity_schema_storage();
    test_identity_rom_merge();

    int tests_run = test_count_run();
    int tests_failed = test_count_failed();

    printf(
        "\nsummary: %d run, %d passed, %d failed\n",
        tests_run,
        tests_run - tests_failed,
        tests_failed
    );

    return tests_failed == 0
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
