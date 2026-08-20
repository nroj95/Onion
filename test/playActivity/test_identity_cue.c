#include "playActivityIdentity.h"
#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* =============================================================================
 * purpose:
 * verify cue-based disc identity behavior.
 *
 * key behavior:
 * - confirms track filenames do not affect stable identity.
 * - confirms referenced track order remains significant.
 * - uses temporary host-side cue and track fixtures.
 * =============================================================================
 */

void test_cue_identity(void)
{
    char temporary_directory[] =
        "/tmp/playactivity-cue-test-XXXXXX";

    if (mkdtemp(temporary_directory) == NULL) {
        check_condition(false, "create cue temporary directory");
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
        "create cue fixture directories"
    );

    char original_first_track[2048];
    char original_second_track[2048];
    char original_cue[2048];
    char type_changed_cue[2048];

    char renamed_first_track[2048];
    char renamed_second_track[2048];
    char renamed_cue[2048];

    char reordered_first_track[2048];
    char reordered_second_track[2048];
    char reordered_cue[2048];

    snprintf(
        original_first_track,
        sizeof(original_first_track),
        "%s/disc track 1.bin",
        original_directory
    );

    snprintf(
        original_second_track,
        sizeof(original_second_track),
        "%s/disc track 2.bin",
        original_directory
    );

    snprintf(
        original_cue,
        sizeof(original_cue),
        "%s/game.cue",
        original_directory
    );

    snprintf(
        type_changed_cue,
        sizeof(type_changed_cue),
        "%s/type changed.cue",
        original_directory
    );

    snprintf(
        renamed_first_track,
        sizeof(renamed_first_track),
        "%s/renamed first.bin",
        renamed_directory
    );

    snprintf(
        renamed_second_track,
        sizeof(renamed_second_track),
        "%s/renamed second.bin",
        renamed_directory
    );

    snprintf(
        renamed_cue,
        sizeof(renamed_cue),
        "%s/renamed game.cue",
        renamed_directory
    );

    snprintf(
        reordered_first_track,
        sizeof(reordered_first_track),
        "%s/first.bin",
        reordered_directory
    );

    snprintf(
        reordered_second_track,
        sizeof(reordered_second_track),
        "%s/second.bin",
        reordered_directory
    );

    snprintf(
        reordered_cue,
        sizeof(reordered_cue),
        "%s/reordered game.cue",
        reordered_directory
    );

    bool tracks_written =
        write_fixture(original_first_track, "first track contents") &&
        write_fixture(original_second_track, "second track contents") &&
        write_fixture(renamed_first_track, "first track contents") &&
        write_fixture(renamed_second_track, "second track contents") &&
        write_fixture(reordered_first_track, "first track contents") &&
        write_fixture(reordered_second_track, "second track contents");

    check_condition(
        tracks_written,
        "write cue track fixtures"
    );

    const char *original_cue_contents =
        "FILE \"disc track 1.bin\" BINARY\n"
        "  TRACK 01 MODE1/2352\n"
        "    INDEX 01 00:00:00\n"
        "FILE \"disc track 2.bin\" BINARY\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 00:00:00\n";

    const char *type_changed_cue_contents =
        "FILE \"disc track 1.bin\" WAVE\n"
        "  TRACK 01 MODE1/2352\n"
        "    INDEX 01 00:00:00\n"
        "FILE \"disc track 2.bin\" BINARY\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 00:00:00\n";

    const char *renamed_cue_contents =
        "FILE \"renamed first.bin\" BINARY\n"
        "  TRACK 01 MODE1/2352\n"
        "    INDEX 01 00:00:00\n"
        "FILE \"renamed second.bin\" BINARY\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 00:00:00\n";

    const char *reordered_cue_contents =
        "FILE \"second.bin\" BINARY\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 00:00:00\n"
        "FILE \"first.bin\" BINARY\n"
        "  TRACK 01 MODE1/2352\n"
        "    INDEX 01 00:00:00\n";

    bool cues_written =
        write_fixture(original_cue, original_cue_contents) &&
        write_fixture(type_changed_cue, type_changed_cue_contents) &&
        write_fixture(renamed_cue, renamed_cue_contents) &&
        write_fixture(reordered_cue, reordered_cue_contents);

    check_condition(
        cues_written,
        "write cue sheet fixtures"
    );

    RomContentIdentity original_identity;
    RomContentIdentity type_changed_identity;
    RomContentIdentity renamed_identity;
    RomContentIdentity reordered_identity;

    bool original_calculated =
        rom_identity_calculate_cue(
            original_cue,
            &original_identity
        );

    bool type_changed_calculated =
        rom_identity_calculate_cue(
            type_changed_cue,
            &type_changed_identity
        );

    bool renamed_calculated =
        rom_identity_calculate_cue(
            renamed_cue,
            &renamed_identity
        );

    bool reordered_calculated =
        rom_identity_calculate_cue(
            reordered_cue,
            &reordered_identity
        );

    check_condition(
        original_calculated &&
        type_changed_calculated &&
        renamed_calculated &&
        reordered_calculated,
        "calculate cue identities"
    );

    check_condition(
        strcmp(original_identity.type, "cue-fnv1a64") == 0,
        "cue identity uses cue-fnv1a64"
    );

    check_condition(
        strcmp(
            original_identity.value,
            renamed_identity.value
        ) == 0 &&
        original_identity.content_size ==
            renamed_identity.content_size,
        "renamed cue tracks preserve identity"
    );

    check_condition(
        strcmp(
            original_identity.value,
            reordered_identity.value
        ) != 0,
        "reordered cue tracks change identity"
    );

    check_condition(
        strcmp(
            original_identity.value,
            type_changed_identity.value
        ) != 0,
        "changed cue file type changes identity"
    );

    char original_source_signature[17];
    char type_changed_source_signature[17];

    bool source_signatures_calculated =
        rom_identity_calculate_cue_source_signature(
            original_cue,
            original_source_signature,
            sizeof(original_source_signature)
        ) &&
        rom_identity_calculate_cue_source_signature(
            type_changed_cue,
            type_changed_source_signature,
            sizeof(type_changed_source_signature)
        );

    check_condition(
        source_signatures_calculated,
        "calculate cue source signatures"
    );

    check_condition(
        source_signatures_calculated &&
        strcmp(
            original_source_signature,
            type_changed_source_signature
        ) != 0,
        "changed cue file type invalidates source signature"
    );

    unlink(original_first_track);
    unlink(original_second_track);
    unlink(original_cue);
    unlink(type_changed_cue);

    unlink(renamed_first_track);
    unlink(renamed_second_track);
    unlink(renamed_cue);

    unlink(reordered_first_track);
    unlink(reordered_second_track);
    unlink(reordered_cue);

    rmdir(original_directory);
    rmdir(renamed_directory);
    rmdir(reordered_directory);
    rmdir(temporary_directory);
}
