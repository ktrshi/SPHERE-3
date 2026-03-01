# Миграция на Geant4 Multi-Threading

**Дата:** 2026-02-06
**Статус:** Draft

## Цель

Перевести симуляцию SPHERE-3 G4 с однопоточного `G4RunManager` на многопоточный режим (`G4RunManagerFactory`) для:
- Ускорения симуляции (параллельная обработка ливней на всех ядрах CPU)
- Приведения архитектуры в соответствие с современными практиками Geant4 MT

## Текущее состояние

- `G4RunManager` (однопоточный)
- ~40 глобальных мутабельных переменных в `SPHERE-3.cpp` (ofstream, ifstream, счётчики, координаты)
- Action-классы зарегистрированы как синглтоны через `SetUserAction()` в `main()`
- Нет `G4VUserActionInitialization`, нет `EventAction`
- Нет синхронизации (`G4Mutex`, `G4AutoLock`, `atomic` — не используются)
- `/run/numberOfThreads 10` в макросе игнорируется

## Модель данных

- **Один входной файл = один ливень = одно Geant4 event** (множество первичных частиц)
- **Один выходной файл на событие:** `phels_to_trace_XXX` → `moshits_XXX`
- Счётчики: per-event (в выходной файл) + глобальные итоги (`G4Accumulable`)
- Число потоков задаётся через макрос `/run/numberOfThreads N`

## Архитектура

### Новые компоненты

#### FileQueue

Потокобезопасная очередь входных файлов. Создаётся в `main()`, заполняется из списка файлов. Каждый воркер атомарно извлекает следующий файл.

```cpp
class FileQueue {
    std::queue<std::string> fFiles;
    G4Mutex fMutex;
public:
    void Push(const std::string& path);
    bool Pop(std::string& path);  // false = очередь пуста
    size_t Size();
};
```

#### SimConfig

Read-only конфигурация, задаётся один раз в `main()` и передаётся как `const SimConfig*` во все Action-классы через ActionInitialization.

```cpp
struct SimConfig {
    G4double phi, the;   // углы поворота детектора
    G4double p1;         // экспонента чувствительности PMT
    G4double zz;         // z-уровень снега
    G4double xsh, ysh;   // сдвиг оси ШАЛ
    // прочие read-only параметры, ранее бывшие глобалами
};
```

#### WorkerEventData

Per-worker структура, заменяющая глобальные переменные. Создаётся в `ActionInitialization::Build()` для каждого воркера и передаётся по указателю в PrimaryGeneratorAction, EventAction и SteppingAction данного воркера.

```cpp
struct WorkerEventData {
    // Заполняется PrimaryGeneratorAction
    std::string inputFileSuffix;
    G4double xx{0}, yy{0}, t0{0};
    G4int origin{0};
    G4int phl_CloneNum{0}, phl_ii{0}, phl_jj{0}, phl_kk{0}, phl_mmm{0};

    // Управляется EventAction
    std::ofstream moshits;

    // Per-event счётчики (сбрасываются в BeginOfEventAction)
    G4int TotPhot{0};
    G4int NEntry{0};
    G4double tmin{1.8e6 * CLHEP::ns};
    G4double tmax{1.8e5 * CLHEP::ns};
};
```

#### sphmirrActionInitialization

Фабрика действий. Владеет `FileQueue*` и `const SimConfig*`.

```cpp
class sphmirrActionInitialization : public G4VUserActionInitialization {
    FileQueue* fFileQueue;
    const SimConfig* fConfig;
public:
    sphmirrActionInitialization(FileQueue* fq, const SimConfig* cfg);

    void Build() const override {
        auto* eventData = new WorkerEventData();
        SetUserAction(new sphmirrPrimaryGeneratorAction(fFileQueue, eventData, fConfig));
        SetUserAction(new sphmirrRunAction());
        SetUserAction(new sphmirrEventAction(eventData));
        SetUserAction(new sphmirrSteppingAction(eventData, fConfig));
    }

    void BuildForMaster() const override {
        SetUserAction(new sphmirrRunAction());
    }
};
```

#### sphmirrEventAction (новый класс)

Управляет жизненным циклом выходного файла и per-event счётчиков.

- `BeginOfEventAction()`: сбрасывает счётчики в `WorkerEventData`, открывает `moshits_<suffix>.dat`
- `EndOfEventAction()`: пишет per-event статистику в файл, закрывает поток, аккумулирует значения в `G4Accumulable` (через RunAction)

### Модифицируемые компоненты

#### sphmirrPrimaryGeneratorAction

