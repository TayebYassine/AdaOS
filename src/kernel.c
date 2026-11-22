#include "screen.h"
#include "shell.h"

/* Entry point of AdaOS */
void kernel_main(void) {
    screen_clear();

    shell_run();
}
