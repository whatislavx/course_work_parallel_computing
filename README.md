# Пошуковий сервіс з паралельною побудовою індексу і розширенням запиту через LLM

Курсова робота з дисципліни «Паралельні обчислення» (КПІ ім. Ігоря Сікорського, ІПСА, 122 Комп'ютерні науки).

Сервіс будує інвертований індекс у кілька потоків власним пулом потоків, відповідає на пошукові запити через REST і WebSocket, розширює запит через локальну модель Ollama, має консольний клієнт і веб-інтерфейс. Усе запускається через Docker Compose.

## Швидкий старт

```bash
git clone <URL_репозиторію> && cd course_work_parallel_computing
cp .env.example .env                        # за потреби змініть параметри
mkdir -p dataset                            # покладіть сюди .txt-документи (див. «Датасет»)
docker compose up --build
```

Після старту:
- Web UI: `http://localhost:8081/`
- Перевірка готовності: `curl -s http://localhost:8081/api/health` → `{"status":"ok","ollama":true}`

Ollama запускається окремим сервісом (`ollama`). Сервіс `ollama-model` один раз завантажує модель з `OLLAMA_MODEL` (за замовчуванням `qwen2.5:0.5b`, див. ADR-005). Сервер стартує після готовності Ollama.

> Стан перевірки: конфігурацію compose перевірено командою `docker compose config`. Повний запуск з чистого клону ще не верифіковано в середовищі розробки (потрібен запущений Docker-демон). Результат оновлюватиметься у записці.

## Порти і сервіси

| Сервіс         | Призначення                                        | Порт на хості |
|----------------|----------------------------------------------------|---------------|
| `server`       | Пошуковий сервер (REST і WebSocket), C++          | — (внутрішній, 8080) |
| `ollama`       | LLM для розширення запиту                          | —             |
| `ollama-model` | Одноразове завантаження моделі                     | —             |
| `web`          | nginx: Web UI і проксі `/api` та `/ws/search`      | 8081          |

## Датасет

Датасет не зберігається в репозиторії.

- **Формат:** каталог із текстовими файлами `*.txt`, по одному документу на файл, кодування ASCII або UTF-8. Ідентифікатор документа (`doc_id`) — ім'я файлу.
- **Розташування:** на хості це каталог `DATASET_HOST_DIR` (за замовчуванням `./dataset`). У контейнері сервера він монтується як `/data/docs` лише для читання (`DATASET_DIR`).
- **Вимоги:** не менше 10 000 документів, загальний обсяг не менше 1 ГБ (для бенчмарків).
- **Джерело:** ще не затверджено викладачем (OQ-01). Для відтворення вимірювань використано синтетичний корпус, який створює `scripts/generate_dataset.py` (детермінований, seed 20261108, 10 500 документів, 1.1 ГБ). Його можна згенерувати так: `python3 scripts/generate_dataset.py ./dataset`.

## REST API

Базовий шлях `/api`. Усі відповіді — JSON. Помилки мають єдиний формат:

```json
{ "error": { "code": "invalid_query", "message": "query must be a non-empty string" } }
```

### `GET /api/health`

Відповідь `200`:
```json
{ "status": "ok", "ollama": true }
```
`ollama` дорівнює `false`, якщо Ollama недоступна. Код відповіді при цьому все одно `200`.

### `POST /api/index`

Синхронна побудова індексу. Тіло необов'язкове.

Запит:
```json
{ "threads": 4 }
```
`threads` — ціле від 1 до `MAX_INDEX_THREADS`. Якщо поля немає або тіло порожнє, використовується `DEFAULT_INDEX_THREADS` (0 означає кількість логічних процесорів).

Відповідь `200`:
```json
{ "status": "ok", "documents_indexed": 10500, "time_ms": 21770 }
```

Помилки: `400 invalid_json`, `400 invalid_threads`, `409 index_build_in_progress`, `413`, `500 dataset_unavailable`, `500 index_build_failed`, `500 internal_error`, `503 server_busy`.

### `POST /api/search`

Запит:
```json
{ "query": "parallel index", "mode": "keyword" }
```
- `query` — рядок, після обрізання пробілів довжиною від 1 до `MAX_QUERY_LENGTH`.
- `mode` — `"keyword"` (без розширення) або `"expanded"` (розширення через Ollama; за збою чи тайм-ауту пошук виконується без розширення).

Відповідь `200`:
```json
{
  "results": [
    { "doc_id": "doc000189.txt", "score": 0.3621, "snippet": "…" }
  ],
  "time_ms": 56
}
```
- `results` — до `SEARCH_TOP_K` елементів, за спаданням `score`.
- `snippet` — до `SNIPPET_MAX_CHARS` символів, містить терм запиту.

Помилки: `400 invalid_json`, `400 invalid_query`, `400 invalid_mode`, `400 unsupported_mode`, `409 index_not_ready`, `413`, `500 internal_error`, `503 server_busy`.

### `GET /api/stats`

Відповідь `200`:
```json
{ "documents": 10500, "index_build_time_ms": 21770, "threads_used": 8 }
```
До першої успішної побудови всі значення дорівнюють `0`.

### Приклади

```bash
curl -s -X POST http://localhost:8081/api/index -H 'Content-Type: application/json' -d '{"threads": 4}'
curl -s -X POST http://localhost:8081/api/search -H 'Content-Type: application/json' -d '{"query":"parallel index","mode":"keyword"}'
curl -s http://localhost:8081/api/stats
```

## WebSocket: `/ws/search`

Один запит на з'єднання. Клієнт надсилає текстовий кадр з JSON, сервер відповідає серією подій у фіксованому порядку.

Клієнт → сервер:
```json
{ "query": "parallel index" }
```

Сервер → клієнт (по порядку):
```json
{ "stage": "query_expansion", "data": { "original": "parallel index", "expanded_terms": ["concurrency"] } }
{ "stage": "keyword_search", "data": { "results": [ { "doc_id": "…", "score": 0.3621, "snippet": "…" } ], "time_ms": 12 } }
{ "stage": "done" }
```

Правила:
- Якщо Ollama недоступна або повернула некоректний результат, `query_expansion` все одно надсилається з `expanded_terms: []`, і пайплайн продовжується.
- Помилка надсилається як `{ "stage": "error", "data": { "code": "…", "message": "…" } }`. Коди: `invalid_json`, `invalid_query`, `index_not_ready`, `pipeline_busy`, `internal_error`.
- Сервер не закриває з'єднання після `done` і після `error`. Наступний запит на тому ж з'єднанні можливий, поки попередній пайплайн завершено.
- Закриття WebSocket-сесій з кодом `1001` при зупинці сервера ще не реалізовано (див. «Відомі обмеження»).

Повна специфікація: `docs/specification/06-api-specification.md` (розділ 4) і `docs/specification/openapi.yaml` для REST.

## CLI-клієнт

```bash
./build/src/search "parallel index"                        # URL з SEARCH_SERVER_URL або http://localhost:8080
./build/src/search --url http://localhost:8081 "parallel index"
```

Вихід: результати у вигляді нумерованого списку з `doc_id`, `score` і `snippet`. Коди виходу: `0` — успіх (у т.ч. без результатів), `1` — сервер недоступний або повернув помилку чи неочікувану відповідь, `2` — помилка аргументів.

Збірка: див. розділ нижче.

## Web UI

Статична сторінка без збірки (`web/static/`). Форма запиту, список результатів, етапи пайплайну через WebSocket. Текст документів вставляється без HTML-розбору, nginx монтує лише `web/static` (тести і `package.json` у контейнер не потрапляють), `root` і `try_files` мають блокувати path traversal. Перевірка цього в запущеному контейнері ще не виконана.

## Локальна збірка і тести

Потрібні: CMake ≥ 3.20, компілятор з C++20 (GCC 13 або Clang 17+), Boost 1.83 (Beast, JSON, Asio), Node.js 22 для тестів Web UI.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
node --test web/test/app.test.mjs
```

- Тести тягнуть GoogleTest з GitHub, потрібна мережа. Без тестів: `-DSEARCH_BUILD_TESTS=OFF`.
- Санітайзери: `-DSEARCH_SANITIZER=thread` (TSan), `address` або `undefined` (UBSan).
- Бенчмарки: `benchmarks/bench_index.py` (побудова, S(p), E(p), RSS), `benchmarks/bench_throughput.py` (throughput і затримки). Результати і умови — `benchmarks/results/README.md`.
- Перевірки якості: `scripts/check_format.sh`, `scripts/check_secrets.sh`, `scripts/check_commits.sh`, `scripts/check_dependencies.sh`.

## Конфігурація

Параметри читаються з змінних середовища. Приклад — у `.env.example`. Значення за замовчуванням і межі — у `src/config/config.hpp`. Compose передає в сервер значення з `.env` (`env_file`); значення, задані в `environment:` у `docker-compose.yml`, мають пріоритет.

Ключові змінні: `SERVER_PORT`, `DATASET_DIR`, `DATASET_HOST_DIR`, `DEFAULT_INDEX_THREADS`, `MAX_INDEX_THREADS`, `MAX_REQUEST_BODY_BYTES`, `REQUEST_TIMEOUT_MS`, `OLLAMA_URL`, `OLLAMA_MODEL`, `OLLAMA_TIMEOUT_MS`, `SEARCH_TOP_K`, `MAX_QUERY_LENGTH`, `SNIPPET_MAX_CHARS`, `MAX_EXPANDED_TERMS`.

## Структура репозиторію

```
src/           Бекенд: пул потоків, індекс, пошук, HTTP і WebSocket, Ollama, CLI
tests/         GoogleTest-тести і фікстури (tests/fixtures)
web/           Web UI (web/static), тести клієнта (web/test), конфігурація nginx
docker/        Dockerfile сервера і скрипт завантаження моделі Ollama
benchmarks/    Скрипти вимірювань і результати (benchmarks/results)
scripts/       Перевірки якості і генератор датасету
docs/          Вимоги, специфікація, архітектура, ADR, API, план розробки
```

Рішення з відкритих питань: `docs/architecture/decisions/` (ADR-004, ADR-005) і `docs/development/backlog.md`.

## Відомі обмеження

- Датасет і його джерело ще не затверджені (OQ-01).
- Повний запуск compose з чистого клону і E2E-тести не виконані в середовищі розробки (потрібен Docker-демон).
- Скасування побудови при SIGTERM і код `1001` для WebSocket-сесій ще не реалізовані (TCK-039).
- Перевірка path traversal у запущеному Web-контейнері не виконана.
- Синтаксис PlantUML-діаграм (`docs/architecture/uml/`) не перевірено рендером.
- Бенчмарки в репозиторії виміряні на 2 логічних ядрах; перемір на ширшій машині — окремо.
