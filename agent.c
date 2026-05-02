#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <sys/wait.h>
#include <termios.h>
#include <poll.h>

#define NOB_IMPLEMENTATION
#include "nob.h"

#define BOARD_ROWS 10
#define BOARD_COLS 10
#define COL_WIDTH 3
#define BOARD_SIZE_IN_BYTES ((BOARD_COLS*COL_WIDTH)*BOARD_ROWS + BOARD_ROWS)

bool interactive = false;

bool is_board_char(char x)
{
    const char *board_chars = " .[]@%\n";
    return strchr(board_chars, x) || isdigit(x);
}

void parse_board(String_View sv, char *board, int *cur_row, int *cur_col)
{
    for (size_t row = 0; sv.count > 0 && row < BOARD_ROWS; ++row) {
        String_View line = sv_chop_by_delim(&sv, '\n');
        assert(line.count == BOARD_COLS*COL_WIDTH);
        for (size_t col = 0; col < BOARD_COLS; ++col) {
            char l = line.data[col*COL_WIDTH + 0];
            char c = line.data[col*COL_WIDTH + 1];
            char r = line.data[col*COL_WIDTH + 2];
            if (l == '[') {
                assert(r == ']');
                assert(*cur_row < 0);
                assert(*cur_col < 0);
                *cur_row = row;
                *cur_col = col;
            }
            board[row*BOARD_COLS + col] = c;
        }
    }
    assert(cur_row >= 0);
    assert(cur_col >= 0);
}

void trace_board(char *board, size_t cur_row, size_t cur_col)
{
    for (size_t row = 0; row < BOARD_ROWS; ++row) {
        for (size_t col = 0; col < BOARD_COLS; ++col) {
            bool cur = row == cur_row && col == cur_col;
            printf("%c", cur ? '[' : ' ');
            printf("%c", board[row*BOARD_COLS + col]);
            printf("%c", cur ? ']' : ' ');
        }
        printf("\n");
    }
    if (interactive) {
        usleep(5*1000);
    }
}

bool something_is(char *board, char what)
{
    for (size_t row = 0; row < BOARD_ROWS; ++row) {
        for (size_t col = 0; col < BOARD_COLS; ++col) {
            if (board[row*BOARD_COLS + col] == what) return true;
        }
    }
    return false;
}

bool everything_is(char *board, char what)
{
    for (size_t row = 0; row < BOARD_ROWS; ++row) {
        for (size_t col = 0; col < BOARD_COLS; ++col) {
            if (board[row*BOARD_COLS + col] != what) return false;
        }
    }
    return true;
}

static inline bool everything_is_closed(char *board)
{
    return everything_is(board, '.');
}

inline static bool something_is_closed(char *board)
{
    return something_is(board, '.');
}

void write_char(int fd, char cmd)
{
    ssize_t n = write(fd, &cmd, 1);
    if (n == 0) exit(0);
    if (n < 0) {
        fprintf(stderr, "ERROR: could not write into input of the child: %s\n", strerror(errno));
        abort();
    }
}

char read_char(int fd)
{
    char buf;
    ssize_t n = read(fd, &buf, 1);
    if (n == 0) exit(0);
    if (n < 0) {
        fprintf(stderr, "ERROR: could not read output of the child: %s\n", strerror(errno));
        abort();
    }
    return buf;
}

typedef enum {
    START,
    BOARD,
    TURN,
    REFRESH,
    WON,
    LOST,
} Harness_State;

const char *harness_state_name(Harness_State state)
{
    switch (state) {
    case START:   return "START";
    case BOARD:   return "BOARD";
    case TURN:    return "TURN";
    case REFRESH: return "REFRESH";
    case WON:     return "WON";
    case LOST:    return "LOST";
    default: UNREACHABLE("State");
    }
}

typedef enum {
    DECIDE,
    WALKING,
} Agent_State;

typedef struct {
    int row, col;
} Coord;

static inline Coord make_coord(int row, int col)
{
    Coord coord;
    coord.row = row;
    coord.col = col;
    return coord;
}

