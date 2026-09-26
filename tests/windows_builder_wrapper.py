from pathlib import Path

p = Path(__file__).resolve().parents[1] / "Build-Windows-Docker.ps1"
s = p.read_text(encoding="utf-8")
checks = [
    ("windows-artifacts target", "--target windows-artifacts" in s),
    ("compile/export output", "--output \"type=local,dest=$out\"" in s),
    ("temporary Continue preference", "$ErrorActionPreference = \"Continue\"" in s),
    ("restores preference", "$ErrorActionPreference = $savedErrorActionPreference" in s),
    ("checks Docker exit code", "$code = $LASTEXITCODE" in s and "if ($code -ne 0)" in s),
    ("stringifies ErrorRecord", "[System.Management.Automation.ErrorRecord]" in s),
    ("automatic log append", "Add-Content -LiteralPath $log" in s),
    ("no Wine target", "--target native-verified" not in s and "wine_smoke" not in s),
]
failed = [name for name, ok in checks if not ok]
if failed:
    raise SystemExit("FAIL: " + ", ".join(failed))
print(f"PASS: {len(checks)} Windows builder wrapper checks")
