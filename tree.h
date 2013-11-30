#ifndef TREE_H_
#define TREE_H_ 1

#include <memory>
#include <string>
#include <vector>

struct tree {
  struct node {
    std::string path, value;
  };

  std::string name;
  std::vector<node> nodes;
};

tree* tree_create(const char* name);

void tree_create_node(tree* t, const char* path, const char* value);

long long int tree_get_integer_default(const tree* t, const char* path,
                                       long long int def);

const char* tree_get_string_default(const tree* t, const char* path,
                                    const char* def);

size_t tree_get_strings(const tree* t, const char* path, char*** result);

tree* tree_load_cfg(int home_fd, const char* path);

#endif /* !TREE_H_ */
