<?php
// btc-slot-machine: Test all available filter sizes in one request
header('Content-Type: application/json');

$startTime = microtime(true);

$hex = $_GET['address_hex'] ?? '';

if ((!in_array(strlen($hex), [40, 64])) || !ctype_xdigit($hex)) {
    die(json_encode(['error' => 'Please provide a 40-char Hash160 or 64-char 32-byte hex string']));
}

// Zero-pad to 32 bytes (64 hex chars) — Hash160 types get 12 zero bytes appended
$hex32  = str_pad($hex, 64, '0');
$bytes  = hex2bin($hex32);

// 64-bit FNV-1a hash over 32 bytes (same as C++ with keyBytes=32)
$hash   = gmp_init("14695981039346656037"); // 0xCBF29CE484222325
$prime  = gmp_init("1099511628211");         // 0x00000100000001B3
$mask64 = gmp_init("18446744073709551615");  // 0xFFFFFFFFFFFFFFFF

for ($i = 0; $i < 32; $i++) {
    $hash = gmp_xor($hash, ord($bytes[$i]));
    $hash = gmp_and(gmp_mul($hash, $prime), $mask64);
}

$tabS = 6;
$tabM = 63;

$allFilters = [
    ['file' => '16384mb.bin',    'bits' => 37, 'label' => 'all-addresses (32-byte key)'],
    ['file' => '16384mb_bal.bin','bits' => 37, 'label' => 'balance (32-byte key)'],
];

$results = [];

foreach ($allFilters as $entry) {
    $path = __DIR__ . '/filter/' . $entry['file'];
    if (!file_exists($path)) continue;

    $bits = $entry['bits'];
    $t = microtime(true);

    $h_trunc = gmp_intval(gmp_and($hash, gmp_sub(gmp_pow("2", $bits), "1")));
    $byteOffset = ($h_trunc >> $tabS) * 8;

    $f = fopen($path, 'rb');
    fseek($f, $byteOffset);
    $chunkData = fread($f, 8);
    fclose($f);

    if ($chunkData === false || strlen($chunkData) !== 8) {
        $results[] = ['filter' => $entry['file'], 'label' => $entry['label'], 'error' => 'read error'];
        continue;
    }

    $chunk  = unpack('P', $chunkData)[1];
    $bitPos = 1 << ($h_trunc & $tabM);
    $found  = ($chunk & $bitPos) !== 0;

    $results[] = [
        'filter'  => $entry['file'],
        'label'   => $entry['label'],
        'found'   => $found,
        'time_ms' => round((microtime(true) - $t) * 1000, 3),
    ];
}

if (!$results) {
    die(json_encode(['error' => 'No filter files found in filter/ directory']));
}

echo json_encode([
    'address_hex'   => $hex,
    'results'       => $results,
    'time_total_ms' => round((microtime(true) - $startTime) * 1000, 3),
]);
?>
