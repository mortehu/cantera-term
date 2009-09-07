#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "error.h"
#include "hashmap.h"

struct HASHMAP_node
{
  char* key;
  int value;
};

struct hashmap
{
  char* name;

  struct HASHMAP_node* nodes;
  size_t size;
  size_t alloc;
};

struct hashmap*
hashmap_create(const char* name)
{
  struct hashmap* result;

  result = malloc(sizeof(struct hashmap));
  result->name = malloc(strlen(name) + 1);
  strcpy(result->name, name);
  result->nodes = 0;
  result->size = 0;
  result->alloc = 0;

  return result;
}

const size_t
hash(const char* key)
{
  size_t v = *key++;

  while(*key)
    v = v * 31 + *key++;

  return v;
}

void
hashmap_insert(struct hashmap* h, const char* key, int value)
{
  size_t i, n;

  assert(h);
  assert(key);

  if(h->size * 4 >= h->alloc * 3)
  {
    struct HASHMAP_node* new_nodes;
    size_t new_alloc;

    if(!h->alloc)
      new_alloc = 15;
    else
      new_alloc = (h->alloc + 1) * 2 - 1;

    new_nodes = calloc(sizeof(struct HASHMAP_node), new_alloc);

    if(!new_nodes)
      fatal_error("malloc of %zu hashmap nodes failed: %s", new_alloc, strerror(errno));

    for(i = 0; i < h->alloc; ++i)
    {
      if(!h->nodes[i].key)
        continue;

      n = hash(h->nodes[i].key) % new_alloc;

      while(new_nodes[n].key)
        n = (n + 1) % new_alloc;

      new_nodes[n] = h->nodes[i];
    }

    free(h->nodes);
    h->nodes = new_nodes;
    h->alloc = new_alloc;
  }

  n = hash(key) % h->alloc;

  while(h->nodes[n].key)
  {
    if(!strcmp(h->nodes[n].key, key))
      fatal_error("Duplicate key '%s' inserted into hashmap '%s'", key, h->name);

    n = (n + 1) % h->alloc;
  }

  h->nodes[n].key = malloc(strlen(key) + 1);
  strcpy(h->nodes[n].key, key);
  h->nodes[n].value = value;
  ++h->size;
}

int hashmap_has_key(struct hashmap* h, const char* key)
{
  size_t n;

  assert(h);
  assert(key);

  if(!h->alloc)
    return 0;

  n = hash(key) % h->alloc;

  while(h->nodes[n].key)
  {
    if(!strcmp(h->nodes[n].key, key))
      return 1;

    n = (n + 1) % h->alloc;
  }

  return 0;
}

int hashmap_get(struct hashmap* h, const char* key)
{
  size_t n;

  assert(h);
  assert(key);

  n = hash(key) % h->alloc;

  while(h->nodes[n].key)
  {
    if(!strcmp(h->nodes[n].key, key))
      return h->nodes[n].value;

    n = (n + 1) % h->alloc;
  }

  fatal_error("Attempt to look up non-existent key '%s' in hashmap '%s'", key, h->name);

  return -2147483647;
}
