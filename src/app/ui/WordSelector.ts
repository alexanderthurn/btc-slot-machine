import { Select } from "@pixi/ui";
import { Container, Graphics, Text } from "pixi.js";
import { ENGLISH_WORDS } from "../utils/bip39Words";

const RANDOM_OPTION = "Random";

export interface WordSelectorOptions {
  index: number;
  selectedWord?: string | null;
  onSelect?: (index: number, word: string | null) => void;
}

/**
 * A dropdown selector for choosing a locked word or "Random"
 */
export class WordSelector extends Container {
  public readonly index: number;
  private select: Select;
  private indexLabel: Text;
  private onSelectCallback?: (index: number, word: string | null) => void;
  private items: string[];

  constructor(options: WordSelectorOptions) {
    super();

    this.index = options.index;
    this.onSelectCallback = options.onSelect;

    // Create items list: "Random" first, then all BIP39 words
    this.items = [RANDOM_OPTION, ...ENGLISH_WORDS];

    // Label showing position number
    this.indexLabel = new Text({
      text: `${this.index + 1}:`,
      style: {
        fontFamily: "Arial, sans-serif",
        fontSize: 14,
        fontWeight: "bold",
        fill: 0xF7931A,
      },
    });
    this.indexLabel.anchor.set(0, 0.5);
    this.addChild(this.indexLabel);

    // Create closed background
    const closedBG = new Graphics()
      .roundRect(0, 0, 120, 28, 6)
      .fill({ color: 0x2a2a2a })
      .stroke({ color: 0x444444, width: 1 });

    // Create open background
    const openBG = new Graphics()
      .roundRect(0, 0, 120, 28, 6)
      .fill({ color: 0x3a3a3a })
      .stroke({ color: 0xF7931A, width: 2 });

    // Find selected index
    let selectedIndex = 0; // Default to "Random"
    if (options.selectedWord) {
      const wordIndex = this.items.indexOf(options.selectedWord);
      if (wordIndex >= 0) {
        selectedIndex = wordIndex;
      }
    }

    // Create the select dropdown
    this.select = new Select({
      closedBG,
      openBG,
      textStyle: {
        fontFamily: "Courier New, monospace",
        fontSize: 12,
        fill: 0xffffff,
      },
      selected: selectedIndex,
      selectedTextOffset: { x: 8, y: 0 },
      items: {
        items: this.items,
        backgroundColor: 0x1a1a1a,
        hoverColor: 0xF7931A,
        width: 120,
        height: 24,
        textStyle: {
          fontFamily: "Courier New, monospace",
          fontSize: 11,
          fill: 0xcccccc,
        },
        radius: 4,
      },
      scrollBox: {
        width: 120,
        height: 200,
        radius: 6,
      },
      visibleItems: 8,
    });

    this.select.x = 28;
    this.addChild(this.select);

    // Listen for selection changes
    this.select.onSelect.connect((value: number, text: string) => {
      const selectedWord = text === RANDOM_OPTION ? null : text;
      this.onSelectCallback?.(this.index, selectedWord);
    });
  }

  /**
   * Get current selected word (null = Random)
   */
  public get selectedWord(): string | null {
    const text = this.items[this.select.value];
    return text === RANDOM_OPTION ? null : text;
  }

  /**
   * Set selected word
   */
  public set selectedWord(word: string | null) {
    if (word === null) {
      this.select.value = 0; // Random
    } else {
      const index = this.items.indexOf(word);
      if (index >= 0) {
        this.select.value = index;
      }
    }
  }
}

