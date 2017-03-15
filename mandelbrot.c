#include <stdlib.h>
#include <termios.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/select.h>

struct termios orig_termios;

struct color {
    unsigned short int r;
    unsigned short int g;
    unsigned short int b;
};

void reset_terminal_mode() {
    tcsetattr(0, TCSANOW, &orig_termios);
}

void set_conio_terminal_mode() {
    struct termios new_termios;

    // Two copies
    tcgetattr(0, &orig_termios);
    memcpy(&new_termios, &orig_termios, sizeof(new_termios));

    // Register cleanup handler, set terminal mode
    atexit(reset_terminal_mode);
    cfmakeraw(&new_termios);
    tcsetattr(0, TCSANOW, &new_termios);
}

int getch() {
    int r;
    unsigned char c;
    if ((r = read(0, &c, sizeof(c))) < 0) {
        return r;
    } else {
        return c;
    }
}

int kbhit() {
    struct timeval tv = { 0L, 0L };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv);
}

struct color hsv_to_rgb(int h, float s, float v) {
    float c = v * s;
    float h1 = h / 60.0;
    float x = c * (1 - fabs(fmod(h1, 2) -1));
    struct color col;
    float r = 0;
    float g = 0;
    float b = 0;
    if (0 <= h1 && h1 < 1) {
        r = c;
        g = x;
    } else if (1 <= h1 && h1 < 2) {
        r = x;
        g = c;
    } else if (2 <= h1 && h1 < 3) {
        g = c;
        b = x;
    } else if (3 <= h1 && h1 < 4) {
        g = x;
        r = c;
    } else if (4 <= h1 && h1 < 5) {
        r = x;
        b = c;
    } else if (5 <= h1 && h1 < 6) {
        r = c;
        b = x;
    }
    float m = v - c;
    col.r = (r + m) * 255;
    col.g = (g + m) * 255;
    col.b = (b + m) * 255;
    return col;
}

void draw_rgb(struct color c, unsigned short int x, unsigned short int y, struct fb_var_screeninfo vinfo, struct fb_fix_screeninfo finfo, char* fbp) {
    if (x > vinfo.xres_virtual || y > vinfo.yres_virtual) return;
    long int location = (x+vinfo.xoffset) * (vinfo.bits_per_pixel/8) + (y+vinfo.yoffset) * finfo.line_length;
    if (vinfo.bits_per_pixel == 32) {
        *(fbp + location)     = c.b;
        *(fbp + location + 1) = c.g;
        *(fbp + location + 2) = c.r;
        *(fbp + location + 3) = 0; // no alpha
    } else { // 16 bpp
        *((unsigned short int*)(fbp + location)) = (unsigned short int)((c.r & 0x1f) << 11 | (c.g & 0x1f) << 5 | (c.b & 0x1f));
    }
}

int main() {
    int fbfd = 0;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    long int screensize = 0;
    char *fbp = 0;

    // Open file for reading and writing
    fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd == -1) {
        perror("Error: cannot open framebuffer device");
        exit(1);
    }
    printf("The framebuffer device was opened successfully.\n");

    // Get fixed screen information
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        perror("Error reading fixed information");
        exit(2);
    }

    // Get variable screen information
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        perror("Error reading variable information");
        exit(3);
    }

    printf("%dx%d, %dbpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

    // Figure out the size of the screen in bytes
    screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;

    // Map the device to memory
    fbp = (char *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if ((int)fbp == -1) {
        perror("Error: failed to map framebuffer device to memory");
        exit(4);
    }
    printf("The framebuffer device was mapped to memory successfully.\n");


    set_conio_terminal_mode();

    // Program logic
    double zoom = 1.0;
    double centerX = 0.0;
    double centerY = 0.0;
    int log2 = 9;
    int xb, yb;
    int iter = 256;
    long int count;
    long int b;
    long int next;
    while(1) {
        count = 0;
        next = 3;
        while (!kbhit() && count < 1 << (log2 * 2)) {
            b = count;
            xb = 0x00;
            yb = 0x00;
            for (int i = 0; i < log2 * 2; ++i) {
                if (i % 2 == 0) {
                    xb |= ((b >> i) & 0x1) << (log2 - (int)(i / 2) - 1);
                } else {
                    yb |= ((b >> i) & 0x1) << (log2 - (int)((i - 1) / 2) - 1);
                }
            }
            double cr = centerX + ((double)xb - (1 << log2) / 2) / (double)(1 << log2) / zoom;
            double ci = centerY + ((double)yb - (1 << log2) / 2) / (double)(1 << log2) / zoom;
            double zr = 0.0;
            double zi = 0.0;
            struct color col = {0, 0, 0};
            int i = 0;
            for (i = 0; i < iter; ++i) {
                if (zr * zr + zi * zi >= 2.0) {
                    col = hsv_to_rgb(i % 360, 1.0, 1.0);
                    break;
                }
                double real = zr;
                double imag = zi;
                zr = real * real - imag * imag + cr;
                zi = 2 * real * imag + ci;
            }
            draw_rgb(col, xb, yb, vinfo, finfo, fbp);
            ++count;
        }
        char exit = 0;
        char ch = getch();
        switch(ch) {
            case 'q':   exit  = 1;      break;
            case '=':   zoom *= 2;      break;
            case '-':   zoom /= 2;      break;
            case 'w':
                        centerY -=  1 / zoom / 4;
                        break;
            case 'a':
                        centerX -=  1 / zoom / 4;
                        break;
            case 's':
                        centerY +=  1 / zoom / 4;
                        break;
            case 'd':
                        centerX +=  1 / zoom / 4;
                        break;
            case ']':   ++iter;         break;
            case '[':   --iter;         break;
            case '}':   iter += 10;     break;
            case '{':   iter -= 10;     break;
            case ')':   iter += 100;    break;
            case '(':   iter -= 100;    break;
            case '+':   ++log2;         break;
            case '_':   --log2;         break;
                        // default:    exit  = 1;      break;
        }
        if (iter < 1) iter = 1;
        if (log2 < 1) log2 = 1;
        if (exit) break;
    }

    // Close framebuffer
    munmap(fbp, screensize);
    close(fbfd);
    return 0;
}
