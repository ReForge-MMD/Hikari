# Hikari — Спецификация фазы 2: независимая DX11-платформа для MMD

> Статус: **SPEC-проект v1.0** (на реализацию другим агентом)
> Дата: 2026-10-27
> Ветка проекта: `E:\Projects\Github\MMD\Hikari`
> Входные факты: `hikari.dll` (текущий "plugin-режим") работает до степени
> «MME-эффект компилится в SM5, модельный проход рисуется в DX11, кадр
> вклеивается в нативный backbuffer». Новая фаза — **отвязка от
> MMDPlugin/MMHack/MMEffect** и переход на два собственных модуля.
> Язык реализации: Rust (workspace `Cargo.toml` уже настроен, зависимости
> `windows = "0.62"`).

---

## 1. Цель и рамка

Построить **независимую DX11-платформу рендера для MMD** из двух модулей:

| Модуль | Файл в папке MMD | Роль |
|---|---|---|
| **d3d9.dll** | `<MMD>\d3d9.dll` | прокси-загрузчик реального D3D9 + перехват draw/state остента finals space рен-поток для MMD |
| **HikariEffects.dll** | `<MMD>\HikariEffects.dll` | собственная effects-система вместо MME: эффект-движок, окно назначения, кнопка в топбаре |

**Укладка — корень программы, НЕ plugin-папка:**
```
<MMD>\
  MikuMikuDance.exe
  d3d9.dll                  ← прокси (Windows грузит его вместо системного)
  HikariEffects.dll         ← effects-система (загружается d3d9.dll при init)
  hikari_core.dll           ← общий DX11-слой (линкуются оба выше)
  hikari_assignments.json   ← назначения (создаётся/читается effects)
  hikari.ini                ← опции (лог, композит)
  hikari.log                ← лог всех трёх модулей
```
Файлы вообще не зависят от `plugin\` — MMD ничего о них не знает,
кроме автоматической загрузки `d3d9.dll`.

**Главные речения:
- Обработчик DX11 (существующий код в `src/d3d11.rs`, `src/effect.rs`,
  `src/shader.rs`, `src/reflection.rs`, `src/states.rs`, `src/targets.rs`)
  сохраняется как ядро — переиспользуется как библиотека между модулями.
- MMD продолжает думать, что рисует через D3D9.
- Никакой зависимости от MMDPlugin.dll / MMHack.dll / MMEffect.dll / d3dx9_43.dll-шима.

**Не-цели (out of scope):**
- Полная эмуляция IDirect3DDevice9 с фиксированным конвейером (fixed-function
  pipeline полностью не нужна — реальный D3D9 продолжает рисовать нативно,
  перехватывается только поток draw/state-вызовов).
- DX9→DX11 байткод-транслятор (эффекты пишутся на нашем собственного
  формата DX11/HLSL,profiles: vs_5_0..cs_5_0).
- Эквивалент MMD-встроенных шейдеров (resource 117/118) в фазе 1 — MMD
  продолжает рисовать модель нативным D3D9-рендером, если не назначен
  эффект (см. §7 про «двойной рендер»).

---

## 2. Диагностика текущего режима (для контекста)

Plugin-режим (`plugin/Hikari/hikari.dll` + `mmdplugin.config.json`) даёт нам
только безболевой доступ к hook-точкам, но тянет за собой:
- толстую зависимость от MMDPlugin-диспатчера (vtable-слоты v[17]..v[249]);
- interplay с MMHack/MMEffect (benign SEH-исключения, стей-тесты на
  GetPassDesc — исторический крэш `MSVCR90.dll+0x1E48E` — MMD-движок;
  направляется внутренним bone-loop'ом);
- конфликт с d3d9.dll-шимом MMDX11-типов (мы его не контролируем).

Новый режим убирает все три зависимости и держит контракт:
**MMD ↔ реальный D3D9; Hikari вызывает фильтр на элементарных API и решает,
что рисовать нативно, а что — через DX11.**

---

## 3. Компонент 1 — `d3d9.dll` (прокси-загрузчик)

### 3.1 Механика загрузки

Windows при запуске MMD.exe ищет `d3d9.dll` **сначала в папке приложения**.
Мы кладём туда свой файл — он видит `Direct3DCreate9`, создаёт реальный
D3D9 (`LoadLibraryW("System32\d3d9.dll")` → его `Direct3DCreate9`) и ставит
перехват на интересующие уровни.

```
MMD.exe ── LoadLibrary("d3d9.dll")
             └── наш d3d9.dll
                   ├── LoadLibrary(System32\d3d9.dll)      ← реальный
                   ├── создаёт IDirect3D9/Device-обёртки    ← наша обёртка
                   └── пересылает draw/state в DX11-ядро    ← hikari-core
