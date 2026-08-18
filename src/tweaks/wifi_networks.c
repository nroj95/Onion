#include "./wifi_networks.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Onion-owned active, backup, and temporary network configurations. */
#define WPA_CLI_PATH "/customer/app/wpa_cli"
#define ACTIVE_CONFIG_PATH "/appconfigs/wpa_supplicant.conf"
#define BACKUP_CONFIG_PATH "/mnt/SDCARD/.tmp_update/config/wpa_supplicant.conf.backup"
#define SAVED_TEMP_PATH "/mnt/SDCARD/.tmp_update/config/wifi_saved_networks.conf.new"
#define BACKUP_TEMP_PATH "/mnt/SDCARD/.tmp_update/config/wpa_supplicant.conf.backup.new"
#define ACTIVE_TEMP_PATH "/appconfigs/wpa_supplicant.conf.new"

#define MAX_NETWORK_BLOCK 8192
#define LINE_BUFFER_SIZE 1024

static bool reconfigure_wifi(void)
{
    int status = system(
        WPA_CLI_PATH
        " -i wlan0 reconfigure >/dev/null 2>&1");

    if (status != 0) {
        fprintf(
            stderr,
            "Wi-Fi reconfigure failed: status=%d\n",
            status);

        return false;
    }

    fprintf(
        stderr,
        "Wi-Fi runtime configuration reloaded\n");

    return true;
}

static bool wifi_connection_completed(void)
{
    FILE *status_pipe = popen(
        WPA_CLI_PATH
        " -i wlan0 status 2>/dev/null",
        "r");

    if (status_pipe == NULL) {
        return false;
    }

    bool completed = false;
    char line[STR_MAX];

    while (
        fgets(
            line,
            sizeof(line),
            status_pipe) != NULL) {
        line[strcspn(
            line,
            "\r\n")] = '\0';

        if (
            strcmp(
                line,
                "wpa_state=COMPLETED") == 0) {
            completed = true;
            break;
        }
    }

    if (pclose(status_pipe) != 0) {
        return false;
    }

    return completed;
}

static bool restart_wifi_dhcp(void)
{
    const int connection_attempts = 75;
    const useconds_t retry_delay_us = 200000;

    bool connection_completed = false;

    for (
        int attempt = 0;
        attempt < connection_attempts;
        attempt++) {
        if (wifi_connection_completed()) {
            connection_completed = true;
            break;
        }

        usleep(retry_delay_us);
    }

    if (!connection_completed) {
        fprintf(
            stderr,
            "Wi-Fi DHCP restart skipped: connection did not complete\n");

        return false;
    }

    system(
        "pkill -9 udhcpc "
        ">/dev/null 2>&1");

    system(
        "ifconfig wlan0 0.0.0.0 "
        ">/dev/null 2>&1");

    system(
        "route del default dev wlan0 "
        ">/dev/null 2>&1");

    int status = system(
        "udhcpc -i wlan0 "
        "-s /etc/init.d/udhcpc.script "
        ">/dev/null 2>&1 &");

    if (status != 0) {
        fprintf(
            stderr,
            "Wi-Fi DHCP restart failed: status=%d\n",
            status);

        return false;
    }

    fprintf(
        stderr,
        "Wi-Fi DHCP client restarted\n");

    return true;
}

static bool reload_wifi_runtime(void)
{
    if (!reconfigure_wifi()) {
        return false;
    }

    return restart_wifi_dhcp();
}

static char *skip_leading_whitespace(char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }

    return text;
}

static bool append_text(
    char *destination,
    size_t destination_size,
    const char *source)
{
    size_t destination_length = strlen(destination);
    size_t source_length = strlen(source);

    if (
        destination_length + source_length >=
        destination_size) {
        return false;
    }

    memcpy(
        destination + destination_length,
        source,
        source_length + 1);

    return true;
}

static bool append_text_length(
    char *destination,
    size_t destination_size,
    const char *source,
    size_t source_length)
{
    size_t destination_length = strlen(destination);

    if (
        destination_length + source_length >=
        destination_size) {
        return false;
    }

    memcpy(
        destination + destination_length,
        source,
        source_length);

    destination[destination_length + source_length] = '\0';

    return true;
}

static bool parse_ssid_line(
    const char *line,
    char *ssid,
    size_t ssid_size)
{
    const char *first_quote = strchr(line, '"');
    const char *last_quote = strrchr(line, '"');

    if (
        first_quote == NULL ||
        last_quote == NULL ||
        last_quote <= first_quote) {
        return false;
    }

    size_t ssid_length =
        (size_t)(last_quote - first_quote - 1);

    if (ssid_length >= ssid_size) {
        ssid_length = ssid_size - 1;
    }

    memcpy(
        ssid,
        first_quote + 1,
        ssid_length);

    ssid[ssid_length] = '\0';

    return true;
}

