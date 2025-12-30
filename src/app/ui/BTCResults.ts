import { animate } from "motion";
import { Container, Graphics, Text } from "pixi.js";
import type { BTCCheckResult } from "../utils/btcAddressChecker";

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
  private resultText: Text;
  private _width: number;
  private _height: number;
  private dots: number = 0;
  private animationInterval: number | null = null;

  constructor(options: BTCResultsOptions = {}) {
    super();

    this._width = options.width ?? 350;
    this._height = options.height ?? 80;

    // Background panel
    this.background = new Graphics();
    this.drawBackground(0x1a1a1a);
    this.addChild(this.background);

    // Status text (e.g., "Checking addresses...")
    this.statusText = new Text({
      text: "",
      style: {
        fontFamily: "Courier New, monospace",
        fontSize: 14,
        fill: 0x888888,
        align: "center",
      },
    });
    this.statusText.anchor.set(0.5);
    this.statusText.y = -15;
    this.addChild(this.statusText);

    // Result text (e.g., "Found 0.5 BTC!")
    this.resultText = new Text({
      text: "",
      style: {
        fontFamily: "Arial, sans-serif",
        fontSize: 18,
        fontWeight: "bold",
        fill: 0xffffff,
        align: "center",
      },
    });
    this.resultText.anchor.set(0.5);
    this.resultText.y = 15;
    this.addChild(this.resultText);

    this.alpha = 0;
  }

  private drawBackground(color: number) {
    this.background.clear();
    this.background.roundRect(
      -this._width / 2,
      -this._height / 2,
      this._width,
      this._height,
      12
    );
    this.background.fill({ color, alpha: 0.85 });
    this.background.stroke({ color: 0x333333, width: 2 });
  }

  /**
   * Show checking status with animated dots
   */
  public showChecking(addressCount: number) {
    this.stopAnimation();
    
    this.drawBackground(0x1a1a1a);
    this.resultText.text = "";
    this.dots = 0;
    
    const updateDots = () => {
      this.dots = (this.dots + 1) % 4;
      const dotsStr = ".".repeat(this.dots);
      this.statusText.text = `Checking ${addressCount} addresses${dotsStr}`;
      this.statusText.style.fill = 0x888888;
    };
    
    updateDots();
    this.animationInterval = window.setInterval(updateDots, 400);
    
    this.alpha = 0;
    animate(this, { alpha: 1 }, { duration: 0.3 });
  }

  /**
   * Show results of BTC check
   */
  public showResults(result: BTCCheckResult) {
    this.stopAnimation();
    
    this.statusText.text = `Checked ${result.addressesChecked} addresses`;
    this.statusText.style.fill = 0x666666;
    
    if (result.matches.length > 0) {
      // Found BTC!
      const totalBTC = result.matches.reduce((sum, m) => sum + m.balance, 0);
      this.drawBackground(0x1a3a1a);
      this.resultText.text = `🎉 FOUND ${totalBTC.toFixed(8)} BTC! 🎉`;
      this.resultText.style.fill = 0x00ff00;
      
      // Celebration animation
      animate(this, { scale: 1.1 }, { duration: 0.2 }).then(() => {
        animate(this, { scale: 1 }, { duration: 0.2 });
      });
    } else {
      // No BTC found
      this.drawBackground(0x1a1a1a);
      this.resultText.text = "No BTC found - try again!";
      this.resultText.style.fill = 0x888888;
    }
  }

  /**
   * Hide the results display
   */
  public async hide() {
    this.stopAnimation();
    await animate(this, { alpha: 0 }, { duration: 0.3 });
    this.statusText.text = "";
    this.resultText.text = "";
  }

  /**
   * Show ready state
   */
  public showReady() {
    this.stopAnimation();
    this.statusText.text = "Press SPIN to generate seed phrase";
    this.statusText.style.fill = 0x666666;
    this.resultText.text = "";
    this.drawBackground(0x1a1a1a);
    
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
    this._width = Math.min(width - 40, 400);
    this.drawBackground(0x1a1a1a);
  }

  public override destroy() {
    this.stopAnimation();
    super.destroy();
  }
}


