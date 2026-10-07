# UtilitaryRS — Device-oriented control protocol for embedded systems (C++17)

**UtilitaryRS** is a high-level, *device-centric* protocol and library for building control/telemetry networks where a **master (PC, embedded Linux, ESP32, STM32)** talks to multiple **devices (e.g., ESP32 or STM32 nodes)** — including **device discovery**, **addressing by deviceId**, and **file transfer for OTA**.

Originally built for **RS485**, it keeps RS485 support, but the design targets **larger systems** with queues, retries, and device state tracking via **DeviceHub**.

> Not Arduino-compatible (v2 uses **C++17**).

## Example Project

https://github.com/V-Nezlo/PiHydroWave

## What you get (v2.0)

- **Bus discovery**: auto-detect devices and read their **permanent UID** and **firmware version**
- **Work by deviceId** (the current numeric bus address, 1–32)
- **Commands with arguments**
- **Data requests** up to **255 bytes** from slaves
- **Reboot command** with magic numbers
- **File transfer** with intermediate CRC checks + final CRC (usable for **OTA**)
- **DeviceHub**: queues, automatic requests, retries, return codes handling
- **Device health/state** + flags with auto-request
- **Composite devices** (one physical device exposing multiple nodes)

## Master / slave operation

The protocol uses **one master**, at address 0. Nodes transmit only in response to master requests.
Discovery uses hash groups to separate responses on shared transports such as RS485.

## Quick usage flow

1. Create `DeviceHub(version, masterUid, interface)` and `DeviceHubObserver`
2. Keep calling `hub.process(...)`: startup scans addresses 1–32, then discovers and allocates new nodes; `hub.state()` reports the current state
3. Use high-level methods like:
   - `sendCmdToDevice(deviceId, ...)`
   - `sendFile(deviceId, fileNumber, ...)`
   - periodic or one-shot data requests
4. Track results via `DeviceHubObserver` — each event includes the permanent `NodeUid` and current `deviceId`, alongside the status or data

### DeviceHub lifecycle (high level)

- Hub sequentially requests device information at addresses **1–32**, waiting for a response or a 200 ms timeout before the next address
- Hub registers responding devices using their **permanent UID** and **firmware version**, preserving their assigned deviceId
- After version is known, device enters **operational mode**
- File sending is a 3-stage state machine: **request → chunk transfer → finalize**

Outbound commands, requests, schedules and file transfers accept only `deviceId`; no UID is required. The wire-format device name remains in `DeviceInfo`, but DeviceHub does not store it.

### Node allocation API

Pass a permanent `std::array<uint8_t, 16>` UID to the node handler:

```cpp
RS::NodeUid uid{/* 16 bytes supplied by the application */};
RS::RsHandler<Interface, Crc8, 100> node("device", version, uid, interface);
```

The handler starts without an address and automatically answers matching discovery requests,
accepts assignments for its UID, and confirms repeated assignments of the same transaction.
The UID is copied; addresses and allocation state are held only in RAM.

On its first `hub.process(...)` call, the master scans addresses 1–32,
then discovers unallocated nodes.
Allocated nodes retain their addresses. Their information responses include permanent UIDs,
allowing the master to rebuild its address table directly from the bus.
Startup information requests reset exchange state on the addressed node before it replies.
There is no broadcast reset command.
No persistent allocation storage or restore calls are needed.

The node clears its allocation transaction when it receives a startup information request, preserving its UID and address.
Override `handleExchangeReset()` if the application owns additional exchange state, such as a file writer.