static bool parse_priority_line(
    const char *line,
    int *priority)
{
    const char *equals = strchr(line, '=');
    char *end_pointer = NULL;
    long parsed_priority;

    if (equals == NULL) {
        return false;
    }

    parsed_priority = strtol(
        equals + 1,
        &end_pointer,
        10);

    if (end_pointer == equals + 1) {
        return false;
    }

    *priority = (int)parsed_priority;

    return true;
}

int count_networks_with_ssid(
    const WifiConfig *config,
    const char *ssid)
{
    if (
        config == NULL ||
        ssid == NULL) {
        return 0;
    }

    int match_count = 0;

    for (
        int network_index = 0;
        network_index < config->network_count;
        network_index++) {
        if (
            strcmp(
                config->networks[network_index].ssid,
                ssid) == 0) {
            match_count++;
        }
    }

    return match_count;
}

static int find_unique_network(
    const WifiConfig *config,
    const char *ssid)
{
    int found_index = -1;

    for (
        int network_index = 0;
        network_index < config->network_count;
        network_index++) {
        if (
            strcmp(
                config->networks[network_index].ssid,
                ssid) != 0) {
            continue;
        }

        if (found_index >= 0) {
            return -2;
        }

        found_index = network_index;
    }

    return found_index;
}

static bool get_network_block(
    const WifiConfig *config,
    int network_index,
    char *output,
    size_t output_size)
{
    if (
        config == NULL ||
        output == NULL ||
        output_size == 0 ||
        network_index < 0 ||
        network_index >= config->network_count) {
        return false;
    }

    const WifiNetwork *network =
        &config->networks[network_index];

    if (
        network->block_end < network->block_start ||
        network->block_end > config->text_length) {
        return false;
    }

    size_t block_length =
        network->block_end - network->block_start;

    if (block_length >= output_size) {
        return false;
    }

    memcpy(
        output,
        config->text + network->block_start,
        block_length);

    output[block_length] = '\0';

    return true;
}

static bool replace_config_range(
    WifiConfig *config,
    size_t start,
    size_t end,
    const char *replacement,
    size_t replacement_length)
{
    if (
        config == NULL ||
        start > end ||
        end > config->text_length ||
        (replacement_length > 0 &&
         replacement == NULL)) {
        return false;
    }

    size_t removed_length = end - start;

    if (
        config->text_length -
            removed_length +
            replacement_length >
        MAX_WIFI_CONFIG_SIZE) {
        return false;
    }

    size_t tail_length =
        config->text_length - end;

    memmove(
        config->text + start + replacement_length,
        config->text + end,
        tail_length);

    if (replacement_length > 0) {
        memcpy(
            config->text + start,
            replacement,
            replacement_length);
    }

    config->text_length =
        config->text_length -
        removed_length +
        replacement_length;

    config->text[config->text_length] = '\0';

    return true;
}

static bool remove_all_network_blocks(
    WifiConfig *config)
{
    if (config == NULL) {
        return false;
    }

    for (
        int network_index =
            config->network_count - 1;
        network_index >= 0;
        network_index--) {
        const WifiNetwork *network =
            &config->networks[network_index];

        if (
            !replace_config_range(
                config,
                network->block_start,
                network->block_end,
                NULL,
                0)) {
            return false;
        }
    }

    config->network_count = 0;

    return true;
}

static bool parse_network_block_metadata(
    const char *block,
    size_t block_length,
    size_t block_start,
    WifiNetwork *network)
{
    if (
        block == NULL ||
        network == NULL ||
        block_length == 0) {
        return false;
    }

    memset(network, 0, sizeof(*network));

    network->block_start = block_start;
    network->block_end =
        block_start + block_length;

    bool saw_network_start = false;
    bool saw_closing_brace = false;
    size_t line_start = 0;

    while (line_start < block_length) {
        const char *newline = memchr(
            block + line_start,
            '\n',
            block_length - line_start);

        size_t line_end =
            newline != NULL
                ? (size_t)(newline - block) + 1
                : block_length;

        size_t content_end = line_end;

        while (
            content_end > line_start &&
            (block[content_end - 1] == '\n' ||
             block[content_end - 1] == '\r')) {
            content_end--;
        }

        size_t content_length =
            content_end - line_start;

        if (content_length >= LINE_BUFFER_SIZE) {
            return false;
        }

        char line[LINE_BUFFER_SIZE];

        memcpy(
            line,
            block + line_start,
            content_length);

        line[content_length] = '\0';

        char *trimmed =
            skip_leading_whitespace(line);

        if (
            strncmp(
                trimmed,
                "network={",
                9) == 0) {
            saw_network_start = true;
        }
        else if (
            strncmp(
                trimmed,
                "ssid=",
                5) == 0) {
            parse_ssid_line(
                trimmed,
                network->ssid,
                sizeof(network->ssid));
        }
        else if (
            strncmp(
                trimmed,
                "priority=",
                9) == 0) {
            network->has_priority =
                parse_priority_line(
                    trimmed,
                    &network->priority);
        }
        else if (trimmed[0] == '}') {
            saw_closing_brace = true;
        }

        line_start = line_end;
    }

    return (
        saw_network_start &&
        saw_closing_brace);
}

