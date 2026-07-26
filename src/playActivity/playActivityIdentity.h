#ifndef PLAY_ACTIVITY_IDENTITY_H
#define PLAY_ACTIVITY_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "utils/file.h"
#include "utils/str.h"

/* =============================================================================
 * purpose:
 * determine how launched rom content should be identified across path changes.
 *
 * key behavior:
 * - derives the onion system folder and selected retroarch core.
 * - distinguishes raw roms, zip archives, arcade sets, and unsupported content.
 * - calculates crc32 incrementally without loading an entire rom into memory.
 * - leaves archive extraction and database reconciliation to later layers.
 * =============================================================================
 */

#define ROM_IDENTITY_TYPE_MAX 32
#define ROM_IDENTITY_VALUE_MAX 65

typedef enum {
    ROM_IDENTITY_KIND_UNSUPPORTED = 0,
    ROM_IDENTITY_KIND_RAW,
    ROM_IDENTITY_KIND_ZIP,
    ROM_IDENTITY_KIND_M3U,
    ROM_IDENTITY_KIND_ARCADE
} RomIdentityKind;

typedef struct {
    RomIdentityKind kind;
    char system[STR_MAX];
    char core[STR_MAX];
    char extension[32];
} RomIdentityContext;

typedef struct {
    char type[ROM_IDENTITY_TYPE_MAX];
    char value[ROM_IDENTITY_VALUE_MAX];
    uint64_t content_size;
} RomContentIdentity;

bool rom_identity_context_build(
    const char *rom_path,
    RomIdentityContext *context
);

bool rom_identity_calculate_raw(
    const char *rom_path,
    RomContentIdentity *identity
);

bool rom_identity_calculate_zip(
    const char *rom_path,
    RomContentIdentity *identity
);

bool rom_identity_calculate_m3u(
    const char *rom_path,
    RomContentIdentity *identity
);

bool rom_identity_calculate_m3u_source_signature(
    const char *rom_path,
    char *signature_out,
    size_t signature_out_size
);

const char *rom_identity_kind_name(RomIdentityKind kind);

#endif
