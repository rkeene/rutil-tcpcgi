/*
    cache.h  -- Generic cache routines headers and definitions file.
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

#ifndef CACHE_H_RSK
#define CACHE_H_RSK
/*
 * Generic cache routines -- designed for the LAN Block Device, but
 * should be usable for almost anything (hopefully).
 */


#include <sys/types.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

struct cache_item;

struct cache_item {
	void *data;               /* Pointer to the actual data. [REQUIRED] */
	void *key;                /* (Start) Key to index this record with. [REQUIRED] */
	uint8_t keylen;           /* Length of key. [REQUIRED] */
	uint32_t datalen;         /* datalen is optional, and only there for convience. */
	time_t expires;           /* Absolute time when this record expires. */
	int (*cleanup)();         /* Clean-up routine, executed when this record expires, called as: cleanup(data,key,keylen); */
	struct cache_item *_next; /* Pointer to the next item, so we can be in a list. [REQUIRED] */
	struct cache_item *_prev; /* Pointer to the previous item, so we can delete more easily. [REQUIRED] */
};

typedef struct cache_item cachei_t;

typedef struct {
	cachei_t **hashtable;
	uint32_t hashsize;
} cache_t;

cache_t *cache_create(int num);
int cache_destroy(cache_t *c);
void *cache_add(cache_t *c, void *key, uint8_t keylen, void *data, uint32_t datalen, time_t ttl, void *cleanup);
void *cache_delete(cache_t *c, void *key, uint8_t keylen, uint32_t *datalen, int cleanup);
void *cache_find(cache_t *c, void *key, uint8_t keylen, uint32_t *datalen);
int cache_save(cache_t *c, FILE *fp, int fd);
cache_t *cache_load(FILE *fp, int fd);
#if defined(DEBUG) || defined(CACHE_DEBUG)
void cache_print(cache_t *c);
#endif

#endif
