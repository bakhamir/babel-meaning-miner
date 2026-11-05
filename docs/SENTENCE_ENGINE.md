# Sentence Discovery Engine

## Pipeline

Sentence mode reuses the Aho–Corasick lexical matcher. Boundary-valid dictionary hits are grouped only when the original bytes between them contain spaces or commas. A period terminates a span. Each run emits at most one bounded candidate (default 3–20 words), preventing combinatorial substring enumeration. Original zero-based, half-open byte offsets are retained.

The rule grammar recognizes a deliberately small subset of English:

- subject + verb;
- subject + verb + object;
- subject + linking verb + complement;
- subject + auxiliary + verb;
- subject + verb/link + prepositional phrase.

Subjects may be pronouns or determiner/adjective/noun groups. Objects and prepositional objects accept the same limited noun-phrase form. A small built-in POS lexicon checks obvious third-person/plural and `is`/`are` disagreements. This is not a general parser: coordination, clauses, irregular morphology, ambiguity, questions, imperatives, and many valid constructions are outside its coverage.

## Language model

`NGramLanguageModel` stores unigram, bigram, and trigram counts with add-0.1 smoothing and sentence boundary tokens. Scores are natural-log probabilities; the reported normalized value is `(0.2*unigram + 0.3*bigram + 0.5*trigram) / token_count`. Train on a legally usable user-supplied corpus:

```sh
bmm train-language-model --corpus corpus.txt --output models/english.ngram
bmm score-text --text "the dog is under the table" --model models/english.ngram
```

BMM does not bundle or mislabel a tiny synthetic corpus as real language data. Without `--model`, it uses `baseline-untrained`, a deterministic structural fallback. The files in `data/evaluation` are hand-labeled test fixtures only.

## Classification and ranking

- `WORD`: boundary-valid dictionary word.
- `PHRASE`: contiguous dictionary sequence in legacy word mode.
- `SENTENCE_CANDIDATE`: bounded span that did not pass every configured threshold.
- `GRAMMATICAL_SENTENCE`: passed the implemented rule grammar and thresholds.
- `SEMANTICALLY_VALIDATED`: reserved; the current build never emits it because no semantic validator runs.

The convenience ranking is `0.45*grammar + 0.25*completeness + 0.30*sigmoid(normalized_ngram)`. It is only an ordering function, not a calibrated probability or statistical-significance test. Component scores are persisted and exported separately.
