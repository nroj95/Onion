#include "./playActivityUI.h"

static bool quit = false;

static void sigHandler(int sig)
{
    switch (sig) {
    case SIGINT:
    case SIGTERM:
        quit = true;
        break;
    default:
        break;
    }
}

static SDL_Surface *video;
static SDL_Surface *screen;

static SDL_Surface *background;

static TTF_Font *font40;
static TTF_Font *font30;
static TTF_Font *fontCJKRomName25;
static TTF_Font *font18;

static PlayActivities *play_activities;

static SDL_Color color_white = {255, 255, 255};
static SDL_Color color_purple = {136, 97, 252};
static SDL_Color color_grey = {117, 123, 156};
static SDL_Color color_lightgrey = {214, 223, 246};

static bool show_raw_names = false;

static Uint32 marquee_started_at = 0;
static Uint32 marquee_last_frame = 0;

static bool ensureIdentitySchema(void)
{
    play_activity_db_open();

    if (play_activity_db == NULL)
        return false;

    bool schema_ready =
        play_activity_identity_schema_ensure(play_activity_db);

    play_activity_db_close();

    return schema_ready;
}

static int renderText(
    const char *text,
    TTF_Font *font,
    SDL_Color color,
    SDL_Rect *rect
);

static void renderPage(
    int current_page,
    int current_index
);

static int maxInt(int left, int right)
{
    return left > right ? left : right;
}

static void fillCircle(
    SDL_Surface *surface,
    int center_x,
    int center_y,
    int radius,
    Uint32 color
)
{
    for (int y = -radius; y <= radius; y++) {
        int width = 0;

        while (width * width + y * y <= radius * radius)
            width++;

        SDL_Rect line = {
            center_x - width + 1,
            center_y + y,
            width * 2 - 1,
            1
        };

        SDL_FillRect(surface, &line, color);
    }
}

static void renderButtonHint(
    int x,
    const char *button,
    const char *label
)
{
    const int center_x = x + 16;
    const int center_y = 451;

    fillCircle(
        screen,
        center_x,
        center_y,
        16,
        SDL_MapRGB(
            screen->format,
            color_purple.r,
            color_purple.g,
            color_purple.b
        )
    );

    int button_width = 0;
    int button_height = 0;

    TTF_SizeUTF8(
        font18,
        button,
        &button_width,
        &button_height
    );

    renderText(
        button,
        font18,
        color_white,
        &(SDL_Rect){
            center_x - button_width / 2,
            center_y - button_height / 2 - 1,
            button_width,
            button_height
        }
    );

    renderText(
        label,
        font30,
        color_white,
        &(SDL_Rect){
            x + 38,
            430,
            150,
            44
        }
    );
}

static void renderFooter(void)
{
    SDL_Rect footer = {
        0,
        420,
        640,
        60
    };

    SDL_FillRect(
        screen,
        &footer,
        SDL_MapRGB(
            screen->format,
            34,
            35,
            45
        )
    );

    renderButtonHint(
        18,
        "Y",
        show_raw_names
            ? "CLEAN"
            : "RAW"
    );

    renderButtonHint(
        230,
        "X",
        "REMOVE"
    );

    renderButtonHint(
        480,
        "B",
        "EXIT"
    );
}

static void fitTextEllipsis(
    const char *text,
    TTF_Font *font,
    int max_width,
    char *output,
    size_t output_size
)
{
    if (output == NULL || output_size == 0)
        return;

    output[0] = '\0';

    if (text == NULL || font == NULL)
        return;

    snprintf(output, output_size, "%s", text);

    int width = 0;
    int height = 0;

    if (TTF_SizeUTF8(font, output, &width, &height) != 0 ||
        width <= max_width) {
        return;
    }

    const char *ellipsis = "...";
    size_t length = strlen(output);

    while (length > 0) {
        length--;

        while (length > 0 &&
               ((unsigned char)output[length] & 0xc0) == 0x80) {
            length--;
        }

        output[length] = '\0';

        if (length + strlen(ellipsis) + 1 > output_size)
            continue;

        snprintf(
            output + length,
            output_size - length,
            "%s",
            ellipsis
        );

        if (TTF_SizeUTF8(
                font,
                output,
                &width,
                &height
            ) == 0 &&
            width <= max_width) {
            return;
        }

        output[length] = '\0';
    }

    snprintf(output, output_size, "%s", ellipsis);
}