static bool append_network_block(
    WifiConfig *config,
    const char *block)
{
    if (
        config == NULL ||
        block == NULL ||
        block[0] == '\0' ||
        config->network_count >= MAX_SAVED_NETWORKS) {
        return false;
    }

    size_t block_length = strlen(block);

    const char *separator = "";
    size_t separator_length = 0;

    if (config->text_length > 0) {
        if (
            config->text[config->text_length - 1] != '\n') {
            separator = "\n\n";
            separator_length = 2;
        }
        else if (
            config->text_length < 2 ||
            config->text[config->text_length - 2] != '\n') {
            separator = "\n";
            separator_length = 1;
        }
    }

    if (
        config->text_length +
            separator_length +
            block_length >
        MAX_WIFI_CONFIG_SIZE) {
        return false;
    }

    size_t block_start =
        config->text_length +
        separator_length;

    WifiNetwork network = {0};

    if (
        !parse_network_block_metadata(
            block,
            block_length,
            block_start,
            &network)) {
        return false;
    }

    if (separator_length > 0) {
        memcpy(
            config->text + config->text_length,
            separator,
            separator_length);

        config->text_length +=
            separator_length;
    }

    memcpy(
        config->text + config->text_length,
        block,
        block_length);

    config->text_length += block_length;
    config->text[config->text_length] = '\0';

    config->networks[config->network_count] = network;

    config->network_count++;

    return true;
}

bool load_wifi_config(
    const char *path,
    WifiConfig *config)
{
    if (
        path == NULL ||
        config == NULL) {
        return false;
    }

    memset(config, 0, sizeof(*config));

    FILE *config_file = fopen(path, "rb");

    if (config_file == NULL) {
        return false;
    }

    config->text_length = fread(
        config->text,
        1,
        MAX_WIFI_CONFIG_SIZE,
        config_file);

    if (ferror(config_file)) {
        fclose(config_file);
        return false;
    }

    if (
        config->text_length ==
        MAX_WIFI_CONFIG_SIZE) {
        int extra_byte = fgetc(config_file);

        if (extra_byte != EOF) {
            fclose(config_file);
            return false;
        }
    }

    fclose(config_file);

    config->text[config->text_length] = '\0';

    bool inside_network = false;
    WifiNetwork current_network = {0};

    size_t line_start = 0;

    while (line_start < config->text_length) {
        const char *line_pointer =
            config->text + line_start;

        const char *newline = memchr(
            line_pointer,
            '\n',
            config->text_length - line_start);

        size_t line_end =
            newline != NULL
                ? (size_t)(newline -
                           config->text) +
                      1
                : config->text_length;

        size_t content_end = line_end;

        while (
            content_end > line_start &&
            (config->text[content_end - 1] == '\n' ||
             config->text[content_end - 1] == '\r')) {
            content_end--;
        }

        size_t content_length =
            content_end - line_start;

        if (content_length >= LINE_BUFFER_SIZE) {
            return false;
        }

        char line[LINE_BUFFER_SIZE];

        memcpy(
            line,
            config->text + line_start,
            content_length);

        line[content_length] = '\0';

        char *trimmed =
            skip_leading_whitespace(line);

        if (!inside_network) {
            if (
                strncmp(
                    trimmed,
                    "network={",
                    9) == 0) {
                if (
                    config->network_count >=
                    MAX_SAVED_NETWORKS) {
                    return false;
                }

                memset(
                    &current_network,
                    0,
                    sizeof(current_network));

                current_network.block_start =
                    line_start;

                inside_network = true;
            }
        }
        else {
            if (
                strncmp(
                    trimmed,
                    "ssid=",
                    5) == 0) {
                parse_ssid_line(
                    trimmed,
                    current_network.ssid,
                    sizeof(current_network.ssid));
            }
            else if (
                strncmp(
                    trimmed,
                    "priority=",
                    9) == 0) {
                current_network.has_priority =
                    parse_priority_line(
                        trimmed,
                        &current_network.priority);
            }
            else if (trimmed[0] == '}') {
                current_network.block_end =
                    line_end;

                config->networks[config->network_count] = current_network;

                config->network_count++;
                inside_network = false;
            }
        }

        line_start = line_end;
    }

    return !inside_network;
}

