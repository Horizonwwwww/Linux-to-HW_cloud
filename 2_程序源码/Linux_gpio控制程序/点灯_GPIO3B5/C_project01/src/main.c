
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#define LED_PATH "/sys/class/leds/gpio3b5/brightness"
void write_to_file(const char *filename, const char *value) {
    int fd = open(filename, O_WRONLY);
    if (fd < 0) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }
    write(fd, value, strlen(value));
    close(fd);
}
int main() {
    // 1. 设置 LED（GPIO3_B5）高电平
    write_to_file(LED_PATH, "1");
    printf("GPIO3_B5 已设为高电平\n");
    sleep(2);  // 等待 2 秒
  printf("lk_linux nice!\n");
    return 0;
}
