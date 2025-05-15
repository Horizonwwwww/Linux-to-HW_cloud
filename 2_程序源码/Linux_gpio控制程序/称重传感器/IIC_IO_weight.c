#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gpiod.h>
#include <time.h>

#define I2C_SDA_PIN 140   // RK3568 GPIO140
#define I2C_SCL_PIN 141   // RK3568 GPIO141
#define PCF8574_ADDR 0x20 // PCF8574T I2C地址

// HX711使用的扩展GPIO定义
#define HX711_DT_BIT 0    // P0作为DT输入
#define HX711_SCK_BIT 1   // P1作为SCK输出

// 延时函数（微秒级）
static void delay_us(int microseconds) {
    struct timespec ts = {
        .tv_sec = microseconds / 1000000,
        .tv_nsec = (microseconds % 1000000) * 1000
    };
    nanosleep(&ts, NULL);
}

// 软件I2C总线控制结构体
struct i2c_soft {
    struct gpiod_line *sda;
    struct gpiod_line *scl;
};

// 初始化I2C总线
int i2c_init(struct i2c_soft *i2c, const char *chipname) {
    struct gpiod_chip *chip = gpiod_chip_open_by_name(chipname);
    if (!chip) {
        perror("Error opening GPIO chip");
        return -1;
    }

    // 获取GPIO线
    i2c->sda = gpiod_chip_get_line(chip, I2C_SDA_PIN);
    i2c->scl = gpiod_chip_get_line(chip, I2C_SCL_PIN);
    if (!i2c->sda || !i2c->scl) {
        perror("Error getting GPIO lines");
        gpiod_chip_close(chip);
        return -1;
    }

    // 配置为输出模式，初始高电平
    if (gpiod_line_request_output(i2c->sda, "hx711", 1) < 0 ||
        gpiod_line_request_output(i2c->scl, "hx711", 1) < 0) {
        perror("Error requesting GPIO lines");
        gpiod_chip_close(chip);
        return -1;
    }

    return 0;
}

// I2C起始条件
static void i2c_start(struct i2c_soft *i2c) {
    gpiod_line_set_value(i2c->sda, 1);
    gpiod_line_set_value(i2c->scl, 1);
    delay_us(5);
    gpiod_line_set_value(i2c->sda, 0);
    delay_us(5);
    gpiod_line_set_value(i2c->scl, 0);
}

// I2C停止条件
static void i2c_stop(struct i2c_soft *i2c) {
    gpiod_line_set_value(i2c->sda, 0);
    gpiod_line_set_value(i2c->scl, 1);
    delay_us(5);
    gpiod_line_set_value(i2c->sda, 1);
    delay_us(5);
}

// 写入字节并检查ACK
static int i2c_write_byte(struct i2c_soft *i2c, uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        gpiod_line_set_value(i2c->sda, (byte >> i) & 0x01);
        delay_us(2);
        gpiod_line_set_value(i2c->scl, 1);
        delay_us(5);
        gpiod_line_set_value(i2c->scl, 0);
    }

    // 检查ACK
    gpiod_line_set_direction(i2c->sda, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_set_value(i2c->scl, 1);
    delay_us(2);
    int ack = gpiod_line_get_value(i2c->sda);
    gpiod_line_set_value(i2c->scl, 0);
    gpiod_line_set_direction(i2c->sda, GPIOD_LINE_DIRECTION_OUTPUT);

    return ack;
}

// 读取PCF8574状态
static int pcf8574_read(struct i2c_soft *i2c, uint8_t *data) {
    i2c_start(i2c);
    if (i2c_write_byte(i2c, (PCF8574_ADDR << 1) | 1)) {
        i2c_stop(i2c);
        return -1;
    }

    gpiod_line_set_direction(i2c->sda, GPIOD_LINE_DIRECTION_INPUT);
    *data = 0;
    for (int i = 7; i >= 0; i--) {
        gpiod_line_set_value(i2c->scl, 1);
        delay_us(2);
        *data |= gpiod_line_get_value(i2c->sda) << i;
        gpiod_line_set_value(i2c->scl, 0);
        delay_us(2);
    }
    i2c_stop(i2c);
    gpiod_line_set_direction(i2c->sda, GPIOD_LINE_DIRECTION_OUTPUT);

    return 0;
}

