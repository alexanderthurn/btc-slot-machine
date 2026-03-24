<?php
// btc-slot-machine: Minimal O(1) PHP Filter Tester
// Upload this file and the 'filter/' directory to your ALL-INKL web server!

header('Content-Type: application/json');

$startTime = microtime(true);

// Accept the Hash160 (40-character Hex string) from the URL parameter
// Example: index.php?address_hex=59b9b40f93d4a8989e02773a153b54a273ad1736
$hex = $_GET['address_hex'] ?? '';

if (strlen($hex) !== 40 || !ctype_xdigit($hex)) {
    die(json_encode(['error' => 'Please provide a valid 40-char Hash160 hex string (e.g. ?address_hex=... )']));
}

$bytes = hex2bin($hex);

// 1. Calculate the 64-bit FNV-1a Hash
// (We use GMP here to prevent standard PHP floats/overflows on 64-bit math)
$hash = gmp_init("14695981039346656037"); // 0xCBF29CE484222325
$prime = gmp_init("1099511628211");      // 0x00000100000001B3
$mask64 = gmp_init("18446744073709551615"); // 0xFFFFFFFFFFFFFFFF

for ($i = 0; $i < 20; $i++) {
    $byte = ord($bytes[$i]);
    $hash = gmp_xor($hash, $byte);
    $hash = gmp_and(gmp_mul($hash, $prime), $mask64);
}

// 2. O(1) Direct Disk Access Strategy
// Find the largest available filter file to minimize false positives
$availableFilters = [
    2048 => 34,
    1024 => 33,
    512  => 32,
    256  => 31,
    128  => 30,
    64   => 29,
    32   => 28
];

$filterFile = null;
$targetBitsExp = null;

foreach ($availableFilters as $mb => $bits) {
    $checkPath = __DIR__ . '/filter/' . $mb . 'mb.bin';
    if (file_exists($checkPath)) {
        $filterFile = $checkPath;
        $targetBitsExp = $bits;
        break;
    }
}

if (!$filterFile) {
    die(json_encode(['error' => 'No filter files found in the filter/ directory!']));
}

$tabS = 6; 
$tabM = 63;

// Emulate C++ logic precisely:
// Mask the unused top bits while still in GMP space 
// to ensure the resulting number securely fits into a standard PHP integer (< 63 bits)
$maskBitsGmp = gmp_sub(gmp_pow("2", $targetBitsExp), "1");
$h_trunc_gmp = gmp_and($hash, $maskBitsGmp);

// Since $h_trunc is now safely capped (max 34 bits), 
// we can convert it to a blazing fast native PHP integer without clamping bugs.
$h_trunc = gmp_intval($h_trunc_gmp);

// Calculate exact byte offset on the SSD for the read command
$byteOffset = ($h_trunc >> $tabS) * 8; // 8 bytes per 64-bit chunk

// O(1) Direct I/O: Open file, jump exactly to the offset, read 8 bytes, close.
// (Consumes ~0 MB RAM regardless of filter size!)
$f = fopen($filterFile, 'rb');
fseek($f, $byteOffset);
$chunkData = fread($f, 8);
fclose($f);

if ($chunkData === false || strlen($chunkData) !== 8) {
    die(json_encode(['error' => 'File read error / unexpected EOF']));
}

$chunkArray = unpack('P', $chunkData); // P = standard 64-bit unsigned little-endian integer
$chunk = $chunkArray[1];

// Calculate exact bit index within the 8-byte chunk
$bitPos = 1 << ($h_trunc & $tabM);

// Check if the bit is true
$found = ($chunk & $bitPos) !== 0;

$durationMs = round((microtime(true) - $startTime) * 1000, 3);

// Output result JSON
echo json_encode([
    'address_hex' => $hex,
    'filter_used' => basename($filterFile),
    'found'       => $found,
    'time_php_ms' => $durationMs
]);
?>
