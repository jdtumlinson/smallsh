#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

/*
Things to get done:
    1: Command prompt                                                   DONE
    2: Handle blank lines and comments (using # character)              DONE
    3: Expanision for the variable $$                                   DONE
    4: Commands 'exit', 'cd' and 'status'                               DONE
    5: Execute other commands using the exec family of functions        DONE
    6: Support input and output redirection                             DONE
    7: Support running commands in foreground and background proceesses DONE
    8: Implement custom handlers for 2 singals, SIGINT and SIGTSTP      DONE
    9*: Commenting                                                      DONE
*/

//Variable used for dealing with the use of a signal handle needing to change something
//As signal handlers are already global, I hope that this is okay as I did not see another way
int allowBG = 0;



//Basic struct containing the parsed command
struct parCommand {
    char *argv[512];        //The command itself and its arguments (capped at 512)
    int inputFlag;          //Flag to signify that there is anS input file
    char inputF[1000];      //An input file if any
    int outputFlag;         //Flag to signify that there is an output file
    char outputF[1000];     //An output file if any
    int background;         //Flag for running in background
};



/*
    Function: pStatus()
    Arguments: 
        -   int childStatus: the return value of the child exit status
    Description: prints the exit status of a child process
*/
void pStatus(int childStatus) {
    //If WIFEXITED returns true, then process was exited by a signal
    if(WIFEXITED(childStatus)) {
        printf("exited by signal %d\n", WEXITSTATUS(childStatus));
        fflush(stdout);     //Clear STDOUT
    }
    else {
        printf("terminated by signal %d\n", WTERMSIG(childStatus)); //If not, it was terminated
        fflush(stdout);     //Clear STDOUT
    }    
    

    return; //END
}



/*
    Function: getInput()
    Arguments: 
        -   struct parCommand *input: pointer to a defined struct to be filled with given information
        -   int pid: the process ID
    Description: Requests input from the user, editing the values of a given struct with the information
                 from the parsed input
*/
void getInput(struct parCommand *input, int pid) {
    char userInput[2048];                   //a given command cannot be longer then 2048 characters
    
    //Get the input from the user
    printf(": ");
    fflush(stdout);
    fgets(userInput, 2048, stdin);

    //If the input is just a new line character, do nothing and return
    if(strcmp(userInput, "\n") == 0) return;

    //Variables to iterate through the user input and switch the new line character to an end line
    int got = 0;    //0 = not found; 1 = found
    int temp = 0;   //Iterator

    //Loop through the string, checking if the character is the new line or not. Change if it is
    while(got == 0) {
        if(userInput[temp] == '\n') {
            userInput[temp] = '\0';
            got = 1;
        }
        temp++;
        if(temp >= 2048) got = 1;
    }

    int i = 0;                          //Iterator
    char *ptr = strtok(userInput, " "); //First token

    //Loop through the string, tokenizing it, and adding it to its correct variable in the struct
    while(ptr != NULL) {    //NULL is end of the string
        //If and ampersand is found at the end of the string, then mark the background flag
        if(strcmp(ptr, "&") == 0) {
            input->background = 1;
        }
        //If '<' is found then copy the next token to the input file variable
        else if(strcmp(ptr, "<") == 0) {
            ptr = strtok(NULL, " ");
            strcpy(input->inputF, ptr);
            input->inputFlag = 1;
        }
        //Just like above but with '>' and output
        else if(strcmp(ptr, ">") == 0) {
            ptr = strtok(NULL, " ");
            strcpy(input->outputF, ptr);
            input->outputFlag = 1;
        }
        //If it is not any of those, then it is part of the command (command itself or an argument)
        else {
            //Duplicate the string
            input->argv[i] = strdup(ptr);

            //Check the string for variable expansion
            for(int j = 0; j < strlen(input->argv[i]) - 1; j++) {
                if(input->argv[i][j] == '$' && input->argv[i][j + 1] == '$') {
                    //If '$$' is found, then...
                    int length = strlen(input->argv[i]);    //Length of the string
                    char pidStr[50];                        //pid as string
                    sprintf(pidStr, "%d", pid);             //Make the pid a string
                    int pidLen = strlen(pidStr);            //Get the length of the pid string
                    
                    //Iterate from the back of the string, placing each character 'pidLen' 
                    //(-2 to account for removing '$$') characters ahead
                    for(int k = length; k >= j + 2; k--) input->argv[i][k + pidLen - 2] = input->argv[i][k];
                    //Place the process ID in the correct spots -- from 'j' to pidLen
                    for(int k = 0; k < pidLen; k++) input->argv[i][k + j] = pidStr[k];
                }
            }
        }
        i++;                        //Iterator for the loop
        ptr = strtok(NULL, " ");    //New token
    }

    return; //END
}



