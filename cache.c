/*
    cache.c  -- Generic cache routines.
    Copyright (C) 2003  Roy Keene

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

	email: tcpcgi@rkeene.org

*/

/*
 * Generic cache routines -- designed for the LAN Block Device, but
 * should be usable for almost anything (hopefully).
 */


#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "cache.h"

#ifndef MIN
#define MIN(x,y) ((x)<(y)?(x):(y))
#endif

/*
 * Internal only.
 */
static uint32_t cache_hash(unsigned char *key, uint8_t keylen) {
	uint32_t i,h=0,g;

	for (i=0;i<keylen;i++) {
		h = (h << 4) + key[i];
		if ((g = (h & 0xf0000000))) h ^= (g >> 24);
		h &= ~g;
	}

	return(h);
}

/*
 * Internal only.
 */
static cachei_t *cache_find_i(cache_t *c, uint32_t hashkey, void *key, uint8_t keylen) {
	time_t ctime;
	uint8_t keylenmin;
	cachei_t *cur, *del;

	if (!c) return(NULL);

	ctime=time(NULL);
	/* Compute location based on hash of key value. */
	cur=c->hashtable[hashkey];
	while (cur) {
		if (ctime>cur->expires && cur->expires!=0) {
			del=cur;
			cur=cur->_next;
			/* Move the pointers around before we delete things. */
			if (del->_next) del->_next->_prev=del->_prev;
			if (del->_prev) {
				del->_prev->_next=del->_next;
			} else {
				c->hashtable[hashkey]=del->_next;
			}
			if (!del->_next && !del->_prev) c->hashtable[hashkey]=NULL;
			if (del->cleanup) del->cleanup(del, del->key, del->keylen);
			/* Noone should be referencing us now, delete. */
			free(del);
			continue;
		}
		keylenmin=MIN(cur->keylen, keylen);
		if (memcmp(key, cur->key, keylenmin)!=0 || keylen!=cur->keylen) {
			cur=cur->_next;
		} else {
			break;
		}
	}
	return(cur);
}

/*
 * cache_t *cache_create(
 *                       int num            Size of hash table to create.
 *                      );
 */
cache_t *cache_create(int num) {
	cache_t *ret;

	ret=malloc(sizeof(cache_t));
	if ((ret->hashtable=calloc(num, sizeof(cachei_t *)))==NULL) {
		return(NULL);
	}
	ret->hashsize=num;
	return(ret);
}

/*
 * int cache_destroy(
 *                   cache_t *c       Cache object to destroy.
 *                  );
 */
int cache_destroy(cache_t *c) {
	cachei_t *cur, *del;
	int i;

	if (!c) return(0);
	if (!c->hashtable) return(0);

	for (i=0;i<(c->hashsize);i++) {
		cur=c->hashtable[i];
		while (cur) {
			del=cur;
			cur=cur->_next;
			if (del->cleanup) del->cleanup(del->data, del->key, del->keylen);
			free(del);
		}
	}

	free(c->hashtable);
	free(c);
	return(1);
}

#if defined(DEBUG) || defined(CACHE_DEBUG)
/*
 * void cache_print(
 *                  cache_t *c       Cache object
 *                 );
 * Notes:
 *       This function dumps a representation of the hash table to stdout.
 */
void cache_print(cache_t *c) {
	cachei_t *cur;
	int i;

	if (!c) return;

	for (i=0;i<(c->hashsize);i++) {
		printf("%p[%i]=", c, i);
		cur=c->hashtable[i];
		while (cur) {
			printf("%p{\"%s\",%i} -> ", cur, (char *) cur->key, cur->keylen);
			cur=cur->_next;
		}
		printf("NULL\n");
	}
	return;
}
#endif

/*
 * cache_add(
 *           cache_t *c        Cache to operate on.
 *           void *key         Key to used to reference data.
 *           uint8_t keylen    Size of the key.
 *           void *data        Data to add
 *           uint32_t datalen  Size of data
 *          );
 *  Return value:
 * 		NULL	if no previous value was found
 *              (void *) to data being replaced with the new value.
 * 
 */
