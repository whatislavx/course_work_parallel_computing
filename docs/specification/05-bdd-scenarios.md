# 05. BDD-сценарії

Ключові слова Gherkin — англійською (стандарт), кроки — українською. Кожен сценарій має тег з власним ID (`@BDD-XXX-NN`) і теги вимог (`@FR-…`, `@NFR-…`). Сценарії описують спостережувану поведінку через публічні інтерфейси (REST, WebSocket, CLI, Web UI, лог сервера як вихід для оператора, контрольована Ollama як зовнішня межа); кроки не залежать від реалізації. Вимоги до внутрішнього пулу потоків FR-036…FR-041 не мають BDD і перевіряються модульними тестами (`09` §2). Виняток: BDD-IDX-12 виконується на рівні інтеграційного тесту (TEST-IT-06), де збій вносить тестовий двійник компонента читання документів через ін'єкцію залежності. Виробничий бінарник не має тестового режиму чи хуків.

Спільні визначення для всіх features:

- **«тестовий корпус»** — фіксований набір невеликих англійських документів у `tests/fixtures/corpus/` з відомими термами (FIX-01, див. `09` §1).
- **«контрольована Ollama»** — тестовий двійник HTTP-сервісу Ollama, відповіді та затримки якого задає тест.
- **«помилка у форматі API»** — тіло `{ "error": { "code": <string>, "message": <string> } }` (06 §2.4).
- **«допуск»** — `TIMING_TOLERANCE_MS` (`09` §1).
- **«триває побудова індексу»** — запущено побудову корпусу FIX-06 з 1 потоком, і відповідь на цей `POST /api/index` ще не отримано на момент кроку When. Якщо відповідь уже надійшла, прогін недійсний, і тест повторюється з більшим FIX-06 (`09` §1).

---

## Feature: Стан сервера (HLT)

```gherkin
Feature: Перевірка стану сервера
  Як оператор, я хочу дізнатися, чи працює сервер і чи доступна Ollama,
  щоб знати, чи готова система до пошуку.

  Background:
    Given пошуковий сервер запущено

  @BDD-HLT-01 @FR-001 @FR-002 @NFR-013
  Scenario: Сервер і Ollama доступні
    Given Ollama доступна і налаштована модель присутня
    When клієнт надсилає GET /api/health
    Then код відповіді 200
    And тіло містить "status" зі значенням "ok"
    And тіло містить "ollama" зі значенням true
    And тіло не містить інших полів

  @BDD-HLT-02 @FR-002 @NFR-008
  Scenario: Ollama недоступна
    Given Ollama зупинена
    When клієнт надсилає GET /api/health
    Then код відповіді 200
    And тіло містить "status" зі значенням "ok"
    And тіло містить "ollama" зі значенням false

  @BDD-HLT-03 @NFR-010 @FR-001
  Scenario: Health під час побудови індексу
    Given триває побудова індексу бенчмаркового датасету з кількістю потоків, що дорівнює кількості логічних процесорів
    When клієнт надсилає GET /api/health
    Then код відповіді 200
    And відповідь отримано не пізніше HEALTH_CHECK_TIMEOUT_MS плюс допуск

  @BDD-HLT-04 @FR-002
  Scenario: Ollama приймає з'єднання, але не відповідає
    Given контрольована Ollama не відповідає на перевірку доступності
    When клієнт надсилає GET /api/health
    Then код відповіді 200
    And тіло містить "ollama" зі значенням false
    And відповідь отримано не пізніше HEALTH_CHECK_TIMEOUT_MS плюс допуск
```

---

## Feature: Побудова індексу (IDX)

