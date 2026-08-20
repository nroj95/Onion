#ifndef TWEAKS_NETWORK_H__
#define TWEAKS_NETWORK_H__

#include <SDL/SDL_image.h>
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "components/list.h"
#include "system/keymap_sw.h"
#include "theme/render/dialog.h"
#include "theme/sound.h"
#include "utils/apply_icons.h"
#include "utils/json.h"
#include "utils/keystate.h"
#include "utils/netinfo.h"
#include "utils/process.h"

#include "./appstate.h"
#include "./info_dialog.h"
#include "./wifi_networks.h"

#define NET_SCRIPT_PATH "/mnt/SDCARD/.tmp_update/script/network"
#define SMBD_CONFIG_PATH "/mnt/SDCARD/.tmp_update/config/smb.conf"

static char wifi_selected_network_ssid[STR_MAX] = {0};
static int wifi_selected_network_index = -1;

void menu_wifiSavedNetworks(void *pt);

static struct network_s {
    bool smbd;
    bool http;
    bool ssh;
    bool telnet;
    bool ftp;
    bool hotspot;
    bool ntp;
    bool ntp_wait;
    bool auth_smbd;
    bool auth_ftp;
    bool auth_http;
    bool auth_ssh;
    bool manual_tz;
    bool check_updates;
    bool keep_alive;
    bool vncserv;
    bool loaded;
    bool force_wifi_on_startup;
    int vncfps;
} network_state = {
    .vncfps = 20,
};

void network_loadState(void)
{
    if (network_state.loaded)
        return;

    network_state.smbd = config_flag_get(".smbdState");
    network_state.http = config_flag_get(".httpState");
    network_state.ssh = config_flag_get(".sshState");
    network_state.telnet = config_flag_get(".telnetState");
    network_state.ftp = config_flag_get(".ftpState");
    network_state.hotspot = config_flag_get(".hotspotState");
    network_state.ntp = config_flag_get(".ntpState");
    network_state.ntp_wait = config_flag_get(".ntpWait");
    network_state.force_wifi_on_startup = config_flag_get(".ntpForce");
    network_state.auth_ftp = config_flag_get(".authftpState");
    network_state.auth_http = config_flag_get(".authhttpState");
    network_state.auth_ssh = config_flag_get(".authsshState");
    network_state.manual_tz = config_flag_get(".manual_tz");
    network_state.check_updates = config_flag_get(".checkUpdates");
    network_state.keep_alive = config_flag_get(".keepServicesAlive");
    network_state.vncserv = config_flag_get(".vncServer");
    config_get(".vncfps", CONFIG_INT, &network_state.vncfps);
    network_state.loaded = true;
}

typedef struct {
    char name[STR_MAX - 11];
    char path[STR_MAX];
    int available;     // 1 if available = yes, 0 otherwise
    long availablePos; // in file position for the available property
} Share;

static Share *_network_shares = NULL;
static int network_numShares;

void network_freeSmbShares()
{
    if (_network_shares != NULL) {
        free(_network_shares);
    }
}

void network_getSmbShares()
{
    if (_network_shares != NULL) {
        return;
    }

    int numShares = 0;

    FILE *file = fopen(SMBD_CONFIG_PATH, "r");
    if (file == NULL) {
        printf("Failed to open smb.conf file.\n");
        return;
    }

    char line[STR_MAX];

    bool found_shares = false;
    bool is_available = false;
    long availablePos = -1;

    while (fgets(line, sizeof(line), file) != NULL) {
        char *trimmedLine = strtok(line, "\n");
        if (trimmedLine == NULL) {
            continue;
        }

        if (strstr(trimmedLine, "available = ") != NULL) {
            availablePos = ftell(file) - strlen(trimmedLine) - 1;
            is_available = strstr(trimmedLine, "1") != NULL;
            continue;
        }

        if (strstr(trimmedLine, "path = ") != NULL) {
            strncpy(_network_shares[numShares - 1].path, trimmedLine + 7, STR_MAX);
            continue;
        }

        if (strncmp(trimmedLine, "[", 1) == 0 && strncmp(trimmedLine + strlen(trimmedLine) - 1, "]", 1) == 0) {
            if (found_shares) {
                _network_shares[numShares - 1].available = is_available;
                _network_shares[numShares - 1].availablePos = availablePos;
                is_available = false;
            }

            char *shareName = strtok(trimmedLine + 1, "]");
            if (shareName != NULL && strlen(shareName) > 0) {
                if (strcmp(shareName, "global") == 0) {
                    continue;
                }

                numShares++;
                _network_shares = (Share *)realloc(_network_shares, numShares * sizeof(Share));

                bool add_exclamation = false;
                if (strncmp("__", shareName, 2) == 0) {
                    shareName = shareName + 2;
                    add_exclamation = true;
                }

                strncpy(_network_shares[numShares - 1].name, shareName, STR_MAX - 11);

                if (add_exclamation) {
                    strncat(_network_shares[numShares - 1].name, " (!)", STR_MAX - 11 - strlen(shareName));
                }

                found_shares = true;
            }
        }
    }

    if (found_shares) {
        _network_shares[numShares - 1].available = is_available;
        _network_shares[numShares - 1].availablePos = availablePos;
    }

    network_numShares = numShares;

    fclose(file);
}

