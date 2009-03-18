#ifndef CANTERA_CONFIG_H_
#define CANTERA_CONFIG_H_

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**********************************************/
/* Functions callable from configuration code */
/**********************************************/

/**
 * Launches a command, returning the pid of the new process.
 */
pid_t launch(const char* command);

/************************************************************/
/* Functions that can be declared in the configuration code */
/************************************************************/

/**
 * Called when the Super key is pressed in combination with a character key.
 *
 * The `ch' parameters contains the character pressed.
 */
void handle_hotkey(int ch);

#ifdef __cplusplus
}
#endif

#endif /* !CANTERA_CONFIG_H_ */
