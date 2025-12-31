/**
 * BTC Address Checker using real cryptography and mempool.space API
 */
import { generateMnemonic, mnemonicToSeedSync, validateMnemonic } from "@scure/bip39";
import { wordlist } from "@scure/bip39/wordlists/english.js";
import { HDKey } from "@scure/bip32";
import * as btc from "@scure/btc-signer";
import { hexToBytes, bytesToHex } from "@noble/hashes/utils.js";

// Test mnemonic (abandon x12) - has historical activity for testing
export const TEST_MNEMONIC = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";

export interface AddressInfo {
  address: string;
  privateKeyWIF: string;
  derivationPath: string;
  balance: number;
  totalReceived: number;
  txCount: number;
}

export interface BTCCheckResult {
  mnemonic: string;
  seedHex: string;
  masterPrivateKey: string;
  addressesChecked: AddressInfo[];
  totalBalance: number;
  totalReceived: number;
  hasActivity: boolean;
  isChecking: boolean;
  error?: string;
}

// Common BTC derivation paths
export const DERIVATION_PATHS = {
  BIP44_LEGACY: "m/44'/0'/0'/0", // Legacy (P2PKH) - starts with 1
  BIP84_SEGWIT: "m/84'/0'/0'/0", // Native SegWit (P2WPKH) - starts with bc1q
  BIP49_SEGWIT_COMPAT: "m/49'/0'/0'/0", // SegWit Compatible (P2SH-P2WPKH) - starts with 3
};

/**
 * Generate a new random mnemonic
 */
export function generateNewMnemonic(): string {
  return generateMnemonic(wordlist, 128); // 128 bits = 12 words
}

/**
 * Validate a mnemonic phrase
 */
export function isValidMnemonic(mnemonic: string): boolean {
  return validateMnemonic(mnemonic, wordlist);
}

/**
 * Convert mnemonic to seed hex
 */
export function mnemonicToSeedHex(mnemonic: string): string {
  const seed = mnemonicToSeedSync(mnemonic);
  return bytesToHex(seed);
}

/**
 * Derive a BIP44 Legacy address from seed
 */
function deriveAddress(seedHex: string, accountPath: string, index: number): { address: string; privateKeyWIF: string; path: string } {
  const seed = hexToBytes(seedHex);
  const hdKey = HDKey.fromMasterSeed(seed);
  
  // Derive the key
  const fullPath = `${accountPath}/${index}`;
  const derived = hdKey.derive(fullPath);
  
  if (!derived.privateKey) {
    throw new Error("Failed to derive private key");
  }

  // Get the address based on path type
  let address: string;
  
  if (accountPath.startsWith("m/84'")) {
    // Native SegWit (P2WPKH)
    const payment = btc.p2wpkh(derived.publicKey!);
    address = payment.address!;
  } else if (accountPath.startsWith("m/49'")) {
    // SegWit Compatible (P2SH-P2WPKH)
    const p2wpkh = btc.p2wpkh(derived.publicKey!);
    const payment = btc.p2sh(p2wpkh);
    address = payment.address!;
  } else {
    // Legacy (P2PKH)
    const payment = btc.p2pkh(derived.publicKey!);
    address = payment.address!;
  }

  // Convert private key to WIF
  const privateKeyWIF = btc.WIF().encode(derived.privateKey);

  return {
    address,
    privateKeyWIF,
    path: fullPath,
  };
}

/**
 * Get master private key (xprv) from seed
 */
export function getMasterPrivateKey(seedHex: string): string {
  const seed = hexToBytes(seedHex);
  const hdKey = HDKey.fromMasterSeed(seed);
  return hdKey.privateExtendedKey;
}

/**
 * Fetch address info from mempool.space API
 */
async function fetchAddressInfo(address: string): Promise<{ balance: number; totalReceived: number; txCount: number }> {
  try {
    const response = await fetch(`https://mempool.space/api/address/${address}`);
    if (!response.ok) {
      throw new Error(`API error: ${response.status}`);
    }
    const data = await response.json();
    
    const balance = (data.chain_stats.funded_txo_sum - data.chain_stats.spent_txo_sum) + 
                    (data.mempool_stats.funded_txo_sum - data.mempool_stats.spent_txo_sum);
    const totalReceived = data.chain_stats.funded_txo_sum + data.mempool_stats.funded_txo_sum;
    const txCount = data.chain_stats.tx_count + data.mempool_stats.tx_count;
    
    return { balance, totalReceived, txCount };
  } catch (error) {
    console.error(`Error fetching address ${address}:`, error);
    return { balance: 0, totalReceived: 0, txCount: 0 };
  }
}

/**
 * Check BTC addresses for a given mnemonic
 */
export async function checkBTCAddresses(
  mnemonic: string,
  addressCount: number = 10,
  onProgress?: (checked: number, total: number) => void
): Promise<BTCCheckResult> {
  const result: BTCCheckResult = {
    mnemonic,
    seedHex: "",
    masterPrivateKey: "",
    addressesChecked: [],
    totalBalance: 0,
    totalReceived: 0,
    hasActivity: false,
    isChecking: true,
  };

  try {
    // Convert mnemonic to seed
    result.seedHex = mnemonicToSeedHex(mnemonic);
    result.masterPrivateKey = getMasterPrivateKey(result.seedHex);

    // Check addresses for multiple derivation paths
    const paths = [DERIVATION_PATHS.BIP44_LEGACY, DERIVATION_PATHS.BIP84_SEGWIT];
    let checkedCount = 0;
    const totalToCheck = paths.length * addressCount;

    for (const basePath of paths) {
      for (let i = 0; i < addressCount; i++) {
        const { address, privateKeyWIF, path } = deriveAddress(result.seedHex, basePath, i);
        
        // Fetch balance from API
        const { balance, totalReceived, txCount } = await fetchAddressInfo(address);
        
        const addressInfo: AddressInfo = {
          address,
          privateKeyWIF,
          derivationPath: path,
          balance,
          totalReceived,
          txCount,
        };
        
        result.addressesChecked.push(addressInfo);
        result.totalBalance += balance;
        result.totalReceived += totalReceived;
        
        if (totalReceived > 0 || balance > 0) {
          result.hasActivity = true;
        }

        checkedCount++;
        onProgress?.(checkedCount, totalToCheck);

        // Small delay to avoid rate limiting
        await new Promise(resolve => setTimeout(resolve, 100));
      }
    }
  } catch (error) {
    result.error = error instanceof Error ? error.message : "Unknown error";
    console.error("BTC check error:", error);
  }

  result.isChecking = false;
  return result;
}

/**
 * Quick check - only checks first address of each path
 */
export async function quickCheckBTCAddresses(
  mnemonic: string,
  onProgress?: (checked: number, total: number) => void
): Promise<BTCCheckResult> {
  return checkBTCAddresses(mnemonic, 1, onProgress);
}

/**
 * Format satoshis to BTC string
 */
export function satoshisToBTC(satoshis: number): string {
  return (satoshis / 100000000).toFixed(8);
}

/**
 * Format BTC with proper decimals
 */
export function formatBTC(satoshis: number): string {
  return satoshisToBTC(satoshis) + " BTC";
}

/**
 * Truncate address for display
 */
export function truncateAddress(address: string, chars: number = 8): string {
  if (address.length <= chars * 2 + 3) return address;
  return `${address.slice(0, chars)}...${address.slice(-chars)}`;
}

/**
 * Truncate WIF for display
 */
export function truncateWIF(wif: string, chars: number = 6): string {
  if (wif.length <= chars * 2 + 3) return wif;
  return `${wif.slice(0, chars)}...${wif.slice(-chars)}`;
}
