#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <time.h>

#define I2C_DEVICE "/dev/i2c-6"
#define PCF8574_ADDR 0x30     // PCF8574T VCC = 5V​​，固定高位地址为 0x20，且 A2=1, A1=1, A0=1，I²C 地址 =0x27
#define DT_MASK      0x01     // DT连接在P0引脚
#define SCK_MASK     0x02     // SCK连接在P1引脚
#define DEBUG        1        // 调试信息开关

// 全局变量保存当前IO状态
static unsigned char current_io_state = 0xFF & ~SCK_MASK; // 初始状态：SCK低，其他输入
static int fd;                // I2C文件描述符
static long zero_value = 0;   // 零点校准值
static float scale_factor = 1.0; // 比例系数
static unsigned char buf;

// 错误处理宏
#define I2C_CHECK(fd, expr) \
do { \
    if ((expr) == -1) { \
        perror("I2C error"); \
        close(fd); \
        exit(EXIT_FAILURE); \
    } \
} while(0)

// 调试输出宏
#if DEBUG
#define DEBUG_PRINT(fmt, ...) fprintf(stderr, "DEBUG: " fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...)
#endif

// 读取DT引脚状态
int read_dt(int fd) {
    //unsigned char buf;
    if (read(fd, &buf, 1) != 1) {
        perror("read PCF8574 error");
        DEBUG_PRINT("PCF8574 read: 0x%02X\n", buf);
        //perror("读取PCF8574失败");
        return -1;
    }
    return (buf & DT_MASK) ? 1 : 0;
}

// 写入IO状态
void write_io(int fd, unsigned char state) {
    if (write(fd, &state, 1) != 1) {
        perror("write PCF8574 error");
        //perror("写入PCF8574失败");
        exit(EXIT_FAILURE);
    }
    current_io_state = state;
}

// 读取HX711原始数据
long hx711_read(int fd) {
    struct timespec ts = {0, 200000}; // 延迟200微秒
    int timeout = 100000; // 约10秒超时

    DEBUG_PRINT("Timeout remaining: %d\n", timeout);
    DEBUG_PRINT("waiting data DT...\n");
    while (timeout-- > 0) {
        int dt_state = read_dt(fd);
        if (dt_state < 0) return -1;
        if (dt_state == 0) {
            DEBUG_PRINT("Data ready signal detected\n");
            break;
        }
        nanosleep(&ts, NULL);
    }

    if (timeout <= 0) {
        DEBUG_PRINT("Error: Waiting for data ready timed out\n");
        return -1;
    }

    long data = 0;

    for (int i = 0; i < 24; i++) {
        // 时钟上升沿
        write_io(fd, current_io_state | SCK_MASK);
        nanosleep(&ts, NULL);

        // 读取DT
        unsigned char buf2;
        if (read(fd, &buf2, 1) != 1) {
            perror("read PCF8574 error");
            return -1;
        }
        int bit = (buf2 & DT_MASK) ? 1 : 0;

        data = (data << 1) | bit;

        DEBUG_PRINT("λ %02d: %d (READ IO BYTE: 0x%02X)\n", i + 1, bit, buf2);

        // 时钟下降沿
        write_io(fd, current_io_state & ~SCK_MASK);
        nanosleep(&ts, NULL);
    }

    // 设置增益为128，额外1个SCK脉冲
    for (int i = 0; i < 1; i++) {
        write_io(fd, current_io_state | SCK_MASK);
        nanosleep(&ts, NULL);
        write_io(fd, current_io_state & ~SCK_MASK);
        nanosleep(&ts, NULL);
    }

    // 符号扩展
    if (data & 0x00800000) data |= 0xFF000000;

    DEBUG_PRINT("Final raw data: %ld (0x%08lX)\n", data, data);
    return data;
}


// 校准传感器
void hx711_calibrate()
 {
    printf("Calibrating...\n");
    long sum = 0;
    for (int i = 0; i < 5; i++) {
        sum += hx711_read(fd);
        usleep(100000);
    }
    zero_value = sum / 5;

    float known_weight;
    printf("Enter known weight in grams: ");
    scanf("%f", &known_weight);
    getchar(); // 吃掉 scanf 留下的 \n
    printf("known_weight = %.2f\n", known_weight);
    printf("Place a known weight and press Enter...");
    getchar();

    sum = 0;
    for (int i = 0; i < 5; i++) {
        sum += hx711_read(fd);
        usleep(100000);
    }
    long known_value = sum / 5 - zero_value;
    scale_factor = known_value / known_weight; // 使用已知重量校准
}

// 获取重量
float get_weight() 
{
    long raw_value = hx711_read(fd);
    return (raw_value - zero_value) / scale_factor;
}


int main() {
    //int fd;
    
    // 打开I2C设备
    if ((fd = open(I2C_DEVICE, O_RDWR)) < 0) {
        perror("open I2C device error");
        exit(EXIT_FAILURE);
    }

    // 设置从机地址
    if (ioctl(fd, I2C_SLAVE, PCF8574_ADDR) < 0) {
        perror("set I2C error");
        close(fd);
        exit(EXIT_FAILURE);
    }

    // 初始化IO状态
    write_io(fd, current_io_state);
    DEBUG_PRINT("Initialization complete,IO:0x%02X\n", current_io_state);

    // hx711_calibrate();

    // 主循环
    while (1) {
        long raw = hx711_read(fd);


        int dt_state = read_dt(fd);
        DEBUG_PRINT("READ IO BYTE: 0x%02X | bit: %d\n", buf, dt_state);


        if (raw != -1) {
            // 转换为实际重量（需要根据传感器校准）
            float weight = raw / 1000.0; // 示例转换系数
            //float weight = get_weight();

            printf("Original value: %8ld | weight: %.2f kg\n", 
                  raw, weight);
        } else {
            printf("error:read data error\n");
        }
        sleep(1); // 每秒读取一次
    }

    close(fd);
    return 0;
}