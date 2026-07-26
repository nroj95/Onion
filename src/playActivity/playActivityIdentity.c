#include "./playActivityIdentity.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define CMD_TO_RUN_PATH "/mnt/SDCARD/.tmp_update/cmd_to_run.sh"
#define ROMS_PATH_PREFIX "/mnt/SDCARD/Roms/"
#define SEVEN_ZIP_PATH "/mnt/SDCARD/.tmp_update/bin/7z"

/* =============================================================================
 * purpose:
 * derive identity-relevant launch context without modifying the activity db.
 *
 * key behavior:
 * - treats the resolved rom path as the source of the onion system folder.
 * - extracts cores ending in "_libretro.so" from launch commands or scripts.
 * - uses explicit arcade-core recognition instead of assuming every zip is
 *   safe to inspect as a single cartridge rom.
 * - leaves unsupported formats unchanged until a deliberate resolver exists.
 * =============================================================================
 */

static void copy_lowercase(
    char *destination,
    size_t destination_size,
    const char *source
)
{
    if (destination_size == 0)
        return;

    size_t index = 0;
    while (source[index] != '\0' && index + 1 < destination_size) {
        destination[index] = (char)tolower((unsigned char)source[index]);
        index++;
    }

    destination[index] = '\0';
}

static bool extract_system_folder(
    const char *rom_path,
    char *system_out,
    size_t system_out_size
)
{
    const char *system_start = strstr(rom_path, ROMS_PATH_PREFIX);

    if (system_start != NULL) {
        system_start += strlen(ROMS_PATH_PREFIX);
    }
    else {
        system_start = strstr(rom_path, "../../Roms/");
        if (system_start == NULL)
            return false;

        system_start += strlen("../../Roms/");
    }

    const char *system_end = strchr(system_start, '/');
    if (system_end == NULL || system_end == system_start)
        return false;

    size_t system_length = (size_t)(system_end - system_start);
    if (system_length >= system_out_size)
        system_length = system_out_size - 1;

    memcpy(system_out, system_start, system_length);
    system_out[system_length] = '\0';

    return true;
}

static bool extract_core_from_text(
    const char *text,
    char *core_out,
    size_t core_out_size
)
{
    const char *suffix = "_libretro.so";
    const char *suffix_position = strstr(text, suffix);

    if (suffix_position == NULL)
        return false;

    const char *core_start = suffix_position;
    while (core_start > text) {
        char previous = core_start[-1];

        if (previous == '/' ||
            previous == '\\' ||
            previous == '"' ||
            previous == '\'' ||
            isspace((unsigned char)previous)) {
            break;
        }

        core_start--;
    }

    size_t core_length = (size_t)(suffix_position - core_start);
    if (core_length == 0)
        return false;

    if (core_length >= core_out_size)
        core_length = core_out_size - 1;

    memcpy(core_out, core_start, core_length);
    core_out[core_length] = '\0';

    return true;
}

static bool extract_first_quoted_path(
    const char *text,
    char *path_out,
    size_t path_out_size
)
{
    const char *path_start = strchr(text, '"');
    if (path_start == NULL)
        return false;

    path_start++;

    const char *path_end = strchr(path_start, '"');
    if (path_end == NULL || path_end == path_start)
        return false;

    size_t path_length = (size_t)(path_end - path_start);
    if (path_length >= path_out_size)
        path_length = path_out_size - 1;

    memcpy(path_out, path_start, path_length);
    path_out[path_length] = '\0';

    return true;
}

static bool get_selected_core(
    char *core_out,
    size_t core_out_size
)
{
    char *command = file_read(CMD_TO_RUN_PATH);
    if (command == NULL)
        return false;

    bool core_found = extract_core_from_text(
        command,
        core_out,
        core_out_size
    );

    if (!core_found) {
        char launch_script[PATH_MAX];

        if (extract_first_quoted_path(
                command,
                launch_script,
                sizeof(launch_script))) {
            char *launch_contents = file_read(launch_script);

            if (launch_contents != NULL) {
                core_found = extract_core_from_text(
                    launch_contents,
                    core_out,
                    core_out_size
                );

                free(launch_contents);
            }
        }
    }

    free(command);
    return core_found;
}

static bool is_path_only_system(const char *system)
{
    char lowercase_system[STR_MAX];

    copy_lowercase(
        lowercase_system,
        sizeof(lowercase_system),
        system
    );

    return strcmp(lowercase_system, "ports") == 0 ||
           strcmp(lowercase_system, "scummvm") == 0;
}

static bool is_path_only_rom(
    const char *system,
    const char *rom_path
)
{
    if (system == NULL || rom_path == NULL)
        return false;

    if (strcasecmp(system, "PICO") != 0)
        return false;

    const char *basename = strrchr(rom_path, '/');
    basename = basename == NULL ? rom_path : basename + 1;

    return strcmp(
        basename,
        "~Run PICO-8 with Splore.png"
    ) == 0;
}