```gherkin
Feature: Побудова інвертованого індексу
  Як оператор, я хочу будувати індекс з заданою кількістю потоків,
  щоб шукати документи і досліджувати масштабованість.

  Background:
    Given пошуковий сервер запущено
    And каталог датасету містить тестовий корпус з 12 документів, якщо сценарій не вказує інше

  @BDD-IDX-01 @FR-003 @FR-004 @FR-005 @NFR-013
  Scenario: Успішна побудова з 4 потоками
    When клієнт надсилає POST /api/index з тілом {"threads": 4}
    Then код відповіді 200
    And "status" дорівнює "ok"
    And "documents_indexed" дорівнює 12
    And "time_ms" є цілим числом не менше 0
    And тіло не містить інших полів

  @BDD-IDX-02 @FR-004 @FR-006 @FR-010 @NFR-005
  Scenario Outline: Результат побудови не залежить від кількості потоків
    When клієнт надсилає POST /api/index з тілом {"threads": <p>}
    Then "documents_indexed" дорівнює 12
    And GET /api/stats повертає "threads_used" рівне <p>
    And результати пошуку для кожного запиту з таблиці тестових запитів збігаються з еталонними

    Examples:
      | p |
      | 1 |
      | 2 |
      | 4 |
      | 8 |

  @BDD-IDX-03 @FR-007
  Scenario Outline: Некоректне значення threads
    Given індекс уже побудовано з 2 потоками
    When клієнт надсилає POST /api/index з тілом <body>
    Then код відповіді 400
    And тіло є помилкою у форматі API з кодом "invalid_threads"
    And GET /api/stats повертає "threads_used" рівне 2

    Examples:
      | body                                  |
      | {"threads": 0}                        |
      | {"threads": -1}                       |
      | {"threads": "four"}                   |
      | {"threads": 1.5}                      |
      | {"threads": 4.0}                      |
      | {"threads": null}                     |
      | {"threads": MAX_INDEX_THREADS + 1}    |

  @BDD-IDX-04 @FR-004
  Scenario: Побудова без параметра threads
    Given значення threads за замовчуванням налаштовано як D
    When клієнт надсилає POST /api/index з тілом {}
    Then код відповіді 200
    And GET /api/stats повертає "threads_used" рівне D

  @BDD-IDX-05 @FR-008 @FR-010
  Scenario: Повторна побудова замінює індекс
    Given індекс побудовано з 2 потоками
    When клієнт надсилає POST /api/index з тілом {"threads": 4}
    Then GET /api/stats повертає "threads_used" рівне 4
    And "index_build_time_ms" дорівнює "time_ms" з останньої відповіді побудови
    And результати пошуку для таблиці тестових запитів не змінилися

  @BDD-IDX-06 @FR-009
  Scenario: Друга побудова під час першої відхиляється
    Given триває побудова індексу
    When клієнт надсилає ще один POST /api/index з тілом {"threads": 2}
    Then код відповіді 409
    And тіло є помилкою у форматі API з кодом "index_build_in_progress"
    And перша побудова завершується з кодом 200

  @BDD-IDX-07 @FR-003
  Scenario: Порожній каталог датасету
    Given каталог датасету порожній
    When клієнт надсилає POST /api/index з тілом {"threads": 2}
    Then код відповіді 200
    And "documents_indexed" дорівнює 0

  @BDD-IDX-08 @FR-003 @NFR-009
  Scenario: Каталог датасету недоступний
    Given індекс уже побудовано з тестового корпусу
    And каталог датасету видалено
    When клієнт надсилає POST /api/index з тілом {"threads": 2}
    Then код відповіді 500
    And тіло є помилкою у форматі API з кодом "dataset_unavailable"
    And пошук за термом з тестового корпусу повертає попередні результати

  @BDD-IDX-09 @FR-003
  Scenario: Нечитабельний документ пропускається з записом у лозі
    Given один із 12 документів містить некоректну послідовність UTF-8
    When клієнт надсилає POST /api/index з тілом {"threads": 2}
    Then код відповіді 200
    And "documents_indexed" дорівнює 11
    And лог сервера містить назву пропущеного документа

  @BDD-IDX-10 @FR-007
  Scenario: Некоректний JSON у запиті побудови
    When клієнт надсилає POST /api/index з тілом "{threads: 4"
    Then код відповіді 400
    And тіло є помилкою у форматі API з кодом "invalid_json"

  @BDD-IDX-11 @FR-004
  Scenario Outline: Тіло без threads, порожнє тіло і довільний Content-Type
    Given значення threads за замовчуванням налаштовано як D
    When клієнт надсилає POST /api/index з тілом <body> і заголовком Content-Type <ctype>
    Then код відповіді 200
    And GET /api/stats повертає "threads_used" рівне <expected>

    Examples:
      | body             | ctype                               | expected |
      | (порожнє)        | (відсутній)                         | D        |
      | {}               | application/json                    | D        |
      | {"threads": 2}   | application/x-www-form-urlencoded   | 2        |

  @BDD-IDX-12 @FR-042
  Scenario: Аварійне завершення задачі індексації
    Given індекс уже побудовано з тестового корпусу
    And компонент індексації зібрано в інтеграційному тесті з двійником, через який одна задача завершується внутрішнім винятком (не помилкою читання файлу)
    When клієнт надсилає POST /api/index з тілом {"threads": 4}
    Then код відповіді 500
    And тіло є помилкою у форматі API з кодом "index_build_failed"
    And пошук за термом з тестового корпусу повертає попередні результати
    And наступна побудова без збою повертає код 200
```

