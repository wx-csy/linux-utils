#include "kvdb.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/file.h>

#define KVDB_SYN    0x66666666

typedef struct kvdb_entry {
  uint32_t sync_flag;
  uint32_t key_length;
  uint32_t value_length;
  uint32_t key_hash;
} kvdb_entry_t;

static int update_size(kvdb_t *db) {
  kvdb_entry_t entry;
  if (fseek(db->fp, 0, SEEK_END)) return -1;
  long filesz = ftell(db->fp);
  if (filesz < 0) return -1;
  if (fseek(db->fp, db->size, SEEK_SET)) return -1;
  while (fread(&entry, 1, sizeof entry, db->fp)) {
    if (entry.sync_flag != KVDB_SYN) break;
    if (fseek(db->fp, entry.key_length + entry.value_length, SEEK_CUR))
      return -1;
    if (ftell(db->fp) > filesz) break;
    db->size = ftell(db->fp);
    if (db->size < 0) return -1;
  }
  return ftruncate(fileno(db->fp), db->size);
}

// Note: this function does not guarantee thread-safety!
int kvdb_open(kvdb_t *db, const char *filename) {
  //  // BUG: no error checking
  db->fd = open(filename, O_RDWR | O_CREAT, 0666);
  if (db->fd < 0) {
    db->fd = 0;
    return -1;
  }
  db->fp = fdopen(db->fd, "r+");
  if (db->fp == NULL) {
    db->fd = 0;
    return -1;
  }
  if (pthread_mutex_init(&db->mutex, NULL)) goto fret1;
  db->size = 0;
  if (flock(fileno(db->fp), LOCK_SH)) goto fret1;  
  if (update_size(db)) goto fret2;
  flock(fileno(db->fp), LOCK_UN);
  return 0;
fret2:
  flock(fileno(db->fp), LOCK_UN);
fret1:
  fclose(db->fp);
  return -1;
}

// Note: this function does not guarantee thread-safety!
int kvdb_close(kvdb_t *db) {
  //  // BUG: no error checking
  fclose(db->fp);
  db->fp = NULL;
  pthread_mutex_destroy(&db->mutex);
  return 0;
}

int kvdb_put(kvdb_t *db, const char *key, const char *value) {
  // BUG: no error checking
  long pos;
  kvdb_entry_t entry;
  if (pthread_mutex_lock(&db->mutex)) return -1;
  if (flock(fileno(db->fp), LOCK_EX)) goto fret1;
  if (update_size(db)) goto fret2;
  if (fseek(db->fp, 0, SEEK_END)) goto fret2;
  pos = ftell(db->fp);
  if (pos < 0) goto fret2;
  entry.sync_flag = 0;
  entry.key_length = strlen(key) + 1;
  entry.value_length = strlen(value) + 1;
  entry.key_hash = 0;
  if (fwrite(&entry, 1, sizeof entry, db->fp) < 1       ||
      fwrite(key, 1, entry.key_length, db->fp) < 1      ||
      fwrite(value, 1, entry.value_length, db->fp) < 1) 
    goto fret2;
  if (fflush(db->fp) || fsync(fileno(db->fp))) goto fret2;
  if (fseek(db->fp, pos, SEEK_SET)) goto fret2;
  entry.sync_flag = KVDB_SYN;
  if (fwrite(&entry.sync_flag, 1, sizeof(uint32_t), db->fp) < 1) goto fret2;
  if (fflush(db->fp) || fsync(fileno(db->fp))) goto fret2;
  flock(fileno(db->fp), LOCK_UN);
  pthread_mutex_unlock(&db->mutex);
  return 0;
fret2:
  flock(fileno(db->fp), LOCK_UN);
fret1:
  pthread_mutex_unlock(&db->mutex);
  return -1;
}

char *kvdb_get(kvdb_t *db, const char *key) {
  // BUG: no error checking
  long pos, ret_pos = 0;
  uint32_t value_length, key_length = strlen(key) + 1;
  kvdb_entry_t entry;
  char *ret = NULL, *tmp = NULL;
  if (pthread_mutex_lock(&db->mutex)) return NULL;
  if (flock(fileno(db->fp), LOCK_SH)) goto fret1;
  tmp = malloc(key_length);
  if (tmp == NULL) goto fret2;
  if (update_size(db)) goto fret3;
  if (fseek(db->fp, 0, SEEK_SET)) goto fret3;
  while ((pos = ftell(db->fp)) < db->size) {
    if (pos < 0) goto fret3;
    if (fread(&entry, 1, sizeof entry, db->fp) < 1) goto fret3;
    if (entry.key_length == key_length) {
      if (fread(tmp, 1, key_length, db->fp) < 1) goto fret3;
      if (tmp[key_length - 1]) goto fret3;
      if (strcmp(tmp, key) == 0) {
        ret_pos = ftell(db->fp);
        value_length = entry.value_length;
        if (ret_pos < 0) goto fret3;
      } 
      if (fseek(db->fp, entry.value_length, SEEK_CUR)) goto fret3;
    } else {
      if (fseek(db->fp, entry.key_length + entry.value_length, SEEK_CUR))
        goto fret3;
    }
  }
  if (ret_pos) {
    ret = malloc(value_length);
    if (ret == NULL) goto fret3;
    if (fseek(db->fp, ret_pos, SEEK_SET)) goto fret4;
    if (fread(ret, 1, value_length, db->fp) < 1) goto fret4;
    if (ret[value_length - 1]) goto fret4;
  }
  flock(fileno(db->fp), LOCK_UN);
  pthread_mutex_unlock(&db->mutex);
  return ret;
fret4:
  free(ret);
fret3:
  free(tmp);
fret2:
  flock(fileno(db->fp), LOCK_UN);
fret1:
  pthread_mutex_unlock(&db->mutex);
  return NULL;
}

int kvdb_traverse(kvdb_t *db, void (*callback)(char*, char*)) {
  // BUG: no error checking
  long pos;
  kvdb_entry_t entry;
  char *buf = NULL;
  if (pthread_mutex_lock(&db->mutex)) return -1;
  if (flock(fileno(db->fp), LOCK_SH)) goto fret1;
  if (update_size(db)) goto fret2;
  if (fseek(db->fp, 0, SEEK_SET)) goto fret2;
  while ((pos = ftell(db->fp)) < db->size) {
    if (pos < 0) goto fret2;
    if (fread(&entry, 1, sizeof entry, db->fp) < 1) goto fret2;
    buf = malloc(entry.key_length + entry.value_length);
    if (buf == NULL) goto fret2;
    if (fread(buf, 1, entry.key_length + entry.value_length, db->fp) < 1) 
      goto fret3;
    callback(buf, buf + entry.key_length);
    free(buf);
  }
  flock(fileno(db->fp), LOCK_UN);
  pthread_mutex_unlock(&db->mutex);
  return 0;
fret3:
  free(buf);
fret2:
  flock(fileno(db->fp), LOCK_UN);
fret1:
  pthread_mutex_unlock(&db->mutex);
  return -1;
}
