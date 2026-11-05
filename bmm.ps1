[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Command = "menu",

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$Arguments
)

$ErrorActionPreference = "Stop"
$ProjectRoot = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
$Executable = Join-Path $ProjectRoot "build\bmm.exe"
$Database = Join-Path $ProjectRoot "data\bmm-v03.db"
$Dictionary = Join-Path $ProjectRoot "data\words.txt"
$LastExperimentFile = Join-Path $ProjectRoot ".bmm-v03-last-experiment"

function Find-CMake {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $wingetRoot = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"
    $candidate = Get-ChildItem -LiteralPath $wingetRoot -Recurse -Filter cmake.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
    if ($candidate) { return $candidate }
    throw "CMake не найден. Установи CMake и открой терминал заново."
}

function Ensure-Build {
    if (Test-Path -LiteralPath $Executable) { return }
    Write-Host "BMM ещё не собран. Выполняю локальную сборку..." -ForegroundColor Yellow
    $cmake = Find-CMake
    & $cmake -S $ProjectRoot -B (Join-Path $ProjectRoot "build") -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw "Не удалось настроить сборку." }
    & $cmake --build (Join-Path $ProjectRoot "build") --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "Не удалось собрать BMM." }
}

function Invoke-Tests {
    Ensure-Build
    $cmake = Find-CMake
    & $cmake --build (Join-Path $ProjectRoot "build") --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "Не удалось собрать BMM перед тестами." }
    & (Join-Path (Split-Path $cmake) "ctest.exe") --test-dir (Join-Path $ProjectRoot "build") -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Есть ошибки в тестах." }
}

function Invoke-Engine([string[]]$EngineArguments) {
    Ensure-Build
    Push-Location $ProjectRoot
    try {
        & $Executable @EngineArguments
        if ($LASTEXITCODE -ne 0) { throw "BMM завершился с кодом $LASTEXITCODE." }
    }
    finally { Pop-Location }
}

function Invoke-JsonEngine([string[]]$EngineArguments) {
    Ensure-Build
    Push-Location $ProjectRoot
    try {
        $raw = & $Executable @EngineArguments
        if ($LASTEXITCODE -ne 0) { throw "BMM завершился с кодом $LASTEXITCODE." }
        try { return ($raw -join "`n" | ConvertFrom-Json) }
        catch { throw "Движок вернул повреждённый JSON: $($raw -join ' ')" }
    }
    finally { Pop-Location }
}

function Write-ScanReport($Result) {
    $names = @{ words = "Лексическое сканирование"; sentences = "Поиск предложений"; passages = "Поиск проходов" }
    Write-Host ""
    Write-Host $names[$Result.mode] -ForegroundColor Cyan
    Write-Host ("  Эксперимент:       {0}" -f $Result.experiment)
    Write-Host ("  Режим источника:   {0}" -f $Result.source_mode)
    Write-Host ("  Статус:            {0}" -f $Result.status)
    Write-Host ("  Обработано страниц:{0,12:N0}" -f [double]$Result.pages)
    Write-Host ("  Время:             {0,12:N3} с" -f [double]$Result.seconds)
    Write-Host ("  Скорость:          {0,12:N1} стр./с" -f [double]$Result.pages_per_second)
    Write-Host ("  Сохранено находок: {0,12:N0}" -f [double]$Result.stored_findings)
    Write-Host ("  Лексических совп.: {0,12:N0}" -f [double]$Result.metrics.lexical)
    if ($Result.mode -ne "words") {
        Write-Host ("  Кандидатов:        {0,12:N0}" -f [double]$Result.metrics.sentence_candidates)
        Write-Host ("  Принято грамматикой:{0,11:N0}" -f [double]$Result.metrics.grammar_accepted)
    }
    if ($Result.mode -eq "passages") {
        Write-Host ("  Проходов:          {0,12:N0}" -f [double]$Result.passages)
        Write-Host ("  Связных проходов:  {0,12:N0}" -f [double]$Result.metrics.coherent_passages)
    }
}

