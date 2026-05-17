param(
  [int[]]$Players = @(2,3,4,5,6,7,8),
  [long]$Moves = 1000000,
  [long[]]$CellTargets = @(10000,40000,160000,640000,1000000),
  [int]$CellPx = 1
)

$ErrorActionPreference = 'Stop'

foreach ($p in $Players) {
  Write-Host "Simulating n=$p moves=$Moves"
  & .\n_knights.exe simulate --players $p --moves $Moves

  $state = "outputs/n_knights_$p/states/state_${Moves}.json"

  foreach ($ct in $CellTargets) {
    $side = [int][Math]::Floor([Math]::Sqrt([double]$ct))
    if ($side -lt 1) { continue }
    $half = [int][Math]::Floor($side / 2)
    $xmin = -$half
    $ymin = -$half
    $xmax = $xmin + $side - 1
    $ymax = $ymin + $side - 1

    Write-Host "Render n=$p cells~$($side*$side)"
    & .\n_knights.exe render --state $state --xmin $xmin --xmax $xmax --ymin $ymin --ymax $ymax --cell $CellPx
  }
}