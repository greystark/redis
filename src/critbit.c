#include "critbit.h"

typedef uint64_t u64;
typedef int64_t s64;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint8_t u8;
typedef int8_t s8;

#define streq(a,b) (strcmp((a),(b)) == 0)
#define strstarts(str,prefix) (strncmp((str),(prefix),strlen(prefix)) == 0)

static const unsigned char DEBRUIJN_IDX32[32]={
   0, 1,28, 2,29,14,24, 3,30,22,20,15,25,17, 4, 8,
  31,27,13,23,21,19,16, 7,26,12,18, 6,11, 5,10, 9
};

int ilog32(uint32_t _v) {
/*On a Pentium M, this branchless version tested as the fastest version without
   multiplications on 1,000,000,000 random 32-bit integers, edging out a
   similar version with branches, and a 256-entry LUT version.*/
# if defined(ILOG_NODEBRUIJN)
  int ret;
  int m;
  ret=_v>0;
  m=(_v>0xFFFFU)<<4;
  _v>>=m;
  ret|=m;
  m=(_v>0xFFU)<<3;
  _v>>=m;
  ret|=m;
  m=(_v>0xFU)<<2;
  _v>>=m;
  ret|=m;
  m=(_v>3)<<1;
  _v>>=m;
  ret|=m;
  ret+=_v>1;
  return ret;
/*This de Bruijn sequence version is faster if you have a fast multiplier.*/
# else
  int ret;
  ret=_v>0;
  _v|=_v>>1;
  _v|=_v>>2;
  _v|=_v>>4;
  _v|=_v>>8;
  _v|=_v>>16;
  _v=(_v>>1)+1;
  ret+=DEBRUIJN_IDX32[_v*0x77CB531U>>27&0x1F];
  return ret;
# endif
}

static int ilog32_nz(uint32_t _v) {
  return ilog32(_v);
}

static char *sdsstr(const sds s) {
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->buf;
}


static void _strset_init(struct strset *set) {
	set->u.n = NULL;
}

/*static bool _strset_empty(const struct strset *set) {
	return set->u.n == NULL;
        }*/

static bool _is_node(struct strset n) {
  /* Anything with first byte 0 is a node. */
  /* this only works because redisObject's first field,
      type, for redis_string is not 0 */
  return !((char *)n.u.s)[0];
}


strset *critbitCreate() {
  strset *s = zmalloc(sizeof(strset));
  _strset_init(s);
  return s;
}

struct node {
	/* To differentiate us from strings. */
	char nul_byte;
	/* The bit where these children differ. */
	u8 bit_num;
	/* The byte number where first bit differs (-1 == empty string node). */
	size_t byte_num;
	/* These point to strings or nodes. */
	struct strset child[2];
};

static robj *closest(struct strset n, sds member) {
	size_t len = sdslen(member);
	u8 *bytes = (u8 *)sdsstr(member);

	while (_is_node(n)) { 
		u8 direction = 0;

		/* Special node which represents the empty string. */
		if (n.u.n->byte_num == (size_t)-1) {
			n = n.u.n->child[0];
			break;
		}

		if (n.u.n->byte_num < len) {
			u8 c = bytes[n.u.n->byte_num];
			direction = (c >> n.u.n->bit_num) & 1;
		}
		n = n.u.n->child[direction];
	}
	return n.u.s;
}

// refactor char* to robj* and sds
robj *strset_get(struct strset *set, robj *member) {
	robj *closest_obj;
        sds sds_member = (sds) member->ptr;
        char *closest_str, *member_str = sdsstr(sds_member);
	/* Non-empty set? */
	if (set->u.n) {
		closest_obj = closest(*set, sds_member);
                closest_str = sdsstr((sds) closest_obj->ptr);
		if (streq(member_str, closest_str))
			return closest_obj;
	}
        //	errno = ENOENT;
	return NULL;
}

static bool emptystring(const robj *member) {
  return sdslen((sds)member->ptr) > 0;
}

static bool set_obj(struct strset *set,
		       struct strset *n, robj *member) {
	/* Substitute magic empty node if this is the empty string */
  if (emptystring(member)) {
		n->u.n = zmalloc(sizeof(*n->u.n));
		if (!n->u.n) {
                  //			errno = ENOMEM;
			return false;
		}
		n->u.n->nul_byte = '\0';
		n->u.n->byte_num = (size_t)-1;
		/* Attach the obj to child[0] */
		n = &n->u.n->child[0];
	}
	n->u.s = member;
	return true;
}

