#include <winsock2.h>
#include <ws2tcpip.h>
#include <combaseapi.h>
#include <future>
#include <iostream>
#include <stack>
#include <thread>
#include "guid.h"
#include "socket.h"

using namespace std;

int main()
{
    system("chcp 65001");
    setlocale(LC_ALL, ".UTF8");

    cout << "PING";

    SOCKET sock = createSocket();
    if (sock == 1) {
        cerr << "Ошибка создания сокета";
        return 1;
    }

    if (connectAddr(sock) == SOCKET_ERROR) {
        cerr << "Не удалось подключиться к удаленному узлу. Ошибка Winsock: " << WSAGetLastError() << endl;
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    stack<GUID> guids = genGuids();
    thread sendThread(sendStack, sock, guids);
    future<stack<GUID>> recvFuture = async(launch::async, recvStack, sock);

    if (sendThread.joinable()) {
        sendThread.join();
    }

    stack<GUID> recvedGuids = recvFuture.get();

    if (compareGuidStack(guids, recvedGuids)) {
        cout << "Успех";
    } else {
        cout << "Ошибка";
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}

