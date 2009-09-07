#ifndef INPUT_H_
#define INPUT_H_ 1

#ifdef __cplusplus
extern "C"
{
#endif

enum device_type
{
  device_keyboard,
  device_pointer,
  device_gamepad
};

enum button_state_bits
{
  button_force_mask = 0xff,
  button_pressed = 0x100,   /* Pressed this frame */
  button_released = 0x200,  /* Released this frame */
  button_repeated = 0x400   /* Keyboard auto-repeat */
};

typedef struct
{
  const char* name;
  enum device_type type;
  int connected; /* 0 = disconnected, 1 = connected */
  int axis_count;
  signed short* axis_states; /* -32768 = far left/up, 32767 = far right/down */
  const char** axis_names;
  int button_count;
  unsigned short* button_states;
  const char** button_names;
  char text[32];
} device_state;

struct common_keys_type
{
  int left;
  int right;
  int up;
  int down;
#ifdef PSP
  int triangle;
  int circle;
  int cross;
  int square;
#else
  int escape;
  int enter;
  int space;
  int lshift;
  int rshift;
  int lctrl;
  int rctrl;
  int lalt;
  int ralt;
  int f[12];
  int insert;
  int del;
  int home;
  int end;
  int pgup;
  int pgdown;
#endif
};

extern struct common_keys_type common_keys;

device_state* input_get_device_states(int* device_count);

#ifdef __cplusplus
}
#endif

#endif /* INPUT_H_ */
