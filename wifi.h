// Copyright 2021-2021 The SUMEC Authors. All rights reserved.
// esp32��WIFI����
// Authors: jdh99 <jdh821@163.com>

#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>
#include <stdint.h>

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
    uint8_t Bssid[WIFI_BSSID_LEN];
    uint8_t Ssid[WIFI_SSID_LEN_MAX];
    uint8_t Channel;
    int8_t Rssi;
    // �����㷨
    wifi_auth_mode_t Authmode;
    // �ɶ������׼�
    wifi_cipher_type_t PairwiseCipher;
    // �鲥�����׼�
    wifi_cipher_type_t GroupCipher;
} WifiApInfo;

// WifiConnectInfo wifi������Ϣ
typedef struct {
    char Ssid[WIFI_SSID_LEN_MAX];
    char Pwd[WIFI_PWD_LEN_MAX];
    wifi_auth_mode_t Authmode;
    uint32_t IP;
    uint32_t Gateway;
} WifiConnectInfo;

// WifiScanResultFunc ɨ����
// ɨ��ʧ��apInfoΪNULL,lenΪ0
typedef void (*WifiScanResultFunc)(WifiApInfo *apInfo, int len);

// WifiConnectResultFunc ���ӽ��
typedef void (*WifiConnectResultFunc)(bool result);

// WifiLoad ģ������
// ����֮ǰ���ʼ��nvs_flash_init,esp_netif_init,esp_event_loop_create_default
bool WifiLoad(void);

// WifiIsBusy �Ƿ�æµ
bool WifiIsBusy(void);

// WifiScan ����ɨ���ȵ�
// ����false˵��������æ
bool WifiScan(void);

// WifiConnect ���������ȵ�
bool WifiConnect(char *ssid, char *pwd, wifi_auth_mode_t authMode);

// WifiDisconnect �Ͽ�����
void WifiDisconnect(void);

// WifiIsConnect �Ƿ�������
bool WifiIsConnect(void);

// WifiGetConnectInfo ��ȡ��ǰ�����ӵ���Ϣ
// ���δ�����򷵻�NULL
WifiConnectInfo *WifiGetConnectInfo(void);

// WifiGetRssi ��ȡwifi��rssi
int8_t WifiGetRssi(void);

// WifiGetMac ��ȡmac��ַ
void WifiGetMac(uint8_t mac[6]);

// WifiSetCallbackScanResult ����ɨ��ص�
void WifiSetCallbackScanResult(WifiScanResultFunc func);

// WifiSetCallbackConnectResult �������ӻص�
void WifiSetCallbackConnectResult(WifiConnectResultFunc func);

#endif
