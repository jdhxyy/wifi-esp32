// Copyright 2021-2021 The SUMEC Authors. All rights reserved.
// esp32��WIFI����
// Authors: jdh99 <jdh821@163.com>

#include "wifi.h"
#include "async.h"
#include "bror.h"
#include "lagan.h"
#include "tzmalloc.h"

#include <string.h>

#include "esp_event.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#define TAG "wifi"

// �������м��.��λ:ms
#define TASK_INTERVAL 100

#define SCAN_THREAD_SIZE 4096
#define CONNECT_THREAD_SIZE 4096

// tzmalloc�ֽ���
#define MALLOC_TOTAL 2048

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_DISCONNECTED_BIT BIT1
#define WIFI_FAIL_BIT BIT2

#define WIFI_MAC_LEN 6

// ֧��ɨ���б�������
#define SCAN_AP_NUM_MAX 10

typedef enum {
    IDLE,
    SCAN,
    CONNECT,
} tState;

static int mid = -1;

static bool gIsTriggerScan = false;
static bool gIsTriggerConnect = false;
static tState gState = IDLE;

static bool gIsLoad = false;
static bool isWifiStart = false;
static bool isConnect = false;

// �Ƿ���ɨ����.������Ҫ����
static bool isHaveScanResult = false;
static WifiApInfo gScanApInfo[SCAN_AP_NUM_MAX] = {0};
static int gScanApNum = 0;

// �Ƿ������ӽ��.������Ҫ����
static bool isHaveConnectResult = false;
static WifiConnectInfo connectInfo;

static EventGroupHandle_t wifiEventGroup;

static WifiScanResultFunc scanResultCallback = NULL;
static WifiConnectResultFunc connectResultCallback = NULL;

static wifi_ap_record_t gApInfo;

static uint8_t gWifiMac[6] = {0};

static int task(void);
static void eventHandler(void *arg, esp_event_base_t eventBase, int32_t eventID,
                         void *eventData);

static bool isRepeat(uint8_t *ssid);
static void mainThread(void *param);
static bool checkTrigger(void);
static void scan(void);
static void connect(void);

// WifiLoad ģ������
// ����֮ǰ���ʼ��nvs_flash_init,esp_netif_init,esp_event_loop_create_default
bool WifiLoad(char *hostname) {
    mid = TZMallocRegister(0, TAG, MALLOC_TOTAL);
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

    // ����WIFI������
    if (hostname != NULL) {
        tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, hostname);
    }

    // �¼���.��������
    wifiEventGroup = xEventGroupCreate();

    esp_event_handler_instance_t instanceAnyID;
    esp_event_handler_instance_t instanceGotIP;
    if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            eventHandler, NULL, &instanceAnyID) != ESP_OK) {
        return false;
    }
    if (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            eventHandler, NULL, &instanceGotIP)) {
        return false;
    }

    if (AsyncStart(task, TASK_INTERVAL * ASYNC_MILLISECOND) == false) {
        return false;
    }

    esp_wifi_get_mac(WIFI_IF_STA, gWifiMac);

    BrorThreadCreate(mainThread, "wifi_task", BROR_THREAD_PRIORITY_LOWEST, SCAN_THREAD_SIZE);
    gIsLoad = true;

    return true;
}

static int task(void) {
    static struct pt pt = {0};

    PT_BEGIN(&pt);

    PT_WAIT_UNTIL(&pt, gIsLoad == true && isWifiStart == true);

    if (gState == SCAN && isHaveScanResult == true) {
        if (scanResultCallback != NULL) {
            LD(TAG, "push scan result.ap num:%d", gScanApNum);
            scanResultCallback(gScanApInfo, gScanApNum);
        }

        isHaveScanResult = false;
        gState = IDLE;

        PT_EXIT(&pt);
    }

    if (gState == CONNECT && isHaveConnectResult == true) {
        if (connectResultCallback != NULL) {
            LI(TAG, "push connect result.is connect:%d", isConnect);
            connectResultCallback(isConnect);
        }

        isHaveConnectResult = false;
        gState = IDLE;
    }

    PT_END(&pt);
}

