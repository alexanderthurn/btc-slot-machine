<?php
// btc-slot-machine: Minimal O(1) PHP Filter Tester
// Supports single lookup (?address_hex=...) and batch lookup (POST JSON body).
// Upload this file and the 'filter/' directory to your web server.

header('Content-Type: application/json');

$startTime = microtime(true);

// ── Find largest available filter ────────────────────────────────────────────
$availableFilters = [
    2048 => 34,
    1024 => 33,
    512  => 32,
    256  => 31,
    128  => 30,
    64   => 29,
    32   => 28
];

$filterFile    = null;
$targetBitsExp = null;

foreach ($availableFilters as $mb => $bits) {
    $checkPath = __DIR__ . '/filter/' . $mb . 'mb.bin';
    if (file_exists($checkPath)) {
        $filterFile    = $checkPath;
        $targetBitsExp = $bits;
        break;
    }
}

if (!$filterFile) {
    die(json_encode(['error' => 'No filter files found in the filter/ directory!']));
}

// ── FNV-1a lookup (O(1) disk seek) ───────────────────────────────────────────
$prime  = gmp_init("1099511628211");         // 0x00000100000001B3
$mask64 = gmp_init("18446744073709551615");  // 0xFFFFFFFFFFFFFFFF
$maskBitsGmp = gmp_sub(gmp_pow("2", $targetBitsExp), "1");
$tabS = 6;
$tabM = 63;

$fh = fopen($filterFile, 'rb');

function lookupHex($hex) {
    global $fh, $prime, $mask64, $maskBitsGmp, $tabS, $tabM;

    $bytes = hex2bin($hex);
    $hash  = gmp_init("14695981039346656037"); // FNV offset basis

    for ($i = 0; $i < 20; $i++) {
        $hash = gmp_xor($hash, ord($bytes[$i]));
        $hash = gmp_and(gmp_mul($hash, $prime), $mask64);
    }

    $h_trunc    = gmp_intval(gmp_and($hash, $maskBitsGmp));
    $byteOffset = ($h_trunc >> $tabS) * 8;

    fseek($fh, $byteOffset);
    $chunkData = fread($fh, 8);

    if ($chunkData === false || strlen($chunkData) !== 8) return null;

    $chunk  = unpack('P', $chunkData)[1];
    $bitPos = 1 << ($h_trunc & $tabM);

    return ($chunk & $bitPos) !== 0;
}

// ── Resolve input: batch POST JSON or single GET param ───────────────────────
$body = file_get_contents('php://input');
$json = $body ? json_decode($body, true) : null;

if ($json && isset($json['addresses']) && is_array($json['addresses'])) {
    // Batch mode
    $results = [];
    foreach ($json['addresses'] as $hex) {
        if (strlen($hex) !== 40 || !ctype_xdigit($hex)) {
            $results[$hex] = null; // invalid
            continue;
        }
        $results[$hex] = lookupHex($hex);
    }

    fclose($fh);
    echo json_encode([
        'filter_used' => basename($filterFile),
        'results'     => $results,
        'time_php_ms' => round((microtime(true) - $startTime) * 1000, 3)
    ]);

} else {
    // Single mode (backward compat)
    $hex = $_GET['address_hex'] ?? '';

    if (strlen($hex) !== 40 || !ctype_xdigit($hex)) {
        fclose($fh);
        die(json_encode(['error' => 'Provide a valid 40-char Hash160 hex string (?address_hex=...)']));
    }

    $found = lookupHex($hex);
    fclose($fh);

    echo json_encode([
        'address_hex' => $hex,
        'filter_used' => basename($filterFile),
        'found'       => $found,
        'time_php_ms' => round((microtime(true) - $startTime) * 1000, 3)
    ]);
}
?>
