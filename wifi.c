// Copyright 2021-2021 The SUMEC Authors. All rights reserved.
// esp32的WIFI驱动
// Authors: jdh99 <jdh821@163.com>

#include "wifi.h"
#include "async.h"
#include "bror.h"
#include "tzmalloc.h"
#include "lagan.h"

#include <string.h>

#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_event.h"

#define TAG "wifi"

// 任务运行间隔.单位:ms
#define TASK_INTERVAL 100

#define SCAN_THREAD_SIZE 4096
#define CONNECT_THREAD_SIZE 4096

// tzmalloc字节数
#define MALLOC_TOTAL 2048

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int mid = -1;

static bool isStart = false;
static bool isBusy = false;
static bool isWifiStart = false;

// 是否有扫描结果.有则需要推送
static bool isHaveScanResult = false;
static WifiApInfo* scanApInfo = NULL;
static int scanApNum = 0;

static bool isConnect = false;
static bool isStartConnect = false;
// 是否有连接结果.有则需要推送
static bool isHaveConnectResult = false;
static WifiConnectInfo connectInfo;

static EventGroupHandle_t wifiEventGroup;

static WifiScanResultFunc scanResultCallback = NULL;
static WifiConnectResultFunc connectResultCallback = NULL;

static wifi_ap_record_t apInfo;

static int task(void);
static void eventHandler(void* arg, esp_event_base_t eventBase, int32_t eventID, 
    void* eventData);

static void scanThread(void* param);
static void connectThread(void* param);

// WifiLoad 模块载入
// 载入之前需初始化nvs_flash_init,esp_netif_init,esp_event_loop_create_default
bool WifiLoad(void) {
    mid = TZMallocRegister(0, "wifi", MALLOC_TOTAL);
    if (mid == -1) {
        return false;
    }

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == NULL) {
        return false;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        return false;
    }

    // 事件组.用于连接
    wifiEventGroup = xEventGroupCreate();

    esp_event_handler_instance_t instanceAnyID;
    esp_event_handler_instance_t instanceGotIP;
    if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
        eventHandler, NULL, &instanceAnyID) != ESP_OK) {
        return false;
    }
    if (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
        &eventHandler, NULL, &instanceGotIP)) {
        return false;
    }

    if (AsyncStart(task, TASK_INTERVAL * ASYNC_MILLISECOND) == false) {
        return false;
    }

    isStart = true;

    return true;
}

static int task(void) {
    static struct pt pt = {0};

    PT_BEGIN(&pt);

    if (isHaveScanResult) {
        isBusy = false;
        if (scanResultCallback != NULL) {
            LI(TAG, "push scan result.ap num:%d", scanApNum);
            scanResultCallback(scanApInfo, scanApNum);
        }
        if (scanApInfo != NULL) {
            TZFree(scanApInfo);
            scanApInfo = NULL;
            scanApNum = 0;
        }
        isHaveScanResult = false;
    }

    if (isHaveConnectResult) {
        isBusy = false;
        if (connectResultCallback != NULL) {
            LI(TAG, "push connect result.is connect:%d", isConnect);
            connectResultCallback(isConnect);
        }
        isHaveConnectResult = false;
    }

    PT_END(&pt);
}