static void eventHandler(void *arg, esp_event_base_t eventBase, int32_t eventID,
                         void *eventData) {
    LD(TAG, "event base:%s,id:%d", eventBase, eventID);
    if (eventBase == WIFI_EVENT && eventID == WIFI_EVENT_STA_START) {
        LD(TAG, "wifi connect ap start");
        return;
    }

    if (eventBase == WIFI_EVENT && eventID == WIFI_EVENT_STA_DISCONNECTED) {
        isConnect = false;

        if (gState == CONNECT) {
            LE(TAG, "wifi connect fail!");
            xEventGroupSetBits(wifiEventGroup, WIFI_FAIL_BIT);
        } else {
            LE(TAG, "wifi disconnect!");
            xEventGroupSetBits(wifiEventGroup, WIFI_DISCONNECTED_BIT);
        }

        return;
    }

    if (eventBase == IP_EVENT && eventID == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)eventData;
        isConnect = true;

        connectInfo.IP = ntohl(event->ip_info.ip.addr);
        connectInfo.Gateway = ntohl(event->ip_info.gw.addr);

        LI(TAG, "connect success.ip:0x%x gw:0x%x", connectInfo.IP, connectInfo.Gateway);
        xEventGroupSetBits(wifiEventGroup, WIFI_CONNECTED_BIT);
    }
}

static void mainThread(void *param) {
    while (1) {
        if (checkTrigger() == false) {
            BrorDelayMS(10);
            continue;
        }

        switch (gState) {
        case IDLE:
            break;
        case SCAN:
            scan();
            isHaveScanResult = true;
            break;
        case CONNECT:
            connect();
            isHaveConnectResult = true;
            break;
        default:
            break;
        }
    }
}

static bool checkTrigger(void) {
    if (gState != IDLE) {
        return false;
    }

    if (gIsTriggerConnect == true) {
        gState = CONNECT;
        gIsTriggerConnect = false;
        return true;
    } else if (gIsTriggerScan == true) {
        gState = SCAN;
        gIsTriggerScan = false;
        return true;
    }

    return false;
}

// WifiIsBusy �Ƿ�����æµ
bool WifiIsBusy(void) {
    return gState != IDLE || gIsTriggerConnect == true || gIsTriggerScan == true;
}

// WifiScan ����ɨ���ȵ�
// ����false˵��������æ
bool WifiScan(void) {
    if (gIsLoad == false) {
        LW(TAG, "wifi not start!");
        return false;
    }

    if (WifiIsBusy() == true) {
        LW(TAG, "scan start failed!is busy");
        return false;
    }

    gIsTriggerScan = true;

    LD(TAG, "wifi trigger scan!");

    return true;
}

static void scan(void) {
    wifi_ap_record_t *apInfo = NULL;

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

    LD(TAG, "wifi scan start");
    esp_wifi_scan_start(NULL, true);

    uint16_t apCount = 0;
    if (esp_wifi_scan_get_ap_num(&apCount) != ESP_OK) {
        LE(TAG, "scan failed!get ap num failed");
        goto EXIT;
    }
    LD(TAG, "scan ap num:%d", apCount);
    if (apCount == 0) {
        LE(TAG, "scan failed!get ap num is 0");
        goto EXIT;
    }

    apInfo = (wifi_ap_record_t *)pvPortMalloc(sizeof(wifi_ap_record_t) * apCount);
    if (apInfo == NULL) {
        LE(TAG, "scan failed!malloc failed");
        apInfo = (wifi_ap_record_t *)pvPortMalloc(sizeof(wifi_ap_record_t) * WIFI_SCAN_LIST_LEN_MAX);
        if (apInfo == NULL) {
            LE(TAG, "scan failed!malloc failed");
            goto EXIT;
        }
        apCount = WIFI_SCAN_LIST_LEN_MAX;
    }

    if (esp_wifi_scan_get_ap_records(&apCount, apInfo) != ESP_OK) {
        LE(TAG, "scan failed!get ap records failed");
        goto EXIT;
    }

    gScanApNum = 0;
    for (int i = 0; i < apCount; i++) {
        if (isRepeat(apInfo[i].ssid) == true) {
            continue;
        }

        memcpy(gScanApInfo[gScanApNum].Bssid, apInfo[i].bssid, WIFI_BSSID_LEN);
        memcpy(gScanApInfo[gScanApNum].Ssid, apInfo[i].ssid, WIFI_SSID_LEN_MAX);
        gScanApInfo[gScanApNum].Rssi = apInfo[i].rssi;
        gScanApInfo[gScanApNum].Channel = apInfo[i].primary;
        gScanApInfo[gScanApNum].Authmode = apInfo[i].authmode;
        gScanApInfo[gScanApNum].PairwiseCipher = apInfo[i].pairwise_cipher;
        gScanApInfo[gScanApNum].GroupCipher = apInfo[i].group_cipher;
        if (++gScanApNum >= SCAN_AP_NUM_MAX) {
            break;
        }
    }

EXIT:
    if (apInfo != NULL) {
        vPortFree(apInfo);
    }
    esp_wifi_scan_stop();
}

