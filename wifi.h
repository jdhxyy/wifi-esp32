// Copyright 2021-2021 The SUMEC Authors. All rights reserved.
// esp32��WIFI����
// Authors: jdh99 <jdh821@163.com>

#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>
#include <stdbool.h>

#include "esp_wifi.h"

// SSID��󳤶�.����'\0'
#define WIFI_SSID_LEN_MAX 33
// ������󳤶�.����'\0'
#define WIFI_PWD_LEN_MAX 65
#define WIFI_BSSID_LEN 6

// wifiɨ���ȵ��б���󳤶�
#define WIFI_SCAN_LIST_LEN_MAX 10

// ��������Դ���
#define WIFI_CONNECT_RETRY_MAX 3

// WifiApInfo ap��Ϣ
typedef struct {
    // ap��MAC��ַ
    uint8_t bssid[WIFI_BSSID_LEN];
    uint8_t ssid[WIFI_SSID_LEN_MAX];
    uint8_t channel;
    int8_t rssi;
    // �����㷨
    wifi_auth_mode_t authmode;
    // �ɶ������׼�
    wifi_cipher_type_t pairwise_cipher;
    // �鲥�����׼�
    wifi_cipher_type_t group_cipher;
} WifiApInfo;

// WifiConnectInfo wifi������Ϣ
typedef struct {
    char Ssid[WIFI_SSID_LEN_MAX];
    char Pwd[WIFI_PWD_LEN_MAX];
    uint8_t IP[4];
    uint8_t Gateway[4];
} WifiConnectInfo;

// WifiScanResultFunc ɨ����
// ɨ��ʧ��apInfoΪNULL,lenΪ0
typedef void (*WifiScanResultFunc)(WifiApInfo* apInfo, int len);

// WifiConnectResultFunc ���ӽ��
typedef void (*WifiConnectResultFunc)(bool result);

// WifiLoad ģ������
bool WifiLoad(void);

// WifiIsBusy �Ƿ�æµ
bool WifiIsBusy(void);

// WifiScan ����ɨ���ȵ�
// ����false˵��������æ
bool WifiScan(void);

// WifiConnect ���������ȵ�
bool WifiConnect(char* ssid, char* pwd);

// WifiDisconnect �Ͽ�����
void WifiDisconnect(void);

// WifiIsConnect �Ƿ�������
bool WifiIsConnect(void);

// WifiGetConnectInfo ��ȡ��ǰ�����ӵ���Ϣ
// ���δ�����򷵻�NULL
WifiConnectInfo* WifiGetConnectInfo(void);

// WifiSetCallbackScanResult ����ɨ��ص�
void WifiSetCallbackScanResult(WifiScanResultFunc func);

// WifiSetCallbackConnectResult �������ӻص�
void WifiSetCallbackConnectResult(WifiConnectResultFunc func);

#endif