void *cache_add(cache_t *c, void *key, uint8_t keylen, void *data, uint32_t datalen, time_t ttl, void *cleanup) {
	uint32_t hashkey;
	cachei_t *cur=NULL, *ocur, *new;
	void *ret;

	if (!c) return(NULL);

	/* Compute location based on hash of key value. */
	hashkey=(cache_hash(key, keylen))%(c->hashsize);
	ocur=c->hashtable[hashkey];

	cur=cache_find_i(c, hashkey, key, keylen);

	if (cur) {
		/* Found... do what?  Save the old data (to return) 
		 * and replace the data field, then return the old.
		 */
		ret=cur->data;
		cur->data=data;
		cur->datalen=datalen;
		cur->expires=(ttl)?(time(NULL)+ttl):(0);
		new->cleanup=cleanup;
		return(ret);
	}

	/* NOTFOUND=add it */
	if ((new=malloc(sizeof(cachei_t)))==NULL) return(NULL);
	new->_next=ocur;
	new->_prev=NULL;
	new->data=data;
	new->key=key;
	new->datalen=datalen;
	new->keylen=keylen;
	new->expires=(ttl)?(time(NULL)+ttl):(0);
	new->cleanup=cleanup;
	c->hashtable[hashkey]=new;
	if (ocur) ocur->_prev=new;
	return(NULL);
}

/*
 * void *cache_delete(
 *                    cache_t *c         Cache object to operate on.
 *                    void *key          Key to entry in cache object.
 *                    uint8_t keylen     Size of the key.
 *                    uint32_t *datalen  Length of data being returned,
 *                                       if provided when _add()'d, may
 *                                       be NULL.
 *                    int cleanup        If (TRUE) then call the cleanup
 *                                       function for the data, otherwise
 *                                       don't.
 *                   );
 * Return value:
 *            Fail:
 *               NULL if not found, and *datalen is set to (~0ULL) (all bits
 *               set).
 *            Success:
 *               Otherwise, data provided is returned and datalen is set to
 *               added value, if it's not NULL (this data may no longer be
 *               useful if cleanup is set to TRUE.)
 *
 */
void *cache_delete(cache_t *c, void *key, uint8_t keylen, uint32_t *datalen, int cleanup) {
	uint32_t hashkey;
	cachei_t *cur;
	void *ret;

	if (!c) return(NULL);

	/* Compute location based on hash of key value. */
	hashkey=(cache_hash(key, keylen))%(c->hashsize);
	cur=cache_find_i(c, hashkey, key, keylen);

	/* If not found, abort the deletion operation. */
	if (!cur) {
		if (datalen) *datalen=(uint32_t) (~0ULL);
		return(NULL);
	}

	if (datalen) *datalen=cur->datalen;
	ret=cur->data;

	/* Move the pointers around before we delete things. */
	if (cur->_next) cur->_next->_prev=cur->_prev;
	if (cur->_prev) {
		cur->_prev->_next=cur->_next;
	} else {
		c->hashtable[hashkey]=cur->_next;
	}
	if (!cur->_next && !cur->_prev) c->hashtable[hashkey]=NULL;

	if (cleanup && cur->cleanup) cur->cleanup(cur->data, cur->key, cur->keylen);

	/* Noone should be referencing us now, delete. */
	free(cur);

	return(ret);
}

/*
 * void *cache_find(
 *                  cache_t *c        Cache object to operate on.
 *                  void *key         Key to entry in cache object.
 *                  uint8_t keylen    Size of the key.
 *                  uint32_t *datalen Length of data being returned,
 *                                    if provided when _add()'d, may
 *                                    be NULL.
 *                 );
 * Return value:
 *            Fail:
 *               NULL if not found, and *datalen is set to (~0ULL) (all bits
 *               set).
 *            Success:
 *               Otherwise, data provided is returned and datalen is set to
 *               added value, if it's not NULL.
 *
 */
void *cache_find(cache_t *c, void *key, uint8_t keylen, uint32_t *datalen) {
	uint32_t hashkey;
	cachei_t *cur;
	void *ret;

	if (!c) return(NULL);

	hashkey=(cache_hash(key, keylen))%(c->hashsize);
	cur=cache_find_i(c, hashkey, key, keylen);
	if (!cur) {
		if (datalen) *datalen=(uint32_t) (~0ULL);
		return(NULL);
	}
	if (datalen) *datalen=cur->datalen;
	ret=cur->data;
	return(ret);
}

#define ARRAY_TO_COMMA4(x) ((char) (((x)>>24)&0xff)),((char) (((x)>>16)&0xff)),((char) (((x)>>8)&0xff)),((char) ((x)&0xff))
/*
 * int cache_save(
 *                cache_t *c          Cache object to save.
 *                FILE *fp            FILE object to save to (see note #4)
 *                int fd              File descriptor to save to (see note #4)
 *               );
 * Return value:
 *               Number of items saved, -1 on error.
 * *** NOTES ***
 * 1.  This assumes time() returns a 32bit integer! This will need to changed by Feb 2038. 
 * 2.  The cleanup() function will be lost, and the data will be automatically cleaned up.
 * 3.  This REQUIRES the datalen field to be accurate.
 * 4.  If `fp' is NULL then `fd' is used, otherwise `fp' is used.
 */
