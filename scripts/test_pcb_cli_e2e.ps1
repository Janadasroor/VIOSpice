# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0
#
# Comprehensive End-to-End Test Suite for VioraEDA PCB CLI Commands

$ErrorActionPreference = "Stop"
$env:PATH = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:PATH
$env:VIORA_NO_DAEMON = "1"

$VioraExe = "C:\VioraEDA\build\viora.exe"
$OutputDir = "C:\VioraEDA\build\test_pcb_e2e_artifacts"

if (-not (Test-Path $VioraExe)) {
    Write-Error "viora.exe not found at $VioraExe"
    exit 1
}

if (Test-Path $OutputDir) {
    Remove-Item -Recurse -Force $OutputDir
}
New-Item -ItemType Directory -Path $OutputDir | Out-Null

$results = [System.Collections.Generic.List[PSCustomObject]]::new()

function Run-PcbStep {
    param(
        [string]$StepName,
        [scriptblock]$CommandBlock
    )
    Write-Host -NoNewline "  - $StepName ... "
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        $rawOutput = & $CommandBlock
        $sw.Stop()
        $durationMs = [Math]::Round($sw.Elapsed.TotalMilliseconds, 1)

        $jsonObj = $null
        try {
            $jsonObj = $rawOutput | ConvertFrom-Json
        } catch {
            $jsonObj = $rawOutput
        }

        Write-Host -ForegroundColor Green "PASS (${durationMs} ms)"
        $results.Add([PSCustomObject]@{
            Step = $StepName
            Status = "PASS"
            DurationMs = $durationMs
            Details = "Success"
        })
        return $jsonObj
    } catch {
        $sw.Stop()
        $durationMs = [Math]::Round($sw.Elapsed.TotalMilliseconds, 1)
        Write-Host -ForegroundColor Red "FAIL (${durationMs} ms)"
        Write-Host "    Error: $_"
        $results.Add([PSCustomObject]@{
            Step = $StepName
            Status = "FAIL"
            DurationMs = $durationMs
            Details = "$_"
        })
        throw $_
    }
}

Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "         VioraEDA PCB CLI End-to-End Automated Test Suite        " -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan

# -------------------------------------------------------------------------
# Phase 1: Footprint Discovery & Search
# -------------------------------------------------------------------------
Write-Host "`n[Phase 1: Footprint Database & Discovery]" -ForegroundColor Yellow
Run-PcbStep "1.1 List Standard Footprints" {
    & $VioraExe footprint-list --limit 10 --json
}

# -------------------------------------------------------------------------
# Phase 2: Layer Stackup & Dielectric Impedance Analysis
# -------------------------------------------------------------------------
Write-Host "`n[Phase 2: Layer Stackup & Impedance Calculator]" -ForegroundColor Yellow
Run-PcbStep "2.1 Compute 4-Layer Microstrip & Differential Impedance" {
    & $VioraExe pcb-stackup --layers 4 --width 0.2 --space 0.15 --thick 0.2 --json
}

# -------------------------------------------------------------------------
# Phase 3: PCB Initialization (Standalone & Schematic-Assisted)
# -------------------------------------------------------------------------
Write-Host "`n[Phase 3: PCB Layout Initialization]" -ForegroundColor Yellow
$boardStandalone = Join-Path $OutputDir "standalone_board.pcb"
Run-PcbStep "3.1 Initialize Standalone 4-Layer Board (100x80mm)" {
    & $VioraExe pcb-init $boardStandalone --width 100 --height 80 --layers 4 --json
}

# Synthesize a schematic to test schematic-to-PCB ECO layout synchronization
$cirPath = Join-Path $OutputDir "power_stage.cir"
$flxschPath = Join-Path $OutputDir "power_stage.flxsch"
$boardFromSch = Join-Path $OutputDir "power_stage.pcb"

