# Source research (verified 2026-11-05)

## Findings

| Source | What it does | License | Compatibility / reuse conclusion |
|---|---|---|---|
| [official algorithm repository](https://github.com/librarianofbabel/libraryofbabel.info-algo) | C++ example of an invertible multiprecision PRNG; its README says it is the **image** library (`babelia`) algorithm, which is more efficient than the text-site algorithm. | README label: CC BY-SA-NC (“No liability”); no conventional LICENSE file was visible. | It does not establish text-page address compatibility and its non-commercial share-alike terms are unsuitable for silent incorporation into this MIT project. Not integrated. |
| [official LoB-API repository](https://github.com/librarianofbabel/LoB-API) | Repository description promises an API, but the inspected default branch contains only a LICENSE and no implementation or endpoint documentation. | CC0-1.0. | No usable API is supplied by the repository. |
| [jrhea/library-of-babel](https://github.com/jrhea/library-of-babel) | Fully static client-side text page generator/searcher using base-29 encoding and an invertible LCG. | Apache-2.0. | The README calls it an “attempt at a faithful ... recreation,” not a verified exact implementation. It generates text offline, but no official reference fixture or proof of address identity was found. Not labeled original-compatible. |
| [victor-cortez Python API](https://github.com/victor-cortez/Library-of-Babel-Python-API) | A remote scraper: POSTs to `download.cgi` and `search.cgi`, then parses returned HTML/text. It does not generate pages independently. | MIT. | Tied to undocumented website endpoints and response markup; unsuitable for uncontrolled scanning or an offline engine. The repository shows only five commits and no evidence of a stable official API contract. |
| [libraryofbabel.info](https://libraryofbabel.info/) | Official text web UI exposing Browse, Search, Random, theory/about pages, and a separate image archive. | Site-wide reusable-code license not verified. | The site was reachable during research, but no documented, supported programmatic API or current automation policy was found in the inspected materials. |

## Answers to the requested questions

1. **Text versus images:** jrhea generates text pages locally. Victor Cortez retrieves text from the official site. The official `libraryofbabel.info-algo` repository explicitly discusses the more efficient `babelia` image algorithm, so it is not evidence for the official text-page mapping.
2. **Exact compatibility:** none of the inspected independent implementations provides verified exact compatibility with the original text website. The jrhea project is a recreation and uses its own documented reversible mapping. No reference vectors were available to prove identity.
3. **API status:** the official-named LoB-API repository has no API code. The Python wrapper targets undocumented CGI endpoints; that is an unofficial scraper, not a stable supported API. Its live behavior was not exercised because the MVP must not automate the website without verified usage permission.
4. **Address mapping:** the official website uses a hexagon plus wall/shelf/volume/page coordinates. The inspected official source does not publish the complete text mapping. Jrhea's independent mapping treats a page as a base-29 number, combines position, applies a reversible LCG, and encodes an address in base 36. BMM's local mapping is deliberately separate: `local:<seed>:<index>` seeds a versioned SplitMix64 stream from the seed and index.
5. **Licenses:** official image-algorithm example: CC BY-SA-NC statement; LoB-API: CC0-1.0; jrhea: Apache-2.0; Victor Cortez: MIT. BMM copies no source from these repositories.
6. **Independent generation:** yes for experimental/recreation schemes (jrhea and BMM), but exact independent generation of official text pages was not verified.

## Unverified assumptions and limitations

- Endpoint availability does not establish permission, stability, or an API contract. Robots/rate-limit terms were not sufficiently documented, so BMM ships no remote adapter.
- Website output was not captured as a fixture and no original-backend verification can honestly pass. `bmm verify --backend original` therefore exits nonzero.
- Repository pages and the official landing page were inspected on the date above; no claim is made about undocumented server code or future changes.

The v0.2 live-interface audit and the single timed-out historical endpoint request are recorded in [REMOTE_INTEGRATION.md](REMOTE_INTEGRATION.md).
