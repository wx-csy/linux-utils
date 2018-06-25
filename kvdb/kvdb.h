#include <stdio.h>
#include <stdint.h>
#include <pthread.h>

struct kvdb {
  int fd;
  FILE *fp;
  pthread_mutex_t mutex;
  long size;
};

typedef struct kvdb kvdb_t;

#ifdef __cplusplus
extern "C" {
#endif

int kvdb_open(kvdb_t *db, const char *filename);
int kvdb_close(kvdb_t *db);
int kvdb_put(kvdb_t *db, const char *key, const char *value);
char *kvdb_get(kvdb_t *db, const char *key);
int kvdb_traverse(kvdb_t *db, void (*callback)(char*, char*));

#ifdef __cplusplus
}
#endif

