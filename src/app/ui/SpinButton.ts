import { Container, Graphics, Text } from "pixi.js";
import { animate } from "motion";

export interface SpinButtonOptions {
  width?: number;
  height?: number;
  onPress?: () => void;
}

/**
 * Custom Bitcoin-themed SPIN button
 */
export class SpinButton extends Container {
  private background: Graphics;
  private glow: Graphics;
  private label: Text;
  private _enabled: boolean = true;
  private onPressCallback?: () => void;
  private _width: number;
  private _height: number;

  constructor(options: SpinButtonOptions = {}) {
    super();

    this._width = options.width ?? 200;
    this._height = options.height ?? 70;
    this.onPressCallback = options.onPress;

    // Glow effect
    this.glow = new Graphics();
    this.addChild(this.glow);

    // Background
    this.background = new Graphics();
    this.addChild(this.background);

    // Label
    this.label = new Text({
      text: "SPIN",
      style: {
        fontFamily: "Arial Black, Arial, sans-serif",
        fontSize: 32,
        fontWeight: "bold",
        fill: 0xffffff,
        dropShadow: {
          color: 0x000000,
          blur: 2,
          distance: 1,
        },
      },
    });
    this.label.anchor.set(0.5);
    this.addChild(this.label);

    this.drawButton();

    // Make interactive
    this.eventMode = "static";
    this.cursor = "pointer";

    this.on("pointerdown", this.handlePointerDown.bind(this));
    this.on("pointerup", this.handlePointerUp.bind(this));
    this.on("pointerupoutside", this.handlePointerUp.bind(this));
    this.on("pointerover", this.handlePointerOver.bind(this));
    this.on("pointerout", this.handlePointerOut.bind(this));
  }

  private drawButton() {
    const w = this._width;
    const h = this._height;
    const radius = 15;

    // Draw glow
    this.glow.clear();
    if (this._enabled) {
      this.glow.roundRect(-w / 2 - 4, -h / 2 - 4, w + 8, h + 8, radius + 4);
      this.glow.fill({ color: 0xF7931A, alpha: 0.4 });
    }

    // Draw background
    this.background.clear();
    
    // Outer border
    this.background.roundRect(-w / 2, -h / 2, w, h, radius);
    
    if (this._enabled) {
      // Bitcoin orange gradient effect (top lighter, bottom darker)
      this.background.fill({ color: 0xF7931A });
      
      // Inner highlight
      this.background.roundRect(-w / 2 + 3, -h / 2 + 3, w - 6, h / 2 - 3, radius - 2);
      this.background.fill({ color: 0xFFB84D, alpha: 0.5 });
      
      // Border
      this.background.roundRect(-w / 2, -h / 2, w, h, radius);
      this.background.stroke({ color: 0xFFD93D, width: 3 });
    } else {
      // Disabled state
      this.background.fill({ color: 0x555555 });
      this.background.roundRect(-w / 2, -h / 2, w, h, radius);
      this.background.stroke({ color: 0x666666, width: 2 });
    }

    // Update label
    this.label.style.fill = this._enabled ? 0xffffff : 0x888888;
  }

  private handlePointerDown() {
    if (!this._enabled) return;
    
    animate(this, { scale: 0.95 }, { duration: 0.1 });
  }

  private handlePointerUp() {
    if (!this._enabled) return;
    
    animate(this, { scale: 1 }, { duration: 0.1 });
    this.onPressCallback?.();
  }

  private handlePointerOver() {
    if (!this._enabled) return;
    
    animate(this, { scale: 1.05 }, { duration: 0.1 });
  }

  private handlePointerOut() {
    if (!this._enabled) return;
    
    animate(this, { scale: 1 }, { duration: 0.1 });
  }

  get enabled(): boolean {
    return this._enabled;
  }

  set enabled(value: boolean) {
    this._enabled = value;
    this.cursor = value ? "pointer" : "default";
    this.drawButton();
  }

  public setOnPress(callback: () => void) {
    this.onPressCallback = callback;
  }

  public resize(width: number, height: number) {
    this._width = width;
    this._height = height;
    this.drawButton();
  }
}