void network_toggleSmbAvailable(void *item)
{
    ListItem *listItem = (ListItem *)item;
    Share *share = (Share *)listItem->payload_ptr;

    FILE *file = fopen(SMBD_CONFIG_PATH, "r+");
    if (file == NULL) {
        printf("Failed to open smb.conf file.\n");
        return;
    }

    if (fseek(file, share->availablePos, SEEK_SET) != 0) {
        printf("Failed to seek to the available property of share '%s'.\n", share->name);
        fclose(file);
        return;
    }

    char line[STR_MAX];
    fgets(line, sizeof(line), file);
    share->available = strstr(line, "1") == NULL; // toggle

    fseek(file, share->availablePos, SEEK_SET);
    fprintf(file, "available = %d\n", share->available);
    fflush(file);

    fclose(file);
}

void network_setState(bool *state_ptr, const char *flag_name, bool value)
{
    *state_ptr = value;
    config_flag_set(flag_name, value);
}

void network_execServiceState(const char *service_name, bool background)
{
    char state[256];
    char command[512];

    sync();

    sprintf(state, NET_SCRIPT_PATH "/update_networking.sh %s toggle", service_name);
    sprintf(command, "%s 2>&1", state);
    if (background)
        strcat(command, " &");
    system(command);

    printf_debug("network_execServiceState: %s\n", state);
}

void network_execServiceAuth(const char *service_name)
{
    char authed[256];
    char command[512];

    sync();

    sprintf(authed, NET_SCRIPT_PATH "/update_networking.sh %s authed", service_name);
    sprintf(command, "%s 2>&1", authed);

    system(command);

    printf_debug("network_execServiceAuth: %s\n", authed);
}

void network_commonEnableToggle(List *list, ListItem *item, bool *value_pt, const char *service_name, const char *service_flag)
{
    bool enabled = item->value == 1;
    network_setState(value_pt, service_flag, enabled);
    if (_menu_network._created) {
        list_currentItem(&_menu_network)->value = enabled;
    }
    network_execServiceState(service_name, false);
    reset_menus = true;
    all_changed = true;
}

void network_setSmbdState(void *pt)
{
    network_commonEnableToggle(&_menu_smbd, (ListItem *)pt, &network_state.smbd, "smbd", ".smbdState");
}

void network_setHttpState(void *pt)
{
    network_commonEnableToggle(&_menu_http, (ListItem *)pt, &network_state.http, "http", ".httpState");
}

void network_setSshState(void *pt)
{
    network_commonEnableToggle(&_menu_ssh, (ListItem *)pt, &network_state.ssh, "ssh", ".sshState");
}

void network_setFtpState(void *pt)
{
    network_commonEnableToggle(&_menu_ftp, (ListItem *)pt, &network_state.ftp, "ftp", ".ftpState");
}

void network_setTelnetState(void *pt)
{
    network_commonEnableToggle(&_menu_telnet, (ListItem *)pt, &network_state.telnet, "telnet", ".telnetState");
}

void network_setHotspotState(void *pt)
{
    network_setState(&network_state.hotspot, ".hotspotState", ((ListItem *)pt)->value);
    network_execServiceState("hotspot", false);
    reset_menus = true;
    all_changed = true;
}

void network_setNtpState(void *pt)
{
    network_setState(&network_state.ntp, ".ntpState", ((ListItem *)pt)->value);
    temp_flag_set("ntp_synced", false);
    network_execServiceState("ntp", true);
    reset_menus = true;
    all_changed = true;
}

void network_setNtpForceState(void *pt)
{
    network_setState(&network_state.force_wifi_on_startup, ".ntpForce", ((ListItem *)pt)->value);
}

void network_setNtpWaitState(void *pt)
{
    network_setState(&network_state.ntp_wait, ".ntpWait", ((ListItem *)pt)->value);
}

void network_setCheckUpdates(void *pt)
{
    network_setState(&network_state.check_updates, ".checkUpdates", ((ListItem *)pt)->value);
}

void network_keepServicesAlive(void *pt)
{
    network_setState(&network_state.keep_alive, ".keepServicesAlive", !((ListItem *)pt)->value);
}

void network_setFtpAuthState(void *pt)
{
    network_setState(&network_state.auth_ftp, ".authftpState", ((ListItem *)pt)->value);
    network_execServiceAuth("ftp");
}

void network_setHttpAuthState(void *pt)
{
    network_setState(&network_state.auth_http, ".authhttpState", ((ListItem *)pt)->value);
    network_execServiceAuth("http");
}

