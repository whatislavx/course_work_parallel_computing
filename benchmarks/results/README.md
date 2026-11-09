# Результати бенчмарків

## Умови

- Машина: Intel Core i5-10300H (4 фізичні ядра, 8 логічних), 15,5 ГіБ ОП, Ubuntu 24.04.5, ядро 7.0.0-42, регулятор частоти `powersave` (частоту не фіксовано).
- Збірка: GCC 13.3.0, CMake `Release` (`-O3 -DNDEBUG`).
- Побудова індексу: сервер запущено нативно, окремий процес на кожне p; `T(p)` — медіана з 5 повторів після одного прогрівочного запуску.
- Пропускна здатність: сервер у Docker-стеку (`docker compose`), запит `the`, 5 серій по 200 запитів на кожне K; клієнт `bench_throughput.py` на тому самому хості.
- Датасет: синтетичний корпус, 10 500 документів, 1,045 ГіБ, seed 20261108 (`scripts/generate_dataset.py`).

## index_build.csv і index_build_sharded.csv

Побудова індексу, базова версія (глобальне блокування) і версія з шардуванням за термом (64 шарди). Обидві виміряно в одному прогоні, по черзі для кожного p.
`S(p) = T(1) / T(p)`, `E(p) = S(p) / p` обчислює `benchmarks/index_speedup.py` із сирих CSV (`raw/sharding/{base,shard}/index_build_t{1,2,4,8}.csv`).

| p | базова, мс | S(p) | E(p) | шардування, мс | S(p) | E(p) |
|---|---|---|---|---|---|---|
| 1 | 15 144 | 1,000 | 1,000 | 15 048 | 1,000 | 1,000 |
| 2 | 8 790 | 1,723 | 0,861 | 8 611 | 1,748 | 0,874 |
| 4 | 8 014 | 1,890 | 0,472 | 5 870 | 2,564 | 0,641 |
| 8 | 8 338 | 1,816 | 0,227 | 5 706 | 2,637 | 0,330 |

Коректність шардованої версії: 203 тести GoogleTest і ThreadSanitizer без попереджень; відповіді пошуку на 40 запитах для 10 500 документів збігаються з базовою версією.

## throughput_summary.csv

Пошук через Docker-стек: медіана з 5 серій, мінімум і максимум пропускної здатності за серіями. Файли `throughput.csv` і `throughput_summary.csv` містять сирі серії та агрегат.

## Відтворення

```bash
python3 benchmarks/bench_index.py http://127.0.0.1:8080 <PID_сервера> raw/index_build_t1.csv --threads 1 --repeats 5
python3 benchmarks/index_speedup.py index_build.csv raw/index_build_t1.csv raw/index_build_t2.csv raw/index_build_t4.csv raw/index_build_t8.csv
python3 benchmarks/bench_throughput.py http://127.0.0.1:8081 raw/throughput.csv --clients 1,2,4,8,16 --requests 200 --query the --repeats 5
```

Сирі файли (`raw/`) не комітяться (див. `.gitignore`).
