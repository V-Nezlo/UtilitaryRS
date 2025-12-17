# UtilitaryRS — Device-oriented control protocol for embedded systems (C++17)

**UtilitaryRS** is a high-level, *device-centric* protocol and library for building control/telemetry networks where a **master (PC, embedded Linux, ESP32, STM32)** talks to multiple **devices (e.g., ESP32 or STM32 nodes)** — including **device discovery**, **named addressing**, and **file transfer for OTA**.

Originally built for **RS485**, it keeps RS485 support, but the design targets **larger systems** with queues, retries, and device state tracking via **DeviceHub**.

> Not Arduino-compatible (v2 uses **C++17**).

## Example Project

https://github.com/V-Nezlo/PiHydroWave

## What you get (v2.0)

- **Bus discovery**: auto-detect devices and read their **name** and **firmware version**
- **Work by device name** (not just numeric address/UID)
- **Commands with arguments**
- **Data requests** up to **255 bytes** from slaves
- **Reboot command** with magic numbers
- **File transfer** with intermediate CRC checks + final CRC (usable for **OTA**)
- **DeviceHub**: queues, automatic requests, retries, return codes handling
- **Device health/state** + flags with auto-request
- **Composite devices** (one physical device exposing multiple nodes)

## Multi-master / multi-slave notes

The protocol supports **multi-master** and **multi-slave**. If your transport has no built-in arbitration, **TX arbitration must be implemented on the client side** (token/ownership passing). Each master receives only frames addressed to it, with per-message verification.

## Quick usage flow

1. Create `DeviceHub` and `DeviceHubObserver`
2. Call `hub.probeAll()` to discover devices and register them
3. Use high-level methods like:
   - `sendCommand(deviceName, ...)`
   - `sendFile(deviceName, fileNumber, ...)`
   - periodic or one-shot data requests
4. Track results via `DeviceHubObserver` — you get status for each command/file/request

### DeviceHub lifecycle (high level)

- Hub sends **probe** → devices respond
- Hub registers devices and requests **firmware version**
- After version is known, device enters **operational mode**
- File sending is a 3-stage state machine: **request → chunk transfer → finalize**

## Constraints

- **One node = one UID = one name**
  Duplicate UID or duplicate device name is not allowed.
- Multi-master requires **TX line arbitration**.

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

**UtilitaryRS** — библиотека и протокол верхнего уровня для сетей управления/телеметрии, где **мастер (embedded Linux, STM32, ESP32)** работает с набором **устройств (devices), например ESP32-ноды**. Акцент на “системность”: **обнаружение устройств на шине**, работа **по имени**, очереди/повторы, коды возвратов и **передача файлов (в т.ч. OTA)** через тот же протокол.

Изначально проект создавался под **RS485**, поддержка RS485 сохранена, но архитектура ориентирована на **большие системы** и автоматизацию взаимодействия через **DeviceHub**.

> V2 **не поддерживает Arduino**: используется **C++17**.

## Пример проекта, использующего протокол

https://github.com/V-Nezlo/PiHydroWave

## Что есть в протоколе (v2.0)

- **Автодетект** устройств на шине + запрос **имени** и **версии ПО**
- Работа с устройствами **по имени** (а не только по UID/адресу)
- Отправка **команд с аргументами**
- **Запрос данных** размером до **255 байт** от слейвов
- Команда **перезагрузки** с “магическими числами”
- **Отправка файлов** с промежуточным контролем CRC и финальным CRC (подходит для **OTA**)
- **DeviceHub**: система очередей, авто-запросы, повторные отправки, обработка кодов возврата
- Система **состояний устройств (Health)** и **флаги** с автореквестом
- Поддержка **композитных устройств** (одно устройство может реализовывать несколько нод)

## Мульти-мастер / мульти-слейв

Протокол поддерживает **мульти-мастер / мульти-слейв**. Если на транспорте нет встроенного арбитража, **арбитраж TX** должен быть реализован на стороне клиента (например, через передачу владения/токен). При этом каждый мастер получает только сообщения, адресованные именно ему, с проверкой каждого сообщения.

## Типовой сценарий использования

1. Создаём `DeviceHub` и `DeviceHubObserver`
2. Вызываем `hub.probeAll()` — хаб ищет устройства на шине и регистрирует их
3. Дальше можно вызывать высокоуровневые функции:
   - `sendCommand(имя_устройства, ...)`
   - `sendFile(имя_устройства, номер_файла, ...)`
   - разовые или периодические запросы данных
4. `DeviceHubObserver` возвращает статус каждой операции (команда/файл/запрос)

### Логика работы DeviceHub (в общих чертах)

- Хаб отправляет **probe** → устройства отвечают
- Хаб регистрирует устройства и запрашивает **версию ПО**
- После получения версии устройство переводится в **рабочий режим**
- Отправка файлов — 3 состояния: **запрос → отправка чанков → финализация**

## Ограничения

- **Одна нода = один UID = одно имя**
  Несколько нод с одинаковым UID или одинаковым именем — недопустимо.
- Для мультимастера нужен **арбитраж TX линии**.

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
