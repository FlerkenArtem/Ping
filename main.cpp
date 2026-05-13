#include <combaseapi.h>
#include <future>
#include <iostream>
#include <regex>
#include <stack>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>

using namespace std;

struct icmpHeader {
    unsigned char type;
    unsigned char code;
    unsigned short checkSum;
    unsigned short id;
    unsigned short sequence;
};

struct icmpPacket {
    icmpHeader header;
    GUID data;
};

/// Генерация стека GUID для отправки
stack<GUID> genGuids();

/// Создание сокета
int createSocket();

/// Отправка стека GUID
void sendStack(SOCKET sock, sockaddr_in destAddr, std::stack<GUID> guidStack);

/// Прием стека GUID
stack<GUID> recvStack(SOCKET sock);

/// Сравнение стеков GUID
bool compareGuidStack(stack<GUID> a, stack<GUID> b);

/// Вычисление контрольной суммы
unsigned short calculateChecksum(unsigned short* buffer, int size);

/// Подключение сокетв
sockaddr_in connectAddr();

/// Подключение сокета по IP-адресу
sockaddr_in connectIpAddr();

/// Подключение сокета по DNS-имени
sockaddr_in connectDnsAddr();

/// Точка входа
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

    sockaddr_in destAddr = connectAddr();

    stack<GUID> guids = genGuids();

    future<stack<GUID>> recvFuture = async(launch::async, recvStack, sock);

    thread sendThread(sendStack, sock, destAddr, guids);

    if (sendThread.joinable()) {
        sendThread.join();
    }

    stack<GUID> recvedGuids = recvFuture.get();

    if (compareGuidStack(guids, recvedGuids)) {
        cout << "Успех" << endl;
    } else {
        cout << "Ошибка" << endl;
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}

stack<GUID> genGuids() {
    stack<GUID> guids;
    for (int i = 0; i < 4; i++){
        GUID guid;
        if (CoCreateGuid(&guid) == S_OK){
            guids.push(guid);
        }
    }
    return guids;
}

bool compareGuidStack(stack<GUID> a, stack<GUID> b) {
    if (a.size() != b.size()){
        return false;
    }

    stack<GUID> invertedB;
    while (!b.empty()) {
        invertedB.push(b.top());
        b.pop();
    }

    while (!a.empty() && !invertedB.empty()) {
        if (!IsEqualGUID(a.top(), invertedB.top())) {
            return false;
        }
        a.pop();
        invertedB.pop();
    }

    return true;
}

int createSocket() {
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    wVersionRequested = MAKEWORD(2, 2);

    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        cout << "Ошибка инициализации WSAStartup: " << err << endl;
        return 1;
    }

    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        cout << "Не найдена нужная версия Winsock" << endl;
        WSACleanup();
        return 1;
    }
    else
        cout << "Winsock 2.2 dll успешно найден" << endl;

    SOCKET sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock == INVALID_SOCKET) {
        cerr << "Ошибка создания сокета: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in localAddr;
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = 0;
    localAddr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(sock,
               (SOCKADDR*)&localAddr,
               sizeof(localAddr)) == SOCKET_ERROR) {
        cerr << "Ошибка bind для SOCK_RAW: " << WSAGetLastError() << endl;

        closesocket(sock);
        WSACleanup();

        return 1;
    }

    cout << "Сокет успешно создан и привязан к интерфейсу" << endl;
    return sock;
}

sockaddr_in connectAddr() {
    sockaddr_in conn;
    int type = 0;
    while (type != 1 && type != 2) {
        cout << "Выберите тип соединения: " << endl;
        cout << "1. IP" << endl;
        cout << "2. DNS" << endl;
        cin >> type;
        if (type != 1 && type != 2) {
            cout << "Ошибка при вводе типа подключения.";
        }
    }
    if (type == 1) {
        conn = connectIpAddr();
    } else if (type == 2) {
        conn = connectDnsAddr();
    } else {
        cerr << "Не выбран тип соединения, невозможно подключиться";
    }
    return conn;
}

sockaddr_in connectIpAddr() {
    const std::regex ipPattern(
        "^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}"
        "(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$");
    string ip;

    while (true) {
        cout << "Введите IP-адрес: ";
        cin >> ip;
        if (!std::regex_match(ip, ipPattern)){
            cout << "IP-адрес введен неверно";
            continue;
        }
        break;
    }

    sockaddr_in destAddr;
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = 0;
    destAddr.sin_addr.s_addr = inet_addr(ip.c_str());

    return destAddr;
}