typedef struct {
    Coord *items;
    size_t count;
    size_t capacity;
} Coords;

bool check_prompt(int fd, char buf, const char *prompt)
{
    while (buf == *prompt) {
        prompt += 1;
        if (*prompt == 0) break;
        buf = read_char(fd);
    }
    return *prompt == 0;
}

void count_nbors(char *board, Coord coord, char kind, Coords *nbors)
{
    for (int dx = -1; dx <= 1; ++dx) {
        int col = coord.col + dx;
        if (!(0 <= col && col < BOARD_COLS)) continue;
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx == 0 && dy == 0) continue;
            int row = coord.row + dy;
            if (!(0 <= row && row < BOARD_ROWS)) continue;
            if (board[row*BOARD_COLS + col] == kind) {
                da_append(nbors, make_coord(row, col));
            }
        }
    }
}

void thinking(const char *fmt, ...) NOB_PRINTF_FORMAT(1, 2);
void thinking(const char *fmt, ...)
{
    if (!interactive) {
        printf("THINKING: ");
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
        printf("\n");
    }
}

typedef enum {
    OPEN,
    FLAG,
    HALT,
} Solver_Action;

#define da_random(da) (assert((da)->count > 0), (da)->items[rand()%(da)->count])

Solver_Action solver(char *board, Coord *coord)
{
    if (everything_is_closed(board)) {
        thinking("Everything is closed. This is the start of the game. Opening a random cell.");
        coord->row = rand()%BOARD_ROWS;
        coord->col = rand()%BOARD_COLS;
        return OPEN;
    }

    assert(something_is_closed(board));

    static Coords closed_nbors = {0};
    static Coords flagged_nbors = {0};

    for (int row = 0; row < BOARD_ROWS; ++row) {
        for (int col = 0; col < BOARD_COLS; ++col) {
            if (board[row*BOARD_COLS + col] == '@') {
                UNREACHABLE(temp_sprintf("A bomb slipped into the solver at row %d column %d", row, col));
            }
            if (isdigit(board[row*BOARD_COLS + col])) {
                closed_nbors.count = 0;
                flagged_nbors.count = 0;
                count_nbors(board, make_coord(row, col), '.', &closed_nbors);
                count_nbors(board, make_coord(row, col), '%', &flagged_nbors);
                size_t mine_count = board[row*BOARD_COLS + col] - '0';
                if (flagged_nbors.count > mine_count) {
                    UNREACHABLE(temp_sprintf("Overflagged! (flagged: %zu, mines: %zu)", flagged_nbors.count, mine_count));
                }
                mine_count -= flagged_nbors.count;

                if (mine_count == 0) {
                    if (closed_nbors.count > 0) {
                        *coord = da_random(&closed_nbors);
                        thinking("Found a good candidate to open at row %d, column %d", coord->row, coord->col);
                        return OPEN;
                    }
                } else if (mine_count == closed_nbors.count) {
                    *coord = da_random(&closed_nbors);
                    thinking("Found a good candidate to FLAG at row %d, column %d", coord->row, coord->col);
                    return FLAG;
                }
            }
        }
    }

    closed_nbors.count = 0;
    for (int row = 0; row < BOARD_ROWS; ++row) {
        for (int col = 0; col < BOARD_COLS; ++col) {
            if (board[row*BOARD_COLS + col] == '.') {
                da_append(&closed_nbors, make_coord(row, col));
            }
        }
    }

    *coord = da_random(&closed_nbors);
    thinking("Could not find any obvious candidates. Picking a random cell at row %d, column %d", coord->row, coord->col);
    return OPEN;
}

