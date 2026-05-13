#include "socket.h"
#include <iostream>
#include <regex>
#include <thread>

int createSocket() {
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
    return sock;
}
int connectAddr(SOCKET sock) {
    int conn = 0;
    int type = 0;
    while (type != 1 && type != 2) {
        cout << "Выберите тип соединения: " << endl;
        cout << "1. IP, Port" << endl;
        cout << "2. DNS" << endl;
        cin >> type;
        if (type != 1 && type != 2) {
            cout << "Ошибка при вводе типа подключения.";
        }
    }
    if (type == 1) {
        conn = connectIpAddr(sock);
    } else if (type == 2) {
        conn = connectDnsAddr(sock);
    } else {
        cerr << "Не выбран тип соединения, невозможно подключиться";
    }
    return conn;
}

int connectIpAddr(SOCKET sock) {
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

    return connect(sock, (SOCKADDR*)&destAddr, sizeof(destAddr));
}

int connectDnsAddr(SOCKET sock) {
    string hostname;
    int port;

    while (true) {
        cout << "Введите DNS-имя: ";
        cin >> hostname;

        cout << "Введите порт: ";
        if (!(cin >> port)) {
            cout << "Порт должен быть числом" << endl;
            continue;
        } else if (port < 0 || port > 65535) {
            cout << "Порт введен неверно" << endl;
            continue;
        }
        break;
    }

    struct addrinfo hints;
    struct addrinfo* result = nullptr;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    string portStr = to_string(port);

    int dnsResult = getaddrinfo(hostname.c_str(), portStr.c_str(), &hints, &result);
    if (dnsResult != 0) {
        cerr << "Ошибка разрешения DNS: " << dnsResult << endl;
        return SOCKET_ERROR;
    }

    int conn = connect(sock, result->ai_addr, static_cast<int>(result->ai_addrlen));

    freeaddrinfo(result);

    return conn;
}

void sendStack(SOCKET sock, std::stack<GUID> guidStack) {
    while (!guidStack.empty()){
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

        if (!guidStack.empty()) {
            std::this_thread::sleep_for(1s);
        }
    }
}

stack<GUID> recvStack(SOCKET sock) {
    stack<GUID> recvedGuids;
    GUID recvedGuid;

    DWORD timeout = 3000;
    if (setsockopt(sock,
                   SOL_SOCKET,
                   SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout),
                   sizeof(timeout)) == SOCKET_ERROR) {
        cerr << "Ошибка установки таймаута сокета: " << WSAGetLastError() << endl;
        return recvedGuids;
    }

    while (recvedGuids.size() < 4) {
        int bytesRecved = recv(sock,
                               reinterpret_cast<char*>(&recvedGuid),
                               sizeof(GUID),
                               0);
        if (bytesRecved > 0) {
            if (bytesRecved == sizeof(GUID)) {
                cout << "Получен GUID";
                recvedGuids.push(recvedGuid);
            } else {
                cerr << "Получены неполные данные";
                break;
            }
        } else if (bytesRecved == 0) {
            cout << "Соединение закрыто";
        } else {
            int errorCode = WSAGetLastError();
            if (errorCode == WSAETIMEDOUT) {
                cerr << "Превыщено время ожидания: " << errorCode << endl;
            }
            if (errorCode != WSAESHUTDOWN && errorCode != WSAECONNRESET) {
                cerr << "Ошибка приема: " << errorCode << endl;
            }
            break;
        }
    }
    return recvedGuids;
}