void network_setSshAuthState(void *pt)
{
    network_setState(&network_state.auth_ssh, ".authsshState", ((ListItem *)pt)->value);
    network_execServiceAuth("ssh");
}

void network_wpsConnect(void *pt)
{
    system("sh " NET_SCRIPT_PATH "/wpsclient.sh");
}

void network_setTzManualState(void *pt)
{
    bool enabled = ((ListItem *)pt)->value;
    network_setState(&network_state.manual_tz, ".manual_tz", !enabled);
    if (enabled) {
        char utc_str[10];
        if (config_get(".tz_sync", CONFIG_STR, utc_str)) {
            setenv("TZ", utc_str, 1);
            tzset();
            config_setString(".tz", utc_str);
        }
        else {
            temp_flag_set("ntp_synced", false);
        }
    }
    reset_menus = true;
    all_changed = true;
}

void network_setTzSelectState(void *pt)
{
    char utc_str[10];
    int select_value = ((ListItem *)pt)->value;
    double utc_value = ((double)select_value / 2.0) - 12.0;
    bool half_past = round(utc_value) != utc_value;

    if (utc_value == 0.0) {
        strcpy(utc_str, "UTC");
    }
    else {
        // UTC +/- is reversed for export TZ
        sprintf(utc_str, utc_value > 0 ? "UTC-%02d:%02d" : "UTC+%02d:%02d", (int)floor(abs(utc_value)), half_past ? 30 : 0);
    }

    printf_debug("Set timezone: %s\n", utc_str);

    setenv("TZ", utc_str, 1);
    tzset();
    config_setString(".tz", utc_str);
}

void network_toggleVNC(void *pt)
{
    char command_start[STR_MAX];
    char command_stop[STR_MAX];

    int new_fps = (int)network_state.vncfps;

    sprintf(command_start, "/mnt/SDCARD/.tmp_update/bin/vncserver -k /dev/input/event0 -F %d -r 180 > /dev/null 2>&1 &", new_fps);
    sprintf(command_stop, "killall -9 vncserver");

    if (!network_state.vncserv) {
        network_state.vncserv = true;
        network_setState(&network_state.vncserv, ".vncServer", true);
        reset_menus = true;
        if (!process_isRunning("vncserver")) {
            system(command_start);
        }
    }
    else {
        network_state.vncserv = false;
        network_setState(&network_state.vncserv, ".vncServer", false);
        reset_menus = true;
        if (process_isRunning("vncserver")) {
            system(command_stop);
        }
    }
}

void network_setVNCFPS(void *pt)
{
    network_state.vncfps = ((ListItem *)pt)->value;
    config_setNumber(".vncfps", network_state.vncfps);

    if (network_state.vncserv) {
        network_toggleVNC(pt);
        network_state.vncserv = false;
        network_setState(&network_state.vncserv, ".vncServer", false);
        reset_menus = true;
    }
}

void menu_smbd(void *pt)
{
    ListItem *item = (ListItem *)pt;
    item->value = (int)network_state.smbd;

    if (!_menu_smbd._created) {
        network_getSmbShares();

        _menu_smbd = list_createWithSticky(1 + network_numShares, "Samba");

        list_addItemWithInfoNote(&_menu_smbd,
                                 (ListItem){
                                     .label = "Enable",
                                     .sticky_note = "Enable Samba file sharing",
                                     .item_type = TOGGLE,
                                     .value = (int)network_state.smbd,
                                     .action = network_setSmbdState},
                                 item->info_note);

        for (int i = 0; i < network_numShares; i++) {
            ListItem shareItem = {
                .item_type = TOGGLE,
                .disabled = !network_state.smbd,
                .action = network_toggleSmbAvailable, // set the action to the wrapper function
                .value = _network_shares[i].available,
                .payload_ptr = _network_shares + i // store a pointer to the share in the payload
            };
            snprintf(shareItem.label, STR_MAX - 1, "Share: %s", _network_shares[i].name);
            strncpy(shareItem.sticky_note, str_replace(_network_shares[i].path, "/mnt/SDCARD", "SD:"), STR_MAX - 1);
            list_addItem(&_menu_smbd, shareItem);
        }
    }

    menu_stack[++menu_level] = &_menu_smbd;
    header_changed = true;
}

void menu_http(void *pt)
{
    ListItem *item = (ListItem *)pt;
    item->value = (int)network_state.http;
    if (!_menu_http._created) {
        _menu_http = list_create(2, LIST_SMALL);
        strcpy(_menu_http.title, "HTTP");
        list_addItemWithInfoNote(&_menu_http,
                                 (ListItem){
                                     .label = "Enable",
                                     .item_type = TOGGLE,
                                     .value = (int)network_state.http,
                                     .action = network_setHttpState},
                                 item->info_note);
        list_addItemWithInfoNote(&_menu_http,
                                 (ListItem){
                                     .label = "Enable authentication",
                                     .item_type = TOGGLE,
                                     .disabled = !network_state.http,
                                     .value = (int)network_state.auth_http,
                                     .action = network_setHttpAuthState},
                                 "Username: admin\n"
                                 "Password: admin\n"
                                 " \n"
                                 "It's recommended you change this\n"
                                 "at first login.");
    }
    menu_stack[++menu_level] = &_menu_http;
    header_changed = true;
}