---

## Feature: Статистика (STA)

```gherkin
Feature: Статистика індексу

  Background:
    Given пошуковий сервер запущено

  @BDD-STA-01 @FR-010
  Scenario: Статистика після побудови
    Given каталог датасету містить тестовий корпус з 12 документів
    And клієнт побудував індекс з 3 потоками і отримав time_ms = T
    When клієнт надсилає GET /api/stats
    Then код відповіді 200
    And "documents" дорівнює 12
    And "index_build_time_ms" дорівнює T
    And "threads_used" дорівнює 3
    And тіло не містить інших полів

  @BDD-STA-02 @FR-011
  Scenario: Статистика до першої побудови
    Given індекс ще не будувався
    When клієнт надсилає GET /api/stats
    Then код відповіді 200
    And "documents", "index_build_time_ms", "threads_used" дорівнюють 0
```

---

## Feature: Пошук за ключовими словами (SRC)

```gherkin
Feature: Пошук за ключовими словами через REST

  Background:
    Given пошуковий сервер запущено
    And SEARCH_TOP_K налаштовано не менше 12, якщо сценарій не задає іншого
    And індекс побудовано з тестового корпусу, якщо сценарій не вказує інше

  @BDD-SRC-01 @FR-012 @FR-013 @FR-015 @NFR-013
  Scenario: Знайдено документи за термом
    When клієнт надсилає POST /api/search з тілом {"query": "parallel", "mode": "keyword"}
    Then код відповіді 200
    And "results" містить рівно ті документи корпусу, що містять терм "parallel"
    And кожен результат має "doc_id" (рядок), "score" (число), "snippet" (рядок) і жодних інших полів
    And "time_ms" є цілим числом не менше 0

  @BDD-SRC-02 @FR-014
  Scenario: Результати впорядковано за score
    When клієнт шукає "thread pool"
    Then значення "score" у "results" не зростають
    And порядок документів збігається з еталонним розрахунком формули ранжування

  @BDD-SRC-03 @FR-013
  Scenario: Пошук нечутливий до регістру
    When клієнт шукає "PARALLEL"
    Then "results" збігаються з результатами пошуку "parallel"

  @BDD-SRC-04 @FR-013
  Scenario: Запит з кількох термів повертає документи з будь-яким із них
    When клієнт шукає "mutex semaphore"
    Then "results" містять документи, що містять "mutex", і документи, що містять "semaphore"

  @BDD-SRC-05 @FR-017
  Scenario: Немає збігів
    When клієнт шукає "zzzxqqnotaword"
    Then код відповіді 200
    And "results" є порожнім масивом

  @BDD-SRC-06 @FR-016
  Scenario Outline: Некоректний пошуковий запит
    When клієнт надсилає POST /api/search з тілом <body>
    Then код відповіді 400
    And тіло є помилкою у форматі API з кодом <code>

    Examples:
      | body                                          | code               |
      | {"mode": "keyword"}                           | "invalid_query"    |
      | {"query": "", "mode": "keyword"}              | "invalid_query"    |
      | {"query": "   ", "mode": "keyword"}           | "invalid_query"    |
      | {"query": 42, "mode": "keyword"}              | "invalid_query"    |
      | {"query": "<MAX_QUERY_LENGTH+1 символів>", "mode": "keyword"} | "invalid_query" |
      | {"query": "parallel"}                         | "invalid_mode"     |
      | {"query": "parallel", "mode": "semantic"}     | "unsupported_mode" |
      | {"query": "parallel", "mode": 1}              | "invalid_mode"     |
      | "{query:"                                     | "invalid_json"     |
      | []                                            | "invalid_json"     |

  @BDD-SRC-07 @FR-018
  Scenario: Пошук до побудови індексу
    Given сервер щойно запущено і індекс ще не будувався
    When клієнт шукає "parallel"
    Then код відповіді 409
    And тіло є помилкою у форматі API з кодом "index_not_ready"

  @BDD-SRC-08 @FR-015
  Scenario: Snippet містить терм запиту і обмежений за довжиною
    When клієнт шукає "parallel"
    Then "snippet" кожного результату містить "parallel" без урахування регістру
    And довжина кожного "snippet" не перевищує SNIPPET_MAX_CHARS

  @BDD-SRC-09 @FR-035 @FR-008
  Scenario: Пошук під час перебудови використовує попередній індекс
    Given запущено повторну побудову індексу на великому датасеті
    When під час побудови клієнт шукає "parallel"
    Then код відповіді 200
    And "results" збігаються з результатами попереднього індексу

  @BDD-SRC-10 @FR-013
  Scenario: Кількість результатів обмежена
    Given SEARCH_TOP_K налаштовано як 3
    And терм "the" міститься в усіх 12 документах
    When клієнт шукає "the"
    Then "results" містить рівно 3 елементи

  @BDD-SRC-11 @FR-012 @FR-019
  Scenario Outline: Режим REST-пошуку визначає звернення до Ollama (рішення OQ-02)
    Given контрольована Ollama записує всі отримані запити
    When клієнт надсилає POST /api/search з тілом {"query": "car", "mode": "<mode>"}
    Then код відповіді 200
    And контрольована Ollama отримала рівно <calls> запит(ів)

    Examples:
      | mode     | calls |
      | keyword  | 0     |
      | expanded | 1     |

  @BDD-SRC-12 @FR-012
  Scenario: JSON-тіло з іншим Content-Type приймається
    When клієнт надсилає POST /api/search з тілом {"query": "parallel", "mode": "keyword"} і заголовком Content-Type "text/plain"
    Then код відповіді 200
```

