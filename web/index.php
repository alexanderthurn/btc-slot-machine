<?php
// btc-slot-machine: O(1) bloom filter lookup — binary response, no JSON, no GMP.
// Client sends [[row,col],...] as JSON. Response is 8 raw bytes (uint64 LE bitmask).
// Bit i of the bitmask = 1 if lookup[i] hit. Supports up to 64 lookups per request.

$filterFile = __DIR__ . '/filter/2048mb.bin';
if (!file_exists($filterFile)) {
    header('HTTP/1.1 503 Service Unavailable');
    exit;
}

$fh   = fopen($filterFile, 'rb');
$body = file_get_contents('php://input');
$json = json_decode($body, true);

if (!$json || !isset($json['lookups'])) {
    fclose($fh);
    header('HTTP/1.1 400 Bad Request');
    exit;
}

// Build uint64 bitmask: bit i set if lookup i matched the filter.
// PHP integers are 63-bit signed; bits 0–62 are safe with <<. Bit 63 handled via pack.
$lo = 0; // bits 0–31
$hi = 0; // bits 32–63
foreach ($json['lookups'] as $i => $item) {
    fseek($fh, (int)$item[0] * 8);
    $data = fread($fh, 8);
    if ($data === false || strlen($data) !== 8) continue;
    $chunk = unpack('P', $data)[1];
    if ((($chunk >> (int)$item[1]) & 1) === 1) {
        if ($i < 32) $lo |= (1 << $i);
        else         $hi |= (1 << ($i - 32));
    }
}

fclose($fh);
header('Content-Type: application/octet-stream');
echo pack('VV', $lo, $hi); // 8 bytes, two uint32 LE
?>