int main(int argc, char **argv)
{
    const char *program_name = shift(argv, argc);

    while (argc > 0) {
        if (strcmp(argv[0], "-i") == 0) {
            interactive = true;
            shift(argv, argc);
        } else {
            break;
        }
    }

    if (argc <= 0) {
        fprintf(stderr, "Usage: %s [OPTIONS] [COMMAND LINE...]\n", program_name);
        fprintf(stderr, "OPTIONS:\n");
        fprintf(stderr, "  -i\n");
        fprintf(stderr, "    Run the solver in interactive mode\n");
        fprintf(stderr, "ERROR: no command line is provided\n");
        return 1;
    }

    struct termios tattr, saved_tattr;
    if (interactive) {
        if (isatty(STDIN_FILENO)) {
            tcgetattr(STDIN_FILENO, &tattr);
            saved_tattr = tattr;
            tattr.c_lflag    &= (~(ICANON | ECHO));
            tattr.c_cc[VMIN]  = 1;
            tattr.c_cc[VTIME] = 0;
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &tattr);
        }
    }

    int input_pipe[2];
    if (pipe(input_pipe) < 0) {
        fprintf(stderr, "ERROR: could not create input pipe: %s\n", strerror(errno));
        return 1;
    }
    int input_pipe_read  = input_pipe[0];
    int input_pipe_write = input_pipe[1];

    int output_pipe[2];
    if (pipe(output_pipe) < 0) {
        fprintf(stderr, "ERROR: could not create output pipe: %s\n", strerror(errno));
        return 1;
    }
    int output_pipe_read  = output_pipe[0];
    int output_pipe_write = output_pipe[1];

    pid_t child = fork();
    if (child == 0) {
        if (dup2(input_pipe_read, STDIN_FILENO) < 0) {
            fprintf(stderr, "ERROR: Could not setup stdin for child process: %s\n", strerror(errno));
            exit(1);
        }
        close(input_pipe_write);

        if (dup2(output_pipe_write, STDOUT_FILENO) < 0) {
            fprintf(stderr, "ERROR: Could not setup stdout for child process: %s\n", strerror(errno));
            exit(1);
        }
        close(output_pipe_read);

        Cmd cmd = {0};
        while (argc > 0) {
            cmd_append(&cmd, shift(argv, argc));
        }
        cmd_append(&cmd, NULL);

        if (execvp(*cmd.items, (char *const*)cmd.items) < 0) {
            String_Builder sb = {0};
            cmd_render(cmd, &sb);
            sb_append_null(&sb);
            fprintf(stderr, "ERROR: could not start child process: %s: %s\n", sb.items, strerror(errno));
            exit(1);
        }
    }

    close(input_pipe_read);
    close(output_pipe_write);

    Harness_State harness = START;
    Agent_State agent = DECIDE;
    Coord agent_target;
    Solver_Action solver_action;

    String_Builder output = {0};
    for (;;) { // Agentic loop
        switch (harness) {
        case START: {
            char buf = read_char(output_pipe_read);
            if (is_board_char(buf)) {
                sb_append(&output, buf);
                harness = BOARD;
            } else {
                UNREACHABLE("START: does not look like a board");
            }
        } break;
        case BOARD: {
            if (output.count < BOARD_SIZE_IN_BYTES) {
                char buf = read_char(output_pipe_read);
                if (is_board_char(buf)) {
                    sb_append(&output, buf);
                    harness = BOARD;
                } else {
                    UNREACHABLE(temp_sprintf("BOARD: does not look like a board: %c", buf));
                }
            } else if (output.count == BOARD_SIZE_IN_BYTES) {
                harness = TURN;
            } else {
                assert(output.count > BOARD_SIZE_IN_BYTES);
                UNREACHABLE(temp_sprintf("BOARD: board too big (%zu, expected %d)", output.count, BOARD_SIZE_IN_BYTES));
            }
        } break;
        case TURN: {
            int cur_row = -1;
            int cur_col = -1;
            char board[BOARD_COLS*BOARD_ROWS] = {0};

            parse_board(sb_to_sv(output), board, &cur_row, &cur_col);
            output.count = 0;
            trace_board(board, cur_row, cur_col);

            if (something_is(board, '@')) {
                thinking("I see bombs. Looks like we died...");
                harness = LOST;
            } else if (something_is(board, '.')) {
                switch (agent) {
                case DECIDE:
                    solver_action = solver(board, &agent_target);
                    switch (solver_action) {
                        case OPEN: // fallthrough
                        case FLAG:
                            agent = WALKING;
                            break;
                        case HALT: goto over;
                        default: UNREACHABLE("Solver_Action");
                    }
                    // fallthrough
                case WALKING:
                    if (cur_row < agent_target.row) {
                        thinking("moving down");
                        write_char(input_pipe_write, 's');
                        agent = WALKING;
                    } else if (cur_row > agent_target.row) {
                        thinking("moving up");
                        write_char(input_pipe_write, 'w');
                        agent = WALKING;
                    } else if (cur_col < agent_target.col) {
                        thinking("moving right");
                        write_char(input_pipe_write, 'd');
                        agent = WALKING;
                    } else if (cur_col > agent_target.col) {
                        thinking("moving left");
                        write_char(input_pipe_write, 'a');
                        agent = WALKING;
                    } else {
                        switch (solver_action) {
                        case OPEN:
                            thinking("opening");
                            write_char(input_pipe_write, ' ');
                            agent = DECIDE;
                            break;
                        case FLAG:
                            thinking("flagging");
                            write_char(input_pipe_write, 'f');
                            agent = DECIDE;
                            break;
                        case HALT: UNREACHABLE("Halting");
                        default:   UNREACHABLE("Solver_Action");
                        }
                    }
                    harness = REFRESH;
                    break;
                    default: UNREACHABLE("Action_State");
                }
            } else {
                thinking("I see neither bombs nor closed cells. Looks like we won!");
                harness = WON;
            }
        } break;
        case REFRESH: {
            const char *reset_escape_sequence = "\x1b[10A\x1b[30D";

            char buf = read_char(output_pipe_read);
            if (!check_prompt(output_pipe_read, buf, reset_escape_sequence)) {
                UNREACHABLE("WAIT: weird reset sequence has been recieved");
            }

            if (interactive) {
                printf("%s", reset_escape_sequence);
            }

            harness = BOARD;
        } break;
        case LOST: {
            const char *you_died_restart = "You Died! Restart? [y/n] ";

            char buf = read_char(output_pipe_read);
            if (!check_prompt(output_pipe_read, buf, you_died_restart)) {
                UNREACHABLE("WAIT: weird you_died_restart sequence has been recieved");
            }

            printf("%s", you_died_restart);

            char default_choice = 'y';
            write_char(input_pipe_write, default_choice);
            if (read_char(output_pipe_read) != default_choice) {
                UNREACHABLE("WAIT: weird you_died_restart response has been recieved");
            }
            if (read_char(output_pipe_read) != '\n') {
                UNREACHABLE("WAIT: weird you_died_restart response has been recieved");
            }
            printf("%c\n", default_choice);

            harness = BOARD;
        } break;
        case WON: {
            const char *you_won_restart = "You Won! Restart? [y/n] ";

            char buf = read_char(output_pipe_read);
            if (!check_prompt(output_pipe_read, buf, you_won_restart)) {
                UNREACHABLE("WAIT: weird you_won_restart sequence has been recieved");
            }
            printf("%s", you_won_restart);

            char default_choice = 'n';
            write_char(input_pipe_write, default_choice);
            if (read_char(output_pipe_read) != default_choice) {
                UNREACHABLE("WAIT: weird you_won_restart response has been recieved");
            }
            if (read_char(output_pipe_read) != '\n') {
                UNREACHABLE("WAIT: weird you_won_restart response has been recieved");
            }
            printf("%c\n", default_choice);

            goto over;
        } break;
        default: UNREACHABLE("state");
        }
    } over:

    if (interactive) {
        if (isatty(STDIN_FILENO)) {
            tcsetattr(STDIN_FILENO, TCSANOW, &saved_tattr);
        }
    }

    return 0;
}
// TODO: maybe kill the child process and restore the terminal state somewhere in atexit
//   Sometimes when the solver hits an abort() the child process just turns into zombie.
//   I'm not sure if atexit is even triggered on abort(). I need to double check that.
