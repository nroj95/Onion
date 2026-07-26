#include "./playActivityIdentity.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
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

static char *trim_playlist_entry(char *text);

static void fnv1a64_update_text(
    uint64_t *hash,
    const char *text
);

static void fnv1a64_update_uint64(
    uint64_t *hash,
    uint64_t value
);

static void normalize_path_separators(char *path);

static bool resolve_playlist_entry_path(
    const char *playlist_path,
    const char *entry,
    char *path_out,
    size_t path_out_size
);

static bool extract_cue_file_path(
    const char *line,
    char *path_out,
    size_t path_out_size
)
{
    if (line == NULL ||
        path_out == NULL ||
        path_out_size == 0) {
        return false;
    }

    while (isspace((unsigned char)*line))
        line++;

    if (strncasecmp(line, "FILE", 4) != 0 ||
        !isspace((unsigned char)line[4])) {
        return false;
    }

    line += 4;

    while (isspace((unsigned char)*line))
        line++;

    if (*line == '\0')
        return false;

    const char *path_start = line;
    const char *path_end = NULL;

    if (*path_start == '"') {
        path_start++;
        path_end = strchr(path_start, '"');
    }
    else {
        path_end = path_start;

        while (*path_end != '\0' &&
               !isspace((unsigned char)*path_end)) {
            path_end++;
        }
    }

    if (path_end == NULL || path_end == path_start)
        return false;

    size_t path_length = (size_t)(path_end - path_start);

    if (path_length >= path_out_size)
        return false;

    memcpy(path_out, path_start, path_length);
    path_out[path_length] = '\0';

    normalize_path_separators(path_out);

    return true;
}

static bool calculate_cue_identity_internal(
    const char *rom_path,
    RomContentIdentity *identity
)
{
    if (rom_path == NULL || identity == NULL)
        return false;

    FILE *cue_file = fopen(rom_path, "r");

    if (cue_file == NULL)
        return false;

    uint64_t hash = UINT64_C(14695981039346656037);
    uint64_t total_content_size = 0;
    size_t file_count = 0;
    char line[PATH_MAX * 2];

    while (fgets(line, sizeof(line), cue_file) != NULL) {
        if (strchr(line, '\n') == NULL && !feof(cue_file)) {
            fclose(cue_file);
            return false;
        }

        trim_line_end(line);

        char *cue_line = trim_playlist_entry(line);

        if (cue_line[0] == '\0')
            continue;

        char referenced_entry[PATH_MAX];

        if (!extract_cue_file_path(
                cue_line,
                referenced_entry,
                sizeof(referenced_entry))) {
            fnv1a64_update_text(&hash, cue_line);
            continue;
        }

        char referenced_path[PATH_MAX];

        if (!resolve_playlist_entry_path(
                rom_path,
                referenced_entry,
                referenced_path,
                sizeof(referenced_path))) {
            fclose(cue_file);
            return false;
        }

        RomContentIdentity referenced_identity;

        if (!rom_identity_calculate_raw(
                referenced_path,
                &referenced_identity)) {
            fprintf(
                stderr,
                "Warning: unable to fingerprint cue file entry: %s\n",
                referenced_path
            );

            fclose(cue_file);
            return false;
        }

        if (UINT64_MAX - total_content_size <
            referenced_identity.content_size) {
            fclose(cue_file);
            return false;
        }

        fnv1a64_update_text(&hash, "FILE");
        fnv1a64_update_text(
            &hash,
            referenced_identity.type
        );
        fnv1a64_update_text(
            &hash,
            referenced_identity.value
        );
        fnv1a64_update_uint64(
            &hash,
            referenced_identity.content_size
        );

        total_content_size += referenced_identity.content_size;
        file_count++;
    }

    bool read_successfully = !ferror(cue_file);
    fclose(cue_file);

    if (!read_successfully || file_count == 0)
        return false;

    memset(identity, 0, sizeof(*identity));

    snprintf(
        identity->type,
        sizeof(identity->type),
        "%s",
        "cue-fnv1a64"
    );

    snprintf(
        identity->value,
        sizeof(identity->value),
        "%016llx",
        (unsigned long long)hash
    );

    identity->content_size = total_content_size;

    return true;
}

