#include <algorithm>
#include <chrono>
#include <combaseapi.h>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>

using namespace std;
using namespace std::chrono;

/// Заголовок IP
#pragma pack(push, 1)
struct ipHeader
{
    unsigned char len : 4;
    unsigned char version : 4;
    unsigned char tos;
    unsigned short totalLen;
    unsigned short id;
    unsigned short flags;
    unsigned char ttl;
    unsigned char proto;
    unsigned short checkSum;

    unsigned int srcIp;
    unsigned int destIp;
};

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
    GUID recvedGuid;
    time_point<high_resolution_clock> sendTime;
    time_point<high_resolution_clock> recvTime;

    bool recved = false;
    bool timeout = false;
    bool error = false;
};
#pragma pack(pop)

/// Подключение сокета
optional<sockaddr_in> connectAddr();

/// Получение количества шагов
int getSteps();

/// Выполнение Ping
unsigned long long ping(sockaddr_in destAddr, int steps = 4);

/// Вывод статистики
void stats(map<GUID, packData> guids, vector<double> timeDiff, int steps);

/// Расчет контрольной суммы
unsigned short calculateChecksum(unsigned short *buffer, int size);

/// Оператор сравнения 2 GUID
bool operator<(const GUID &guid1, const GUID &guid2);

/// Обработка ошибок ICMP
void errors(unsigned char type, unsigned char code);

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

    addrinfo hints;
    addrinfo *result = nullptr;

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
    // Создание сокета
    SOCKET sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock == INVALID_SOCKET) {
        cerr << "Ошибка создания сокета: " << WSAGetLastError() << endl;
        WSACleanup();
        closesocket(sock);
        return INVALID_SOCKET;
    }

    // Перевод сокета в неблокирующий режим
    unsigned long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);

    // Создание словаря guids
    map<GUID, packData> guids;

    // Вектор времени ответа
    vector<double> timeDiff;

    // Последний отправленный GUID
    GUID lastSendedGuid{};

    GUID lastRecvedGuid{};

    vector<bool> processed{};

    bool end = false;
    int i = 0;

    // Цикл отправки и приема пакетов
    while (!end) {
        // Расчет времени между отправкой
        high_resolution_clock::duration iterationSendDiff;
        if (i != 0) {
            iterationSendDiff = high_resolution_clock::now() - guids[lastSendedGuid].sendTime;
        }

        // Отправка GUID
        if ((i == 0 || iterationSendDiff >= 1s) && i < steps) {
            // Формирование GUID
            GUID origGuid;
            if (!(CoCreateGuid(&origGuid) == S_OK)) {
                cerr << "Ошибка генерации GUID";
                closesocket(sock);
                return 1;
            }

            // Формирование пакета на отправку
            icmpPacket sendPack;
            sendPack.header.type = 8;
            sendPack.header.code = 0;
            sendPack.header.checkSum = 0;
            sendPack.data = origGuid;
            sendPack.header.checkSum = calculateChecksum((unsigned short *) &sendPack,
                                                         sizeof(sendPack));

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

            guids[origGuid].sendTime = high_resolution_clock::now();

            lastSendedGuid = origGuid;
        }

        // Минимальная длина IP-заголовка
        int ipHeaderMinLen = sizeof(ipHeader);

        // Установка размера буфера: IPv4-заголовок + ICMP-пакет
        const int bufferSize = ipHeaderMinLen + sizeof(icmpPacket);

        // Создание буфера
        char *recvBuffer = new char[bufferSize];

        // Длина адреса
        int addrLen = sizeof(destAddr);

        optional<duration<double, milli>> recvWaitDiff;

        if (i != 0) {
            // Разница между началом получения и текущей попыткой
            recvWaitDiff = high_resolution_clock::now() - guids[lastRecvedGuid].recvTime;
        }

        auto recvStart = high_resolution_clock::now();

        if (i == 0 || !recvWaitDiff.has_value() || recvWaitDiff >= 3s) {
            // Структура fd_set для хранения сокетов
            fd_set fdSet;
            FD_ZERO(&fdSet);      // Очистка
            FD_SET(sock, &fdSet); // Добавление сокета в набор

            // Таймаут функции select
            timeval timeout;
            timeout.tv_sec = 3;
            timeout.tv_usec = 0;

            // select
            int selectRes = select(0, &fdSet, NULL, NULL, &timeout);

            int bytesRecved = 0;

            sockaddr_in fromAddr{};

            // Ошибка select
            if (selectRes <= 0) {
                continue;
            }

            // Сокет sock содержится в fdSet
            if (FD_ISSET(sock, &fdSet)) {
                int recvError = 0;
                do {
                    duration<double, milli> recvTimeout = high_resolution_clock::now() - recvStart;
                    if (recvTimeout > 3s) {
                        break;
                    }

                    bytesRecved = recvfrom(sock,       // сокет
                                           recvBuffer, // указатель на буфер для приема данных
                                           bufferSize, // размер буфера
                                           0,          // флаги
                                           (SOCKADDR *) &fromAddr, // указатель на адрес источника
                                           &addrLen); // указатель на длину структуры адреса источника
                    if (bytesRecved == SOCKET_ERROR) {
                        recvError = WSAGetLastError();
                        if (recvError != WSAEWOULDBLOCK) {
                            delete[] recvBuffer;
                            cerr << "Возникла ошибка при получении :" << recvError;
                            return 1;
                        }
                    } else if (bytesRecved > 0) {
                        // Проверка на то, что ответ пришел с целевого узла
                        if (fromAddr.sin_addr.s_addr != destAddr.sin_addr.s_addr) {
                            continue;
                        }

                        // Проверка что ответ содержит данные
                        if (bytesRecved <= 0) {
                            continue;
                        }

                        // Получение IP-заголовка из буфера
                        ipHeader *ipHdr = (ipHeader *) recvBuffer;

                        // Вычисление реальной длины IP-заголовка:
                        // поле len обозначает длину IP-заголовка в 32 битных словах,
                        // находим длину в байтах
                        int ipHeaderLen = ipHdr->len * 4;

                        // Проверка по длине,
                        // что полученный пакет содержит IP-заголовок
                        // и ICMP заголовок
                        if (bytesRecved < ipHeaderLen + (int) sizeof(icmpHeader)) {
                            continue;
                        }

                        // Получение ICMP пакета
                        icmpPacket *recvPack = (icmpPacket *) (recvBuffer + ipHeaderLen);

                        // Типы кроме 0 пропускаются
                        if (recvPack->header.type != 0 && recvPack->header.code != 0) {
                            // Формирование ICMP-сообщения об ошибке
                            icmpPacket *errorPack = (icmpPacket *) (recvBuffer + ipHeaderLen + 8);

                            // Получение GUID из полученного пакета
                            GUID recvGuid = recvPack->data;

                            // Итератор по map guids по значению полученного Guid
                            auto it = guids.find(recvGuid);

                            // Чужой пакет
                            if (it == guids.end()) {
                                continue;
                            }

                            // Вывод ошибок
                            errors(errorPack->header.type, errorPack->header.code);

                            processed.push_back(true);

                            continue;
                        }

                        // Получение GUID из полученного пакета
                        GUID recvGuid = recvPack->data;

                        lastRecvedGuid = recvGuid;

                        // Итератор по map guids по значению полученного Guid
                        auto it = guids.find(recvGuid);

                        // Чужой пакет
                        if (it == guids.end()) {
                            continue;
                        }

                        // Получение времени получения пакета
                        it->second.recvTime = high_resolution_clock::now();

                        it->second.recved = true;

                        // Вычисление промежутка времени между отправкой и получением пакета
                        duration<double, milli> diff = it->second.recvTime - it->second.sendTime;

                        // Получение TTL
                        int ttl = ipHdr->ttl;

                        cout << "Успешное получение (" << diff.count() << " мс, TTL = " << ttl
                             << ")." << endl;

                        processed.push_back(true);

                        timeDiff.push_back(diff.count());
                    }
                } while (recvError != WSAEWOULDBLOCK);
            }
        }

        // Обработка истечения таймаута
        high_resolution_clock::duration recvDiff = high_resolution_clock::now() - recvStart;
        if (recvDiff >= 3s) {
            cerr << "Таймаут истек";
            processed.push_back(false);
        }

        // Завершение цикла
        if (processed.size() == (unsigned long long) steps) {
            if (count(processed.begin(), processed.end(), true)) {
                end = true;
            }
        }

        // Очистка памяти
        delete[] recvBuffer;

        i++;
    }

    // Вывод статистики
    stats(guids, timeDiff, steps);

    // Закрытие сокета
    closesocket(sock);
    return 0;
}

