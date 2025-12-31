import { Slider } from "@pixi/ui";
import { Graphics } from "pixi.js";

import { Label } from "./Label";

/**
 * A volume slider component to be used in the Settings popup.
 */
export class VolumeSlider extends Slider {
  /** Message displayed for the slider */
  public messageLabel: Label;

  constructor(label: string, min = -0.1, max = 100, value = 100) {
    const width = 280;
    const height = 20;
    const radius = 20;
    const border = 4;
    const handleRadius = 14;
    const handleBorder = 4;
    const meshColor = 0xF7931A;      // Bitcoin orange
    const fillColor = 0xFFB84D;       // Light orange
    const borderColor = 0xF7931A;     // Bitcoin orange
    const backgroundColor = 0x1a1a1a; // Dark background to match game

    const bg = new Graphics()
      .roundRect(0, 0, width, height, radius)
      .fill({ color: borderColor })
      .roundRect(
        border,
        border,
        width - border * 2,
        height - border * 2,
        radius,
      )
      .fill({ color: backgroundColor });

    const fill = new Graphics()
      .roundRect(0, 0, width, height, radius)
      .fill({ color: borderColor })
      .roundRect(
        border,
        border,
        width - border * 2,
        height - border * 2,
        radius,
      )
      .fill({ color: fillColor });

    const slider = new Graphics()
      .circle(0, 0, handleRadius + handleBorder)
      .fill({ color: meshColor });

    super({
      bg,
      fill,
      slider,
      min,
      max,
    });

    this.value = value;

    this.messageLabel = new Label({
      text: label,
      style: {
        align: "left",
        fill: 0xcccccc,  // Light gray text for dark background
        fontSize: 18,
      },
    });
    this.messageLabel.anchor.x = 0;
    this.messageLabel.x = 10;
    this.messageLabel.y = -18;
    this.addChild(this.messageLabel);
  }
}