static bool is_arcade_system(const char *system)
{
    return system != NULL &&
           strcasecmp(system, "ADVMAME") == 0;
}

static bool is_arcade_core(const char *core)
{
    static const char *arcade_cores[] = {
        "fbalpha",
        "fbalpha2012",
        "fbalpha2012_cps1",
        "fbalpha2012_cps2",
        "fbalpha2012_cps3",
        "fbalpha2012_neogeo",
        "fbneo",
        "km_mame2003_xtreme",
        "mame2000",
        "mame2003",
        "mame2003_plus",
        "mba_mini"
    };

    size_t core_count =
        sizeof(arcade_cores) / sizeof(arcade_cores[0]);

    for (size_t index = 0; index < core_count; index++) {
        if (strcmp(core, arcade_cores[index]) == 0)
            return true;
    }

    return false;
}

static void initialize_crc32_table(uint32_t table[256])
{
    for (uint32_t value = 0; value < 256; value++) {
        uint32_t checksum = value;

        for (int bit = 0; bit < 8; bit++) {
            if ((checksum & 1U) != 0) {
                checksum =
                    (checksum >> 1U) ^ UINT32_C(0xedb88320);
            }
            else {
                checksum >>= 1U;
            }
        }

        table[value] = checksum;
    }
}

static bool calculate_stream_crc32(
    FILE *stream,
    uint32_t *checksum_out,
    uint64_t *size_out
)
{
    static uint32_t crc32_table[256];
    static bool crc32_table_ready = false;

    if (stream == NULL ||
        checksum_out == NULL ||
        size_out == NULL) {
        return false;
    }

    if (!crc32_table_ready) {
        initialize_crc32_table(crc32_table);
        crc32_table_ready = true;
    }

    unsigned char buffer[64 * 1024];
    uint32_t checksum = UINT32_MAX;
    uint64_t total_size = 0;

    while (true) {
        size_t bytes_read = fread(
            buffer,
            1,
            sizeof(buffer),
            stream
        );

        for (size_t index = 0; index < bytes_read; index++) {
            uint32_t table_index =
                (checksum ^ buffer[index]) & UINT32_C(0xff);

            checksum =
                crc32_table[table_index] ^ (checksum >> 8U);
        }

        total_size += bytes_read;

        if (bytes_read < sizeof(buffer)) {
            if (ferror(stream))
                return false;

            break;
        }
    }

    *checksum_out = checksum ^ UINT32_MAX;
    *size_out = total_size;

    return true;
}

static bool calculate_file_crc32(
    const char *file_path,
    uint32_t *checksum_out,
    uint64_t *size_out
)
{
    FILE *rom_file = fopen(file_path, "rb");
    if (rom_file == NULL)
        return false;

    bool calculated = calculate_stream_crc32(
        rom_file,
        checksum_out,
        size_out
    );

    fclose(rom_file);

    return calculated;
}

static bool append_shell_quoted(
    char *destination,
    size_t destination_size,
    const char *value
)
{
    size_t used = strlen(destination);

    if (used + 2 >= destination_size)
        return false;

    destination[used++] = '\'';
    destination[used] = '\0';

    for (size_t index = 0; value[index] != '\0'; index++) {
        const char *escaped =
            value[index] == '\''
                ? "'\\''"
                : NULL;

        if (escaped != NULL) {
            size_t escaped_length = strlen(escaped);

            if (used + escaped_length >= destination_size)
                return false;

            memcpy(
                destination + used,
                escaped,
                escaped_length
            );

            used += escaped_length;
        }
        else {
            if (used + 1 >= destination_size)
                return false;

            destination[used++] = value[index];
        }

        destination[used] = '\0';
    }

    if (used + 2 > destination_size)
        return false;

    destination[used++] = '\'';
    destination[used] = '\0';

    return true;
}

static void trim_line_end(char *text)
{
    size_t length = strlen(text);

    while (length > 0 &&
           (text[length - 1] == '\n' ||
            text[length - 1] == '\r')) {
        text[--length] = '\0';
    }
}

static bool get_single_zip_member(
    const char *archive_path,
    char *member_out,
    size_t member_out_size
)
{
    char command[PATH_MAX * 2] = "";

    snprintf(
        command,
        sizeof(command),
        "%s l -slt ",
        SEVEN_ZIP_PATH
    );

    if (!append_shell_quoted(
            command,
            sizeof(command),
            archive_path)) {
        return false;
    }

    strcat(command, " 2>/dev/null");

    FILE *listing = popen(command, "r");
    if (listing == NULL)
        return false;

    char line[PATH_MAX + 64];
    char current_path[PATH_MAX] = "";
    bool current_is_file = false;
    bool reading_members = false;
    int file_count = 0;

    while (fgets(line, sizeof(line), listing) != NULL) {
        trim_line_end(line);

        if (strcmp(line, "----------") == 0) {
            reading_members = true;
            continue;
        }

        if (!reading_members)
            continue;

        if (line[0] == '\0') {
            if (current_is_file && current_path[0] != '\0') {
                file_count++;

                if (file_count == 1) {
                    snprintf(
                        member_out,
                        member_out_size,
                        "%s",
                        current_path
                    );
                }
            }

            current_path[0] = '\0';
            current_is_file = false;
            continue;
        }

        if (strncmp(line, "Path = ", 7) == 0) {
            snprintf(
                current_path,
                sizeof(current_path),
                "%s",
                line + 7
            );
        }
        else if (strcmp(line, "Folder = -") == 0) {
            current_is_file = true;
        }
    }

    if (current_is_file && current_path[0] != '\0') {
        file_count++;

        if (file_count == 1) {
            snprintf(
                member_out,
                member_out_size,
                "%s",
                current_path
            );
        }
    }

    int status = pclose(listing);

    return status != -1 &&
           WIFEXITED(status) &&
           WEXITSTATUS(status) == 0 &&
           file_count == 1;
}

