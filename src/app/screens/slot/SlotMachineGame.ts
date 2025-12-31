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
 * Locked words are now controlled via Settings popup
 */
export class SlotMachineGame {
  private words: string[] = [];
  private mnemonic: string = "";
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
   * Respects locked words from settings
   */
  private generateNewMnemonic() {
    if (this._testMode) {
      this.mnemonic = TEST_MNEMONIC;
      this.words = this.mnemonic.split(" ");
    } else {
      // Generate a fresh random mnemonic
      const newMnemonic = generateNewMnemonic();
      const newWords = newMnemonic.split(" ");
      
      // Apply locked words from settings
      const lockedWords = userSettings.getLockedWords();
      for (let i = 0; i < 12; i++) {
        if (lockedWords[i] !== null) {
          newWords[i] = lockedWords[i];
        }
      }
      
      this.words = newWords;
      this.mnemonic = newWords.join(" ");
    }
  }

  /**
   * Set test mode (uses "abandon" mnemonic with known activity)
   */
  public setTestMode(enabled: boolean) {
    this._testMode = enabled;
    if (enabled) {
      this.mnemonic = TEST_MNEMONIC;
      this.words = this.mnemonic.split(" ");
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
   * Check if a word is locked (from settings)
   */
  public isLocked(index: number): boolean {
    return userSettings.isPositionLocked(index);
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

  // Promise for the current BTC check (runs parallel to spin animation)
  private currentCheckPromise: Promise<BTCCheckResult> | null = null;

  /**
   * Start a spin
   * Returns the new words for unlocked positions
   */
  public startSpin(): string[] {
    if (this._state !== "idle" && this._state !== "result") {
      return this.words;
    }

    this.setState("spinning");

    // Generate new mnemonic FIRST (before firing onSpinStart)
    // so that getWords() returns the new words in the animation handler
    if (this._testMode) {
      // In test mode, always use the test mnemonic
      this.mnemonic = TEST_MNEMONIC;
      this.words = this.mnemonic.split(" ");
    } else {
      // Generate new random mnemonic, respecting locked words
      const newMnemonic = generateNewMnemonic();
      const newWords = newMnemonic.split(" ");
      
      // Apply locked words from settings
      const lockedWords = userSettings.getLockedWords();
      for (let i = 0; i < 12; i++) {
        if (lockedWords[i] !== null) {
          newWords[i] = lockedWords[i];
        }
      }
      
      this.words = newWords;
      this.mnemonic = newWords.join(" ");
    }

    // Start BTC check immediately (parallel to animation)
    this.startCheckInBackground();

    // Now fire onSpinStart - getWords() will return the NEW words
    this.events.onSpinStart?.();

    return this.words;
  }

  /**
   * Start the BTC check in the background (parallel to spin animation)
   */
  private startCheckInBackground(): void {
    const addressCount = userSettings.getAddressCheckCount();
    this.events.onCheckStart?.(addressCount);

    this.currentCheckPromise = checkBTCAddresses(
      this.mnemonic,
      addressCount,
      (checked, total) => {
        this.events.onCheckProgress?.(checked, total);
      }
    );
  }

  /**
   * Called when spin animation completes
   */
  public async onSpinComplete(): Promise<void> {
    this.events.onSpinComplete?.(this.words);
    this.events.onWordsChange?.(this.words);

    // Wait for BTC check to complete (it started when spin began)
    if (this.currentCheckPromise) {
      try {
        this.setState("checking");
        const result = await this.currentCheckPromise;
        this.lastResult = result;
        this.events.onCheckComplete?.(result);
        this.currentCheckPromise = null;
      } catch (error) {
        console.error("BTC check failed:", error);
        this.currentCheckPromise = null;
      }
    }

    this.setState("result");

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
      }, 1000); // 1 second delay between autoplay spins
    }
  }

  /**
   * Reset the game
   */
  public reset() {
    this.setAutoplay(false);
    this._testMode = false;
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
