"""Незалежний від C++-коду генератор FIX-02 (еталон) і FIX-03 (таблиця запитів).

Токенізація за OQ-21: нижній регістр, послідовності ASCII-літер і цифр, без стоп-слів і стемінгу.
doc_id — відносний шлях файлу (OQ-18). Запуск: python3 tests/fixtures/make_reference.py
"""
import json
import pathlib
import re

HERE = pathlib.Path(__file__).resolve().parent
CORPUS = HERE / "corpus-small"


def tokenize(text: str) -> list[str]:
    return re.findall(r"[a-z0-9]+", text.lower())


def build_postings(corpus: pathlib.Path) -> dict[str, list[str]]:
    postings: dict[str, set[str]] = {}
    for path in sorted(corpus.rglob("*.txt")):
        doc_id = path.relative_to(corpus).as_posix()
        for term in set(tokenize(path.read_text(encoding="utf-8"))):
            postings.setdefault(term, set()).add(doc_id)
    return {term: sorted(ids) for term, ids in sorted(postings.items())}


def main() -> None:
    postings = build_postings(CORPUS)
    (HERE / "reference" / "postings.json").write_text(
        json.dumps(postings, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    # FIX-03: запит → очікувані doc_id (OR-семантика для кількох термів, OQ-03; без розширення).
    queries = {
        "parallel": ["parallel"],
        "thread pool": ["thread", "pool"],
        "mutex": ["mutex"],
        "semaphore": ["semaphore"],
        "automobile": ["automobile"],
        "car": ["car"],
        "the": ["the"],
        "missing-term": ["nonexistenttermxyz"],
    }
    table = {}
    for query, terms in queries.items():
        sets = [set(postings.get(t, [])) for t in terms]
        result = set.union(*sets) if sets else set()
        table[query] = sorted(result)
    (HERE / "reference" / "query-table.json").write_text(
        json.dumps(table, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
