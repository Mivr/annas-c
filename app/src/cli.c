#include "app/app.h"

/* STUB (test-author task A014). */

bool app_cli_parse(int argc, const char *const *argv, app_cmd_t *cmd, app_usage_error_t *err) {
    (void)argc;
    (void)argv;
    (void)cmd;
    (void)err;
    return false;
}

int app_cli_run(const app_cmd_t *cmd, const app_deps_t *deps, core_buf_t *out, core_buf_t *errout) {
    (void)cmd;
    (void)deps;
    (void)out;
    (void)errout;
    return 1;
}