int cache_save(cache_t *c, FILE *fp, int fd) {
	cachei_t *cur;
	FILE *fdfp;
	int i,cnt=0;

	if (!c) return(0);

	if (fp) {
		fdfp=fp;
	} else {
		fdfp=fdopen(fd, "w");
	}

	/* Put our magic at the beginning of the file. */
	fprintf(fdfp, "rkc%c", 0);
	fprintf(fdfp, "%c%c%c%c", ARRAY_TO_COMMA4(c->hashsize));

	for (i=0;i<(c->hashsize);i++) {
		cur=c->hashtable[i];
		while (cur) {
			/* If our datalen is non-sense, fix it. */
			if (cur->data==NULL && cur->datalen!=0) cur->datalen=0;
			fprintf(fdfp, "%c%c%c%c%c%c%c%c%c", cur->keylen, ARRAY_TO_COMMA4(cur->datalen), ARRAY_TO_COMMA4((uint32_t) cur->expires));
			if (fwrite(cur->key, cur->keylen, 1, fdfp)<0) return(-1);
			if (cur->data) {
				if (fwrite(cur->data, cur->datalen, 1, fdfp)<0) return(-1);
			}
			cur=cur->_next;
			cnt++;
		}
	}
	fprintf(fdfp, "%c%c%c%c%c", 0x0, 0xff, 0xff, 0xff, 0xff);
	return(cnt);
}

#ifndef ARRAY_TO_INT32
#define ARRAY_TO_INT32(x, y) ((((uint32_t) x[(y)])<<24) | (((uint32_t) x[(y)+1])<<16) | (((uint32_t) x[(y)+2])<<8) | (((uint32_t) x[(y)+3])))
#endif
/*
 * cache_t *cache_load(
 *                     FILE *fp         FILE object to load from (see note#2).
 *                     int fd           File descriptor to load from (see note #2).
 *                    );
 * Return value:
 *               Returns the cache_t object or NULL on error.
 * *** NOTES ***
 * 1.  This assumes time() returns a 32bit integer! This will need to changed by Feb 2038. 
 * 2.  If `fp' is NULL then `fd' is used, otherwise `fp' is used.
 */
cache_t *cache_load(FILE *fp, int fd) {
	cache_t *ret=NULL;
	FILE *fdfp;
	void *key, *data;
	char magic[4];
	unsigned char ui32c[4];
	uint32_t hashsize, datalen;
	uint8_t keylen;
	time_t expires, ctime, ttl;

	if (fp) {
		fdfp=fp;
	} else {
		fdfp=fdopen(fd, "r");
	}

	ctime=time(NULL);
	fread(&magic, sizeof(magic), 1, fdfp);
	if (memcmp(magic, "rkc\0", 4)!=0) return(NULL);
	fread(&ui32c, sizeof(ui32c), 1, fdfp); /* hashsize */
	hashsize=ARRAY_TO_INT32(ui32c, 0);
	/* Create the cache hash table. */
	ret=cache_create(hashsize);
	if (!ret) {
		return(NULL); /* We've read a bit of data, should we lseek() backwards, SEEK_CUR -8 ? */
	}

	while (!feof(fdfp)) {
		fread(&keylen, sizeof(uint8_t), 1, fdfp); /* keylen */
		fread(&ui32c, sizeof(ui32c), 1, fdfp); /* datalen */
		datalen=ARRAY_TO_INT32(ui32c, 0);
		fread(&ui32c, sizeof(ui32c), 1, fdfp); /* expires */
		expires=(time_t) ARRAY_TO_INT32(ui32c, 0);
		if (keylen==0 && datalen==0xffffffff) break; /* This is our EOF sequence. */
		if ((key=malloc(keylen))==NULL) continue;
		if ((data=malloc(datalen))==NULL) { free(key); continue; }
		fread(key, keylen, 1, fdfp);  /* key */
		fread(data, datalen, 1, fdfp);  /* data */
		/* Add the entry. */
		ttl=(expires-ctime);
		if (ttl<=0) continue; /* No sense in adding an expired entry. */
		cache_add(ret, key, keylen, data, datalen, ttl, free);
	}

	return(ret);
}
