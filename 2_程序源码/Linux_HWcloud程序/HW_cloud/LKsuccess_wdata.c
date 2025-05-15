
#include <stdio.h>
#include <string.h>
#include "iota_init.h"
#include "iota_datatrans.h"
#include "string_util.h"
#include "log_util.h"
#include "iota_cfg.h"
#include "mqttv5_util.h"
#include "cJSON.h"
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
//---------------------------------------------------------------
#include <termios.h>
#include <errno.h>
#include <pthread.h>  // 用于多线程
#include <stdbool.h>

#include <linux/i2c-dev.h>
#include <i2c/smbus.h>

#define I2C_BUS "/dev/i2c-3"
#define ADS1115_ADDR 0x48
#define CONFIG_REG 0x01
#define CONVERSION_REG 0x00

#define CONFIG_HIGH_BYTE 0x42  // MUX = 100 (AIN0-GND), PGA = 001, MODE = 0
#define CONFIG_LOW_BYTE 0x83     // 10000011: DR=100 (128 SPS), COMP_* 保持默认


#define LED_PATH "/sys/class/leds/gpio3b5/brightness"

#define UART_DEVICE "/dev/ttyS0"
#define BAUD_RATE B9600
#define WEIGHT_UART_DEVICE "/dev/ttyS3"


//---------------------------------------------------------------
char *g_address = "82be2f428c.st1.iotda-device.cn-north-4.myhuaweicloud.com"; 
char *g_port = "8883";
// deviceId, The mqtt protocol requires the user name to be filled in.
// Please fill in the deviceId
char *g_deviceId = "67d3ded4375e694aa6927974_test_linux"; 
char *g_password = "KK654321";
//---------------------------------------------------------------

float current_weight = 0.0f;
pthread_mutex_t weight_lock = PTHREAD_MUTEX_INITIALIZER;
int weight_uart_fd;

float current_mq2_voltage = 0.0f;
pthread_mutex_t mq2_lock = PTHREAD_MUTEX_INITIALIZER;
int i2c_fd;

int g_motor_status = 0; // 默认关闭状态
int g_led_status = 0; // 默认关闭状态
float current_temperature = 20;
float current_humidity = 15;

int uart_fd;
pthread_mutex_t sensor_lock = PTHREAD_MUTEX_INITIALIZER;

