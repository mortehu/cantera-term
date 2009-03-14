#ifndef COMMON_H_
#define COMMON_H_ 1

struct picture
{
  Picture pic;
  int width;
  int height;
};

void run_command(int fd, const char* command, const char* arg);
void init_ximage(XImage* image, int width, int height, void* data);
int image_load(const char* path, struct picture* pic);

extern Window window;
extern Display* display;

#endif /* !COMMON_H_ */