function Write-Findings($Rows) {
    $items = @($Rows)
    if (-not $items.Count) { Write-Host "Находок пока нет." -ForegroundColor Yellow; return }
    Write-Host "`nЛучшие находки: $($items.Count)" -ForegroundColor Cyan
    $number = 0
    foreach ($row in $items) {
        $number++
        Write-Host ("  {0,2}. [{1}] {2}" -f $number, $row.classification, $row.text) -ForegroundColor Green
        Write-Host ("      {0}, символы {1}–{2}, режим {3}" -f $row.page_address, $row.start_offset, $row.end_offset, $row.source_mode)
        Write-Host ("      языковая оценка {0:N3}; грамматика {1:N3}" -f [double]$row.language_score, [double]$row.grammar_score) -ForegroundColor DarkGray
    }
}

function Write-Occurrences($Rows, [string]$Kind) {
    $items = @($Rows)
    $title = switch ($Kind) {
        "word" { "Найденные слова" }
        "phrase" { "Совпадения из двух и более слов" }
        default { "Найденные одиночные предложения" }
    }
    Write-Host "`n$title" -ForegroundColor Cyan
    if (-not $items.Count) {
        Write-Host "  В этом эксперименте подходящих результатов нет." -ForegroundColor Yellow
        Write-Host "  Слова и фразы сохраняются в режиме поиска слов, предложения — в режиме поиска предложений." -ForegroundColor DarkGray
        return
    }
    $number = 0
    foreach ($row in $items) {
        $number++
        Write-Host ("  {0,3}. {1,6:N0} ×  {2}" -f $number, [double]$row.count, $row.text) -ForegroundColor Green
        Write-Host ("       класс: {0}; источник: {1}" -f $row.classification, $row.source_mode) -ForegroundColor DarkGray
    }
}

function Write-Passages($Rows) {
    $items = @($Rows)
    if (-not $items.Count) { Write-Host "Проходов пока нет." -ForegroundColor Yellow; return }
    Write-Host "`nНайденные проходы: $($items.Count)" -ForegroundColor Cyan
    foreach ($row in $items) {
        Write-Host ("  #{0} [{1}] {2}" -f $row.id, $row.classification, $row.text) -ForegroundColor Green
        Write-Host ("      предложений: {0}; связность: {1:N3}; источник: {2}" -f $row.sentence_count, [double]$row.coherence, $row.source_mode)
        Write-Host ("      адрес: {0}, символы {1}–{2}" -f $row.page_address, $row.start_offset, $row.end_offset) -ForegroundColor DarkGray
    }
}

function Write-Stats($Stats) {
    Write-Host "`nСтатистика эксперимента $($Stats.experiment)" -ForegroundColor Cyan
    Write-Host ("  Статус:             {0}" -f $Stats.status)
    Write-Host ("  Обработано страниц: {0:N0}" -f [double]$Stats.pages_scanned)
    Write-Host ("  Общее время:        {0:N3} с" -f [double]$Stats.elapsed_seconds)
    Write-Host ("  Средняя скорость:   {0:N1} стр./с" -f [double]$Stats.pages_per_second)
    Write-Host ("  Словарных совпадений: {0:N0}" -f [double]$Stats.metrics.lexical)
    Write-Host ("  Кандидатов в предложения: {0:N0}" -f [double]$Stats.metrics.sentence_candidates)
    Write-Host ("  Принято грамматикой: {0:N0}" -f [double]$Stats.metrics.grammar_accepted)
    Write-Host ("  Проходов:            {0:N0}" -f [double]$Stats.metrics.passages)
    if ($Stats.longest_passage) { Write-Host ("  Самый длинный проход: {0}" -f $Stats.longest_passage.text) -ForegroundColor Green }
}

