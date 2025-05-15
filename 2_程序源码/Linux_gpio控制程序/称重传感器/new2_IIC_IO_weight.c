#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <time.h>

#define DEBUG 1  // 调试模式开关

// PCF8574T默认地址（根据实际地址修改）
#define PCF8574_ADDR 0x20

// HX711引脚映射（根据实际连接修改）
#define HX711_DT_PIN  0  // P0引脚作为数据输入
#define HX711_SCK_PIN 1  // P1引脚作为时钟输出

// HX711参数
#define HX711_GAIN 128   // 增益设置
#define CALIBRATION_FACTOR 10650.0 // 需根据实际传感器校准

int i2c_fd;

// 调试信息输出宏
#if DEBUG
#define debug_print(fmt, ...) \
    do { fprintf(stderr, "DEBUG: %s:%d:%s(): " fmt, \
    __FILE__, __LINE__, __func__, __VA_ARGS__); } while (0)
#else
#define debug_print(fmt, ...) 
#endif

// 初始化I2C连接
int init_i2c(const char* device, int addr) {
    int fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("Failed to open I2C device");
        return -1;
    }

    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        perror("Failed to set I2C address");
        close(fd);
        return -1;
    }

    // 初始化PCF8574T：设置DT为输入（高电平），SCK为低电平
    unsigned char config = (1 << HX711_DT_PIN) | (0 << HX711_SCK_PIN);
    if (write(fd, &config, 1) != 1) {
        perror("Failed to initialize PCF8574T");
        close(fd);
        return -1;
    }

    return fd;
}

// 读取PCF8574T引脚状态
unsigned char read_pcf8574() {
    unsigned char buffer;
    if (read(i2c_fd, &buffer, 1) != 1) {
        perror("Failed to read PCF8574T");
        return 0xFF;
    }
    debug_print("Raw PCF8574 read: 0x%02X\n", buffer);
    return buffer;
}

// 写入PCF8574T引脚状态
void write_pcf8574(unsigned char data) {
    if (write(i2c_fd, &data, 1) != 1) {
        perror("Failed to write PCF8574T");
    }
    debug_print("PCF8574 write: 0x%02X\n", data);
}

// 等待HX711数据就绪
int wait_ready(int timeout_ms) {
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (1) {
        unsigned char state = read_pcf8574();
        int dt_state = (state & (1 << HX711_DT_PIN)) ? 0 : 1;

        if (!dt_state) {
            debug_print("Data ready detected\n", NULL);
            return 1;
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed = (now.tv_sec - start.tv_sec) * 1000 + 
                      (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed > timeout_ms) {
            debug_print("Timeout waiting for data ready\n", NULL);
            return 0;
        }

        usleep(1000); // 1ms间隔检查
    }
}

// 读取HX711原始数据
long read_hx711() {
    unsigned char pcf_data = 0;
    long result = 0;

    // 设置初始状态：SCK低电平
    pcf_data = (1 << HX711_DT_PIN) | (0 << HX711_SCK_PIN);
    write_pcf8574(pcf_data);

    // 等待数据就绪
    if (!wait_ready(1000)) {
        debug_print("HX711 not ready\n", NULL);
        return -1;
    }

    // 读取24位数据 + 1位增益设置
    for (int i = 0; i < 24 + HX711_GAIN/64; i++) {
        // 产生时钟脉冲
        pcf_data |= (1 << HX711_SCK_PIN);
        write_pcf8574(pcf_data);
        usleep(1);  // 保持高电平
        
        // 读取数据位（DT状态）
        unsigned char state = read_pcf8574();
        int bit = (state & (1 << HX711_DT_PIN)) ? 0 : 1;
        result = (result << 1) | bit;

        pcf_data &= ~(1 << HX711_SCK_PIN);
        write_pcf8574(pcf_data);
        usleep(1);  // 保持低电平

        debug_print("Bit %d: %d\n", 24-i, bit);
    }

    // 转换补码到有符号数
    if (result & 0x800000) {
        result |= 0xFF000000;
    }

    debug_print("Raw data: %ld\n", result);
    return result;
}

int main() {
    // 初始化I2C
    const char* i2c_device = "/dev/i2c-2";  // 根据实际I2C总线修改
    if ((i2c_fd = init_i2c(i2c_device, PCF8574_ADDR)) < 0) {
        return EXIT_FAILURE;
    }

    // 主循环
    while (1) {
        long raw_data = read_hx711();
        if (raw_data == -1) {
            fprintf(stderr, "Error reading HX711\n");
            continue;
        }

        // 转换为实际重量（需要校准）
        float weight = raw_data / CALIBRATION_FACTOR;
        printf("Weight: %.2f kg\n", weight);

        sleep(1);
    }

    close(i2c_fd);
    return EXIT_SUCCESS;
}