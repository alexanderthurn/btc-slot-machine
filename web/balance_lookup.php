<?php
// Read-only lookup against filter/balance_utxo.sqlite (built by parser with SQLite).
// Query:
//   - ?txid=<64 hex> [optional &vout=<int>] [optional &key32=<64 hex>]
//   - OR ?key32=<64 hex>
// Optional: &limit=<1..200> (default 50, for key32-only queries)

header('Content-Type: application/json; charset=utf-8');

$dbPath = __DIR__ . '/filter/balance_utxo.sqlite';
if (!is_readable($dbPath)) {
    echo json_encode(['ok' => false, 'error' => 'balance_utxo.sqlite not found or not readable'], JSON_UNESCAPED_SLASHES);
    exit;
}

$txidHex = isset($_GET['txid']) ? preg_replace('/[^0-9a-fA-F]/', '', (string)$_GET['txid']) : '';
$key32Hex = isset($_GET['key32']) ? preg_replace('/[^0-9a-fA-F]/', '', (string)$_GET['key32']) : '';
$hasTxid = (strlen($txidHex) === 64);
$hasKey32 = (strlen($key32Hex) === 64);

if (!$hasTxid && !$hasKey32) {
    echo json_encode(['ok' => false, 'error' => 'provide txid or key32 (64 hex chars)'], JSON_UNESCAPED_SLASHES);
    exit;
}

$limit = 50;
if (isset($_GET['limit']) && $_GET['limit'] !== '') {
    $lv = filter_var($_GET['limit'], FILTER_VALIDATE_INT, ['options' => ['min_range' => 1, 'max_range' => 200]]);
    if ($lv === false) {
        echo json_encode(['ok' => false, 'error' => 'invalid limit (1..200)'], JSON_UNESCAPED_SLASHES);
        exit;
    }
    $limit = $lv;
}

$sql = 'SELECT hex(txid) AS txid_hex, vout, hex(key32) AS key32_hex, value_sat FROM utxo WHERE 1=1';
$params = [];

if ($hasTxid) {
    $txidBin = hex2bin($txidHex);
    if ($txidBin === false || strlen($txidBin) !== 32) {
        echo json_encode(['ok' => false, 'error' => 'invalid txid hex'], JSON_UNESCAPED_SLASHES);
        exit;
    }
    $sql .= ' AND txid = ?';
    $params[] = $txidBin;
}

if (isset($_GET['vout']) && $_GET['vout'] !== '') {
    $vout = filter_var($_GET['vout'], FILTER_VALIDATE_INT, ['options' => ['min_range' => 0]]);
    if ($vout === false) {
        echo json_encode(['ok' => false, 'error' => 'invalid vout'], JSON_UNESCAPED_SLASHES);
        exit;
    }
    $sql .= ' AND vout = ?';
    $params[] = $vout;
}

if ($hasKey32) {
    $kb = hex2bin($key32Hex);
    if ($kb === false || strlen($kb) !== 32) {
        echo json_encode(['ok' => false, 'error' => 'invalid key32 hex'], JSON_UNESCAPED_SLASHES);
        exit;
    }
    $sql .= ' AND key32 = ?';
    $params[] = $kb;
}

$sql .= ' ORDER BY txid, vout LIMIT ' . (int)$limit;

try {
    $pdo = new PDO('sqlite:' . $dbPath, null, null, [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
    ]);
    $st = $pdo->prepare($sql);
    $st->execute($params);
    $rows = $st->fetchAll(PDO::FETCH_ASSOC);
    $totalSat = 0;
    foreach ($rows as $r) {
        $totalSat += (int)$r['value_sat'];
    }
    echo json_encode([
        'ok' => true,
        'count' => count($rows),
        'total_sat' => $totalSat,
        'rows' => $rows,
        'limit' => $limit,
    ], JSON_UNESCAPED_SLASHES);
} catch (Throwable $e) {
    echo json_encode(['ok' => false, 'error' => $e->getMessage()], JSON_UNESCAPED_SLASHES);
}
