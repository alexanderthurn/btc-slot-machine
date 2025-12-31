import { FancyButton } from "@pixi/ui";
import { Graphics } from "pixi.js";

import { engine } from "../getEngine";

import { Label } from "./Label";

const defaultButtonOptions = {
  text: "",
  width: 150,
  height: 60,
  fontSize: 24,
};

type ButtonOptions = typeof defaultButtonOptions;

/**
 * Bitcoin-themed button with dark background and orange accents
 */
export class Button extends FancyButton {
  constructor(options: Partial<ButtonOptions> = {}) {
    const opts = { ...defaultButtonOptions, ...options };

    // Create default view (dark button)
    const defaultView = new Graphics();
    defaultView.roundRect(0, 0, opts.width, opts.height, 10);
    defaultView.fill({ color: 0x2a2a2a });
    defaultView.stroke({ color: 0xF7931A, width: 2 });

    // Create hover view (slightly brighter)
    const hoverView = new Graphics();
    hoverView.roundRect(0, 0, opts.width, opts.height, 10);
    hoverView.fill({ color: 0x3a3a3a });
    hoverView.stroke({ color: 0xFFB84D, width: 2 });

    // Create pressed view
    const pressedView = new Graphics();
    pressedView.roundRect(0, 0, opts.width, opts.height, 10);
    pressedView.fill({ color: 0x1a1a1a });
    pressedView.stroke({ color: 0xF7931A, width: 2 });

    super({
      defaultView,
      hoverView,
      pressedView,
      anchor: 0.5,
      text: new Label({
        text: opts.text,
        style: {
          fill: 0xF7931A,  // Bitcoin orange text
          align: "center",
          fontSize: opts.fontSize,
          fontWeight: "bold",
        },
      }),
      textOffset: { x: 0, y: 0 },
      defaultTextAnchor: 0.5,
      animations: {
        hover: {
          props: {
            scale: { x: 1.05, y: 1.05 },
          },
          duration: 100,
        },
        pressed: {
          props: {
            scale: { x: 0.95, y: 0.95 },
          },
          duration: 100,
        },
      },
    });

    this.onDown.connect(this.handleDown.bind(this));
    this.onHover.connect(this.handleHover.bind(this));
  }

  private handleHover() {
    engine().audio.sfx.play("main/sounds/sfx-hover.wav");
  }

  private handleDown() {
    engine().audio.sfx.play("main/sounds/sfx-press.wav");
  }
}