---

## Feature: Розширення запиту (QEX)

```gherkin
Feature: Розширення запиту через LLM

  Background:
    Given пошуковий сервер запущено
    And індекс побудовано з тестового корпусу
    And клієнт підключився до /ws/search

  # пропозиція OQ-14/OQ-21: правила постобробки
  @BDD-QEX-01 @FR-019 @FR-023
  Scenario: Ollama повертає ключові слова і синоніми
    Given контрольована Ollama на запит про "car" повертає слова "automobile", "Vehicle", "vehicle", "car"
    When клієнт надсилає {"query": "car"}
    Then подія "query_expansion" має "expanded_terms" рівний ["automobile", "vehicle"]

  @BDD-QEX-02 @FR-020
  Scenario: Розширений терм знаходить документ без початкового терма
    Given документ "doc-auto" містить "automobile" і не містить "car"
    And контрольована Ollama на запит про "car" повертає "automobile"
    When клієнт надсилає {"query": "car"}
    Then подія "keyword_search" містить результат з "doc_id" документа "doc-auto"

  @BDD-QEX-03 @FR-021 @NFR-008
  Scenario: Ollama недоступна
    Given Ollama зупинена
    When клієнт надсилає {"query": "parallel"}
    Then подія "query_expansion" має "expanded_terms" рівний []
    And подія "keyword_search" містить ті самі результати, що й REST-пошук "parallel"
    And надходить подія "done"

  @BDD-QEX-04 @FR-021 @NFR-020
  Scenario: Ollama не відповідає довше тайм-ауту
    Given контрольована Ollama відповідає із затримкою, більшою за OLLAMA_TIMEOUT_MS
    When клієнт надсилає {"query": "parallel"}
    Then подія "query_expansion" з "expanded_terms" рівним [] надходить не пізніше OLLAMA_TIMEOUT_MS плюс допуск
    And надходять події "keyword_search" і "done"

  @BDD-QEX-05 @FR-021
  Scenario: Ollama повертає непридатну відповідь
    Given контрольована Ollama повертає відповідь без жодного придатного терма
    When клієнт надсилає {"query": "parallel"}
    Then подія "query_expansion" має "expanded_terms" рівний []
    And надходять події "keyword_search" і "done"

  # пропозиція OQ-14
  @BDD-QEX-06 @FR-019
  Scenario: Кількість розширених термінів обмежена
    Given MAX_EXPANDED_TERMS налаштовано як 3
    And контрольована Ollama повертає 10 різних нових слів
    When клієнт надсилає {"query": "parallel"}
    Then "expanded_terms" містить не більше 3 елементів

  @BDD-QEX-07 @FR-015 @FR-020
  Scenario: Snippet документа, знайденого лише за розширеним термом
    Given документ "doc-auto" містить "automobile" і не містить "car"
    And контрольована Ollama на запит про "car" повертає "automobile"
    When клієнт надсилає {"query": "car"}
    Then snippet результату "doc-auto" у події "keyword_search" містить "automobile"

  @BDD-QEX-08 @FR-019
  Scenario: Розширення запитується через /api/generate
    Given контрольована Ollama записує всі отримані запити
    When клієнт надсилає {"query": "parallel"}
    Then контрольована Ollama отримала запит POST на шлях "/api/generate"
    And тіло цього запиту містить текст "parallel"
```

