#include <combaseapi.h>
#include <iostream>
#include <regex>
#include <stack>
#include <winsock2.h>
// #include <thread>
// #include <chrono>

using namespace std;

stack<GUID> genGuids();
void sendStack(SOCKET sock, std::stack<GUID>& guidStack);

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

    const std::regex ipPattern(
        "^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}"
        "(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$");
    string ip;
    int port;

    while (true) {
        cout << "Введите IP-адрес: ";
        cin >> ip;
        if (!std::regex_match(ip, ipPattern)){
            cout << "IP-адрес введен неверно";
            continue;
        }

        cout << "Введите порт: ";
        if (!(cin >> port)) {
            cout << "Порт должен быть числом" << endl;
            continue;
        } else if (port < 0 || port > 65535){
            cout << "Порт введен неверно";
            continue;
        }

        break;
    }

    sockaddr_in destAddr;
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(port);
    destAddr.sin_addr.s_addr = inet_addr(ip.c_str());

    connect(sock, (SOCKADDR*)&destAddr, sizeof(destAddr));

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

void sendStack(SOCKET sock, std::stack<GUID>& guidStack){
    while (guidStack.empty()){
        GUID guid = guidStack.top();
        int res = send(sock,
                       reinterpret_cast<const char*>(&guid),
                       sizeof(GUID),
                       0);
        if (res == SOCKET_ERROR){
            cerr << "Ошибка отправки: " << WSAGetLastError() << endl;
            break;
        }
        guidStack.pop();
    }
}
