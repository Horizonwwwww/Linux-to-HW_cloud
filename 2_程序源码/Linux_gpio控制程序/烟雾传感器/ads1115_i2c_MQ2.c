#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <errno.h>

#define I2C_BUS "/dev/i2c-3"      // 根据实际 I2C 总线号修改
#define ADS1115_ADDR 0x48        // ADS1115 默认地址
#define CONFIG_REG 0x01          // 配置寄存器地址
#define CONVERSION_REG 0x00      // 转换结果寄存器地址

// ADS1115 配置参数（A0 通道单端输入，±4.096V 量程，连续转换模式）
//#define CONFIG_HIGH_BYTE 0x82    // 10000010: OS=1 (开始转换), MUX=000 (A0), PGA=001 (±4.096V), MODE=0 (连续转换)
#define CONFIG_HIGH_BYTE 0xC2  // 11000010：OS=1, MUX=100, PGA=001, MODE=0
#define CONFIG_LOW_BYTE 0x83     // 10000011: DR=100 (128 SPS), COMP_* 保持默认

#define THRESHOLD_VOLTAGE 0.6  //阈值设定

int main() {
    int i2c_fd;
    char buf[3];
    int16_t raw_value;
    float voltage;

    // 打开 I2C 总线
    i2c_fd = open(I2C_BUS, O_RDWR);
    if (i2c_fd < 0) {
        perror("Failed to open I2C bus");
        return EXIT_FAILURE;
    }

    // 设置从机地址（ADS1115）
    if (ioctl(i2c_fd, I2C_SLAVE, ADS1115_ADDR) < 0) {
        perror("Failed to set I2C slave address");
        close(i2c_fd);
        return EXIT_FAILURE;
    }

    // 配置 ADS1115
    buf[0] = CONFIG_REG;         // 写入配置寄存器
    buf[1] = CONFIG_HIGH_BYTE;  // 配置高字节
    buf[2] = CONFIG_LOW_BYTE;   // 配置低字节
    if (write(i2c_fd, buf, 3) != 3) {
        perror("Failed to configure ADS1115");
        close(i2c_fd);
        return EXIT_FAILURE;
    }

    // 等待首次转换完成（约 1/128 SPS ≈ 8ms）
    usleep(8000);

    printf("Reading MQ-2 sensor data from ADS1115 A0...\n");
    while (1) {
        // 读取转换结果
        buf[0] = CONVERSION_REG;  // 指定读取转换寄存器
        if (write(i2c_fd, buf, 1) != 1) {
            perror("Failed to select conversion register");
            break;
        }

        if (read(i2c_fd, buf, 2) != 2) {
            perror("Failed to read conversion result");
            break;
        }

        // 解析 16 位有符号值（ADS1115 数据为小端格式）
        raw_value = (buf[0] << 8) | buf[1];
        voltage = (raw_value * 4.096) / 32768.0;  // 计算实际电压值（±4.096V 量程）
        // printf("Raw Bytes: 0x%02X 0x%02X\n", buf[0], buf[1]);
        printf("Raw: %6d | Voltage: %.4f V\n", raw_value, voltage);
        if (voltage >= THRESHOLD_VOLTAGE) {
            printf("WARNING! MQ-2 exceeds threshold! \n");
            printf("MQ-2 Voltage: %.2fV \n", voltage);
        }
        sleep(1);
    }

    close(i2c_fd);
    return EXIT_SUCCESS;
}