#include "platform.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *stream)
{
    fputs("Usage: scrollkey MODIFIER [OPTIONS]\n"
          "\n"
          "Hold MODIFIER and move the mouse/trackball to scroll.\n"
          "MODIFIER: shift, control, alt, meta, or left_/right_ variants.\n"
          "Aliases: option=alt, command/win=meta, ctrl=control.\n"
          "\n"
          "  --speed-multiplier NUMBER             Default: 1.0; finite and > 0\n"
          "                                       1.0 matches DragScroll's 3x speed\n"
          "  --help                               Show this help\n"
          "\n"
          "Example: scrollkey left_shift --speed-multiplier 0.5\n"
          "Release the modifier to stop scrolling. Ctrl+C exits.\n", stream);
}

static bool parse_modifier(const char *text, ScrollModifier *modifier)
{
    static const struct { const char *name; ScrollModifier modifier; } names[] = {
        {"shift", SCROLL_MOD_SHIFT}, {"left_shift", SCROLL_MOD_LEFT_SHIFT}, {"right_shift", SCROLL_MOD_RIGHT_SHIFT},
        {"control", SCROLL_MOD_CONTROL}, {"left_control", SCROLL_MOD_LEFT_CONTROL}, {"right_control", SCROLL_MOD_RIGHT_CONTROL},
        {"ctrl", SCROLL_MOD_CONTROL}, {"left_ctrl", SCROLL_MOD_LEFT_CONTROL}, {"right_ctrl", SCROLL_MOD_RIGHT_CONTROL},
        {"alt", SCROLL_MOD_ALT}, {"left_alt", SCROLL_MOD_LEFT_ALT}, {"right_alt", SCROLL_MOD_RIGHT_ALT},
        {"option", SCROLL_MOD_ALT}, {"left_option", SCROLL_MOD_LEFT_ALT}, {"right_option", SCROLL_MOD_RIGHT_ALT},
        {"meta", SCROLL_MOD_META}, {"left_meta", SCROLL_MOD_LEFT_META}, {"right_meta", SCROLL_MOD_RIGHT_META},
        {"command", SCROLL_MOD_META}, {"left_command", SCROLL_MOD_LEFT_META}, {"right_command", SCROLL_MOD_RIGHT_META},
        {"win", SCROLL_MOD_META}, {"left_win", SCROLL_MOD_LEFT_META}, {"right_win", SCROLL_MOD_RIGHT_META}
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strcmp(text, names[i].name) == 0) {
            *modifier = names[i].modifier;
            return true;
        }
    return false;
}

static const char *option_value(int *index, int argc, char **argv, const char *name)
{
    if (*index >= argc || !argv[*index])
        return NULL;
    size_t length = strlen(name);
    const char *argument = argv[*index];
    if (strncmp(argument, name, length) != 0)
        return NULL;
    if (argument[length] == '=')
        return argument + length + 1;
    if (argument[length] == '\0' && *index + 1 < argc && argv[*index + 1])
        return argv[++*index];
    return NULL;
}

int main(int argc, char **argv)
{
    ScrollConfig config = {SCROLL_MOD_LEFT_SHIFT, 1.0};
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(stdout);
        return 0;
    }
    if (argc < 2 || !parse_modifier(argv[1], &config.modifier)) {
        fputs("scrollkey: specify a supported modifier key\n", stderr);
        usage(stderr);
        return 2;
    }
    for (int i = 2; i < argc; ++i) {
        const char *argument = argv[i];
        const char *legacy = "--momentum-scroll-enabled";
        size_t length = strlen(legacy);
        if (strncmp(argument, legacy, length) == 0 &&
            (argument[length] == '\0' || argument[length] == '=')) {
            fputs("scrollkey: direct scrolling has no software momentum; remove --momentum-scroll-enabled\n", stderr);
            return 2;
        }
        const char *value = option_value(&i, argc, argv, "--speed-multiplier");
        if (value) {
            char *end;
            errno = 0;
            double speed = strtod(value, &end);
            if (errno || end == value || *end || !isfinite(speed) || speed <= 0.0) {
                fputs("scrollkey: speed multiplier must be a finite number greater than zero\n", stderr);
                return 2;
            }
            config.speed_multiplier = speed;
            continue;
        }
        fprintf(stderr, "scrollkey: unknown option or missing value: %s\n", argv[i]);
        return 2;
    }
    return platform_run(&config);
}
