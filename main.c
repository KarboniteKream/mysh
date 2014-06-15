#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#define MAX_TOKENS 32
#define NUM_COMMANDS 21

char shell_name[64] = "mysh";
char cwd[256];

char *tokens[MAX_TOKENS], *pipe_commands[MAX_TOKENS] = { NULL };
int num_tokens = 0, num_pipe_commands, pipe_idx, pipe_fd[MAX_TOKENS][2];
int exit_status, exit_code = 0;
bool is_pipe = false;

int tokenize(char *line, int length);
void handle_sigchld();

void help();
void name();
void status();
void shell_exit();
void print();
void echo();
void pid();
void ppid();

void dir();
void dirwhere();
void dirmake();
void dirremove();
void dirlist();

void linkhard();
void linksoft();
void linkread();
void linklist();
void shell_unlink();
void shell_rename();
void cpcat();

void pipes();

struct COMMAND {
    char *name;
    char *description;
    void (*function)();
    bool background;
    bool change_status;
} commands[NUM_COMMANDS] = {
    {"help", "Print short help.", help, false, false},
    {"name", "Print or change shell name.", name, false, false},
    {"status", "Print last command status.", status, false, false},
    {"exit", "Exit from shell.", shell_exit, false, true},
    {"print", "Print arguments.", print, false, false},
    {"echo", "Print arguments with a newline.", echo, false, false},
    {"pid", "Print PID.", pid, false, false},
    {"ppid", "Print PPID.", ppid, false, false},
    {"dir", "Change directory.", dir, false, true},
    {"dirwhere", "Print current working directory.", dirwhere, true, false},
    {"dirmake", "Create a directory.", dirmake, true, true},
    {"dirremove", "Remove a directory.", dirremove, true, true},
    {"dirlist", "List directory contents.", dirlist, true, true},
    {"linkhard", "Create a hard link.", linkhard, true, true},
    {"linksoft", "Create a symbolic link.", linksoft, true, true},
    {"linkread", "Print symbolic link target.", linkread, true, true},
    {"linklist", "Print hard links to a file.", linklist, true, true},
    {"unlink", "Remove a file.", shell_unlink, true, true},
    {"rename", "Rename a file or a directory.", shell_rename, true, true},
    {"cpcat", "Copy a file.", cpcat, false, true},
    {"pipes", "Create a pipeline.", pipes, false, true}
};

bool redirect_in = false;
bool redirect_out = false;

