/*************************************************************************
	> File Name: rsh.c
	> Author: dingzewei
	> Mail: dzwtom@126.com
	> Created Time: Mon 31 May 2021 10:04:41 PM CST
 ************************************************************************/

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>

// ASSUME: single word length < 15

// use struct to encapsulate one command
typedef struct Command {
    char** argv;            // all args in the command, including flags
    int argc;               // number of the arg
    int changeStdIn;        // check whether the command change fd0
    int changeStdOut;       // check whether the command change fd1
    char* inFile;           // if change fd0, the input file
    char* outFile;          // if change fd1, the output file
} Command;

typedef struct CommandLine {
    int num;                // total number of command
    Command** commands;     // commands array
} CommandLine;

// init command
Command* initCommand(int argc, char argv[15][15]) {
    Command* cmd = (Command*) malloc(sizeof(Command));
    cmd->argc = 0;
    cmd->argv = (char**) malloc(argc * sizeof(char*));
    cmd->changeStdIn = 0;
    cmd->changeStdOut = 0;
    cmd->inFile = NULL;
    cmd->outFile = NULL;

    int i;
    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "<")) {
            cmd->changeStdIn = 1;
            i++;
            cmd->inFile = (char*) malloc(15 * sizeof(char));
            strcpy(cmd->inFile, argv[i]);
            continue;
        }
        if (!strcmp(argv[i], ">")) {
            cmd->changeStdOut = 1;
            i++;
            cmd->outFile = (char*) malloc(15 * sizeof(char));
            strcpy(cmd->outFile, argv[i]);
            continue;
        }
        cmd->argv[cmd->argc] = (char*) malloc(15 * sizeof(char));
        strcpy(cmd->argv[cmd->argc], argv[i]);
        cmd->argc++;
    }
    // add end of each command, avoid bad address
    cmd->argv[cmd->argc] = NULL;
    
    return cmd;
}

// init command line
CommandLine* initCommandLine(const int* num, char (*cmd)[15][15]) {
    CommandLine* cl = (CommandLine*) malloc(sizeof(CommandLine));
    
    int i;
    for (i = 0; num[i]; i++) {}
    cl->num = i;

    cl->commands = (Command**) malloc(sizeof(Command*) * cl->num);

    for (i = 0; i < cl->num; i++) {
        cl->commands[i] = initCommand(num[i], cmd[i]);
    }
    return cl;
}

int isOpt(char c) {
    return c == '>' || c == '<' || c == '|';
}

void parse(char* line, int* argc, char (*argv)[100]) {
    // get the command splited by space
    // special char, <, > and | need to be separated as single opt
    
    /*
     * for each char:
     *      if cur is not ' ' and prev char is ' ' or one of >, <, |
     *      if cur is begining of the line
     *          begining of a new cmd
     *      if cur is '>' || '<' || '|'
     *          single char cmd
     *      if cur is not ' ' and next char is ' ' or one of >, <, |
     *      if cur is the tail of the line
     *          end of a new cmd
     *      if cur is ' '
     *          continue
     */
    
    int i, j, start = 0;
    for (i = 0; line[i]; i++) {
        if (isOpt(line[i])) {
            argv[*argc][0] = line[i];
            (*argc)++;
            continue;
        }
        if (line[i] == ' ') {
            continue;
        }
        if (i == 0 || line[i] != ' ' && (line[i - 1] == ' ' || isOpt(line[i - 1]))) {
            start = i;
        }
        if (!line[i + 1] || line[i] != ' ' && (line[i + 1] == ' ' || isOpt(line[i + 1]))) {
            for (j = start; j <= i; j++) {
                argv[*argc][j - start] = line[j];
            }
            argv[*argc][j - start] = '\0';
            (*argc)++;
        }
    }    
}

void splitPipe(int* argc, char (*argv)[100], int num[15], char (*cmd)[15][15]) {
    int i, j, k, start = 0, cnt = 0;
    
    for (i = 0; i < *argc; i++) {
        if (!strcmp(argv[i], "|")) {
            for (j = start, k = 0; j < i; j++, k++) {
                strcpy(cmd[cnt][k], argv[j]);
            }
            num[cnt] = k;
            cnt++;
            start = i + 1;
        }
    }
    
    for (j = start, k = 0; j < *argc; j++, k++) {
        strcpy(cmd[cnt][k], argv[j]);
    }
    num[cnt] = k;
    cnt++;
    *argc = cnt;
}

