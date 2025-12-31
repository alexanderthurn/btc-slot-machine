import { Container, Graphics, Text } from "pixi.js";

export interface AutoplayToggleOptions {
  width?: number;
  height?: number;
  active?: boolean;
  onToggle?: (active: boolean) => void;
}

/**
 * Autoplay checkbox/toggle for continuous spinning
 */
export class AutoplayToggle extends Container {
  private background: Graphics;
  private checkmark: Graphics;
  private label: Text;
  private _active: boolean = false;
  private onToggle?: (active: boolean) => void;
  private boxSize: number = 28;

  constructor(options: AutoplayToggleOptions = {}) {
    super();

    this._active = options.active ?? false;
    this.onToggle = options.onToggle;

    // Checkbox background
    this.background = new Graphics();
    this.addChild(this.background);

    // Checkmark
    this.checkmark = new Graphics();
    this.addChild(this.checkmark);

    // Label
    this.label = new Text({
      text: "AUTO",
      style: {
        fontFamily: "Arial, sans-serif",
        fontSize: 16,
        fontWeight: "bold",
        fill: 0xffffff,
      },
    });
    this.label.anchor.set(0, 0.5);
    this.label.x = this.boxSize + 10;
    this.addChild(this.label);

    this.updateVisual();

    // Make interactive
    this.eventMode = "static";
    this.cursor = "pointer";

    this.on("pointerdown", this.handleClick.bind(this));
    this.on("pointerover", this.handleHover.bind(this));
    this.on("pointerout", this.handleOut.bind(this));
  }

  private updateVisual() {
    const size = this.boxSize;
    const cornerRadius = 6;

    this.background.clear();
    
    if (this._active) {
      // Active state - Bitcoin orange
      this.background.roundRect(0, -size / 2, size, size, cornerRadius);
      this.background.fill({ color: 0xF7931A });
      this.background.stroke({ color: 0xFFB84D, width: 2 });
    } else {
      // Inactive state
      this.background.roundRect(0, -size / 2, size, size, cornerRadius);
      this.background.fill({ color: 0x2a2a2a });
      this.background.stroke({ color: 0x555555, width: 2 });
    }

    // Draw checkmark
    this.checkmark.clear();
    if (this._active) {
      this.checkmark.moveTo(6, 0);
      this.checkmark.lineTo(12, 6);
      this.checkmark.lineTo(22, -6);
      this.checkmark.stroke({ color: 0xffffff, width: 3 });
    }

    // Label always white
    this.label.style.fill = 0xffffff;
  }

  private handleClick() {
    this._active = !this._active;
    this.updateVisual();
    this.onToggle?.(this._active);
  }

  private handleHover() {
    this.alpha = 0.8;
  }

  private handleOut() {
    this.alpha = 1;
  }

  get active(): boolean {
    return this._active;
  }

  set active(value: boolean) {
    this._active = value;
    this.updateVisual();
  }

  public setOnToggle(callback: (active: boolean) => void) {
    this.onToggle = callback;
  }
}