static bool build_block_with_priority(
    const char *source_block,
    int priority,
    char *output_block,
    size_t output_size)
{
    const char *cursor = source_block;
    bool priority_written = false;
    bool closing_brace_found = false;

    output_block[0] = '\0';

    while (*cursor != '\0') {
        const char *line_end = strchr(cursor, '\n');
        size_t line_length = line_end != NULL
                                 ? (size_t)(line_end - cursor)
                                 : strlen(cursor);

        char line[LINE_BUFFER_SIZE];
        char trimmed_line[LINE_BUFFER_SIZE];
        char *trimmed;

        if (line_length >= sizeof(line)) {
            fprintf(
                stderr,
                "priority build failed: line too long (%lu bytes)\n",
                (unsigned long)line_length);
            return false;
        }

        memcpy(line, cursor, line_length);
        line[line_length] = '\0';

        strncpy(
            trimmed_line,
            line,
            sizeof(trimmed_line) - 1);

        trimmed_line[sizeof(trimmed_line) - 1] = '\0';

        trimmed = skip_leading_whitespace(
            trimmed_line);

        if (
            strncmp(
                trimmed,
                "priority=",
                9) == 0) {
            char priority_line[64];

            snprintf(
                priority_line,
                sizeof(priority_line),
                "        priority=%d",
                priority);

            if (
                !append_text(
                    output_block,
                    output_size,
                    priority_line)) {
                fprintf(
                    stderr,
                    "priority build failed: replacement exceeded buffer\n");
                return false;
            }

            priority_written = true;
        }
        else {
            if (trimmed[0] == '}') {
                closing_brace_found = true;

                if (!priority_written) {
                    char priority_line[64];

                    snprintf(
                        priority_line,
                        sizeof(priority_line),
                        "        priority=%d\n",
                        priority);

                    if (
                        !append_text(
                            output_block,
                            output_size,
                            priority_line)) {
                        fprintf(
                            stderr,
                            "priority build failed: insertion exceeded buffer\n");
                        return false;
                    }

                    priority_written = true;
                }
            }

            if (
                !append_text_length(
                    output_block,
                    output_size,
                    line,
                    line_length)) {
                fprintf(
                    stderr,
                    "priority build failed: source line exceeded buffer\n");
                return false;
            }
        }

        if (line_end == NULL) {
            break;
        }

        if (
            !append_text(
                output_block,
                output_size,
                "\n")) {
            fprintf(
                stderr,
                "priority build failed: newline exceeded buffer\n");
            return false;
        }

        cursor = line_end + 1;
    }

    if (!priority_written || !closing_brace_found) {
        fprintf(
            stderr,
            "priority build failed: priority=%d closing_brace=%d block_length=%lu\n",
            priority_written,
            closing_brace_found,
            (unsigned long)strlen(source_block));
        return false;
    }

    return true;
}

static bool write_wifi_config(
    const char *path,
    const WifiConfig *config)
{
    if (
        path == NULL ||
        config == NULL) {
        return false;
    }

    FILE *output_file = fopen(path, "wb");

    if (output_file == NULL) {
        return false;
    }

    if (
        config->text_length > 0 &&
        fwrite(
            config->text,
            1,
            config->text_length,
            output_file) != config->text_length) {
        fclose(output_file);
        return false;
    }

    if (
        fflush(output_file) != 0 ||
        fclose(output_file) != 0) {
        return false;
    }

    return true;
}

static bool copy_file(
    const char *source_path,
    const char *destination_path)
{
    FILE *source_file;
    FILE *destination_file;

    char buffer[4096];
    size_t bytes_read;

    source_file = fopen(source_path, "rb");

    if (source_file == NULL) {
        return false;
    }

    destination_file = fopen(
        destination_path,
        "wb");

    if (destination_file == NULL) {
        fclose(source_file);
        return false;
    }

    while (
        (
            bytes_read = fread(
                buffer,
                1,
                sizeof(buffer),
                source_file)) > 0) {
        if (
            fwrite(
                buffer,
                1,
                bytes_read,
                destination_file) != bytes_read) {
            fclose(source_file);
            fclose(destination_file);
            return false;
        }
    }

    if (ferror(source_file)) {
        fclose(source_file);
        fclose(destination_file);
        return false;
    }

    fclose(source_file);

    if (
        fflush(destination_file) != 0 ||
        fclose(destination_file) != 0) {
        return false;
    }

    return true;
}