function Write-Feasibility($Report) {
    Write-Host "`nОценка статистической редкости" -ForegroundColor Cyan
    Write-Host ("  Размер словаря: {0:N0} уникальных слов" -f [double]$Report.dictionary_unique)
    Write-Host ("  Проверяемый объём: {0:N0} страниц" -f [double]$Report.pages)
    Write-Host ("  Словарных слов на страницу:       {0:N6}" -f [double]$Report.expected_words_per_page)
    Write-Host ("  Пар соседних слов на страницу:    {0:E3}" -f [double]$Report.expected_two_words_per_page)
    Write-Host ("  Троек соседних слов на страницу:  {0:E3}" -f [double]$Report.expected_three_words_per_page)
    Write-Host ("  Грамматических троек в модели:    {0:N0}" -f [double]$Report.finite_three_word_grammar_count)
    Write-Host ("  Среднее ожидание предложения:     {0:E3} страниц" -f [double]$Report.mean_wait_pages_finite_grammar_approx)
    Write-Host ("  Нижняя оценка ожидания прохода:   {0:E3} страниц" -f [double]$Report.waiting_pages_lower_bound_finite_passage_approx)
    Write-Host ""
    Write-Host "Вывод: случайно найти несколько связанных предложений практически нереально." -ForegroundColor Yellow
    Write-Host "Это математическая оценка конечной грамматики, а не вероятность всего английского языка." -ForegroundColor DarkGray
}

function Write-Evaluation($Result) {
    Write-Host "`nКонтролируемая проверка детектора" -ForegroundColor Cyan
    Write-Host ("  Верно найдено (TP):   {0}" -f $Result.TP)
    Write-Host ("  Пропущено (FN):       {0}" -f $Result.FN)
    Write-Host ("  Ложных срабатываний:  {0}" -f $Result.FP)
    Write-Host ("  Верно отклонено (TN): {0}" -f $Result.TN)
    Write-Host ("  Точность: {0:P1}; полнота: {1:P1}" -f [double]$Result.precision, [double]$Result.recall) -ForegroundColor Green
    Write-Host "  Проверка синтетическая; это не натуральные находки." -ForegroundColor DarkGray
}

function Write-PassageScore($Result) {
    Write-Host "`nОценка прохода" -ForegroundColor Cyan
    Write-Host ("  Текст: {0}" -f $Result.text)
    Write-Host ("  Классификация: {0}" -f $Result.classification) -ForegroundColor Green
    Write-Host ("  Предложений: {0}; общая связность: {1:N3}" -f $Result.sentence_count, [double]$Result.coherence)
    Write-Host ("  Лексическая связь: {0:N3}; сущности: {1:N3}; тема: {2:N3}; переход: {3:N3}" -f [double]$Result.lexical_continuity, [double]$Result.entity_repetition, [double]$Result.topic_consistency, [double]$Result.transition_score)
    Write-Host "  Это поверхностная эвристика, не семантическое доказательство." -ForegroundColor DarkGray
}

function Write-GeneratedPage($Page) {
    Write-Host "`nСгенерированная страница" -ForegroundColor Cyan
    Write-Host ("  Адрес: {0}" -f $Page.address)
    Write-Host ("  Режим источника: {0}" -f $Page.source_mode)
    Write-Host "  Текст:" -ForegroundColor DarkGray
    Write-Host $Page.text
}

function Save-ExperimentFromOutput($Lines) {
    $text = $Lines -join "`n"
    if ($text -match "Experiment:\s*(\d+)") {
        Set-Content -LiteralPath $LastExperimentFile -Value $Matches[1] -NoNewline
        Write-Host "Последний эксперимент: $($Matches[1])" -ForegroundColor DarkGray
    }
}

function Get-Experiment([string]$ExplicitId) {
    if ($ExplicitId) { return $ExplicitId }
    if (Test-Path -LiteralPath $LastExperimentFile) {
        return (Get-Content -Raw -LiteralPath $LastExperimentFile).Trim()
    }
    return "1"
}

function Invoke-Scan([uint64]$Pages = 10000, [uint32]$Threads = 4, [uint64]$Seed = 42, [string]$Mode = "words") {
    $result = Invoke-JsonEngine @("scan", "--mode", $Mode, "--pages", $Pages, "--threads", $Threads, "--seed", $Seed, "--db", $Database, "--dictionary", $Dictionary, "--json")
    Write-ScanReport $result
    Set-Content -LiteralPath $LastExperimentFile -Value $result.experiment -NoNewline
    Write-Host "Последний эксперимент: $($result.experiment)" -ForegroundColor DarkGray
}

