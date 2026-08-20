#include "playActivityIdentity.h"
#include "test_support.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* =============================================================================
 * purpose:
 * verify single-file zip identity behavior.
 *
 * key behavior:
 * - confirms archive compression does not affect rom identity.
 * - confirms changed member contents produce a different identity.
 * - uses temporary host-side zip fixtures.
 * =============================================================================
 */

void test_single_file_zip_identity(void)
{
    char temporary_directory[] =
        "/tmp/playactivity-zip-test-XXXXXX";

    if (mkdtemp(temporary_directory) == NULL) {
        check_condition(false, "create zip temporary directory");
        return;
    }

    char member_path[512];
    char first_zip_path[512];
    char second_zip_path[512];
    char changed_zip_path[512];
    char quoted_member_path[512];
    char quoted_zip_path[512];

    snprintf(
        member_path,
        sizeof(member_path),
        "%s/game.rom",
        temporary_directory
    );

    snprintf(
        first_zip_path,
        sizeof(first_zip_path),
        "%s/first.zip",
        temporary_directory
    );

    snprintf(
        second_zip_path,
        sizeof(second_zip_path),
        "%s/second.zip",
        temporary_directory
    );

    snprintf(
        changed_zip_path,
        sizeof(changed_zip_path),
        "%s/changed.zip",
        temporary_directory
    );

    snprintf(
        quoted_member_path,
        sizeof(quoted_member_path),
        "%s/quoted member's game.rom",
        temporary_directory
    );

    snprintf(
        quoted_zip_path,
        sizeof(quoted_zip_path),
        "%s/quoted archive's game.zip",
        temporary_directory
    );

    bool member_written =
        write_fixture(member_path, "same zipped rom contents");

    check_condition(member_written, "write zip member fixture");

    char first_command[2048];
    char second_command[2048];

    snprintf(
        first_command,
        sizeof(first_command),
        "cd '%s' && /usr/bin/7z a -bd -y -tzip -mx=0 '%s' game.rom >/dev/null",
        temporary_directory,
        first_zip_path
    );

    snprintf(
        second_command,
        sizeof(second_command),
        "cd '%s' && /usr/bin/7z a -bd -y -tzip -mx=9 '%s' game.rom >/dev/null",
        temporary_directory,
        second_zip_path
    );

    bool archives_created =
        run_command(first_command) &&
        run_command(second_command);

    check_condition(
        archives_created,
        "create differently compressed zip fixtures"
    );

    RomContentIdentity first_identity;
    RomContentIdentity second_identity;

    bool first_calculated =
        rom_identity_calculate_zip(first_zip_path, &first_identity);

    bool second_calculated =
        rom_identity_calculate_zip(second_zip_path, &second_identity);

    check_condition(
        first_calculated && second_calculated,
        "calculate single-file zip identities"
    );

    check_condition(
        strcmp(first_identity.type, "crc32") == 0,
        "single-file zip identity uses member crc32"
    );

    check_condition(
        strcmp(first_identity.value, second_identity.value) == 0 &&
        first_identity.content_size == second_identity.content_size,
        "recompressed zip preserves member identity"
    );

    bool changed_member_written =
        write_fixture(member_path, "changed zipped rom contents");

    check_condition(
        changed_member_written,
        "rewrite changed zip member fixture"
    );

    char changed_command[2048];

    snprintf(
        changed_command,
        sizeof(changed_command),
        "cd '%s' && /usr/bin/7z a -bd -y -tzip -mx=9 '%s' game.rom >/dev/null",
        temporary_directory,
        changed_zip_path
    );

    check_condition(
        run_command(changed_command),
        "create changed zip fixture"
    );

    RomContentIdentity changed_identity;

    bool changed_calculated =
        rom_identity_calculate_zip(
            changed_zip_path,
            &changed_identity
        );

    check_condition(
        changed_calculated,
        "calculate changed zip identity"
    );

    check_condition(
        strcmp(first_identity.value, changed_identity.value) != 0,
        "changed zip member produces different identity"
    );

    bool quoted_member_written =
        write_fixture(
            quoted_member_path,
            "rom contents in shell-sensitive filenames"
        );

    check_condition(
        quoted_member_written,
        "write shell-sensitive zip member fixture"
    );

    char quoted_command[2048];

    snprintf(
        quoted_command,
        sizeof(quoted_command),
        "cd '%s' && "
        "/usr/bin/7z a -bd -y -tzip -mx=5 "
        "\"quoted archive's game.zip\" "
        "\"quoted member's game.rom\" "
        ">/dev/null",
        temporary_directory
    );

    check_condition(
        run_command(quoted_command),
        "create zip fixture with spaces and apostrophes"
    );

    RomContentIdentity quoted_identity;

    bool quoted_calculated =
        rom_identity_calculate_zip(
            quoted_zip_path,
            &quoted_identity
        );

    check_condition(
        quoted_calculated,
        "calculate zip identity with spaces and apostrophes"
    );

    check_condition(
        quoted_calculated &&
        strcmp(quoted_identity.type, "crc32") == 0,
        "shell-sensitive zip uses member crc32"
    );

    unlink(member_path);
    unlink(first_zip_path);
    unlink(second_zip_path);
    unlink(changed_zip_path);
    unlink(quoted_member_path);
    unlink(quoted_zip_path);
    rmdir(temporary_directory);
}
