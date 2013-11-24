#ifndef TREE_H_
#define TREE_H_ 1

#ifdef __cplusplus
extern "C" {
#endif

struct tree;

struct tree* tree_create(const char* name);

void tree_destroy(struct tree* t);

void tree_create_node(struct tree* t, const char* path, const char* value);

long long int tree_get_integer_default(const struct tree* t, const char* path,
                                       long long int def);

const char* tree_get_string_default(const struct tree* t, const char* path,
                                    const char* def);

size_t tree_get_strings(const struct tree* t, const char* path, char*** result);

struct tree* tree_load_cfg(const char* path);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* !TREE_H_ */