---

## Feature: WebSocket-стрімінг (WS)

```gherkin
Feature: Потокова передача етапів пайплайну

  Background:
    Given пошуковий сервер запущено
    And індекс побудовано з тестового корпусу, якщо сценарій не вказує інше

  @BDD-WS-01 @FR-022 @FR-025 @FR-026
  Scenario: Повний пайплайн у правильному порядку
    Given клієнт підключився до /ws/search
    When клієнт надсилає {"query": "parallel"}
    Then клієнт отримує рівно три повідомлення
    And поля "stage" повідомлень у порядку надходження: "query_expansion", "keyword_search", "done"
    # пропозиція OQ-31 (суперечність C-07)
    And повідомлення "done" не містить поля "data"

  @BDD-WS-02 @FR-023
  Scenario: Дані етапу query_expansion
    Given клієнт підключився до /ws/search
    When клієнт надсилає {"query": "Parallel Computing"}
    Then "data.original" події "query_expansion" дорівнює "Parallel Computing"
    And "data.expanded_terms" є масивом рядків

  @BDD-WS-03 @FR-024
  Scenario: Дані етапу keyword_search
    Given клієнт підключився до /ws/search
    When клієнт надсилає {"query": "parallel"}
    Then "data.results" події "keyword_search" має ту саму структуру елементів, що й REST-пошук
    And значення "score" у "data.results" не зростають
    And "data.time_ms" є цілим числом не менше 0

  @BDD-WS-04 @FR-027
  Scenario Outline: Некоректне повідомлення клієнта
    Given клієнт підключився до /ws/search
    When клієнт надсилає <message>
    Then клієнт отримує подію "error" з "data.code" рівним <code>
    And з'єднання залишається відкритим
    And наступне повідомлення {"query": "parallel"} дає повний пайплайн

    Examples:
      | message             | code            |
      | not json            | "invalid_json"  |
      | [1, 2]              | "invalid_json"  |
      | (бінарний кадр)     | "invalid_json"  |
      | {"q": "parallel"}   | "invalid_query" |
      | {"query": ""}       | "invalid_query" |
      | {"query": "   "}    | "invalid_query" |
      | {"query": 7}        | "invalid_query" |
      | {"query": "<MAX_QUERY_LENGTH+1 символів>"} | "invalid_query" |

  @BDD-WS-05 @FR-018 @FR-027
  Scenario: Запит до побудови індексу
    Given індекс ще не будувався
    And клієнт підключився до /ws/search
    When клієнт надсилає {"query": "parallel"}
    Then клієнт отримує подію "error" з "data.code" рівним "index_not_ready"
    And клієнт не отримує подій "query_expansion" і "keyword_search"
    And контрольована Ollama не отримала жодного запиту

  @BDD-WS-06 @NFR-009 @FR-028
  Scenario: Обрив з'єднання під час пайплайну не впливає на інших
    Given клієнти A і B підключені до /ws/search
    And контрольована Ollama відповідає із затримкою 2 секунди
    When клієнт A надсилає {"query": "parallel"} і закриває з'єднання до отримання "query_expansion"
    And клієнт B надсилає {"query": "mutex"}
    Then клієнт B отримує повний пайплайн
    And GET /api/health повертає 200

  @BDD-WS-07 @FR-022
  Scenario: Кілька запитів в одній сесії
    Given клієнт підключився до /ws/search
    When клієнт надсилає {"query": "parallel"} і отримує "done"
    And клієнт надсилає {"query": "mutex"}
    Then клієнт отримує другий повний пайплайн з "data.original" рівним "mutex"

  @BDD-WS-08 @FR-027
  Scenario: Новий запит до завершення попереднього в тій самій сесії
    Given клієнт підключився до /ws/search
    And контрольована Ollama відповідає із затримкою 2 секунди
    When клієнт надсилає {"query": "parallel"}
    And до отримання "done" клієнт надсилає {"query": "mutex"}
    Then клієнт отримує подію "error" з "data.code" рівним "pipeline_busy"
    And перший пайплайн завершується подією "done"

  @BDD-WS-09 @FR-026
  Scenario: Етапи надсилаються окремо в міру виконання
    Given контрольована Ollama відповідає із затримкою 1 секунда
    And клієнт підключився до /ws/search
    When клієнт надсилає {"query": "parallel"}
    Then подія "query_expansion" надходить не раніше ніж через 1 секунду після запиту
    And подія "query_expansion" надходить раніше події "keyword_search"
```