void menu_ftp(void *pt)
{
    ListItem *item = (ListItem *)pt;
    item->value = (int)network_state.ftp;
    if (!_menu_ftp._created) {
        _menu_ftp = list_create(2, LIST_SMALL);
        strcpy(_menu_ftp.title, "FTP");
        list_addItemWithInfoNote(&_menu_ftp,
                                 (ListItem){
                                     .label = "Enable",
                                     .item_type = TOGGLE,
                                     .value = (int)network_state.ftp,
                                     .action = network_setFtpState},
                                 item->info_note);
        list_addItemWithInfoNote(&_menu_ftp,
                                 (ListItem){
                                     .label = "Enable authentication",
                                     .item_type = TOGGLE,
                                     .disabled = !network_state.ftp,
                                     .value = (int)network_state.auth_ftp,
                                     .action = network_setFtpAuthState},
                                 "Username: onion\n"
                                 "Password: onion\n"
                                 " \n"
                                 "We're using a new auth system. User defined\n"
                                 "passwords will come in a future update.");
    }
    menu_stack[++menu_level] = &_menu_ftp;
    header_changed = true;
}

void menu_wps(void *_)
{
    if (!_menu_wps._created) {
        _menu_wps = list_create(1, LIST_SMALL);
        strcpy(_menu_wps.title, "WPS");
        list_addItem(&_menu_wps,
                     (ListItem){
                         .label = "WPS connect",
                         .action = network_wpsConnect});
    }
    menu_stack[++menu_level] = &_menu_wps;
    header_changed = true;
}

void menu_ssh(void *pt)
{
    ListItem *item = (ListItem *)pt;
    item->value = (int)network_state.ssh;
    if (!_menu_ssh._created) {
        _menu_ssh = list_create(2, LIST_SMALL);
        strcpy(_menu_ssh.title, "SSH");
        list_addItemWithInfoNote(&_menu_ssh,
                                 (ListItem){
                                     .label = "Enable",
                                     .item_type = TOGGLE,
                                     .value = (int)network_state.ssh,
                                     .action = network_setSshState},
                                 item->info_note);
        list_addItemWithInfoNote(&_menu_ssh,
                                 (ListItem){
                                     .label = "Enable authentication",
                                     .item_type = TOGGLE,
                                     .disabled = !network_state.ssh,
                                     .value = (int)network_state.auth_ssh,
                                     .action = network_setSshAuthState},
                                 "Username: onion\n"
                                 "Password: onion\n"
                                 " \n"
                                 "We're using a new auth system. User defined\n"
                                 "passwords will come in a future update.");
    }
    menu_stack[++menu_level] = &_menu_ssh;
    header_changed = true;
}

void menu_vnc(void *pt)
{
    ListItem *item = (ListItem *)pt;
    item->value = (int)network_state.vncserv;
    if (!_menu_vnc._created) {
        _menu_vnc = list_create(2, LIST_SMALL);
        strcpy(_menu_vnc.title, "VNC");
        list_addItem(&_menu_vnc,
                     (ListItem){
                         .label = "Enable",
                         .item_type = TOGGLE,
                         .value = (int)network_state.vncserv,
                         .action = network_toggleVNC});
        list_addItemWithInfoNote(&_menu_vnc,
                                 (ListItem){
                                     .label = "Framerate",
                                     .item_type = MULTIVALUE,
                                     .value_max = 20,
                                     .value_min = 1,
                                     .value = (int)network_state.vncfps,
                                     .action = network_setVNCFPS},
                                 "Set the framerate of the VNC server\n"
                                 "between 1 and 20. The higher the \n"
                                 "framerate the more CPU it will use \n");
    }
    menu_stack[++menu_level] = &_menu_vnc;
    header_changed = true;
}

void network_applySavedNetworks(void *pt)
{
    (void)pt;

    if (
        !showConfirmDialog(
            "Apply saved networks?",
            "This makes your saved networks\n"
            "the active WiFi configuration.\n"
            "\n"
            "Only networks saved here will remain\n"
            "after applying this list.")) {
        return;
    }

    if (apply_saved_networks()) {
        __showInfoDialog(
            "Saved networks applied",
            "Your saved networks are now active.");
    }
    else {
        __showInfoDialog(
            "Apply failed",
            "Saved networks could not be applied.");
    }

    list_changed = true;
}

