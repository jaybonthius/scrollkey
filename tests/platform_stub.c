/* CLI test boundary: report settings instead of installing native input hooks. */
#include "platform.h"
#include <stdio.h>

int platform_run(const ScrollConfig *config)
{
    static const char *names[] = {
        "shift", "left_shift", "right_shift", "control", "left_control", "right_control",
        "alt", "left_alt", "right_alt", "meta", "left_meta", "right_meta"
    };
    printf("%s %.17g\n", names[config->modifier], config->speed_multiplier);
    return 0;
}
