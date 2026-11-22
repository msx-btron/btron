I have investigated the architecture of the **Tibetan Bookkeeping of Terms "Osal"** in `./assets/osal/`, the existing Ruby indexers, the Wylie converter, Clarity's document and layout engines, and the TAD Browser format specifications.

Here is the architectural analysis and implementation plan for your review.

---

### 1. Architecture of the Osal Book Source Format

#### A. File Hierarchy & Canonical Chapter Sequence
The book in `./assets/osal/` is a complete Tibetan Buddhist compendium containing ~32,000 lines across 40 `.txt` files.
- **The Scroll Order (`shog.dril.sh`)**: In Tibetan bookmaking, *shog-dril* (ཤོག་དྲིལ) is the continuous rolled book or scroll volume. `shog.dril.sh` defines the canonical reading order of 32 core chapter files:
  1. `dkar.chag.txt` (དཀར་ཆག — Catalog / Table of Contents)
  2. `smon.lam.txt` (སྨོན་ལམ — Aspiration Prayers)
  3. `sngon.'gro.txt` (སྔོན་འགྲོ — Preliminary Practices)
  4. `khrus.txt` (ཁྲུས — Purification)
  5. `dge.bsngo.txt` (དགེ་བསྔོ — Dedication of Merit)
  6. `rnam.thar.txt` (རྣམ་ཐར — Hagiographies / Biographies)
  7. `lo.rgyud.txt` (ལོ་རྒྱུད — Historical Chronicles)
  8. `dbu.ma.txt` (དབུ་མ — Madhyamaka Philosophy)
  9. `rnal.'byor.spyod.pa.txt` (རྣལ་འབྱོར་སྤྱོད་པ — Yogācāra Philosophy)
  10. `rgyud.txt` (རྒྱུད — Tantras)
  ... through `gtum.mo`, `rtsa.rlung.thig`, `nang.gsang.rnal.'byor`, `sems.sde`, `khregs.chod`, `thod.rgal`, `'od.gsal`, `bar.do`, and `yang.ti.txt`.

#### B. Doctrinal 5-Class Classifier
In `dkar.chag.txt`, the texts are organized into five structural classes:
1. **Class I (`I) /sngon 'gro dang gzhi'i chos/`)**: Preliminary Practices & Foundational Dharma (8 sections).
2. **Class II (`II) /gzhung dang lta ba'i chos/`)**: Canonical Texts & Philosophical Dharma — Abhidharma, Madhyamaka, Yogacara, Pramana, Tantras, Songs (7 sections).
3. **Class III (`III) /bskyed rim mchod pa'i cho ga/`)**: Generation Stage Rituals, Offerings, Deities & Sadhanas (16 sections).
4. **Class IV (`IV) /rdzogs rim/`)**: Completion Stage Practices — Phowa, Tummo, Tsa-lung-thigle, Secret Guru Yoga (4 sections).
5. **Class V (`V) /rdzogs chen gyi nyams len/`)**: Great Perfection (Dzogchen) — 4 Mind Contemplations, Trekchö, Tögal, Ösal, Bardo, Yangti (6 sections).

#### C. Entry & Term Micro-Syntax
- **Text Headers**: `@#/_/<tradition>/ <wylie title>/ <translation>`
  - `@#/_/` represents the sacred Tibetan initial glyph cluster `༄༅། །` (*yig-mgo mdun-ma* + double *shad*).
  - Example: `@#/_/klong chen 'od gsal snying thig/ dkar chag/ Лонгчен Осал: Коментар на Зміст`
- **Glossary Entries**: `/wylie_term/ Translation and definition`
  - Example: `/khro bo ka li zhi ba/ Кродха Калі. Чорна мирна (zhi ba)...`
- **In-line Transliterations**: Parenthesized Wylie `(wylie)` embedded within translation prose.
- **Verse and Quote Markers**: Lines beginning with `| ` indicate verse/lineated commentary.
- **Enumerations & Lists**: `01) //term/ Title`, `1) ...`, and `* ...`.

---

### 2. Wylie → Tibetan Conversion Pipeline

The B-System codebase already includes a kernel TIP Extended Wylie Transliteration Scheme (EWTS) converter in `src/desktop/tip/tibetan/wylie.c` (`wylie_to_tibetan()`), which passes all unit tests for complex conjuncts, Sanskrit mantras, and root terms (e.g. `klong chen snying thig` $\rightarrow$ `ཀློང་ཆེན་སྙིང་ཐིག`).

- **Header Transliteration**:
  `@#/_/` converts to `༄༅། །`, followed by converted Tibetan title, and the translation title.
- **Term Transliteration**:
  Entries like `/khro bo ka li zhi ba/` convert to `[ ཁྲོ་བོ་ཀ་ལི་ཞི་བ། ]` or `ཁྲོ་བོ་ཀ་ལི་ཞི་བ།`.
- **In-line Transliterations**:
  Parenthetical terms `(...)` whose content is valid Wylie are transliterated to Tibetan script inside parentheses.
- **TRON Code Plane 10**:
  Tibetan Unicode codepoints (`0x0F00`..`0x0FFF`) map directly to TRON Code Plane 10 (`0x9F00`..`0x9FFF`) for rendering via `src/desktop/font/tibetan_fonts.c`.

---

### 3. Tibetan Pecha Page Dimensions for Clarity