static void remove_temporary_files(void)
{
    unlink(SAVED_TEMP_PATH);
    unlink(BACKUP_TEMP_PATH);
    unlink(ACTIVE_TEMP_PATH);
}

bool get_connected_network_ssid(
    char *ssid,
    size_t ssid_size)
{
    FILE *status_pipe;
    char line[LINE_BUFFER_SIZE];
    bool connection_completed = false;

    if (
        ssid == NULL ||
        ssid_size == 0) {
        return false;
    }

    ssid[0] = '\0';

    status_pipe = popen(
        WPA_CLI_PATH
        " -i wlan0 status 2>/dev/null",
        "r");

    if (status_pipe == NULL) {
        return false;
    }

    while (
        fgets(
            line,
            sizeof(line),
            status_pipe) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';

        if (
            strcmp(
                line,
                "wpa_state=COMPLETED") == 0) {
            connection_completed = true;
        }
        else if (
            strncmp(
                line,
                "ssid=",
                5) == 0) {
            strncpy(
                ssid,
                line + 5,
                ssid_size - 1);

            ssid[ssid_size - 1] = '\0';
        }
    }

    pclose(status_pipe);

    return (
        connection_completed &&
        ssid[0] != '\0');
}

SaveCurrentNetworkResult save_current_network(
    char *saved_ssid,
    size_t saved_ssid_size)
{
    WifiConfig *active_config =
        calloc(1, sizeof(WifiConfig));

    WifiConfig *saved_config =
        calloc(1, sizeof(WifiConfig));

    WifiConfig *validation_config =
        calloc(1, sizeof(WifiConfig));

    char *source_block =
        calloc(1, MAX_NETWORK_BLOCK);

    char *prioritized_block =
        calloc(1, MAX_NETWORK_BLOCK);

    char connected_ssid[STR_MAX] = {0};

    bool saved_file_exists =
        access(SAVED_CONFIG_PATH, F_OK) == 0;

    if (
        saved_ssid == NULL ||
        saved_ssid_size == 0 ||
        active_config == NULL ||
        saved_config == NULL ||
        validation_config == NULL ||
        source_block == NULL ||
        prioritized_block == NULL) {
        fprintf(
            stderr,
            "save failed: insufficient memory\n");

        free(active_config);
        free(saved_config);
        free(validation_config);
        free(source_block);
        free(prioritized_block);

        return SAVE_CURRENT_NETWORK_FAILED;
    }

    saved_ssid[0] = '\0';
    remove_temporary_files();

    if (
        !get_connected_network_ssid(
            connected_ssid,
            sizeof(connected_ssid))) {
        free(active_config);
        free(saved_config);
        free(validation_config);
        free(source_block);
        free(prioritized_block);

        return SAVE_CURRENT_NETWORK_NOT_CONNECTED;
    }

    strncpy(
        saved_ssid,
        connected_ssid,
        saved_ssid_size - 1);

    saved_ssid[saved_ssid_size - 1] = '\0';

    if (
        !load_wifi_config(
            ACTIVE_CONFIG_PATH,
            active_config)) {
        fprintf(
            stderr,
            "save failed: active Wi-Fi configuration is invalid\n");

        free(active_config);
        free(saved_config);
        free(validation_config);
        free(source_block);
        free(prioritized_block);

        return SAVE_CURRENT_NETWORK_FAILED;
    }

    int active_index = find_unique_network(
        active_config,
        connected_ssid);

    if (active_index < 0) {
        fprintf(
            stderr,
            active_index == -2
                ? "save failed: connected SSID is ambiguous in active configuration\n"
                : "save failed: connected network was not found in active configuration\n");

        free(active_config);
        free(saved_config);
        free(validation_config);
        free(source_block);
        free(prioritized_block);

        return active_index == -2
                   ? SAVE_CURRENT_NETWORK_AMBIGUOUS
                   : SAVE_CURRENT_NETWORK_FAILED;
    }

    if (saved_file_exists) {
        if (
            !load_wifi_config(
                SAVED_CONFIG_PATH,
                saved_config)) {
            fprintf(
                stderr,
                "save failed: saved Wi-Fi configuration is invalid\n");

            free(active_config);
            free(saved_config);
            free(validation_config);
            free(source_block);
            free(prioritized_block);

            return SAVE_CURRENT_NETWORK_FAILED;
        }

        if (
            find_unique_network(
                saved_config,
                connected_ssid) != -1) {
            free(active_config);
            free(saved_config);
            free(validation_config);
            free(source_block);
            free(prioritized_block);

            return SAVE_CURRENT_NETWORK_ALREADY_SAVED;
        }
    }
    else {
        *saved_config = *active_config;

        if (!remove_all_network_blocks(saved_config)) {
            fprintf(
                stderr,
                "save failed: could not prepare saved configuration\n");

            free(active_config);
            free(saved_config);
            free(validation_config);
            free(source_block);
            free(prioritized_block);

            return SAVE_CURRENT_NETWORK_FAILED;
        }
    }

    if (
        !get_network_block(
            active_config,
            active_index,
            source_block,
            MAX_NETWORK_BLOCK)) {
        fprintf(
            stderr,
            "save failed: active network block is too large or invalid\n");

        free(active_config);
        free(saved_config);
        free(validation_config);
        free(source_block);
        free(prioritized_block);

        return SAVE_CURRENT_NETWORK_FAILED;
    }

    const WifiNetwork *active_network =
        &active_config->networks[active_index];

    int priority =
        active_network->has_priority
            ? active_network->priority
            : 0;

    if (
        !build_block_with_priority(
            source_block,
            priority,
            prioritized_block,
            MAX_NETWORK_BLOCK) ||
        !append_network_block(
            saved_config,
            prioritized_block)) {
        fprintf(
            stderr,
            "save failed: could not prepare network block for %s\n",
            connected_ssid);

        free(active_config);
        free(saved_config);
        free(validation_config);
        free(source_block);
        free(prioritized_block);

        return SAVE_CURRENT_NETWORK_FAILED;
    }

    int expected_network_count =
        saved_config->network_count;

    if (
        !write_wifi_config(
            SAVED_TEMP_PATH,
            saved_config) ||
        !load_wifi_config(
            SAVED_TEMP_PATH,
            validation_config) ||
        validation_config->network_count !=
            expected_network_count) {
        fprintf(
            stderr,
            "save failed: saved configuration did not validate\n");

        remove_temporary_files();

        free(active_config);
        free(saved_config);
        free(validation_config);
        free(source_block);
        free(prioritized_block);

        return SAVE_CURRENT_NETWORK_FAILED;
    }

    if (
        rename(
            SAVED_TEMP_PATH,
            SAVED_CONFIG_PATH) != 0) {
        fprintf(
            stderr,
            "save failed: could not install saved configuration\n");

        remove_temporary_files();

        free(active_config);
        free(saved_config);
        free(validation_config);
        free(source_block);
        free(prioritized_block);

        return SAVE_CURRENT_NETWORK_FAILED;
    }

    sync();
    remove_temporary_files();

    free(active_config);
    free(saved_config);
    free(validation_config);
    free(source_block);
    free(prioritized_block);

    return SAVE_CURRENT_NETWORK_SUCCESS;
}

