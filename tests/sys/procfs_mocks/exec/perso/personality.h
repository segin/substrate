#ifndef _PERSONALITY_H
#define _PERSONALITY_H
struct personality {
    const char *name;
};
struct personality *perso_lookup(int id);
#endif
