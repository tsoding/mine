#define NOB_IMPLEMENTATION
#include "nob.h"

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    Cmd cmd = {0};
    Procs procs = {0};
    bool run = false;

    const char *program_name = shift(argv, argc);
    while (argc > 0) {
        const char *arg = shift(argv, argc);
        if (strcmp(arg, "-run") == 0) {
            run = true;
        } else {
            nob_log(ERROR, "Unknown flag `%s`", arg);
            return 1;
        }
    }

    cmd_append(&cmd, "clang");
    cmd_append(&cmd, "-Wall");
    cmd_append(&cmd, "-Wextra");
    cmd_append(&cmd, "-ggdb");
    cmd_append(&cmd, "-o", "agent");
    cmd_append(&cmd, "agent.c");
    if (!cmd_run(&cmd, .async = &procs)) return 1;

    cmd_append(&cmd, "fpc");
    cmd_append(&cmd, "mine.pas");
    if (!cmd_run(&cmd, .async = &procs)) return 1;

    if (!procs_flush(&procs)) return 1;

    if (run) {
        cmd_append(&cmd, "./agent");
        cmd_append(&cmd, "./mine");
        if (!cmd_run(&cmd)) return 1;
    }

    return 0;
}