bool rom_identity_calculate_cue(
    const char *rom_path,
    RomContentIdentity *identity
)
{
    return calculate_cue_identity_internal(
        rom_path,
        identity
    );
}

#define M3U_MAX_NESTING_DEPTH 8

static void fnv1a64_update(
    uint64_t *hash,
    const void *data,
    size_t data_size
)
{
    const unsigned char *bytes = data;

    for (size_t index = 0; index < data_size; index++) {
        *hash ^= bytes[index];
        *hash *= UINT64_C(1099511628211);
    }
}

static void fnv1a64_update_text(
    uint64_t *hash,
    const char *text
)
{
    fnv1a64_update(hash, text, strlen(text));

    const unsigned char separator = 0;
    fnv1a64_update(hash, &separator, sizeof(separator));
}

static void fnv1a64_update_uint64(
    uint64_t *hash,
    uint64_t value
)
{
    unsigned char bytes[8];

    for (size_t index = 0; index < sizeof(bytes); index++) {
        bytes[index] = (unsigned char)(value & UINT64_C(0xff));
        value >>= 8U;
    }

    fnv1a64_update(hash, bytes, sizeof(bytes));
}

static char *trim_playlist_entry(char *text)
{
    while (isspace((unsigned char)*text))
        text++;

    size_t length = strlen(text);

    while (length > 0 &&
           isspace((unsigned char)text[length - 1])) {
        text[--length] = '\0';
    }

    return text;
}

static void normalize_path_separators(char *path)
{
    for (size_t index = 0; path[index] != '\0'; index++) {
        if (path[index] == '\\')
            path[index] = '/';
    }
}

static bool resolve_playlist_entry_path(
    const char *playlist_path,
    const char *entry,
    char *path_out,
    size_t path_out_size
)
{
    if (playlist_path == NULL ||
        entry == NULL ||
        path_out == NULL ||
        path_out_size == 0) {
        return false;
    }

    if (entry[0] == '/') {
        int written = snprintf(
            path_out,
            path_out_size,
            "%s",
            entry
        );

        if (written < 0 || (size_t)written >= path_out_size)
            return false;

        normalize_path_separators(path_out);
        return true;
    }

    char directory[PATH_MAX];

    int copied = snprintf(
        directory,
        sizeof(directory),
        "%s",
        playlist_path
    );

    if (copied < 0 || (size_t)copied >= sizeof(directory))
        return false;

    char *separator = strrchr(directory, '/');

    if (separator != NULL)
        separator[1] = '\0';
    else
        directory[0] = '\0';

    int written = snprintf(
        path_out,
        path_out_size,
        "%s%s",
        directory,
        entry
    );

    if (written < 0 || (size_t)written >= path_out_size)
        return false;

    normalize_path_separators(path_out);
    return true;
}

static bool calculate_m3u_identity_internal(
    const char *rom_path,
    RomContentIdentity *identity,
    unsigned int depth
);

static bool calculate_referenced_identity(
    const char *rom_path,
    RomContentIdentity *identity,
    unsigned int depth
)
{
    RomIdentityContext context;

    if (!rom_identity_context_build(rom_path, &context))
        return false;

    switch (context.kind) {
    case ROM_IDENTITY_KIND_RAW:
        return rom_identity_calculate_raw(rom_path, identity);

    case ROM_IDENTITY_KIND_ZIP:
        return rom_identity_calculate_zip(rom_path, identity);

    case ROM_IDENTITY_KIND_CUE:
        return rom_identity_calculate_cue(rom_path, identity);

    case ROM_IDENTITY_KIND_M3U:
        return calculate_m3u_identity_internal(
            rom_path,
            identity,
            depth + 1
        );

    case ROM_IDENTITY_KIND_ARCADE:
    case ROM_IDENTITY_KIND_UNSUPPORTED:
    default:
        return false;
    }
}

