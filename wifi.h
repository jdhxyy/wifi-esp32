// Copyright 2021-2021 The SUMEC Authors. All rights reserved.
// esp32的WIFI驱动
// Authors: jdh99 <jdh821@163.com>

#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>
#include <stdbool.h>

#include "esp_wifi.h"

// SSID最大长度.包括'\0'
#define WIFI_SSID_LEN_MAX 33
// 密码最大长度.包括'\0'
#define WIFI_PWD_LEN_MAX 65
#define WIFI_BSSID_LEN 6

// wifi扫描热点列表最大长度
#define WIFI_SCAN_LIST_LEN_MAX 10

// 重连最大尝试次数
#define WIFI_CONNECT_RETRY_MAX 3

// WifiApInfo ap信息
typedef struct {
    // ap的MAC地址
    uint8_t bssid[WIFI_BSSID_LEN];
    uint8_t ssid[WIFI_SSID_LEN_MAX];
    uint8_t channel;
    int8_t rssi;
    // 加密算法
    wifi_auth_mode_t authmode;
    // 成对密码套件
    wifi_cipher_type_t pairwise_cipher;
    // 组播加密套件
    wifi_cipher_type_t group_cipher;
} WifiApInfo;

// WifiConnectInfo wifi连接信息
typedef struct {
    char Ssid[WIFI_SSID_LEN_MAX];
    char Pwd[WIFI_PWD_LEN_MAX];
    uint8_t IP[4];
    uint8_t Gateway[4];
} WifiConnectInfo;

// WifiScanResultFunc 扫描结果
// 扫描失败apInfo为NULL,len为0
typedef void (*WifiScanResultFunc)(WifiApInfo* apInfo, int len);

// WifiConnectResultFunc 连接结果
typedef void (*WifiConnectResultFunc)(bool result);

// WifiLoad 模块载入
bool WifiLoad(void);

// WifiIsBusy 是否忙碌
bool WifiIsBusy(void);

// WifiScan 启动扫描热点
// 返回false说明驱动正忙
bool WifiScan(void);

// WifiConnect 启动连接热点
bool WifiConnect(char* ssid, char* pwd);

// WifiDisconnect 断开连接
void WifiDisconnect(void);

// WifiIsConnect 是否已连接
bool WifiIsConnect(void);

// WifiGetConnectInfo 读取当前已连接的信息
// 如果未连接则返回NULL
WifiConnectInfo* WifiGetConnectInfo(void);

// WifiSetCallbackScanResult 设置扫描回调
void WifiSetCallbackScanResult(WifiScanResultFunc func);

// WifiSetCallbackConnectResult 设置连接回调
void WifiSetCallbackConnectResult(WifiConnectResultFunc func);

#endif
