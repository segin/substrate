#ifndef _EXEC_PERSO_H
#define _EXEC_PERSO_H

struct personality {
    char *name;
};

struct personality *perso_lookup(int id);

#endif