void network_saveCurrentNetwork(void *pt)
{
    (void)pt;

    char ssid[STR_MAX] = {0};
    char message[STR_MAX];

    SaveCurrentNetworkResult result =
        save_current_network(
            ssid,
            sizeof(ssid));

    if (
        result ==
        SAVE_CURRENT_NETWORK_SUCCESS) {
        snprintf(
            message,
            sizeof(message),
            "\"%.*s\" was added to your saved networks.",
            200,
            ssid);

        __showInfoDialog(
            "Network saved",
            message);
    }
    else if (
        result ==
        SAVE_CURRENT_NETWORK_ALREADY_SAVED) {
        snprintf(
            message,
            sizeof(message),
            "\"%.*s\" is already in your saved networks.",
            200,
            ssid);

        __showInfoDialog(
            "Nothing changed",
            message);
    }
    else if (
        result ==
        SAVE_CURRENT_NETWORK_NOT_CONNECTED) {
        __showInfoDialog(
            "No network connected",
            "Connect to a network before saving it.");
    }
    else if (
        result ==
        SAVE_CURRENT_NETWORK_AMBIGUOUS) {
        __showInfoDialog(
            "Multiple matching profiles",
            "More than one active profile uses this SSID.\n"
            "The connected profile cannot be identified safely.");
    }
    else {
        __showInfoDialog(
            "Save failed",
            "The current network could not be saved.");
    }

    list_changed = true;
}

static bool network_editPriorityDialog(
    const char *ssid,
    int current_priority,
    int *new_priority)
{
    bool confirmed = false;
    bool dialog_closed = false;
    SDLKey changed_key = SDLK_UNKNOWN;

    int edited_priority =
        current_priority;

    char message[STR_MAX];

    SDL_Surface *background =
        SDL_CreateRGBSurface(
            SDL_SWSURFACE,
            screen->w,
            screen->h,
            32,
            0,
            0,
            0,
            0);

    if (background == NULL) {
        return false;
    }

    SDL_BlitSurface(
        screen,
        NULL,
        background,
        NULL);

    keys_enabled = false;

    while (!dialog_closed) {
        SDL_BlitSurface(
            background,
            NULL,
            screen,
            NULL);

        snprintf(
            message,
            sizeof(message),
            "Priority: %d\n"
            "Higher numbers connect first\n"
            "\n"
            "Left / Right to adjust",
            edited_priority);

        theme_renderDialog(
            screen,
            ssid,
            message,
            true);

        SDL_BlitSurface(
            screen,
            NULL,
            video,
            NULL);

        SDL_Flip(video);

        if (
            updateKeystate(
                keystate,
                &dialog_closed,
                true,
                &changed_key)) {
            if (
                keystate[SW_BTN_LEFT] >=
                PRESSED) {
                edited_priority -= 10;

                if (
                    edited_priority <
                    MIN_NETWORK_PRIORITY) {
                    edited_priority =
                        MIN_NETWORK_PRIORITY;
                }

                sound_change();
            }
            else if (
                keystate[SW_BTN_RIGHT] >=
                PRESSED) {
                edited_priority += 10;

                if (
                    edited_priority >
                    MAX_NETWORK_PRIORITY) {
                    edited_priority =
                        MAX_NETWORK_PRIORITY;
                }

                sound_change();
            }
            else if (
                changed_key == SW_BTN_A &&
                keystate[SW_BTN_A] == PRESSED) {
                confirmed = true;
                dialog_closed = true;
            }
            else if (
                changed_key == SW_BTN_B &&
                keystate[SW_BTN_B] == PRESSED) {
                dialog_closed = true;
            }
        }

        SDL_Delay(8);
    }

    keys_enabled = true;

    SDL_BlitSurface(
        background,
        NULL,
        screen,
        NULL);

    SDL_FreeSurface(background);

    if (
        confirmed &&
        new_priority != NULL) {
        *new_priority =
            edited_priority;
    }

    all_changed = true;
    list_changed = true;

    return confirmed;
}

static bool network_confirmRemoveSavedNetwork(const char *ssid)
{
    char message[STR_MAX];

    snprintf(
        message,
        sizeof(message),
        "Remove \"%.*s\" from your\n"
        "saved network list?",
        120,
        ssid);

    return showConfirmDialog(
        "Remove saved network?",
        message);
}

static void network_refreshPriorityDescriptions(
    int priority)
{
    snprintf(
        _menu_wifi_network_actions
            .items[0]
            .description,
        STR_MAX,
        "Current priority: %d | Higher connects first",
        priority);

    if (
        wifi_selected_network_index >= 0 &&
        wifi_selected_network_index <
            _menu_wifi_saved_networks.item_count) {
        snprintf(
            _menu_wifi_saved_networks
                .items[wifi_selected_network_index]
                .description,
            STR_MAX,
            "Connection priority: %d",
            priority);
    }
}

