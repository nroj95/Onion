#include "playActivityHistory.h"
#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* =============================================================================
 * purpose:
 * verify retroarch history core lookup behavior.
 *
 * key behavior:
 * - matches a launched rom by its normalized path.
 * - returns the exact core path recorded by retroarch.
 * - rejects missing rom entries and malformed history files.
 * =============================================================================
 */

void test_history_core_lookup(void)
{
    char temporary_directory[] =
        "/tmp/playactivity-history-test-XXXXXX";

    if (mkdtemp(temporary_directory) == NULL) {
        check_condition(
            false,
            "create history temporary directory"
        );
        return;
    }

    char history_path[512];
    char rom_path[512];
    char alternate_rom_path[512];
    char normalized_lookup_path[512];
    char malformed_history_path[512];

    snprintf(
        history_path,
        sizeof(history_path),
        "%s/content_history.lpl",
        temporary_directory
    );

    snprintf(
        rom_path,
        sizeof(rom_path),
        "%s/game.rom",
        temporary_directory
    );

    snprintf(
        alternate_rom_path,
        sizeof(alternate_rom_path),
        "%s/other.rom",
        temporary_directory
    );

    snprintf(
        normalized_lookup_path,
        sizeof(normalized_lookup_path),
        "%s/folder/../game.rom",
        temporary_directory
    );

    snprintf(
        malformed_history_path,
        sizeof(malformed_history_path),
        "%s/malformed.lpl",
        temporary_directory
    );

    bool roms_written =
        write_fixture(rom_path, "rom contents") &&
        write_fixture(
            alternate_rom_path,
            "other contents"
        );

    check_condition(
        roms_written,
        "write history rom fixtures"
    );

    char history_contents[2048];

    snprintf(
        history_contents,
        sizeof(history_contents),
        "{\n"
        "  \"version\": \"1.5\",\n"
        "  \"items\": [\n"
        "    {\n"
        "      \"path\": \"%s\",\n"
        "      \"core_path\": "
        "\"/cores/first_libretro.so\"\n"
        "    },\n"
        "    {\n"
        "      \"path\": \"%s\",\n"
        "      \"core_path\": "
        "\"/cores/second_libretro.so\"\n"
        "    }\n"
        "  ]\n"
        "}\n",
        alternate_rom_path,
        rom_path
    );

    bool histories_written =
        write_fixture(
            history_path,
            history_contents
        ) &&
        write_fixture(
            malformed_history_path,
            "{not valid json"
        );

    check_condition(
        histories_written,
        "write history fixtures"
    );

    char core_path[512] = "";

    bool core_found =
        play_activity_history_find_core_path(
            history_path,
            normalized_lookup_path,
            core_path,
            sizeof(core_path)
        );

    check_condition(
        core_found,
        "find core for normalized rom path"
    );

    check_condition(
        strcmp(
            core_path,
            "/cores/second_libretro.so"
        ) == 0,
        "history returns recorded core path"
    );

    char missing_normalized_path[512];

    snprintf(
        missing_normalized_path,
        sizeof(missing_normalized_path),
        "%s/missing-folder/../game.rom",
        temporary_directory
    );

    core_path[0] = '\0';

    bool moved_path_core_found =
        play_activity_history_find_core_path(
            history_path,
            missing_normalized_path,
            core_path,
            sizeof(core_path)
        );

    check_condition(
        moved_path_core_found &&
        strcmp(
            core_path,
            "/cores/second_libretro.so"
        ) == 0,
        "history lookup does not require rom to exist"
    );

    char short_core_path[8] = "";

    bool truncated_core_found =
        play_activity_history_find_core_path(
            history_path,
            rom_path,
            short_core_path,
            sizeof(short_core_path)
        );

    check_condition(
        !truncated_core_found &&
        short_core_path[0] == '\0',
        "reject truncated history core path"
    );

    core_path[0] = 'x';
    core_path[1] = '\0';

    bool missing_found =
        play_activity_history_find_core_path(
            history_path,
            "/tmp/missing.rom",
            core_path,
            sizeof(core_path)
        );

    check_condition(
        !missing_found &&
        core_path[0] == '\0',
        "missing history entry clears output"
    );

    bool malformed_found =
        play_activity_history_find_core_path(
            malformed_history_path,
            rom_path,
            core_path,
            sizeof(core_path)
        );

    check_condition(
        !malformed_found,
        "reject malformed history"
    );

    unlink(history_path);
    unlink(malformed_history_path);
    unlink(rom_path);
    unlink(alternate_rom_path);
    rmdir(temporary_directory);
}
