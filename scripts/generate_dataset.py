#!/usr/bin/env python3
"""Synthetic English-like corpus generator (детермінований, seed-based).

Призначено для вимірювань, коли джерело датасету ще не затверджено (OQ-01).
Документи — псевдослова з розподілом Ціпфа; дані НЕ комітяться в Git.

Використання: generate_dataset.py OUT_DIR [--docs 10500] [--chars 100000] [--seed 20261108]
"""
import argparse
import os
import random

CONSONANTS = "bcdfghjklmnprstvwz"
VOWELS = "aeiou"


def build_vocabulary(rng: random.Random, size: int) -> list[str]:
    words: set[str] = set()
    while len(words) < size:
        length = rng.randint(2, 4)
        word = "".join(
            rng.choice(CONSONANTS) if i % 2 == 0 else rng.choice(VOWELS)
            for i in range(length * 2)
        )
        words.add(word)
    return sorted(words)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("out_dir")
    parser.add_argument("--docs", type=int, default=10500)
    parser.add_argument("--chars", type=int, default=100_000)
    parser.add_argument("--seed", type=int, default=20261108)
    args = parser.parse_args()

    rng = random.Random(args.seed)
    vocab = build_vocabulary(rng, 30_000)
    weights = [1.0 / (rank + 1) for rank in range(len(vocab))]
    os.makedirs(args.out_dir, exist_ok=True)
    for doc_index in range(args.docs):
        words_needed = args.chars // 6
        words = rng.choices(vocab, weights=weights, k=words_needed)
        text = " ".join(words)[: args.chars]
        path = os.path.join(args.out_dir, f"doc{doc_index:06d}.txt")
        with open(path, "w", encoding="ascii") as handle:
            handle.write(text + "\n")


if __name__ == "__main__":
    main()
