# wifi-esp32

## 1. 介绍
esp32下的wifi驱动

## 2. 功能
- 作为STA，扫描周围热点
- 作为STA，连接热点

提供回调函数接口，应用模块通过回调函数获取扫描热点和连接热点的结果。

## 3. 初始化API
```c
// WifiLoad 模块载入
bool WifiLoad(void);
```

## 4. 扫描热点API
```c
// WifiScan 启动扫描热点
// 返回false说明驱动正忙
bool WifiScan(void);

// WifiSetCallbackScanResult 设置扫描回调
void WifiSetCallbackScanResult(WifiScanResultFunc func);
```

## 5. 连接热点API
```c
// WifiConnect 启动连接热点
bool WifiConnect(char* ssid, char* pwd);

// WifiDisconnect 断开连接
void WifiDisconnect(void);

// WifiIsConnect 是否已连接
bool WifiIsConnect(void);

// WifiGetConnectInfo 读取当前已连接的信息
// 如果未连接则返回NULL
WifiConnectInfo* WifiGetConnectInfo(void);

// WifiSetCallbackConnectResult 设置连接回调
void WifiSetCallbackConnectResult(WifiConnectResultFunc func);
```