void init(void)
{
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    SDL_Init(SDL_INIT_VIDEO);
    SDL_ShowCursor(SDL_DISABLE);
    SDL_EnableKeyRepeat(300, 50);
    TTF_Init();

    video = SDL_SetVideoMode(640, 480, 32, SDL_HWSURFACE);
    screen = SDL_CreateRGBSurface(SDL_HWSURFACE, 640, 480, 32, 0, 0, 0, 0);

    background = IMG_Load("./res/background.png");

    font40 = TTF_OpenFont("/customer/app/Exo-2-Bold-Italic.ttf", 40);
    font30 = TTF_OpenFont("/customer/app/Exo-2-Bold-Italic.ttf", 30);
    fontCJKRomName25 = TTF_OpenFont("/customer/app/wqy-microhei.ttc", 25);
    font18 = TTF_OpenFont("/customer/app/wqy-microhei.ttc", 18);
}

void free_resources(void)
{
    TTF_CloseFont(font40);
    TTF_CloseFont(font30);
    TTF_CloseFont(fontCJKRomName25);
    TTF_CloseFont(font18);

    TTF_Quit();

    SDL_FreeSurface(background);

    SDL_FreeSurface(screen);
    SDL_FreeSurface(video);
    SDL_Quit();

    free_play_activities(play_activities);
}

int _renderText(const char *text, TTF_Font *font, SDL_Color color, SDL_Rect *rect, bool right_align)
{
    int text_width = 0;
    SDL_Surface *textSurface = TTF_RenderUTF8_Blended(font, text, color);
    if (textSurface != NULL) {
        text_width = textSurface->w;
        if (right_align)
            SDL_BlitSurface(textSurface, NULL, screen, &(SDL_Rect){rect->x - textSurface->w, rect->y, rect->w, rect->h});
        else
            SDL_BlitSurface(textSurface, NULL, screen, rect);
        SDL_FreeSurface(textSurface);
    }
    return text_width;
}

int renderText(const char *text, TTF_Font *font, SDL_Color color, SDL_Rect *rect)
{
    return _renderText(text, font, color, rect, false);
}

int renderTextAlignRight(const char *text, TTF_Font *font, SDL_Color color, SDL_Rect *rect)
{
    return _renderText(text, font, color, rect, true);
}


