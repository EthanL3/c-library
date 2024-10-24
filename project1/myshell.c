#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#define MAX_LENGTH 512
#define MAX_ARGS 512
#define MAX_TOKEN_LENGTH 32
#define STDOUT 1
#define STDIN 0

char *trim_spacing(char *str)
{
    char *end;
    while(isspace((unsigned char)*str)) {
        str++;
    }

    if(*str == 0)
        return str;

    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) {
        end--;
    }
    end[1] = '\0';
    return str;
}

int read_commands(char* input, char** commands, bool *background_run) {
    input[strcspn(input, "\n")] = '\0';

    if (input[strlen(input) - 1] == '&') {
        *background_run = true;
        input[strlen(input) - 1] = '\0';
    }
    else {
        *background_run = false;
    }

    char *token = strtok(input, "|");

    int i = 0;
    while(token) {
        commands[i] = token;
        token = strtok(NULL, "|");
        i++;
    }
    commands[i] = NULL;
    return i;
}

// parse individual arguments from a single command
char **parse_args(char* command) {
    char **args = malloc(MAX_ARGS * sizeof(char*));
    char *token = strtok(command, " \t\n\"");
    int i = 0;

    while (token != NULL) {
        if (strlen(token) > MAX_TOKEN_LENGTH) {
            printf("ERROR: Token exceeds maximum length of 32 characters\n");
            free(args);
            return NULL;
        }
        args[i] = token;
        i++;
        if (i >= MAX_ARGS - 1) {
            printf("ERROR: Too many arguments\n");
            break;
        }
        token = strtok(NULL, " \t\n\"");
    }
    args[i] = NULL;
    return args;
}

void handle_sigchild(int sig, siginfo_t *siginfo, void *context) {
    int saved_errno = errno;
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0);
    errno = saved_errno;
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0); // fflush(stdout) always

    struct sigaction sa;
    sa.sa_sigaction = &handle_sigchild;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        printf("ERROR: sigaction error");
        exit(1);
    }

    bool suppress = false;
    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        suppress = true;
    }
    
    char input[MAX_LENGTH];
    char *commands[MAX_ARGS];
    
    while(1) {
        if (!suppress) {
            printf("my_shell$ ");
        }

        if(fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n");
            break;
        }

        bool background_run = false;

        int num_commands = read_commands(input, commands, &background_run);
        for(int i = 0; i < num_commands; i++) {
            commands[i] = trim_spacing(commands[i]);
        }

        if (num_commands > 1) {
            int num_pipes = num_commands - 1;
            int pipes[num_pipes][2];

            // create pipeline
            for(int i = 0; i < num_pipes; i++) {
                int pipe_status = pipe(pipes[i]);
                if(pipe_status == -1) {
                    printf("ERROR: Pipe creation failed");
                    exit(1);
                }
            }
            
            // fork each command
            for(int i = 0; i < num_commands; i++) {
                pid_t pid = fork();

                if(pid == 0) {
                    // Child
                    char **args;

                    // input redirection
                    if (i == 0 && strchr(commands[i], '<') != NULL) {
                        char *command_file_input = strtok(commands[i], "<");
                        char *filename = trim_spacing(strtok(NULL, "<"));
                        args = parse_args(command_file_input);
                        int fd = open(filename, O_RDONLY);
                        if (fd == -1) {
                            printf("ERROR: file not opened properly\n");
                            exit(1);
                        }
                        close(STDIN);
                        dup2(fd, STDIN);
                        close(fd);
                    }

                    // output redirection
                    else if (i == num_commands - 1 && strchr(commands[i], '>') != NULL) {
                        char *command_file_output = strtok(commands[i], ">");
                        char *filename = trim_spacing(strtok(NULL, ">"));
                        args = parse_args(command_file_output);
                        int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fd == -1) {
                            printf("ERROR: file not opened properly\n");
                            exit(1);
                        }
                        close(STDOUT);
                        dup2(fd, STDOUT);
                        close(fd);
                    }

                    else {
                        args = parse_args(commands[i]);
                    }

                    if (i > 0) {
                        close(STDIN);
                        dup2(pipes[i - 1][0], STDIN);
                        close(pipes[i - 1][0]);
                    }

                    if (i < num_pipes) {
                        close(STDOUT);
                        dup2(pipes[i][1], STDOUT);
                        close(pipes[i][1]);
                    }

                    execvp(args[0], args);
                    printf("ERROR: command not found\n");
                    exit(1);
                }
                else if (pid > 0) {
                    // Parent
                    if (i > 0) {
                        close(pipes[i - 1][0]); 
                    }
                    if (i < num_pipes) {
                        close(pipes[i][1]);
                    }
                }
                else {
                    printf("ERROR: Fork failed\n");
                    exit(1);
                }
            }
            int status;
            for(int i = 0; i < num_commands; i++) {
                if (!background_run) {
                    int status;
                    wait(&status);
                }
            }
        }
        
        else {
            pid_t pid = fork();
            if (pid == 0) {
                // Child
                char** args;

                // something like cat < x.txt > y.txt
                if (strchr(commands[0], '<') != NULL && strchr(commands[0], '>') != NULL) {
                    char *command_file_input = strtok(commands[0], "<");
                    char *rest = strtok(NULL, "");
                    char *input_filename = trim_spacing(strtok(rest, ">"));
                    char *output_filename = trim_spacing(strtok(NULL, ">"));
                    args = parse_args(command_file_input);
                    
                    int in_fd = open(input_filename, O_RDONLY);
                    if (in_fd == -1) {
                        printf("ERROR: error opening file\n");
                        exit(1);
                    }
                    close(STDIN);
                    dup2(in_fd, STDIN);
                    close(in_fd);

                    int out_fd = open(output_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    close(STDOUT);
                    dup2(out_fd, STDOUT);
                    close(out_fd);
                }
                
                else if (strchr(commands[0], '<') != NULL) {
                    char *command_file_input = strtok(commands[0], "<");
                    char *filename = trim_spacing(strtok(NULL, "<"));
                    args = parse_args(command_file_input);
                    int fd = open(filename, O_RDONLY);
                    if (fd == -1) {
                        printf("ERROR: file not opened properly\n");
                        exit(1);
                    }
                    close(STDIN);
                    dup2(fd, STDIN);
                    close(fd);
                }

                else if (strchr(commands[0], '>') != NULL) {
                    char *command_file_output = strtok(commands[0], ">");
                    char *filename = trim_spacing(strtok(NULL, ">"));
                    args = parse_args(command_file_output);
                    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd == -1) {
                        printf("ERROR: file not opened properly\n");
                        exit(1);
                    }
                    close(STDOUT);
                    dup2(fd, STDOUT);
                    close(fd);
                }
                
                else {
                    args = parse_args(commands[0]);
                }
                execvp(args[0], args);
                printf("ERROR: command not found\n");
                exit(1);
            }
            else if (pid > 0) {
                // Parent
                if (!background_run) {
                    int status;
                    waitpid(pid, &status, 0);
                } 
            }
            else {
                printf("ERROR: Fork failed\n");
                exit(1);
            }
        }
    }
    return 0;
}