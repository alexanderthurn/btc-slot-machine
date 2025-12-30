import { getRandomWord } from "../../utils/bip39Words";
import { checkBTCAddresses, type BTCCheckResult } from "../../utils/btcAddressChecker";
import { userSettings } from "../../utils/userSettings";

export type GameState = "idle" | "spinning" | "checking" | "result";

export interface SlotMachineGameEvents {
  onStateChange?: (state: GameState) => void;
  onWordsChange?: (words: string[]) => void;
  onCheckStart?: (addressCount: number) => void;
  onCheckComplete?: (result: BTCCheckResult) => void;
  onSpinStart?: () => void;
  onSpinComplete?: (words: string[]) => void;
}

/**
 * Manages the slot machine game state
 */
export class SlotMachineGame {
  private words: string[] = [];
  private lockedIndices: Set<number> = new Set();
  private _state: GameState = "idle";
  private _autoplayActive: boolean = false;
  private autoplayTimeout: number | null = null;
  private events: SlotMachineGameEvents = {};

  constructor() {
    // Initialize with 12 random words
    this.words = Array.from({ length: 12 }, () => getRandomWord());
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

    // Generate new words for unlocked positions
    const newWords = this.words.map((word, index) => {
      if (this.lockedIndices.has(index)) {
        return word; // Keep locked words
      }
      return getRandomWord();
    });

    this.words = newWords;
    return newWords;
  }

  /**
   * Called when spin animation completes
   */
  public async onSpinComplete(): Promise<void> {
    this.events.onSpinComplete?.(this.words);
    this.events.onWordsChange?.(this.words);

    // Start BTC check
    await this.checkAddresses();

    // If autoplay is active, schedule next spin
    if (this._autoplayActive) {
      this.autoplayTimeout = window.setTimeout(() => {
        if (this._autoplayActive && this._state === "result") {
          this.startSpin();
        }
      }, 2000); // 2 second delay between autoplay spins
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
      const result = await checkBTCAddresses(this.words, addressCount);
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
    this.lockedIndices.clear();
    this.words = Array.from({ length: 12 }, () => getRandomWord());
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