Keep calling `hub.process(...)`: discovery of unallocated nodes runs every 10 seconds.
Background passes query all 32 hash groups with a new nonce; allocated nodes do not answer.
A rebooted slave with the same UID receives its previous address from the current master's RAM table.
Passes do not overlap, and file transfers defer background discovery.
`hub.allocateNodes()` explicitly scans and allocates, defaulting to four discovery passes.
`hub.probeAll()` scans assigned addresses only.
The hub state machine is `Starting → Waiting → Scanning → Allocating → Running`.
`Waiting` drains pending exchanges before scanning or allocation.
Within `Allocating`, discovery queries hash groups, then the device scheduler services each node's
`Assigning → InfoRequest → Running` states. Pending requests and retry counts belong to each device.
Devices are visited in round-robin order, with at most one outstanding bus request; waiting never blocks `process(...)`.
Normal commands, telemetry and health requests run only in `Running`.
Discovery windows last 200 ms; assignments and information requests get up to three attempts.
Reservations are kept for the lifetime of the master, including assignments without confirmation.
The master also requires its own permanent UID; its bus address is always 0.

## Constraints

- Maximum **32 device nodes**, at addresses **1–32**.
- **One node = one permanent UID = one current deviceId**
  Permanent UIDs and assigned addresses must be unique. Device names may repeat; DeviceHub does not use them.
- Nodes send only responses to the single master.

## Roadmap ideas

- Consider replacing pre-programmed unique UIDs with **hash-based identities** (similar in spirit to UAVCAN).

---

## Build