static bool renderSelectedRawTitle(
    int current_page,
    int current_index,
    bool present
)
{
    if (!show_raw_names ||
        play_activities == NULL ||
        play_activities->count == 0 ||
        current_index < 0 ||
        current_index >= play_activities->count) {
        return false;
    }

    int row = current_index - current_page * 4;

    if (row < 0 || row >= 4)
        return false;

    PlayActivity *entry =
        play_activities->play_activity[current_index];

    ROM *rom = entry->rom;

    if (rom == NULL ||
        rom->name == NULL ||
        rom->name[0] == '\0') {
        return false;
    }

    TTF_Font *title_font =
        includeCJK(rom->name)
            ? fontCJKRomName25
            : font30;

    SDL_Surface *title_surface =
        TTF_RenderUTF8_Blended(
            title_font,
            rom->name,
            color_white
        );

    if (title_surface == NULL)
        return false;

    const int title_width = 430;

    if (title_surface->w <= title_width) {
        SDL_FreeSurface(title_surface);
        return false;
    }

    int num_width = 50;

    if (current_page >= 2)
        num_width += 20;

    if (current_page >= 24)
        num_width += 20;

    SDL_Rect title_area = {
        num_width + 100,
        75 + 90 * row,
        title_width,
        40
    };

    SDL_Rect background_source = title_area;
    SDL_Rect background_target = title_area;

    SDL_BlitSurface(
        background,
        &background_source,
        screen,
        &background_target
    );

    Uint32 elapsed =
        SDL_GetTicks() - marquee_started_at;

    const Uint32 start_pause = 1000;
    const Uint32 end_pause = 3000;
    const Uint32 pixels_per_second = 30;

    int overflow =
        title_surface->w - title_width;

    Uint32 scroll_duration =
        ((Uint32)overflow * 1000) /
        pixels_per_second;

    Uint32 cycle_duration =
        start_pause +
        scroll_duration +
        end_pause;

    Uint32 phase =
        cycle_duration == 0
            ? 0
            : elapsed % cycle_duration;

    int offset = 0;

    if (phase > start_pause) {
        Uint32 scroll_phase =
            phase - start_pause;

        if (scroll_phase >= scroll_duration) {
            offset = overflow;
        }
        else {
            offset =
                (int)(
                    scroll_phase *
                    pixels_per_second /
                    1000
                );
        }
    }

    SDL_Rect source = {
        offset,
        0,
        title_width,
        title_surface->h
    };

    if (source.x + source.w > title_surface->w)
        source.w = title_surface->w - source.x;

    SDL_Rect target = {
        title_area.x,
        title_area.y,
        source.w,
        source.h
    };

    SDL_BlitSurface(
        title_surface,
        &source,
        screen,
        &target
    );

    SDL_FreeSurface(title_surface);

    if (present) {
        /*
         * The Miyoo video surface may use flipped hardware buffers.
         * Updating only the title rectangle can reveal stale content
         * from the other buffer, so present the complete composed frame.
         */
        SDL_BlitSurface(
            screen,
            NULL,
            video,
            NULL
        );

        SDL_Flip(video);
    }

    return true;
}

SDL_Surface *loadRomImage(const char *image_path)
{
    SDL_Surface *img = IMG_Load(is_file(image_path) ? image_path : "/mnt/SDCARD/miyoo/app/skin/thumb-default.png");

    double sw = (double)IMG_MAX_WIDTH / img->w;
    double sh = (double)IMG_MAX_HEIGHT / img->h;
    double s = MIN(sw, sh);

    SDL_PixelFormat *ft = img->format;
    SDL_Surface *dst = SDL_CreateRGBSurface(0, (int)(s * img->w), (int)(s * img->h), ft->BitsPerPixel, ft->Rmask, ft->Gmask, ft->Bmask, ft->Amask);

    SDL_Rect src_rect = {0, 0, img->w, img->h};
    SDL_Rect dst_rect = {0, 0, dst->w, dst->h};
    SDL_SoftStretch(img, &src_rect, dst, &dst_rect);

    SDL_FreeSurface(img);

    return dst;
}

static void drawSelectionBorder(int row)
{
    SDL_Rect top = {18, 68 + 90 * row, 604, 2};
    SDL_Rect bottom = {18, 150 + 90 * row, 604, 2};
    SDL_Rect left = {18, 68 + 90 * row, 2, 84};
    SDL_Rect right = {620, 68 + 90 * row, 2, 84};

    Uint32 color = SDL_MapRGB(screen->format, 136, 97, 252);

    SDL_FillRect(screen, &top, color);
    SDL_FillRect(screen, &bottom, color);
    SDL_FillRect(screen, &left, color);
    SDL_FillRect(screen, &right, color);
}

