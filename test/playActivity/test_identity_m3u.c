#include "playActivityIdentity.h"
#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* =============================================================================
 * purpose:
 * verify m3u playlist identity behavior.
 *
 * key behavior:
 * - confirms playlist and entry names do not affect stable identity.
 * - confirms playlist entry order remains significant.
 * - uses temporary host-side playlist fixtures.
 * =============================================================================
 */

void test_m3u_identity(void)
{
    char temporary_directory[] =
        "/tmp/playactivity-m3u-test-XXXXXX";

    if (mkdtemp(temporary_directory) == NULL) {
        check_condition(false, "create m3u temporary directory");
        return;
    }

    char original_directory[1024];
    char renamed_directory[1024];
    char reordered_directory[1024];

    snprintf(
        original_directory,
        sizeof(original_directory),
        "%s/original",
        temporary_directory
    );

    snprintf(
        renamed_directory,
        sizeof(renamed_directory),
        "%s/renamed",
        temporary_directory
    );

    snprintf(
        reordered_directory,
        sizeof(reordered_directory),
        "%s/reordered",
        temporary_directory
    );

    bool directories_created =
        mkdir(original_directory, 0700) == 0 &&
        mkdir(renamed_directory, 0700) == 0 &&
        mkdir(reordered_directory, 0700) == 0;

    check_condition(
        directories_created,
        "create m3u fixture directories"
    );

    char original_first_file[2048];
    char original_second_file[2048];
    char original_playlist[2048];
    char formatted_playlist[2048];

    char renamed_first_file[2048];
    char renamed_second_file[2048];
    char renamed_playlist[2048];

    char reordered_first_file[2048];
    char reordered_second_file[2048];
    char reordered_playlist[2048];

    snprintf(
        original_first_file,
        sizeof(original_first_file),
        "%s/disc one.bin",
        original_directory
    );

    snprintf(
        original_second_file,
        sizeof(original_second_file),
        "%s/disc two.bin",
        original_directory
    );

    snprintf(
        original_playlist,
        sizeof(original_playlist),
        "%s/game.m3u",
        original_directory
    );

    snprintf(
        formatted_playlist,
        sizeof(formatted_playlist),
        "%s/formatted game.m3u8",
        original_directory
    );

    snprintf(
        renamed_first_file,
        sizeof(renamed_first_file),
        "%s/renamed first.bin",
        renamed_directory
    );

    snprintf(
        renamed_second_file,
        sizeof(renamed_second_file),
        "%s/renamed second.bin",
        renamed_directory
    );

    snprintf(
        renamed_playlist,
        sizeof(renamed_playlist),
        "%s/renamed game.m3u",
        renamed_directory
    );

    snprintf(
        reordered_first_file,
        sizeof(reordered_first_file),
        "%s/first.bin",
        reordered_directory
    );

    snprintf(
        reordered_second_file,
        sizeof(reordered_second_file),
        "%s/second.bin",
        reordered_directory
    );

    snprintf(
        reordered_playlist,
        sizeof(reordered_playlist),
        "%s/reordered game.m3u",
        reordered_directory
    );

    bool referenced_files_written =
        write_fixture(original_first_file, "first disc contents") &&
        write_fixture(original_second_file, "second disc contents") &&
        write_fixture(renamed_first_file, "first disc contents") &&
        write_fixture(renamed_second_file, "second disc contents") &&
        write_fixture(reordered_first_file, "first disc contents") &&
        write_fixture(reordered_second_file, "second disc contents");

    check_condition(
        referenced_files_written,
        "write m3u referenced files"
    );

    const char *original_playlist_contents =
        "disc one.bin\n"
        "disc two.bin\n";

    const char *formatted_playlist_contents =
        "\xef\xbb\xbf#EXTM3U\n"
        "\n"
        "  disc one.bin  \n"
        "# between discs\n"
        "disc two.bin\n";

    const char *renamed_playlist_contents =
        "renamed first.bin\n"
        "renamed second.bin\n";

    const char *reordered_playlist_contents =
        "second.bin\n"
        "first.bin\n";

    bool playlists_written =
        write_fixture(
            original_playlist,
            original_playlist_contents
        ) &&
        write_fixture(
            formatted_playlist,
            formatted_playlist_contents
        ) &&
        write_fixture(
            renamed_playlist,
            renamed_playlist_contents
        ) &&
        write_fixture(
            reordered_playlist,
            reordered_playlist_contents
        );

    check_condition(
        playlists_written,
        "write m3u playlist fixtures"
    );

    RomContentIdentity original_identity;
    RomContentIdentity formatted_identity;
    RomContentIdentity renamed_identity;
    RomContentIdentity reordered_identity;

    bool original_calculated =
        rom_identity_calculate_m3u(
            original_playlist,
            &original_identity
        );

    bool formatted_calculated =
        rom_identity_calculate_m3u(
            formatted_playlist,
            &formatted_identity
        );

    bool renamed_calculated =
        rom_identity_calculate_m3u(
            renamed_playlist,
            &renamed_identity
        );

    bool reordered_calculated =
        rom_identity_calculate_m3u(
            reordered_playlist,
            &reordered_identity
        );

    check_condition(
        original_calculated &&
        formatted_calculated &&
        renamed_calculated &&
        reordered_calculated,
        "calculate m3u identities"
    );

    check_condition(
        strcmp(original_identity.type, "m3u-fnv1a64") == 0,
        "m3u identity uses m3u-fnv1a64"
    );

    check_condition(
        strcmp(
            original_identity.value,
            renamed_identity.value
        ) == 0 &&
        original_identity.content_size ==
            renamed_identity.content_size,
        "renamed m3u entries preserve identity"
    );

    check_condition(
        strcmp(
            original_identity.value,
            formatted_identity.value
        ) == 0 &&
        original_identity.content_size ==
            formatted_identity.content_size,
        "m3u formatting preserves identity"
    );

    check_condition(
        strcmp(
            original_identity.value,
            reordered_identity.value
        ) != 0,
        "reordered m3u entries change identity"
    );

    unlink(original_first_file);
    unlink(original_second_file);
    unlink(original_playlist);
    unlink(formatted_playlist);

    unlink(renamed_first_file);
    unlink(renamed_second_file);
    unlink(renamed_playlist);

    unlink(reordered_first_file);
    unlink(reordered_second_file);
    unlink(reordered_playlist);

    rmdir(original_directory);
    rmdir(renamed_directory);
    rmdir(reordered_directory);
    rmdir(temporary_directory);
}