bool get_saved_network_priority(
    int network_index,
    int *priority)
{
    WifiConfig *saved_config =
        calloc(1, sizeof(WifiConfig));

    if (
        priority == NULL ||
        saved_config == NULL) {
        free(saved_config);
        return false;
    }

    if (
        !load_wifi_config(
            SAVED_CONFIG_PATH,
            saved_config) ||
        network_index < 0 ||
        network_index >= saved_config->network_count) {
        free(saved_config);
        return false;
    }

    const WifiNetwork *network =
        &saved_config->networks[network_index];

    *priority =
        network->has_priority
            ? network->priority
            : 0;

    free(saved_config);
    return true;
}

bool update_saved_network_priority(
    int network_index,
    int priority)
{
    WifiConfig *saved_config =
        calloc(1, sizeof(WifiConfig));

    WifiConfig *validation_config =
        calloc(1, sizeof(WifiConfig));

    char *source_block =
        calloc(1, MAX_NETWORK_BLOCK);

    char *prioritized_block =
        calloc(1, MAX_NETWORK_BLOCK);

    if (
        saved_config == NULL ||
        validation_config == NULL ||
        source_block == NULL ||
        prioritized_block == NULL) {
        fprintf(
            stderr,
            "priority update failed: insufficient memory\n");

        free(saved_config);
        free(validation_config);
        free(source_block);
        free(prioritized_block);

        return false;
    }

    remove_temporary_files();

    if (
        !load_wifi_config(
            SAVED_CONFIG_PATH,
            saved_config) ||
        network_index < 0 ||
        network_index >= saved_config->network_count) {
        fprintf(
            stderr,
            "priority update failed: network index is invalid\n");

        free(saved_config);
        free(validation_config);
        free(source_block);
        free(prioritized_block);

        return false;
    }

    if (
        !get_network_block(
            saved_config,
            network_index,
            source_block,
            MAX_NETWORK_BLOCK) ||
        !build_block_with_priority(
            source_block,
            priority,
            prioritized_block,
            MAX_NETWORK_BLOCK)) {
        fprintf(
            stderr,
            "priority update failed: could not update network block\n");

        free(saved_config);
        free(validation_config);
        free(source_block);
        free(prioritized_block);

        return false;
    }

    const WifiNetwork *network =
        &saved_config->networks[network_index];

    if (
        !replace_config_range(
            saved_config,
            network->block_start,
            network->block_end,
            prioritized_block,
            strlen(prioritized_block)) ||
        !write_wifi_config(
            SAVED_TEMP_PATH,
            saved_config) ||
        !load_wifi_config(
            SAVED_TEMP_PATH,
            validation_config) ||
        network_index >= validation_config->network_count ||
        !validation_config
             ->networks[network_index]
             .has_priority ||
        validation_config
                ->networks[network_index]
                .priority != priority) {
        fprintf(
            stderr,
            "priority update failed: value did not validate\n");

        remove_temporary_files();

        free(saved_config);
        free(validation_config);
        free(source_block);
        free(prioritized_block);

        return false;
    }

    if (
        rename(
            SAVED_TEMP_PATH,
            SAVED_CONFIG_PATH) != 0) {
        fprintf(
            stderr,
            "priority update failed: could not install saved configuration\n");

        remove_temporary_files();

        free(saved_config);
        free(validation_config);
        free(source_block);
        free(prioritized_block);

        return false;
    }

    sync();
    remove_temporary_files();

    free(saved_config);
    free(validation_config);
    free(source_block);
    free(prioritized_block);

    return true;
}

