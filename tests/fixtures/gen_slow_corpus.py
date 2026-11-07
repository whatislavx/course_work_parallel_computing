"""Генератор «повільного» корпусу (FIX-06). Не зберігається в Git: вихід — у каталозі збірки.

Детермінований: однаковий seed і параметри дають однаковий вміст. Розмір, що дає побудову
щонайменше 3 с при p = 1, калібрується окремо після появи індексу (TCK-008); значення за
замовчуванням тимчасові.

  python3 tests/fixtures/gen_slow_corpus.py --out build/slow-corpus --docs 2000 --seed 42
"""
import argparse
import pathlib
import random

VOCAB = [
    "parallel", "mutex", "semaphore", "thread", "pool", "index", "query", "search",
    "document", "worker", "server", "cache", "token", "term", "rank", "queue",
    "socket", "request", "response", "build", "load", "store", "result",
]


def generate(out: pathlib.Path, docs: int, words: int, seed: int) -> None:
    rng = random.Random(seed)
    out.mkdir(parents=True, exist_ok=True)
    for i in range(docs):
        text = " ".join(rng.choice(VOCAB) for _ in range(words))
        (out / f"doc{i:06d}.txt").write_text(text + "\n", encoding="utf-8")


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--out", required=True)
    p.add_argument("--docs", type=int, default=2000, help="тимчасове значення, калібрується в TCK-008")
    p.add_argument("--words", type=int, default=400, help="тимчасове значення, калібрується в TCK-008")
    p.add_argument("--seed", type=int, default=42)
    args = p.parse_args()
    generate(pathlib.Path(args.out), args.docs, args.words, args.seed)


if __name__ == "__main__":
    main()