The Clarity DTP engine (`src/apps/clarity/clarity_layout.c`) currently supports `FMT_PECHA` with ceremonial double-border (`lcags-ri`) and margin folios (`sna-chen`). We will support the 4 canonical Tibetan Pecha formats documented in `b-system/apps/Clarity.html`:
1. **Kangyur / Tengyur (標準経典判)**: $560 \times 110\text{ mm}$ (Default loose-leaf)
2. **Rinchen Terdzö (大蔵経・宝庫判)**: $650 \times 140\text{ mm}$ (Large treasury format)
3. **Derge Woodblock (デルゲ木版大判)**: $700 \times 180\text{ mm}$ (Derge print house monumental edition)
4. **Pocket Dharma (行者暗誦判)**: $320 \times 85\text{ mm}$ (Portable recitation pocket pecha)

---

### 4. Early Apple / BeOS Class Minimalist Open Dialog

Following the clean aesthetic in `b_drivesetup.c`:
- **Window Frame**: 3D beveled frame with navy title bar: `書籍を開く (Open Book / Osal)`
- **Path Field**: Default `./assets/osal` (or user editable).
- **Format Radio / Cycle Selector**:
  - `(•) Kangyur Pecha (560 × 110 mm)`
  - `( ) Rinchen Terdzö (650 × 140 mm)`
  - `( ) Derge Woodblock (700 × 180 mm)`
  - `( ) Pocket Dharma (320 × 85 mm)`
- **Options Checkbox**:
  - `[X] Wylie 自動チベット文字変換 (Convert Wylie to Tibetan script)`
- **Buttons**: `[ 読込 (Open Book) ]` and `[ 取消 (Cancel) ]`.
- **Keyboard Handling**: `Tab` cycles focus, `Up/Down` selects page size, `Enter` confirms, `Esc` cancels.

---

### 5. Clarity Working Menu & Legal TAD Book Export

#### Menu Additions:
- `ファイル(F)`:
  - `新規作成 (New)`
  - `書籍を開く (Open Book)...` (`Ctrl+B`) $\rightarrow$ opens the Minimalist Open Dialog.
  - `TAD 書籍出力 (Produce TAD Book)...` (`Ctrl+E`) $\rightarrow$ generates the complete TAD book.
  - `TAD Browser で閲覧 (Preview in TAD Browser)` (`Ctrl+P`) $\rightarrow$ instantly launches TAD Browser viewing the generated book.

#### Legal Binary TAD Generation (`src/apps/clarity/clarity_export.c`):
Produces a 100% compliant BTRON3 TAD binary stream:
- **Record Type 1 Header**: `0x0001` + 4-byte payload length.
- **`TS_TPAGE` (0xFFA0)**: Geometrical page dimensions, Pecha margins.
- **`TS_TFONT` (0xFFA2) & `TS_TCHAR` (0xFFA3)**: Typography styling, point size, weights, and colors.
- **`TS_TRULER` (0xFFA1)**: Line pitches and indents.
- **`TS_VOBJ` (0xFFA8)**: Hypermedia Virtual Body links for Table of Contents, linking directly to each chapter!
- **`TS_FPRIM` (0xFFB0)**: Vector dividing lines between sections.
- **`TS_TEXT`**: Valid UTF-8 spans with converted Tibetan script.
- **Output files generated**:
  - `tad_bin/osal/index.tad` (Master Book Catalogue with TOC links to chapters)
  - `tad_bin/osal/*.tad` (Individual chapter TAD files)
  - `tad_bin/osal_shog_dril.tad` (Unified complete scroll volume)

---

### 6. Verification & Automated Test Suite

1. **Automated Unit & Integration Test (`verify/tests/test_clarity_osal_book.c`)**:
   - Parses `./assets/osal` using the Osal Book parser.
   - Verifies all 5 Classes and 32+ chapters are categorized.
   - Verifies Wylie $\rightarrow$ Tibetan conversion of titles and terms.
   - Serializes the book into binary TAD.
   - Feeds the generated TAD directly into `tad_browser_load_buffer()` and asserts:
     - TAD Browser detects valid binary TAD (`is_binary_tad == TRUE`).
     - Spans are loaded with non-zero count.
     - Tibetan text spans are present.
     - Virtual Body links (`TS_VOBJ`) hit-test correctly.
2. **Build Target**:
   - Integrated into `make test-clarity-book` and tested under `$ make run-posix` core build.

---

### Proposed Implementation Plan

1. **Phase 1: Osal Parser & Book Data Structures (`src/apps/clarity/clarity_book.h`, `clarity_book.c`)**
   - Data structures: `ClarityBook`, `ClarityChapter`, `ClaritySection`, `ClarityClass`.
   - File scanner and classifier parser reading `./assets/osal/dkar.chag.txt` and `.txt` files in `shog.dril.sh` order.
   - Integrated Wylie $\rightarrow$ Tibetan conversion using `wylie_to_tibetan()`.

2. **Phase 2: Minimalist Open Dialog & Clarity UI Integration (`src/apps/clarity/clarity.c`, `clarity_layout.c`)**
   - Modal Open Dialog implementation with Pecha page format options and Wylie toggle.
   - Display loaded book in Clarity with chapter navigation, Pecha margins, and folio indices.

3. **Phase 3: Legal Binary TAD Book Serializer (`src/apps/clarity/clarity_export.c`)**
   - Generator for standard BTRON3 binary TAD (TOC index + chapters + scroll).
   - "Produce TAD Book" and "Preview in TAD Browser" menu actions.

4. **Phase 4: Automated Verification Suite & POSIX Build Validation**
   - Implement `verify/tests/test_clarity_osal_book.c`.
   - Add `make test-clarity-book` to `Makefile`.
   - Ensure clean compilation with `make btron-posix` and all existing tests passing.

---

Does this plan match your expectations? If you have any specific preferences regarding the menu naming, default paths, or Pecha dimensions, please let me know and we will proceed with the implementation!