---

## Feature: Одночасна обробка (CON)

```gherkin
Feature: Одночасна обробка запитів кількох клієнтів

  Background:
    Given пошуковий сервер запущено
    And індекс побудовано з тестового корпусу

  @BDD-CON-01 @FR-028
  Scenario: Одночасні REST-пошуки дають коректні результати
    Given 16 клієнтів, кожен зі своїм запитом із таблиці тестових запитів
    When усі клієнти одночасно надсилають POST /api/search
    Then усі відповіді мають код 200
    And кожна відповідь збігається з відповіддю на той самий запит, виконаний окремо

  @BDD-CON-02 @NFR-007
  Scenario: Повільне розширення не блокує інших клієнтів
    Given контрольована Ollama відповідає із затримкою 5 секунд
    And клієнт A надіслав {"query": "parallel"} через /ws/search
    When клієнт B надсилає POST /api/search з запитом "mutex"
    Then клієнт B отримує відповідь 200 менш ніж за 2.5 секунди

  @BDD-CON-03 @FR-035 @NFR-004
  Scenario: Пошуки під час побудови завершуються успішно
    Given триває повторна побудова індексу
    When 8 клієнтів одночасно виконують пошуки
    Then усі відповіді мають код 200
    And жодна відповідь не містить документів, відсутніх у попередньому індексі

  @BDD-CON-04 @NFR-005 @FR-006
  Scenario Outline: Паралельна побудова еквівалентна послідовній
    Given результати пошуку кожного терма тестового корпусу після побудови з 1 потоком збережено як еталон
    When індекс будується з <p> потоками
    Then результати пошуку кожного терма корпусу (doc_id і score) збігаються з еталоном

    Examples:
      | p  |
      | 2  |
      | 3  |
      | 4  |
      | 8  |

  @BDD-CON-05 @FR-028
  Scenario: Одночасні WS-сесії ізольовані
    Given 8 клієнтів підключені до /ws/search
    When кожен одночасно надсилає власний запит
    Then кожен клієнт отримує рівно три події
    And "data.original" у кожного дорівнює його власному запиту

  @BDD-CON-06 @FR-028
  Scenario: Запити обробляються паралельно, а не по черзі
    Given контрольована Ollama відповідає на кожен запит із затримкою 1 секунда
    When 4 клієнти одночасно надсилають запити через /ws/search
    Then усі 4 клієнти отримують "done" менш ніж за 4 секунди від початку
```

---

## Feature: Консольний клієнт (CLI)