void TimeSleep(int ms)
{
#if defined(WIN32) || defined(WIN64)
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

static void MyPrintLog(int level, char *format, va_list args)
{
    vprintf(format, args);
    /*
     * if you want to printf log in system log files,you can do this:
     * vsyslog(level, format, args);
     */
}



// 独立的UART读取线程
// --------------- UART_温湿度 --------------------------------------------------------------------------------------------

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

// 数据解析函数修改
void parse_data(const char *buffer) 
{ 
    float temp, humi;
    if (sscanf(buffer, "R:%fRH %fC", &humi, &temp) == 2) {
        pthread_mutex_lock(&sensor_lock);
        current_temperature = temp;
        current_humidity = humi;
        pthread_mutex_unlock(&sensor_lock);
        
        printf("Temperature: %.2f°C\nHumidity: %.2f%%\n", temp, humi);
    }
}


void *uart_read_thread(void *arg) 
{ 
    char buffer[256] = {0};
    int idx = 0;

    while (1) {
        char ch;
        int len = read(uart_fd, &ch, 1);
        if (len > 0) {
            if (ch == '\n' || ch == '\r') {
                if (idx > 0) {
                    buffer[idx] = '\0'; // 添加字符串终止符
                    parse_data(buffer);
                    idx = 0; // 重置索引
                }
            } else {
                if (idx < sizeof(buffer) - 1) {
                    buffer[idx++] = ch;
                }
            }
        }
        usleep(1000); // 1ms等待
    }
    return NULL;
}

//称重传感器初始化函数
// ---------------------- weight ---------------------------------------------------------------------------------
int init_weight_uart() {
    weight_uart_fd = open(WEIGHT_UART_DEVICE, O_RDWR | O_NOCTTY);
    if (weight_uart_fd < 0) {
        perror("Failed to open Weight UART");
        return -1;
    }

    struct termios options;
    tcgetattr(weight_uart_fd, &options);
    cfsetispeed(&options, B9600);
    cfsetospeed(&options, B9600);
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_oflag &= ~OPOST;
    
    if (tcsetattr(weight_uart_fd, TCSANOW, &options) != 0) {
        perror("Weight UART config error");
        close(weight_uart_fd);
        return -1;
    }
    return 0;
}

// 称重数据读取解析函数
void parse_weight_data(unsigned char *buffer, int len) 
{
    if (len >= 10 && buffer[0] == 0xAA && buffer[1] == 0xA3) {
        unsigned int weight_raw = ((unsigned int)buffer[4] << 16) |
                                 ((unsigned int)buffer[5] << 8) |
                                 buffer[6];
        
        // 添加校准系数
        float calibration_factor = 1.0f / 4.59f; // 反向补偿，2.66/0.58
        float weight = (weight_raw / 100.0f) * calibration_factor;
        
        pthread_mutex_lock(&weight_lock);
        current_weight = weight;
        pthread_mutex_unlock(&weight_lock);
        
        printf("Weight: %.2f kg\n", weight);
    }
}

void *weight_read_thread(void *arg) {
    unsigned char rx_buffer[256];
    unsigned char read_cmd[] = {0xA3, 0x00, 0xA2, 0xA4, 0xA5};
    
    while (1) {
        // 发送读取命令
        write(weight_uart_fd, read_cmd, sizeof(read_cmd));
        
        usleep(100000); // 等待响应
        int len = read(weight_uart_fd, rx_buffer, sizeof(rx_buffer));
        
        if (len > 0) {
            parse_weight_data(rx_buffer, len);
        }
        
        usleep(500000); // 500ms读取间隔
    }
    return NULL;
}


// 烟雾传感器
// ---------------------- MQ-2 ---------------------------------------------------------------------------------
// ====================== ADS1115初始化函数 ======================
int init_mq2_sensor() {
    // 打开I2C总线
    i2c_fd = open(I2C_BUS, O_RDWR);
    if (i2c_fd < 0) {
        perror("Failed to open I2C bus");
        return -1;
    }

    // 设置从机地址
    if (ioctl(i2c_fd, I2C_SLAVE, ADS1115_ADDR) < 0) {
        perror("Failed to set I2C slave address");
        close(i2c_fd);
        return -1;
    }

    char buflk[3];
    // 配置 ADS1115
    buflk[0] = CONFIG_REG;         // 写入配置寄存器
    buflk[1] = CONFIG_HIGH_BYTE;  // 配置高字节
    buflk[2] = CONFIG_LOW_BYTE;   // 配置低字节

    int write_ads_config_smbus(int fd, uint8_t high, uint8_t low) {
    uint8_t data[2] = { high, low };
    int res = i2c_smbus_write_i2c_block_data(fd, CONFIG_REG, 2, data);
    if (res < 0) {
        perror("smbus config write failed");
        return -1;
    }
    return 0;
}

    usleep(8000); // 等待首次转换
    return 0;
}

// ====================== MQ-2数据读取函数 ======================
void read_mq2_value() {
    int16_t raw_value;

    // 使用SMBus读取数据
    int res = i2c_smbus_read_word_data(i2c_fd, CONVERSION_REG);
    if (res < 0) {
        perror("SMBus MQ2 read failed");
        return;
    }

    // 注意：SMBus返回的是低字节在前（小端），需要转换字节顺序
    raw_value = ((res & 0xFF) << 8) | ((res >> 8) & 0xFF);

    float voltage = (raw_value * 4.096) / 32768.0;

    pthread_mutex_lock(&mq2_lock);
    current_mq2_voltage = voltage;
    pthread_mutex_unlock(&mq2_lock);
}

const char* get_mq2_status(float voltage) {
    if (voltage <= 0.6f) {
        return "low";
    } else if (voltage <= 0.8f) {
        return "middle";
    } else {
        return "high";
    }
}



//LK    上报属性

// ---------------------- Property reporting ---------------------------------------------------------------------------------
static void Test_PropertiesReport(void)
{
    const int serviceNum = 1; // 单个服务
    ST_IOTA_SERVICE_DATA_INFO services[serviceNum];

    //  T、H 精度转换
    char temp_str[16], humi_str[16];
    snprintf(temp_str, sizeof(temp_str), "%.2f", current_temperature);
    snprintf(humi_str, sizeof(humi_str), "%.2f", current_humidity);

    // 获取重量值
    float weight;
    pthread_mutex_lock(&weight_lock);
    weight = current_weight;
    pthread_mutex_unlock(&weight_lock);
    char weight_str[16];
    snprintf(weight_str, sizeof(weight_str), "%.2f", weight);

    // 获取MQ-2值
    float mq2_voltage;
    pthread_mutex_lock(&mq2_lock);
    mq2_voltage = current_mq2_voltage;
    pthread_mutex_unlock(&mq2_lock);
    // 根据电压值获取状态字符串
    const char* mq2_status = get_mq2_status(mq2_voltage);

    // 使用cJSON构建复杂属性结构
    cJSON *properties = cJSON_CreateObject();
    cJSON_AddNumberToObject(properties, "motor", g_motor_status); // 使用状态变量     // 电机状态
    cJSON_AddNumberToObject(properties, "LED", g_led_status); // 使用状态变量     // LED
    cJSON_AddNumberToObject(properties, "Temperature", atof(temp_str));  // 温度
    cJSON_AddNumberToObject(properties, "Humidity", atof(humi_str));  // 湿度
    cJSON_AddNumberToObject(properties, "weight", atof(weight_str));   // 重量
    cJSON_AddStringToObject(properties, "MQ_2", mq2_status);      // 烟雾传感器
    char *properties_str = cJSON_PrintUnformatted(properties);

    // 服务数据填充
    services[0].event_time = GetEventTimesStamp();  // 动态时间戳
    services[0].service_id = "K5";                  // 服务ID
    services[0].properties = properties_str;        // 属性数据

    // 执行属性上报
    int messageId = IOTA_PropertiesReport(services, serviceNum, 0, NULL);
    if (messageId < 0) {
        PrintfLog(EN_LOG_LEVEL_ERROR, "properties_test: Test_PropertiesReport() failed, messageId %d\n", messageId);
    }

    // 内存清理
    cJSON_Delete(properties);
    free(properties_str);
    MemFree(&services[0].event_time);
}






//  在SET后控制LED

// --------------- 控制LED --------------------------------------------------------------------------------------------

void write_to_file(const char *filename, const char *value) {
    int fd = open(filename, O_WRONLY);
    if (fd < 0) {
        perror("Error opening file");
        return;
    }
    
    if (write(fd, value, strlen(value)) < 0) {
        perror("Error writing to file");
    }
    
    close(fd);
}


static void SetLEDHardware(void)
{
    // 1. 设置 LED（GPIO3_B5）高电平
    char val_str[2]; // 因为只写 0 或 1，够用了
    snprintf(val_str, sizeof(val_str), "%d", g_led_status); // write_to_file要求字符串指针（const char *），但g_led_status被定义为int，所以需要转换

    write_to_file(LED_PATH, val_str);
    printf("GPIO3_B5 update to %d \n",g_led_status);
    sleep(2);  // 等待 2 秒
  printf("lk_linux nice!\n");
}


// 解析云端下发的属性设置指令
// ---------------- Set Device Properties --------------------------------------------------------------------------
static void Test_PropSetResponse(char *requestId)
{
    int messageId = IOTA_PropertiesSetResponse(requestId, 0, "success", NULL);
    if (messageId < 0) {
        PrintfLog(EN_LOG_LEVEL_ERROR, "properties_test: Test_PropSetResponse() failed, messageId %d\n", messageId);
    }
}


void HandlePropertiesSet(EN_IOTA_PROPERTY_SET *rsp) {
    if (!rsp) return;

    // 调试日志增强
    PrintfLog(EN_LOG_LEVEL_DEBUG, "[SET] Received request_id=%s", rsp->request_id);

    for (int i = 0; i < rsp->services_count; i++) {
        const char *service_id = rsp->services[i].service_id;
        const char *properties = rsp->services[i].properties;

        // 安全解析JSON
        cJSON *props = cJSON_Parse(properties);
        if (!props) {
            PrintfLog(EN_LOG_LEVEL_ERROR, "[SET] Invalid JSON in service %s", service_id);
            continue;
        }

        // 处理K5服务
        if (strcmp(service_id, "K5") == 0) {
        
        
         // 处理motor属性
            if (cJSON_HasObjectItem(props, "motor")) 
            {
            cJSON *motor = cJSON_GetObjectItem(props, "motor");
            if (motor && cJSON_IsNumber(motor)) {
                int new_val = motor->valueint;
                
                // 验证取值范围
                if (new_val == 0 || new_val == 1) {
                    g_motor_status = new_val;
                    PrintfLog(EN_LOG_LEVEL_INFO, "[SET] Motor set to %d", new_val);
                    
                    // 实际控制电机（示例）
           //         SetMotorHardware(new_val); // 需实现硬件接口
                } else {
                    PrintfLog(EN_LOG_LEVEL_WARNING, "[SET] Invalid motor value: %d", new_val);
                }
            } else {
                PrintfLog(EN_LOG_LEVEL_ERROR, "[SET] Missing/invalid motor property");
            }
            }
            
            
                // 处理LED属性
            if (cJSON_HasObjectItem(props, "LED")) 
            {
            cJSON *LED = cJSON_GetObjectItem(props, "LED");
            if (LED && cJSON_IsNumber(LED)) {
                int new_led = LED->valueint;
                
                // 验证取值范围
                if (new_led == 0 || new_led == 1) {
                    g_led_status = new_led;
                    PrintfLog(EN_LOG_LEVEL_INFO, "[SET] LED set to %d", new_led);
                    
                    // 实际控制LED
                    SetLEDHardware(); // 需实现硬件接口
                } else {
                    PrintfLog(EN_LOG_LEVEL_WARNING, "[SET] Invalid motor value: %d", new_led);
                }
            } else {
                PrintfLog(EN_LOG_LEVEL_ERROR, "[SET] Missing/invalid motor property");
            }
            }
        }
        
        cJSON_Delete(props);
    }

    Test_PropSetResponse(rsp->request_id);
}



// 返回当前设备属性值

// --------------- Query Device Properties --------------------------------------------------------------------------------------------


static void Test_PropGetResponse(char *requestId)
{
    const int serviceNum = 1; // 单个服务
    ST_IOTA_SERVICE_DATA_INFO services[serviceNum];

    //  T、H 精度转换
    char temp_str[16], humi_str[16];
    snprintf(temp_str, sizeof(temp_str), "%.2f", current_temperature);
    snprintf(humi_str, sizeof(humi_str), "%.2f", current_humidity);

    // 获取重量值
    float weight;
    pthread_mutex_lock(&weight_lock);
    weight = current_weight;
    pthread_mutex_unlock(&weight_lock);
    char weight_str[16];
    snprintf(weight_str, sizeof(weight_str), "%.2f", weight);

    // 获取MQ-2值
    float mq2_voltage;
    pthread_mutex_lock(&mq2_lock);
    mq2_voltage = current_mq2_voltage;
    pthread_mutex_unlock(&mq2_lock);
    // 根据电压值获取状态字符串
    const char* mq2_status = get_mq2_status(mq2_voltage);

    // 使用cJSON构建复杂属性结构
    cJSON *properties = cJSON_CreateObject();
    cJSON_AddNumberToObject(properties, "motor", g_motor_status); // 使用状态变量     // 电机状态
    cJSON_AddNumberToObject(properties, "LED", g_led_status); // 使用状态变量     // LED
    cJSON_AddNumberToObject(properties, "Temperature", atof(temp_str));  // 温度
    cJSON_AddNumberToObject(properties, "Humidity", atof(humi_str));  // 湿度
    cJSON_AddNumberToObject(properties, "weight", atof(weight_str));   // 重量
    cJSON_AddStringToObject(properties, "MQ_2", mq2_status);      // 烟雾传感器
    char *properties_str = cJSON_PrintUnformatted(properties);

    // 服务数据填充
    services[0].event_time = GetEventTimesStamp();  // 动态时间戳
    services[0].service_id = "K5";                  // 服务ID
    services[0].properties = properties_str;        // 属性数据

    // 执行属性上报
    int messageId = IOTA_PropertiesReport(services, serviceNum, 0, NULL);
    if (messageId < 0) {
        PrintfLog(EN_LOG_LEVEL_ERROR, "properties_test: Test_PropertiesReport() failed, messageId %d\n", messageId);
    }

    // 内存清理
    cJSON_Delete(properties);
    free(properties_str);
    MemFree(&services[0].event_time);
}

void HandlePropertiesGet(EN_IOTA_PROPERTY_GET *rsp) {
	if (rsp == NULL) {
		return;
	}
	PrintfLog(EN_LOG_LEVEL_INFO, "properties_test: HandlePropertiesSet(), messageId %d \n", rsp->mqtt_msg_info->messageId);
	PrintfLog(EN_LOG_LEVEL_INFO, "properties_test: HandlePropertiesSet(), request_id %s \n", rsp->request_id);
	PrintfLog(EN_LOG_LEVEL_INFO, "properties_test: HandlePropertiesSet(), object_device_id %s \n", rsp->object_device_id);

	PrintfLog(EN_LOG_LEVEL_INFO, "properties_test: HandlePropertiesSet(), service_id %s \n", rsp->service_id);

	Test_PropGetResponse(rsp->request_id); //response
}




// ---------------------------- secret authentication ------------------------------------------------------------------
static void mqttDeviceSecretInit(char *address, char *port, char *deviceId, char *password) 
{
    IOTA_Init("."); // The certificate address is ./conf/rootcert.pem
    IOTA_SetPrintLogCallback(MyPrintLog); 
 
    // MQTT protocol when the connection parameter is 1883; MQTTS protocol when the connection parameter is 8883
    IOTA_ConnectConfigSet(address, port, deviceId, password);
    // Set authentication method to secret authentication
    IOTA_ConfigSetUint(EN_IOTA_CFG_AUTH_MODE, EN_IOTA_CFG_AUTH_MODE_SECRET);
    
    // Set connection callback function
    IOTA_DefaultCallbackInit();
}


// ---------------------------- main ----------------------------------------------------------------------------------------------
int main(int argc, char **argv) 
{
    // 初始化UART
    if (init_uart(UART_DEVICE, BAUD_RATE) < 0) {
            return -1;
    }
    write(uart_fd, "Auto\r\n", 6); // 启动自动模式

    // 初始化称重UART
    if (init_weight_uart() < 0) {
        return -1;
    }

    // 创建UART读取线程
    pthread_t uart_thread;
    pthread_create(&uart_thread, NULL, uart_read_thread, NULL);

    // 创建称重读取线程
    pthread_t weight_thread;
    pthread_create(&weight_thread, NULL, weight_read_thread, NULL);

    // 初始化MQ2传感器
    if (init_mq2_sensor() < 0) {
        fprintf(stderr, "MQ2 sensor init failed\n");
        return -1;
    }

    // secret authentication initialization
    mqttDeviceSecretInit(g_address, g_port, g_deviceId, g_password); 

    // Callback
    IOTA_SetPropSetCallback(HandlePropertiesSet);
    IOTA_SetPropGetCallback(HandlePropertiesGet);

    int ret = IOTA_Connect();
    if (ret != 0) {
        PrintfLog(EN_LOG_LEVEL_ERROR, "properties_test: IOTA_Connect() error, Auth failed, result %d\n", ret);
        return -1;
    }
    TimeSleep(1500);


    // 主循环
    while (1) 
    {
        read_mq2_value();       // 读取MQ-2传感器

        //写入一个共享文件供QT实时读取
        float T_temperature = current_temperature;
        float H_humidity = current_humidity;
        float W_weight = current_weight;
        float M_mq2_voltage = current_mq2_voltage;
        float L_led_status = (float)g_led_status;
        FILE *fp = fopen("/tmp/sensor_data.txt", "w");
        if (fp == NULL) {
            perror("Failed to open file");
            return 1;
        }
        fprintf(fp, "%.2f %.2f %.2f %.2f %.2f\n", T_temperature, H_humidity, W_weight, M_mq2_voltage, L_led_status);
        fclose(fp);

        Test_PropertiesReport(); // 云平台上报数据
        TimeSleep(1000); // 每1秒上报一次
    }

    // 清理（理论上不会执行到这里）
    pthread_join(uart_thread, NULL);
    close(uart_fd);
    IOTA_Destroy();
    return 0;

}




