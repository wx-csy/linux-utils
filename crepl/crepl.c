#if !(defined __linux__ || defined __linux || defined linux)
# error "this utility is for linux only"
#endif

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ftw.h>
#include <dlfcn.h>

void die(const char *msg) {
  perror(msg);
  exit(EXIT_FAILURE);
}

void dl_die() {
  fprintf(stderr, "%s\n", dlerror());
  exit(EXIT_FAILURE);
}

void welcome(void) {
  printf("%s",
    "This is a read-eval-print loop for C programming language.\n"
    "To exit, type `exit'.\n");
}

typedef uint32_t guid_t;

guid_t guid(void) {
  static guid_t next = 0;
  return next++;
}

char *readline(const char* intro) {
  int cap = 16;
  char *buf = malloc(cap);
  char *last = buf;
  if (buf == NULL)
    die("malloc()");
  printf("%s", intro);
  fflush(stdin);
  while (1) {
    if (fgets(last, cap - (last - buf), stdin) == NULL) {
      if (feof(stdin)) {
        free(buf);
        return NULL;
      } else die("fgets()");
    }
    int len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') return buf;
    buf = realloc(buf, cap << 1);
    last = buf + cap - 1;
    cap <<= 1;
  }
}

char tmpdir[] = "/tmp/crepl-XXXXXX";

int rm_callback(const char *fpath, const struct stat *sb, int typeflag,
      struct FTW *ftwbuf) {
//  printf("remove: %s\n", fpath);
  remove(fpath);
  return 0;
}

void rmtmp(void) {
  nftw(tmpdir, rm_callback, 8, FTW_DEPTH | FTW_MOUNT | FTW_PHYS);
}

int is_function(const char* str) {
  static const char int_name[] = "int ";
  return strncmp(int_name, str, strlen(int_name)) == 0;
}

int compile(const char* srcpath, char* objpath) {
  int pid;
  guid_t id = guid();
  sprintf(objpath, "%s/%"PRIx32".so", tmpdir, id);

  pid = fork();
  if (pid < 0) {
    die("fork()");
  } else if (pid == 0) {  // child
    if (execlp("gcc", "gcc", "-fPIC", "-shared", 
          "-nostartfiles",
          "-Wno-implicit-function-declaration",
          "-o", objpath, srcpath, (char *)NULL) == -1)
      die("execlp()");
  } else {  // parent
    int status;
    if (waitpid(pid, &status, 0) == -1)
      die("waitpid()");
    if (!WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
  }
  assert(0);
}

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

typedef void *phandle;

vector(phandle) solist;

void dynamic_cleanup() {
  for (int i = 0; i < solist.size; i++)
    dlclose(solist.item[i]);
  vector_dtor(solist);
}

void dynamic_load(const char* path) {
  void *handle;
  if ((handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL)) == NULL)
    dl_die();
  vector_push(solist);
  vector_top(solist) = handle;
}

void dynamic_run(const char* path, const char* sym) {
  void *handle;
  int (*fn)(void);
  if ((handle = dlopen(path, RTLD_NOW | RTLD_LOCAL)) == NULL)
    dl_die();
  if ((fn = dlsym(handle, sym)) == NULL) {
    dlclose(handle);
    dl_die();
  }
  printf("%d\n", fn()); 
  if (dlclose(handle) != 0)
    dl_die();
}

void compile_function(const char* val) {
  guid_t id = guid();
  char srcpath[256], objpath[256];
  FILE *fsrc;
  sprintf(srcpath, "%s/%"PRIx32".c", tmpdir, id);
  if ((fsrc = fopen(srcpath, "w")) == NULL)
    die("fopen()");
  fprintf(fsrc, "%s", val);
  fclose(fsrc);
  if (compile(srcpath, objpath) == 0) {
    dynamic_load(objpath);
  }
}

void compile_expr(const char* val) {
  guid_t id = guid();
  char srcpath[256], objpath[256];
  char symbol[64];
  FILE *fsrc;
  sprintf(srcpath, "%s/%"PRIx32".c", tmpdir, id);
  sprintf(symbol, "__crepl_%"PRIx32, id);
  if ((fsrc = fopen(srcpath, "w")) == NULL)
    die("fopen()");
  fprintf(fsrc,
      "int %s() {\n"
      "  return (%s);\n"
      "}\n", symbol, val);
  fclose(fsrc);
  if (compile(srcpath, objpath) == 0) {
    dynamic_run(objpath, symbol); 
  }
}

int main() {
  if (mkdtemp(tmpdir) == NULL)
    die("mkdtemp()");
  atexit(rmtmp);
  vector_ctor(solist);
  atexit(dynamic_cleanup);

  welcome();
  char* val;
  while ((val = readline(">>> ")) != NULL) {
    if (val[0] == '\n') continue;
    if (strcmp(val, "exit\n") == 0)
      exit(EXIT_SUCCESS);
    if (is_function(val)) {
      compile_function(val); 
    } else {
      compile_expr(val);
    }
    free(val);
  }
  return 0;
}

