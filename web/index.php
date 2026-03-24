<?php
// btc-slot-machine: minimal bloom filter lookup.
// Request:  raw binary, 5 bytes per lookup — uint32 LE row + uint8 col.
// Response: 8 raw bytes — uint64 LE bitmask, bit i = lookup[i] matched.
// No JSON, no GMP, no file_exists. Up to 64 lookups per request.

$fh   = fopen(__DIR__ . '/filter/16384mb.bin', 'rb');
$body = file_get_contents('php://input');
$n    = (int)(strlen($body) / 5);

$lo = 0;
$hi = 0;
for ($i = 0; $i < $n; $i++) {
    $row = unpack('V', substr($body, $i * 5, 4))[1];
    $col = ord($body[$i * 5 + 4]);
    fseek($fh, $row * 8);
    $d = fread($fh, 8);
    if (strlen($d) === 8 && ((unpack('P', $d)[1] >> $col) & 1) === 1) {
        if ($i < 32) $lo |= (1 << $i);
        else         $hi |= (1 << ($i - 32));
    }
}

fclose($fh);
header('Content-Type: application/octet-stream');
echo pack('VV', $lo, $hi);
?>
