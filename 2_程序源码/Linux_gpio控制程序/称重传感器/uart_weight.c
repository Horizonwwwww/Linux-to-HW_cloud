#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

#define UART_DEVICE "/dev/ttyS3"
#define BAUDRATE B9600

int configure_uart(int fd) {
    struct termios options;
    if (tcgetattr(fd, &options) != 0) {
        perror("tcgetattr error");
        return -1;
    }

    cfsetispeed(&options, BAUDRATE);
    cfsetospeed(&options, BAUDRATE);
    options.c_cflag |= (CLOCAL | CREAD);     // 本地连接，启用接收
    options.c_cflag &= ~PARENB;             // 无校验
    options.c_cflag &= ~CSTOPB;             // 1 停止位
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;                 // 8 数据位
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // 原始模式
    options.c_oflag &= ~OPOST;              // 原始输出

    options.c_cc[VMIN] = 0;                 // 非阻塞读取
    options.c_cc[VTIME] = 10;               // 超时 1 秒

    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        perror("tcsetattr error");
        return -1;
    }
    return 0;
}

int main() {
    int uart_fd = open(UART_DEVICE, O_RDWR | O_NOCTTY | O_NDELAY);
    if (uart_fd < 0) {
        perror("Open UART failed");
        exit(EXIT_FAILURE);
    }

    if (configure_uart(uart_fd) < 0) {
        close(uart_fd);
        exit(EXIT_FAILURE);
    }

    printf("Reading weight data...\n");
    char buffer[256];
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int len = read(uart_fd, buffer, sizeof(buffer));
        if (len > 0) {
            // 示例数据格式：假设模块发送 "Weight: 12.34kg\r\n"
            float weight;
            if (sscanf(buffer, "Weight: %fkg", &weight) == 1) {
                printf("Current Weight: %.2f kg\n", weight);
            } else {
                printf("Raw Data: %s\n", buffer); // 调试用
            }
        } else {
            usleep(100000); // 等待 100ms 避免忙等待
        }
    }

    close(uart_fd);
    return 0;
}