```

### 3.2 Экспорт

- `Direct3DCreate9(SDKVersion)` → `IDirect3D9*` (обёртка).
- (Опционально) `Direct3DCreate9Ex`.
- (Опционально) `D3DPERF_*` и `DebugSetLevel` — заглушки-форварды.

### 3.3 Перехват-уровень (минимальная поверхность)

Прокси-обёртки только для данных методов `IDirect3DDevice9`
(остальные — чистый форвард к реальному девайсу):

| Метод (D3D9 vtable idx) | Поведение обёртки |
|---|---|
| `BeginScene` (41) | форвард; при необходимости init DX11-контекста |
| `EndScene` (42) | форвард; после — наш композит-пасс (если активен эффект) |
| `Clear` (43) | форвард; параллельная очистка DX11-модель-цели |
| `SetStreamSource` (100) | форвард + фиксация `VB addr, stride, offset` |
| `SetIndices` (104) | форвард + фиксация `IB addr` |
| `DrawIndexedPrimitive` (82) | если назначен Hikari-эффект: рисуем в DX11 (наш
  SM5-проход) и ****не форвардим** (запрещаем дублирующий нативный draw);
  иначе — чистый форвард |
| `DrawPrimitive` (81) | аналогично DrawIndexedPrimitive |
| `DrawPrimitiveUP` (84) | форвард; используется для пост-квадов (`Draw=Buffer`) |
| `SetTexture` (113) | форвард + фиксация для SRV-аплоада |
| `SetRenderTarget` (22) | форвард + фиксация target-адреса |
| `SetViewport` (48) | форвард + маппинг в DX11 viewport |
| `Present` (17) | форвард (MMD сам себе рисует окно) |

> Обёртка реализуется через **виртуальную vtable-патчинг** на конкретные
> указатели слотов реального девайса (как в `src/hook.rs` — техника уже
> отработана на IAT) или через полный COM-прокси интерфейс.

### 3.4 JSON-конфиг (опционально)

`hikari.ini` рядом с d3d9.dll: `log_level`, `composite_mode`,
`force_dx11_draw`, etc. Минимально — не блокирует фазу 1.

---

## 4. Компонент 2 — `HikariEffects.dll` (effects-система)

### 4.1 Роль

Полноценный аналог MMEffect.dll поверх нашего DX11-ядра:
- загрузка .fx/hkfx файлов эффектов (см. §5);
- assignment-model (какой эффект к какой модели/материалу);
- окно UI (аналог «Map Effect File»);
- кнопка в топбаре MMD.

### 4.2 Загрузка HikariEffects.dll

**Никаких plugin-папок.** `HikariEffects.dll` лежит в корне рядом с
d3d9.dll и загружается proxy'ем при первой инициализации девайса:

```rust
// внутри d3d9.dll (наш прокси), при CreateDevice:
let _effects = LoadLibraryW(w!("HikariEffects.dll")); // корень MMD
```
Сценарий:
1. Windows грузит нашу `d3d9.dll` (LdrLoadDll по папке приложения).
2. Наша d3d9.dll резолвит реальный System32 d3d9 и ждёт CreateDevice.
3. При CreateDevice: init `hikari_core.dll`, `LoadLibraryW(HikariEffects.dll)`
   (он находит core уже загруженным) → effects подписывается и создаёт UI.

### 4.3 Интеграция в окно MMD

- Найти HWND главного окна MMD (FindWindow по классу/заголовку).
- В topbar (только главное окно) встроить кнопку через
  `CreateWindowExW(BS_OWNERDRAW)`-child поверх существующей тулбар-полосы,
  либо (проще и стабильнее) — добавление пункта меню через
  `GetMenu`/`InsertMenuItem` + подкласс `WndProc` (SetWindowLongPtrW) —
  MMD уже грузит нас из d3d9.dll, так что окно гарантируемо существует к
  моменту первого BeginScene.
- Собственное окно эффектов: `CreateWindowExW`-child в клиенте MMD,
  рамка-флоут с таблицей (как MME),remember position в ini.

### 4.4 Модель назначений

```
Assignment {
  object_id   : ModelID | AccessoryID | Stage,
  material_i  : Option<u32>,   // None = весь объект
  effect_path : PathBuf,
  technique   : Option<String>, // None = первый
}
```
Хранится в `hikari_assignments.json` в папке MMD; подгружается/сохраняется.

### 4.5 Граница с d3d9.dll

d3d9.dll и HikariEffects.dll живут в одном процессе. Канал связи —
экспортируемое C-API вместо COM (осипFor binary compat):

```c
// hikari_core.dll (общий DX11-слой) — экспорт HIKARI_CORE_API
HRESULT HikariCore_Init(HMODULE device_module);      // d3d9.dll передаёт себя
HRESULT HikariCore_Shutdown();
HRESULT HikariCore_BeginEffectPass(...);             // на draw-хукe
HRESULT HikariCore_CommitConstants(...);
HRESULT HikariCore_CaptureNativeTexture(...);
// + регистрация windows/UI-объектов для рисования overlay'ом
```
(точные сигнатуры — на этапе P2; до этого — прямая линковка через общий
cargo workspace: `hikari-core` = lib crate, `d3d9` и `HikariEffects` —
thin DLL bins, оба линкую `hikari-core`.)

### 4.6 Многозианная сборка (cargo)

```
[workspace]
members = ["core", "d3d9", "effects"]
core    -> target/…/hikari_core.dll    (cdylib)
d3d9    -> target/…/d3d9.dll           (cdylib, линкует core)
effects -> target/…/HikariEffects.dll  (cdylib, линкует core)
```

---

## 5. Формат эффектов

### 5.1 Файл — обычный HLSL

`.hx` файл — стандартный HLSL SM 5.0 (тот же синтаксис, что уже
поддержан `parse_techniques`), расширенный аннотациями:

```hlsl
technique Main <
    string HikariPass = "object";       // object | shadow | edge | stage
