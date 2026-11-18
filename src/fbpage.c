/* fbpage.c — read-only helper: print the active fb page (yoffset/480) as "0"/"1".
 * Used by round_capture.py to pick the active half of the 480×960 fb.
 * Build: zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 -o fbpage fbpage.c
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
int main(void){
    int fd=open("/dev/fb0",O_RDONLY);
    if(fd<0){ perror("open fb0"); return 1; }
    struct fb_var_screeninfo vi;
    if(ioctl(fd,FBIOGET_VSCREENINFO,&vi)<0){ perror("ioctl"); return 1; }
    printf("%d\n",(vi.yoffset/480)&1);
    close(fd);
    return 0;
}