static void eventHandler(void* arg, esp_event_base_t eventBase, int32_t eventID, 
    void* eventData) {
    static int retryNum = 0;
    
    LI(TAG, "event base:%s,id:%d", eventBase, eventID);
    if (eventBase == WIFI_EVENT && eventID == WIFI_EVENT_STA_START && isStartConnect) {
        LI(TAG, "wifi connect ap start");
        esp_wifi_connect();
        return;
    } 
    
    if (eventBase == WIFI_EVENT && eventID == WIFI_EVENT_STA_DISCONNECTED) {
        LW(TAG, "wifi disconnect!");
        isConnect = false;

        if (isStartConnect) {
            if (retryNum < WIFI_CONNECT_RETRY_MAX) {
                retryNum++;
                LI(TAG, "wifi connect ap retry:%d", retryNum);
                esp_wifi_connect();
            } else {
                LE(TAG, "wifi connect failed!retry too many");
                xEventGroupSetBits(wifiEventGroup, WIFI_FAIL_BIT);
                isHaveConnectResult = true;
            }
        } else {
            isHaveConnectResult = true;
        }
        return;
    }
    
    if (eventBase == IP_EVENT && eventID == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) eventData;
        retryNum = 0;
        isConnect = true;

        connectInfo.IP = ntohl(event->ip_info.ip.addr);
        connectInfo.Gateway = ntohl(event->ip_info.gw.addr);

        LI(TAG, "connect success.ip:0x%x gw:0x%x", connectInfo.IP, connectInfo.Gateway);
        isHaveConnectResult = true;
        xEventGroupSetBits(wifiEventGroup, WIFI_CONNECTED_BIT);
    }
}

// WifiIsBusy 是否忙碌
bool WifiIsBusy(void) {
    return (isBusy & isStart);
}

// WifiScan 启动扫描热点
// 返回false说明驱动正忙
bool WifiScan(void) {
    if (isStart == false) {
        LW(TAG, "wifi not start!");
        return false;
    }

    if (isBusy) {
        LW(TAG, "wifi scan failed,because is busy!");
        return false;
    }

    isBusy = true;
    isHaveScanResult = false;
    if (scanApInfo != NULL) {
        TZFree(scanApInfo);
        scanApInfo = NULL;
    }
    scanApNum = 0;

    LD(TAG, "wifi scan start!create thread to scan");
    BrorThreadCreate(scanThread, "scanThread", BROR_THREAD_PRIORITY_LOWEST, SCAN_THREAD_SIZE);
    return true;
}

static void scanThread(void* param) {
    if (scanResultCallback == NULL) {
        LE(TAG, "scan failed!callback is null");
        goto EXIT;
    }

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        LE(TAG, "scan failed!set mode failed");
        goto EXIT;
    }
    if (isWifiStart == false) {
        if (esp_wifi_start() != ESP_OK) {
            LE(TAG, "scan failed!wifi start failed");
            goto EXIT;
        }
        isWifiStart = true;
    }

    LI(TAG, "wifi scan start");
    esp_wifi_scan_start(NULL, true);
    
    uint16_t number = WIFI_SCAN_LIST_LEN_MAX;
    wifi_ap_record_t apInfo[WIFI_SCAN_LIST_LEN_MAX];
    memset(apInfo, 0, sizeof(apInfo));
    if (esp_wifi_scan_get_ap_records(&number, apInfo) != ESP_OK) {
        LE(TAG, "scan failed!get ap records failed");
        goto EXIT;
    }

    uint16_t apCount = 0;
    if (esp_wifi_scan_get_ap_num(&apCount) != ESP_OK) {
        LE(TAG, "scan failed!get ap num failed");
        goto EXIT;
    }
    LI(TAG, "scan ap num:%d", apCount);
    if (apCount == 0) {
        LE(TAG, "scan failed!get ap num is 0");
        goto EXIT;
    }

    if (apCount > WIFI_SCAN_LIST_LEN_MAX) {
        apCount = WIFI_SCAN_LIST_LEN_MAX;
    }

    scanApInfo = (WifiApInfo*)TZMalloc(mid, sizeof(WifiApInfo) * apCount);
    if (scanApInfo == NULL) {
        LE(TAG, "scan failed!malloc failed");
        goto EXIT;
    }

    for (int i = 0; i < apCount; i++) {
        memcpy(scanApInfo[i].Bssid, apInfo[i].bssid, WIFI_BSSID_LEN);
        memcpy(scanApInfo[i].Ssid, apInfo[i].ssid, WIFI_SSID_LEN_MAX);
        scanApInfo[i].Rssi = apInfo[i].rssi;
        scanApInfo[i].Channel = apInfo[i].primary;
        scanApInfo[i].Authmode = apInfo[i].authmode;
        scanApInfo[i].PairwiseCipher = apInfo[i].pairwise_cipher;
        scanApInfo[i].GroupCipher = apInfo[i].group_cipher;
    }
    scanApNum = apCount;

