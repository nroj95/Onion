#include "playActivityIdentity.h"
#include "test_support.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* =============================================================================
 * purpose:
 * verify raw rom content identity behavior.
 *
 * key behavior:
 * - confirms identical bytes produce the same crc32 identity.
 * - confirms changed bytes produce a different identity.
 * - uses temporary host-side fixture files.
 * =============================================================================
 */

void test_raw_identity(void)
{
    char temporary_directory[] =
        "/tmp/playactivity-identity-test-XXXXXX";

    if (mkdtemp(temporary_directory) == NULL) {
        check_condition(false, "create temporary directory");
        return;
    }

    char first_path[512];
    char second_path[512];

    snprintf(
        first_path,
        sizeof(first_path),
        "%s/first.rom",
        temporary_directory
    );

    snprintf(
        second_path,
        sizeof(second_path),
        "%s/second.rom",
        temporary_directory
    );

    bool fixtures_written =
        write_fixture(first_path, "same rom contents") &&
        write_fixture(second_path, "same rom contents");

    check_condition(fixtures_written, "write raw fixtures");

    RomContentIdentity first_identity;
    RomContentIdentity second_identity;

    bool first_calculated =
        rom_identity_calculate_raw(first_path, &first_identity);

    bool second_calculated =
        rom_identity_calculate_raw(second_path, &second_identity);

    check_condition(
        first_calculated && second_calculated,
        "calculate raw identities"
    );

    check_condition(
        strcmp(first_identity.type, "crc32") == 0,
        "raw identity uses crc32"
    );

    check_condition(
        strcmp(first_identity.value, second_identity.value) == 0 &&
        first_identity.content_size == second_identity.content_size,
        "same bytes produce same identity"
    );

    bool changed_fixture_written =
        write_fixture(second_path, "changed rom contents");

    check_condition(
        changed_fixture_written,
        "rewrite changed raw fixture"
    );

    RomContentIdentity changed_identity;

    bool changed_calculated =
        rom_identity_calculate_raw(second_path, &changed_identity);

    check_condition(
        changed_calculated,
        "calculate changed raw identity"
    );

    check_condition(
        strcmp(first_identity.value, changed_identity.value) != 0,
        "changed bytes produce different identity"
    );

    RomIdentityContext splore_context;
    RomIdentityContext pico_cart_context;
    RomIdentityContext advmame_zip_context;
    RomIdentityContext advmame_7z_context;
    RomIdentityContext advmame_chd_context;

    bool contexts_built =
        rom_identity_context_build(
            "/mnt/SDCARD/Roms/PICO/~Run PICO-8 with Splore.png",
            &splore_context
        ) &&
        rom_identity_context_build(
            "/mnt/SDCARD/Roms/PICO/normal game.p8.png",
            &pico_cart_context
        ) &&
        rom_identity_context_build(
            "/mnt/SDCARD/Roms/ADVMAME/game.zip",
            &advmame_zip_context
        ) &&
        rom_identity_context_build(
            "/mnt/SDCARD/Roms/ADVMAME/game.7z",
            &advmame_7z_context
        ) &&
        rom_identity_context_build(
            "/mnt/SDCARD/Roms/ADVMAME/game.chd",
            &advmame_chd_context
        );

    check_condition(
        contexts_built,
        "build pico-8 identity contexts"
    );

    check_condition(
        contexts_built &&
        splore_context.kind == ROM_IDENTITY_KIND_UNSUPPORTED,
        "keep pico-8 splore launcher path-based"
    );

    check_condition(
        contexts_built &&
        pico_cart_context.kind == ROM_IDENTITY_KIND_RAW,
        "keep pico-8 cartridges content-based"
    );

    check_condition(
        contexts_built &&
        advmame_zip_context.kind == ROM_IDENTITY_KIND_ARCADE,
        "keep advancemame zip roms path-based"
    );

    check_condition(
        contexts_built &&
        advmame_7z_context.kind == ROM_IDENTITY_KIND_ARCADE,
        "keep advancemame 7z roms path-based"
    );

    check_condition(
        contexts_built &&
        advmame_chd_context.kind == ROM_IDENTITY_KIND_ARCADE,
        "keep advancemame chd roms path-based"
    );

    unlink(first_path);
    unlink(second_path);
    rmdir(temporary_directory);
}
