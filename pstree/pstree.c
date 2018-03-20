#if !(defined __linux__ || defined __linux || defined linux)
# error "this utility is for linux only"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>
#include <sys/types.h>
#include <dirent.h>

int opt_show_pids, opt_numeric_sort, opt_version;

void die(const char *msg) {
  perror(msg);
  exit(EXIT_FAILURE);
}

typedef struct process {
  pid_t pid, ppid;
  int is_thread, printed;
  char name[16];
} process;

int process_ctor(process *proc, pid_t pid, int is_thread) {
  char stat_filename[30];
  FILE* fd;
  char file_buf[4096];

  snprintf(stat_filename, sizeof stat_filename, "/proc/%d/stat", pid);
  if ((fd = fopen(stat_filename, "r")) == NULL) return 0;
  if (fgets(file_buf, sizeof file_buf, fd) == NULL) {
    fclose(fd);
    return 0;
  }
  fclose(fd);

  char *lpar = file_buf, *rpar = file_buf + strlen(file_buf);
  while (lpar < rpar && *lpar != '(') lpar++;
  while (rpar > lpar && *rpar != ')') rpar--;
  if (lpar >= rpar || *lpar != '(' || *rpar != ')') return 0;
  lpar++; *rpar = 0;
  strncpy(proc->name, lpar, sizeof proc->name);
  proc->name[sizeof(proc->name) - 1] = 0;
  rpar++;

  proc->pid = pid;
  proc->is_thread = is_thread;
  proc->printed = 0;
  if (is_thread) {
    if (sscanf(rpar, " %*c %*d %d", &proc->ppid) != 1) return 0;
  } else {
    if (sscanf(rpar, " %*c %d", &proc->ppid) != 1) return 0;
  }
  return 1;
}

int process_cmp_ppid_t(const void *lhs, const void* rhs) {
  pid_t ppid = *(pid_t*)lhs;
  const process *proc = rhs;
  if (ppid > proc->ppid) return 1;
  if (ppid < proc->ppid) return -1;
  return 0;
}

int process_cmp_nosort(const void *lhs, const void *rhs) {
  const process *proc1 = lhs, *proc2 = rhs;
  if (proc1->ppid < proc2->ppid) return -1;
  if (proc1->ppid > proc2->ppid) return 1;
  return strcmp(proc1->name, proc2->name);
}

int process_cmp_sort_pid(const void *lhs, const void *rhs) {
  const process *proc1 = lhs, *proc2 = rhs;
  if (proc1->ppid < proc2->ppid) return -1;
  if (proc1->ppid > proc2->ppid) return 1;
  if (proc1->pid < proc2->pid) return -1;
  if (proc1->pid > proc2->pid) return 1;
  return 0;
}

typedef struct stack {
  void *data;
  size_t element_size;
  size_t size, capacity;
} stack;

void stack_ctor(stack *s, size_t element_size) {
  s->size = 0;
  s->capacity = 16;
  s->element_size = element_size;
  if ((s->data = malloc(s->capacity * element_size)) == NULL)
    die("malloc()");
}

void* stack_access(stack *s, size_t pos) {
  return s->data + pos * s->element_size;
}

void stack_push(stack *s, const void *element) {
  if (s->size >= s->capacity) {
    s->capacity *= 2;
    if ((s->data = realloc(s->data, s->capacity * s->element_size))
        == NULL) die("realloc()");
  }
  memcpy(stack_access(s, s->size++), element, s->element_size);
}

void stack_pop(stack *s) {
  if (s->size == 0) return;
  s->size--;
}

void stack_dtor(void *obj) {
  stack *s = obj;
  if (s == NULL) return;
  free(s->data);
}

stack proc_list;

void usage() {
  printf("Usage: pstree [ -n ] [ -p ]\n"
         "       pstree -V\n"
         "Display a tree of processes.\n"
         "\n"
         "  -n, --numeric-sort  sort output by PID\n"
         "  -p, --show-pids     show PIDs\n"
         "  -V, --version       display version information\n");
  exit(EXIT_FAILURE);
}

void version() {
  printf("pstree (OS mini lab) 0.1\n"
         "Copyright (C) 2018 Chen Shaoyuan\n");
  exit(EXIT_SUCCESS);
}

stack indents;

