#ifndef VAR_H_
#define VAR_H_ 1

#ifdef __cplusplus
extern "C" {
#endif

enum var_flags
{
  var_default = 0x0000,
  var_archive = 0x0001
};

enum var_type
{
  var_float,
  var_asciiz
};

typedef struct
{
  const char* name; /* Not owned by this structure.  Probably static */
  int flags;

  enum var_type type;

  char* vasciiz; /* Owned by this structure */
  float vfloat;  /* Accurately represents any integer
                  * with magnitude less than 2^24 */
} var;

void var_register(var* variables);
var* var_find(const char* name);

#ifdef __cplusplus
}
#endif

#endif /* !VAR_H_ */