static bool isRepeat(uint8_t *ssid) {
    for (int j = 0; j < gScanApNum; j++) {
        if (memcmp(gScanApInfo[j].Ssid, ssid, WIFI_SSID_LEN_MAX) == 0) {
            return true;
        }
    }

    return false;
}

// WifiConnect ���������ȵ�
bool WifiConnect(char *ssid, char *pwd, wifi_auth_mode_t authMode) {
    if (gIsLoad == false) {
        LW(TAG, "wifi not start!");
        return false;
    }

    if (WifiIsBusy() == true) {
        LW(TAG, "connect start failed!is busy");
        return false;
    }

    uint8_t ssidLen = strlen(ssid);
    uint8_t pwdLen = strlen(pwd);
    if (ssidLen == 0 || ssidLen >= WIFI_SSID_LEN_MAX || pwdLen >= WIFI_PWD_LEN_MAX) {
        LW(TAG, "connect start failed!ssid or pwd is wrong");
        return false;
    }

    strcpy(connectInfo.Ssid, ssid);
    strcpy(connectInfo.Pwd, pwd);
    connectInfo.Authmode = authMode;

    LI(TAG, "wifi trigger connect!");

    gIsTriggerConnect = true;

    return true;
}

static void connect(void) {
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        LE(TAG, "connect failed!set mode failed");
        return;
    }

    wifi_config_t wifiConfig = {
        .sta = {
            .threshold.authmode = connectInfo.Authmode,
            .pmf_cfg = {
                .capable = true,
                .required = false},
        },
    };
    strcpy((char *)wifiConfig.sta.ssid, connectInfo.Ssid);
    if (connectInfo.Authmode != WIFI_AUTH_OPEN) {
        strcpy((char *)wifiConfig.sta.password, connectInfo.Pwd);
    }
    if (esp_wifi_set_config(WIFI_IF_STA, &wifiConfig) != ESP_OK) {
        LE(TAG, "connect failed!set config failed");
        return;
    }

    if (isWifiStart == false) {
        if (esp_wifi_start() != ESP_OK) {
            LE(TAG, "connect failed!wifi start failed");
            return;
        }
        isWifiStart = true;
    }

    LI(TAG, "wifi connect ap start");
    esp_wifi_connect();

    LI(TAG, "connect start,wait result");
    // �ȴ��¼�
    xEventGroupWaitBits(wifiEventGroup, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

    LI(TAG, "connect end");
}

// WifiDisconnect �Ͽ�����
bool WifiDisconnect(void) {
    if (WifiIsBusy() == true) {
        LE(TAG, "disconnect start failed!is busy");
        return false;
    }

    if (WifiIsConnect() == false) {
        LE(TAG, "disconnect start failed!is not connect");
        return false;
    }

    if (esp_wifi_disconnect() != ESP_OK) {
        LE(TAG, "disconnect start failed!disconnect failed");
        return false;
    }

    LI(TAG, "disconnect");

    memset(&connectInfo, 0, sizeof(connectInfo));

    return true;
}

// WifiIsConnect �Ƿ�������
bool WifiIsConnect(void) {
    return isConnect;
}

// WifiGetConnectInfo ��ȡ��ǰ�����ӵ���Ϣ
// ���δ�����򷵻�NULL
WifiConnectInfo *WifiGetConnectInfo(void) {
    if (WifiIsConnect() == false) {
        return NULL;
    }
    return &connectInfo;
}

// WifiSetCallbackScanResult ����ɨ��ص�
void WifiSetCallbackScanResult(WifiScanResultFunc func) {
    scanResultCallback = func;
}

// WifiSetCallbackConnectResult �������ӻص�
void WifiSetCallbackConnectResult(WifiConnectResultFunc func) {
    connectResultCallback = func;
}

// WifiGetRssi ��ȡwifi��rssi
int8_t WifiGetRssi(void) {
    if (WifiIsConnect() == false) {
        return 0;
    }

    esp_wifi_sta_get_ap_info(&gApInfo);

    return gApInfo.rssi;
}

// WifiGetMac ��ȡmac��ַ
void WifiGetMac(uint8_t mac[6]) {
    memcpy(mac, gWifiMac, sizeof(gWifiMac));
}

// WifiGetScanHistoryResult ��ȡ��ʷɨ����
// apNum ��ʷɨ�����ĸ���
WifiApInfo *WifiGetScanHistoryResult(uint8_t *apNum) {
    if (gState == SCAN) {
        *apNum = 0;
        return NULL;
    }

    *apNum = gScanApNum;
    return gScanApInfo;
}