$cirContent = @"
* Active Power Stage Netlist
V1 VIN 0 DC 12
R1 VIN GATE 10k
R2 GATE 0 10k
C1 VIN 0 100uF
C2 VOUT 0 22uF
.end
"@
[System.IO.File]::WriteAllText($cirPath, $cirContent)

Run-PcbStep "3.2 Synthesize Schematic from Netlist" {
    & $VioraExe netlist-to-schematic $cirPath --out $flxschPath --json
}

Run-PcbStep "3.3 Initialize PCB from Schematic with ECO Placement" {
    & $VioraExe pcb-init $boardFromSch --schematic $flxschPath --json
}

# -------------------------------------------------------------------------
# Phase 4: Composition & Layout Injection (pcb-compose)
# -------------------------------------------------------------------------
Write-Host "`n[Phase 4: Programmatic PCB Composition]" -ForegroundColor Yellow
Run-PcbStep "4.1 Inject Components, Netclasses, Traces, Vias & Pours" {
    & $VioraExe pcb-compose $boardStandalone `
        --add-netclass "name=POWER,width=0.5,clearance=0.25" `
        --assign-net "net=VIN,class=POWER" `
        --add-component "name=U1,footprint=DIP-8,x=50,y=40,layer=Top,value=NE555" `
        --add-component "name=R1,footprint=R_0805,x=30,y=30,layer=Top,value=10k" `
        --add-component "name=C1,footprint=C_0603,x=70,y=30,layer=Top,value=100n" `
        --add-trace "x1=30,y1=30,x2=50,y2=40,width=0.3,layer=Top,net=VIN" `
        --add-via "x=50,y=35,diameter=0.8,drill=0.4,startlayer=Top,endlayer=Bottom,net=VIN" `
        --add-pour "layer=Bottom,net=GND,clearance=0.3" `
        --json
}

# -------------------------------------------------------------------------
# Phase 5: Querying & Netlist Extraction
# -------------------------------------------------------------------------
Write-Host "`n[Phase 5: Layout Inspection & Netlist Extraction]" -ForegroundColor Yellow
Run-PcbStep "5.1 Query Board Statistics & Components" {
    & $VioraExe pcb-query $boardStandalone --json
}

Run-PcbStep "5.2 Dump Board Netlist & Connectivity" {
    & $VioraExe pcb-netlist $boardStandalone --json
}

# -------------------------------------------------------------------------
# Phase 6: High-Speed Serpentine & RF Via Fencing
# -------------------------------------------------------------------------
Write-Host "`n[Phase 6: High-Speed & RF Processing]" -ForegroundColor Yellow
$boardTeardrop = Join-Path $OutputDir "board_teardrops.pcb"
Run-PcbStep "6.1 Generate Stress-Relief Teardrops on Pads and Vias" {
    & $VioraExe pcb-teardrops $boardStandalone --shape curved -o $boardTeardrop --json
}

$boardFenced = Join-Path $OutputDir "board_fenced.pcb"
Run-PcbStep "6.2 Generate RF Ground Via Fencing" {
    & $VioraExe pcb-via-fence $boardTeardrop --net GND --pitch 2.5 --offset 1.5 -o $boardFenced --json
}

# -------------------------------------------------------------------------
# Phase 7: Geometric Cleanup & Board Shrinking
# -------------------------------------------------------------------------
Write-Host "`n[Phase 7: Board Cleanup & Boundary Shrinking]" -ForegroundColor Yellow
$boardClean = Join-Path $OutputDir "board_cleaned.pcb"
Run-PcbStep "7.1 Automated Geometric Cleanup (Purge Stubs & Vias)" {
    & $VioraExe pcb-cleanup $boardFenced -o $boardClean --json
}

Run-PcbStep "7.2 Shrink Board Outline (EdgeCuts) to Fit Components" {
    & $VioraExe pcb-shrink $boardClean --margin 5.0 --json
}

# -------------------------------------------------------------------------
# Phase 8: Design Rule Checks (DRC) & Validation
# -------------------------------------------------------------------------
Write-Host "`n[Phase 8: Design Rule Verification (DRC)]" -ForegroundColor Yellow
Run-PcbStep "8.1 Run Automated DRC Check" {
    & $VioraExe pcb-drc $boardClean --clearance 0.15 --min-width 0.15 --min-drill 0.2 --json
}

Run-PcbStep "8.2 Full PCB Validation" {
    & $VioraExe pcb-validate $boardClean --json
}

# -------------------------------------------------------------------------
# Phase 9: High-Resolution Rendering
# -------------------------------------------------------------------------
Write-Host "`n[Phase 9: High-Resolution Visual Rendering]" -ForegroundColor Yellow
$pngFab = Join-Path $OutputDir "board_fab.png"
Run-PcbStep "9.1 Render Fabrication Image (PNG)" {
    & $VioraExe pcb-render $boardClean $pngFab --mode fab --labels --grid --json
}

$pngCopper = Join-Path $OutputDir "board_copper.png"
Run-PcbStep "9.2 Render Copper Visualization (PNG)" {
    & $VioraExe pcb-render $boardClean $pngCopper --mode copper --scale 2.0 --json
}

# -------------------------------------------------------------------------
# Phase 10: Manufacturing Exports (Gerber, Drill, POS, STEP, PDF, IPC-2581)
# -------------------------------------------------------------------------
Write-Host "`n[Phase 10: Multi-Format Manufacturing Exports]" -ForegroundColor Yellow
$gerberDir = Join-Path $OutputDir "gerbers"
Run-PcbStep "10.1 Export RS-274X Gerbers & Excellon Drill (.drl)" {
    & $VioraExe pcb-export $boardClean -f gerber -o $gerberDir --json
}

$posFile = Join-Path $OutputDir "board_pos.csv"
Run-PcbStep "10.2 Export Pick-and-Place Centroid Table (.pos)" {
    & $VioraExe pcb-export $boardClean -f pos -o $posFile --json
}

$pdfDir = Join-Path $OutputDir "board_pdf"
Run-PcbStep "10.3 Export Multi-Layer Fabrication PDF Documentation" {
    & $VioraExe pcb-export $boardClean -f pdf -o $pdfDir --json
}

$stepFile = Join-Path $OutputDir "board_3d.step"
Run-PcbStep "10.4 Export 3D MCAD STEP Solid Model" {
    & $VioraExe pcb-export $boardClean -f step -o $stepFile --json
}

$ipcFile = Join-Path $OutputDir "board_ipc2581.xml"
Run-PcbStep "10.5 Export IPC-2581 Manufacturing XML Package" {
    & $VioraExe pcb-export $boardClean -f ipc2581 -o $ipcFile --json
}

# -------------------------------------------------------------------------
# Summary & Verification Report
# -------------------------------------------------------------------------
Write-Host "`n================================================================" -ForegroundColor Cyan
Write-Host "                      E2E TEST SUMMARY                          " -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan

$totalTests = $results.Count
$passedTests = ($results | Where-Object { $_.Status -eq "PASS" }).Count
$totalDurationMs = ($results | Measure-Object -Property DurationMs -Sum).Sum

$results | Format-Table -AutoSize Step, Status, DurationMs

Write-Host "Total Tests Run : $totalTests"
Write-Host "Passed          : $passedTests" -ForegroundColor Green
Write-Host "Failed          : $($totalTests - $passedTests)" -ForegroundColor $(if ($totalTests -eq $passedTests) { "Green" } else { "Red" })
Write-Host "Total Execution : $([Math]::Round($totalDurationMs / 1000, 2)) seconds" -ForegroundColor Cyan

if ($passedTests -eq $totalTests) {
    Write-Host "`n>>> ALL PCB CLI END-TO-END TESTS PASSED SUCCESSFULLY! <<<`n" -ForegroundColor Green
    exit 0
} else {
    Write-Host "`n>>> PCB CLI E2E TESTS FAILED! <<<`n" -ForegroundColor Red
    exit 1
}
