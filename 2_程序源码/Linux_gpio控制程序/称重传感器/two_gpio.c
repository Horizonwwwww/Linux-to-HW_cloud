
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

// 定义 GPIO 编号（需根据实际连接修改）
#define HX711_SCK_GPIO 111  // GPIO3_B7
#define HX711_DT_GPIO  112  // GPIO3_C0


// 全局校准参数
long zero_value = 0;      // 零点值
float scale_factor = 1.0; // 比例系数




//___________________________________________________________

// 导出 GPIO 并设置方向
int gpio_export(int gpio) 
{
    //检查 GPIO 是否已导出​
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", gpio);
    if (access(path, F_OK) == 0) return 0; // 已导出

    //打开 /sys/class/gpio/export
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) return -1;

    //写入 GPIO 编号到 export
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", gpio);
    if (write(fd, buf, strlen(buf)) < 0)
    {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

// GPIO 设置方向
int gpio_set_direction(int gpio, const char *direction) 
{
    //构造 GPIO 方向文件路径
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio);
    //打开方向文件
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    //写入方向设置
    if (write(fd, direction, strlen(direction)) < 0) 
    {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}


int gpio_set_value(int gpio, int value) 
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    char c = value ? '1' : '0';
    if (write(fd, &c, 1) < 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

int gpio_get_value(int gpio) 
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char c;
    if (read(fd, &c, 1) < 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return (c == '1') ? 1 : 0;
}





//___________________________________________________________

// 初始化 HX711
void hx711_init() 
{
    // 导出并设置 SCK 为输出，DT 为输入
    if (gpio_export(HX711_SCK_GPIO) < 0 || gpio_set_direction(HX711_SCK_GPIO, "out") < 0) {
        perror("Failed to init SCK");
        exit(1);
    }
    if (gpio_export(HX711_DT_GPIO) < 0 || gpio_set_direction(HX711_DT_GPIO, "in") < 0) {
        perror("Failed to init DT");
        exit(1);
    }
    gpio_set_value(HX711_SCK_GPIO, 0);
}

// 读取 HX711 的原始数据
long hx711_read() 
{
    long value = 0;

    // 等待数据就绪（DT 为低电平）
    while (gpio_get_value(HX711_DT_GPIO) == 1) usleep(1000);

    // 读取 24 位数据
    for (int i = 0; i < 24; i++) {
        gpio_set_value(HX711_SCK_GPIO, 1);
        usleep(1);
        value = (value << 1) | gpio_get_value(HX711_DT_GPIO);
        gpio_set_value(HX711_SCK_GPIO, 0);
        usleep(1);
    }

    // 发送第 25 个脉冲设置增益（128）
    gpio_set_value(HX711_SCK_GPIO, 1);
    usleep(1);
    gpio_set_value(HX711_SCK_GPIO, 0);
    usleep(1);

    // 转换为有符号整数（补码）
    if (value & 0x800000)
    {
        value |= 0xFF000000;
    }

    return value;
}

// 校准传感器
void hx711_calibrate() {
    printf("Calibrating...\n");
    long sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += hx711_read();
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
        sum += hx711_read();
        usleep(100000);
    }
    long known_value = sum / 10 - zero_value;
    scale_factor = known_value / known_weight; // 使用已知重量校准
}

// 获取重量
float get_weight() 
{
    long raw_value = hx711_read();
    return (raw_value - zero_value) / scale_factor;
}

//___________________________________________________________

int main() {
    hx711_init();
    hx711_calibrate();

    while (1) {
        float weight = get_weight();
        printf("Weight: %.2f g\n", weight);
        sleep(1);
    }

    return 0;
}

