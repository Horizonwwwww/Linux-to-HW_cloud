#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define I2C_DEV "/dev/i2c-6"  // 根据实际I2C总线修改
#define PCF8574_ADDR 0x30     // PCF8574T默认地址(0x20)

// L298N控制引脚映射(PCF8574T的GPIO)
#define IN1_BIT 0
#define IN2_BIT 1

int i2c_fd;

// 初始化I2C通信
int init_i2c() {
    i2c_fd = open(I2C_DEV, O_RDWR);
    if (i2c_fd < 0) {
        perror("Failed to open I2C device");
        return -1;
    }

    if (ioctl(i2c_fd, I2C_SLAVE, PCF8574_ADDR) < 0) {
        perror("Failed to set I2C slave address");
        close(i2c_fd);
        return -1;
    }

    return 0;
}

// 向PCF8574T写入数据
int write_pcf8574(unsigned char data) {
    if (write(i2c_fd, &data, 1) != 1) {
        perror("Failed to write to PCF8574");
        return -1;
    }
    return 0;
}

// 控制电机状态
void control_motor(int in1, int in2) {
    unsigned char output = 0;
    
    if (in1) output |= (1 << IN1_BIT);
    if (in2) output |= (1 << IN2_BIT);
    
    if (write_pcf8574(output) == 0) {
        printf("控制信号: IN1=%d, IN2=%d\n", in1, in2);
    }
}

int main() {
    if (init_i2c() < 0) {
        return EXIT_FAILURE;
    }

    printf("L298N 控制程序已启动 (通过I2C PCF8574T)\n");
    printf("命令说明:\n");
    printf("  f - 正转\n");
    printf("  r - 反转\n");
    printf("  s - 停止\n");
    printf("  q - 退出\n");

    char cmd;
    while (1) {
        printf("请输入命令: ");
        scanf(" %c", &cmd);

        switch (cmd) {
            case 'f':
                control_motor(1, 0);
                printf("[状态] 正转\n");
                break;
            case 'r':
                control_motor(0, 1);
                printf("[状态] 反转\n");
                break;
            case 's':
                control_motor(0, 0);
                printf("[状态] 停止\n");
                break;
            case 'q':
                printf("程序退出\n");
                close(i2c_fd);
                return EXIT_SUCCESS;
            default:
                printf("无效命令，请重新输入\n");
        }
    }
}