/**
 * Mock BTC Address Checker
 * This will be replaced with actual API calls later
 */

export interface BTCMatch {
  address: string;
  balance: number;
  derivationPath: string;
  addressIndex: number;
}

export interface BTCCheckResult {
  seedPhrase: string[];
  addressesChecked: number;
  derivationPaths: string[];
  matches: BTCMatch[];
  isChecking: boolean;
}

// Common BTC derivation paths
export const DERIVATION_PATHS = {
  LEGACY: "m/44'/0'/0'/0", // Legacy (P2PKH)
  SEGWIT_NATIVE: "m/84'/0'/0'/0", // Native SegWit (P2WPKH)
  SEGWIT_COMPAT: "m/49'/0'/0'/0", // SegWit Compatible (P2SH-P2WPKH)
};

/**
 * Generate a mock BTC address (for display purposes only)
 * @param derivationPath The derivation path
 * @param index The address index
 * @returns A mock BTC address string
 */
function generateMockAddress(derivationPath: string, _index: number): string {
  // Generate different address formats based on derivation path
  const chars = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
  let address = "";
  
  if (derivationPath.includes("84'")) {
    // Native SegWit (bc1...)
    address = "bc1q";
    for (let i = 0; i < 38; i++) {
      address += chars[Math.floor(Math.random() * chars.length)];
    }
  } else if (derivationPath.includes("49'")) {
    // SegWit Compatible (3...)
    address = "3";
    for (let i = 0; i < 33; i++) {
      address += chars[Math.floor(Math.random() * chars.length)];
    }
  } else {
    // Legacy (1...)
    address = "1";
    for (let i = 0; i < 33; i++) {
      address += chars[Math.floor(Math.random() * chars.length)];
    }
  }
  
  return address;
}

/**
 * Mock implementation of BTC address checking
 * Returns random results for demonstration purposes
 * 
 * @param seedPhrase Array of 12 BIP39 words
 * @param addressCount Number of addresses to check per derivation path
 * @returns Promise<BTCCheckResult>
 */
export async function checkBTCAddresses(
  seedPhrase: string[],
  addressCount: number = 10
): Promise<BTCCheckResult> {
  // Simulate network delay
  await new Promise(resolve => setTimeout(resolve, 1500 + Math.random() * 1000));
  
  const derivationPaths = [
    DERIVATION_PATHS.LEGACY,
    DERIVATION_PATHS.SEGWIT_NATIVE,
  ];
  
  const matches: BTCMatch[] = [];
  
  // 3% chance of finding BTC (for demo excitement)
  const foundBTC = Math.random() < 0.03;
  
  if (foundBTC) {
    const randomPath = derivationPaths[Math.floor(Math.random() * derivationPaths.length)];
    const randomIndex = Math.floor(Math.random() * addressCount);
    const randomBalance = Math.random() * 2; // 0-2 BTC
    
    matches.push({
      address: generateMockAddress(randomPath, randomIndex),
      balance: parseFloat(randomBalance.toFixed(8)),
      derivationPath: randomPath,
      addressIndex: randomIndex,
    });
  }
  
  return {
    seedPhrase,
    addressesChecked: addressCount * derivationPaths.length,
    derivationPaths,
    matches,
    isChecking: false,
  };
}

/**
 * Format satoshis to BTC
 */
export function satoshisToBTC(satoshis: number): string {
  return (satoshis / 100000000).toFixed(8);
}

/**
 * Format BTC with proper decimals
 */
export function formatBTC(btc: number): string {
  return btc.toFixed(8) + " BTC";
}

