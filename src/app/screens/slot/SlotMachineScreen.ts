import { FancyButton } from "@pixi/ui";
import { animate } from "motion";
import { Container, Graphics, Sprite, Text, TilingSprite, Texture } from "pixi.js";

import { engine } from "../../getEngine";
import { SettingsPopup } from "../../popups/SettingsPopup";
import { AutoplayToggle } from "../../ui/AutoplayToggle";
import { BTCResults } from "../../ui/BTCResults";
import { SpinButton } from "../../ui/SpinButton";

import { Reel } from "./Reel";
import { SlotMachineGame, type GameState } from "./SlotMachineGame";

/**
 * The main slot machine screen
 * Displays 12 BIP39 word reels in a grid layout
 */
export class SlotMachineScreen extends Container {
  public static assetBundles = ["main"];

  private game: SlotMachineGame;
  private background: TilingSprite;
  private reelsContainer: Container;
  private reels: Reel[] = [];
  private controlsContainer: Container;
  private spinButton: SpinButton;
  private testButton: Container;
  private testButtonBg: Graphics;
  private testButtonText: Text;
  private autoplayToggle: AutoplayToggle;
  private btcResults: BTCResults;
  private settingsButton: FancyButton;
  private logo: Sprite;
  private titleText: Text;
  private headerBackground: Graphics;
  private footerBackground: Graphics;
  private paused = false;

  // Layout configuration
  private columns = 3;
  private rows = 4;
  private reelWidth = 140;
  private reelHeight = 60;
  private reelGapX = 12;
  private reelGapY = 10;

  constructor() {
    super();

    // Initialize game logic
    this.game = new SlotMachineGame();
    this.setupGameEvents();

    // Tiled background texture
    this.background = new TilingSprite({
      texture: Texture.from("texture-background.jpg"),
      width: 800,
      height: 600,
    });
    // Scale texture down to make it tile more (5x smaller)
    this.background.tileScale.set(0.2, 0.2);
    this.addChild(this.background);

    // Header background
    this.headerBackground = new Graphics();
    this.addChild(this.headerBackground);

    // Logo (top left corner)
    this.logo = Sprite.from("logo-casino.jpg");
    this.logo.anchor.set(0, 0);  // Anchor at top-left
    this.addChild(this.logo);

    // Title text (centered)
    this.titleText = new Text({
      text: "₿ SEED SLOT MACHINE ₿",
      style: {
        fontFamily: "Arial, sans-serif",
        fontSize: 28,
        fontWeight: "bold",
        fill: 0xF7931A,
        dropShadow: {
          color: 0x000000,
          blur: 4,
          distance: 2,
        },
      },
    });
    this.titleText.anchor.set(0.5);
    this.addChild(this.titleText);

    // Settings button
    const buttonAnimations = {
      hover: {
        props: { scale: { x: 1.1, y: 1.1 } },
        duration: 100,
      },
      pressed: {
        props: { scale: { x: 0.9, y: 0.9 } },
        duration: 100,
      },
    };
    
    this.settingsButton = new FancyButton({
      defaultView: "icon-settings.png",
      anchor: 0.5,
      animations: buttonAnimations,
    });
    this.settingsButton.onPress.connect(() =>
      engine().navigation.presentPopup(SettingsPopup)
    );
    this.addChild(this.settingsButton);

    // Reels container
    this.reelsContainer = new Container();
    this.addChild(this.reelsContainer);

    // Create 12 reels
    this.createReels();

    // Footer background
    this.footerBackground = new Graphics();
    this.addChild(this.footerBackground);

    // Controls container
    this.controlsContainer = new Container();
    this.addChild(this.controlsContainer);

    // Spin button
    this.spinButton = new SpinButton({
      width: 200,
      height: 70,
      onPress: () => this.handleSpin(),
    });
    this.controlsContainer.addChild(this.spinButton);

    // Test button (small, left-aligned text)
    this.testButton = new Container();
    this.testButtonBg = new Graphics();
    this.testButtonBg.roundRect(0, -12, 80, 24, 4);
    this.testButtonBg.fill({ color: 0x333333 });
    this.testButtonBg.stroke({ color: 0x555555, width: 1 });
    this.testButton.addChild(this.testButtonBg);
    
    this.testButtonText = new Text({
      text: "🎲 TEST",
      style: {
        fontFamily: "Arial, sans-serif",
        fontSize: 12,
        fontWeight: "bold",
        fill: 0xffffff,
      },
    });
    this.testButtonText.anchor.set(0, 0.5);
    this.testButtonText.x = 8;
    this.testButton.addChild(this.testButtonText);
    
    this.testButton.eventMode = "static";
    this.testButton.cursor = "pointer";
    this.testButton.on("pointerdown", () => this.handleTestMode());
    this.testButton.on("pointerover", () => {
      this.testButtonBg.alpha = 0.8;
    });
    this.testButton.on("pointerout", () => {
      this.testButtonBg.alpha = 1;
    });
    this.controlsContainer.addChild(this.testButton);

    // Autoplay toggle
    this.autoplayToggle = new AutoplayToggle({
      onToggle: (active) => this.handleAutoplayToggle(active),
    });
    this.controlsContainer.addChild(this.autoplayToggle);

    // BTC Results display
    this.btcResults = new BTCResults();
    this.addChild(this.btcResults);
  }

