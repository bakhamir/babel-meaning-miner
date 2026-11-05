# Experiments and scientific limits

The local null model samples every character independently and uniformly from the configured alphabet. For a fixed word of length `L` and alphabet size `A`, its raw expected substring count across `N` characters is approximately `(N-L+1)/A^L`; boundary filtering multiplies this by the probability that both neighboring symbols are nonletters (with edge corrections). Overlaps and shared dictionary prefixes mean dictionary-wide counts are not independent, so Monte Carlo runs using fixed seeds are the recommended baseline.

The current language score, `log(1+letters) * word_count`, only orders candidates. It is explicitly a limited baseline, not corpus-derived probability. Exact substrings, boundary-valid dictionary words, adjacent phrases, and semantic statements are distinct categories. BMM currently establishes only the first three syntactically and makes no semantic or statistical-significance claim.

For a defensible experiment, register a frozen dictionary/corpus checksum, run many independent seeds, retain score distributions, predeclare thresholds, and compare the observed tail against the identical generator/alphabet/boundary null model.

## Benchmark captured on this development machine

Release build, 10,000 pages per run, 3,200 characters per page, bundled 43-word baseline dictionary, SQLite persistence, 2026-11-05:

| Workers | Pages/s | Characters/s |
|---:|---:|---:|
| 1 | 8,269.4 | 26,462,013.9 |
| 2 | 14,807.6 | 47,384,354.0 |
| 4 | 21,421.6 | 68,549,184.4 |
| 8 | 16,437.7 | 52,600,676.1 |
| 12 | 13,481.7 | 43,141,487.6 |

These are actual single-run measurements, not portable performance claims. Four workers was best in this run; database serialization and CPU contention reduce scaling beyond that point.

## v0.2 sentence experiment

A deterministic run of 10,000 local pages (seed 4242, four workers, 32,000,000 characters) produced 15,111 persisted lexical findings but zero spans of at least three contiguous dictionary words. Consequently it produced zero sentence candidates and zero grammatical sentences. This null result is reported as-is; no test sentence was injected into scanned pages. Runtime was 9.491 seconds (1,053.61 pages/s) on the development machine.

The tiny held-out grammar fixture produced TP=4, FN=0, FP=0, TN=5 (precision=1, recall=1). The sample is far too small and curated to generalize; it verifies implemented rules only. Random-text false positives in the 10,000-page run were 0 accepted sentences / 32,000,000 characters for that seed and dictionary, not a significance claim.

Stage-isolated v0.2 measurements (single run): generation 41,459.5 pages/s (132.67M chars/s); legacy lexical+phrase analysis 10,636.6 pages/s over 2,000 pre-generated pages; sentence-mode staged analysis 10,163.3 pages/s over the same sized sample; rule grammar 2.870M candidates/s over 100,000 repetitions; fallback N-gram scoring 2.661M candidates/s over 100,000 repetitions; SQLite batch persistence 46,431.9 findings/s over 10,000 distinct rows. Combined sentence scan with SQLite was 1,053.61 pages/s in the experiment above. CPU utilization was not measured reliably in this environment.
