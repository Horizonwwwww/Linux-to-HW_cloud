#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gpiod.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#define GPIO_CHIP    "gpiochip0"   // GPIO 控制器名称
#define GPIO_PIN     18            // 根据实际 GPIO 编号调整
#define RETRIES      5
#define TIMEOUT_US   1000          // 超时时间（微秒）

// 获取时间差（微秒）
static long time_since(struct timespec *start) 
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start->tv_sec) * 1000000 + (now.tv_nsec - start->tv_nsec) / 1000;
}

// 读取单比特数据
static int read_bit(struct gpiod_line *line) 
{
    struct timespec start;
    int val;

    // 等待低电平开始
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (1) {
        val = gpiod_line_get_value(line);
        if (val < 0) return -1;
        if (val == 0) break;
        if (time_since(&start) > 250) return -1;
        usleep(1);
    }

    // 等待高电平开始
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (1) {
        val = gpiod_line_get_value(line);
        if (val < 0) return -1;
        if (val == 1) break;
        if (time_since(&start) > 250) return -1;
        usleep(1);
    }

    // 测量高电平持续时间
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (1) {
        val = gpiod_line_get_value(line);
        if (val < 0) return -1;
        if (val == 0) break;
        if (time_since(&start) > 300) {
            fprintf(stderr, "High level too long (timeout)\n");
            return -1;
        }
        usleep(1);
    }

    long duration = time_since(&start);
    printf("duration = %ldus\n", duration);
    return (duration > 60) ? 1 : 0;
}

int main() 
{
    sleep(2);  // 等待模块稳定
    struct gpiod_chip *chip;
    struct gpiod_line *line;
    int ret = EXIT_FAILURE;
    unsigned char data[5] = {0};

    // 打开 GPIO 控制器
    chip = gpiod_chip_open_by_name(GPIO_CHIP);
    if (!chip) {
        fprintf(stderr, "Failed to open GPIO controller: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    // 获取 GPIO 线路
    line = gpiod_chip_get_line(chip, GPIO_PIN);
    if (!line) {
        fprintf(stderr, "Failed to get GPIO pin: %s\n", strerror(errno));
        //perror("获取GPIO线路失败");
        goto cleanup_chip;
    }

    // 发送起始信号（拉低 18ms 后拉高 20-40us）
    if (gpiod_line_request_output(line, "dht11", 1) < 0) { // 初始高电平
        fprintf(stderr, "Failed to set GPIO out: %s\n", strerror(errno));
        //perror("设置输出模式失败");
        goto cleanup_line;
    }
    gpiod_line_set_value(line, 0);
    usleep(18000);
    gpiod_line_set_value(line, 1);
    usleep(30);

    // 切换为输入模式
    gpiod_line_release(line);
    if (gpiod_line_request_input(line, "dht11") < 0) {
        fprintf(stderr, "Failed to set GPIO in: %s\n", strerror(errno));
        //perror("设置输入模式失败");
        goto cleanup_line;
    }

    // 等待传感器响应（80us 低电平 + 80us 高电平）
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (gpiod_line_get_value(line) == 1) {
        if (time_since(&start) > 200) 
        {
            fprintf(stderr, "Sensor no response\n");
            //fprintf(stderr, "传感器无响应\n");
            goto cleanup_line;
        }
    }

    // 读取 40 位数据
    for (int i = 0; i < 40; i++) 
    {
        int bit = read_bit(line);
        if (bit < 0) 
        {
            fprintf(stderr, "Failed to read the data bit\n");
            //fprintf(stderr, "读取数据位失败\n");
            goto cleanup_line;
        }
        printf("Bit %d = %d\n", i, bit); 
        if (bit) {
            data[i/8] |= (1 << (7 - (i % 8)));
        }
    }

    // 校验和验证
    if (data[4] != (data[0] + data[1] + data[2] + data[3])) 
    {
        fprintf(stderr, "Checksum error\n");
        //fprintf(stderr, "校验和错误\n");
        goto cleanup_line;
    }

    // 输出结果
    printf("Humidity: %d%%, temperature: %d℃\n", data[0], data[2]);
    //printf("湿度: %d%%, 温度: %d℃\n", data[0], data[2]);
    ret = EXIT_SUCCESS;

cleanup_line:
    gpiod_line_release(line);
cleanup_chip:
    gpiod_chip_close(chip);
    return ret;
}