  private createReels() {
    const initialWords = this.game.getWords();
    
    for (let i = 0; i < 12; i++) {
      const reel = new Reel({
        index: i,
        width: this.reelWidth,
        height: this.reelHeight,
      });
      
      reel.word = initialWords[i];
      this.reels.push(reel);
      this.reelsContainer.addChild(reel);
    }
  }

  /**
   * Refresh reel lock states from settings
   * Called after returning from settings popup
   */
  public refreshLockStates() {
    for (const reel of this.reels) {
      reel.updateLockState();
    }
  }

  private setupGameEvents() {
    this.game.setEvents({
      onStateChange: (state) => this.handleStateChange(state),
      onCheckStart: (count) => this.btcResults.showChecking(count),
      onCheckProgress: (checked, total) => this.btcResults.updateProgress(checked, total),
      onCheckComplete: (result) => this.btcResults.showResults(result),
      onSpinStart: () => this.startReelAnimations(),
      onWordsChange: (words) => this.updateReelWords(words),
      onActivityFound: (result) => {
        // Play win sound when BTC activity is found!
        engine().audio.sfx.play("main/sounds/sfx-win.wav", { volume: 0.8 });
        this.btcResults.showActivityFound(result);
        this.autoplayToggle.active = false;
      },
    });
  }

  private updateReelWords(words: string[]) {
    for (let i = 0; i < 12; i++) {
      // Only update if not currently spinning (animation handles that)
      if (!this.reels[i].spinning) {
        this.reels[i].word = words[i];
      }
    }
  }

  private handleStateChange(state: GameState) {
    switch (state) {
      case "spinning":
        this.spinButton.enabled = false;
        break;
      case "checking":
        this.spinButton.enabled = false;
        break;
      case "result":
      case "idle":
        this.spinButton.enabled = !this.game.autoplayActive;
        break;
    }
  }

  private handleSpin() {
    if (this.game.state === "spinning" || this.game.state === "checking") {
      return;
    }

    // Start spin - words will be updated via stopSpin() in startReelAnimations()
    this.game.startSpin();
  }

