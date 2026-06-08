#ifndef UNO_NETWORK_H
#define UNO_NETWORK_H

#include <string>
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

// ===== 客户端网络 API =====

// 连接到服务器（IP:port 格式，如 "192.168.1.100:8888"）
// 返回 true 表示连接成功
bool networkConnect(const std::wstring& address);

// 发送一行消息给服务器（自动追加 \n）
bool networkSend(const std::string& message);

// 接收一行消息（以 \n 分隔），超时等待
// timeoutMs: 最大等待毫秒，0 = 非阻塞，-1 = 阻塞等待
// 返回 true 表示成功收到一行
bool networkRecv(std::string& message, int timeoutMs = -1);

// 断开连接
void networkDisconnect();

// 返回当前连接状态
bool networkIsConnected();

// 获取最后一次错误描述
std::string networkLastError();

#endif // UNO_NETWORK_H