void network_changeSelectedPriority(void *pt)
{
    (void)pt;

    int current_priority = 0;
    int new_priority = 0;

    if (
        !get_saved_network_priority(
            wifi_selected_network_index,
            &current_priority)) {
        __showInfoDialog(
            "Priority unavailable",
            "The current priority could not be read.");

        list_changed = true;
        return;
    }

    if (
        !network_editPriorityDialog(
            wifi_selected_network_ssid,
            current_priority,
            &new_priority)) {
        return;
    }

    if (new_priority == current_priority) {
        __showInfoDialog(
            "Nothing changed",
            "The network priority was not changed.");

        return;
    }

    if (
        !update_saved_network_priority(
            wifi_selected_network_index,
            new_priority)) {
        __showInfoDialog(
            "Priority update failed",
            "The saved network priority could not be updated.");

        list_changed = true;
        return;
    }

    network_refreshPriorityDescriptions(
        new_priority);

    __showInfoDialog(
        "Priority updated",
        "Apply saved networks to use the new priority.");

    list_changed = true;
}

void network_removeSelectedNetwork(void *pt)
{
    (void)pt;

    if (
        !network_confirmRemoveSavedNetwork(
            wifi_selected_network_ssid)) {
        return;
    }

    if (
        !delete_saved_network(
            wifi_selected_network_index)) {
        __showInfoDialog(
            "Remove failed",
            "The saved network could not be removed.");

        list_changed = true;
        return;
    }

    wifi_selected_network_ssid[0] = '\0';
    wifi_selected_network_index = -1;

    __showInfoDialog(
        "Network removed",
        "Apply saved networks to use the revised list.");

    /*
     * Replace the action submenu with a newly built saved-network submenu.
     * The current stack is:
     * network -> wi-fi -> saved networks -> network actions.
     */
    if (menu_level >= 2) {
        menu_level -= 2;
    }

    menu_wifiSavedNetworks(NULL);

    all_changed = true;
    list_changed = true;
}

void menu_wifiNetworkActions(void *pt)
{
    ListItem *selected_item =
        (ListItem *)pt;

    if (selected_item == NULL) {
        return;
    }

    char *end_pointer = NULL;

    long parsed_index = strtol(
        selected_item->payload,
        &end_pointer,
        10);

    if (
        end_pointer == selected_item->payload ||
        *end_pointer != '\0' ||
        parsed_index < 0 ||
        parsed_index > INT_MAX) {
        __showInfoDialog(
            "Network unavailable",
            "The selected network could not be opened.");

        list_changed = true;
        return;
    }

    WifiConfig *saved_config =
        calloc(1, sizeof(WifiConfig));

    if (
        saved_config == NULL ||
        !load_wifi_config(
            SAVED_CONFIG_PATH,
            saved_config) ||
        parsed_index >= saved_config->network_count) {
        free(saved_config);

        __showInfoDialog(
            "Network unavailable",
            "The selected network could not be opened.");

        list_changed = true;
        return;
    }

    wifi_selected_network_index =
        (int)parsed_index;

    const WifiNetwork *network =
        &saved_config->networks[wifi_selected_network_index];

    strncpy(
        wifi_selected_network_ssid,
        network->ssid,
        sizeof(wifi_selected_network_ssid) - 1);

    wifi_selected_network_ssid[sizeof(wifi_selected_network_ssid) - 1] = '\0';

    int current_priority =
        network->has_priority
            ? network->priority
            : 0;

    free(saved_config);

    list_free(&_menu_wifi_network_actions);

    _menu_wifi_network_actions =
        list_create(
            3,
            LIST_SMALL);

    strncpy(
        _menu_wifi_network_actions.title,
        wifi_selected_network_ssid,
        STR_MAX - 1);

    _menu_wifi_network_actions.title[STR_MAX - 1] = '\0';

    char priority_info[STR_MAX];

    snprintf(
        priority_info,
        sizeof(priority_info),
        "Current priority: %d | Higher connects first",
        current_priority);

    list_addItem(
        &_menu_wifi_network_actions,
        (ListItem){
            .label = "Higher priority connects first",
            .disabled = true,
            .action = NULL});

    list_addItemWithInfoNote(
        &_menu_wifi_network_actions,
        (ListItem){
            .label = "Connection priority",
            .action = network_changeSelectedPriority},
        priority_info);

    list_addItemWithInfoNote(
        &_menu_wifi_network_actions,
        (ListItem){
            .label = "Remove network",
            .action = network_removeSelectedNetwork},
        "Remove this profile from your saved\n"
        "network list.");

    menu_stack[++menu_level] =
        &_menu_wifi_network_actions;

    header_changed = true;
    list_changed = true;
}

