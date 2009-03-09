#ifndef JPC_H_
#define JPC_H_ 1

#define JPC_DEFAULT   0x8000
#define JPC_FG_BLACK  0x0000
#define JPC_FG_BLUE   0x0001
#define JPC_FG_GREEN  0x0002
#define JPC_FG_RED    0x0004
#define JPC_BG_BLACK  0x0000
#define JPC_BG_BLUE   0x0010
#define JPC_BG_GREEN  0x0020
#define JPC_BG_RED    0x0040
#define JPC_STANDOUT  0x0100
#define JPC_UNDERLINE 0x0200

#define JPC_FG_CYAN      (JPC_FG_BLUE |  JPC_FG_GREEN)
#define JPC_FG_MAGENTA   (JPC_FG_BLUE |  JPC_FG_RED)
#define JPC_FG_YELLOW    (JPC_FG_GREEN | JPC_FG_RED)
#define JPC_FG_WHITE     (JPC_FG_BLUE |  JPC_FG_GREEN | JPC_FG_RED)
#define JPC_BG_CYAN      (JPC_BG_BLUE |  JPC_BG_GREEN)
#define JPC_BG_MAGENTA   (JPC_BG_BLUE |  JPC_BG_RED)
#define JPC_BG_YELLOW    (JPC_BG_GREEN | JPC_BG_RED)
#define JPC_BG_WHITE     (JPC_BG_BLUE |  JPC_BG_GREEN | JPC_BG_RED)

#define JPC_KEY_UP      0x8001
#define JPC_KEY_DOWN    0x8002
#define JPC_KEY_LEFT    0x8003
#define JPC_KEY_RIGHT   0x8004
#define JPC_KEY_PPAGE   0x8005
#define JPC_KEY_NPAGE   0x8006
#define JPC_KEY_HOME    0x8007
#define JPC_KEY_END     0x8008
#define JPC_KEY_F(n)    (0x8009 + (n))

/**
 * Initialize.
 */
void jpc_init();

/**
 * Clean up.
 *
 * Called automatically through atexit() handler installed by jpc_init().  Does
 * nothing the second time if called twice.
 */
void jpc_exit();

/**
 * Handle screen resize.
 *
 * Not invoked automatically.  Catch SIGWINCH and call this.
 */
void jpc_resize();

/**
 * Update screen with contents of canvas.
 */
void jpc_paint();

/**
 * Clear screen, then update with contents of canvas.
 */
void jpc_full_repaint();

/**
 * Clear canvas contents.
 */
void jpc_clear();

/**
 * Add string to canvas.
 */
void jpc_addstring(unsigned int attr, int x, int y, const wchar_t* text);

void jpc_addstring_utf8(unsigned int attr, int x, int y, const unsigned char* text);

/**
 * Useful if you want to launch some external program.
 *
 * Restores canvas contents on screen when calling jpc_enable.
 */
void jpc_disable();

/**
 * The opposite of jpc_disable().
 */
void jpc_enable();

wchar_t jpc_getc();

void jpc_get_size(int* width, int* height);

#endif /* JPC_H_ */
