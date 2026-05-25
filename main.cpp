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

/// Подключение сокета
optional<sockaddr_in> connectAddr();

/// Получение количества шагов
int getSteps();

/// Выполнение Ping
unsigned long long ping(sockaddr_in destAddr, int steps = 4);

/// Условие завершения
bool endPing(const map<int, bool> &recved, const map<int, bool> &sended);

/// Расчет контрольной суммы
unsigned short calculateChecksum(unsigned short *buffer, int size);

int main()
{
    system("chcp 65001 > nul");
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
        memcpy(&destAddr, result->ai_addr, sizeof(sockaddr_in));
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

    /// Создание словарей
    /// Ключ - целое число, служит для связи GUID и его состояний
    map<int, bool> sended;
    map<int, bool> recved;
    map<int, GUID> guids;

    /// Начальное заполнение словарей
    if (!FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        for (int i = 0; i < steps; i++) {
            GUID guid;
            sended[i] = false;
            recved[i] = false;
            if (CoCreateGuid(&guid) == S_OK) {
                guids[i] = guid;
            } else {
                cerr << "Ошибка генерации GUID";
                CoUninitialize();
                closesocket(sock);
                return 1;
            }
        }
        CoUninitialize();
    } else {
        cerr << "Ошибка инициализации COM,"
                "Невозможно создать GUID.";
        CoUninitialize();
        closesocket(sock);
        return 1;
    }

    int i = 0;

    /// Цикл отправки и приема пакетов
    while (i < steps) {
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
            sended[i] = true;
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

            if (bytesRecved == SOCKET_ERROR) {
                cerr << "Ошибка получения";
                closesocket(sock);
                delete[] recvBuffer;
                return 1;
            }
        } else if (selectRes == 0) {
            cerr << "Превышен интервал ожидания запроса." << endl;
            delete[] recvBuffer;
            recved[i] = true;
            i++;
            if (endPing(recved, sended))
                break;
            continue;

        } else {
            cerr << "Ошибка select: " << WSAGetLastError() << endl;
            delete[] recvBuffer;
            recved[i] = true;
            i++;
            if (endPing(recved, sended))
                break;
            continue;
        }

        // Время принятия пакета
        auto end = high_resolution_clock::now();

        // Разница
        duration<double, milli> diff = end - start;

        if (bytesRecved > 0) {
            int ipHeaderLen = (recvBuffer[0] & 0x0F) * 4; // Вычисление длины IPv4 заголовка

            // Обработка несоответствия размера полученного пакета минимальному
            if ((unsigned int) bytesRecved < ipHeaderLen + sizeof(icmpPacket)) {
                cerr << "Ошибка: Слишком малый размер полученных данных.";
                delete[] recvBuffer;
                continue;
            }
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

        // Очистка памяти
        delete[] recvBuffer;

        if (endPing(recved, sended))
            break;

        i++;

        // Задержка
        this_thread::sleep_for(chrono::milliseconds(1000));
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

bool endPing(const map<int, bool> &recved, const map<int, bool> &sended)
{
    /// Проверка условий завершения
    bool allSendedTrue = all_of(sended.begin(), sended.end(), [](const pair<int, bool> &pair) {
        return pair.second == true;
    });
    bool allRecvedTrue = all_of(recved.begin(), recved.end(), [](const pair<int, bool> &pair) {
        return pair.second == true;
    });

    /// Завершение
    if (allSendedTrue && allRecvedTrue) {
        return true;
    } else {
        return false;
    }
}
