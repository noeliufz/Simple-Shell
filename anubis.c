#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#define MAXIMUM_CMD_NUM 256

// Use this _DEBUG variable to control the error messages printed
// by the ERROR function.
//  _DEBUG=0: print only one error message;
//  _DEBUG=1: print custom messages.
// Make sure you set _DEBUG to 0 for the submitted version.

int _DEBUG = 0;
int _BATCH = 0;
char **path;
int path_size;
char **batch_buffer;
int batch_buffer_size;

int pipes[MAXIMUM_CMD_NUM];
int pipe_num;


char **parse(char *line, int *n, char *delim);
void do_outer_cmd(int argc, char *argv[]);
int is_built_in_cmd(int argc, char *argv[]);
void do_built_in_cmd(int argc, char **argv, char *line);
void do_one_line_cmd(char *line);
int set_re_output(char *cmd);
char *remove_consecutive_space(char *text);
void connect_pipe(char **parsed_PIPE, int i, int size_pipe, int rc);


void ERROR(int errnum, const char *format, ...) {
    if (_DEBUG) {
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
        if (errnum > 0)
            fprintf(stderr, " : %s", strerror(errnum));
        fprintf(stderr, "\n");
        return;
    }

    fprintf(stderr, "An error has occurred\n");
}

int main(int argc, char *argv[]) {
    // if there only exist 2 arguments, set to batch mode
    if (argc == 2)
        _BATCH = 1;
    else if (argc == 1)
        ;
    else {
        // if there exist more than 2 arguments, print error message
        ERROR(1, "Argument number wrong.");
        exit(EXIT_FAILURE);
    }
    // set default path to "/bin"
    path = (char **)malloc(sizeof(char *));
    *path = "/bin";

    if (_BATCH == 1) {
        // open batch file
        FILE *p = fopen(argv[1], "r");
        if (p == NULL) {
            ERROR(1, "open batch file");
            exit(EXIT_FAILURE);
        }
        // default length for 30 lines of the batch
        int sz = 30;
        batch_buffer = (char **)malloc(sz * sizeof(char *));
        char *line = NULL;
        size_t len = 0;
        int i = 0;
        while ((getline(&line, &len, p)) != -1) {
            if (i > sz - 1) {
                batch_buffer = realloc(batch_buffer, (i + 1) * sizeof(char *));
            }
            batch_buffer[i] = strdup(line);

            i++;
        }
        // set size to real size
        batch_buffer_size = i;
        free(line);
        fclose(p);

        // set the last character of each line to '\0'
        for (int k = 0; k < batch_buffer_size; k++) {
            batch_buffer[k][strlen(batch_buffer[k]) - 1] = '\0';
        }

        for (int j = 0; j < batch_buffer_size; j++) {
            // run each line command
            do_one_line_cmd(batch_buffer[j]);
            free(batch_buffer[j]);
        }

    } else {
        while (1) {
            printf("%s", "anubis> ");
            // read line from stdin
            char *line = NULL;
            size_t len = 0;

            if (getline(&line, &len, stdin) == -1) {
                free(line);
                break;
            }

            do_one_line_cmd(line);
            free(line);
        }
    }

    return 0;
}

