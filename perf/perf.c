#if !(defined __linux__ || defined __linux || defined linux)
# error "this utility is for linux only"
#endif

#ifdef __STRICT_ANSI__
# undef __STRICT_ANSI__   // enable some POSIX-specific functions
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <float.h>
#include <time.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

int pid;
int pipefd[2];

void die(const char *msg) {
  perror(msg);
  exit(EXIT_FAILURE);
}

void usage() {
  printf("Usage: perf command [arg]...\n");
  exit(EXIT_FAILURE);
}

char *freadline(FILE *stream) {
  int cap = 64, size = 0;
  char *buf = malloc(cap);
  if (buf == NULL)
    die("malloc()");
  while (1) {
    while (size < cap) {
      buf[size] = fgetc(stream);
      if (buf[size] == EOF) {
        free(buf);
        return 0;
      }
      if (buf[size] == '\n') {
        buf[size] = 0;
        return buf;
      }
      size++;
    }
    cap = cap * 2;
    if ((buf = realloc(buf, cap)) == NULL)
      die("realloc()");
  }
}

typedef struct syscall_rcd {
  char name[64];
  double time;
  uint32_t count;
} syscall_rcd;

int syscall_rcd_cmp_time(const void *lhs, const void *rhs) {
  const syscall_rcd *rcd1 = lhs, *rcd2 = rhs;
  if (rcd1->time > rcd2->time) return -1;
  if (rcd1->time < rcd2->time) return 1;
  return 0;
}

double total_syscall_time = DBL_MIN; // avoid divide by zero

#define vector(typename) struct { \
  typename *item; \
  size_t size, capacity; \
}

#define vector_ctor(name) do { \
  name.size = 0; \
  name.capacity = 16; \
  name.item = malloc(name.capacity * sizeof(name.item[0])); \
  if (name.item == NULL) die("malloc()"); \
} while (0)

#define vector_push(name) do { \
  if (name.size == name.capacity) { \
    name.capacity += name.capacity >> 1 | 1; \
    name.item = realloc(name.item, name.capacity * sizeof(name.item[0])); \
    if (name.item == NULL) die("realloc()"); \
  } \
  name.size++; \
} while (0)

#define vector_top(name) (name.item[name.size - 1])

#define vector_dtor(name) (free(name.item))

vector(syscall_rcd) syscalls;

void add_syscall_time(const char *name, double time) {
  total_syscall_time += time;
  for (int i = 0; i < syscalls.size; i++) {
    if (strcmp(syscalls.item[i].name, name) == 0) {
      syscalls.item[i].time += time;
      syscalls.item[i].count++;
      return;
    }
  }
  vector_push(syscalls);
  strcpy(vector_top(syscalls).name, name);
  vector_top(syscalls).time = time;
  vector_top(syscalls).count = 1;
}

enum ESC_COLOR {
  ESC_BLACK = 30, ESC_RED = 31,     ESC_GREEN = 32, ESC_YELLOW = 33,
  ESC_BLUE = 34,  ESC_MAGENTA = 35, ESC_CYAN = 36,  ESC_WHITE = 37,
  ESC_BACKGROUND = 10, ESC_BRIGHT = 60
};

inline void esc_reset() {
  printf("\033c");
}

inline void esc_clear() {
  printf("\033[2J");
}

inline void esc_setpos(int x, int y) {
  printf("\033[%d;%dH", x, y);
}

inline void esc_resetattr() {
  printf("\033[0m");
}

inline void esc_setcolor(int color) {
  printf("\033[%dm", color);
}

inline void esc_linewrap(int action) {
  printf("\033[?7%c", action ? 'h' : 'l');
}

