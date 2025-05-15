#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

#define UART_DEVICE "/dev/ttyS3"
#define BAUDRATE B9600

// 校验函数声明
int verify_checksum(unsigned char *data, int length);

int configure_uart(int fd) {
    struct termios options;
    if (tcgetattr(fd, &options) != 0) {
        perror("tcgetattr error");
        return -1;
    }

    cfsetispeed(&options, BAUDRATE);
    cfsetospeed(&options, BAUDRATE);
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_oflag &= ~OPOST;

    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 10;

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

    // 校准步骤（需要根据3.1节补充具体校准指令）

    // printf("Performing calibration...\n");
    // 此处应添加校准指令发送代码，例如：
    
    // unsigned char cal_cmd[] = {0x...};
    // write(uart_fd, cal_cmd, sizeof(cal_cmd));
    sleep(1); // 等待校准完成

    // 读取重量指令
    unsigned char read_cmd[] = {0xA3, 0x00, 0xA2, 0xA4, 0xA5};
    
    unsigned char rx_buffer[256];
    while (1) {
        // 发送读取命令
        if (write(uart_fd, read_cmd, sizeof(read_cmd)) < 0) {
            perror("Write command failed");
        }

        // 等待数据接收
        usleep(100000); // 100ms 等待响应

        // 读取响应数据
        int len = read(uart_fd, rx_buffer, sizeof(rx_buffer));
        if (len >= 10) { // 确保收到完整数据帧
            // 验证帧头
            if (rx_buffer[0] == 0xAA && rx_buffer[1] == 0xA3) 
            {
                // 校验验证（需要根据实际校验算法实现）
                //if (verify_checksum(rx_buffer, len)) 
                //{
                    // 解析符号位（第4字节）
                    //int sign = (rx_buffer[3] == 0) ? 1 : -1;
                    
                    // 解析重量值（第5-7字节）
                    unsigned int weight_raw = ((unsigned int)rx_buffer[4] << 16) |
                                             ((unsigned int)rx_buffer[5] << 8) |
                                             rx_buffer[6];
                    
                    // 转换为有符号浮点数（假设精度为0.01kg）
                    //float weight = sign * (weight_raw / 100.0f);
                    float weight =  weight_raw / 100.0f;
                    printf("Current Weight: %.2f kg\n", weight);
                //} else {
                //    printf("Checksum error!\n");
                //}
            }
        } else if (len > 0) {
            printf("Incomplete frame received (%d bytes)\n", len);
        }

        usleep(500000); // 500ms 读取间隔
    }

    close(uart_fd);
    return 0;
}

/*
// 校验函数实现（示例为异或校验，需要根据模块文档实现具体算法）
int verify_checksum(unsigned char *data, int length) {
    // 此处应实现实际的校验算法
    // 示例：校验最后两个字节是否为前8字节的异或值
    if (length < 10) return 0;
    
    unsigned char checksum = 0;
    for (int i = 0; i < 8; i++) {
        checksum ^= data[i];
    }
    
    return (checksum == data[8]) && (data[9] == 0xFF); // 根据示例数据假设
}
    */