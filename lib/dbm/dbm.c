#include "dbm.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

// Extremely naive DBM implementation: Linear search in a file.
// Format: [KeyLen(4)][Key][ValLen(4)][Value]...

DBM *dbm_open(const char *file, int flags, int mode) {
    DBM *db = malloc(sizeof(DBM));
    if (!db) return NULL;
    db->fd = open(file, flags, mode);
    if (db->fd < 0) {
        free(db);
        return NULL;
    }
    db->iter_pos = 0;
    return db;
}

void dbm_close(DBM *db) {
    if (db) {
        close(db->fd);
        free(db);
    }
}

datum dbm_firstkey(DBM *db) {
    if (!db) return (datum){NULL, 0};
    db->iter_pos = 0;
    return dbm_nextkey(db);
}

datum dbm_nextkey(DBM *db) {
    datum ret = {NULL, 0};
    if (!db || db->fd < 0) return ret;
    
    lseek(db->fd, db->iter_pos, SEEK_SET);
    
    int klen, vlen;
    if (read(db->fd, &klen, sizeof(int)) == sizeof(int)) {
        ret.dsize = klen;
        ret.dptr = malloc(klen);
        read(db->fd, ret.dptr, klen);
        
        read(db->fd, &vlen, sizeof(int));
        lseek(db->fd, vlen, SEEK_CUR);
        db->iter_pos = lseek(db->fd, 0, SEEK_CUR);
        return ret;
    }
    return ret;
}

// Stubs
int dbm_delete(DBM *db, datum key) { (void)db; (void)key; return -1; }
