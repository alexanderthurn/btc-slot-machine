import { animate } from "motion";
import { Container, Graphics, Text } from "pixi.js";
import { getRandomWord } from "../../utils/bip39Words";
import { userSettings } from "../../utils/userSettings";

export interface ReelOptions {
  index: number;
  width?: number;
  height?: number;
}

/**
 * A single reel that displays a BIP39 word
 * Lock state is controlled via Settings popup
 */
export class Reel extends Container {
  public readonly index: number;
  private background: Graphics;
  private wordContainer: Container;
  private wordText: Text;
  private indexLabel: Text;
  private lockIcon: Text;
  private _word: string = "";
  private _spinning: boolean = false;
  private reelWidth: number;
  private reelHeight: number;
  private spinInterval: number | null = null;

  constructor(options: ReelOptions) {
    super();

    this.index = options.index;
    this.reelWidth = options.width ?? 140;
    this.reelHeight = options.height ?? 60;

    // Background frame
    this.background = new Graphics();
    this.addChild(this.background);

    // Word container with mask for scroll effect
    this.wordContainer = new Container();
    this.addChild(this.wordContainer);

    // Word text
    this.wordText = new Text({
      text: "",
      style: {
        fontFamily: "Courier New, monospace",
        fontSize: 16,
        fontWeight: "bold",
        fill: 0xffffff,
        align: "center",
      },
    });
    this.wordText.anchor.set(0.5);
    this.wordContainer.addChild(this.wordText);

    // Index label (1-12)
    this.indexLabel = new Text({
      text: `${this.index + 1}`,
      style: {
        fontFamily: "Arial, sans-serif",
        fontSize: 11,
        fontWeight: "bold",
        fill: 0xF7931A,
      },
    });
    this.indexLabel.anchor.set(0.5);
    this.indexLabel.x = -this.reelWidth / 2 + 14;
    this.indexLabel.y = -this.reelHeight / 2 + 12;
    this.addChild(this.indexLabel);

    // Lock icon (shows when locked via settings)
    this.lockIcon = new Text({
      text: "🔒",
      style: {
        fontSize: 12,
      },
    });
    this.lockIcon.anchor.set(0.5);
    this.lockIcon.x = this.reelWidth / 2 - 14;
    this.lockIcon.y = -this.reelHeight / 2 + 12;
    this.lockIcon.visible = false;
    this.addChild(this.lockIcon);

    // Set initial word
    this._word = getRandomWord();
    this.wordText.text = this._word;
    
    // Update visual based on lock state
    this.updateLockState();
  }

  /**
   * Update visual to reflect lock state from settings
   */
  public updateLockState() {
    const isLocked = userSettings.isPositionLocked(this.index);
    this.lockIcon.visible = isLocked;
    this.drawBackground(isLocked);
  }

  private drawBackground(locked: boolean) {
    this.background.clear();
    
    // Main reel background
    this.background.roundRect(
      -this.reelWidth / 2,
      -this.reelHeight / 2,
      this.reelWidth,
      this.reelHeight,
      8
    );
    
    if (locked) {
      // Locked state - orange border
      this.background.fill({ color: 0x1a1a1a });
      this.background.stroke({ color: 0xF7931A, width: 3 });
    } else {
      // Normal state
      this.background.fill({ color: 0x1a1a1a });
      this.background.stroke({ color: 0x333333, width: 2 });
    }

    // Inner gradient effect
    this.background.roundRect(
      -this.reelWidth / 2 + 3,
      -this.reelHeight / 2 + 3,
      this.reelWidth - 6,
      this.reelHeight - 6,
      6
    );
    this.background.fill({ color: 0x0d0d0d, alpha: 0.5 });
  }

  /**
   * Check if this reel is locked (from settings)
   */
  get locked(): boolean {
    return userSettings.isPositionLocked(this.index);
  }

  /**
   * Start spinning the reel
   */
  public startSpin() {
    if (this.locked || this._spinning) return;
    
    this._spinning = true;
    
    // Rapidly change words during spin
    this.spinInterval = window.setInterval(() => {
      const randomWord = getRandomWord();
      this.wordText.text = randomWord;
      
      // Quick blur effect
      this.wordText.alpha = 0.7;
    }, 50);
  }

  /**
   * Stop spinning and land on a specific word
   */
  public async stopSpin(finalWord: string, delay: number = 0): Promise<void> {
    if (!this._spinning) {
      this._word = finalWord;
      this.wordText.text = finalWord;
      return;
    }

    await new Promise(resolve => setTimeout(resolve, delay));

    if (this.spinInterval) {
      clearInterval(this.spinInterval);
      this.spinInterval = null;
    }

    this._spinning = false;
    this._word = finalWord;
    
    // Landing animation
    this.wordText.text = finalWord;
    this.wordText.alpha = 1;
    
    // Bounce effect
    await animate(
      this.wordContainer,
      { y: [-5, 3, 0] },
      { duration: 0.3, ease: "easeOut" }
    );
  }

  /**
   * Force stop immediately (for cleanup)
   */
  public forceStop() {
    if (this.spinInterval) {
      clearInterval(this.spinInterval);
      this.spinInterval = null;
    }
    this._spinning = false;
    this.wordText.alpha = 1;
  }

  get word(): string {
    return this._word;
  }

  set word(value: string) {
    this._word = value;
    this.wordText.text = value;
  }

  get spinning(): boolean {
    return this._spinning;
  }

  public resize(width: number, height: number) {
    this.reelWidth = width;
    this.reelHeight = height;
    this.drawBackground(this.locked);
    
    this.indexLabel.x = -this.reelWidth / 2 + 14;
    this.indexLabel.y = -this.reelHeight / 2 + 12;
    
    this.lockIcon.x = this.reelWidth / 2 - 14;
    this.lockIcon.y = -this.reelHeight / 2 + 12;
  }

  public override destroy() {
    this.forceStop();
    super.destroy();
  }
}