void do_one_line_cmd(char *line) {
    // remove consecutive space of each line
    line = strdup(remove_consecutive_space(line));
    // if the line is empty, return
    if (!strcmp(line, ""))
        return;
    // set redirective output if possible
    int output_file;

    // parse from &
    int size_at = 0;
    int size_space = 0;
    char **parsed_AT = parse(line, &size_at, "&");
    // parse from ' ' (space)
    char **parsed_SPACE = parse(line, &size_space, " ");
    // if it is built in command, do built in command
    if (is_built_in_cmd(size_space, parsed_SPACE)) {
        free(parsed_AT[0]);
        free(parsed_AT);
        do_built_in_cmd(size_space, parsed_SPACE, line);
        for (int i = 0; i < size_space; i++) {
            free(parsed_SPACE[i]);
        }
        free(parsed_SPACE);
        return;
    }
    // array of child processes to run background
    // i.e. cmmands separated by &
    int rc[size_at];
    // fork to run in a new child process
    int fk = fork();
    if (fk == -1) {
        ERROR(1, "fork failed");
        exit(EXIT_FAILURE);
    }
    if (fk == 0) {
        int i = 0;
        while (i != size_at) {
            // fork the current command separated by & and save it to array
            rc[i] = fork();
            if(rc[i] == -1) {
                ERROR(1, "fork failed");
                exit(EXIT_FAILURE);
            }
            // child process do the commands
            if (rc[i] == 0) {
                int size_out;
                int size_pipe;
                // command array separated by |
                char **parsed_PIPE;
                // if there exists >, set the redirect the output
                if (strchr(parsed_AT[i], '>') != NULL) {
                    output_file = set_re_output(parsed_AT[i]);
                    // if the output file set failes, exit the process
                    if (output_file == -1) {
                        free(parsed_AT[i]);
                        exit(EXIT_FAILURE);
                    }
                    parsed_PIPE = parse(parse(parsed_AT[i], &size_out, ">")[0],
                                        &size_pipe, "|");
                }
                parsed_PIPE = parse(parsed_AT[i], &size_pipe, "|");
                // if there is no commands that should use pipeline, do the
                // command directly
                if (size_pipe == 1) {
                    parsed_SPACE = parse(parsed_AT[i], &size_space, " ");
                    free(parsed_AT[i]);
                    free(parsed_PIPE);
                    do_outer_cmd(size_space, parsed_SPACE);
                } else {
                    // fork a new process to run commands connected by pipe
                    // lines
                    int rc_pipe = fork();
                    // in child process, connect pipe lines
                    if (rc_pipe == 0) {
                        free(parsed_AT[i]);
                        connect_pipe(parsed_PIPE, 0, size_pipe, 0);

                    } else {
                        // parent process waits for child process
                        int status;
                        pid_t terminated_pid;
                        while ((terminated_pid = wait(&status)) > 0)
                            exit(EXIT_SUCCESS);

                    }
                }
                for (int k = 0; k < size_pipe; k++) {
                    free(parsed_PIPE[k]);
                }
                free(parsed_PIPE);
            }
            // free(parsed_AT);
            //  free(parsed_SPACE);
            //   parent process go on running new command separated by &
            i++;
        }
        for (int k = 0; k < size_at; k++) {
            free(parsed_AT[k]);
        }
        for (int k = 0; k < size_space; k++) {
            free(parsed_SPACE[k]);
        }
        free(parsed_AT);
        free(parsed_SPACE);

        for (int i = 0; i < size_at; i++) {
            int status;
            waitpid(rc[i], &status, 0);
        }
        exit(EXIT_SUCCESS);
    }
    for (int k = 0; k < size_at; k++) {
        free(parsed_AT[k]);
    }
    for (int k = 0; k < size_space; k++) {
        free(parsed_SPACE[k]);
    }
    free(parsed_AT);
    free(parsed_SPACE);
    // wait for all child processes to end
    wait(NULL);
    // restore standard output and standard error
    dup2(STDERR_FILENO, STDERR_FILENO);
    dup2(STDOUT_FILENO, STDOUT_FILENO);
}

// parse: a simple function to parse a line of text.
// Parameters:
// - char *line: a string containing characters to be parsed
// - int *n: this will contain the number of tokens parsed when the
//           function returns.
// - char *delim: this contains the characters that will be used as
//           the delimiters.
// Return value:
// - A pointer to an array of strings containing the tokens parsed,
//   with the last element being the NULL pointer.
// - Or, NULL if the parsing failed for some reasons.
//
// Note: this function allocates memory dynamicall using malloc/realloc.
//       Make sure you free them if they are no longer in use, using the
//       free_token function (see below).
char **parse(char *line, int *n, char *delim) {
    int sz = 32;
    char **a = malloc(sz * sizeof(char *));
    assert(a != NULL);
    memset(a, 0, sz * sizeof(char *));

    int i = 0;
    *n = 0;
    char *token;

    while (line) {
        token = strsep(&line, delim);
        if (token == NULL)
            break;
        if (token[0] == '\0')
            continue;
        if (i >= sz - 1) {
            ++sz;
            a = realloc(a, sz * sizeof(char *));
        }
        a[i] = strdup(remove_consecutive_space(token));
        ++i;
    }
    a[i] = NULL;
    *n = i;
    return a;
}

void do_outer_cmd(int argc, char *argv[]) {
    // set status if the command is executable
    int accessible = -1;
    int i = 0;
    char *access_cmd = "";
    while (path != NULL && path[i] != NULL) {
        char access_try[256] = "";
        strcat(access_try, path[i]);
        strcat(access_try, "/");
        strcat(access_try, argv[0]);
        if (access(access_try, X_OK) == 0) {
            accessible = 0;
            access_cmd = strdup(access_try);
            break;
        }
        i++;
    }
    if (accessible == -1) {
        char access_try[256] = "";
        strcat(access_try, argv[0]);
        if (access(access_try, X_OK) == 0) {
            accessible = 0;
            access_cmd = strdup(access_try);
        }
    }
    // try the current path
    if (accessible == -1 || path == NULL || path[0] == NULL) {
        ERROR(1, "access");
        exit(EXIT_FAILURE);
    }
    // execute the command
    int e = execv(access_cmd, argv);
    if (e == -1) {
        free(access_cmd);
        ERROR(1, "execv cmd: %s", access_cmd);
        exit(EXIT_FAILURE);
    }
}

