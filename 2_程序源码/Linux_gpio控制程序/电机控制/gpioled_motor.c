#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#define GPIO_PATH_TEMPLATE "/sys/class/leds/%s/brightness"

// 全局控制标志
volatile sig_atomic_t keep_running = 1;

void signal_handler(int sig __attribute__((unused))) {
    keep_running = 0;
}

int set_gpio(const char *gpio_name, int value) {
    char path[128];
    int fd, ret;
    
    snprintf(path, sizeof(path), GPIO_PATH_TEMPLATE, gpio_name);
    
    if ((fd = open(path, O_WRONLY)) < 0) {
        fprintf(stderr, "Error opening %s: %s\n", path, strerror(errno));
        return -1;
    }
    
    char buf[2] = {0};
    snprintf(buf, sizeof(buf), "%d", value);
    ret = write(fd, buf, sizeof(buf));
    
    close(fd);
    
    if (ret < 0) {
        fprintf(stderr, "Write failed: %s\n", strerror(errno));
        return -1;
    }
    
    return 0;
}

void motor_forward() {
    set_gpio("gpio1b0", 1);
    set_gpio("gpio1b1", 0);
    printf("Motor: FORWARD\n");
}

void motor_backward() {
    set_gpio("gpio1b0", 0);
    set_gpio("gpio1b1", 1);
    printf("Motor: BACKWARD\n");
}

void motor_stop() {
    set_gpio("gpio1b0", 0);
    set_gpio("gpio1b1", 0);
    printf("Motor: STOPPED\n");
}

void print_help() {
    printf("Usage:\n");
    printf("  Single command mode: %s [command]\n", "motor_control");
    printf("  Interactive mode:    %s\n\n", "motor_control");
    printf("Available commands:\n");
    printf("  f    Start forward rotation\n");
    printf("  b    Start backward rotation\n");
    printf("  s       Stop motor\n");
    printf("  e       Exit interactive mode\n");
}

void interactive_mode() {
    char input[32];
    
    printf("Entering interactive mode (type 'exit' to quit)\n");
    while (keep_running) {
        printf("motor> ");
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) break;
        
        // 去除换行符
        input[strcspn(input, "\n")] = 0;
        
        if (strcmp(input, "f") == 0) {
            motor_forward();
        } else if (strcmp(input, "b") == 0) {
            motor_backward();
        } else if (strcmp(input, "s") == 0) {
            motor_stop();
        } else if (strcmp(input, "e") == 0) {
            break;
        } else if (strcmp(input, "help") == 0) {
            print_help();
        } else {
            printf("Invalid command. Available: f/b/s/e/help\n");
        }
    }
}

int main(int argc, char *argv[]) {
    // 注册信号处理
    signal(SIGINT, signal_handler);
    
    // 命令行参数模式
    if (argc > 1) {
        if (strcmp(argv[1], "f") == 0) {
            motor_forward();
        } else if (strcmp(argv[1], "b") == 0) {
            motor_backward();
        } else if (strcmp(argv[1], "s") == 0) {
            motor_stop();
        } else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_help();
        } else {
            fprintf(stderr, "Invalid command. Use --help for usage info\n");
            return 1;
        }
        return 0;
    }
    
    // 交互模式
    interactive_mode();
    motor_stop();  // 确保退出时停止
    
    return 0;
}