- Конструктор принимает `FileQueue*`, `WorkerEventData*`, `const SimConfig*`
- `GeneratePrimaries()`: вызывает `fFileQueue->Pop(filename)`, открывает **локальный** `std::ifstream`, читает все фотоны, создаёт `G4PrimaryVertex` для каждого
- Парсит суффикс файла: `phels_to_trace_XXX` → `XXX`, сохраняет в `fEventData->inputFileSuffix`
- Записывает `xx`, `yy`, `t0`, `origin`, `phl_*` в `fEventData`
- Если `Pop()` вернул `false` — генерирует пустое событие (0 вершин)

#### sphmirrSteppingAction

- Конструктор принимает `WorkerEventData*`, `const SimConfig*`
- Все обращения к глобалам заменяются на `fEventData->...` и `fConfig->...`
- `moshits << ...` → `fEventData->moshits << ...`
- `TotPhot++` → `fEventData->TotPhot++`
- `tmin`/`tmax` → `fEventData->tmin` / `fEventData->tmax`
- `pixelCache` остаётся полем класса (per-worker, безопасно)

#### sphmirrRunAction

- Поля `G4Accumulable` с кастомными merge-режимами:
  - `fTotPhotTotal{0}` — `kSum`
  - `fNEntryTotal{0}` — `kSum`
  - `fTminAll{DBL_MAX}` — `kMinimum`
  - `fTmaxAll{0.0}` — `kMaximum`
- `BeginOfRunAction()`: `G4AccumulableManager::Instance()->Reset()`
- `EndOfRunAction()`: `G4AccumulableManager::Instance()->Merge()`, вывод итогов в `G4cout`

#### SPHERE-3.cpp (main)

```cpp
int main(int argc, char** argv) {
    auto* runManager = G4RunManagerFactory::CreateRunManager();

    runManager->SetUserInitialization(new sphmirrDetectorConstruction);
    runManager->SetUserInitialization(new sphmirrPhysicsList);

    // Конфигурация (read-only)
    auto* config = new SimConfig();
    // ... заполнение config из аргументов/файлов ...

    // Очередь файлов
    auto* fileQueue = new FileQueue();
    std::ifstream names("file_list.txt");
    std::string fname;
    while (std::getline(names, fname))
        fileQueue->Push(fname);
    names.close();

    runManager->SetUserInitialization(
        new sphmirrActionInitialization(fileQueue, config));

    runManager->Initialize();
    G4int nEvents = fileQueue->Size();
    UImanager->ApplyCommand("/run/beamOn " + std::to_string(nEvents));

    delete runManager;
    delete config;
    delete fileQueue;
}
```

## Карта файлов

### Новые файлы

| Файл | Назначение |
|------|-----------|
| `include/sphmirrActionInitialization.hh` + `src/...cc` | Фабрика действий |
| `include/sphmirrEventAction.hh` + `src/...cc` | Per-event файловый I/O и счётчики |
| `include/FileQueue.hh` + `src/FileQueue.cc` | Потокобезопасная очередь файлов |
| `include/WorkerEventData.hh` | Per-worker данные события |
| `include/SimConfig.hh` | Read-only конфигурация |

### Модифицируемые файлы

| Файл | Изменения |
|------|----------|
| `SPHERE-3.cpp` | RunManagerFactory, убрать глобалы, создать FileQueue + SimConfig, зарегистрировать ActionInitialization |
| `sphmirrPrimaryGeneratorAction.hh/.cc` | Новые аргументы конструктора, локальный ifstream, запись в WorkerEventData |
| `sphmirrSteppingAction.hh/.cc` | Новые аргументы конструктора, все глобалы → fEventData/fConfig |
| `sphmirrRunAction.hh/.cc` | G4Accumulable поля, Merge в EndOfRunAction |
| `CMakeLists.txt` | Добавить новые .cc (если не GLOB) |

### Удаляемые файлы

| Файл | Причина |
|------|---------|
| `sphmirrCounters.cc` / `.hh` | Заменены на G4Accumulable в RunAction |

### Неизменяемые файлы

- `sphmirrDetectorConstruction` — thread-safe by Geant4 design
- `sphmirrPhysicsList` — аналогично
- STL-геометрия, макросы (кроме добавления `/run/numberOfThreads`)

## Thread-safety гарантии

| Компонент | Механизм защиты |
|-----------|----------------|
| Входные файлы | `FileQueue` с `G4Mutex` — атомарный Pop |
| Выходные файлы | Per-worker `std::ofstream` в `WorkerEventData` — нет sharing |
| Per-event данные | Per-worker `WorkerEventData` — нет sharing |
| Глобальные счётчики | `G4Accumulable` с автоматическим Merge |
| Read-only конфиг | `const SimConfig*` — immutable после init |
| Детектор геометрия | Geant4 гарантирует thread-safety для `G4VUserDetectorConstruction` |
| pixelCache | Per-worker (поле SteppingAction) — нет sharing |
