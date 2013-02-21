#include "redis.h"
#include "critbit.h"
/*mine*/

static int MAX_PREFIX_RESULTS = 100;

bool cbAdd(robj *cb, robj *value) {
  bool ret = false;
  if (cb->encoding != REDIS_ENCODING_CRITBIT ||
      value->type != REDIS_STRING || value->encoding != REDIS_ENCODING_RAW) {
    return false;
  }
  ret = strset_add((strset*)cb->ptr, value, (sds) value->ptr);
  if (ret) {
    incrRefCount(value);
  }
  return ret;
}

/* cbadd key val */
void cbaddCommand(redisClient *c) {
  int j = 0, added = 0;
  robj *cbobj = lookupKeyWrite(c->db, c->argv[1]);
  if (cbobj && cbobj->type != REDIS_CRITBIT) {
    addReply(c,shared.wrongtypeerr);
    return;
  }

  // TODO: signal cb as ready?
  for (j = 2; j < c->argc; j++) {
    if (!cbobj) {
      cbobj = createCritbitObject();
      dbAdd(c->db, c->argv[1], cbobj);
    }
    if (cbAdd(cbobj, c->argv[j])) {
      added++;
    }
    // TODO: fail on failed add? or just redisPanic
  }
  addReplyLongLong(c, added);

  // TODO: signalModifiedKey, notifyKeyspaceevent
  server.dirty += added;
}

void cbgetCommand(redisClient *c) {
  robj *cbobj = lookupKeyWrite(c->db, c->argv[1]);
  bool ret = false;
  if (cbobj && cbobj->type != REDIS_CRITBIT) {
    addReply(c,shared.wrongtypeerr);
    return;
  }
  if (!cbobj) {
    addReply(c,shared.nokeyerr);
    return;
  }
  ret = strset_get((strset*)cbobj->ptr, c->argv[2]);
  if (ret) {
    addReplyLongLong(c, 1);
  } else {
    addReplyLongLong(c, 0);
  }
}

void cbdelCommand(redisClient *c) {
  robj *o;
  int j, deleted = 0, keyremoved = 0;
  strset *cb;
  if ((o = lookupKeyWriteOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,REDIS_CRITBIT)) return;

  cb = (strset*)o->ptr;
  for (j = 2; j < c->argc; j++) {
    if (strset_del(cb, c->argv[j], (sds) c->argv[j]->ptr)) {
      deleted++;
      if (strset_empty(cb)) {
        dbDelete(c->db,c->argv[1]);
        keyremoved = 1;
        break;
      }
    }
  }
  if (deleted) {
    //        signalModifiedKey(c->db,c->argv[1]);
    //        notifyKeyspaceEvent(REDIS_NOTIFY_HASH,"hdel",c->argv[1],c->db->id);
    /* if (keyremoved) */
    /*     notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",c->argv[1], */
    /*                         c->db->id); */
    server.dirty += deleted;
  }
  addReplyLongLong(c,deleted);
    
}


bool addPrefix(robj *r, long *limit, void *data) {
  if (*limit <= 0) return false;  
  if (r) {
    (*limit)--;
    addReplyBulk(((redisClient*)data), r);
  }
  return true;
}

void cbprefixCommand(redisClient *c) {
  long max, limit;
  strset *prefix;
  void *replylen = NULL;
  
  if (c->argc > 4) {
    addReply(c,shared.syntaxerr);
    return;
  }
  /* parse optional extra max argument */
  if (c->argc == 3) {
    max = 10;
  } else if (getLongFromObjectOrReply(c, c->argv[3], &max, NULL) != REDIS_OK) {
    return;
  }
  if (max < 1 || max > MAX_PREFIX_RESULTS) {
    addReply(c,shared.syntaxerr);
    return;
  }

  // get cribit from key
  robj *cbobj = lookupKeyWrite(c->db, c->argv[1]);
  if (cbobj && cbobj->type != REDIS_CRITBIT) {
    addReply(c,shared.wrongtypeerr);
    return;
  }
  if (!cbobj) {
    addReply(c,shared.nokeyerr);
    return;
  }  
  if ((prefix = strset_prefix((strset*)cbobj->ptr, \
                              (sds)c->argv[2]->ptr))->u.s == NULL) {
    addReply(c, shared.emptymultibulk);
    return;
  }
  replylen = addDeferredMultiBulkLength(c);
  limit = max;
  strset_iterate(prefix,
                 addPrefix, &limit, c);
  setDeferredMultiBulkLength(c, replylen, max-limit);  
}