```gherkin
Feature: Пошук з командного рядка

  Background:
    Given пошуковий сервер запущено
    And індекс побудовано з тестового корпусу, якщо сценарій не вказує інше

  @BDD-CLI-01 @FR-029
  Scenario: Успішний пошук
    When користувач виконує ./search "parallel"
    Then вивід містить кожен "doc_id" з відповіді REST-пошуку "parallel" у тому самому порядку
    And для кожного результату виведено score і snippet
    And код виходу 0

  @BDD-CLI-02 @FR-029 @FR-017
  Scenario: Немає результатів
    When користувач виконує ./search "zzzxqqnotaword"
    Then вивід містить повідомлення про відсутність результатів
    And код виходу 0

  @BDD-CLI-03 @FR-030
  Scenario: Сервер недоступний
    Given пошуковий сервер зупинено
    When користувач виконує ./search "parallel"
    Then stderr містить повідомлення про недоступність сервера
    And код виходу не 0

  @BDD-CLI-04 @FR-030
  Scenario: Немає аргументу
    When користувач виконує ./search без аргументів
    Then stderr містить інструкцію використання
    And код виходу не 0

  @BDD-CLI-05 @FR-030
  Scenario: Сервер повертає помилку
    Given індекс ще не будувався
    When користувач виконує ./search "parallel"
    Then stderr містить текст "message" з помилки сервера
    And код виходу не 0
```

---

## Feature: Веб-інтерфейс (WEB)

```gherkin
Feature: Пошук у Web UI з візуалізацією етапів

  Background:
    Given система запущена командою docker compose up
    And індекс побудовано, якщо сценарій не вказує інше

  @BDD-WEB-01 @FR-031 @FR-034
  Scenario: Сторінка з формою пошуку
    When користувач відкриває адресу Web UI з README
    Then сторінка містить поле вводу запиту і кнопку пошуку

  @BDD-WEB-02 @FR-032
  Scenario: Відображення результатів
    When користувач вводить "parallel" і натискає кнопку пошуку
    Then сторінка показує результати в тому самому порядку, що й подія "keyword_search"
    And для кожного результату показано doc_id, score і snippet

  @BDD-WEB-03 @FR-033
  Scenario: Етапи пайплайну відображаються в міру надходження
    Given контрольована Ollama відповідає із затримкою 2 секунди
    When користувач шукає "parallel"
    Then етап "query_expansion" спочатку показано як "очікує"
    And після надходження події етап показано як "виконано" разом з розширеними термінами
    And після "done" пайплайн показано як завершений

  @BDD-WEB-04 @FR-033 @FR-021
  Scenario: Розширення недоступне
    Given Ollama зупинена
    When користувач шукає "parallel"
    Then етап "query_expansion" показує "розширення недоступне"
    And результати пошуку показано

  @BDD-WEB-05 @FR-031
  Scenario: Порожній запит не надсилається
    When користувач натискає кнопку пошуку з порожнім полем
    Then запит на сервер не надсилається
    And сторінка показує підказку ввести запит

  @BDD-WEB-06 @FR-032 @FR-027
  Scenario: Відображення помилки
    Given індекс ще не будувався
    When користувач шукає "parallel"
    Then сторінка показує текст поля "message" з події "error"

  @BDD-WEB-07 @FR-032
  Scenario: Сервер недоступний для Web UI
    Given сторінку Web UI відкрито
    And пошуковий сервер зупинено
    When користувач шукає "parallel"
    Then сторінка показує повідомлення про недоступність сервера
```

---

## Feature: Розгортання (DEP)

```gherkin
Feature: Запуск системи однією командою

  @BDD-DEP-01 @NFR-011 @FR-001
  Scenario: Чистий запуск
    Given свіжий клон репозиторію на машині з Docker і Docker Compose
    And датасет розміщено згідно з README
    When оператор виконує docker compose up
    Then протягом READINESS_TIMEOUT_S GET /api/health повертає 200
    And жодних інших ручних дій не виконувалося

  @BDD-DEP-02 @NFR-012
  Scenario: Склад сервісів
    When оператор переглядає перелік сервісів docker compose
    Then перелік містить сервіс пошукового сервера і сервіс Ollama

  @BDD-DEP-03 @NFR-024 @FR-019
  Scenario: Модель Ollama доступна після запуску без ручних дій
    Given свіжий клон репозиторію з порожнім томом моделі Ollama
    When оператор виконує docker compose up
    Then протягом READINESS_TIMEOUT_S GET /api/health повертає "ollama" зі значенням true
    And після побудови індексу WS-запит із таблиці тестових запитів дає непорожні "expanded_terms"
```