bool strset_add(struct strset *set, robj *value, sds member) {
	size_t len = sdslen(member);
        char *member_str = sdsstr(member);        
	const u8 *bytes = (const u8 *)member_str;

	struct strset *np;
	robj *closest_obj;
        char *closest_str;
	struct node *newn;
	size_t byte_num;
	u8 bit_num, new_dir;

	/* Empty set? */
	if (!set->u.n) {
		return set_obj(set, set, value);
	}

	/* Find closest existing member. */
	closest_obj = closest(*set, member);
        closest_str = sdsstr((sds) closest_obj->ptr);
        
	/* Find where they differ. */
	for (byte_num = 0; closest_str[byte_num] == member_str[byte_num]; byte_num++) {
		if (member_str[byte_num] == '\0') {
			/* All identical! used to be false, return true */
                  /*mine*/
			return true;
		}
	}

	/* Find which bit differs (if we had ilog8, we'd use it) */
	bit_num = ilog32_nz((u8)closest_str[byte_num] ^ bytes[byte_num]) - 1;
        //	assert(bit_num < CHAR_BIT);

	/* Which direction do we go at this bit? */
	new_dir = ((bytes[byte_num]) >> bit_num) & 1;

	/* Allocate new node. */
	newn = zmalloc(sizeof(*newn));
	if (!newn) {
          //		errno = ENOMEM; 
		return false;
	}
	newn->nul_byte = '\0';
	newn->byte_num = byte_num;
	newn->bit_num = bit_num;
	if (!set_obj(set, &newn->child[new_dir], value)) {
		zfree(newn);
		return false;
	}

	/* Find where to insert: not closest, but first which differs! */
	np = set;
	while (_is_node(*np)) {
		u8 direction = 0;

		/* Special node which represents the empty string will
		 * break here too! */ //(bec. it's size_t -1)
		if (np->u.n->byte_num > byte_num)
			break;
		/* Subtle: bit numbers are "backwards" for comparison */
		if (np->u.n->byte_num == byte_num && np->u.n->bit_num < bit_num)
			break;

		if (np->u.n->byte_num < len) {
			u8 c = bytes[np->u.n->byte_num];
			direction = (c >> np->u.n->bit_num) & 1;
		}
		np = &np->u.n->child[direction];
	}

	newn->child[!new_dir]= *np;
	np->u.n = newn;
	return true;
}

bool strset_del(struct strset *set, robj value, sds member) {
	size_t len = sdslen(member);
        char *member_str = sdsstr(member);
	const u8 *bytes = (const u8 *)member_str;
	struct strset *parent = NULL, *n;
	u8 direction = 0; /* prevent bogus gcc warning. */

	/* Empty set? */
	if (!set->u.n) {
          //		errno = ENOENT;
		return false;
	}

	/* Find closest, but keep track of parent. */
	n = set;
	/* Anything with first byte 0 is a node. */
	while (_is_node(*n)) {
		u8 c = 0;

		/* Special node which represents the empty string. */
		if (n->u.n->byte_num == (size_t)-1) {
                  char *empty_str = sdsstr((sds) n->u.n->child[0].u.s->ptr);

			if (member_str[0]) {
                          //				errno = ENOENT;
				return false;
			}

			/* Sew empty string back so remaining logic works */
			zfree(n->u.n);
                        // blerg!
			n->u.s = createObject(REDIS_STRING, (void *) empty_str);
			break;
		}

		parent = n;
		if (n->u.n->byte_num < len) {
			c = bytes[n->u.n->byte_num];
			direction = (c >> n->u.n->bit_num) & 1;
		} else
			direction = 0;
		n = &n->u.n->child[direction];
	}

	/* Did we find it? */
	if (!streq(member_str, sdsstr((sds) n->u.s->ptr))) {
          //		errno = ENOENT;
		return false;
	}

        //	ret = n->u.s;

	if (!parent) {
		/* We deleted last node. */
		set->u.n = NULL;
	} else {
		struct node *old = parent->u.n;
		/* Raise other node to parent. */
		*parent = old->child[!direction];
		zfree(old);
	}

	return true;
        //return (char *)ret;
}

static bool iterate(struct strset n,
		    bool (*handle)(robj *, long *, void *), long *limit, void *data) {
  if (!_is_node(n)) {
    return handle(n.u.s, limit, (void *)data);
  }
  /* Special node which represents the empty string. */  
  if (n.u.n->byte_num == (size_t)-1) {
    return handle(n.u.n->child[0].u.s, limit, (void *)data);
  }
  return iterate(n.u.n->child[0], handle, limit, data)
      && iterate(n.u.n->child[1], handle, limit, data);
}

void strset_iterate(struct strset *set,
                    bool (*handle)(robj *, long *, void *), long *limit, void *data)
{
	/* Empty set? */
	if (!set->u.n)
		return;

	iterate(*set, handle, limit, data);
}

struct strset *strset_prefix(struct strset *set, sds prefix) {
	struct strset *n, *top;
	size_t len = sdslen(prefix);
        char *cur_str, *prefix_str = sdsstr(prefix);
	const u8 *bytes = (const u8 *)prefix_str;


	/* Empty set -> return empty set. */
	if (!set->u.n)
		return set;

	top = n = set;

	/* We walk to find the top, but keep going to check prefix matches. */
	while (_is_node(*n)) {
		u8 c = 0, direction;

		/* Special node which represents the empty string. */
		if (n->u.n->byte_num == (size_t)-1) {
			n = &n->u.n->child[0];
			break;
		}

		if (n->u.n->byte_num < len)
			c = bytes[n->u.n->byte_num];

		direction = (c >> n->u.n->bit_num) & 1;
		n = &n->u.n->child[direction];
		if (c)
			top = n;
	}
        cur_str = sdsstr((sds)n->u.s->ptr);
	if (!strstarts(cur_str, prefix_str)) {
		/* Convenient return for prefixes which do not appear in set. */
		static struct strset empty_set;
		return &empty_set;
	}

	return top;
}

static void clear(struct strset n) {
  if (_is_node(n)) {
		if (n.u.n->byte_num != (size_t)-1) {
			clear(n.u.n->child[0]);
			clear(n.u.n->child[1]);
		}
		zfree(n.u.n);
	}
}

void strset_clear(struct strset *set) {
	if (set->u.n)
		clear(*set);
	set->u.n = NULL;
}

void strsetRelease(struct strset *set) {
  strset_clear(set);
  zfree(set);
}



#undef u64
#undef s64
#undef u32
#undef s32
#undef u16
#undef s16
#undef u8
#undef s8

