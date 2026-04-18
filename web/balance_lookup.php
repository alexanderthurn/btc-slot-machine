<?php
// Read-only lookup against filter/balance_utxo.sqlite (built by parser with SQLite).
// Query: ?txid=<64 hex>  (required)  optional &vout=<int>  optional &key32=<64 hex>

header('Content-Type: application/json; charset=utf-8');

$dbPath = __DIR__ . '/filter/balance_utxo.sqlite';
if (!is_readable($dbPath)) {
    echo json_encode(['ok' => false, 'error' => 'balance_utxo.sqlite not found or not readable'], JSON_UNESCAPED_SLASHES);
    exit;
}

$txidHex = isset($_GET['txid']) ? preg_replace('/[^0-9a-fA-F]/', '', (string)$_GET['txid']) : '';
if (strlen($txidHex) !== 64) {
    echo json_encode(['ok' => false, 'error' => 'txid must be 64 hex characters'], JSON_UNESCAPED_SLASHES);
    exit;
}

$txidBin = hex2bin($txidHex);
if ($txidBin === false || strlen($txidBin) !== 32) {
    echo json_encode(['ok' => false, 'error' => 'invalid txid hex'], JSON_UNESCAPED_SLASHES);
    exit;
}

$sql = 'SELECT hex(txid) AS txid_hex, vout, hex(key32) AS key32_hex, value_sat FROM utxo WHERE txid = ?';
$params = [$txidBin];

if (isset($_GET['vout']) && $_GET['vout'] !== '') {
    $vout = filter_var($_GET['vout'], FILTER_VALIDATE_INT, ['options' => ['min_range' => 0]]);
    if ($vout === false) {
        echo json_encode(['ok' => false, 'error' => 'invalid vout'], JSON_UNESCAPED_SLASHES);
        exit;
    }
    $sql .= ' AND vout = ?';
    $params[] = $vout;
}

if (isset($_GET['key32']) && $_GET['key32'] !== '') {
    $k = preg_replace('/[^0-9a-fA-F]/', '', (string)$_GET['key32']);
    if (strlen($k) !== 64) {
        echo json_encode(['ok' => false, 'error' => 'key32 must be 64 hex characters'], JSON_UNESCAPED_SLASHES);
        exit;
    }
    $kb = hex2bin($k);
    if ($kb === false || strlen($kb) !== 32) {
        echo json_encode(['ok' => false, 'error' => 'invalid key32 hex'], JSON_UNESCAPED_SLASHES);
        exit;
    }
    $sql .= ' AND key32 = ?';
    $params[] = $kb;
}

$sql .= ' ORDER BY vout';

try {
    $pdo = new PDO('sqlite:' . $dbPath, null, null, [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
    ]);
    $st = $pdo->prepare($sql);
    $st->execute($params);
    $rows = $st->fetchAll(PDO::FETCH_ASSOC);
    echo json_encode(['ok' => true, 'count' => count($rows), 'rows' => $rows], JSON_UNESCAPED_SLASHES);
} catch (Throwable $e) {
    echo json_encode(['ok' => false, 'error' => $e->getMessage()], JSON_UNESCAPED_SLASHES);
}