static void getRomSystem(
    const ROM *rom,
    char *system_name,
    size_t system_name_size
)
{
    if (system_name == NULL || system_name_size == 0)
        return;

    system_name[0] = '\0';

    if (rom == NULL ||
        rom->file_path == NULL ||
        rom->file_path[0] == '\0') {
        snprintf(
            system_name,
            system_name_size,
            "UNKNOWN"
        );
        return;
    }

    const char *system_start = rom->file_path;

    const char *absolute_prefix =
        "/mnt/SDCARD/Roms/";

    const char *relative_prefix =
        "../../Roms/";

    if (strncmp(
            system_start,
            absolute_prefix,
            strlen(absolute_prefix)
        ) == 0) {
        system_start += strlen(absolute_prefix);
    }
    else if (strncmp(
                 system_start,
                 relative_prefix,
                 strlen(relative_prefix)
             ) == 0) {
        system_start += strlen(relative_prefix);
    }

    while (*system_start == '/')
        system_start++;

    const char *separator = strchr(system_start, '/');

    size_t system_length =
        separator == NULL
            ? strlen(system_start)
            : (size_t)(separator - system_start);

    if (system_length == 0) {
        snprintf(
            system_name,
            system_name_size,
            "UNKNOWN"
        );
        return;
    }

    if (system_length >= system_name_size)
        system_length = system_name_size - 1;

    memcpy(
        system_name,
        system_start,
        system_length
    );

    system_name[system_length] = '\0';
}

static bool confirmRemoval(
    int current_page,
    int current_index
)
{
    renderPage(current_page, current_index);

    SDL_Rect overlay = {95, 125, 450, 230};
    SDL_Rect top = {95, 125, 450, 3};
    SDL_Rect bottom = {95, 352, 450, 3};
    SDL_Rect left = {95, 125, 3, 230};
    SDL_Rect right = {542, 125, 3, 230};

    Uint32 overlay_color =
        SDL_MapRGB(screen->format, 28, 28, 40);
    Uint32 border_color =
        SDL_MapRGB(screen->format, 136, 97, 252);

    SDL_FillRect(screen, &overlay, overlay_color);
    SDL_FillRect(screen, &top, border_color);
    SDL_FillRect(screen, &bottom, border_color);
    SDL_FillRect(screen, &left, border_color);
    SDL_FillRect(screen, &right, border_color);

    renderText(
        "REMOVE ACTIVITY?",
        font30,
        color_white,
        &(SDL_Rect){170, 155, 300, 40}
    );

    renderText(
        "This removes only the tracked history.",
        font18,
        color_lightgrey,
        &(SDL_Rect){145, 220, 360, 30}
    );

    renderText(
        "ROMs, saves, states and artwork stay untouched.",
        font18,
        color_lightgrey,
        &(SDL_Rect){115, 250, 420, 30}
    );

    renderText(
        "A  REMOVE",
        font18,
        color_purple,
        &(SDL_Rect){175, 310, 120, 30}
    );

    renderText(
        "B  CANCEL",
        font18,
        color_grey,
        &(SDL_Rect){350, 310, 120, 30}
    );

    SDL_BlitSurface(screen, NULL, video, NULL);
    SDL_Flip(video);

    bool dialog_quit = false;
    bool opener_released = false;
    KeyState dialog_keystate[320] = {(KeyState)0};

    /*
     * X opened this dialog. Treat it as held until its
     * KEYUP event arrives so that repeats cannot leak
     * into either the dialog or the main input loop.
     */
    dialog_keystate[SW_BTN_X] = PRESSED;

    while (!dialog_quit && !quit) {
        if (!updateKeystate(
                dialog_keystate,
                &quit,
                true,
                NULL
            )) {
            continue;
        }

        if (!opener_released) {
            if (dialog_keystate[SW_BTN_X] == RELEASED)
                opener_released = true;

            continue;
        }

        if (dialog_keystate[SW_BTN_A] == PRESSED)
            return true;

        if (dialog_keystate[SW_BTN_B] == PRESSED)
            return false;
    }

    return false;
}

