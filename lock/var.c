#include <assert.h>
#include <string.h>

#include "error.h"
#include "var.h"

static var* var_arrays[8];

void var_register(var* variables)
{
  int i;

  for(i = 0; i < sizeof(var_arrays) / sizeof(var_arrays[0]); ++i)
  {
    if(!var_arrays[i])
    {
      var_arrays[i] = variables;

      return;
    }
  }

  assert(!"Out of var arrays");
}

var* var_find(const char* name)
{
  int i, j;

  for(i = 0; i < sizeof(var_arrays) / sizeof(var_arrays[0]); ++i)
  {
    var* variables = var_arrays[i];

    if(!variables)
      continue;

    for(j = 0; variables[j].name; ++j)
    {
      if(!strcmp(variables[j].name, name))
        return &variables[j];
    }
  }

  fatal_error("Did not find variable '%s'", name);

  return 0;
}