char *remove_consecutive_space(char *text) {
    char *read_ptr = text;
    char *write_ptr = text;
    // trim the white space at the beginning and at the end
    while (isspace((unsigned char)*read_ptr)) {
        read_ptr++;
    }
    char *s = read_ptr;
    write_ptr = s;

    while (*(read_ptr + 1) != EOF && *(read_ptr + 1) != '\0' &&
           *(read_ptr + 1) != '\n') {
        if (!((*read_ptr == ' ' || *read_ptr == '\t') &&
              (*(read_ptr + 1) == ' ' || *(read_ptr + 1) == '\t'))) {
            *write_ptr = *read_ptr;
            write_ptr++;
        }
        read_ptr++;
    }
    *write_ptr = *read_ptr;
    write_ptr++;
    read_ptr++;
    *write_ptr = '\0';
    return s;
}
int is_built_in_cmd(int argc, char *argv[]) {
    if (argc == 0)
        return 0;
    if (!strcmp(argv[0], "exit"))
        return 1;
    if (!strcmp(argv[0], "cd"))
        return 1;
    if (!strcmp(argv[0], "path"))
        return 1;

    return 0;
}

void do_built_in_cmd(int argc, char **argv, char *line) {
    if (!strcmp(argv[0], "exit")) {
        if (argc != 1)
            ERROR(1, "exit");
        else {
            free(argv[0]);
            free(argv);
            for (int i = 0; i < path_size; i++) {
                free(path[i]);
            }
            free(path);
            if (_BATCH) {
                free(batch_buffer);
            } else {
                free(line);
            }
            exit(EXIT_SUCCESS);
        }
    }
    if (!strcmp(argv[0], "cd")) {
        if (argc != 2)
            ERROR(1, "cd argc");
        else if (chdir(argv[1]) == -1)
            ERROR(1, "cd");
    }
    if (!strcmp(argv[0], "path")) {
        int i = 1;
        for (int i = 0; i < path_size; i++) {
            free(path[i]);
        }
        free(path);
        path = (char **)malloc(argc * sizeof(char *));
        while (i != argc) {
            path[i - 1] = strdup(argv[i]);
            i++;
        }
        path[i - 1] = NULL;
        path_size = argc;
    }
}

/*
 * Redirect the output path
 * if success return the file descriptor, false return -1
 */
int set_re_output(char *cmd) {
    int size_out;
    int output_file;
    char **parsed_OUT = parse(cmd, &size_out, ">");
    if (size_out != 2) {
        ERROR(1, "parse '>'");
        free(parsed_OUT);
        return -1;
    } else {
        char **parsed_FILE = parse(parsed_OUT[1], &size_out, " ");
        if (size_out != 1) {
            ERROR(1, "parse '>'");
            return -1;
        }
        char *file_name = parsed_FILE[0];
        output_file =
            open(file_name, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);

        if (output_file == -1) {
            ERROR(1, "open file");
            return -1;
        } else {
            dup2(output_file, STDOUT_FILENO);
            dup2(output_file, STDERR_FILENO);
        }
        free(parsed_FILE[0]);
        free(parsed_FILE);
    }
    for (int i = 0; i < size_out; i++) {
        free(parsed_OUT[i]);
    }
    free(parsed_OUT);
    return output_file;
}

void connect_pipe(char **parsed_PIPE, int i, int size_pipe, int rc) {
    if (i == size_pipe) {
        // wait for processes in pipes to exit
        int status;
        waitpid(rc, &status, 0);
        free(parsed_PIPE);
        
        pipe_num = size_pipe;
        exit(EXIT_SUCCESS);
    }
    int fd[2];
    pipe(fd);
    rc = fork();
    pipes[i] = rc;
    if (rc == -1) {
        ERROR(1, "fork");
        exit(EXIT_FAILURE);
    }
    if (rc == 0) {
        int size;
        if (i != size_pipe - 1)
            dup2(fd[1], STDOUT_FILENO);
        close(fd[0]);
        char **parsed_SPACE = parse(parsed_PIPE[i], &size, " ");
        free(parsed_PIPE[i]);
        do_outer_cmd(size, parsed_SPACE);
    }
    if (rc != 0) {
        dup2(fd[0], STDIN_FILENO);
        close(fd[1]);
        i++;
        connect_pipe(parsed_PIPE, i, size_pipe, rc);
    }
}