function Invoke-CustomScan {
    $pagesText = Read-Host "Сколько страниц просканировать"
    [uint64]$pages = 0
    if (-not [uint64]::TryParse($pagesText, [ref]$pages) -or $pages -lt 1 -or $pages -gt 1000000000) {
        Write-Host "Нужно целое число от 1 до 1 000 000 000." -ForegroundColor Red
        return
    }
    Write-Host "  1. Слова"
    Write-Host "  2. Одиночные предложения"
    Write-Host "  3. Проходы из нескольких предложений"
    $modeChoice = Read-Host "Режим поиска (по умолчанию 1)"
    $mode = switch ($modeChoice) { "2" { "sentences" } "3" { "passages" } default { "words" } }
    $threadsText = Read-Host "Количество потоков (по умолчанию 4)"
    [uint32]$threads = 4
    if ($threadsText -and (-not [uint32]::TryParse($threadsText, [ref]$threads) -or $threads -lt 1 -or $threads -gt 256)) {
        Write-Host "Количество потоков должно быть от 1 до 256." -ForegroundColor Red
        return
    }
    Invoke-Scan $pages $threads 42 $mode
}

function Show-Menu {
    while ($true) {
        Write-Host ""
        Write-Host "BABEL MEANING MINER — локальный запуск" -ForegroundColor Cyan
        Write-Host "  1. Быстрый тест (1 000 страниц)"
        Write-Host "  2. Обычное сканирование (10 000 страниц)"
        Write-Host "  3. Показать лучшие находки"
        Write-Host "  4. Показать статистику"
        Write-Host "  5. Сгенерировать страницу"
        Write-Host "  6. Запустить тесты"
        Write-Host "  7. Поиск проходов (10 000 страниц)"
        Write-Host "  8. Найденные проходы"
        Write-Host "  9. Оценить шансы обнаружения"
        Write-Host " 10. Найденные слова и частота"
        Write-Host " 11. Найденные одиночные предложения и частота"
        Write-Host " 12. Кастомный поиск по числу страниц"
        Write-Host " 13. Совпадения из двух и более слов"
        Write-Host "  0. Выход"
        $choice = Read-Host "Выбери действие"
        switch ($choice) {
            "1" { Invoke-Scan 1000 4 42 }
            "2" { Invoke-Scan 10000 4 42 }
            "3" { Write-Findings @(Invoke-JsonEngine @("findings", "--experiment", (Get-Experiment), "--limit", "20", "--db", $Database)) }
            "4" { Write-Stats (Invoke-JsonEngine @("stats", "--experiment", (Get-Experiment), "--db", $Database)) }
            "5" { $page = Read-Host "Номер страницы (по умолчанию 0)"; if (-not $page) { $page = "0" }; Write-GeneratedPage (Invoke-JsonEngine @("generate", "--seed", "42", "--page", $page)) }
            "6" { Invoke-Tests }
            "7" { Invoke-Passages 10000 }
            "8" { Write-Passages @(Invoke-JsonEngine @("passages", "--experiment", (Get-Experiment), "--db", $Database)) }
            "9" { Write-Feasibility (Invoke-JsonEngine @("feasibility", "--pages", "1000000000", "--dictionary", $Dictionary)) }
            "10" { Write-Occurrences @(Invoke-JsonEngine @("occurrences", "--type", "word", "--experiment", (Get-Experiment), "--limit", "100", "--db", $Database)) "word" }
            "11" { Write-Occurrences @(Invoke-JsonEngine @("occurrences", "--type", "sentence", "--experiment", (Get-Experiment), "--limit", "100", "--db", $Database)) "sentence" }
            "12" { Invoke-CustomScan }
            "13" { Write-Occurrences @(Invoke-JsonEngine @("occurrences", "--type", "phrase", "--experiment", (Get-Experiment), "--limit", "100", "--db", $Database)) "phrase" }
            "0" { return }
            default { Write-Host "Неизвестный пункт." -ForegroundColor Red }
        }
    }
}

