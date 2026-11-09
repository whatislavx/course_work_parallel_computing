#!/usr/bin/env python3
"""Корпус зі справжніх англійських слів (для вимірювань; джерело датасету OQ-01 ще не затверджено).

Слова беруться зі словника (наприклад, /usr/share/dict/american-english, пакет wamerican),
частотність задає закон Ціпфа. Тексти синтетичні: це набір реальних слів, а не природна проза.
Дані генеруються детерміновано (seed) і не комітяться.

Використання:
  generate_real_words_dataset.py OUT_DIR --wordlist /usr/share/dict/american-english \
      [--docs 10500] [--chars 100000] [--vocab 20000] [--seed 20261108]

  Режим за частотою (--ranked): --wordlist — список «слово частота» або просто слова, відсортовані
  від найуживанішого до найрідшого; словник = перші --vocab слів цього списку, ранг Ціпфа = позиція.
  Приклад (частотний список OpenSubtitles, FrequencyWords/en_50k.txt):
  generate_real_words_dataset.py OUT_DIR --wordlist en_50k.txt --ranked --vocab 2000
"""
import argparse
import itertools
import os
import random
import re

WORD_RE = re.compile(r"[a-z]{3,}")
WORDS_PER_LINE = 12

# Найуживаніші англійські слова (спільна основа для пошукових запитів). Ставляться на початок словника,
# тож отримують найвищі ранги Ціпфа: пошук за типовими словами («car», «garden», «energy») дає результати.
COMMON_WORDS = """
the be to of and a in that have it for not on with he as you do at this but his by from they we say her she
or an will my one all would there their what so up out if about who get which go me when make can like time no
just him know take people into year your good some could them see other than then now look only come its over
think also back after use two how our work first well way even new want because any these give day most us
man world house still help find where much should home big large small great old new long high last own part
place number right part case week company system program question government group problem fact hand eye head
water room school book word night end door life line side city country car road tree garden flower river mountain
forest animal bird fish dog cat horse food bread milk money market price value energy power light sun moon star
sky rain snow wind fire earth stone metal wood paper letter story music picture color sound voice language
science research data computer network server client program code file document text search query index result
engine machine tool design method process model theory practice test quality level state local national public
private social economic political history culture art garden solar window floor wall roof kitchen table chair
bed morning evening winter summer spring autumn season weather north south east west middle inside outside
open close begin start stop run walk stand sit speak listen read write learn teach play love live die feel
believe remember forget happen become seem leave keep bring carry hold move turn follow lead meet send build
break cut pull push fall rise grow show hear answer ask call try need mean put set let continue watch
""".split()




def load_words(path):
    with open(path, encoding="utf-8", errors="ignore") as handle:
        candidates = {line.strip() for line in handle}
    return sorted(word for word in candidates if WORD_RE.fullmatch(word))


def load_ranked_words(path):
    """Слова у порядку файлу (перше — найуживаніше), без дублів; повертає лише слова з WORD_RE."""
    seen = set()
    ranked = []
    with open(path, encoding="utf-8", errors="ignore") as handle:
        for line in handle:
            fields = line.split()
            if not fields:
                continue
            word = fields[0].lower()
            if WORD_RE.fullmatch(word) and word not in seen:
                seen.add(word)
                ranked.append(word)
    return ranked


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out_dir")
    parser.add_argument("--wordlist", required=True)
    parser.add_argument("--docs", type=int, default=10500)
    parser.add_argument("--chars", type=int, default=100_000)
    parser.add_argument("--vocab", type=int, default=20000)
    parser.add_argument("--seed", type=int, default=20261108)
    parser.add_argument("--ranked", action="store_true",
                        help="--wordlist впорядкований за частотою; словник = перші --vocab слів")
    args = parser.parse_args()

    rng = random.Random(args.seed)
    if args.ranked:
        ranked = load_ranked_words(args.wordlist)
        if len(ranked) < args.vocab:
            raise SystemExit(f"у списку {len(ranked)} слів, потрібно {args.vocab}")
        vocab = ranked[:args.vocab]
    else:
        words = load_words(args.wordlist)
        if len(words) < args.vocab:
            raise SystemExit(f"у словнику {len(words)} слів, потрібно {args.vocab}")
        head = [word for word in dict.fromkeys(COMMON_WORDS) if WORD_RE.fullmatch(word)]
        if len(head) > args.vocab:
            raise SystemExit("--vocab замалий для списку частих слів")
        head_set = set(head)
        rest = [word for word in words if word not in head_set]
        vocab = head + rng.sample(rest, args.vocab - len(head))
    # Закон Ціпфа: слово на рангу r має вагу 1/r^1.07.
    cum_weights = list(itertools.accumulate(1.0 / rank ** 1.07 for rank in range(1, args.vocab + 1)))
    avg_len = sum(len(word) for word in vocab) / len(vocab) + 1  # +1 пробіл
    words_per_doc = int(args.chars / avg_len)

    os.makedirs(args.out_dir, exist_ok=True)
    for doc_index in range(args.docs):
        picks = rng.choices(vocab, cum_weights=cum_weights, k=words_per_doc)
        lines = [" ".join(picks[i:i + WORDS_PER_LINE]) for i in range(0, len(picks), WORDS_PER_LINE)]
        path = os.path.join(args.out_dir, f"doc{doc_index:06d}.txt")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("\n".join(lines) + "\n")

    print(f"згенеровано {args.docs} документів у {args.out_dir} (словник: {args.vocab} слів)")


if __name__ == "__main__":
    main()
