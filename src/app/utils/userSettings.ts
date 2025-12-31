import { storage } from "../../engine/utils/storage";
import { engine } from "../getEngine";

// Keys for saved items in storage
const KEY_VOLUME_MASTER = "volume-master";
const KEY_VOLUME_BGM = "volume-bgm";
const KEY_VOLUME_SFX = "volume-sfx";
const KEY_ADDRESS_CHECK_COUNT = "address-check-count";
const KEY_LOCKED_WORDS = "locked-words";

// Type for locked words: null means random, string means fixed word
export type LockedWords = (string | null)[];

/**
 * Persistent user settings of volumes.
 */
class UserSettings {
  public init() {
    engine().audio.setMasterVolume(this.getMasterVolume());
    engine().audio.bgm.setVolume(this.getBgmVolume());
    engine().audio.sfx.setVolume(this.getSfxVolume());
  }

  /** Get overall sound volume */
  public getMasterVolume() {
    return storage.getNumber(KEY_VOLUME_MASTER) ?? 0.5;
  }

  /** Set overall sound volume */
  public setMasterVolume(value: number) {
    engine().audio.setMasterVolume(value);
    storage.setNumber(KEY_VOLUME_MASTER, value);
  }

  /** Get background music volume */
  public getBgmVolume() {
    return storage.getNumber(KEY_VOLUME_BGM) ?? 1;
  }

  /** Set background music volume */
  public setBgmVolume(value: number) {
    engine().audio.bgm.setVolume(value);
    storage.setNumber(KEY_VOLUME_BGM, value);
  }

  /** Get sound effects volume */
  public getSfxVolume() {
    return storage.getNumber(KEY_VOLUME_SFX) ?? 1;
  }

  /** Set sound effects volume */
  public setSfxVolume(value: number) {
    engine().audio.sfx.setVolume(value);
    storage.setNumber(KEY_VOLUME_SFX, value);
  }

  /** Get address check count (10 or 20) */
  public getAddressCheckCount(): number {
    return storage.getNumber(KEY_ADDRESS_CHECK_COUNT) ?? 1;
  }

  /** Set address check count */
  public setAddressCheckCount(value: number) {
    storage.setNumber(KEY_ADDRESS_CHECK_COUNT, value);
  }

  /** Get locked words array (12 elements, null = random, string = fixed word) */
  public getLockedWords(): LockedWords {
    const stored = storage.getString(KEY_LOCKED_WORDS);
    if (stored) {
      try {
        const parsed = JSON.parse(stored);
        if (Array.isArray(parsed) && parsed.length === 12) {
          return parsed;
        }
      } catch {
        // Invalid JSON, return default
      }
    }
    // Default: all random
    return Array(12).fill(null);
  }

  /** Set locked word at specific position (null = random) */
  public setLockedWord(index: number, word: string | null) {
    const words = this.getLockedWords();
    if (index >= 0 && index < 12) {
      words[index] = word;
      storage.setString(KEY_LOCKED_WORDS, JSON.stringify(words));
    }
  }

  /** Set all locked words at once */
  public setLockedWords(words: LockedWords) {
    storage.setString(KEY_LOCKED_WORDS, JSON.stringify(words));
  }

  /** Check if a position is locked */
  public isPositionLocked(index: number): boolean {
    const words = this.getLockedWords();
    return words[index] !== null;
  }

  /** Get locked word at position (null if random) */
  public getLockedWord(index: number): string | null {
    const words = this.getLockedWords();
    return words[index] ?? null;
  }
}

/** Shared user settings instance */
export const userSettings = new UserSettings();