function Invoke-Passages([uint64]$Pages = 10000) {
    $result = Invoke-JsonEngine @("scan", "--mode", "passages", "--pages", $Pages, "--threads", "4", "--db", $Database, "--dictionary", $Dictionary, "--json")
    Write-ScanReport $result
    Set-Content -LiteralPath $LastExperimentFile -Value $result.experiment -NoNewline
    Write-Host "Последний эксперимент: $($result.experiment)" -ForegroundColor DarkGray
}
$alias = $Command.ToLowerInvariant()
switch ($alias) {
    { $_ -in @("menu", "start", "run") } { Show-Menu; break }
    { $_ -in @("quick", "q") } { Invoke-Scan 1000 4 42; break }
    { $_ -in @("passages", "pp") } { Invoke-Passages $(if ($Arguments.Count) { [uint64]$Arguments[0] } else { 10000 }); break }
    { $_ -in @("pass", "lp") } { Write-Passages @(Invoke-JsonEngine @("passages", "--experiment", (Get-Experiment), "--db", $Database)); break }
    { $_ -in @("score-passage", "sp") } { Write-PassageScore (Invoke-JsonEngine @("score-passage", "--text", ($Arguments -join " "))); break }
    { $_ -in @("chance", "feasibility") } { Write-Feasibility (Invoke-JsonEngine @("feasibility", "--pages", $(if ($Arguments.Count) { $Arguments[0] } else { "1000000000" }), "--dictionary", $Dictionary)); break }
    { $_ -in @("evaluate", "eval") } { Write-Evaluation (Invoke-JsonEngine @("evaluate", "--mode", "injection", "--dictionary", $Dictionary)); break }
    { $_ -in @("found-words", "words", "fw") } { Write-Occurrences @(Invoke-JsonEngine @("occurrences", "--type", "word", "--experiment", (Get-Experiment), "--limit", "100", "--db", $Database)) "word"; break }
    { $_ -in @("found-sentences", "fs") } { Write-Occurrences @(Invoke-JsonEngine @("occurrences", "--type", "sentence", "--experiment", (Get-Experiment), "--limit", "100", "--db", $Database)) "sentence"; break }
    { $_ -in @("found-phrases", "phrases", "fp") } { Write-Occurrences @(Invoke-JsonEngine @("occurrences", "--type", "phrase", "--experiment", (Get-Experiment), "--limit", "100", "--db", $Database)) "phrase"; break }
    { $_ -in @("custom", "c") } { Invoke-CustomScan; break }
    { $_ -in @("scan", "s") } {
        $pages = if ($Arguments.Count -ge 1) { [uint64]$Arguments[0] } else { 10000 }
        $threads = if ($Arguments.Count -ge 2) { [uint32]$Arguments[1] } else { 4 }
        $seed = if ($Arguments.Count -ge 3) { [uint64]$Arguments[2] } else { 42 }
        Invoke-Scan $pages $threads $seed
        break
    }
    { $_ -in @("sentences", "sentence", "ss") } {
        $pages = if ($Arguments.Count -ge 1) { [uint64]$Arguments[0] } else { 10000 }
        $threads = if ($Arguments.Count -ge 2) { [uint32]$Arguments[1] } else { 4 }
        $result = Invoke-JsonEngine @("scan", "--mode", "sentences", "--pages", $pages, "--threads", $threads, "--seed", "42", "--db", $Database, "--dictionary", $Dictionary, "--json")
        Write-ScanReport $result
        Set-Content -LiteralPath $LastExperimentFile -Value $result.experiment -NoNewline
        Write-Host "Последний эксперимент: $($result.experiment)" -ForegroundColor DarkGray
        break
    }
    { $_ -in @("score", "sc") } {
        if (-not $Arguments.Count) { throw "Передай текст в кавычках: .\bmm score 'the dog sees a cat'" }
        Invoke-Engine @("score-text", "--text", ($Arguments -join " "))
        break
    }
    { $_ -in @("page", "p", "generate", "gen") } {
        $page = if ($Arguments.Count) { $Arguments[0] } else { "0" }
        Write-GeneratedPage (Invoke-JsonEngine @("generate", "--seed", "42", "--page", $page))
        break
    }
    { $_ -in @("find", "f", "findings") } {
        $id = Get-Experiment $(if ($Arguments.Count) { $Arguments[0] } else { $null })
        $limit = if ($Arguments.Count -ge 2) { $Arguments[1] } else { "20" }
        Write-Findings @(Invoke-JsonEngine @("findings", "--experiment", $id, "--limit", $limit, "--db", $Database))
        break
    }
    { $_ -in @("stats", "st") } {
        $id = Get-Experiment $(if ($Arguments.Count) { $Arguments[0] } else { $null })
        Write-Stats (Invoke-JsonEngine @("stats", "--experiment", $id, "--db", $Database))
        break
    }
    { $_ -in @("resume", "r") } {
        $id = Get-Experiment $(if ($Arguments.Count) { $Arguments[0] } else { $null })
        $pages = if ($Arguments.Count -ge 2) { $Arguments[1] } else { "10000" }
        Write-ScanReport (Invoke-JsonEngine @("resume", "--experiment", $id, "--pages", $pages, "--threads", "4", "--db", $Database, "--json"))
        break
    }
    { $_ -in @("export", "x") } {
        $id = Get-Experiment $(if ($Arguments.Count) { $Arguments[0] } else { $null })
        $format = if ($Arguments.Count -ge 2) { $Arguments[1] } else { "json" }
        $output = Join-Path $ProjectRoot "findings.$format"
        Invoke-Engine @("export", "--experiment", $id, "--format", $format, "--output", $output, "--db", $Database)
        break
    }
    { $_ -in @("build", "b") } { Ensure-Build; & (Find-CMake) --build (Join-Path $ProjectRoot "build") --config Release --parallel; break }
    { $_ -in @("test", "t") } { Invoke-Tests; break }
    { $_ -in @("bench", "benchmark") } { Invoke-Engine @("benchmark", "--pages", $(if ($Arguments.Count) { $Arguments[0] } else { "10000" }), "--dictionary", $Dictionary); break }
    { $_ -in @("help", "h", "--help", "-h") } {
        Write-Host @"
Короткие команды BMM:
  .\bmm                 интерактивное меню
  .\bmm quick           быстрый тест на 1 000 страниц
  .\bmm scan            сканировать 10 000 страниц, 4 потока, seed 42
  .\bmm scan 50000 8    страницы и потоки можно передать позиционно
  .\bmm sentences 10000 сканировать предложения
  .\bmm passages 10000  искать последовательные предложения
  .\bmm pass            показать проходы последнего эксперимента
  .\bmm sp "the cat sleeps. the cat wakes."  оценить связность
  .\bmm chance          оценить статистическую редкость
  .\bmm eval            проверить синтетические внедрения
  .\bmm words           найденные слова и число повторений
  .\bmm fs              найденные одиночные предложения и число повторений
  .\bmm phrases         совпадения из двух и более соседних слов
  .\bmm custom          выбрать режим и ввести своё число страниц
  .\bmm score "the dog sees a cat"  проверить произвольный текст
  .\bmm page 100        сгенерировать страницу
  .\bmm find            находки последнего эксперимента
  .\bmm stats           статистика последнего эксперимента
  .\bmm resume          продолжить последний эксперимент
  .\bmm export          экспорт последнего эксперимента в JSON
  .\bmm test            собрать и запустить тесты
  .\bmm bench 10000     запустить бенчмарк

Есть короткие сокращения: q, s, ss, fw, fs, fp, c, sc, p, f, st, r, x, b, t.
"@
        break
    }
    default { throw "Неизвестная команда '$Command'. Выполни '.\bmm help'." }
}