static bool calculate_zip_member_crc32(
    const char *archive_path,
    const char *member_path,
    uint32_t *checksum_out,
    uint64_t *size_out
)
{
    char command[PATH_MAX * 3] = "";

    snprintf(
        command,
        sizeof(command),
        "%s x -so ",
        SEVEN_ZIP_PATH
    );

    if (!append_shell_quoted(
            command,
            sizeof(command),
            archive_path)) {
        return false;
    }

    strcat(command, " ");

    if (!append_shell_quoted(
            command,
            sizeof(command),
            member_path)) {
        return false;
    }

    strcat(command, " 2>/dev/null");

    FILE *stream = popen(command, "r");
    if (stream == NULL)
        return false;

    bool calculated = calculate_stream_crc32(
        stream,
        checksum_out,
        size_out
    );

    int status = pclose(stream);

    return calculated &&
           status != -1 &&
           WIFEXITED(status) &&
           WEXITSTATUS(status) == 0;
}

bool rom_identity_calculate_raw(
    const char *rom_path,
    RomContentIdentity *identity
)
{
    if (rom_path == NULL || identity == NULL)
        return false;

    memset(identity, 0, sizeof(*identity));

    uint32_t checksum = 0;
    uint64_t content_size = 0;

    if (!calculate_file_crc32(
            rom_path,
            &checksum,
            &content_size)) {
        return false;
    }

    snprintf(
        identity->type,
        sizeof(identity->type),
        "%s",
        "crc32"
    );

    snprintf(
        identity->value,
        sizeof(identity->value),
        "%08x",
        checksum
    );

    identity->content_size = content_size;

    return true;
}

bool rom_identity_calculate_zip(
    const char *rom_path,
    RomContentIdentity *identity
)
{
    if (rom_path == NULL || identity == NULL)
        return false;

    memset(identity, 0, sizeof(*identity));

    char member_path[PATH_MAX];

    if (!get_single_zip_member(
            rom_path,
            member_path,
            sizeof(member_path))) {
        return false;
    }

    uint32_t checksum = 0;
    uint64_t content_size = 0;

    if (!calculate_zip_member_crc32(
            rom_path,
            member_path,
            &checksum,
            &content_size)) {
        return false;
    }

    snprintf(
        identity->type,
        sizeof(identity->type),
        "%s",
        "crc32"
    );

    snprintf(
        identity->value,
        sizeof(identity->value),
        "%08x",
        checksum
    );

    identity->content_size = content_size;

    return true;
}

bool rom_identity_context_build(
    const char *rom_path,
    RomIdentityContext *context
)
{
    if (rom_path == NULL || context == NULL)
        return false;

    memset(context, 0, sizeof(*context));
    context->kind = ROM_IDENTITY_KIND_UNSUPPORTED;

    extract_system_folder(
        rom_path,
        context->system,
        sizeof(context->system)
    );

    copy_lowercase(
        context->extension,
        sizeof(context->extension),
        file_getExtension(rom_path)
    );

    get_selected_core(
        context->core,
        sizeof(context->core)
    );

    if (is_arcade_core(context->core) ||
        is_arcade_system(context->system)) {
        context->kind = ROM_IDENTITY_KIND_ARCADE;
    }
    else if (is_path_only_system(context->system) ||
             is_path_only_rom(context->system, rom_path)) {
        context->kind = ROM_IDENTITY_KIND_UNSUPPORTED;
    }
    else if (strcmp(context->extension, "zip") == 0) {
        context->kind = ROM_IDENTITY_KIND_ZIP;
    }
    else if (context->extension[0] != '\0') {
        context->kind = ROM_IDENTITY_KIND_RAW;
    }

    return true;
}

const char *rom_identity_kind_name(RomIdentityKind kind)
{
    switch (kind) {
    case ROM_IDENTITY_KIND_RAW:
        return "raw";

    case ROM_IDENTITY_KIND_ZIP:
        return "zip";

    case ROM_IDENTITY_KIND_ARCADE:
        return "arcade";

    case ROM_IDENTITY_KIND_UNSUPPORTED:
    default:
        return "unsupported";
    }
}