> {
    pass P0 {
        VertexShader = compile vs_5_0 VS_Main();
        PixelShader  = compile ps_5_0 PS_Main();
        // blend/depth/raster задаются в HLSL-блоках состояния (см. P3)
    }
}
```

Ограничение первой фазы: без MME-script (`RenderColorTarget`,
`LoopByCount`, `ScriptExternal`) — это фаза P5 (см. дорожную карту).

### 5.2 Стандартные семантики (совместимы с текущей реализацией)

`WORLD`, `VIEW`, `PROJECTION`, `WORLDVIEWPROJECTION` (и transpose-варианты),
`LIGHT_DIRECTION`, `LIGHT_DIFFUSE/AMBIENT/SPECULAR`, `CAMERA_POSITION`,
`MATERIALTEXTURE`, `TIME`, `MATERIALDIFFUSE/AMBIENT/SPECULAR/EMISSIVE` —
наполняются из D3D9-состояния на draw-хукe (d3d9.dll фиксирует состояние;
HikariEffects/hikari-core строит CB).

---

## 6. Двойной рендер и его подавление

Подход этапа 1 (уже проверен): DX11 рисует модель-слой в прозрачный
off-screen target, слои композитятся в нативный backbuffer последним
EndScene-пассом. Известный подводный камень: модуль MMD (bone-loop) может
быть не подготовлен к чужому state — см. историю `MSVCR90.dll+0x1E48E`.

СтратегияSafety в новой архитектуре: **строгий перехват всех копируемых
draw'ов модели** (см. §3.3): когда назначен эффект — нативный draw **не
форвардится**, double-render не возникает, bone-loop MMD не активируется
принудительно (мы не трогаем их движок, они не трогают наш DX11).

Если при назначении эффекта MMD всё же выполняет свой bone-loop (copy
матриц для стандартного эффекта) — это уже вопрос порядка: назначение
эффекта должно ЗАМЕЩАТЬ нативный материал-рендер полностью, для чего
эксперименты из expA/expB показывают точные барьеры. Этот вопрос —
первый пункт P3 (см. §8).

---

## 7. Компонент DX11-ядро (hikari-core) — что уже есть, что добавить

**Уже реализовано (переиспользуем как есть):**
- `d3d11.rs`: контекст D3D11 (device/context/RT/Depth), states, dynamic
  VB/IB, input-layout кэш (PMX-layouts: stride 52/44/38/32/20),
  D3D9-texture→SRV upload (CPU-копия), composite chain (model target +
  canvas + stage).
- `effect.rs`: прокси-ID3DXEffect-поверхность (MME-совместимая) — в новом
  режиме переиспользуется как **HikariEffect** (наш internal class);
  GetPassDesc возвращает **реальный DXBC** (device наш — Create*Shader
  принимает DXBC напрямую).
- `shader.rs`: compile_hlsl (SM5 и legacy), reflection через D3DReflect —
  offsets/структуры CB, списки текстур/сэмплеров.
- `states.rs`, `targets.rs`: blend/depth/raster/mrt.
- `hook.rs`: IAT-перехват (для уровня d3d9.dll — будет переориентирован).

**Добавить в core (P-этапы):**
- `core/backend.rs`: IDirect3D9/IDirect3DDevice9 COM-обёртки поверх
  реального девайса (перехват-поверхность §3.3) + state-трекер для
  заполнения CB.
- `core/effects.rs`: загрузка .hx → HikariEffect (без MME-совместимости,
  чистый внутренний API), assignment store.
- `core/ui.rs`: окно/кнопка/меню (§4.2) — может быть вынесено в
  HikariEffects.dll.

---

## 8. Дорожная карта (P0..P6) — acceptance criteria

**P0 — каркас workspace** (обязателен первым):
- workspace `core/d3d9/effects`, старый plugin-бин остаётся для
  regression-сравнения; все существующие тесты (`cargo test`) зеленые.
- Deploy: `deploy.ps1` копирует **в корень MMD**: `d3d9.dll`,
  `hikari_core.dll`, `HikariEffects.dll` (+ шаблон `hikari.ini`).

**P1 — d3d9.dll прокси без перехвата** (MMD работает как раньше):
- `Direct3DCreate9` → реальный d3d9, Все методы — чистый форвард.
- **AC**: MMD запускается, рендерит нативно, ничего не падает; лог
  `hikari.log` показывает `[Hikari][backend] device created: caps…`.

**P2 — state-перехват** (draw hooks, VB/IB/текстуры):
- Все слоты §3.3 патчатся; состояние пишется в TrackState.
- **AC**: каталог-тест: лог фиксирует модельный draw (VB/IB/stride/текстур),
  отчёт синхронен с expA/expB признаками; MMD стабилен 10+ минут.

**P3 — эффект назначен → DX11-проход** (главный milestone):
- HikariEffects.exe: кнопка + окно (можно оверлей), загрузка `test_model.fx`
  (существующий тестовый эффект) → ассигн на модель → draw перехвачен →
  DX11 SM5 перехват + композит в backbuffer (существующий композит).
- **AC**: модель с эффектом (красный силуэт test_model.fx) поверх живой
  сцены; нативный дубль отсутствует; ни вылета в течение 10 минут.

**P4 — state→CB / полное покрытие семантик**:
- Все стандартные семантики §5.2 заполняются из device state.
- **AC**: эффект с материал-зависимым цветом (DIFFUSE) даёт правильный
  цвет; матрица движется с моделью/камерой.

**P5 — overlay/UI-полировка**:
- Окно эффектов: список моделей/материалов, назначение/чизRemoving,
  сохранение ассигнмент-JSON, кнопка в меню/topbar.
- **AC**: полный UX-цикл без ручного редактирования json.

**P6 — постэффекты и RT-граф**:
- `RenderColorTarget/DepthStencil`-аннотации, `Draw=Buffer` через
  `DrawPrimitiveUP`-перехват (уже отработан), MRT/UAV-путь (уже есть в
  `targets.rs`).
- **AC**: однофайловый пост-эффект (SSAO-подобный) с RT-блумом работает.

---

## 9. Риски и меры

| Риск | Мера |
|---|---|
| MMPlugin/MMHack-экзeмпляры не удалены пользователем → конфликт | документ: удалять `plugin\mme`, `plugin\MMDPlugin`; d3d9/d3dx9_43-шимы не требуются |
| Реальный D3D9 против ожиданий MMD (caps/формат) | P1 — чистый форвард перед перехватом; caps-репорт не меняем |
| Двойной рендер → крэш bone-loop `MSVCR90+0x1E48E` | §6: подавление нативного draw на назначенных материалах; эксперимент expA/B уже локализовал барьер |
| Init-порядок: девайс существует до эффекта | P2 фиксация state до P3 эффекта |
| Ресурс 117/118 (MMD's built-in shaders) | не трогаем: MMD рисует и нативно; зам Brigham — отдельная фаза (см. не-цели) |
| Пиковая производительность CPU-copy текстур | совместимо с текущим экспериентированным path'ом; позже — keyed-mutex shared textures |

---

## 10. Быстрый старт для агента

1. `git clone/check` ветки `E:\Projects\Github\MMD\Hikari` (текущая master).
2. Прочитать `src/effect.rs`, `src/d3d11.rs`, `src/shader.rs` — ядро уже
   реализовано; новый код оборачивает его, не дублируя.
3. `cargo test` — обязателен pass до начала правок (baseline).
4. Вводить новые модули по §4.5; deploy через §8 P0 `deploy.ps1`.
5. Лог — `hikari.log` в **корне** MMD; все компоненты обязаны писать туда
   через `core::log_msg` (уже существует в `src/lib.rs`).
6. Экспериментальные скрипты (`expA/expB`) в папке MMD — референс
   decommission: удалить после перехода.
7. **Не трогать** `MMDPlugin`, `MMHack`, `MMEffect`, `d3dx9_43.dll` — они
   исключены из архитектуры (см. §1).
8. **Запрещено создавать что-либо в `plugin\`** — вся укладка только в
   корне программы (§1).