void menu_wifiSavedNetworks(void *pt)
{
    (void)pt;

    WifiConfig *saved_config =
        calloc(1, sizeof(WifiConfig));

    char connected_ssid[STR_MAX] = {0};

    get_connected_network_ssid(
        connected_ssid,
        sizeof(connected_ssid));

    list_free(
        &_menu_wifi_saved_networks);

    if (saved_config == NULL) {
        _menu_wifi_saved_networks =
            list_create(
                1,
                LIST_LARGE);

        strcpy(
            _menu_wifi_saved_networks.title,
            "Saved networks");

        list_addItem(
            &_menu_wifi_saved_networks,
            (ListItem){
                .label =
                    "Unable to load networks",
                .description =
                    "Insufficient memory",
                .disabled = true,
                .action = NULL});

        menu_stack[++menu_level] =
            &_menu_wifi_saved_networks;

        header_changed = true;
        list_changed = true;
        return;
    }

    bool config_loaded =
        load_wifi_config(
            SAVED_CONFIG_PATH,
            saved_config);

    int item_count =
        config_loaded &&
                saved_config->network_count > 0
            ? saved_config->network_count
            : 1;

    _menu_wifi_saved_networks =
        list_create(
            item_count,
            LIST_LARGE);

    strcpy(
        _menu_wifi_saved_networks.title,
        "Saved networks");

    if (!config_loaded) {
        ListItem status_item = {
            .disabled = true,
            .action = NULL};

        if (
            access(
                SAVED_CONFIG_PATH,
                F_OK) == 0) {
            strncpy(
                status_item.label,
                "Saved list could not be read",
                STR_MAX - 1);

            strncpy(
                status_item.description,
                "The saved configuration is invalid",
                STR_MAX - 1);
        }
        else {
            strncpy(
                status_item.label,
                "No saved networks",
                STR_MAX - 1);

            strncpy(
                status_item.description,
                "Save the current network first",
                STR_MAX - 1);
        }

        status_item.label[STR_MAX - 1] = '\0';

        status_item.description[STR_MAX - 1] = '\0';

        list_addItem(
            &_menu_wifi_saved_networks,
            status_item);
    }
    else if (
        saved_config->network_count == 0) {
        list_addItem(
            &_menu_wifi_saved_networks,
            (ListItem){
                .label = "No saved networks",
                .description =
                    "Save the current network first",
                .disabled = true,
                .action = NULL});
    }
    else {
        for (
            int network_index = 0;
            network_index <
            saved_config->network_count;
            network_index++) {
            const WifiNetwork *network =
                &saved_config->networks[network_index];

            ListItem network_item = {0};

            int priority =
                network->has_priority
                    ? network->priority
                    : 0;

            bool is_connected =
                connected_ssid[0] != '\0' &&
                strcmp(
                    connected_ssid,
                    network->ssid) == 0 &&
                count_networks_with_ssid(
                    saved_config,
                    connected_ssid) == 1;

            strncpy(
                network_item.label,
                network->ssid,
                STR_MAX - 1);

            network_item.label[STR_MAX - 1] = '\0';

            snprintf(
                network_item.description,
                sizeof(
                    network_item.description),
                is_connected
                    ? "Priority %d | Connected"
                    : "Priority %d",
                priority);

            snprintf(
                network_item.payload,
                sizeof(network_item.payload),
                "%d",
                network_index);

            network_item.action =
                menu_wifiNetworkActions;

            list_addItem(
                &_menu_wifi_saved_networks,
                network_item);
        }
    }

    free(saved_config);

    menu_stack[++menu_level] =
        &_menu_wifi_saved_networks;

    header_changed = true;
    list_changed = true;
}

void menu_wifi(void *_)
{
    if (!_menu_wifi._created) {
        _menu_wifi = list_create(6, LIST_SMALL);
        strcpy(_menu_wifi.title, "WiFi");
        list_addItem(&_menu_wifi,
                     (ListItem){
                         .label = "IP address: N/A",
                         .disabled = true,
                         .action = NULL});
        list_addItemWithInfoNote(&_menu_wifi,
                                 (ListItem){
                                     .label = "Save current network",
                                     .action = network_saveCurrentNetwork},
                                 "Add the currently connected network to\n"
                                 "your persistent priority network list.");
        list_addItemWithInfoNote(&_menu_wifi,
                                 (ListItem){
                                     .label = "Apply saved networks",
                                     .action = network_applySavedNetworks},
                                 "Make your saved networks the active WiFi\n"
                                 "configuration and use their priorities.");
        list_addItemWithInfoNote(&_menu_wifi,
                                 (ListItem){
                                     .label = "Saved networks...",
                                     .action = menu_wifiSavedNetworks},
                                 "View saved networks and their current\n"
                                 "connection priorities.");
        list_addItemWithInfoNote(&_menu_wifi,
                                 (ListItem){
                                     .label = "WiFi Hotspot",
                                     .item_type = TOGGLE,
                                     .value = (int)network_state.hotspot,
                                     .action = network_setHotspotState},
                                 "Use hotspot to host all the network\n"
                                 "services on the go, no router needed.\n"
                                 "Stay connected at anytime, anywhere.\n"
                                 "Compatible with Easy Netplay and\n"
                                 "regular netplay.");
        list_addItemWithInfoNote(&_menu_wifi,
                                 (ListItem){
                                     .label = "WPS connect",
                                     .action = network_wpsConnect},
                                 "Use your WiFi router's WPS function\n"
                                 "to connect your device with a single press.\n"
                                 " \n"
                                 "First press the WPS button on your router,\n"
                                 "then click this option to connect.");
        // list_addItem(&_menu_wifi,
        //              (ListItem){
        //                  .label = "WPS...",
        //                  .action = menu_wps});
    }
    strcpy(_menu_wifi.items[0].label, ip_address_label);
    menu_stack[++menu_level] = &_menu_wifi;
    header_changed = true;
}

