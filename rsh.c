//Program developed by Robert Fudge, 2025

//Header file include
#include "rsh.h"

//Standard Library Includes
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//Posix library include
#include <unistd.h>

//System Includes
#include <sys/types.h>
#include <sys/wait.h>

//For interacting with terminal
#include <termios.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>

//Macros
#define PATH_LENGTH 1024

//Struct for restoring terminal on exit
struct termios orig_termios;

//RSH datastructures
struct __rsh {
    int capacity;
    pid_t running_process;
    char* path;
    struct __hist_node* hist_buffer;    //Head of history SLL
    struct __job_node* job_buffer;
};

//Needed for keeping history (job could technically replace that but imlementation would be more time consuming)
struct __hist_node {
    char* command;
    struct __hist_node* next;
};

//Needed for keeping track of jobs running in the foreground and background
struct __job_node {
    pid_t pid;
    char* command;
    int status;
    struct __job_node* next;
};

static bool rsh_initialized = false;
struct __rsh* rsh;

//Internal functions
void __append_history(char*);
void __append_job(pid_t, const char*, int);
void __disable_raw_mode(void);
void __display_history(void);
void __enable_raw_mode(void);
void __handle_ctrlc(int);
void __handle_ctrlz(int);
int __handle_input(int, char**, char*);
int __handle_pipeline(char***, int);
char** __parse_input(int*, char**);
char*** __parse_pipeline(char*, int*);
void __remove_job(pid_t);
struct __rsh* __rsh_get(void);
void __rsh_destroy(struct __rsh*);

//User facing function to run terminal instance
uint8_t rsh_run(void) {
    //Display intro text
    printf("RSH V0.0.1, program developed by Robert Fudge\n");

    int* argc = malloc(1 * sizeof(int));

    //Prompt user and handle input - main loop
    while (true) {
        char* raw_input = NULL;
        char** argv = __parse_input(argc, &raw_input);

        if (argv == NULL) {
            printf("Error: Failed to get user input\n");
            free(raw_input);
            continue;
        }

        __handle_input(*argc, argv, raw_input);

        free(raw_input);

    }

    free(argc);
}

//Helper function for adding job to rsh datastructure
void __append_job(pid_t pid, const char* cmd, int status) {
    struct __rsh* r = __rsh_get();
    struct __job_node* new_job = malloc(sizeof(struct __job_node));
    new_job->pid = pid;
    new_job->command = strdup(cmd);
    new_job->status = status;
    new_job->next = r->job_buffer;
    r->job_buffer = new_job;
}

//Helper function to append history to rsh datastructure
void __append_history(char* str) {
    //Get RSH Data structure
    struct __rsh* r = __rsh_get();

    if (r->hist_buffer == NULL) {
        r->hist_buffer = malloc(sizeof(struct __hist_node));
        r->hist_buffer->command = NULL;
        r->hist_buffer->next = NULL;
    }

    //Create new node
    struct __hist_node* to_add = malloc(1 * sizeof(struct __hist_node));

    to_add->command = malloc((strlen(str) + 1) * sizeof(char));
    to_add->next = NULL;

    strcpy(to_add->command, str);

    //Add data to SLL containing previous history
    struct __hist_node* current = r->hist_buffer;

    //Iterate to end of list
    while (current->next != NULL) {
        current = current->next;
    }

    current->next = to_add;

    return;
}