/*
    Function: executeCommand()
    Arguments: 
        -   struct parCommand *input: pointer to a defined struct to be filled with given information
        -   int *childStatus: a pointer to the exit status of the child process
    Description: based on the information in the struct 'command' this function will execute the 
                 appropriate command using a given input or output location or running in the 
                 fore/background
*/
void executeCommand(struct parCommand *command, int *childStatus) {
    pid_t spawnPid = fork();            //Fork the process

    //Dup standard in and out so that we can switch back to them
    int stdinDesc = dup(STDIN_FILENO);
    int stdoutDesc = dup(STDOUT_FILENO);

    //Switch statment
    switch(spawnPid) {
    case -1:
        perror("fork()\n");
        exit(1);
        break;
    case 0:     //CHILD PROCESS
        //If input file is not empty, then open the given file (if it exists) and switch the file discriptors
        if(command->inputFlag == 1) {
            int inputFile = open(command->inputF, O_RDONLY);
            if(inputFile == -1) {
                perror("Unable to open input file");
                exit(1);
            }
            
            fflush(stdin);
            int result = dup2(inputFile, STDIN_FILENO);
            if(result == -1) {
                perror("Unable to assign the input file");
                exit(2);
            }
            fcntl(inputFile, F_SETFD, FD_CLOEXEC);
        }

        //Just like above, but for a given output destination
        if(command->outputFlag == 1) {
            int outputFile = open(command->outputF, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if(outputFile == -1) {
                perror("Unable to open output file");
                exit(1);
            }

            fflush(stdout);
            int result = dup2(outputFile, STDOUT_FILENO);
            if(result == -1) {
                perror("Unable to assign the output file");
                exit(2);
            }
            fcntl(outputFile, F_SETFD, FD_CLOEXEC);
        }

        //Execute the command
        execvp(command->argv[0], (char* const*)command->argv);
        perror("execvp");   //Only gets here is there was an error
        exit(2);
        break;
    default:    //PARENT PROCESS
        //If both allow background has not changed and background has been flaged, then run in the background 
        //and print the process ID
        if(allowBG == 0 && command->background == 1) {
            pid_t childPid = waitpid(spawnPid, childStatus, WNOHANG);
            printf("background pid is %d\n", spawnPid);
            fflush(stdout);
        }
        else spawnPid = waitpid(spawnPid, childStatus, 0);     //Else, we wait for the child process to end
        break;
    }

    //See if the child process has terminated
    while((spawnPid = waitpid(-1, childStatus, WNOHANG)) > 0) {
        printf("background pid %d is done: ", spawnPid);
        fflush(stdout);
        pStatus(*childStatus);
    }

    //Now we switch back our input and output destinations to standard in and out
    fflush(stdout);
    dup2(stdinDesc, STDIN_FILENO);
    dup2(stdoutDesc, STDOUT_FILENO);
    close(stdinDesc);
    close(stdoutDesc);

    return; //END
}



/*
    Function: handle_SIGINT()
    Arguments: 
        -   int signo
    Description: function that handles the use of ^C. More or less does nothing
*/
void handle_SIGINT(int signo) {
    char* message = "\n";               
    write(STDOUT_FILENO, message, 1);   //Prints a new line character
    fflush(stdout);

    return; //END
}



/*
    Function: handle_SIGTSTP()
    Arguments: 
        -   int signo
    Description: function that handles the use of ^Z. Switches between using and not using
                 foreground-only mode
*/
void handle_SIGTSTP(int signo) {
    //More or less just a swap function for allowBG
    //As signal handles cannot be given additional arguments, I used a global variable which is not good
    //practice, but as a signal handle is already global, I hope this is acceptable
    if(allowBG == 0) {
        char* message = "\nEntering foreground-only mode (& is not ignored)\n";
        write(STDOUT_FILENO, message, 50);
        allowBG = 1;
    }
    else {
        char* message = "\nExiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 30);
        allowBG = 0;
    }

    return; //END
}



/*
    Function: cleanStruct()
    Arguments: N/A
    Description: returns a clean and fresh parCommand struct object for use with all value initialized
*/
struct parCommand cleanStruct() {
    struct parCommand temp;                             //Declare a new struct
    for(int i = 0; i < 512; i++) temp.argv[i] = NULL;   //Set all variables in the array to NULL
    
    //Set the first character to '\n' to signify the string is empty
    temp.inputF[0] = '\n';
    temp.outputF[0] = '\n';

    temp.inputFlag = 0;
    temp.outputFlag = 0;

    //Set the initial value to 0 (false)
    temp.background = 0;

    return temp; //END
}



/*
    Function: main()
    Arguments: 
        -   int argc
        -   char *argv[]
    Description: main function for the file
*/
int main(int argc, char *argv[]) {
    //SIGINT Handler
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = handle_SIGINT;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);

    //SIGTSTP Handler
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = 0;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    //Variables for the overall program
    struct parCommand input;        //Struct that contains the parsed command
    int pid = getpid();             //Process ID of the parent process
    char cwd[PATH_MAX];                 //The current working directory (I'm assuming it won't be longer then 1000 characters)
    int childStatus;                //Integer to hold the exit status of a child process
    int end = 0;                    //Flag to signify the end of the loop

    //Loop for the program
    while(end == 0) {
        //Clean the struct so it's empty of any previous results/garbage
        input = cleanStruct();

        //Get the input from the user
        getInput(&input, pid);
        
        //If the first argument is empty, then do nothing
        if(input.argv[0] == NULL) continue;

        //If the first argument starts with '#' or '\0' then it is a comment or nothing
        else if(input.argv[0][0] == '#' || input.argv[0][0] == '\0') continue;
        
        //If the first argument is exit, then we can end the program by marking the flag
        else if(strcmp(input.argv[0], "exit") == 0) end = 1;
        
        //if the first argument is cd, then we will attempt to change the directory
        else if(strcmp(input.argv[0], "cd") == 0) {
            //If the second argument is blank, then we will navigate to the home directory
            if(input.argv[1] == NULL) chdir(getenv("HOME"));
            
            //Else, we will attempt to change the given directory if possible
            else if (chdir(input.argv[1]) == -1) {
                printf("cd: %s: No such file or directory \n", input.argv[1]);
                fflush(stdout);
            }
        }
        
        //If the first argument is status, then we will print the status of the last process
        else if(strcmp(input.argv[0], "status") == 0) pStatus(childStatus);
        
        //If it is none of those, then it is a command to be run with the exec family of functions
        else executeCommand(&input, &childStatus);
        input = cleanStruct();
    }

    return 0; //END
}