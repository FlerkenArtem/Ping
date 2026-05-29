#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <combaseapi.h>
#include <iostream>
#include <map>
#include <optional>
#include <thread>

using namespace std;
using namespace std::chrono;

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

/// Структура с дополнительными данными о пакете
struct packData
{
    GUID origGuid;
    GUID recvedGuid;
    bool sended;
    bool recved;
    time_point<system_clock> sendTime;
    time_point<system_clock> recvTime;
};

/// Подключение сокета
optional<sockaddr_in> connectAddr();

/// Получение количества шагов
int getSteps();

/// Выполнение Ping
unsigned long long ping(sockaddr_in destAddr, int steps = 4);

/// Условие завершения
bool endPing(const map<int, packData> &guids);

/// Расчет контрольной суммы
unsigned short calculateChecksum(unsigned short *buffer, int size);

int main()
{
    system("chcp 65001 > nul");
    setlocale(LC_ALL, ".UTF8");

    cout << "PING" << endl;

    WSADATA wsaData;
    int wsaStartupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaStartupResult != 0) {
        cerr << "Ошибка инициализации Winsock: " << wsaStartupResult << endl;
        return 1;
    }
    optional<sockaddr_in> destAddr = connectAddr();
    if (destAddr == nullopt) {
        cerr << "Ошибка подключения к адресу" << endl;
        return 1;
    }

    int steps = getSteps();

    ping(*destAddr, steps);

    WSACleanup();
    return 0;
}

optional<sockaddr_in> connectAddr()
{
    string hostname;
    sockaddr_in destAddr;

    cout << "Введите адрес: ";
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
        destAddr = *(sockaddr_in *) result->ai_addr;
    }

    freeaddrinfo(result);
    return destAddr;
}

int getSteps()
{
    while (true) {
        int steps = 0;
        cout << "Введите количество шагов: ";
        if (cin >> steps && steps > 0) {
            return steps;
        } else {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            cerr << "Ошибка в количестве шагов. "
                    "Необходимо указать целое число шагов, "
                    "которое больше 0."
                 << endl;
        }
    }
}

unsigned long long ping(sockaddr_in destAddr, int steps)
{
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

    /// Создание словаря guids, который содержит структуру содержащую:
    /// оригинальный и полученный GUID,
    /// статус отправки и получения для каждого GUID,
    /// время отправки и получения для каждого GUID.
    map<int, packData> guids;

    /// Начальное заполнение словаря
    for (int i = 0; i < steps; i++) {
        GUID guid;
        if (CoCreateGuid(&guid) == S_OK) {
            packData data;
            data.origGuid = guid;
            guids[i] = data;
        } else {
            cerr << "Ошибка генерации GUID";
            closesocket(sock);
            return 1;
        }
    }

    /// Цикл отправки и приема пакетов
    for (int i = 0; i < steps; i++) {
        // Формирование пакета на отправку
        icmpPacket sendPack;
        sendPack.header.type = 8;
        sendPack.header.code = 0;
        sendPack.header.checkSum = 0;
        sendPack.data = guids[i].origGuid;
        sendPack.header.checkSum = calculateChecksum((unsigned short *) &sendPack, sizeof(sendPack));

        high_resolution_clock::duration iterationSendDiff;

        if (i != 0) {
            iterationSendDiff = high_resolution_clock::now() - guids[i - 1].sendTime;

            if (iterationSendDiff < 1s) {
                high_resolution_clock::duration sleepDuration = 1s - iterationSendDiff;
                this_thread::sleep_for(sleepDuration);
            }
        }

        // Отправка
        int res = sendto(sock,
                         (const char *) &sendPack,
                         sizeof(sendPack),
                         0,
                         (sockaddr *) &destAddr,
                         sizeof(destAddr));

        // Обработка ошибок отправки
        if (res == SOCKET_ERROR) {
            cerr << "Ошибка отправки: " << WSAGetLastError() << endl;
        } else {
            cout << "Пакет отправлен." << endl;
        }

        guids[i].sendTime = high_resolution_clock::now();
        guids[i].sended = true;

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

        timeval timeout;
        timeout.tv_sec = 3;
        timeout.tv_usec = 0;

        // select
        int selectRes = select(0, &fdSet, NULL, NULL, &timeout);

        int bytesRecved = 0;

        sockaddr_in fromAddr;

        if (selectRes == SOCKET_ERROR) {
            cerr << "Ошибка select: " << WSAGetLastError() << endl;
            delete[] recvBuffer;
            guids[i].recved = true;
            i++;
            if (endPing(guids))
                break;
            continue;
        }

        if (selectRes == 0) {
            cerr << "Истек таймаут ожидаения пакета" << endl;
            guids[i].recved = true;
            i++;
            if (endPing(guids))
                break;
            continue;
        }

        if (FD_ISSET(sock, &fdSet)) {
            bytesRecved = recvfrom(sock,                   // сокет
                                   recvBuffer,             // указатель на буфер для приема данных
                                   bufferSize,             // размер буфера
                                   0,                      // флаги
                                   (SOCKADDR *) &fromAddr, // указатель на адрес источника
                                   &addrLen); // указатель на длину структуры адреса источника
            guids[i].recved = true;
            if (bytesRecved == SOCKET_ERROR && WSAGetLastError() != WSAEMSGSIZE) {
                cerr << "Ошибка select: " << WSAGetLastError() << endl;
                delete[] recvBuffer;
                i++;
                if (endPing(guids))
                    break;
                continue;
            }
        }

        // Время принятия пакета
        guids[i].recvTime = high_resolution_clock::now();

        // Разница
        duration<double, milli> diff = guids[i].recvTime - guids[i].sendTime;

        if (bytesRecved > 0) {
            int ipHeaderLen = (recvBuffer[0] & 0x0F) * 4; // Вычисление длины IPv4 заголовка

            // Обработка несоответствия размера полученного пакета минимальному
            if ((unsigned int) bytesRecved < ipHeaderLen + sizeof(icmpPacket)) {
                cerr << "Ошибка: Слишком малый размер полученных данных.";
                delete[] recvBuffer;
                continue;
            }
            icmpPacket *recvPack = (icmpPacket *) (recvBuffer + ipHeaderLen);

            // Эхо-ответ
            if (recvPack->header.type == 0 && recvPack->header.code == 0) {
                // Получение GUID из ответа
                guids[i].recvedGuid = recvPack->data;

                // Сравнение оригинального и полученного GUID
                if (IsEqualGUID(guids[i].recvedGuid, guids[i].origGuid)) {
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

            guids[i].recved = true;
        }

        // Очистка памяти
        delete[] recvBuffer;

        if (endPing(guids))
            break;
    }

    // Закрытие сокета
    closesocket(sock);
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

bool endPing(const map<int, packData> &guids)
{
    return std::none_of(guids.begin(), guids.end(), [](const auto &pair) {
        return !pair.second.sended || !pair.second.recved;
    });
}