int main() {
    char input[256];
    int i, length, idx, fd;

    int in = dup(STDIN_FILENO);
    int out = dup(STDOUT_FILENO);

    bool background = false;
    pid_t pid = 1;

    signal(SIGCHLD, handle_sigchld);
    getcwd(cwd, 256);

    while (true) {
        if (is_pipe == false && isatty(STDIN_FILENO) == 1) {
            printf("%s> ", shell_name);
            fflush(stdout);
        }

        if (is_pipe == false) {
            fgets(input, 256, stdin);
            length = strlen(input);
            input[--length] = '\0';

            if (feof(stdin) != 0) {
                break;
            }

            num_tokens = tokenize(input, length);
        } else {
            num_tokens = tokenize(pipe_commands[pipe_idx], strlen(pipe_commands[pipe_idx]));

            if (pipe_idx < num_pipe_commands - 1) {
                pipe(pipe_fd[pipe_idx]);
            }
        }

        if (num_tokens > 0) {
            if (tokens[num_tokens - 1][0] == '&') {
                background = true;
                num_tokens--;
            }

            if (tokens[num_tokens - 1][0] == '>') {
                fd = creat(tokens[--num_tokens] + 1, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

                if (fd == -1) {
                    perror("redirect");
                    exit_code = errno;
                } else {
                    redirect_out = true;
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
            }

            if (tokens[num_tokens - 1][0] == '<') {
                fd = open(tokens[--num_tokens] + 1, O_RDONLY);

                if (fd == -1) {
                    perror("redirect");
                    exit_code = errno;
                } else {
                    redirect_in = true;
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }
            }

            idx = -1;

            for (i = 0; i < NUM_COMMANDS; i++) {
                if (strcmp(tokens[0], commands[i].name) == 0) {
                    idx = i;
                    break;
                }
            }

            if (background == true) {
                pid = fork();
            }

            if (background == false || (background == true && pid == 0)) {
                if (commands[idx].change_status == true) {
                    exit_code = 0;
                }

                if (idx > 0 && commands[idx].background == false && is_pipe == false) {
                    commands[i].function();
                } else {
                    if (background == false) {
                        pid = fork();
                    }

                    if (pid == 0) {
                        if (is_pipe == true) {
                            if (pipe_idx == 0) {
                                dup2(pipe_fd[pipe_idx][STDOUT_FILENO], STDOUT_FILENO);
                                close(pipe_fd[pipe_idx][STDIN_FILENO]);
                                close(pipe_fd[pipe_idx][STDOUT_FILENO]);
                            } else if (pipe_idx < num_pipe_commands - 1) {
                                dup2(pipe_fd[pipe_idx - 1][STDIN_FILENO], STDIN_FILENO);
                                dup2(pipe_fd[pipe_idx][STDOUT_FILENO], STDOUT_FILENO);
                                close(pipe_fd[pipe_idx - 1][STDIN_FILENO]);
                                close(pipe_fd[pipe_idx - 1][STDOUT_FILENO]);
                                close(pipe_fd[pipe_idx][STDIN_FILENO]);
                                close(pipe_fd[pipe_idx][STDOUT_FILENO]);
                            } else {
                                dup2(pipe_fd[pipe_idx - 1][STDIN_FILENO], STDIN_FILENO);
                                close(pipe_fd[pipe_idx - 1][STDIN_FILENO]);
                                close(pipe_fd[pipe_idx - 1][STDOUT_FILENO]);
                            }
                        }

                        if (idx >= 0) {
                            commands[i].function();
                        } else {
                            tokens[num_tokens] = NULL;
                            execvp(tokens[0], tokens);
                        }
                    } else {
                        if (is_pipe == true && pipe_idx > 0) {
                            close(pipe_fd[pipe_idx - 1][STDIN_FILENO]);
                            close(pipe_fd[pipe_idx - 1][STDOUT_FILENO]);
                        }

                        waitpid(pid, &exit_status, 0);

                        if (commands[idx].change_status == true) {
                            exit_code = WEXITSTATUS(exit_status);
                        }
                    }
                }
            }

            if (pid == 0) {
                return exit_code;
            }

            background = false;

            if (redirect_in == true) {
                dup2(in, STDIN_FILENO);
                redirect_in = false;
            }

            if (redirect_out == true) {
                dup2(out, STDOUT_FILENO);
                redirect_out = false;
            }
        }

        if (is_pipe == true) {
            pipe_idx++;

            if (pipe_idx == num_pipe_commands) {
                is_pipe = false;
            }
        }
    }

    return 0;
}

int tokenize(char *line, int length) {
    int num = 0, i = 0;
    bool quotes = false;

    while (i < length && isspace(line[i]) > 0) {
        i++;
    }

    if (length == 0 || i == length || line[i] == '#') {
        return 0;
    }

    if (line[i] == '\"') {
        tokens[num++] = &line[++i];
        quotes = true;
    } else {
        tokens[num++] = &line[i];
    }

    for (i = i + 1; i <= length; i++) {
        if (quotes == false && isspace(line[i]) > 0) {
            line[i] = '\0';
        } else if (line[i] == '\"') {
            if (quotes == false && line[i - 1] == '\0') {
                tokens[num++] = &line[++i];
                quotes = true;
            } else if (quotes == true) {
                line[i] = '\0';
                quotes = false;
            }
        } else if (line[i] != '\0' && line[i - 1] == '\0') {
            tokens[num++] = &line[i];
        }
    }

    return num;
}

void handle_sigchld() {
    waitpid(WAIT_ANY, &exit_status, WNOHANG);
    exit_code = WEXITSTATUS(exit_status);
}

void help() {
    int i;

    for (i = 0; i < NUM_COMMANDS; i++) {
        printf("%10s - %s\n", commands[i].name, commands[i].description);
    }
}

void name() {
    if (num_tokens > 1) {
        strncpy(shell_name, tokens[1], 64);
    } else {
        printf("%s\n", shell_name);
    }
}

void status() {
    printf("%d\n", exit_code);
}

void shell_exit() {
    if (num_tokens > 1) {
        exit(atoi(tokens[1]));
    } else {
        exit(0);
    }
}

void print() {
    int i;

    for (i = 1; i < num_tokens; i++) {
        printf("%s", tokens[i]);

        if (i != num_tokens - 1) {
            printf(" ");
        }
    }

    fflush(stdout);
}

void echo() {
    int i;

    for (i = 1; i < num_tokens; i++) {
        printf("%s ", tokens[i]);

        if (i != num_tokens - 1) {
            printf(" ");
        }
    }

    printf("\n");
    fflush(stdout);
}

void pid() {
    printf("%d\n", getpid());
}

void ppid() {
    printf("%d\n", getppid());
}

void dir() {
    if (num_tokens > 1) {
        exit_code = chdir(tokens[1]);
    } else {
        exit_code = chdir("/");
    }

    if (exit_code < 0) {
        perror(tokens[0]);
        exit_code = errno;
        return;
    }

    getcwd(cwd, 256);
}

void dirwhere() {
    printf("%s\n", cwd);
}

void dirmake() {
    exit_code = mkdir(tokens[1], S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);

    if (exit_code < 0) {
        perror(tokens[0]);
        exit_code = errno;
    }
}

void dirremove() {
    exit_code = rmdir(tokens[1]);

    if (exit_code < 0) {
        perror(tokens[0]);
        exit_code = errno;
    }
}

void dirlist() {
    DIR *dir;
    struct dirent *file;

    if (num_tokens > 1) {
        dir = opendir(tokens[1]);
    } else {
        dir = opendir(".");
    }

    if (dir == NULL) {
        perror(tokens[0]);
        exit_code = errno;
        return;
    }

    exit_code = 0;

    while ((file = readdir(dir)) != NULL) {
        printf("%s  ", file->d_name);
    }

    printf("\n");
    closedir(dir);
}

void linkhard() {
    exit_code = link(tokens[1], tokens[2]);

    if (exit_code < 0) {
        perror(tokens[0]);
        exit_code = errno;
    }
}

void linksoft() {
    exit_code = symlink(tokens[1], tokens[2]);

    if (exit_code < 0) {
        perror(tokens[0]);
        exit_code = errno;
    }
}

void linkread() {
    char path[256];
    int length = readlink(tokens[1], path, 256);

    if (length < 0) {
        perror(tokens[0]);
        exit_code = errno;
        return;
    }

    exit_code = 0;

    path[length] = '\0';
    printf("%s\n", path);
}

void linklist() {
    struct stat f;
    DIR *dir;
    struct dirent *file;

    exit_code = stat(tokens[1], &f);

    if (exit_code < 0) {
        perror(tokens[0]);
        exit_code = errno;
        return;
    }

    dir = opendir(cwd);

    if (dir == NULL) {
        perror(tokens[0]);
        exit_code = errno;
        return;
    }

    while ((file = readdir(dir)) != NULL) {
        if (file->d_ino == f.st_ino) {
            printf("%s  ", file->d_name);
        }
    }

    printf("\n");
    closedir(dir);
}

void shell_unlink() {
    exit_code = unlink(tokens[1]);

    if (exit_code < 0) {
        perror(tokens[0]);
        exit_code = errno;
    }
}

void shell_rename() {
    exit_code = rename(tokens[1], tokens[2]);

    if (exit_code < 0) {
        perror(tokens[0]);
        exit_code = errno;
    }
}

void cpcat() {
    int fd, len;
    char buffer[4096];

    if (num_tokens > 1 && tokens[1][0] != '-' && redirect_in == false) {
        fd = open(tokens[1], O_RDONLY);

        if (fd < 0) {
            perror(tokens[0]);
            exit_code = errno;
            return;
        }

        dup2(fd, STDIN_FILENO);
        redirect_in = true;
        close(fd);
    }

    if (num_tokens > 2 && tokens[2][0] != '-' && redirect_out == false) {
        fd = creat(tokens[2], S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

        if (fd < 0) {
            perror(tokens[0]);
            exit_code = errno;
            return;
        }

        dup2(fd, STDOUT_FILENO);
        redirect_out = true;
        close(fd);
    }

    while ((len = read(STDIN_FILENO, buffer, 4096)) > 0) {
        write(STDOUT_FILENO, buffer, len);
    }

    if (len < 0) {
        perror(tokens[0]);
        exit_code = errno;
    }
}

void pipes() {
    int i;
    num_pipe_commands = num_tokens - 1;
    pipe_idx = -1;
    is_pipe = true;

    for (i = 1; i < num_tokens; i++) {
        if (pipe_commands[i - 1] != NULL) {
            free(pipe_commands[i - 1]);
        }

        pipe_commands[i - 1] = (char *)malloc(strlen(tokens[i]) * sizeof(char));
        strcpy(pipe_commands[i - 1], tokens[i]);
    }
}