```sh
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

Tests
```sh
ctest
```

Docs
```sh
doxygen Doxyfile
```


## RU

# UtilitaryRS — мультипротокол для систем управления (C++17)

**UtilitaryRS** — библиотека и протокол верхнего уровня для сетей управления/телеметрии, где **мастер (embedded Linux, STM32, ESP32)** работает с набором **устройств (devices), например ESP32-ноды**. Акцент на “системность”: **обнаружение устройств на шине**, работа **по deviceId**, очереди/повторы, коды возвратов и **передача файлов (в т.ч. OTA)** через тот же протокол.

Изначально проект создавался под **RS485**, поддержка RS485 сохранена, но архитектура ориентирована на **большие системы** и автоматизацию взаимодействия через **DeviceHub**.

> V2 **не поддерживает Arduino**: используется **C++17**.

## Пример проекта, использующего протокол

https://github.com/V-Nezlo/PiHydroWave

## Что есть в протоколе (v2.0)

- **Автодетект** устройств на шине + запрос **постоянного UID** и **версии ПО**
- Работа с устройствами **по deviceId** — текущему числовому адресу на шине (1–32)
- Отправка **команд с аргументами**
- **Запрос данных** размером до **255 байт** от слейвов
- Команда **перезагрузки** с “магическими числами”
- **Отправка файлов** с промежуточным контролем CRC и финальным CRC (подходит для **OTA**)
- **DeviceHub**: система очередей, авто-запросы, повторные отправки, обработка кодов возврата
- Система **состояний устройств (Health)** и **флаги** с автореквестом
- Поддержка **композитных устройств** (одно устройство может реализовывать несколько нод)

## Работа мастера и слейвов

Протокол использует **одного мастера**, его адрес — 0. Ноды передают данные только в ответ на запросы мастера.
При обнаружении ответы распределяются по хеш-группам для общей шины, включая RS485.

## Типовой сценарий использования

1. Создаём `DeviceHub` и `DeviceHubObserver`
2. Регулярно вызываем `hub.process(...)` — при запуске хаб сканирует адреса 1–32, затем обнаруживает и аллоцирует новые ноды; `hub.state()` возвращает текущее состояние
3. Дальше можно вызывать высокоуровневые функции:
   - `sendCmdToDevice(deviceId, ...)`
   - `sendFile(deviceId, номер_файла, ...)`
   - разовые или периодические запросы данных
4. `DeviceHubObserver` передаёт постоянный `NodeUid` и текущий `deviceId` вместе со статусом операции или полученными данными

### Логика работы DeviceHub (в общих чертах)

- Хаб последовательно запрашивает информацию по адресам **1–32**, ожидая ответ или таймаут 200 мс перед следующим адресом
- Хаб регистрирует ответившие устройства по **постоянному UID** и **версии ПО**, сохраняя выданные deviceId
- После получения версии устройство переводится в **рабочий режим**
- Отправка файлов — 3 состояния: **запрос → отправка чанков → финализация**

Команды, запросы, расписания и передача файлов принимают только `deviceId`; UID для отправки не нужен. Имя остаётся в формате пакета `DeviceInfo`, но DeviceHub его не хранит.

### API аллокации ноды

Передайте постоянный UID типа `std::array<uint8_t, 16>` в конструктор обработчика ноды:

```cpp
RS::NodeUid uid{/* 16 байт, подготовленных приложением */};
RS::RsHandler<Interface, Crc8, 100> node("device", version, uid, interface);
```

Обработчик начинает работу без адреса, сам отвечает на запросы своей хеш-группы,
принимает назначение для своего UID и подтверждает повторы той же транзакции.
UID копируется; адреса и состояние аллокации хранятся только в RAM.

При первом вызове `hub.process(...)` мастер сканирует адреса 1–32,
затем обнаруживает неаллоцированные ноды.
Аллоцированные ноды сохраняют адреса. Ответы с информацией содержат постоянные UID,
поэтому мастер восстанавливает таблицу адресов непосредственно опросом шины.
Запрос начального сканирования сбрасывает состояние обмена адресованного слейва перед ответом.
Широковещательной команды сброса нет.
Постоянное хранение назначений и вызовы восстановления не требуются.

При получении запроса начального сканирования слейв очищает транзакцию аллокации, сохраняя UID и адрес.
Если приложение владеет дополнительным состоянием обмена, например записью файла,
его сброс выполняется в уведомлении `handleExchangeReset()`.

При регулярных вызовах `hub.process(...)` обнаружение неаллоцированных нод запускается раз в 10 секунд.
Фоновый проход опрашивает все 32 хеш-группы с новым nonce; аллоцированные ноды молчат.
Перезапущенный слейв с тем же UID получает прежний адрес из RAM-таблицы работающего мастера.
Проходы не перекрываются; при передаче файла фоновый запуск откладывается.
`hub.allocateNodes()` явно запускает сканирование и аллокацию, по умолчанию с четырьмя проходами обнаружения.
`hub.probeAll()` сканирует только выданные адреса.
Автомат хаба: `Starting → Waiting → Scanning → Allocating → Running`.
В `Waiting` завершаются уже отправленные запросы перед сканированием или аллокацией.
Внутри `Allocating` обнаружение опрашивает хеш-группы, затем планировщик обслуживает
состояния каждой ноды `Assigning → InfoRequest → Running`.
Ожидаемый ответ и счетчики повторов хранятся в каждой записи устройства.
Устройства обходятся по кругу; на шине одновременно ожидается ответ только на один запрос.
Ожидание не блокирует вызов `process(...)`.
Обычные команды, телеметрия и запросы здоровья выполняются только в `Running`.
Окно обнаружения длится 200 мс; назначение и запрос информации имеют до трех попыток.
Резервирования сохраняются на время жизни мастера, включая назначения без подтверждения.
Мастеру также нужен собственный постоянный UID; его адрес на шине всегда 0.

## Ограничения

- Максимум **32 ноды устройств**, адреса **1–32**.
- **Одна нода = один постоянный UID = один текущий deviceId**
  Постоянные UID и выданные адреса должны быть уникальны. Имена могут повторяться: DeviceHub их не использует.
- Ноды отвечают только на запросы единственного мастера.

## Возможные расширения

- Возможен отказ от уникальных “зашитых” UID в пользу **хешей** (по аналогии с идеями UAVCAN).

Сборка проекта:
```sh
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ../
make
```

Запуск тестов - из каталога build:
```sh
ctest
```

Запуск сборки документации:
```sh
doxygen Doxyfile
```
