#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>

#define MAX_CMD_LEN  128
#define HISTORY_BUFFER 20
#define MAX_ARGS_SIZE 20
#define CMD_HIS "history"
#define CMD_EXIT "exit"
#define INPUT_KEY "<"
#define OUTPUT_KEY ">"
#define PIPE_KEY "|"
#define PROMPT "osh>"
#define TOKEN " "
#define END_STR '\0'
#define IS_IN_CHILD(x) x == 0
#define IS_IN_PARENT(x) x > 0
#define OUTPUT_MODE O_RDWR | O_CREAT | S_IRWXU
#define INPUT_MODE O_RDONLY

enum MSG {
   ENTER_FIRST_CMD,
   HIS_EMPTY,
   PS_NO_CREATE,
   CMD_NO_EXEC,
   FAIL_CREATE_FILE,
   FAIL_OPEN_FILE
};

const char* msg[] = {
   "Error! Please enter first command!",
   "No commands in history!",
   "Failed to create process!",
   "Failed to execute command!",
   "Error creating output file!",
   "No input file!"
};

//Print history list
int history(char hist[HISTORY_BUFFER][MAX_CMD_LEN], int current) {
   //Use 'check' to check if history list is empty
   int check = 0;
   for (int i = 0; i < HISTORY_BUFFER; i++)
      if (hist[i][0] == END_STR)
         check++;

   if (check == HISTORY_BUFFER - 1)
      puts(msg[HIS_EMPTY]);
   else {
      int i = current;
      int hist_num = 1;
      do {
         if (hist[i][0] != END_STR) {
            printf("%4d  %s\n", hist_num, hist[i]);
            hist_num++;
         }
         i = (i + 1) % HISTORY_BUFFER;
      } while (i != current);
   }
   return 0;
}

//Clear history
int clear_history(char hist[HISTORY_BUFFER][MAX_CMD_LEN]) {
   for (int i = 0; i < HISTORY_BUFFER; i++) {
      hist[i][0] = END_STR;
   }
   return 0;
}

//Parsing command
int parsing(char command[MAX_CMD_LEN], char* args[MAX_ARGS_SIZE], int* args_len) {
   char* rest = command;
   int i = 0;

   while(args[i] = strtok_r(rest, " ", &rest)) {
      i++;
   }

   args[i] = NULL;
   *args_len = i;

   return 0;
}

//Find position of key in array
int find_key(char* args[], int args_len, const char key[]) {
   for (int i = 0; i < args_len; i++) {
      if (args[i] == NULL) {
         continue;
      }
      if (strcmp(args[i], key) == 0) {
         return i;
      }
   }
   return -1;
}

//Operator >
int output_file(char filename[]) {
   int fd = creat(filename, OUTPUT_MODE);

   if (fd < 0) {
      puts(msg[FAIL_CREATE_FILE]);
      exit(0);
   }

   dup2(fd, STDOUT_FILENO);
   return 0;
}


//Operator <
int input_file(char filename[])
{
   int fd = open(filename, INPUT_MODE);

   if (fd < 0) {
      puts(msg[FAIL_OPEN_FILE]);
      exit(0);
   }

   dup2(fd, STDIN_FILENO);
   return 0;
}

//Piping
int execute_via_pipe(char* args[], int args_len) {
   char* args2[MAX_ARGS_SIZE];
   int fd[2];
   int i;

   if (pipe(fd) < 0) {
      exit(1);
   }

   int pid2 = fork();

   //Child process 1
   if (IS_IN_CHILD(pid2)) {
      dup2(fd[0], STDIN_FILENO);
      close(fd[0]);
      close(fd[1]);

      for(i = 0; i < args_len; i++)
      {
         args2[i] = args[args_len - 1 + i];
      }
      args2[i] = NULL;

      execvp(args[args_len - 1], args2);
      puts(msg[CMD_NO_EXEC]);
      exit(1);
   }
   //Parent process 1
   else if (IS_IN_PARENT(pid2)) {
      pid_t pid2_child = fork();
      //Child process 2
      if (IS_IN_CHILD(pid2_child)) {
         dup2(fd[1], STDOUT_FILENO);
         close(fd[1]);
         close(fd[0]);
         args[args_len - 2] = NULL;
         execvp(args[0], args);
         puts(msg[CMD_NO_EXEC]);
         exit(1);
      }
      //Parent process 2
      else if (IS_IN_PARENT(pid2_child)) {
         int status;
         close(fd[0]);
         close(fd[1]);
         waitpid(pid2, &status, 0);
      }
      //Error - process 2
      else {
         puts(msg[PS_NO_CREATE]);
      }

      wait(NULL);
   }
   //Error - process 1
   else {
      puts(msg[PS_NO_CREATE]);
   }

   return 0;
}

//Executing
void execute_command(char* args[MAX_ARGS_SIZE], int args_len) {
   int key_pos;
   // Output file
   if (args_len > 2 && (key_pos = find_key(args, args_len, OUTPUT_KEY)) > -1) {
      output_file(args[key_pos + 1]);
      args[key_pos] = NULL;
   }
   //Input file
   if (args_len > 2 && (key_pos = find_key(args, args_len, INPUT_KEY)) > -1) {
      input_file(args[key_pos + 1]);
      args[key_pos] = NULL;
   }
   //Pipe
   if (args_len > 2 && (key_pos = find_key(args, args_len, PIPE_KEY)) > -1) {
      execute_via_pipe(args, args_len);
   }
   //Nornal command
   execvp(args[0], args);
   puts(msg[CMD_NO_EXEC]);
   exit(1);
}


int main() {
   char command[MAX_CMD_LEN];
   char hist[HISTORY_BUFFER][MAX_CMD_LEN] = {"\0"};
   char* args[MAX_ARGS_SIZE];
   int current = 0;
   int args_len = 0;
   int running = 1;
   pid_t pid;

   while (running) {
      printf(PROMPT);
      fflush(stdout);

      //Enter command
      fgets(command, MAX_CMD_LEN, stdin);

      //Case 1: command empty with enter
      if (strlen(command) == 1 && command[0] == '\n') {
         //First position of hist list
         if (current == 0){
            //hist list is not full
            if (hist[HISTORY_BUFFER - 1][0] == END_STR)
               puts(ENTER_FIRST_CMD);
            //hist list is full
            else {
               strcpy(hist[current], hist[HISTORY_BUFFER - 1]);
               strcpy(command, hist[HISTORY_BUFFER - 1]);
               printf("%s\n", hist[current]);
            }
         }
         //Not first position in hist list
         else {
            strcpy(hist[current], hist[current - 1]);
            strcpy(command, hist[current - 1]);
            printf("%s\n", hist[current]);
         }
      }
      //Case 2: normal command
      else {
         //Replace last char with '\0'
         command[strlen(command) - 1] = END_STR;
         strcpy(hist[current], command);
      }


      //If command = "history"
      if (strcmp(command, CMD_HIS) == 0)
         history(hist, current + 1);
      //Terminated
      else if (strcmp(command, CMD_EXIT) == 0)
         break;
      else {
         //Parsing command to args array
         parsing(command, args, &args_len);

         //Duplicate process
         pid = fork();

         //Check process
         if (IS_IN_CHILD(pid)) {
            execute_command(args, args_len);
         }
         else if (IS_IN_PARENT(pid)) {
            //Wait child process to be terminated
            wait(NULL);
         }
         else {
            puts(msg[PS_NO_CREATE]);
         }
      }

      current = (current + 1) % HISTORY_BUFFER;
   }

   //Delete history list after running program
   clear_history(hist);
   return 0;
}