static bool calculate_m3u_identity_internal(
    const char *rom_path,
    RomContentIdentity *identity,
    unsigned int depth
)
{
    if (rom_path == NULL ||
        identity == NULL ||
        depth > M3U_MAX_NESTING_DEPTH) {
        return false;
    }

    FILE *playlist = fopen(rom_path, "r");

    if (playlist == NULL)
        return false;

    uint64_t hash = UINT64_C(14695981039346656037);
    uint64_t total_content_size = 0;
    size_t entry_count = 0;
    char line[PATH_MAX * 2];

    while (fgets(line, sizeof(line), playlist) != NULL) {
        if (strchr(line, '\n') == NULL && !feof(playlist)) {
            fclose(playlist);
            return false;
        }

        trim_line_end(line);

        char *entry = trim_playlist_entry(line);

        if (entry_count == 0 &&
            (unsigned char)entry[0] == 0xef &&
            (unsigned char)entry[1] == 0xbb &&
            (unsigned char)entry[2] == 0xbf) {
            entry += 3;
        }

        if (entry[0] == '\0' || entry[0] == '#')
            continue;

        char referenced_path[PATH_MAX];

        if (!resolve_playlist_entry_path(
                rom_path,
                entry,
                referenced_path,
                sizeof(referenced_path))) {
            fclose(playlist);
            return false;
        }

        RomContentIdentity referenced_identity;

        if (!calculate_referenced_identity(
                referenced_path,
                &referenced_identity,
                depth)) {
            fprintf(
                stderr,
                "Warning: unable to fingerprint playlist entry: %s\n",
                referenced_path
            );

            fclose(playlist);
            return false;
        }

        if (UINT64_MAX - total_content_size <
            referenced_identity.content_size) {
            fclose(playlist);
            return false;
        }

        fnv1a64_update_text(
            &hash,
            referenced_identity.type
        );

        fnv1a64_update_text(
            &hash,
            referenced_identity.value
        );

        fnv1a64_update_uint64(
            &hash,
            referenced_identity.content_size
        );

        total_content_size += referenced_identity.content_size;
        entry_count++;
    }

    bool read_successfully = !ferror(playlist);
    fclose(playlist);

    if (!read_successfully || entry_count == 0)
        return false;

    memset(identity, 0, sizeof(*identity));

    snprintf(
        identity->type,
        sizeof(identity->type),
        "%s",
        "m3u-fnv1a64"
    );

    snprintf(
        identity->value,
        sizeof(identity->value),
        "%016llx",
        (unsigned long long)hash
    );

    identity->content_size = total_content_size;

    return true;
}

static bool calculate_cue_source_signature(
    const char *rom_path,
    uint64_t *hash
)
{
    if (rom_path == NULL || hash == NULL)
        return false;

    struct stat cue_status;

    if (stat(rom_path, &cue_status) != 0)
        return false;

    fnv1a64_update_uint64(
        hash,
        (uint64_t)cue_status.st_size
    );

    fnv1a64_update_uint64(
        hash,
        (uint64_t)cue_status.st_mtime
    );

    FILE *cue_file = fopen(rom_path, "r");

    if (cue_file == NULL)
        return false;

    size_t file_count = 0;
    char line[PATH_MAX * 2];

    while (fgets(line, sizeof(line), cue_file) != NULL) {
        if (strchr(line, '\n') == NULL && !feof(cue_file)) {
            fclose(cue_file);
            return false;
        }

        trim_line_end(line);

        char *cue_line = trim_playlist_entry(line);

        if (cue_line[0] == '\0')
            continue;

        char referenced_entry[PATH_MAX];

        if (!extract_cue_file_path(
                cue_line,
                referenced_entry,
                sizeof(referenced_entry))) {
            fnv1a64_update_text(hash, cue_line);
            continue;
        }

        char referenced_path[PATH_MAX];

        if (!resolve_playlist_entry_path(
                rom_path,
                referenced_entry,
                referenced_path,
                sizeof(referenced_path))) {
            fclose(cue_file);
            return false;
        }

        struct stat referenced_status;

        if (stat(referenced_path, &referenced_status) != 0) {
            fclose(cue_file);
            return false;
        }

        fnv1a64_update_text(hash, "FILE");
        fnv1a64_update_text(hash, referenced_path);

        fnv1a64_update_uint64(
            hash,
            (uint64_t)referenced_status.st_size
        );

        fnv1a64_update_uint64(
            hash,
            (uint64_t)referenced_status.st_mtime
        );

        file_count++;
    }

    bool read_successfully = !ferror(cue_file);
    fclose(cue_file);

    return read_successfully && file_count > 0;
}

