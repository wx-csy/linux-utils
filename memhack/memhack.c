#if !(defined __linux__ || defined __linux || defined linux)
# error "this utility is for linux only"
#endif

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE   500

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>

void die(const char *msg) {
  perror(msg);
  exit(EXIT_FAILURE);
}

int pid;

void usage() {
  printf("Usage: memhack PID\n"
         "Trace the process specified by PID and hack its memory :)\n");
  exit(EXIT_FAILURE);
}

#define MAX_MEM_POS 1024 * 1024 * 8

int cand_cnt;
long cand_addr[MAX_MEM_POS];
int initialized;

void initialize(uint32_t value) { 
  FILE *fmaps, *fmem;
  char path[256];
  cand_cnt = 0;
  sprintf(path, "/proc/%d/maps", pid);
  fmaps = fopen(path, "r");
  if (fmaps == NULL) die("fopen()");
  sprintf(path, "/proc/%d/mem", pid);
  fmem = fopen(path, "rb");
  if (fmem == NULL) die("fopen()");
  long ptr_from, ptr_to;
  char prot[8];
  while (fscanf(fmaps, 
        "%lx-%lx %s %*s %*s %*s %*s", 
        &ptr_from, &ptr_to, prot) == 3) {
    if (prot[0] == 'r' && prot[1] == 'w') {
      if (fseek(fmem, ptr_from, SEEK_SET) == -1) die("fseek()");
      for (long pos = ptr_from; pos < ptr_to; pos += 4) {
        uint32_t rval;
        if (fread(&rval, 4, 1, fmem) != 1) die("fread()");
        if (rval == value) {
          cand_addr[cand_cnt++] = pos;
          if (cand_cnt >= MAX_MEM_POS) {
            printf("Too many candidate positions.\n"
                "Please rechoose a lookup number.\n");
            return;
          }
        }
      }
    }
  }
  fclose(fmaps);
  fclose(fmem);
  initialized = 1;
}


void filter(uint32_t value) {
  char path[256];
  FILE *fmem;
  sprintf(path, "/proc/%d/mem", pid);
  fmem = fopen(path, "rb");
  if (fmem == NULL) die("fopen()");
  int newpos = 0;
  for (int i = 0; i < cand_cnt; i++) {
    long pos = cand_addr[i];
    uint32_t rval;
    if (fseek(fmem, pos, SEEK_SET) == -1) die("fseek()");
    if (fread(&rval, 4, 1, fmem) != 1) die("fread()");
    if (rval == value) cand_addr[newpos++] = pos;
  }
  cand_cnt = newpos;
  fclose(fmem);
}

void setup(uint32_t value) {
  char path[256];
  FILE *fmem;
  sprintf(path, "/proc/%d/mem", pid);
  fmem = fopen(path, "wb");
  if (fmem == NULL) die("fopen()");
  for (int i = 0; i < cand_cnt; i++) {
    long pos = cand_addr[i];
    if (fseek(fmem, pos, SEEK_SET) == -1) die("fseek()");
    if (fwrite(&value, 4, 1, fmem) != 1) die("fwrite()");
  }
  printf("Done! %d position(s) written.\n", cand_cnt);
  fclose(fmem);
}

int paused = 0;

int main(int argc, char *argv[]) {
  if (argc < 2) usage();
  if (sscanf(argv[1], "%d", &pid) < 1) usage();
  char buf[256], cmd[256];
  printf("Welcome to memhack!\n");
  while (1) {
    int pos;
    printf("> ");
    fflush(stdout);
    if (fgets(buf, sizeof buf, stdin) == NULL) perror("fgets()");
    sscanf(buf, "%s%n", cmd, &pos);
    if (strcmp(cmd, "pause") == 0) {
      if (paused) {
        printf("The tracee has already been paused!\n");
      } else {
        if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) == -1) 
          perror("ptrace.attach");
        else {
          wait(NULL);
          paused = 1;
        }
      }
    } else if (strcmp(cmd, "resume") == 0) {
      if (!paused) {
        printf("The tracee is already running!\n");
      } else {
        if (ptrace(PTRACE_DETACH, pid, NULL, NULL) == -1) 
          perror("ptrace.detach");
        else
          paused = 0;
      }   
    } else if (strcmp(cmd, "lookup") == 0) {
      if (!paused) {
        printf("The tracee has not been paused!\n");
      } else {
        uint32_t value;
        char *endpos;
        value = strtoul(buf + pos, &endpos, 0);
        if (*endpos && !isspace(*endpos)) {
          printf("Please type a correct argument!\n");  
        } else {
          if (initialized) {
            filter(value);
          } else {
            initialize(value);
          }
          if (initialized) {
            if (cand_cnt == 0) {
              printf("No candidate found. Reset the candidate list.\n");
              initialized = 0;
            } else {
              for (int i=0; i<cand_cnt; i++) {
                printf("(0x%lx)\n", cand_addr[i]);
                if (i >= 10) {
                  printf("...\n");
                  break;
                }
              } 
            }
          }
        }
      }
    } else if (strcmp(cmd, "setup") == 0) {
      if (!paused) {
        printf("The tracee has not been paused!\n");
      } else {
        uint32_t value;
        char *endpos;
        value = strtoul(buf + pos, &endpos, 0);
        if (*endpos && !isspace(*endpos)) {
          printf("Please type a correct argument!\n");
        } else if (!initialized) {
          printf("Candidate list has not been initialized!\n");
        } else {       
          setup(value);
        } 
      }
    } else if (strcmp(cmd, "exit") == 0) {
      exit(EXIT_SUCCESS);
    } else {
      printf("Unrecognized command: %s\n", cmd);
    }
  }
  return 0;
}

