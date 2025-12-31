import { animate } from "motion";
import { Container, Graphics, Text } from "pixi.js";
import { 
  type BTCCheckResult, 
  satoshisToBTC, 
  truncateAddress
} from "../utils/btcAddressChecker";

export interface BTCResultsOptions {
  width?: number;
  height?: number;
}

/**
 * Display component for BTC address check results
 * Shows full details and allows copying
 */
export class BTCResults extends Container {
  private background: Graphics;
  private statusText: Text;
  private detailsText: Text;
  private balanceText: Text;
  private copyHint: Text;
  private _width: number;
  private _height: number;
  private dots: number = 0;
  private animationInterval: number | null = null;
  private lastResult: BTCCheckResult | null = null;

  constructor(options: BTCResultsOptions = {}) {
    super();

    this._width = options.width ?? 420;
    this._height = options.height ?? 220;

    // Background panel
    this.background = new Graphics();
    this.drawBackground(0x1a1a1a, 80);
    this.addChild(this.background);

    // Status text (e.g., "Checking addresses...")
    this.statusText = new Text({
      text: "",
      style: {
        fontFamily: "Courier New, monospace",
        fontSize: 11,
        fill: 0x888888,
        align: "center",
      },
    });
    this.statusText.anchor.set(0.5, 0);
    this.addChild(this.statusText);

    // Balance text (large, prominent)
    this.balanceText = new Text({
      text: "",
      style: {
        fontFamily: "Arial Black, Arial, sans-serif",
        fontSize: 20,
        fontWeight: "bold",
        fill: 0xffffff,
        align: "center",
      },
    });
    this.balanceText.anchor.set(0.5, 0);
    this.addChild(this.balanceText);

    // Details text (address, WIF, etc.)
    this.detailsText = new Text({
      text: "",
      style: {
        fontFamily: "Courier New, monospace",
        fontSize: 9,
        fill: 0xaaaaaa,
        align: "left",
        wordWrap: true,
        wordWrapWidth: this._width - 30,
        lineHeight: 13,
      },
    });
    this.detailsText.anchor.set(0.5, 0);
    this.addChild(this.detailsText);

    // Copy hint
    this.copyHint = new Text({
      text: "Click to copy full details",
      style: {
        fontFamily: "Arial, sans-serif",
        fontSize: 10,
        fill: 0x555555,
      },
    });
    this.copyHint.anchor.set(0.5, 0);
    this.addChild(this.copyHint);

    // Make interactive for copying
    this.eventMode = "static";
    this.cursor = "pointer";
    this.on("pointerdown", () => this.copyToClipboard());
    this.on("pointerover", () => {
      this.copyHint.style.fill = 0xF7931A;
    });
    this.on("pointerout", () => {
      this.copyHint.style.fill = 0x555555;
    });

    this.alpha = 0;
  }

  private drawBackground(color: number, height: number) {
    this._height = height;
    this.background.clear();
    this.background.roundRect(
      -this._width / 2,
      -height / 2,
      this._width,
      height,
      12
    );
    this.background.fill({ color, alpha: 0.92 });
    this.background.stroke({ color: 0x333333, width: 2 });
  }

  private positionElements(baseY: number) {
    this.statusText.y = baseY + 8;
    this.balanceText.y = baseY + 25;
    this.detailsText.y = baseY + 55;
    this.copyHint.y = baseY + this._height - 25;
  }

  /**
   * Show checking status with animated dots and progress
   */
  public showChecking(addressCount: number) {
    this.stopAnimation();
    
    this.drawBackground(0x1a1a1a, 80);
    this.positionElements(-40);
    this.balanceText.text = "";
    this.detailsText.text = "";
    this.copyHint.text = "";
    this.dots = 0;
    
    const totalAddresses = addressCount * 2; // 2 derivation paths (Legacy + SegWit)
    
    const updateDots = () => {
      this.dots = (this.dots + 1) % 4;
      const dotsStr = ".".repeat(this.dots);
      this.statusText.text = `Checking ${totalAddresses} addr (${addressCount}×Legacy + ${addressCount}×SegWit)${dotsStr}`;
      this.statusText.style.fill = 0x888888;
    };
    
    updateDots();
    this.animationInterval = window.setInterval(updateDots, 300);
    
    if (this.alpha === 0) {
      animate(this, { alpha: 1 }, { duration: 0.3 });
    }
  }

  /**
   * Update progress during checking
   */
  public updateProgress(checked: number, total: number) {
    this.statusText.text = `Checking address ${checked}/${total}...`;
  }

