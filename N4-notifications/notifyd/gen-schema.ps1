# schema.sql -> schema.inc (one C string literal per line), so the SQL lives in one file.
$lines = Get-Content -Encoding UTF8 (Join-Path $PSScriptRoot 'schema.sql')
$out = foreach ($l in $lines) { '"' + $l.Replace('\', '\').Replace('"', '\"') + '\n"' }
Set-Content -Encoding ASCII -Path (Join-Path $PSScriptRoot 'schema.inc') -Value $out