static bool remove_network_from_config(
    WifiConfig *config,
    int network_index)
{
    if (
        config == NULL ||
        network_index < 0 ||
        network_index >= config->network_count) {
        return false;
    }

    const WifiNetwork *network =
        &config->networks[network_index];

    if (
        !replace_config_range(
            config,
            network->block_start,
            network->block_end,
            NULL,
            0)) {
        return false;
    }

    config->network_count--;

    return true;
}

bool delete_saved_network(
    int network_index)
{
    WifiConfig *saved_config =
        calloc(1, sizeof(WifiConfig));

    WifiConfig *validation_config =
        calloc(1, sizeof(WifiConfig));

    if (
        saved_config == NULL ||
        validation_config == NULL) {
        fprintf(
            stderr,
            "remove failed: insufficient memory\n");

        free(saved_config);
        free(validation_config);

        return false;
    }

    remove_temporary_files();

    if (
        !load_wifi_config(
            SAVED_CONFIG_PATH,
            saved_config) ||
        network_index < 0 ||
        network_index >= saved_config->network_count) {
        fprintf(
            stderr,
            "remove failed: network index is invalid\n");

        free(saved_config);
        free(validation_config);

        return false;
    }

    int expected_network_count =
        saved_config->network_count - 1;

    if (
        !remove_network_from_config(
            saved_config,
            network_index) ||
        !write_wifi_config(
            SAVED_TEMP_PATH,
            saved_config) ||
        !load_wifi_config(
            SAVED_TEMP_PATH,
            validation_config) ||
        validation_config->network_count !=
            expected_network_count) {
        fprintf(
            stderr,
            "remove failed: updated configuration did not validate\n");

        remove_temporary_files();

        free(saved_config);
        free(validation_config);

        return false;
    }

    if (
        rename(
            SAVED_TEMP_PATH,
            SAVED_CONFIG_PATH) != 0) {
        fprintf(
            stderr,
            "remove failed: could not install saved configuration\n");

        remove_temporary_files();

        free(saved_config);
        free(validation_config);

        return false;
    }

    sync();
    remove_temporary_files();

    free(saved_config);
    free(validation_config);

    return true;
}

static bool build_applied_config(
    const WifiConfig *active_config,
    const WifiConfig *saved_config,
    WifiConfig *applied_config)
{
    if (
        active_config == NULL ||
        saved_config == NULL ||
        applied_config == NULL) {
        return false;
    }

    *applied_config = *active_config;

    if (!remove_all_network_blocks(applied_config)) {
        return false;
    }

    char network_block[MAX_NETWORK_BLOCK];

    for (
        int network_index = 0;
        network_index < saved_config->network_count;
        network_index++) {
        if (
            !get_network_block(
                saved_config,
                network_index,
                network_block,
                sizeof(network_block)) ||
            !append_network_block(
                applied_config,
                network_block)) {
            return false;
        }
    }

    return true;
}

