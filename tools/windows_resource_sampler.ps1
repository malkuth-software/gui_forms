# Samples a Windows runner's memory, paging and busiest processes while the
# native job builds and tests, as evidence for the build's silent stretches
# (projects desk WINDOWS_CI_30MIN.md). It observes only: it never changes the
# build, and a failed sample writes an `error` row and the loop goes on.
#
#   powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tools/windows_resource_sampler.ps1 `
#       -Log .ci/windows-resources.tsv -Stop .ci/windows-resources.stop [-IntervalSeconds 30] [-MaximumMinutes 130]
#
# Lifetime: the caller starts it in the background and creates the -Stop file
# when its step ends (and kills it if it has not gone two seconds later). The
# sampler checks for that file every second and exits when it appears, and it
# exits by itself after -MaximumMinutes, so it outlives no job.
#
# One tab-separated row per sample, after a header row:
#   time_utc, epoch_ms                 when the sample was taken
#   available_mb                       free physical memory (\Memory\Available MBytes)
#   committed_mb, commit_limit_mb      commit charge and its limit (RAM plus page file)
#   page_faults_per_s, pages_per_s     all faults, and hard faults' pages read or written
#   cpu_percent                        all processors, over the counter's interval
#   disk_percent                       \PhysicalDisk(_Total)\% Disk Time
#   top_working_set                    the 6 largest working sets: name:pid:MB, comma-separated
#   top_cpu                            the 6 processes with the most processor seconds since the
#                                      previous sample: name:pid:seconds
param(
    [Parameter(Mandatory = $true)][string]$Log,
    [Parameter(Mandatory = $true)][string]$Stop,
    [int]$IntervalSeconds = 30,
    [int]$MaximumMinutes = 130
)

$ErrorActionPreference = 'Stop'
$counterPaths = @(
    '\Memory\Available MBytes',
    '\Memory\Committed Bytes',
    '\Memory\Commit Limit',
    '\Memory\Page Faults/sec',
    '\Memory\Pages/sec',
    '\Processor(_Total)\% Processor Time',
    '\PhysicalDisk(_Total)\% Disk Time'
)
$topCount = 6
$header = "time_utc`tepoch_ms`tavailable_mb`tcommitted_mb`tcommit_limit_mb`tpage_faults_per_s`tpages_per_s`tcpu_percent`tdisk_percent`ttop_working_set`ttop_cpu"
$logDirectory = Split-Path -Parent $Log
if ($logDirectory -and -not (Test-Path $logDirectory)) {
    New-Item -ItemType Directory -Path $logDirectory | Out-Null
}
Set-Content -Path $Log -Value $header -Encoding UTF8

# Processor seconds by process id at the previous sample, for the top_cpu deltas.
$previousSeconds = @{}
$deadline = (Get-Date).AddMinutes($MaximumMinutes)

function Read-CounterValue($samples, [string]$suffix) {
    foreach ($sample in $samples) {
        if ($sample.Path.EndsWith($suffix, [System.StringComparison]::OrdinalIgnoreCase)) {
            return [double]$sample.CookedValue
        }
    }
    return [double]::NaN
}

while ((Get-Date) -lt $deadline -and -not (Test-Path $Stop)) {
    $now = [DateTimeOffset]::UtcNow
    $row = ''
    try {
        $samples = (Get-Counter -Counter $counterPaths -SampleInterval 1 -MaxSamples 1).CounterSamples
        $available = Read-CounterValue $samples '\memory\available mbytes'
        $committed = (Read-CounterValue $samples '\memory\committed bytes') / 1MB
        $limit = (Read-CounterValue $samples '\memory\commit limit') / 1MB
        $faults = Read-CounterValue $samples '\memory\page faults/sec'
        $pages = Read-CounterValue $samples '\memory\pages/sec'
        $cpu = Read-CounterValue $samples '\processor(_total)\% processor time'
        $disk = Read-CounterValue $samples '\physicaldisk(_total)\% disk time'

        $processes = Get-Process
        $byWorkingSet = $processes | Sort-Object -Property WorkingSet64 -Descending | Select-Object -First $topCount
        $workingSetText = ($byWorkingSet | ForEach-Object {
            '{0}:{1}:{2:F0}' -f $_.ProcessName, $_.Id, ($_.WorkingSet64 / 1MB)
        }) -join ','

        $currentSeconds = @{}
        $deltas = @()
        foreach ($process in $processes) {
            # A protected process (System, the antivirus service) may refuse
            # its times; it then counts as 0 rather than failing the sample.
            $seconds = 0.0
            try {
                if ($null -ne $process.TotalProcessorTime) {
                    $seconds = $process.TotalProcessorTime.TotalSeconds
                }
            }
            catch {
                $seconds = 0.0
            }
            $currentSeconds[$process.Id] = $seconds
            $before = 0.0
            if ($previousSeconds.ContainsKey($process.Id)) {
                $before = $previousSeconds[$process.Id]
            }
            $deltas += [pscustomobject]@{ Name = $process.ProcessName; Id = $process.Id; Seconds = ($seconds - $before) }
        }
        $previousSeconds = $currentSeconds
        $cpuText = ($deltas | Sort-Object -Property Seconds -Descending | Select-Object -First $topCount | ForEach-Object {
            '{0}:{1}:{2:F1}' -f $_.Name, $_.Id, $_.Seconds
        }) -join ','

        $row = "{0}`t{1}`t{2:F0}`t{3:F0}`t{4:F0}`t{5:F0}`t{6:F0}`t{7:F0}`t{8:F0}`t{9}`t{10}" -f `
            $now.ToString('o'), $now.ToUnixTimeMilliseconds(), $available, $committed, $limit, $faults, $pages,
            $cpu, $disk, $workingSetText, $cpuText
    }
    catch {
        $message = ($_.Exception.Message -replace '\s+', ' ')
        $row = "{0}`t{1}`terror`t`t`t`t`t`t`t`t{2}" -f $now.ToString('o'), $now.ToUnixTimeMilliseconds(), $message
    }
    Add-Content -Path $Log -Value $row -Encoding UTF8

    # Wait out the interval a second at a time, so the stop file ends the
    # sampler within about a second of the step's end.
    $waited = 0
    while ($waited -lt $IntervalSeconds -and -not (Test-Path $Stop)) {
        Start-Sleep -Seconds 1
        $waited += 1
    }
}
