<?php
// btc-slot-machine: Test all available filter sizes in one request
header('Content-Type: application/json');

$startTime = microtime(true);

$hex = $_GET['address_hex'] ?? '';

if (strlen($hex) !== 40 || !ctype_xdigit($hex)) {
    die(json_encode(['error' => 'Please provide a valid 40-char Hash160 hex string']));
}

$bytes = hex2bin($hex);

// 64-bit FNV-1a hash (same as index.php)
$hash   = gmp_init("14695981039346656037"); // 0xCBF29CE484222325
$prime  = gmp_init("1099511628211");         // 0x00000100000001B3
$mask64 = gmp_init("18446744073709551615");  // 0xFFFFFFFFFFFFFFFF

for ($i = 0; $i < 20; $i++) {
    $hash = gmp_xor($hash, ord($bytes[$i]));
    $hash = gmp_and(gmp_mul($hash, $prime), $mask64);
}

$tabS = 6;
$tabM = 63;

$allFilters = [
    16384 => 37,
    1024  => 33,
    256   => 31,
];

$results = [];

foreach ($allFilters as $mb => $bits) {
    $path = __DIR__ . '/filter/' . $mb . 'mb.bin';
    if (!file_exists($path)) continue;

    $t = microtime(true);

    $h_trunc = gmp_intval(gmp_and($hash, gmp_sub(gmp_pow("2", $bits), "1")));
    $byteOffset = ($h_trunc >> $tabS) * 8;

    $f = fopen($path, 'rb');
    fseek($f, $byteOffset);
    $chunkData = fread($f, 8);
    fclose($f);

    if ($chunkData === false || strlen($chunkData) !== 8) {
        $results[] = ['filter' => $mb . 'mb.bin', 'error' => 'read error'];
        continue;
    }

    $chunk  = unpack('P', $chunkData)[1];
    $bitPos = 1 << ($h_trunc & $tabM);
    $found  = ($chunk & $bitPos) !== 0;

    $results[] = [
        'filter'  => $mb . 'mb.bin',
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
