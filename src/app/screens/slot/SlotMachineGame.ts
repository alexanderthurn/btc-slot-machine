import { 
  generateNewMnemonic, 
  checkBTCAddresses, 
  type BTCCheckResult,
  TEST_MNEMONIC,
  satoshisToBTC 
} from "../../utils/btcAddressChecker";
import { userSettings } from "../../utils/userSettings";

export type GameState = "idle" | "spinning" | "checking" | "result";

export interface SlotMachineGameEvents {
  onStateChange?: (state: GameState) => void;
  onWordsChange?: (words: string[]) => void;
  onCheckStart?: (addressCount: number) => void;
  onCheckProgress?: (checked: number, total: number) => void;
  onCheckComplete?: (result: BTCCheckResult) => void;
  onSpinStart?: () => void;
  onSpinComplete?: (words: string[]) => void;
  onActivityFound?: (result: BTCCheckResult) => void;
}

/**
 * Manages the slot machine game state
 */
export class SlotMachineGame {
  private words: string[] = [];
  private mnemonic: string = "";
  private lockedIndices: Set<number> = new Set();
  private _state: GameState = "idle";
  private _autoplayActive: boolean = false;
  private _testMode: boolean = false;
  private autoplayTimeout: number | null = null;
  private events: SlotMachineGameEvents = {};
  private lastResult: BTCCheckResult | null = null;

  constructor() {
    // Initialize with a random mnemonic
    this.generateNewMnemonic();
  }

  /**
   * Generate a new mnemonic and update words
   */
  private generateNewMnemonic() {
    if (this._testMode) {
      this.mnemonic = TEST_MNEMONIC;
    } else {
      this.mnemonic = generateNewMnemonic();
    }
    this.words = this.mnemonic.split(" ");
  }

  /**
   * Set test mode (uses "abandon" mnemonic with known activity)
   */
  public setTestMode(enabled: boolean) {
    this._testMode = enabled;
    if (enabled) {
      this.mnemonic = TEST_MNEMONIC;
      this.words = this.mnemonic.split(" ");
      this.lockedIndices.clear();
      this.events.onWordsChange?.(this.words);
    }
  }

  /**
   * Check if test mode is active
   */
  get testMode(): boolean {
    return this._testMode;
  }

  /**
   * Set event callbacks
   */
  public setEvents(events: SlotMachineGameEvents) {
    this.events = { ...this.events, ...events };
  }

  /**
   * Get current state
   */
  get state(): GameState {
    return this._state;
  }

  /**
   * Set state and notify listeners
   */
  private setState(state: GameState) {
    this._state = state;
    this.events.onStateChange?.(state);
  }

  /**
   * Get current words
   */
  public getWords(): string[] {
    return [...this.words];
  }

  /**
   * Get current mnemonic
   */
  public getMnemonic(): string {
    return this.mnemonic;
  }

  /**
   * Get last check result
   */
  public getLastResult(): BTCCheckResult | null {
    return this.lastResult;
  }

  /**
   * Get word at specific index
   */
  public getWord(index: number): string {
    return this.words[index];
  }

  /**
   * Check if a word is locked
   */
  public isLocked(index: number): boolean {
    return this.lockedIndices.has(index);
  }

  /**
   * Lock or unlock a word
   */
  public setLocked(index: number, locked: boolean) {
    if (locked) {
      this.lockedIndices.add(index);
    } else {
      this.lockedIndices.delete(index);
    }
  }

  /**
   * Get all locked indices
   */
  public getLockedIndices(): Set<number> {
    return new Set(this.lockedIndices);
  }

  /**
   * Check if autoplay is active
   */
  get autoplayActive(): boolean {
    return this._autoplayActive;
  }

  /**
   * Start or stop autoplay
   */
  public setAutoplay(active: boolean) {
    this._autoplayActive = active;
    
    if (!active && this.autoplayTimeout) {
      clearTimeout(this.autoplayTimeout);
      this.autoplayTimeout = null;
    }
  }

  /**
   * Start a spin
   * Returns the new words for unlocked positions
   */
  public startSpin(): string[] {
    if (this._state !== "idle" && this._state !== "result") {
      return this.words;
    }

    this.setState("spinning");
    this.events.onSpinStart?.();

    // Generate new mnemonic (respecting locked words is tricky with real mnemonics)
    // For now, we generate a completely new mnemonic if any word is unlocked
    if (this.lockedIndices.size === 0 || this.lockedIndices.size < 12) {
      if (this._testMode) {
        // In test mode, always use the test mnemonic
        this.mnemonic = TEST_MNEMONIC;
      } else {
        // Generate new random mnemonic
        this.mnemonic = generateNewMnemonic();
      }
      this.words = this.mnemonic.split(" ");
    }

    return this.words;
  }

  /**
   * Called when spin animation completes
   */
  public async onSpinComplete(): Promise<void> {
    this.events.onSpinComplete?.(this.words);
    this.events.onWordsChange?.(this.words);

    // Start BTC check
    await this.checkAddresses();

    // Check if activity was found - stop autoplay if so
    if (this.lastResult?.hasActivity) {
      this.setAutoplay(false);
      this.events.onActivityFound?.(this.lastResult);
      return; // Don't schedule next spin
    }

    // If autoplay is active, schedule next spin
    if (this._autoplayActive) {
      this.autoplayTimeout = window.setTimeout(() => {
        if (this._autoplayActive && this._state === "result") {
          this.startSpin();
        }
      }, 1500); // 1.5 second delay between autoplay spins
    }
  }

  /**
   * Check BTC addresses for current seed phrase
   */
  private async checkAddresses(): Promise<void> {
    this.setState("checking");
    
    const addressCount = userSettings.getAddressCheckCount();
    this.events.onCheckStart?.(addressCount);

    try {
      const result = await checkBTCAddresses(
        this.mnemonic, 
        addressCount,
        (checked, total) => {
          this.events.onCheckProgress?.(checked, total);
        }
      );
      
      this.lastResult = result;
      this.events.onCheckComplete?.(result);
      this.setState("result");
    } catch (error) {
      console.error("BTC check failed:", error);
      this.setState("result");
    }
  }

  /**
   * Reset the game
   */
  public reset() {
    this.setAutoplay(false);
    this._testMode = false;
    this.lockedIndices.clear();
    this.generateNewMnemonic();
    this.lastResult = null;
    this.setState("idle");
    this.events.onWordsChange?.(this.words);
  }

  /**
   * Cleanup
   */
  public destroy() {
    this.setAutoplay(false);
    if (this.autoplayTimeout) {
      clearTimeout(this.autoplayTimeout);
    }
  }
}
