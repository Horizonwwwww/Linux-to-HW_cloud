/*
 * Copyright (c) 2022-2024 Huawei Cloud Computing Technology Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
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
#define LED_PATH "/sys/class/leds/gpio3b5/brightness"


/*
 * Property reporting example
 * 1、Property reporting related documents: https://support.huaweicloud.com/usermanual-iothub/iot_01_0323.html
 * 2、Platform setting device properties documents: https://support.huaweicloud.com/usermanual-iothub/iot_01_0336.html
 * 3、Platform querying device properties documents: https://support.huaweicloud.com/usermanual-iothub/iot_01_0336.html
 */

// You can get the access address from IoT Console "Overview" -> "Access Information"
char *g_address = "82be2f428c.st1.iotda-device.cn-north-4.myhuaweicloud.com"; 
char *g_port = "8883";

// deviceId, The mqtt protocol requires the user name to be filled in.
// Please fill in the deviceId
char *g_deviceId = "67d3ded4375e694aa6927974_test_linux"; 
char *g_password = "KK654321";


int g_motor_status = 0; // 默认关闭状态
int g_led_status = 0; // 默认关闭状态


//int g_smoke_value = 20; // product models  //LK   属性初始值
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






//LK    上报属性,上报当前g_smoke_value值

// ---------------------- Property reporting ---------------------------
static void Test_PropertiesReport(void)
{
    const int serviceNum = 1; // 单个服务
    ST_IOTA_SERVICE_DATA_INFO services[serviceNum];

    // 使用cJSON构建复杂属性结构
    cJSON *properties = cJSON_CreateObject();
    cJSON_AddNumberToObject(properties, "motor", g_motor_status); // 使用状态变量     // 电机状态
    cJSON_AddNumberToObject(properties, "LED", g_led_status); // 使用状态变量     // LED
    cJSON_AddNumberToObject(properties, "DHT11_T", 20);  // 温度
//    cJSON_AddNumberToObject(properties, "DHT11_T", ReadDHT11_T()); // 示例传感器接口
    cJSON_AddNumberToObject(properties, "DHT11_H", 20);  // 湿度
    cJSON_AddNumberToObject(properties, "weight", 20);   // 重量
    cJSON_AddNumberToObject(properties, "MQ_2", 20);      // 烟雾传感器
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

/*
static void Test_PropertiesReport(void)
{
    const int serviceNum = 1; // reported services' totol count
    ST_IOTA_SERVICE_DATA_INFO services[serviceNum];

    char service[100] = {0};
    (void)sprintf_s(service, sizeof(service), "{\"Smoke_value\": %d}", g_smoke_value); 
    // --------------- the data of service-------------------------------
    services[0].event_time = GetEventTimesStamp(); // if event_time is set to NULL, the time will be the iot-platform's time.
    services[0].service_id = "Smoke";
    services[0].properties = service;

    int messageId = IOTA_PropertiesReport(services, serviceNum, 0, NULL);
    if (messageId < 0) {
        PrintfLog(EN_LOG_LEVEL_ERROR, "properties_test: Test_PropertiesReport() failed, messageId %d\n", messageId);
    }

    MemFree(&services[0].event_time);
}
*/














//  在SET后控制LED

// --------------- 控制LED --------------------

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
// ---------------- Set Device Properties --------------------
static void Test_PropSetResponse(char *requestId)
{
    int messageId = IOTA_PropertiesSetResponse(requestId, 0, "success", NULL);
    if (messageId < 0) {
        PrintfLog(EN_LOG_LEVEL_ERROR, "properties_test: Test_PropSetResponse() failed, messageId %d\n", messageId);
    }
}

/*
void HandlePropertiesSet(EN_IOTA_PROPERTY_SET *rsp) {
	if (rsp == NULL) {
		return;
	}
	PrintfLog(EN_LOG_LEVEL_INFO, "properties_test: HandlePropertiesSet(), messageId %d \n", rsp->mqtt_msg_info->messageId);
	PrintfLog(EN_LOG_LEVEL_INFO, "properties_test: HandlePropertiesSet(), request_id %s \n", rsp->request_id);
	PrintfLog(EN_LOG_LEVEL_INFO, "properties_test: HandlePropertiesSet(), object_device_id %s \n", rsp->object_device_id);

	int i = 0;
	while (rsp->services_count > i) {
		PrintfLog(EN_LOG_LEVEL_INFO, "properties_test: HandlePropertiesSet(), service_id %s \n", rsp->services[i].service_id);
		PrintfLog(EN_LOG_LEVEL_INFO, "properties_test: HandlePropertiesSet(), properties %s \n", rsp->services[i].properties);
        if (strncmp(rsp->services[i].service_id, "Smoke", strlen(rsp->services[i].service_id)) == 0) {
            cJSON *properties = cJSON_Parse(rsp->services[i].properties);
            cJSON *smoke = cJSON_GetObjectItem(properties, "Smoke_value");
            if (cJSON_IsNumber(smoke)) {
                g_smoke_value = (int)cJSON_GetNumberValue(smoke);
                PrintfLog(EN_LOG_LEVEL_INFO,"Set smoke_value success! g_smoke_value = %d\n", g_smoke_value);
            }
            cJSON_Delete(properties);
        }
		i++;
	}
	Test_PropSetResponse(rsp->request_id); //response
}
*/
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

// --------------- Query Device Properties --------------------
/*
static void Test_PropGetResponse(char *requestId)
{
    
    // Developers here implement device property querying. The following is an example 
    const int serviceNum = 1;
    ST_IOTA_SERVICE_DATA_INFO serviceProp[serviceNum];

    char property[100] = {0};
    (void)sprintf_s(property, sizeof(property), "{\"Smoke_value\": %d}", g_smoke_value); 

    serviceProp[0].event_time = GetEventTimesStamp();
    serviceProp[0].service_id = "Smoke";
    serviceProp[0].properties = property;

    int messageId = IOTA_PropertiesGetResponse(requestId, serviceProp, serviceNum, NULL);
    if (messageId < 0) {
        PrintfLog(EN_LOG_LEVEL_ERROR, "properties_test: Test_PropGetResponse() failed, messageId %d\n", messageId);
    }

    MemFree(&serviceProp[0].event_time);
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
*/

static void Test_PropGetResponse(char *requestId)
{
    const int serviceNum = 1; // 单个服务
    ST_IOTA_SERVICE_DATA_INFO services[serviceNum];

    // 使用cJSON构建复杂属性结构
    cJSON *properties = cJSON_CreateObject();
    cJSON_AddNumberToObject(properties, "motor", g_motor_status); // 使用状态变量     // 电机状态
    cJSON_AddNumberToObject(properties, "LED", g_led_status); // 使用状态变量     // LED
    cJSON_AddNumberToObject(properties, "DHT11_T", 20);  // 温度
//    cJSON_AddNumberToObject(properties, "DHT11_T", ReadDHT11_T()); // 示例传感器接口
    cJSON_AddNumberToObject(properties, "DHT11_H", 20);  // 湿度
    cJSON_AddNumberToObject(properties, "weight", 20);   // 重量
    cJSON_AddNumberToObject(properties, "MQ_2", 20);      // 烟雾传感器
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




// ---------------------------- secret authentication --------------------------------------
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

int main(int argc, char **argv) 
{
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

    // properties Report
    Test_PropertiesReport();

    int i = 0;
    for (; i < 100; i++) {
        TimeSleep(3500);
   //     g_smoke_value++;
   //     PrintfLog(EN_LOG_LEVEL_INFO, "properties_test: g_smoke_value is %d \n", g_smoke_value);
        Test_PropertiesReport();
    }

    while(1);
    
    IOTA_Destroy();
}




