import { animate } from "motion";
import { Container, Graphics, Text } from "pixi.js";
import { 
  type BTCCheckResult, 
  satoshisToBTC, 
  truncateAddress,
  truncateWIF
} from "../utils/btcAddressChecker";

export interface BTCResultsOptions {
  width?: number;
  height?: number;
}

/**
 * Display component for BTC address check results
 */
export class BTCResults extends Container {
  private background: Graphics;
  private statusText: Text;
  private detailsText: Text;
  private balanceText: Text;
  private _width: number;
  private _height: number;
  private dots: number = 0;
  private animationInterval: number | null = null;
  private expanded: boolean = false;

  constructor(options: BTCResultsOptions = {}) {
    super();

    this._width = options.width ?? 380;
    this._height = options.height ?? 160;

    // Background panel
    this.background = new Graphics();
    this.drawBackground(0x1a1a1a, this._height);
    this.addChild(this.background);

    // Status text (e.g., "Checking addresses...")
    this.statusText = new Text({
      text: "",
      style: {
        fontFamily: "Courier New, monospace",
        fontSize: 12,
        fill: 0x888888,
        align: "center",
      },
    });
    this.statusText.anchor.set(0.5, 0);
    this.statusText.y = -this._height / 2 + 10;
    this.addChild(this.statusText);

    // Balance text (large, prominent)
    this.balanceText = new Text({
      text: "",
      style: {
        fontFamily: "Arial Black, Arial, sans-serif",
        fontSize: 24,
        fontWeight: "bold",
        fill: 0xffffff,
        align: "center",
      },
    });
    this.balanceText.anchor.set(0.5, 0);
    this.balanceText.y = -this._height / 2 + 35;
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
        lineHeight: 14,
      },
    });
    this.detailsText.anchor.set(0.5, 0);
    this.detailsText.y = -this._height / 2 + 70;
    this.addChild(this.detailsText);

    this.alpha = 0;
  }

  private drawBackground(color: number, height: number) {
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

  /**
   * Show checking status with animated dots and progress
   */
  public showChecking(addressCount: number) {
    this.stopAnimation();
    
    this.drawBackground(0x1a1a1a, 80);
    this.balanceText.text = "";
    this.detailsText.text = "";
    this.dots = 0;
    
    const updateDots = () => {
      this.dots = (this.dots + 1) % 4;
      const dotsStr = ".".repeat(this.dots);
      this.statusText.text = `Checking ${addressCount * 2} addresses${dotsStr}`;
      this.statusText.style.fill = 0x888888;
    };
    
    updateDots();
    this.animationInterval = window.setInterval(updateDots, 400);
    
    this.alpha = 0;
    animate(this, { alpha: 1 }, { duration: 0.3 });
  }

  /**
   * Update progress during checking
   */
  public updateProgress(checked: number, total: number) {
    this.statusText.text = `Checking address ${checked}/${total}...`;
  }

  /**
   * Show results of BTC check
   */
  public showResults(result: BTCCheckResult) {
    this.stopAnimation();
    
    const hasActivity = result.hasActivity;
    const balance = result.totalBalance;
    const received = result.totalReceived;
    
    // Update background color based on result
    if (hasActivity) {
      this.drawBackground(0x1a3a1a, 200);
    } else {
      this.drawBackground(0x1a1a1a, 160);
    }

    // Status line
    this.statusText.y = -this._height / 2 + 10;
    this.statusText.text = `Checked ${result.addressesChecked.length} addresses`;
    this.statusText.style.fill = 0x666666;

    // Balance display
    this.balanceText.y = -this._height / 2 + 28;
    if (hasActivity) {
      this.balanceText.text = `🎉 FOUND: ${satoshisToBTC(balance)} BTC 🎉`;
      this.balanceText.style.fill = 0x00ff00;
      this.balanceText.style.fontSize = 20;
      
      // Celebration animation
      animate(this, { scale: 1.05 }, { duration: 0.2 }).then(() => {
        animate(this, { scale: 1 }, { duration: 0.2 });
      });
    } else {
      this.balanceText.text = "No BTC found";
      this.balanceText.style.fill = 0x666666;
      this.balanceText.style.fontSize = 18;
    }

    // Build details text
    let details = "";
    
    // Show seed info
    details += `Mnemonic: ${result.mnemonic.split(" ").slice(0, 3).join(" ")}...\n`;
    
    // Show first address with activity, or just first address
    const activeAddress = result.addressesChecked.find(a => a.totalReceived > 0) || result.addressesChecked[0];
    if (activeAddress) {
      details += `\nAddress: ${truncateAddress(activeAddress.address, 10)}\n`;
      details += `Path: ${activeAddress.derivationPath}\n`;
      details += `WIF: ${truncateWIF(activeAddress.privateKeyWIF, 8)}\n`;
      
      if (hasActivity) {
        details += `\nReceived: ${satoshisToBTC(activeAddress.totalReceived)} BTC\n`;
        details += `Balance: ${satoshisToBTC(activeAddress.balance)} BTC\n`;
        details += `Transactions: ${activeAddress.txCount}`;
      }
    }

    this.detailsText.y = -this._height / 2 + 55;
    this.detailsText.text = details;
    this.detailsText.style.fill = hasActivity ? 0x88ff88 : 0x888888;

    // Adjust height based on content
    const newHeight = hasActivity ? 200 : 160;
    this.drawBackground(hasActivity ? 0x1a3a1a : 0x1a1a1a, newHeight);
  }

  /**
   * Show activity found alert
   */
  public showActivityFound(result: BTCCheckResult) {
    this.stopAnimation();
    
    this.drawBackground(0x2a1a0a, 220);
    
    this.statusText.y = -110 + 10;
    this.statusText.text = "⚠️ ACTIVITY DETECTED - AUTOPLAY STOPPED ⚠️";
    this.statusText.style.fill = 0xF7931A;

    this.balanceText.y = -110 + 35;
    this.balanceText.text = `${satoshisToBTC(result.totalBalance)} BTC`;
    this.balanceText.style.fill = 0xF7931A;
    this.balanceText.style.fontSize = 28;

    // Build details
    let details = `Total Received: ${satoshisToBTC(result.totalReceived)} BTC\n\n`;
    
    // Show addresses with activity
    const activeAddresses = result.addressesChecked.filter(a => a.totalReceived > 0);
    for (const addr of activeAddresses.slice(0, 3)) {
      details += `${truncateAddress(addr.address, 8)}\n`;
      details += `  Path: ${addr.derivationPath}\n`;
      details += `  Bal: ${satoshisToBTC(addr.balance)} | Rcvd: ${satoshisToBTC(addr.totalReceived)}\n\n`;
    }
    
    if (activeAddresses.length > 3) {
      details += `...and ${activeAddresses.length - 3} more addresses with activity`;
    }

    this.detailsText.y = -110 + 70;
    this.detailsText.text = details;
    this.detailsText.style.fill = 0xffcc88;

    // Pulsing animation
    const pulse = () => {
      animate(this.balanceText, { alpha: 0.5 }, { duration: 0.5 }).then(() => {
        animate(this.balanceText, { alpha: 1 }, { duration: 0.5 }).then(pulse);
      });
    };
    pulse();
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
  }

  /**
   * Show ready state
   */
  public showReady() {
    this.stopAnimation();
    this.drawBackground(0x1a1a1a, 80);
    this.statusText.y = -40 + 15;
    this.statusText.text = "Press SPIN to generate seed phrase";
    this.statusText.style.fill = 0x666666;
    this.balanceText.text = "";
    this.detailsText.text = "";
    
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
    this._width = Math.min(width - 40, 420);
    this.detailsText.style.wordWrapWidth = this._width - 30;
    this.drawBackground(0x1a1a1a, this._height);
  }

  public override destroy() {
    this.stopAnimation();
    super.destroy();
  }
}