typedef enum NODE_TYPE {
  NODE_ROOT = 4,
  NODE_FIRST = 1, NODE_LAST = 2
} NODE_TYPE;

void display_proc_rec(NODE_TYPE type, process *proc) {
  if (proc->printed) return;
  proc->printed = 1;
  if (type & NODE_FIRST)
    putchar(type & NODE_LAST ? '-' : '+');
  else
    for (int i = 0; i < indents.size; i++)
      printf("%*s%c", *(int*)stack_access(&indents, i), "",
          i == indents.size - 1 && type == NODE_LAST ? '`' : '|');

  pid_t ppid = proc->pid;
  process *lptr, *rptr;

  lptr = bsearch(&ppid, proc_list.data, proc_list.size,
      sizeof(process), process_cmp_ppid_t);
  rptr = lptr + 1;

  int written, indent = 0;

  if (!(type & NODE_ROOT)) {
    putchar('-');
    indent++;
  }
  if (proc->is_thread)
    printf("{%s}%n", proc->name, &written);
  else
    printf("%s%n", proc->name, &written);
  indent += written;
  if (opt_show_pids) {
    printf("[%d]%n", proc->pid, &written);
    indent += written;
  }
  if (lptr != NULL) {
    putchar('-');
    indent++;
  } else {
    putchar('\n');
    return;
  }

  if (type & NODE_LAST)
    *(int*)stack_access(&indents, indents.size - 1) += indent + 1;
  else
    stack_push(&indents, &indent);

  while (lptr > (process*)proc_list.data
      && lptr[-1].ppid == lptr[0].ppid) lptr--;
  while (rptr < (process*)stack_access(&proc_list, proc_list.size)
      && rptr[0].ppid == rptr[-1].ppid) rptr++;
  for (process *ptr = lptr; ptr < rptr; ptr++) {
    NODE_TYPE mask = 0;
    if (ptr == lptr) mask |= NODE_FIRST;
    if (ptr == rptr - 1) mask |= NODE_LAST;
    display_proc_rec(mask, ptr);
  }

  if (!(type & NODE_LAST)) stack_pop(&indents);
}

void traversal_proc(const char* dir_path, int is_thread, int ppid) {
  DIR *dir;
  struct dirent *entry;

  if ((dir = opendir(dir_path)) == NULL) die("opendir()");
  while ((entry = readdir(dir)) != NULL) {
    int flag_not_digit = 0;
    for (const char* ptr = entry->d_name; *ptr; ptr++)
      if (!isdigit(*ptr)) {
        flag_not_digit = 1;
        break;
      };
    if (flag_not_digit) continue;
    pid_t pid;
    sscanf(entry->d_name, "%d", &pid);
    if (is_thread && pid == ppid) continue;
    process proc;
    if (process_ctor(&proc, pid, is_thread))
      stack_push(&proc_list, &proc);
    if (!is_thread) {
      char dir_name_buf[64];
      sprintf(dir_name_buf, "/proc/%d/task", pid);
      traversal_proc(dir_name_buf, 1, pid);
    }
  }
  closedir(dir);
}

void display() {
  stack_ctor(&proc_list, sizeof(process));

  traversal_proc("/proc", 0, 0);

  qsort(proc_list.data, proc_list.size, sizeof(process),
      opt_numeric_sort ? process_cmp_sort_pid : process_cmp_nosort);

  for (int i = 0; i < proc_list.size; i++) {
    if (((process*)stack_access(&proc_list, i))->pid == 1) {
      stack_ctor(&indents, sizeof(int));
      display_proc_rec(NODE_ROOT, stack_access(&proc_list, i));
      stack_dtor(&indents);
      break;
    }
  }

  stack_dtor(&proc_list);
  exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
  while (1) {
    static struct option long_options[] = {
      {"show-pids",    no_argument, 0, 'p'},
      {"numeric-sort", no_argument, 0, 'n'},
      {"version",      no_argument, 0, 'V'},
      {0,              0,           0, 0}
    };
    int opt_index = 0;
    int c = getopt_long(argc, argv, "pnV", long_options, &opt_index);
    if (c == -1) break;
    switch (c) {
      case 'p': opt_show_pids = 1; break;
      case 'n': opt_numeric_sort = 1; break;
      case 'V': opt_version = 1; break;
      default:  usage();
    }
  }
  if (optind < argc) usage();
  if (opt_version) version();
  display();
}

