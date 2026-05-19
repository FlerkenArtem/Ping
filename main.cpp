#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <combaseapi.h>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <thread>

using namespace std;
using namespace std::chrono;

/// Подключение сокета
optional<sockaddr_in> connectAddr();

/// Подключение сокета по IP-адресу
sockaddr_in connectIpAddr();

/// Подключение сокета по DNS-имени
optional<sockaddr_in> connectDnsAddr();

/// Выполнение Ping
unsigned long long ping(sockaddr_in destAddr);

/// Расчет контрольной суммы
unsigned short calculateChecksum(unsigned short *buffer, int size);

int main()
{
    system("chcp 65001");
    setlocale(LC_ALL, ".UTF8");
    WSADATA wsaData;
    int wsaStartupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaStartupResult != 0) {
        cerr << "Ошибка инициализации Winsock: " << wsaStartupResult << endl;
        return 1;
    }


    cout << "PING" << endl;

    optional<sockaddr_in> destAddr = connectAddr();
    if (destAddr == nullopt) {
        cerr << "Ошибка подключения к адресу" << endl;
        return 1;
    }

    ping(*destAddr);

    WSACleanup();
    return 0;
}

optional<sockaddr_in> connectAddr()
{
    optional<sockaddr_in> conn;
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

sockaddr_in connectIpAddr()
{
    const regex ipPattern("^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}"
                          "(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$");
    string ip;

    while (true) {
        cout << "Введите IP-адрес: ";
        cin >> ip;
        if (!regex_match(ip, ipPattern)) {
            cout << "IP-адрес введен неверно" << endl;
            continue;
        }
        break;
    }

    sockaddr_in destAddr;
    destAddr.sin_family = AF_INET;                    // IPv4
    destAddr.sin_port = 0;                            // Выбор случайного порта
    destAddr.sin_addr.s_addr = inet_addr(ip.c_str()); // Установка IP-адреса

    return destAddr;
}

optional<sockaddr_in> connectDnsAddr()
{
    string hostname;
    sockaddr_in destAddr;

    cout << "Введите DNS-имя: ";
    cin >> hostname;

    struct addrinfo hints;
    struct addrinfo *result = nullptr;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_RAW;
    hints.ai_protocol = IPPROTO_ICMP; // ICMP

    int dnsResult = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
    if (dnsResult != 0) {
        cerr << "Ошибка разрешения DNS: " << dnsResult << endl;
        return nullopt;
    }

    if (result != nullptr && result->ai_addr != nullptr) {
        memcpy(&destAddr, result->ai_addr, sizeof(sockaddr_in));
    }

    freeaddrinfo(result);
    return destAddr;
}

unsigned long long ping(sockaddr_in destAddr)
{
    /// Заголовок ICMP
    struct icmpHeader
    {
        unsigned char type;
        unsigned char code;
        unsigned short checkSum;
    };

    /// ICMP-пакет
    struct icmpPacket
    {
        icmpHeader header;
        GUID data;
    };

    /// Создание сокета
    SOCKET sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock == INVALID_SOCKET) {
        cerr << "Ошибка создания сокета: " << WSAGetLastError() << endl;
        WSACleanup();
        return INVALID_SOCKET;
    }

    /// Перевод сокета в неблокирующий режим
    unsigned long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);

    sockaddr_in localAddr;
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = 0;
    localAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (SOCKADDR *) &localAddr, sizeof(localAddr)) == SOCKET_ERROR) {
        cerr << "Ошибка bind для SOCK_RAW: " << WSAGetLastError() << endl;
        closesocket(sock);
        WSACleanup();
        return SOCKET_ERROR;
    }

    /// Создание словарей
    /// Ключ - целое число, служит для связи GUID и его состояний
    map<int, bool> sended;
    map<int, bool> recved;
    map<int, GUID> guids;

    /// Начальное заполнение словарей
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (int i = 0; i < 4; i++) {
        GUID guid;
        sended[i] = false;
        recved[i] = false;
        if (CoCreateGuid(&guid) == S_OK) {
            guids[i] = guid;
        } else {
            cerr << "Ошибка генерации GUID";
            return 1;
        }
    }
    CoUninitialize();

    int i = 0;

    /// Цикл отправки и приема пакетов
    while (true) {
        // Объявление пары GUID для сравнения
        GUID origGuid = guids[i];
        GUID recvedGuid;

        // Формирование пакета на отправку
        icmpPacket sendPack;
        sendPack.header.type = 8;
        sendPack.header.code = 0;
        sendPack.header.checkSum = 0;
        sendPack.data = origGuid;
        sendPack.header.checkSum = calculateChecksum(reinterpret_cast<unsigned short *>(&sendPack),
                                                     sizeof(sendPack));

        // Таймаут отправки
        DWORD sendTimeout = 1000;

        // Установка таймаута отправки
        if (setsockopt(sock,        // сокет
                       SOL_SOCKET,  // уровень сокета
                       SO_SNDTIMEO, // опция таймаута сокета
                       reinterpret_cast<const char *>(
                           &sendTimeout),   // указатель на буфер, содержащий значение таймаута
                       sizeof(sendTimeout)) // размер буфера
            == SOCKET_ERROR) {
            cerr << "Не удалось установить таймаут на отправку: " << WSAGetLastError() << endl;
            continue;
        }

        // Сохранение времени начала
        auto start = high_resolution_clock::now();

        // Отправка
        int res = sendto(sock,
                         reinterpret_cast<const char *>(&sendPack),
                         sizeof(sendPack),
                         0,
                         reinterpret_cast<SOCKADDR *>(&destAddr),
                         sizeof(destAddr));
        sended[i] = true;

        // Обработка ошибок отправки
        if (res == SOCKET_ERROR) {
            cerr << "Ошибка отправки: " << WSAGetLastError() << endl;
            continue;
        } else {
            cout << "Пакет отправлен." << endl;
            sended[i] = true;
        }

        // Установка размера буфера: IPv4-заголовок (20) + ICMP-пакет
        const int bufferSize = 20 + sizeof(icmpPacket);

        // Создание буфера
        char *recvBuffer = new char[bufferSize];

        // Длина адреса
        int addrLen = sizeof(destAddr);

        // Структура fd_set для хранения сокетов
        fd_set fdSet;
        FD_ZERO(&fdSet);      // Очистка
        FD_SET(sock, &fdSet); // Добавление сокета в набор

        // Таймаут получения
        timeval recvTimeout;
        recvTimeout.tv_sec = 3;  // с
        recvTimeout.tv_usec = 0; // мкс

        // Установка таймаута с помощью select
        int selectRes = select(0, &fdSet, NULL, NULL, &recvTimeout);

        int bytesRecved;

        if (selectRes > 0) {
            bytesRecved = recvfrom(sock,       // сокет
                                   recvBuffer, // указатель на буфер для приема данных
                                   bufferSize, // размер буфера
                                   0,          // флаги
                                   reinterpret_cast<SOCKADDR *>(
                                       &destAddr), // указатель на адрес источника
                                   &addrLen);      // указатель на длину структуры адреса источнка
        } else if (selectRes == 0) {
            cerr << "Превышен интервал ожидания запроса." << endl;
            delete[] recvBuffer;
            continue;
        } else {
            cerr << "Ошибка select: " << WSAGetLastError() << endl;
            delete[] recvBuffer;
            continue;
        }

        // Время принятия пакета
        auto end = high_resolution_clock::now();

        // Разница
        duration<double, milli> diff = end - start;

        if (bytesRecved > 0) {
            int ipHeaderLen = (recvBuffer[0] & 0x0F) * 4; // Вычисление длины IPv4 заголовка

            icmpPacket *recvPack = reinterpret_cast<icmpPacket *>(recvBuffer + ipHeaderLen);

            // Эхо-ответ
            if (recvPack->header.type == 0 && recvPack->header.code == 0) {
                // Получение GUID из ответа
                recvedGuid = recvPack->data;

                // Сравнение оригинального и полученного GUID
                if (IsEqualGUID(recvedGuid, origGuid)) {
                    cout << "Успешное получение (" << diff.count() << " мс)" << endl;
                } else {
                    cerr << "Получен чужой пакет";
                }
            }
            // Обработка ошибок
            else if (recvPack->header.type == 3) {
                cerr << "Ошибка: Адресат недостижим.\t";
                if (recvPack->header.code == 0) {
                    cerr << "Сеть недоступна";
                } else if (recvPack->header.code == 1) {
                    cerr << "Узел недоступен";
                } else if (recvPack->header.code == 2) {
                    cerr << "Протокол недоступен";
                } else if (recvPack->header.code == 3) {
                    cerr << "Порт недоступен";
                } else if (recvPack->header.code == 4) {
                    cerr << "Необходима фрагментация, но не задан бит ее запрета";
                } else if (recvPack->header.code == 5) {
                    cerr << "Ошибка на исходном маршруте";
                } else if (recvPack->header.code == 6) {
                    cerr << "Сеть адресата неизвестна";
                } else if (recvPack->header.code == 7) {
                    cerr << "Узел адресата неизвестен";
                } else if (recvPack->header.code == 8) {
                    cerr << "Исходный узел изолирован";
                } else if (recvPack->header.code == 9) {
                    cerr << "Сеть адресата административно изолирована";
                } else if (recvPack->header.code == 10) {
                    cerr << "Узел адресата административно изолирован";
                } else if (recvPack->header.code == 11) {
                    cerr << "Сеть недоступна для TOS";
                } else if (recvPack->header.code == 12) {
                    cerr << "Узел недоступен для TOS";
                } else if (recvPack->header.code == 13) {
                    cerr << "Связь административно запрещена фильтрацией";
                } else if (recvPack->header.code == 14) {
                    cerr << "Нарушение приоритета узлов";
                } else if (recvPack->header.code == 15) {
                    cerr << "Пренебрежение приоритетом узлов";
                } else {
                    cerr << "Ошибка";
                }
            } else if (recvPack->header.type == 4 && recvPack->header.code == 0) {
                cerr << "Ошибка.\tПодавление отправителя.";
            } else if (recvPack->header.type == 5) {
                cerr << "Ошибка: Перенаправление.\t";
                if (recvPack->header.code == 0) {
                    cerr << "Перенаправление для сети";
                } else if (recvPack->header.code == 1) {
                    cerr << "Перенаправление на узел";
                } else if (recvPack->header.code == 2) {
                    cerr << "Перенаправление на TOS и сеть";
                } else if (recvPack->header.code == 3) {
                    cerr << "Перенаправление на TOS и узел";
                } else {
                    cerr << "Ошибка";
                }
            } else if (recvPack->header.type == 11) {
                cerr << "Ошибка: Время превышено.\t";
                if (recvPack->header.code == 0) {
                    cerr << "TTL в ходе транзита равен 0";
                } else if (recvPack->header.code == 1) {
                    cerr << "TTL в ходе повторной сборки равен 0";
                } else {
                    cerr << "Ошибка";
                }
            } else if (recvPack->header.type == 12) {
                cerr << "Ошибка: Проблема параметра.\t";
                if (recvPack->header.code == 0) {
                    cerr << "Неверный заголовок IP";
                } else if (recvPack->header.code == 1) {
                    cerr << "Отсутствует требуемый параметр";
                } else {
                    cerr << "Ошибка";
                }
            } else {
                cerr << "Ошибка";
            }

            recved[i] = true;
        }

        /// Проверка условий завершения
        bool allSendedTrue = all_of(sended.begin(), sended.end(), [](const pair<int, bool> &pair) {
            return pair.second == true;
        });
        bool allRecvedTrue = all_of(recved.begin(), recved.end(), [](const pair<int, bool> &pair) {
            return pair.second == true;
        });

        /// Очистка памяти
        delete[] recvBuffer;

        /// Завершение
        if (allSendedTrue && allRecvedTrue) {
            break;
        } else if (recved[i] == true && sended[i == true]) {
            i++;
        }

        /// Задержка
        this_thread::sleep_for(chrono::milliseconds(1000));
    }

    return 0;
}

unsigned short calculateChecksum(unsigned short *buffer, int size)
{
    unsigned long cksum = 0;
    while (size > 1) {
        cksum += *buffer++;
        size -= 2;
    }
    if (size) {
        cksum += *(static_cast<unsigned char *>(static_cast<void *>(buffer)));
    }
    cksum = (cksum >> 16) + (cksum & 0xffff);
    cksum += (cksum >> 16);
    return static_cast<unsigned short>(~cksum);
}
