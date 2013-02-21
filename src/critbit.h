#ifndef __CRITBIT_H
#define __CRITBIT_H

#include "redis.h"
#include <stdbool.h>

/* start of critbit */
typedef struct strset {
	union {
		struct node *n;
		robj *s;
	} u;
} strset;

bool strset_empty(const struct strset *set);
strset *critbitCreate();
robj *strset_get(struct strset *set, robj *member);
bool strset_add(struct strset *set, robj *value, sds member);
//bool strset_add(struct strset *set, const robj *member);
//char *strset_del(struct strset *set, const robj *member);
bool strset_del(struct strset *set, robj *value, sds member);
void strset_clear(struct strset *set);
void strset_iterate(struct strset *set,
                    bool (*handle)(robj *, long *,void *), long *limit, void *data);
struct strset *strset_prefix(struct strset *set,
				   sds prefix);
void strsetRelease(strset *s);

#endif /* __CRITBIT_H */