void executeCommand(int in, int out, Command* cmd) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (!pid) {
        // child process
        if (in != 0) {
            // if stdin is changed, set in as fd0
            dup2(in, 0);
            // close in
            close(in);
        }
        if (out != 1) {
            // if stdout is changed, set out as fd1
            dup2(out, 1);
            // close out
            close(out);
        }
        // execute command
        execvp(cmd->argv[0], cmd->argv);
        perror("EXECV ERROR");
        exit(1);
    } else {
        // parent process
        wait(NULL);
    }
    return ;
}

void executeCommandLine(CommandLine* cl) {
    int i;
    pid_t pid;
    int in = 0, fd[2];
    if (cl->commands[0]->changeStdIn) {
        in = open(cl->commands[0]->inFile, O_RDONLY);
        if (in < 0) {
            perror("OPEN ERROR");
        }
    }
    for (i = 0; i < cl->num - 1; i++) {
        // executeCommand(cl->commands[i]);
        if (pipe(fd) < 0) {
            perror("pipe");
            exit(1);
        }
        executeCommand(in, fd[1], cl->commands[i]);
        // close write end on parent process
        close(fd[1]); 
        in = fd[0]; 
    }
    int out = 1;
    if (cl->commands[i]->changeStdOut) {
        out = open(cl->commands[i]->outFile, O_WRONLY | O_CREAT);
        if (out < 0) {
            perror("OPEN ERROR");
        }
    }
    executeCommand(in, out, cl->commands[i]);
}

/*  for each command:
 *      case 1: fd0 is stdin, fd1 is stdout
 *      case 2: fd0 is stdin, fd1 is file
 *      case 3: fd0 is stdin, fd1 is pipe
 *      case 4: fd0 is file, fd1 is stdout
 *      case 5: fd0 is file, fd1 is file
 *      case 6: fd0 is file, fd1 is pipe
 *      case 7: fd0 is pipe, fd1 is stdout
 *      case 8: fd0 is pipe, fd1 is file
 *      case 9: fd0 is pipe, fd1 is pipe
 *
 */

int execute(int argc, int num[15], char (*cmd)[15][15]) {
    // assume we only have at most one pipe here
    int i, j;
    int isFileIn = 0, isFileOut = 0;
    char argv[50][50];
    if (argc == 1) {
        for (i = 0; i < num[0]; i++) {
            if (!strcmp(cmd[0][i], "<")) {
                isFileIn = 1;
            }
            if (!strcmp(cmd[0][i], ">")) {
                isFileOut = 1;
            }
        }
        // use child process to execute the cmd
        char** argv = (char**) malloc((argc + 1) * sizeof(char*));
        int pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }
        if (pid) {
            free(argv);
            wait(NULL);
        } else {
            
            for (i = 0; i < num[0]; i++) {
                argv[i] = (char*) malloc(sizeof(char) * (1 + strlen(cmd[0][i])));
                strcpy(argv[i], cmd[0][i]);
            }
            
            execvp(cmd[0][0], argv);
            // if exec fail, free argv
            perror("Exec");
            for (i = 0; i < num[0]; i++) {
                free(argv[i]);
            }
            free(argv);
            exit(1);
        }
    }
}

int main() {
    
    char line[255] = {0};
    while (1) {
        printf("dzwtom@rsh ;) ");
        scanf(" %[^\n]", line);
        if (!strcmp(line, "quit")) {
            printf("Good bye!\n");
            return 0;
        }
        int argc = 0;
        char argv[100][100] = {0};
        parse(line, &argc, argv);
        char cmd[15][15][15] = {0};
        int num[15] = {0};
        splitPipe(&argc, argv, num, cmd);
        CommandLine* cl = initCommandLine(num, cmd);
        executeCommandLine(cl);
        // TODO: Free Comand Line
    }

    return 0;
}
