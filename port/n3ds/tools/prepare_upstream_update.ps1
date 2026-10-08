<#
Fetch the original Halo3DS main branch and stage any new upstream commits in
their own worktree. This never resets, rebases, or changes local-integration.
Build and test the candidate before promoting it with git merge --ff-only.
#>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'

$repo = (& git rev-parse --show-toplevel).Trim()
if ($LASTEXITCODE -ne 0 -or !$repo) { throw 'Run this from the Halo3DS integration repository.' }
$repo = [IO.Path]::GetFullPath($repo)
$branch = (& git -C $repo branch --show-current).Trim()
if ($branch -ne 'local-integration') { throw 'Check out local-integration before preparing an upstream update.' }
if (@(& git -C $repo status --porcelain).Count) { throw 'Commit or set aside local changes before preparing an upstream update.' }

& git -C $repo fetch origin main
if ($LASTEXITCODE -ne 0) { throw 'Could not fetch the original GitHub main branch.' }
$upstream = (& git -C $repo rev-parse origin/main).Trim()
if ($LASTEXITCODE -ne 0 -or $upstream -notmatch '^[0-9a-f]{40}$') { throw 'origin/main is unavailable.' }
& git -C $repo merge-base --is-ancestor origin/main HEAD
if ($LASTEXITCODE -eq 0) {
    Write-Output "Already includes origin/main $($upstream.Substring(0,12)); local-integration was not changed."
    return
}
if ($LASTEXITCODE -ne 1) { throw 'Could not compare the integration and upstream histories.' }

$short = $upstream.Substring(0,12)
$name = "upstream-review-$short"
$parent = [IO.Path]::GetDirectoryName($repo)
$review = [IO.Path]::GetFullPath((Join-Path $parent "Halo3DS-Upstream-Review-$short"))
$prefix = [IO.Path]::GetFullPath($parent).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
if (!$review.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $review)) {
    throw 'The review worktree path is unsafe or already exists.'
}
& git -C $repo show-ref --verify --quiet "refs/heads/$name"
if ($LASTEXITCODE -eq 0) { throw "Review branch $name already exists; inspect it before another update." }
if ($LASTEXITCODE -ne 1) { throw 'Could not check the review branch name.' }

& git -C $repo worktree add -b $name $review HEAD
if ($LASTEXITCODE -ne 0) { throw 'Could not create the isolated review worktree.' }
& git -C $review merge --no-ff --no-edit origin/main
if ($LASTEXITCODE -ne 0) {
    Write-Output "Upstream merge needs conflict resolution in $review. local-integration is unchanged."
    exit 2
}
Write-Output "Review worktree: $review"
Write-Output "Review branch: $name"
Write-Output 'Build and test both 3DS launchers and setup there. After they pass, fast-forward local-integration to the review branch.'