void renderPage(
    int current_page,
    int current_index
)
{
    char num_str[12];
    char rom_name[STR_MAX];
    char system_name[STR_MAX];
    char total[25];
    char average[25];
    char plays[25];

    int num_width = 50;
    if (current_page >= 2)
        num_width += 20;
    if (current_page >= 24)
        num_width += 20;

    for (int row = 0; row < 4; row++) {
        int index = current_page * 4 + row;

        if (index >= play_activities->count)
            break;

        if (index == current_index)
            drawSelectionBorder(row);

        PlayActivity *entry =
            play_activities->play_activity[index];
        ROM *rom = entry->rom;

        sprintf(num_str, "%d", index + 1);
        renderTextAlignRight(
            num_str,
            font40,
            color_purple,
            &(SDL_Rect){
                num_width,
                80 + 90 * row,
                50,
                39
            }
        );

        SDL_Surface *romImage =
            loadRomImage(rom->image_path);
        SDL_Rect rectRomImage = {
            num_width + 10 + (80 - romImage->w) / 2,
            70 + 90 * row,
            80,
            80
        };
        SDL_BlitSurface(
            romImage,
            NULL,
            screen,
            &rectRomImage
        );
        SDL_FreeSurface(romImage);

        if (show_raw_names)
            strncpy(
                rom_name,
                rom->name,
                STR_MAX - 1
            );
        else
            file_cleanName(
                rom_name,
                rom->name
            );

        rom_name[STR_MAX - 1] = '\0';

        TTF_Font *title_font =
            includeCJK(rom_name)
                ? fontCJKRomName25
                : font30;

        char display_name[STR_MAX];

        fitTextEllipsis(
            rom_name,
            title_font,
            430,
            display_name,
            sizeof(display_name)
        );

        renderText(
            display_name,
            title_font,
            color_white,
            &(SDL_Rect){
                num_width + 100,
                75 + 90 * row,
                430,
                40
            }
        );

        getRomSystem(
            rom,
            system_name,
            sizeof(system_name)
        );

        str_serializeTime(
            total,
            entry->play_time_total
        );
        str_serializeTime(
            average,
            entry->play_time_average
        );
        snprintf(
            plays,
            sizeof(plays),
            "%d",
            entry->play_count
        );

        const char *details[] = {
            "TOTAL ",
            total,
            "  AVG ",
            average,
            "  PLAYS ",
            plays
        };

        SDL_Rect detailsRect = {
            num_width + 100,
            115 + 90 * row,
            430,
            40
        };

        for (int i = 0; i < 6; i++) {
            detailsRect.x += renderText(
                details[i],
                font18,
                i % 2 == 0
                    ? color_grey
                    : color_lightgrey,
                &detailsRect
            );
        }

        detailsRect.x += renderText(
            "  ·  ",
            font18,
            color_grey,
            &detailsRect
        );

        renderText(
            system_name,
            font18,
            color_purple,
            &detailsRect
        );
    }
}