EXIT:
    isHaveScanResult = true;
    esp_wifi_scan_stop();
    BrorThreadDeleteMe();
}

// WifiConnect 启动连接热点
bool WifiConnect(char* ssid, char* pwd, wifi_auth_mode_t authMode) {
    if (isStart == false) {
        LW(TAG, "wifi not start!");
        return false;
    }
    
    if (isBusy) {
        LW(TAG, "connect start failed!is busy");
        return false;
    }
    if (isConnect) {
        LW(TAG, "connect start failed!is connect");
        return false;
    }
    if (isStartConnect) {
        LW(TAG, "connect start failed!is start connect");
        return false;
    }

    if (strlen(ssid) >= WIFI_SSID_LEN_MAX || strlen(pwd) >= WIFI_PWD_LEN_MAX) {
        LW(TAG, "connect start failed!param is wrong");
        return false;
    }

    LI(TAG, "connect start");
    strcpy(connectInfo.Ssid, ssid);
    strcpy(connectInfo.Pwd, pwd);
    connectInfo.Authmode = authMode;

    isBusy = true;
    isStartConnect = true;
    isHaveConnectResult = false;
    BrorThreadCreate(connectThread, "connectThread", BROR_THREAD_PRIORITY_LOWEST, CONNECT_THREAD_SIZE);

    return true;
}

static void connectThread(void* param) {
    if (connectResultCallback == NULL) {
        LE(TAG, "connect failed!callback is null");
        goto EXIT;
    }

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        LE(TAG, "connect failed!set mode failed");
        goto EXIT;
    }

    wifi_config_t wifiConfig = {
        .sta = {
            .threshold.authmode = connectInfo.Authmode,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    strcpy((char*)wifiConfig.sta.ssid, connectInfo.Ssid);
    if (connectInfo.Authmode != WIFI_AUTH_OPEN) {
        strcpy((char *)wifiConfig.sta.password, connectInfo.Pwd);
    }
    if (esp_wifi_set_config(WIFI_IF_STA, &wifiConfig) != ESP_OK) {
        LE(TAG, "connect failed!set config failed");
        goto EXIT;
    }

    if (isWifiStart == false) {
        if (esp_wifi_start() != ESP_OK) {
            LE(TAG, "connect failed!wifi start failed");
            goto EXIT;
        }
        isWifiStart = true;
    } else {
        LI(TAG, "wifi connect ap start");
        esp_wifi_connect();
    }

    LI(TAG, "connect start,wait result");
    // 等待事件
    xEventGroupWaitBits(wifiEventGroup, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, 
        pdFALSE, pdFALSE, portMAX_DELAY);

    isStartConnect = false;
    BrorThreadDeleteMe();
    return;

EXIT:
    isHaveConnectResult = true;
    isStartConnect = false;
    BrorThreadDeleteMe();
}

// WifiDisconnect 断开连接
void WifiDisconnect(void) {
    if (isConnect) {
        return;
    }
    WifiDisconnect();
}

// WifiIsConnect 是否已连接
bool WifiIsConnect(void) {
    return isConnect;
}

// WifiGetConnectInfo 读取当前已连接的信息
// 如果未连接则返回NULL
WifiConnectInfo* WifiGetConnectInfo(void) {
    if (isConnect == false) {
        return NULL;
    }
    return &connectInfo;
}

// WifiSetCallbackScanResult 设置扫描回调
void WifiSetCallbackScanResult(WifiScanResultFunc func) {
    scanResultCallback = func;
}

// WifiSetCallbackConnectResult 设置连接回调
void WifiSetCallbackConnectResult(WifiConnectResultFunc func) {
    connectResultCallback = func;
}

// WifiGetRssi 获取wifi的rssi
int8_t WifiGetRssi(void) {
    if (isConnect == false) {
        return 0;
    }

    esp_wifi_sta_get_ap_info(&apInfo);

    return apInfo.rssi;
}
