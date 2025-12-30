import { Container, Graphics, Text } from "pixi.js";

const LOCK_ICON = "🔒";
const UNLOCK_ICON = "🔓";

export interface LockButtonOptions {
  size?: number;
  locked?: boolean;
  onToggle?: (locked: boolean) => void;
}

/**
 * A toggle button for locking/unlocking individual words
 */
export class LockButton extends Container {
  private background: Graphics;
  private icon: Text;
  private _locked: boolean = false;
  private onToggle?: (locked: boolean) => void;
  private size: number;

  constructor(options: LockButtonOptions = {}) {
    super();

    this.size = options.size ?? 36;
    this._locked = options.locked ?? false;
    this.onToggle = options.onToggle;

    // Background circle
    this.background = new Graphics();
    this.addChild(this.background);

    // Lock icon
    this.icon = new Text({
      text: this._locked ? LOCK_ICON : UNLOCK_ICON,
      style: {
        fontSize: this.size * 0.5,
        fill: 0xffffff,
      },
    });
    this.icon.anchor.set(0.5);
    this.addChild(this.icon);

    this.updateVisual();

    // Make interactive
    this.eventMode = "static";
    this.cursor = "pointer";
    this.hitArea = { contains: (x: number, y: number) => {
      const radius = this.size / 2;
      return x * x + y * y <= radius * radius;
    }};

    this.on("pointerdown", this.handleClick.bind(this));
    this.on("pointerover", this.handleHover.bind(this));
    this.on("pointerout", this.handleOut.bind(this));
  }

  private updateVisual() {
    const radius = this.size / 2;
    
    this.background.clear();
    
    if (this._locked) {
      // Locked state - Bitcoin orange
      this.background.circle(0, 0, radius);
      this.background.fill({ color: 0xF7931A, alpha: 0.9 });
      this.background.stroke({ color: 0xFFB84D, width: 2 });
    } else {
      // Unlocked state - subtle dark
      this.background.circle(0, 0, radius);
      this.background.fill({ color: 0x333333, alpha: 0.7 });
      this.background.stroke({ color: 0x555555, width: 2 });
    }

    this.icon.text = this._locked ? LOCK_ICON : UNLOCK_ICON;
  }

  private handleClick() {
    this._locked = !this._locked;
    this.updateVisual();
    this.onToggle?.(this._locked);
  }

  private handleHover() {
    this.scale.set(1.1);
  }

  private handleOut() {
    this.scale.set(1);
  }

  get locked(): boolean {
    return this._locked;
  }

  set locked(value: boolean) {
    this._locked = value;
    this.updateVisual();
  }

  public setOnToggle(callback: (locked: boolean) => void) {
    this.onToggle = callback;
  }
}


