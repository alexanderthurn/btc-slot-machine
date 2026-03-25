<?php
// btc-slot-machine: balance filter lookup (addresses with unspent BTC only).
// Same binary protocol as index.php — 5 bytes per lookup, 8-byte bitmask response.

$fh   = fopen(__DIR__ . '/filter/2048mb_bal.bin', 'rb');
if (!$fh) { header('Content-Type: application/octet-stream'); echo pack('VV', 0, 0); exit; }
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
