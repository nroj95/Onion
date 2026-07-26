#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =============================================================================
 * purpose:
 * implement shared helpers for host-side play activity tests.
 *
 * key behavior:
 * - keeps assertion counters private to this module.
 * - prints one result for each assertion.
 * - writes deterministic binary fixture contents.
 * - reports command success through the process exit status.
 * =============================================================================
 */

static int tests_run = 0;
static int tests_failed = 0;

void check_condition(
    bool condition,
    const char *test_name
)
{
    tests_run++;

    if (condition) {
        printf("pass: %s\n", test_name);
        return;
    }

    tests_failed++;
    fprintf(stderr, "fail: %s\n", test_name);
}

bool run_command(const char *command)
{
    return system(command) == 0;
}

bool write_fixture(
    const char *path,
    const char *contents
)
{
    FILE *file = fopen(path, "wb");

    if (file == NULL)
        return false;

    size_t content_size = strlen(contents);

    bool written =
        fwrite(contents, 1, content_size, file) == content_size;

    fclose(file);

    return written;
}

int test_count_run(void)
{
    return tests_run;
}

int test_count_failed(void)
{
    return tests_failed;
}