---

## Feature: Загальні помилки HTTP (GEN)

```gherkin
Feature: Обробка невідомих запитів

  Background:
    Given пошуковий сервер запущено

  @BDD-GEN-01 @FR-043
  Scenario: Невідомий шлях
    When клієнт надсилає GET /api/unknown
    Then код відповіді 404
    And тіло є помилкою у форматі API з кодом "not_found"

  @BDD-GEN-02 @FR-043
  Scenario: Недозволений метод
    When клієнт надсилає DELETE /api/index
    Then код відповіді 405
    And тіло є помилкою у форматі API з кодом "method_not_allowed"

  @BDD-GEN-03 @FR-043
  Scenario: Завелике тіло запиту
    When клієнт надсилає POST /api/search з тілом розміром MAX_REQUEST_BODY_BYTES + 1 байт
    Then код відповіді 413
    And тіло є помилкою у форматі API з кодом "payload_too_large"
```

---

## Feature: Ресурси і завершення роботи (OPS)

```gherkin
Feature: Обмеження з'єднань і коректне завершення

  @BDD-OPS-01 @NFR-025
  Scenario: Перевищення ліміту одночасних з'єднань
    Given MAX_CONCURRENT_CONNECTIONS налаштовано як M
    And контрольована Ollama відповідає із затримкою 5 секунд
    And M клієнтів тримають активні WS-пайплайни
    When ще один клієнт надсилає POST /api/search
    Then код відповіді 503
    And тіло є помилкою у форматі API з кодом "server_busy"
    And ще одне WS-рукостискання на /ws/search відхиляється з HTTP 503
    And GET /api/health повертає 200
    And після завершення активних пайплайнів пошук повертає 200

  @BDD-OPS-02 @NFR-026
  Scenario: Коректне завершення за SIGTERM
    Given клієнт має відкриту WS-сесію
    And триває побудова індексу
    When процес сервера отримує SIGTERM
    Then клієнт отримує закриття WebSocket з кодом 1001
    And клієнт, що чекає на відповідь POST /api/index, отримує закриття з'єднання без HTTP-відповіді
    And процес завершується з кодом 0 не пізніше SHUTDOWN_TIMEOUT_MS
```

---

## Feature: Продуктивність (PRF)

```gherkin
Feature: Дослідження масштабованості

  Background:
    Given система запущена на машині бенчмарку
    And розміщено бенчмарковий датасет, що відповідає NFR-014

  @BDD-PRF-01 @NFR-001
  Scenario Outline: Час побудови зменшується зі збільшенням потоків
    Given машина бенчмарку має C фізичних ядер, де <умова>
    When дослідник запускає бенчмарк побудови для p з набору NFR-002
    Then <очікування>

    Examples:
      | умова     | очікування                                                    |
      | 2 ≤ C < 4 | медіана T(2) менша за медіану T(1)                            |
      | C ≥ 4     | медіана T(2) менша за T(1), і медіана T(C) менша за T(2)      |

  @BDD-PRF-02 @NFR-002 @NFR-023 @NFR-022
  Scenario: Звіт бенчмарку побудови
    When дослідник запускає бенчмарк побудови для p від 1 до N
    Then звіт містить для кожного p медіану, min, max, S(p), E(p) і пікову пам'ять
    And звіт містить параметри середовища
    And створено графік "час побудови vs кількість потоків"

  @BDD-PRF-03 @NFR-003 @NFR-023
  Scenario: Звіт throughput
    Given індекс побудовано
    When дослідник запускає бенчмарк навантаження для набору кількостей клієнтів
    Then звіт містить для кожної кількості клієнтів throughput, частку помилок, медіану і p95 затримки
    And створено графік "throughput vs кількість клієнтів"
```

---

## Підсумок

| Feature | Сценаріїв |
|---|---|
| HLT | 4 |
| IDX | 12 |
| STA | 2 |
| SRC | 12 |
| QEX | 8 |
| WS | 9 |
| CON | 6 |
| CLI | 5 |
| WEB | 7 |
| DEP | 3 |
| GEN | 3 |
| OPS | 2 |
| PRF | 3 |
| **Разом: 13 features** | **76** |

Scenario Outline рахується як один сценарій.