  private handleTestMode() {
    const newTestMode = !this.game.testMode;
    this.game.setTestMode(newTestMode);
    
    // Update button appearance
    this.testButtonBg.clear();
    this.testButtonBg.roundRect(0, -12, 80, 24, 4);
    this.testButtonBg.fill({ color: newTestMode ? 0x2a1a0a : 0x333333 });
    this.testButtonBg.stroke({ color: newTestMode ? 0xF7931A : 0x555555, width: 1 });
    
    // Update reels with test mnemonic words
    const words = this.game.getWords();
    for (let i = 0; i < 12; i++) {
      this.reels[i].word = words[i];
      this.reels[i].locked = false;
    }
    
    if (newTestMode) {
      this.btcResults.showReady();
      // Show a hint
      const hint = new Text({
        text: "Test mode: Using 'abandon' mnemonic with known activity",
        style: {
          fontFamily: "Arial, sans-serif",
          fontSize: 11,
          fill: 0xF7931A,
        },
      });
      hint.anchor.set(0.5);
      hint.x = this.btcResults.x;
      hint.y = this.btcResults.y + 60;
      this.addChild(hint);
      
      setTimeout(() => {
        animate(hint, { alpha: 0 }, { duration: 0.5 }).then(() => {
          hint.destroy();
        });
      }, 3000);
    }
  }

  private async startReelAnimations() {
    const newWords = this.game.getWords();
    
    // Play spin sound
    engine().audio.sfx.play("main/sounds/sfx-run.wav", { volume: 0.5 });
    
    // Start all unlocked reels spinning
    for (const reel of this.reels) {
      if (!this.game.isLocked(reel.index)) {
        reel.startSpin();
      }
    }

    // Stop reels with staggered timing
    const stopPromises = this.reels.map((reel, i) => {
      if (!this.game.isLocked(reel.index)) {
        // Stagger stop times: 600ms base + 80ms per reel
        const delay = 600 + i * 80;
        return reel.stopSpin(newWords[reel.index], delay);
      }
      return Promise.resolve();
    });

    await Promise.all(stopPromises);
    
    // Notify game that spin is complete
    await this.game.onSpinComplete();
  }

  private handleAutoplayToggle(active: boolean) {
    this.game.setAutoplay(active);
    
    if (active) {
      // Disable manual spin button during autoplay
      this.spinButton.enabled = false;
      
      // Start spinning if not already
      if (this.game.state === "idle" || this.game.state === "result") {
        this.handleSpin();
      }
    } else {
      // Re-enable spin button
      this.spinButton.enabled = 
        this.game.state === "idle" || this.game.state === "result";
    }
  }

  public prepare() {
    this.btcResults.showReady();
  }

  public update() {
    if (this.paused) return;
  }

  public async pause() {
    this.paused = true;
  }

  public async resume() {
    this.paused = false;
    // Refresh lock states after returning from settings
    this.refreshLockStates();
  }

  public reset() {
    this.game.reset();
    for (let i = 0; i < 12; i++) {
      this.reels[i].word = this.game.getWord(i);
      this.reels[i].updateLockState();
    }
    // Reset test button appearance
    this.testButtonBg.clear();
    this.testButtonBg.roundRect(0, -12, 80, 24, 4);
    this.testButtonBg.fill({ color: 0x333333 });
    this.testButtonBg.stroke({ color: 0x555555, width: 1 });
  }

