#include <iostream>
#include <winsock2.h>
#include <combaseapi.h>
#include <stack>
// #include <thread>
// #include <chrono>

using namespace std;

stack<GUID> genGuids();

int main()
{
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    wVersionRequested = MAKEWORD(2, 2);

    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        cout << "Ошмбка инициализации WSAStartup: " << err << endl;
        return 1;
    }

    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        cout << "Не найдена нужная версия Winsock" << endl;
        WSACleanup();
        return 1;
    }
    else
        cout << "Winsock 2.2 dll успешно найден" << endl;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        cerr << "Ошибка создания сокета: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    cout << "Сокет успешно создан" << endl;

    closesocket(sock);
    WSACleanup();
    return 0;
}

stack<GUID> genGuids(){
    stack<GUID> guids;
    for (int i = 0; i < 4; i++){
        GUID guid;
        if (CoCreateGuid(&guid) == S_OK){
            guids.push(guid);
        }
    }
    return guids;
}

