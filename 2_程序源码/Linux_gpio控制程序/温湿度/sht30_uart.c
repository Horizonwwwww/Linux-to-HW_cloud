#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <pthread.h>  // 用于多线程
#include <stdbool.h>

#define UART_DEVICE "/dev/ttyS0"
#define BAUD_RATE B9600

int uart_fd;
volatile bool auto_mode = true; // 控制是否为自动模式
pthread_mutex_t mode_lock = PTHREAD_MUTEX_INITIALIZER;

// 初始化串口
int init_uart(const char *device, int baudrate) {
    printf("Opening UART device: %s...\n", device);
    uart_fd = open(device, O_RDWR | O_NOCTTY);
    if (uart_fd < 0) {
        perror("Failed to open UART device");
        return -1;
    }

    struct termios options;
    if (tcgetattr(uart_fd, &options) != 0) {
        perror("Failed to get UART attributes");
        close(uart_fd);
        return -1;
    }

    cfsetispeed(&options, baudrate);
    cfsetospeed(&options, baudrate);

    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag &= ~CRTSCTS;
    options.c_cflag |= CREAD | CLOCAL;

    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_oflag &= ~OPOST;

    if (tcsetattr(uart_fd, TCSANOW, &options) != 0) {
        perror("Failed to set UART attributes");
        close(uart_fd);
        return -1;
    }

    printf("UART configured: 9600 8N1\n");
    return uart_fd;
}

// 解析 SHT30 数据
void parse_data(const char *buffer) {
    float temp, humi;
    if (sscanf(buffer, "R:%fRH %fC", &humi, &temp) == 2) {
        printf("Temperature: %.2f°C\nHumidity: %.2f%%\n", temp, humi);
    } else {
        printf("Received raw data: %s\n", buffer);
    }
}

// 串口接收线程
void *uart_read_thread(void *arg) {
    char buffer[128];
    while (1) {
        pthread_mutex_lock(&mode_lock);
        bool is_auto = auto_mode;
        pthread_mutex_unlock(&mode_lock);

        if (!is_auto) {
            sleep(1); // 手动模式不读取，等待触发
            continue;
        }

        memset(buffer, 0, sizeof(buffer));
        int len = read(uart_fd, buffer, sizeof(buffer) - 1);
        if (len > 0) {
            buffer[len] = '\0';
            parse_data(buffer);
        } else if (len < 0) {
            perror("Error reading UART");
        } else {
            sleep(1);
        }
    }
    return NULL;
}

// 控制台输入线程
void *input_thread(void *arg) {
    char cmd[32];
    while (1) {
        printf("Enter command (Auto / Hand / Read): ");
        fflush(stdout);
        if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
            continue;
        }

        cmd[strcspn(cmd, "\n")] = 0;  // 去除换行

        if (strcasecmp(cmd, "Auto") == 0) {
            pthread_mutex_lock(&mode_lock);
            auto_mode = true;
            pthread_mutex_unlock(&mode_lock);
            write(uart_fd, "Auto\r\n", 6);
            printf("[Switched to AUTO mode]\n");
        } else if (strcasecmp(cmd, "Hand") == 0) {
            pthread_mutex_lock(&mode_lock);
            auto_mode = false;
            pthread_mutex_unlock(&mode_lock);
            write(uart_fd, "Hand\r\n", 6);
            printf("[Switched to HAND mode]\n");
        } else if (strcasecmp(cmd, "Read") == 0) {
            pthread_mutex_lock(&mode_lock);
            bool is_auto = auto_mode;
            pthread_mutex_unlock(&mode_lock);

            if (is_auto) {
                printf("Currently in AUTO mode. Switch to HAND mode to use 'Read'.\n");
            } else {
                write(uart_fd, "Read\r\n", 6);
                char buffer[128] = {0};
                int len = read(uart_fd, buffer, sizeof(buffer) - 1);
                if (len > 0) {
                    buffer[len] = '\0';
                    parse_data(buffer);
                } else {
                    printf("No data received after 'Read' command.\n");
                }
            }
        } else {
            printf("Unknown command: %s\n", cmd);
        }
    }
    return NULL;
}

int main() {
    printf("Starting SHT30 UART controller...\n");

    if (init_uart(UART_DEVICE, BAUD_RATE) < 0) {
        return EXIT_FAILURE;
    }

    // 启动默认的自动模式
    write(uart_fd, "Auto\r\n", 6);
    printf("[Default: AUTO mode started]\n");

    // 创建线程
    pthread_t read_tid, input_tid;
    pthread_create(&read_tid, NULL, uart_read_thread, NULL);
    pthread_create(&input_tid, NULL, input_thread, NULL);

    // 等待线程结束（实际上不会结束）
    pthread_join(read_tid, NULL);
    pthread_join(input_tid, NULL);

    close(uart_fd);
    return EXIT_SUCCESS;
}
