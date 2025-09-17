#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>

//Constants
#define MAX_LINE_LEN 1024
#define MAX_ARGS 64
#define MAX_CMDS 16
#define INITIAL_HISTORY_CAPACITY 10

// Structs
typedef struct {
    char command[MAX_LINE_LEN];
    pid_t pid;
    time_t startTime;
    double durationSecs;
} CommandStat;

typedef struct {
    CommandStat* entries;
    int count;
    int capacity;
    int isTestMode;
} CommandHistory;

// Global Dynamic History
CommandHistory history;

// History Functions
void initHistory(CommandHistory* h) {
    h->entries = malloc(INITIAL_HISTORY_CAPACITY * sizeof(CommandStat));
    h->count = 0;
    h->capacity = INITIAL_HISTORY_CAPACITY;
    h->isTestMode = 0;
}

void freeHistory(CommandHistory* h) {
    free(h->entries);
}

void addToHistory(CommandHistory* h, const char* command) {
    if (h->count >= h->capacity) {
        h->capacity *= 2;
        h->entries = realloc(h->entries, h->capacity * sizeof(CommandStat));
    }
    strncpy(h->entries[h->count].command, command, MAX_LINE_LEN - 1);
    h->entries[h->count].command[MAX_LINE_LEN - 1] = '\0';
    h->count++;
}

// Signal Handling
void handleSigint(int sig) {
    printf("\n\n--- SimpleShell Session Summary ---\n");
    for (int i = 0; i < history.count; i++) {
        char timeStr[30];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&history.entries[i].startTime));
        printf("  Cmd %-3d: %s\n", i + 1, history.entries[i].command);
        printf(" -> PID: %-7d | Start: %s | Duration: %.4f s\n",
               history.entries[i].pid, timeStr, history.entries[i].durationSecs);
    }
    printf("\n");
    printf("SimpleShell terminated.\n");
    freeHistory(&history);
    exit(EXIT_SUCCESS);
}

// Built-in Commands
void displayHistory(CommandHistory* h) {
    for (int i = 0; i < h->count; i++) {
        printf("%5d  %s\n", i + 1, h->entries[i].command);
    }
}

int handleBuiltinCommand(char** args) {
    if (strcmp(args[0], "exit") == 0) {
        handleSigint(0);
        return 0;
    }
    if (strcmp(args[0], "history") == 0) {
        displayHistory(&history);
        return 1;
    }
    return -1;  // Not a built-in command
}

// Parsing
void parseSpaces(char* command, char** args) {
    int i = 0;
    char* token = strtok(command, " \t\r\n\a");
    while (token != NULL && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " \t\r\n\a");
    }
    args[i] = NULL;
}

int parsePipes(char* line, char** commands) {
    int i = 0;
    char* token = strtok(line, "|");
    while (token != NULL && i < MAX_CMDS) {
        commands[i++] = token;
        token = strtok(NULL, "|");
    }
    commands[i] = NULL;
    return i;
}

// Command Execution
int executeLine(char* line) {
    char originalLine[MAX_LINE_LEN];
    strncpy(originalLine, line, MAX_LINE_LEN - 1);
    originalLine[MAX_LINE_LEN - 1] = '\0';

    addToHistory(&history, originalLine);
    int historyIndex = history.count - 1;

    char* commands[MAX_CMDS];
    int numCmds = parsePipes(originalLine, commands);

    struct timeval startTv, endTv;
    gettimeofday(&startTv, NULL);
    history.entries[historyIndex].startTime = startTv.tv_sec;

    // Single command: check for built-in
    if (numCmds == 1) {
        char* args[MAX_ARGS];
        char commandCopy[MAX_LINE_LEN];
        strncpy(commandCopy, commands[0], MAX_LINE_LEN - 1);
        commandCopy[MAX_LINE_LEN - 1] = '\0';

        parseSpaces(commandCopy, args);
        if (args[0] == NULL) return 1;

        int builtinStatus = handleBuiltinCommand(args);
        if (builtinStatus != -1) {
            gettimeofday(&endTv, NULL);
            history.entries[historyIndex].pid = getpid();
            history.entries[historyIndex].durationSecs =
                (endTv.tv_sec - startTv.tv_sec) + (endTv.tv_usec - startTv.tv_usec) / 1e6;
            return builtinStatus;
        }
    }

    // External commands / pipelines
    int inFd = STDIN_FILENO;
    pid_t pids[MAX_CMDS];

    for (int i = 0; i < numCmds; i++) {
        int pipeFd[2];
        if (i < numCmds - 1 && pipe(pipeFd) == -1) {
            perror("pipe");
            return 1;
        }

        pids[i] = fork();
        if (pids[i] == -1) {
            perror("fork");
            return 1;
        }

        if (pids[i] == 0) { // Child
            if (i > 0) {
                dup2(inFd, STDIN_FILENO);
                close(inFd);
            }
            if (i < numCmds - 1) {
                dup2(pipeFd[1], STDOUT_FILENO);
                close(pipeFd[0]);
                close(pipeFd[1]);
            }

            char* args[MAX_ARGS];
            parseSpaces(commands[i], args);
            if (args[0] == NULL) exit(EXIT_SUCCESS);
            if (execvp(args[0], args) == -1) {
                fprintf(stderr, "SimpleShell: command not found: %s\n", args[0]);
                exit(EXIT_FAILURE);
            }
        } else { //Parent
            if (i < numCmds - 1) close(pipeFd[1]);
            if (i > 0) close(inFd);
            if (i < numCmds - 1) inFd = pipeFd[0];
        }
    }

    for (int i = 0; i < numCmds; i++) {
        int status;
        waitpid(pids[i], &status, 0);
    }

    gettimeofday(&endTv, NULL);

    long seconds = endTv.tv_sec - startTv.tv_sec;
    long microseconds = endTv.tv_usec - startTv.tv_usec;
    double durationSecs = seconds + microseconds / 1e6;

    history.entries[historyIndex].pid = pids[numCmds - 1];
    history.entries[historyIndex].durationSecs = durationSecs;


    return 1;
}

// Input
char* readLine() {
    char* line = NULL;
    size_t bufsize = 0;
    if (getline(&line, &bufsize, stdin) == -1) {
        if (feof(stdin)) {
            printf("\n");
            handleSigint(0);
        } else {
            perror("getline");
            exit(EXIT_FAILURE);
        }
    }
    line[strcspn(line, "\n")] = 0;
    return line;
}

void shellLoop() {
    char* line;
    int status = 1;

    do {
        printf("SimpleShell> ");
        fflush(stdout);

        line = readLine();

        if (line == NULL || strlen(line) == 0 || strspn(line, " \t\n\r") == strlen(line)) {
            free(line);
            continue;
        }

        status = executeLine(line);
        free(line);

    } while (status);

    printf("\nExited.\n");
    freeHistory(&history);
}

int main(int argc, char** argv) {
    initHistory(&history);
    signal(SIGINT, handleSigint);
    shellLoop();
    return EXIT_SUCCESS;
}