int main(int argc, char *argv[])
{
    log_setName("playActivityUI");

    if (!ensureIdentitySchema()) {
        fprintf(
            stderr,
            "Error: unable to initialize activity tracker schema\n"
        );
        return EXIT_FAILURE;
    }

    init();

    SDL_Rect rectPages = {620, 430, 90, 44};
    SDL_Rect rectMileage = {484, 8, 170, 42};

    play_activities = play_activity_find_all();
    printf_debug(
        "found %d roms\n",
        play_activities->count
    );

    int current_index = 0;
    int current_page = 0;
    int num_pages = maxInt(
        1,
        (int)ceil(
            (double)play_activities->count /
            (double)4
        )
    );

    int play_time_total =
        play_activities->play_time_total;

    char play_time_total_formatted[STR_MAX];
    str_serializeTime(
        play_time_total_formatted,
        play_time_total
    );

    bool changed = true;
    KeyState keystate[320] = {(KeyState)0};

    marquee_started_at = SDL_GetTicks();
    marquee_last_frame = 0;

    while (!quit) {
        if (changed) {
            current_page =
                play_activities->count == 0
                    ? 0
                    : current_index / 4;

            SDL_BlitSurface(
                background,
                NULL,
                screen,
                NULL
            );

            char num_pages_str[25];
            snprintf(
                num_pages_str,
                sizeof(num_pages_str),
                "%d/%d",
                current_page + 1,
                num_pages
            );

            renderTextAlignRight(
                num_pages_str,
                font30,
                color_white,
                &rectPages
            );

            renderText(
                play_time_total_formatted,
                font30,
                color_white,
                &rectMileage
            );

            renderPage(
                current_page,
                current_index
            );

            renderSelectedRawTitle(
                current_page,
                current_index,
                false
            );

            renderFooter();

            SDL_BlitSurface(
                screen,
                NULL,
                video,
                NULL
            );
            SDL_Flip(video);

            changed = false;
        }

        Uint32 now = SDL_GetTicks();

        if (show_raw_names &&
            now - marquee_last_frame >= 50) {
            if (renderSelectedRawTitle(
                    current_page,
                    current_index,
                    true
                )) {
                marquee_last_frame = now;
            }
        }

        if (!updateKeystate(
                keystate,
                &quit,
                true,
                NULL
            )) {
            continue;
        }

        if (keystate[SW_BTN_B] == PRESSED) {
            quit = true;
            continue;
        }

        if (keystate[SW_BTN_DOWN] >= PRESSED &&
            current_index <
                play_activities->count - 1) {
            current_index++;
            marquee_started_at = SDL_GetTicks();
            marquee_last_frame = 0;
            changed = true;
        }

        if (keystate[SW_BTN_UP] >= PRESSED &&
            current_index > 0) {
            current_index--;
            marquee_started_at = SDL_GetTicks();
            marquee_last_frame = 0;
            changed = true;
        }

        if (keystate[SW_BTN_RIGHT] >= PRESSED &&
            current_page < num_pages - 1) {
            current_index += 4;

            if (current_index >=
                play_activities->count) {
                current_index =
                    play_activities->count - 1;
            }

            marquee_started_at = SDL_GetTicks();
            marquee_last_frame = 0;
            changed = true;
        }

        if (keystate[SW_BTN_LEFT] >= PRESSED &&
            current_page > 0) {
            current_index -= 4;

            if (current_index < 0)
                current_index = 0;

            marquee_started_at = SDL_GetTicks();
            marquee_last_frame = 0;
            changed = true;
        }

        if (keystate[SW_BTN_Y] == PRESSED) {
            show_raw_names = !show_raw_names;
            marquee_started_at = SDL_GetTicks();
            marquee_last_frame = 0;
            changed = true;
        }

        if (keystate[SW_BTN_X] == PRESSED &&
            play_activities->count > 0) {
            bool confirmed = confirmRemoval(
                current_page,
                current_index
            );

            memset(
                keystate,
                0,
                sizeof(keystate)
            );

            if (confirmed) {
                int rom_id =
                    play_activities
                        ->play_activity[current_index]
                        ->rom
                        ->id;

                if (play_activity_delete_rom(rom_id)) {
                    free_play_activities(
                        play_activities
                    );

                    play_activities =
                        play_activity_find_all();

                    if (current_index >=
                        play_activities->count) {
                        current_index =
                            play_activities->count - 1;
                    }

                    if (current_index < 0)
                        current_index = 0;

                    num_pages = maxInt(
                        1,
                        (int)ceil(
                            (double)
                                play_activities->count /
                            (double)4
                        )
                    );

                    play_time_total =
                        play_activities
                            ->play_time_total;

                    str_serializeTime(
                        play_time_total_formatted,
                        play_time_total
                    );
                }
                else {
                    fprintf(
                        stderr,
                        "Unable to remove activity entry\n"
                    );
                }
            }

            changed = true;
        }
    }

    free_resources();

    return EXIT_SUCCESS;
}
