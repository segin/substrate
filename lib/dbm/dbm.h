#ifndef _DBM_H
#define _DBM_H

typedef struct {
    char *dptr;
    int dsize;
} datum;

typedef struct {
    int fd;
    long iter_pos;
} DBM;

DBM *dbm_open(const char *file, int flags, int mode);
void dbm_close(DBM *db);
int dbm_store(DBM *db, datum key, datum content, int store_mode);
datum dbm_fetch(DBM *db, datum key);
int dbm_delete(DBM *db, datum key);
datum dbm_firstkey(DBM *db);
datum dbm_nextkey(DBM *db);

#define DBM_INSERT 0
#define DBM_REPLACE 1

#endif