static bool calculate_m3u_source_signature_internal(
    const char *rom_path,
    uint64_t *hash,
    unsigned int depth
)
{
    if (rom_path == NULL ||
        hash == NULL ||
        depth > M3U_MAX_NESTING_DEPTH) {
        return false;
    }

    FILE *playlist = fopen(rom_path, "r");

    if (playlist == NULL)
        return false;

    size_t entry_count = 0;
    char line[PATH_MAX * 2];

    while (fgets(line, sizeof(line), playlist) != NULL) {
        if (strchr(line, '\n') == NULL && !feof(playlist)) {
            fclose(playlist);
            return false;
        }

        trim_line_end(line);

        char *entry = trim_playlist_entry(line);

        if (entry_count == 0 &&
            (unsigned char)entry[0] == 0xef &&
            (unsigned char)entry[1] == 0xbb &&
            (unsigned char)entry[2] == 0xbf) {
            entry += 3;
        }

        if (entry[0] == '\0' || entry[0] == '#')
            continue;

        char referenced_path[PATH_MAX];

        if (!resolve_playlist_entry_path(
                rom_path,
                entry,
                referenced_path,
                sizeof(referenced_path))) {
            fclose(playlist);
            return false;
        }

        RomIdentityContext context;

        if (!rom_identity_context_build(
                referenced_path,
                &context)) {
            fclose(playlist);
            return false;
        }

        fnv1a64_update_text(hash, referenced_path);

        if (context.kind == ROM_IDENTITY_KIND_M3U) {
            if (!calculate_m3u_source_signature_internal(
                    referenced_path,
                    hash,
                    depth + 1)) {
                fclose(playlist);
                return false;
            }
        }
        else if (context.kind == ROM_IDENTITY_KIND_CUE) {
            if (!calculate_cue_source_signature(
                    referenced_path,
                    hash)) {
                fclose(playlist);
                return false;
            }
        }
        else if (context.kind == ROM_IDENTITY_KIND_RAW ||
                 context.kind == ROM_IDENTITY_KIND_ZIP) {
            struct stat file_status;

            if (stat(referenced_path, &file_status) != 0) {
                fclose(playlist);
                return false;
            }

            fnv1a64_update_uint64(
                hash,
                (uint64_t)file_status.st_size
            );

            fnv1a64_update_uint64(
                hash,
                (uint64_t)file_status.st_mtime
            );
        }
        else {
            fclose(playlist);
            return false;
        }

        entry_count++;
    }

    bool read_successfully = !ferror(playlist);
    fclose(playlist);

    return read_successfully && entry_count > 0;
}

bool rom_identity_calculate_m3u_source_signature(
    const char *rom_path,
    char *signature_out,
    size_t signature_out_size
)
{
    if (rom_path == NULL ||
        signature_out == NULL ||
        signature_out_size < 17) {
        return false;
    }

    uint64_t hash = UINT64_C(14695981039346656037);

    if (!calculate_m3u_source_signature_internal(
            rom_path,
            &hash,
            0)) {
        return false;
    }

    int written = snprintf(
        signature_out,
        signature_out_size,
        "%016llx",
        (unsigned long long)hash
    );

    return written == 16;
}

bool rom_identity_calculate_m3u(
    const char *rom_path,
    RomContentIdentity *identity
)
{
    return calculate_m3u_identity_internal(
        rom_path,
        identity,
        0
    );
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
    else if (strcmp(context->extension, "cue") == 0) {
        context->kind = ROM_IDENTITY_KIND_CUE;
    }
    else if (strcmp(context->extension, "m3u") == 0 ||
             strcmp(context->extension, "m3u8") == 0) {
        context->kind = ROM_IDENTITY_KIND_M3U;
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

    case ROM_IDENTITY_KIND_CUE:
        return "cue";

    case ROM_IDENTITY_KIND_M3U:
        return "m3u";

    case ROM_IDENTITY_KIND_ARCADE:
        return "arcade";

    case ROM_IDENTITY_KIND_UNSUPPORTED:
    default:
        return "unsupported";
    }
}