  public resize(width: number, height: number) {
    // Resize tiled background to fill screen
    this.background.width = width;
    this.background.height = height;

    const centerX = width * 0.5;
    const isMobile = width < 600;
    
    // Adjust layout for mobile
    if (isMobile) {
      this.columns = 2;
      this.rows = 6;
      this.reelWidth = Math.min(140, (width - 60) / 2);
      this.reelHeight = 55;
      this.titleText.style.fontSize = 18;
      this.logo.scale.set(0.16);
    } else if (width < 900) {
      this.columns = 3;
      this.rows = 4;
      this.reelWidth = Math.min(140, (width - 80) / 3);
      this.reelHeight = 58;
      this.titleText.style.fontSize = 24;
      this.logo.scale.set(0.2);
    } else {
      this.columns = 4;
      this.rows = 3;
      this.reelWidth = 150;
      this.reelHeight = 65;
      this.titleText.style.fontSize = 28;
      this.logo.scale.set(0.24);
    }

    // Header - no overlay, just show background texture
    this.headerBackground.clear();

    // Logo on top left (bigger, 2x size)
    this.logo.x = 10;
    this.logo.y = 10;

    // Title centered
    this.titleText.x = centerX;
    this.titleText.y = 38;

    // Settings button on top right
    this.settingsButton.x = width - 35;
    this.settingsButton.y = 35;

    // Calculate reel grid dimensions
    const gridWidth = this.columns * this.reelWidth + (this.columns - 1) * this.reelGapX;
    const gridHeight = this.rows * this.reelHeight + (this.rows - 1) * this.reelGapY;
    const gridStartX = (width - gridWidth) / 2 + this.reelWidth / 2;
    const gridStartY = 90 + this.reelHeight / 2;

    // Position reels
    for (let i = 0; i < 12; i++) {
      const col = i % this.columns;
      const row = Math.floor(i / this.columns);
      
      const reel = this.reels[i];
      reel.x = gridStartX + col * (this.reelWidth + this.reelGapX);
      reel.y = gridStartY + row * (this.reelHeight + this.reelGapY);
      reel.resize(this.reelWidth, this.reelHeight);
    }

    // Calculate footer position based on last row of reels
    const lastReelBottomY = gridStartY + (this.rows - 1) * (this.reelHeight + this.reelGapY) + this.reelHeight / 2;
    
    // Footer background - position it below all reels with padding
    const footerY = lastReelBottomY + 25;
    // Footer - no overlay, just show background texture
    this.footerBackground.clear();

    // Controls positioning - SPIN on left, AUTO and TEST stacked on right
    const controlsY = footerY + 50;
    const spinButtonWidth = 200;
    const rightControlsX = centerX + spinButtonWidth / 2 - 30;
    
    // SPIN button slightly left of center
    this.spinButton.x = centerX - 60;
    this.spinButton.y = controlsY;

    // AUTO toggle - top right, left-aligned
    this.autoplayToggle.x = rightControlsX;
    this.autoplayToggle.y = controlsY - 18;

    // TEST button - bottom right, left-aligned (below AUTO)
    this.testButton.x = rightControlsX;
    this.testButton.y = controlsY + 18;

    // BTC Results - give more space for detailed results
    this.btcResults.x = centerX;
    this.btcResults.y = controlsY + 180;
    this.btcResults.resize(width);
  }

  public async show(): Promise<void> {
    // Play casino background music
    engine().audio.bgm.play("main/sounds/bgm-casino.mp3", { volume: 0.4 });

    // Animate elements in
    const elementsToAnimate = [
      this.logo,
      this.titleText,
      this.settingsButton,
      this.reelsContainer,
      this.spinButton,
      this.testButton,
      this.autoplayToggle,
    ];

    for (const element of elementsToAnimate) {
      element.alpha = 0;
    }

    // Staggered fade in
    for (let i = 0; i < elementsToAnimate.length; i++) {
      animate(
        elementsToAnimate[i],
        { alpha: 1 },
        { duration: 0.4, delay: 0.1 * i, ease: "easeOut" }
      );
    }

    // Animate reels with a cascade effect
    for (let i = 0; i < this.reels.length; i++) {
      this.reels[i].alpha = 0;
      animate(
        this.reels[i],
        { alpha: 1 },
        { duration: 0.3, delay: 0.3 + i * 0.05, ease: "easeOut" }
      );
    }

    await new Promise(resolve => setTimeout(resolve, 1000));
  }

  public async hide() {
    await animate(this, { alpha: 0 }, { duration: 0.3 });
  }

  public blur() {
    // Pause autoplay when losing focus
    if (this.game.autoplayActive) {
      this.autoplayToggle.active = false;
      this.game.setAutoplay(false);
    }
  }

  public destroy() {
    this.game.destroy();
    for (const reel of this.reels) {
      reel.destroy();
    }
    super.destroy();
  }
}
