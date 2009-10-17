#ifndef HASHMAP_H_
#define HASHMAP_H_ 1

struct hashmap;

/**
 * Creates an empty hashmap with a given name.
 *
 * The name is used in error messages.
 */
struct hashmap* hashmap_create(const char* name);

/**
 * Inserts a value into the hashmap.
 *
 * If a duplicate key is inserted, an error message is printed and the program
 * is terminated.
 */
void hashmap_insert(struct hashmap* h, const char* key, int value);

/**
 * Determines whether or not a key exists in a hashmap.
 *
 * Returns 1 if the key exists.
 * Returns 0 if the key does not exist.
 */
int hashmap_has_key(struct hashmap* h, const char* key);

/**
 * Returns the value associated with the given key.
 *
 * If the key does not exist, an error message is printed and the program
 * is terminated.
 */
int hashmap_get(struct hashmap* h, const char* key);

#endif /* !HASHMAP_H_ */