//Helper function to disable raw mode
void __disable_raw_mode(void) {
    //Write original copy of terminal struct to terminal settings
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

//Helper function to display contents of history buffer
void __display_history(void) {
    //Get RSH Data structure
    struct __rsh* r = __rsh_get();

    //Iterate through and print strings
    struct __hist_node* current = r->hist_buffer->next;

    //Iterate to end of list
    while (current != NULL) {
        printf("%s\r\n", current->command);
        current = current->next;
    }
}


//User facing function to enable raw mode for testing
void __enable_raw_mode(void) {
    //Initialize terminal struct, retrieving state
    tcgetattr(STDIN_FILENO, &orig_termios);

    //Setup callback for restoring original terminal in case unexpected exit
    atexit(__disable_raw_mode);

    //Create a copy of the original terminal state
    struct termios raw = orig_termios;

    //Use flags to disable all CTRL characters
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);

    //Write modified settings to struct
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

//Agnostic of whether its caused by a signal or byte, the program needs to exit
void __handle_ctrlc(int sig) {
    //Get handle of rsh datastructure
    struct __rsh* r = __rsh_get();

    if (r->running_process != 0) {
        kill(r->running_process, SIGINT);
    }

    else {
        printf("\r\n");

        //Destroy RSH struct
        __rsh_destroy(rsh);
        exit(0);
    }
}

//Helper function to handle ctrl+z
void __handle_ctrlz(int sig) {
    struct __rsh* r = __rsh_get();
    if (r->running_process != 0) {
        kill(r->running_process, SIGTSTP);
        int status;
        waitpid(r->running_process, &status, WUNTRACED);
        if (WIFSTOPPED(status)) {
            __append_job(r->running_process, "command", 1); //Store actual command
        }
        r->running_process = 0;
    }
}

//Helper function to determine if input is valid
int __handle_input(int argc, char** argv, char* raw_input) {
    //Get handle of rsh datastructure
    struct __rsh* r = __rsh_get();

    //Handle empty command - Should come first
    if (argc == 0 || argv[0] == NULL) {
        return -1;
    }

    //Exit command
    else if (strcmp(argv[0], "exit") == 0) {
        __handle_ctrlc(0);
    }

    else if (strcmp(argv[0], "clear")  == 0) {
        for (uint8_t i = 0; i < 255; i++) {
            printf("\r\n");
        }
    }

    //Handle job-control commands
    else if (strcmp(argv[0], "jobs") == 0) {
        struct __job_node* j = r->job_buffer;
        while (j) {
            printf("[%d] %s\t%s\n", j->pid, (j->status == 1) ? "Stopped" : "Running", j->command);
            j = j->next;
        }
        return 0;
    }

    else if (strcmp(argv[0], "fg") == 0) {
        if (argc < 2) {
            fprintf(stderr, "Usage: fg <pid>\n");
            return -1;
        }

        pid_t pid = atoi(argv[1]);

        //Ned to ignore SIGTTOU when transferring group
        signal(SIGTTOU, SIG_IGN);
        tcsetpgrp(STDIN_FILENO, pid);
        signal(SIGTTOU, SIG_DFL);

        //Signal entire process group
        kill(-pid, SIGCONT);
        waitpid(pid, NULL, WUNTRACED);

        signal(SIGTTOU, SIG_IGN);
        tcsetpgrp(STDIN_FILENO, getpid());
        signal(SIGTTOU, SIG_DFL);

        return 0;
    }

    else if (strcmp(argv[0], "bg") == 0) {
        if (argc < 2) {
            fprintf(stderr, "Usage: bg <pid>\n");
            return -1;
        }

        //Resume in background
        pid_t pid = atoi(argv[1]);
        kill(pid, SIGCONT);
        __remove_job(pid);
        return 0;
    }

    int pipe_count = 0;
    char*** commands = __parse_pipeline(raw_input, &pipe_count);

    if (pipe_count > 1) {
        int res = __handle_pipeline(commands, pipe_count);

        //Cleanup commands array
        for (int i = 0; i < pipe_count; i++) {
            for (int j = 0; commands[i][j] != NULL; j++) {
                free(commands[i][j]);
            }

            free(commands[i]);
        }

        free(commands);
        return res;
    }

    //History function
    if (!strcmp(argv[0], "history") || !strcmp(argv[0], "History")) {
        __display_history();
        return 0;
    }

    char* cpy_path = malloc(PATH_LENGTH * sizeof(char));
    strcpy(cpy_path, rsh->path);

    //Fork to create child process
    pid_t id = fork();

    //If in child process
    if (id == 0) {
        //Create new process group
        setpgid(0, 0);

        //Execute command directly using execvp
        execvp(argv[0], argv);
        
        //If execvp returns, there was an error
        perror("execvp failed");
        _exit(127); //Use status 127 for "command not found"
    }

    //Parent process
    else if (id > 0) {
        //Set child process ID
        setpgid(id, id);

        //Set child as foreground process group
        signal(SIGTTOU, SIG_IGN);
        tcsetpgrp(STDIN_FILENO, id);
        signal(SIGTTOU, SIG_DFL);

        //Ignore signals while child is running
        signal(SIGINT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);

        //Store running process ID
        r->running_process = id;
        int status;
        
        //Wait for child process
        do {
            waitpid(id, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status) && !WIFSTOPPED(status));

        //Restore signal handlers
        signal(SIGINT, __handle_ctrlc);
        signal(SIGTSTP, __handle_ctrlz);

        //Reset terminal foreground to shell safely
        signal(SIGTTOU, SIG_IGN);
        tcsetpgrp(STDIN_FILENO, getpid());
        signal(SIGTTOU, SIG_DFL);

        //Handle job status
        if (WIFSTOPPED(status)) {
            __append_job(id, argv[0], 1); //Add to jobs as stopped
        } else {
            __remove_job(id); //Remove from jobs if exited
        }

        r->running_process = 0;
    }

    else {
        fprintf(stderr, "Error: Fork failed\r\n");
        return -1;
    }

    return 0; //Remove the "command not found" message
        
    printf("No executable with the name %s found in path: %s\r\n", argv[0], rsh->path);
    return -2;
}

//Helper fucntion for handling pipelining
int __handle_pipeline(char*** commands, int num_commands) {
    int prev_pipe[2];
    int next_pipe[2];
    pid_t pids[num_commands];

    for (int i = 0; i < num_commands; i++) {
        if (i < num_commands - 1) {
            if (pipe(next_pipe) < 0) {
                //Pipe failed, return with error
                printf("Error (FATAL): Could not open pipe");
                return -1;
            }
        }

        pid_t pid = fork();

        //Child process
        if (pid == 0) {

            //Sets up inputs
            if(i > 0) {
                dup2(prev_pipe[0], STDIN_FILENO);
                close(prev_pipe[0]);
                close(prev_pipe[1]);
            }

            //Sets up outputs
            if (i < num_commands - 1) {
                close(next_pipe[0]);
                dup2(next_pipe[1], STDOUT_FILENO);
                close(next_pipe[1]);
            }

            execvp(commands[i][0], commands[i]);
        }

        else if (pid < 0) {
            perror("fork");
            return -1;
        }

        pids[i] = pid;

        //If not first run, then close previous pipes
        if (i > 0) {
            close(prev_pipe[0]);
            close(prev_pipe[1]);
        }

        if (i < num_commands - 1) {
            prev_pipe[0] = next_pipe[0];
            prev_pipe[1] = next_pipe[1];
        }
    }

    int status = 0;
    for (int i = 0; i < num_commands; i++) {
        waitpid(pids[i], &status, 0);
    }

    return WEXITSTATUS(status);
}

//Helper function to get input from user
char** __parse_input(int* argc, char** input_ptr) {
//Initialize command variables - input can be as long as path length
    *input_ptr = malloc(PATH_LENGTH * sizeof(char));

    if (*input_ptr == NULL) {
        perror("Failed to allocate memory for input");
        return NULL;
    }

    size_t input_len = 0;

    //Prompt user for input
    printf("\r> ");
    fflush(stdout);

    //Allocate char to populate to
    char *c = malloc(sizeof(char));
    if (c == NULL) {
        perror("Failed to allocate memory for character");
        return NULL;
    }

    //Read input in loop
    while (true) {
        //Attempt to read char from stdin
        int read_res = read(STDIN_FILENO, c, sizeof(char));

        //If read cannot occur
        if (read_res == -1) {
            if (errno == EINTR) {
                //Signal interrupted read, restart
                continue;
            }

            perror("Error (FATAL): Cannot read from stdin");
            free(c);
            return NULL;
        }

        //Handle control characters
        if (iscntrl((unsigned char)*c)) {
            //Newline (Enter Key) - Could be \n or \r, add null byte to end of input
            if (*c == '\n' || *c == '\r') {
                (*input_ptr)[input_len] = '\0';
                printf("\r\n");
                break;
            }
            
            //Handle backspace
            else if (*c == '\b' || *c == 127) {
                if (input_len > 0) {
                    input_len--; //Remove the last character
                    printf("\b \b"); //Move cursor back, overwrite with space, move back again
                    fflush(stdout);
                }
            }
            
            //Add autocomplete at some point
            else if (*c == '\t') {
                printf("\t"); // Just print a tab for now
                fflush(stdout);
            }

            //Handle CTRL+C
            else if (*c == 0x03) {
                __handle_ctrlc(0);
            }
        }

        //Not a control character, add to the input buffer
        else {
            if (input_len < PATH_LENGTH - 1) {
                (*input_ptr)[input_len++] = *c; //Add the character to the input buffer
                printf("%c", *c); //Echo the character to the terminal
                fflush(stdout);
            }
            
            else {
                //Input buffer is full
                printf("\r\nInput too long! Maximum length is %d\r\n", PATH_LENGTH - 1);
                break;
            }
        }
    }

    //TODO get capacity from RSH datastructure
    size_t capacity = 16;

    //Allocate array for char pointers, to store passed tokens, commonly referred to as argv
    char **argv = malloc(capacity * sizeof(char*));

    //If malloc failed, return NULL, trying to abandon the !ptr notation as for others reading its harder to determine intent
    if (argv == NULL) {
        return NULL;
    }

    //Argc is to be used to index argv
    int ind = 0;

    //Add command to history
    __append_history(*input_ptr);

    char temp_buffer[PATH_LENGTH];
    strcpy(temp_buffer, *input_ptr);

    //Tokenize the input using space, \t, and \n
    char *token = strtok(temp_buffer, " \t\n");

    //Iterate through token list to find end
    while (token != NULL) {
        //If argc ends up surpassing capacity, I need to reallocate, easiest just to double capacity
        if (ind >= capacity) {
            capacity *= 2;
            char **temp = realloc(argv, capacity * sizeof(char *));

            //If temp could not be reallocated free every pointer in args, free args, free input buffer, and return NULL to caller
            if (!temp) {
                for (int i = 0; i < ind; free(argv[i++]));
                free(argv);

                return NULL;
            }
            argv = temp;
        }

        //Duplicate string, allocates memory dynamically on the heap
        argv[ind] = strdup(token);

        //If the element could not be copied, follow the same destruction procedure as above
        if (!argv[ind]) {
            for (int i = 0; i < ind; free(argv[i++]));
            free(argv);

            return NULL;
        }

        //increment argc as an element copied
        ind++;

        //Tell strtok  this is a subsequent call by passing NULL
        token = strtok(NULL, " \t\n");
    }

    //NULL terminate the array
    argv[ind] = NULL;

    //Return argc
    *argc = ind;

    //Return the pointer to args
    return argv;
}

char*** __parse_pipeline(char* in, int* pipe_count) {
    char*** commands = malloc(16 * sizeof(char**));
    *pipe_count = 0;
    char* saved_ptr;

    char* token = strtok_r(in, "|", &saved_ptr);


    while (token != NULL) {
        char** args = malloc(16 * sizeof(char*));
        int count = 0;
        char* space_save;
        char* arg = strtok_r(token, " \t\n", &space_save);

        //Iterate through arguments
        while (arg != NULL) {
            args[count++] = strdup(arg);
            arg = strtok_r(NULL, " \t\n", &space_save);
        }

        //NULL terminate list
        args[count] = NULL;

        //Looks complex but isnt, derefernce to get value of pipe_count, post-increment
        commands[(*pipe_count)++] = args;
        token = strtok_r(NULL, "|", &saved_ptr);
    }

    return commands;
}

//
void __remove_job(pid_t pid) {
    struct __rsh* r = __rsh_get();
    struct __job_node** curr = &r->job_buffer;
    while (*curr) {
        if ((*curr)->pid == pid) {
            struct __job_node* temp = *curr;
            *curr = (*curr)->next;
            free(temp->command);
            free(temp);
            return;
        }
        curr = &(*curr)->next;
    }
}

//
struct __rsh* __rsh_get() {
    if (!rsh_initialized) {
        __enable_raw_mode();

        //Set up handling of ctrl signals
        signal(SIGINT, __handle_ctrlc);
        signal(SIGTSTP, __handle_ctrlz);

        rsh = (struct __rsh*) malloc(sizeof(struct __rsh));

        //Initialize "class members"
        rsh->capacity = 16;
        rsh->running_process = 0;
        rsh->hist_buffer = malloc(1 * sizeof(struct __hist_node));
        rsh->path = strdup(getenv("PATH") ? getenv("PATH") : "/bin:/usr/bin");;

        rsh->hist_buffer->command = NULL;
        rsh->hist_buffer->next = NULL;

        rsh_initialized = true;

        //Return the pointer to the newly allocated memory
        return rsh;
    }
    
    return rsh;
}

//Helper function to destroy the rsh datastructure and any contained elements
void __rsh_destroy(struct __rsh* r) {
    //Clean history
    struct __hist_node* hist = r->hist_buffer;
    while (hist) {
        struct __hist_node* next = hist->next;
        free(hist->command);
        free(hist);
        hist = next;
    }
    
    //Clean jobs
    struct __job_node* job = r->job_buffer;
    while (job) {
        struct __job_node* next = job->next;
        free(job->command);
        free(job);
        job = next;
    }

    free(r->path);
    free(r);
}