void stats(map<GUID, packData> guids, vector<double> timeDiff, int steps)
{
    cout << endl << "Статистика" << endl << endl;

    if (guids.size() == (unsigned long long) steps) {
        int cnt = guids.size();
        int trueCnt = count_if(guids.begin(), guids.end(), [](const auto &pair) {
            return pair.second.recved;
        });
        int falseCnt = cnt - trueCnt;

        double falsePercent = ((double) falseCnt / (double) cnt) * 100.0;

        cout << "Пакетов отправлено: " << cnt << endl
             << "Успешно: " << trueCnt << endl
             << "С ошибкой: " << falseCnt << endl
             << "Процент ошибок: " << (int) falsePercent << " %" << endl
             << endl;
    } else {
        cerr << "Ошибка при формировании статистики по отправке пакетов. "
                "Статистика создана на иное количество пакетов, "
                "чем было отправлено."
             << endl
             << endl;
    }

    if (timeDiff.size() > 0) {
        double min = *min_element(timeDiff.begin(), timeDiff.end());
        double avg = accumulate(timeDiff.begin(), timeDiff.end(), 0.0) / timeDiff.size();
        double max = *max_element(timeDiff.begin(), timeDiff.end());

        cout << "Минимальное время: " << min << " мс." << endl
             << "Среднее время: " << avg << " мс." << endl
             << "Максимальное время: " << max << " мс." << endl
             << endl;
    } else {
        cerr << "Не было получено ни одного пакета. "
                "Статистику времени получения составить невозможно."
             << endl
             << endl;
    }
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

bool operator<(const GUID &guid1, const GUID &guid2)
{
    if (guid1.Data1 != guid2.Data1) {
        return guid1.Data1 < guid2.Data1;
    }
    if (guid1.Data2 != guid2.Data2) {
        return guid1.Data2 < guid2.Data2;
    }
    if (guid1.Data3 != guid2.Data3) {
        return guid1.Data3 < guid2.Data3;
    }
    for (int i = 0; i < 8; i++) {
        if (guid1.Data4[i] != guid2.Data4[i]) {
            return guid1.Data4[i] < guid2.Data4[i];
        }
    }
    return false;
}

void errors(unsigned char type, unsigned char code)
{
    if (type == 0 && code == 0)
        return;
    if (type == 3) {
        cerr << "Ошибка: Адресат недостижим.\t";
        if (code == 0) {
            cerr << "Сеть недоступна.";
        } else if (code == 1) {
            cerr << "Узел недоступен.";
        } else if (code == 2) {
            cerr << "Протокол недоступен.";
        } else if (code == 3) {
            cerr << "Порт недоступен.";
        } else if (code == 4) {
            cerr << "Необходима фрагментация, но не задан бит ее запрета.";
        } else if (code == 5) {
            cerr << "Ошибка на исходном маршруте.";
        } else if (code == 6) {
            cerr << "Сеть адресата неизвестна.";
        } else if (code == 7) {
            cerr << "Узел адресата неизвестен.";
        } else if (code == 8) {
            cerr << "Исходный узел изолирован.";
        } else if (code == 9) {
            cerr << "Сеть адресата административно изолирована.";
        } else if (code == 10) {
            cerr << "Узел адресата административно изолирован.";
        } else if (code == 11) {
            cerr << "Сеть недоступна для TOS.";
        } else if (code == 12) {
            cerr << "Узел недоступен для TOS.";
        } else if (code == 13) {
            cerr << "Связь административно запрещена фильтрацией.";
        } else if (code == 14) {
            cerr << "Нарушение приоритета узлов.";
        } else if (code == 15) {
            cerr << "Пренебрежение приоритетом узлов.";
        } else {
            cerr << "Ошибка. Код: " << code;
        }
    } else if (type == 4 && code == 0) {
        cerr << "Ошибка.\tПодавление отправителя.";
    } else if (type == 5) {
        cerr << "Ошибка: Перенаправление.\t";
        if (code == 0) {
            cerr << "Перенаправление для сети.";
        } else if (code == 1) {
            cerr << "Перенаправление на узел.";
        } else if (code == 2) {
            cerr << "Перенаправление на TOS и сеть.";
        } else if (code == 3) {
            cerr << "Перенаправление на TOS и узел.";
        } else {
            cerr << "Ошибка. Код: " << code;
        }
    } else if (type == 11) {
        cerr << "Ошибка: Время превышено.\t";
        if (code == 0) {
            cerr << "TTL в ходе транзита равен 0.";
        } else if (code == 1) {
            cerr << "TTL в ходе повторной сборки равен 0.";
        } else {
            cerr << "Ошибка. Код: " << code;
        }
    } else if (type == 12) {
        cerr << "Ошибка: Проблема параметра.\t";
        if (code == 0) {
            cerr << "Неверный заголовок IP.";
        } else if (code == 1) {
            cerr << "Отсутствует требуемый параметр.";
        } else {
            cerr << "Ошибка. Код: " << code;
        }
    } else {
        cerr << "Ошибка. Тип: " << type << ", код: " << code;
    }
}