  /**
   * Show results of BTC check - ALWAYS shows full details
   */
  public showResults(result: BTCCheckResult) {
    this.stopAnimation();
    this.lastResult = result;
    
    const hasActivity = result.hasActivity;
    const balance = result.totalBalance;
    const received = result.totalReceived;
    
    // Calculate height based on content
    const height = hasActivity ? 260 : 220;
    this.drawBackground(hasActivity ? 0x1a3a1a : 0x1a1a1a, height);
    this.positionElements(-height / 2);

    // Status line
    this.statusText.text = `Checked ${result.addressesChecked.length} addresses`;
    this.statusText.style.fill = 0x666666;

    // Balance display
    if (hasActivity) {
      this.balanceText.text = `🎉 FOUND: ${satoshisToBTC(balance)} BTC`;
      this.balanceText.style.fill = 0x00ff00;
      this.balanceText.style.fontSize = 18;
      
      // Celebration animation
      animate(this, { scale: 1.02 }, { duration: 0.15 }).then(() => {
        animate(this, { scale: 1 }, { duration: 0.15 });
      });
    } else {
      this.balanceText.text = `Balance: ${satoshisToBTC(balance)} BTC`;
      this.balanceText.style.fill = 0x888888;
      this.balanceText.style.fontSize = 16;
    }

    // Build full details text - grouped by derivation path
    let details = "";
    
    // Mnemonic (show first 4 words + ...)
    const words = result.mnemonic.split(" ");
    details += `Mnemonic: ${words.slice(0, 4).join(" ")}...\n`;
    
    // Group addresses by path type
    const legacyAddrs = result.addressesChecked.filter(a => a.derivationPath.includes("44'"));
    const segwitAddrs = result.addressesChecked.filter(a => a.derivationPath.includes("84'"));

    // Legacy addresses (starts with 1)
    if (legacyAddrs.length > 0) {
      const addr = legacyAddrs[0];
      details += `\n🔶 Legacy (m/44'/0'/0'/0):\n`;
      details += `   ${truncateAddress(addr.address, 14)}\n`;
      details += `   Bal: ${satoshisToBTC(addr.balance)} | Rcv: ${satoshisToBTC(addr.totalReceived)}\n`;
    }

    // SegWit addresses (starts with bc1)
    if (segwitAddrs.length > 0) {
      const addr = segwitAddrs[0];
      details += `\n🔷 SegWit (m/84'/0'/0'/0):\n`;
      details += `   ${truncateAddress(addr.address, 14)}\n`;
      details += `   Bal: ${satoshisToBTC(addr.balance)} | Rcv: ${satoshisToBTC(addr.totalReceived)}\n`;
    }
    
    // If there's activity, show more details
    if (hasActivity) {
      details += `\n✅ Total Received: ${satoshisToBTC(received)} BTC\n`;
      
      const activeAddrs = result.addressesChecked.filter(a => a.totalReceived > 0);
      if (activeAddrs.length > 0) {
        details += `   Active addresses: ${activeAddrs.length}`;
      }
    }

    this.detailsText.text = details;
    this.detailsText.style.fill = hasActivity ? 0x88ff88 : 0x999999;

    // Copy hint
    this.copyHint.text = "📋 Click to copy full details";
    this.copyHint.y = -height / 2 + height - 20;
  }

  /**
   * Show activity found alert (special case)
   */
  public showActivityFound(result: BTCCheckResult) {
    // Just update styling to make it more prominent
    this.showResults(result);
    
    // Add pulsing effect to balance
    const pulse = () => {
      if (!this.lastResult?.hasActivity) return;
      animate(this.balanceText, { alpha: 0.6 }, { duration: 0.4 }).then(() => {
        animate(this.balanceText, { alpha: 1 }, { duration: 0.4 }).then(pulse);
      });
    };
    pulse();
  }

  /**
   * Copy full details to clipboard
   */
  private async copyToClipboard() {
    if (!this.lastResult) return;
    
    const result = this.lastResult;
    let text = "=== BTC Seed Slot Machine Results ===\n\n";
    text += `Mnemonic: ${result.mnemonic}\n\n`;
    text += `Seed Hex: ${result.seedHex}\n\n`;
    text += `Master Private Key: ${result.masterPrivateKey}\n\n`;
    text += `Total Balance: ${satoshisToBTC(result.totalBalance)} BTC\n`;
    text += `Total Received: ${satoshisToBTC(result.totalReceived)} BTC\n`;
    text += `Has Activity: ${result.hasActivity ? "YES" : "No"}\n\n`;
    text += `=== Addresses Checked ===\n\n`;
    
    for (const addr of result.addressesChecked) {
      text += `Path: ${addr.derivationPath}\n`;
      text += `Address: ${addr.address}\n`;
      text += `WIF: ${addr.privateKeyWIF}\n`;
      text += `Balance: ${satoshisToBTC(addr.balance)} BTC\n`;
      text += `Received: ${satoshisToBTC(addr.totalReceived)} BTC\n`;
      text += `Transactions: ${addr.txCount}\n\n`;
    }
    
    try {
      await navigator.clipboard.writeText(text);
      
      // Show feedback
      const originalText = this.copyHint.text;
      this.copyHint.text = "✅ Copied to clipboard!";
      this.copyHint.style.fill = 0x00ff00;
      
      setTimeout(() => {
        this.copyHint.text = originalText;
        this.copyHint.style.fill = 0x555555;
      }, 2000);
    } catch (err) {
      console.error("Failed to copy:", err);
      this.copyHint.text = "❌ Copy failed";
      setTimeout(() => {
        this.copyHint.text = "📋 Click to copy full details";
      }, 2000);
    }
  }

  /**
   * Hide the results display
   */
  public async hide() {
    this.stopAnimation();
    await animate(this, { alpha: 0 }, { duration: 0.3 });
    this.statusText.text = "";
    this.balanceText.text = "";
    this.detailsText.text = "";
    this.copyHint.text = "";
    this.lastResult = null;
  }

  /**
   * Show ready state
   */
  public showReady() {
    this.stopAnimation();
    this.drawBackground(0x1a1a1a, 80);
    this.positionElements(-40);
    this.statusText.text = "Press SPIN to generate seed phrase";
    this.statusText.style.fill = 0x666666;
    this.balanceText.text = "";
    this.detailsText.text = "";
    this.copyHint.text = "";
    
    if (this.alpha === 0) {
      animate(this, { alpha: 1 }, { duration: 0.3 });
    }
  }

  private stopAnimation() {
    if (this.animationInterval !== null) {
      clearInterval(this.animationInterval);
      this.animationInterval = null;
    }
  }

  public resize(width: number) {
    this._width = Math.min(width - 30, 450);
    this.detailsText.style.wordWrapWidth = this._width - 30;
  }

  public override destroy() {
    this.stopAnimation();
    super.destroy();
  }
}
