#include <algorithm>
#include <combaseapi.h>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <thread>
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
#pragma pack(pop)

/// Заголовок ICMP
#pragma pack(push, 1)
struct icmpHeader
{
    unsigned char type;
    unsigned char code;
    unsigned short checkSum;
};
#pragma pack(pop)

/// ICMP-пакет
#pragma pack(push, 1)
struct icmpPacket
{
    icmpHeader header;
    GUID data;
};
#pragma pack(pop)

/// Структура с дополнительными данными о пакете
struct packData
{
    GUID recvedGuid;
    time_point<high_resolution_clock> sendTime;
    time_point<high_resolution_clock> recvTime;
};

/// Подключение сокета
optional<sockaddr_in> connectAddr();

/// Получение количества шагов
int getSteps();

/// Выполнение Ping
unsigned long long ping(sockaddr_in destAddr, int steps = 4);

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

    // Вектор успеха или неудач при получени
    vector<bool> success;

    // Последний обработанный GUID
    GUID lastGuid{};

    // Цикл отправки и приема пакетов
    for (int i = 0; i < steps;) {
        // Формирование флагов для определения,
        // что итерацию цикла требуется завершить
        bool created = false;
        bool sended = false;
        bool sendError = false;
        bool recved = false;
        bool recvError = false;
        bool timeoutEnded = false;

        GUID origGuid;
        if (!(CoCreateGuid(&origGuid) == S_OK)) {
            cerr << "Ошибка генерации GUID";
            closesocket(sock);
            return 1;
        } else {
            packData data{};
            guids[origGuid] = data;
            created = true;
        }

        // Формирование пакета на отправку
        icmpPacket sendPack;
        sendPack.header.type = 8;
        sendPack.header.code = 0;
        sendPack.header.checkSum = 0;
        sendPack.data = origGuid;
        sendPack.header.checkSum = calculateChecksum((unsigned short *) &sendPack, sizeof(sendPack));

        high_resolution_clock::duration iterationSendDiff;

        if (i != 0) {
            iterationSendDiff = high_resolution_clock::now() - guids[lastGuid].sendTime;

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
            sendError = true;
        } else {
            cout << "Пакет отправлен." << endl;
            sended = true;
        }

        guids[origGuid].sendTime = high_resolution_clock::now();

        // Минимальная длина IP-заголовка
        int ipHeaderMinLen = sizeof(ipHeader);

        // Установка размера буфера: IPv4-заголовок + ICMP-пакет
        const int bufferSize = ipHeaderMinLen + sizeof(icmpPacket);

        // Создание буфера
        char *recvBuffer = new char[bufferSize];

        // Длина адреса
        int addrLen = sizeof(destAddr);

        // Флаг нахождения данных
        bool found = false;

        // Время начала приема данных
        time_point<high_resolution_clock> recvWaitStart = high_resolution_clock::now();

        // Попытка получения данных до тех пор, пока не будет найден отправленный пакет
        while (!found) {
            recved = false;
            recvError = false;
            timeoutEnded = false;

            // Разница между началом получения и текущей попыткой
            auto recvWaitDiff = high_resolution_clock::now() - recvWaitStart;
            if (recvWaitDiff > 3s) {
                cerr << "Истек таймаут ожидаения пакета." << endl;
                success.push_back(false);
                timeoutEnded = true;
                break;
            }

            // Структура fd_set для хранения сокетов
            fd_set fdSet;
            FD_ZERO(&fdSet);      // Очистка
            FD_SET(sock, &fdSet); // Добавление сокета в набор

            // Таймаут функции select
            timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000;

            // select
            int selectRes = select(0, &fdSet, NULL, NULL, &timeout);

            int bytesRecved = 0;

            sockaddr_in fromAddr{};

            // Ошибка select
            if (selectRes <= 0) {
                recvError = true;
                continue;
            }

            // Сокет sock содержится в fdSet
            if (FD_ISSET(sock, &fdSet)) {
                bytesRecved = recvfrom(sock,       // сокет
                                       recvBuffer, // указатель на буфер для приема данных
                                       bufferSize, // размер буфера
                                       0,          // флаги
                                       (SOCKADDR *) &fromAddr, // указатель на адрес источника
                                       &addrLen); // указатель на длину структуры адреса источника
            }

            // Проверка на то, что ответ пришел с целевого узла
            if (fromAddr.sin_addr.s_addr != destAddr.sin_addr.s_addr) {
                continue;
            }

            // Проверка что ответ содержит данные
            if (bytesRecved <= 0) {
                continue;
            }

            ipHeader *ipHdr = (ipHeader *) recvBuffer;
            int ipHeaderLen = ipHdr->len * 4;
            int icmpHeaderMinLen = sizeof(icmpHeader);

            if (bytesRecved < ipHeaderLen + icmpHeaderMinLen) {
                continue;
            }

            icmpPacket *recvPack = (icmpPacket *) (recvBuffer + ipHeaderLen);

            // Типы кроме 0 пропускаются
            if (recvPack->header.type != 0) {
                // Вывод ошибок
                errors(recvPack->header.type, recvPack->header.code);
                recvError = true;
                continue;
            }

            GUID recvGuid = recvPack->data;

            // Итератор по map guids по значению полученного Guid
            auto it = guids.find(recvGuid);

            // Чужой пакет
            if (it == guids.end()) {
                recvError = true;
                continue;
            }

            it->second.recvTime = high_resolution_clock::now();

            duration<double, milli> diff = it->second.recvTime - it->second.sendTime;

            int ttl = ipHdr->ttl;

            cout << "Успешное получение (" << diff.count() << " мс, TTL = " << ttl << ")." << endl;

            success.push_back(true);
            timeDiff.push_back(diff.count());

            recved = true;

            found = true;
        }

        // Очистка памяти
        delete[] recvBuffer;

        if ((created && sended && (recved || recvError || timeoutEnded)) || (created && sendError)) {
            i++;
            lastGuid = origGuid;
        }
    }

    // Вывод статистики
    cout << endl << "Статистика" << endl << endl;

    if (success.size() == (unsigned long long) steps) {
        int cnt = success.size();
        int trueCnt = count(success.begin(), success.end(), true);
        int falseCnt = count(success.begin(), success.end(), false);

        double falsePercent = ((double) falseCnt / (double) success.size()) * 100.0;

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