static bool restore_active_config_backup(void)
{
    if (
        !copy_file(
            BACKUP_CONFIG_PATH,
            ACTIVE_TEMP_PATH) ||
        rename(
            ACTIVE_TEMP_PATH,
            ACTIVE_CONFIG_PATH) != 0) {
        fprintf(
            stderr,
            "apply saved networks rollback failed: "
            "could not restore active configuration\n");

        remove_temporary_files();
        return false;
    }

    sync();

    if (!reload_wifi_runtime()) {
        fprintf(
            stderr,
            "apply saved networks rollback failed: "
            "restored configuration did not reconnect\n");

        remove_temporary_files();
        return false;
    }

    fprintf(
        stderr,
        "apply saved networks rollback complete\n");

    remove_temporary_files();

    return true;
}

bool apply_saved_networks(void)
{
    if (access(SAVED_CONFIG_PATH, F_OK) != 0) {
        fprintf(
            stderr,
            "apply saved networks failed: no saved configuration\n");

        return false;
    }

    WifiConfig *saved_config =
        calloc(1, sizeof(WifiConfig));

    WifiConfig *active_config =
        calloc(1, sizeof(WifiConfig));

    WifiConfig *applied_config =
        calloc(1, sizeof(WifiConfig));

    if (
        saved_config == NULL ||
        active_config == NULL ||
        applied_config == NULL) {
        fprintf(
            stderr,
            "apply saved networks failed: insufficient memory\n");

        free(saved_config);
        free(active_config);
        free(applied_config);

        return false;
    }

    remove_temporary_files();

    if (
        !load_wifi_config(
            SAVED_CONFIG_PATH,
            saved_config) ||
        saved_config->network_count == 0) {
        fprintf(
            stderr,
            "apply saved networks failed: "
            "saved configuration has no usable networks\n");

        free(saved_config);
        free(active_config);
        free(applied_config);

        return false;
    }

    if (
        !load_wifi_config(
            ACTIVE_CONFIG_PATH,
            active_config)) {
        fprintf(
            stderr,
            "apply saved networks failed: "
            "active configuration is invalid\n");

        free(saved_config);
        free(active_config);
        free(applied_config);

        return false;
    }

    if (
        !build_applied_config(
            active_config,
            saved_config,
            applied_config) ||
        !write_wifi_config(
            ACTIVE_TEMP_PATH,
            applied_config)) {
        fprintf(
            stderr,
            "apply saved networks failed: "
            "could not prepare replacement configuration\n");

        remove_temporary_files();

        free(saved_config);
        free(active_config);
        free(applied_config);

        return false;
    }

    /*
     * Reparse the generated file before touching the live configuration.
     * This verifies that every saved network survived the merge.
     */
    memset(
        active_config,
        0,
        sizeof(*active_config));

    if (
        !load_wifi_config(
            ACTIVE_TEMP_PATH,
            active_config) ||
        active_config->network_count !=
            saved_config->network_count) {
        fprintf(
            stderr,
            "apply saved networks failed: "
            "replacement did not validate\n");

        remove_temporary_files();

        free(saved_config);
        free(active_config);
        free(applied_config);

        return false;
    }

    int compare_status = system(
        "cmp -s " ACTIVE_TEMP_PATH
        " " ACTIVE_CONFIG_PATH);

    if (compare_status == 0) {
        fprintf(
            stderr,
            "apply saved networks: configuration already active\n");

        remove_temporary_files();

        free(saved_config);
        free(active_config);
        free(applied_config);

        return true;
    }

    if (
        !copy_file(
            ACTIVE_CONFIG_PATH,
            BACKUP_TEMP_PATH) ||
        rename(
            BACKUP_TEMP_PATH,
            BACKUP_CONFIG_PATH) != 0) {
        fprintf(
            stderr,
            "apply saved networks failed: "
            "could not install active configuration backup\n");

        remove_temporary_files();

        free(saved_config);
        free(active_config);
        free(applied_config);

        return false;
    }

    if (
        rename(
            ACTIVE_TEMP_PATH,
            ACTIVE_CONFIG_PATH) != 0) {
        fprintf(
            stderr,
            "apply saved networks failed: "
            "could not install active configuration\n");

        remove_temporary_files();

        free(saved_config);
        free(active_config);
        free(applied_config);

        return false;
    }

    sync();

    if (!reload_wifi_runtime()) {
        fprintf(
            stderr,
            "apply saved networks failed: "
            "new configuration did not reconnect; rolling back\n");

        bool rollback_succeeded =
            restore_active_config_backup();

        if (!rollback_succeeded) {
            fprintf(
                stderr,
                "apply saved networks failed: rollback was unsuccessful\n");
        }

        free(saved_config);
        free(active_config);
        free(applied_config);

        return false;
    }

    fprintf(
        stderr,
        "apply saved networks complete: %d network%s\n",
        saved_config->network_count,
        saved_config->network_count == 1
            ? ""
            : "s");

    remove_temporary_files();

    free(saved_config);
    free(active_config);
    free(applied_config);

    return true;
}