sockaddr_in connectDnsAddr() {
    string hostname;
    sockaddr_in destAddr;

    while (true) {
        cout << "Введите DNS-имя: ";
        cin >> hostname;
        break;
    }

    struct addrinfo hints;
    struct addrinfo* result = nullptr;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_RAW;
    hints.ai_protocol = IPPROTO_ICMP;

    int dnsResult = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
    if (dnsResult != 0) {
        cerr << "Ошибка разрешения DNS: " << dnsResult << endl;
        return destAddr;
    }

    if (result != nullptr && result->ai_addr != nullptr) {
        memcpy(&destAddr, result->ai_addr, sizeof(sockaddr_in));
    }

    freeaddrinfo(result);
    return destAddr;
}

void sendStack(SOCKET sock, sockaddr_in destAddr, std::stack<GUID> guidStack) {
    unsigned short sequenseNumber = 0;
    unsigned short processId = static_cast<unsigned short>(GetCurrentProcessId());

    while (!guidStack.empty()) {
        icmpPacket pac;
        pac.header.type = 8;
        pac.header.code = 0;
        pac.header.checkSum = 0;
        pac.header.id = processId;
        pac.header.sequence = htons(sequenseNumber++);
        pac.data = guidStack.top();

        pac.header.checkSum = calculateChecksum(reinterpret_cast<unsigned short*>(&pac),
                                                sizeof(pac));

        int res = sendto(sock,
                         reinterpret_cast<const char*>(&pac),
                         sizeof(pac),
                         0,
                         reinterpret_cast<SOCKADDR*>(&destAddr),
                         sizeof(destAddr));
        if (res == SOCKET_ERROR) {
            cerr << "Ошибка отправки: " << WSAGetLastError() << endl;
            break;
        }
        cout << "Пакет отправлен." << endl;

        guidStack.pop();

        if (!guidStack.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }
}

stack<GUID> recvStack(SOCKET sock) {
    stack<GUID> recvedGuids;

    DWORD timeout = 3000;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == SOCKET_ERROR) {
        cerr << "Не удалось установить таймаут: " << WSAGetLastError() << endl;
        return recvedGuids;
    }
    const int bufferSize = 20 + sizeof(icmpPacket);
    char* recvBuffer = new char[bufferSize];
    unsigned short processId = static_cast<unsigned short>(GetCurrentProcessId());

    while (recvedGuids.size() < 4) {
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);

        int bytesRecved = recvfrom(sock,
                                   recvBuffer,
                                   bufferSize,
                                   0,
                                   reinterpret_cast<SOCKADDR*>(&fromAddr),
                                   &fromLen);

        if (bytesRecved > 0) {
            int ipHeaderLen = (recvBuffer[0] & 0x0F) * 4;

            if (bytesRecved >= ipHeaderLen + static_cast<int>(sizeof(icmpPacket))) {
                icmpPacket* pac = reinterpret_cast<icmpPacket*>(recvBuffer + ipHeaderLen);

                if (pac->header.type == 0 && pac->header.id == processId) {
                    cout << "Успешное получение" << endl;
                    recvedGuids.push(pac->data);
                } else {
                    continue;
                }
            }
        } else {
            int errorCode = WSAGetLastError();
            if (errorCode == WSAETIMEDOUT) {
                cerr << "Истекли 3 секунды ожидания " << endl;
            } else if (errorCode != WSAESHUTDOWN && errorCode != WSAECONNRESET) {
                cerr << "Ошибка получения: " << errorCode << endl;
            }
            break;
        }
    }

    delete[] recvBuffer;
    return recvedGuids;
}

unsigned short calculateChecksum(unsigned short* buffer, int size) {
    unsigned long cksum = 0;
    while (size > 1) {
        cksum += *buffer++;
        size -= 2;
    }
    if (size) {
        cksum += *(static_cast<unsigned char*>(static_cast<void*>(buffer)));
    }
    cksum = (cksum >> 16) + (cksum & 0xffff);
    cksum += (cksum >> 16);
    return static_cast<unsigned short>(~cksum);
}
