#ifndef SOCKET_H
#define SOCKET_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <combaseapi.h>
#include <stack>

using namespace std;

/// Создание сокета
int createSocket();

/// Подключение сокета
int connectAddr(SOCKET sock);

/// Подключение сокета по IP и порту
int connectIpAddr(SOCKET sock);

/// Подключение сокета по DNS
int connectDnsAddr(SOCKET sock);

/// Отправка стека GUID
void sendStack(SOCKET sock, std::stack<GUID> guidStack);

/// Получение стека GUID
stack<GUID> recvStack(SOCKET sock);

#endif // SOCKET_H

#pragma once