void menu_network(void *_)
{
    if (!_menu_network._created) {
        _menu_network = list_create(9, LIST_SMALL);
        strcpy(_menu_network.title, "Network");

        network_loadState();

        list_addItem(&_menu_network,
                     (ListItem){
                         .label = "IP address: N/A",
                         .disabled = true,
                         .action = NULL});
        list_addItem(&_menu_network,
                     (ListItem){
                         .label = "WiFi: Advanced...",
                         .action = menu_wifi});
        list_addItemWithInfoNote(&_menu_network,
                                 (ListItem){
                                     .label = "Samba: Network file share...",
                                     .item_type = TOGGLE,
                                     .disabled = !settings.wifi_on,
                                     .alternative_arrow_action = true,
                                     .arrow_action = network_setSmbdState,
                                     .value = (int)network_state.smbd,
                                     .action = menu_smbd},
                                 "Samba is a file sharing protocol that provides\n"
                                 "integrated sharing of files and directories\n"
                                 "between your Miyoo Mini Plus and your PC.\n"
                                 " \n"
                                 "Username: onion\n"
                                 "Password: onion\n");
        list_addItemWithInfoNote(&_menu_network,
                                 (ListItem){
                                     .label = "HTTP: Web-based file sync...",
                                     .item_type = TOGGLE,
                                     .disabled = !settings.wifi_on,
                                     .alternative_arrow_action = true,
                                     .arrow_action = network_setHttpState,
                                     .value = (int)network_state.http,
                                     .action = menu_http},
                                 "HTTP file server allows you to manage your\n"
                                 "files through a web browser on your phone,\n"
                                 "PC or tablet.\n"
                                 " \n"
                                 "Think of it as a website hosted by Onion,\n"
                                 "simply enter the IP address in your browser.");
        list_addItemWithInfoNote(&_menu_network,
                                 (ListItem){
                                     .label = "SSH: Secure shell...",
                                     .item_type = TOGGLE,
                                     .disabled = !settings.wifi_on,
                                     .alternative_arrow_action = true,
                                     .arrow_action = network_setSshState,
                                     .value = (int)network_state.ssh,
                                     .action = menu_ssh},
                                 "SSH provides a secure command line host\n"
                                 "for communicating with your device remotely.\n"
                                 " \n"
                                 "SFTP provides a secure file transfer protocol.");
        list_addItemWithInfoNote(&_menu_network,
                                 (ListItem){
                                     .label = "FTP: File server...",
                                     .item_type = TOGGLE,
                                     .disabled = !settings.wifi_on,
                                     .alternative_arrow_action = true,
                                     .arrow_action = network_setFtpState,
                                     .value = (int)network_state.ftp,
                                     .action = menu_ftp},
                                 "FTP provides a method of transferring files\n"
                                 "between Onion and a PC, phone, or tablet.\n"
                                 "You'll need an FTP client installed on the\n"
                                 "other device.");
        list_addItemWithInfoNote(&_menu_network,
                                 (ListItem){
                                     .label = "Telnet: Remote shell",
                                     .item_type = TOGGLE,
                                     .disabled = !settings.wifi_on,
                                     .value = (int)network_state.telnet,
                                     .action = network_setTelnetState},
                                 "Telnet provides unencrypted remote shell\n"
                                 "access to your device.");
        list_addItemWithInfoNote(&_menu_network,
                                 (ListItem){
                                     .label = "VNC: Screen share...",
                                     .item_type = TOGGLE,
                                     .disabled = !settings.wifi_on,
                                     .alternative_arrow_action = true,
                                     .arrow_action = network_toggleVNC,
                                     .value = (int)network_state.vncserv,
                                     .action = menu_vnc},
                                 "Connect to your MMP from another device\n"
                                 "to view the screen and interact with it.");
        list_addItemWithInfoNote(&_menu_network,
                                 (ListItem){
                                     .label = "Disable services in game",
                                     .item_type = TOGGLE,
                                     .value = !network_state.keep_alive,
                                     .action = network_keepServicesAlive},
                                 "Disable all network services (except WiFi)\n"
                                 "while playing games.\n"
                                 " \n"
                                 "This helps to conserve battery and\n"
                                 "to keep performance at a maximum.");
    }
    strcpy(_menu_network.items[0].label, ip_address_label);
    menu_stack[++menu_level] = &_menu_network;
    header_changed = true;
}

#endif // TWEAKS_NETWORK_H__