void draw(int force) {
  static int drawn = 0;
  static time_t ltime;
  if (!drawn) {
    drawn = 1;
    ltime = time(NULL);
  } else {
    if (!force && time(NULL) == ltime) return;
    ltime = time(NULL);
  }
  qsort(syscalls.item, syscalls.size, sizeof(syscall_rcd),
    syscall_rcd_cmp_time);
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  esc_clear();
  esc_setpos(1, 1);
  esc_linewrap(0);
  esc_setcolor(ESC_BLUE + ESC_BACKGROUND);
  printf(" overhead |   time(s)   |   count   | syscall          \n");
  esc_resetattr();
  for (int i = 0; i < syscalls.size; i++) {
    if (i == w.ws_row - 2) break;
    double perc = syscalls.item[i].time / total_syscall_time * 100;
    int color;
    if (perc > 10.0) color = ESC_RED + ESC_BRIGHT;
    else if (perc > 1.0) color = ESC_YELLOW;
    else color = ESC_GREEN + ESC_BRIGHT;
    esc_setcolor(color);
    printf("%8.4f%%  ", perc);
    esc_setcolor(ESC_WHITE);
    printf("%12.6f  ", syscalls.item[i].time);
    printf("%10u   ", syscalls.item[i].count);
    printf("%-15.15s\n", syscalls.item[i].name);
  }
  esc_linewrap(1);
  esc_resetattr();
  fflush(stdout);
}

void parent(const char *procname) {
  int ptr, len;
  FILE *fstrace;
  char *msg, *pos;
  char syscall_name[64];
  double syscall_time;
  if (close(pipefd[1]) == -1) // parent closes the writing end of pipe
    die("close()");
  fstrace = fdopen(pipefd[0], "r"); // bind the pipe to a file stream
  if (fstrace == NULL)
    die("fdopen()");
  
  vector_ctor(syscalls);
  while ((msg = freadline(fstrace)) != NULL) {
    len = strlen(msg);
    switch (msg[0]) {
      case 0: break;
      case '-': // a signal received;
        break;
      case '+': // program exited;
        break;
      case '*': // tracer's error
        ptr = 0;
        while (msg[ptr] && msg[ptr] != ':') ptr++;
        if (msg[ptr] == 0) break;
        printf("%s%s\n", procname, msg + ptr);
        exit(EXIT_FAILURE);
      default:
        if (!isalpha(msg[0]) && msg[0] != '_') break;
        pos = strchr(msg, '(');
        if (pos == NULL) break;
        *pos = 0;
        strcpy(syscall_name, msg);
        if (msg[len - 1] != '>') break;
        pos = msg + len;
        while (*pos != '<') {
          pos--;
          if (pos <= msg) break;
        }
        if (sscanf(pos + 1, "%lf", &syscall_time) != 1) break;
        add_syscall_time(syscall_name, syscall_time);
    }
    free(msg);
    draw(0);
  }

  draw(1);
  waitpid(pid, NULL, 0);
  printf("The tracee has been terminated. Press <return> to exit.");
  getchar();
  esc_reset();
  fclose(fstrace);
  vector_dtor(syscalls);
}

void child(int argc, char *argv[], char *envp[]) {
  int fdnull;
  char strace_name[] = "*strace", // mangle the tracer's name
       strace_show_time[] = "-T";
  char **argv_c;
  if ((argv_c = malloc((argc + 1) * sizeof(char*))) == NULL)
    die("malloc()");
  argv_c[0] = strace_name;
  argv_c[1] = strace_show_time;
  memcpy(argv_c + 2, argv + 1, argc * sizeof(char*));
  if (close(pipefd[0]) == -1) // child closes the reading end of pipe
    die("close()");
  if ((fdnull = open("/dev/null", O_WRONLY)) == -1)
    die("open()");
  if (dup2(fdnull, STDOUT_FILENO) == -1) // redirect stdout to null device
    die("dup2()");
  if (close(fdnull) == -1)
    die("close()");
  if (dup2(pipefd[1], STDERR_FILENO) == -1) // redirect stderr to pipe
    die("dup2()");
  if (close(pipefd[1]) == -1)
    die("close()");
  
  
  if (execve("/usr/bin/strace", argv_c, envp) == -1)
    die("execve()");
}

int main(int argc, char *argv[], char *envp[]) {
  if (argc < 2) usage();
  if (argv[1][0] == '-') usage();

  if (pipe(pipefd) == -1) // create an anonymous pipe
    die("pipe()");

  pid = fork();
  if (pid == -1)
    die("fork()");
  else if (pid == 0)
    child(argc, argv, envp);
  else
    parent(argv[0]);
}