// 设置SCK引脚状态
static void hx711_set_sck(struct i2c_soft *i2c, int state) {
    uint8_t data;
    if (pcf8574_read(i2c, &data) == 0) {
        data = state ? (data | (1 << HX711_SCK_BIT)) : 
                      (data & ~(1 << HX711_SCK_BIT));
        i2c_start(i2c);
        i2c_write_byte(i2c, PCF8574_ADDR << 1);
        i2c_write_byte(i2c, data);
        i2c_stop(i2c);
    }
}

// 读取DT引脚状态
static int hx711_get_dt(struct i2c_soft *i2c) {
    uint8_t data;
    if (pcf8574_read(i2c, &data) == 0) {
        return (data >> HX711_DT_BIT) & 0x01;
    }
    return -1;
}

// 读取HX711数据
long hx711_read_data(struct i2c_soft *i2c) {
    long value = 0;
    int retry = 0;

    // 等待DT变低
    while (hx711_get_dt(i2c) == 1) {
        if (retry++ > 100) return 0;
        delay_us(100);
    }

    // 读取24位数据
    for (int i = 0; i < 24; i++) {
        hx711_set_sck(i2c, 1);
        delay_us(1);
        value = (value << 1) | hx711_get_dt(i2c);
        hx711_set_sck(i2c, 0);
        delay_us(1);
    }

    // 设置增益（1个脉冲对应增益128）
    for (int i = 0; i < 1; i++) {
        hx711_set_sck(i2c, 1);
        delay_us(1);
        hx711_set_sck(i2c, 0);
        delay_us(1);
    }

    // 补码转换
    if (value & 0x800000) {
        value |= 0xFF000000;
    }

    return value;
}


// 校准传感器
void hx711_calibrate()
 {
    printf("Calibrating...\n");
    long sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += hx711_read_data(&i2c);
        usleep(100000);
    }
    zero_value = sum / 10;

    float known_weight;
    printf("Enter known weight in grams: ");
    scanf("%f", &known_weight);
    getchar(); // 吃掉 scanf 留下的 \n
    printf("known_weight = %.2f\n", known_weight);
    printf("Place a known weight and press Enter...");
    getchar();

    sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += hx711_read_data(&i2c);
        usleep(100000);
    }
    long known_value = sum / 10 - zero_value;
    scale_factor = known_value / known_weight; // 使用已知重量校准
}

// 获取重量
float get_weight() 
{
    long raw_value = hx711_read_data(&i2c);
    return (raw_value - zero_value) / scale_factor;
}



int main() {
    struct i2c_soft i2c;
    
    // 初始化I2C总线
    if (i2c_init(&i2c, "gpiochip0") < 0) {
        fprintf(stderr, "Failed to initialize I2C\n");
        return EXIT_FAILURE;
    }

    // 初始化HX711
    hx711_set_sck(&i2c, 0); // 初始SCK低电平
    printf("HX711 Initialized\n");

    hx711_calibrate();

    while (1) {
        
        long raw_data = hx711_read_data(&i2c);
        float weight = get_weight();
        
        // 转换为实际压力值（需根据传感器规格校准）
        //float pressure = (float)raw_data / 1000.0; // 示例转换
        
        printf("Raw: %ld | Weight: %.2f g\n", raw_data, weight);
        sleep(1);
    }

    // 清理资源（通常不会执行到这里）
    gpiod_line_release(i2c.sda);
    gpiod_line_release(i2c.scl);
    return EXIT_SUCCESS;
}