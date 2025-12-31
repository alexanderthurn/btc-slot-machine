import { List } from "@pixi/ui";
import { animate } from "motion";
import type { Text } from "pixi.js";
import { BlurFilter, Container, Sprite, Texture } from "pixi.js";

import { engine } from "../getEngine";
import { Button } from "../ui/Button";
import { Label } from "../ui/Label";
import { RoundedBox } from "../ui/RoundedBox";
import { VolumeSlider } from "../ui/VolumeSlider";
import { userSettings } from "../utils/userSettings";

/** Popup for settings - volume controls */
export class SettingsPopup extends Container {
  /** The dark semi-transparent background covering current screen */
  private bg: Sprite;
  /** Container for the popup UI components */
  private panel: Container;
  /** The popup title label */
  private title: Text;
  /** Button that closes the popup */
  private doneButton: Button;
  /** The panel background */
  private panelBase: RoundedBox;
  /** The build version label */
  private versionLabel: Text;
  /** Layout that organises the volume sliders */
  private volumeLayout: List;
  /** Slider that changes the master volume */
  private masterSlider: VolumeSlider;
  /** Slider that changes background music volume */
  private bgmSlider: VolumeSlider;
  /** Slider that changes sound effects volume */
  private sfxSlider: VolumeSlider;

  constructor() {
    super();

    this.bg = new Sprite(Texture.WHITE);
    this.bg.tint = 0x0;
    this.bg.interactive = true;
    this.addChild(this.bg);

    this.panel = new Container();
    this.addChild(this.panel);

    // Panel for volume controls
    this.panelBase = new RoundedBox({ 
      width: 350,
      height: 380,
      color: 0x1a1a1a,
      shadowColor: 0x000000,
    });
    this.panel.addChild(this.panelBase);

    // Title
    this.title = new Label({
      text: "Settings",
      style: {
        fill: 0xF7931A,
        fontSize: 36,
      },
    });
    this.title.y = -this.panelBase.boxHeight * 0.5 + 45;
    this.panel.addChild(this.title);

    // Volume sliders
    this.volumeLayout = new List({ type: "vertical", elementsMargin: 8 });
    this.volumeLayout.x = -140;
    this.volumeLayout.y = -60;
    this.panel.addChild(this.volumeLayout);

    this.masterSlider = new VolumeSlider("Master");
    this.masterSlider.onUpdate.connect((v) => {
      userSettings.setMasterVolume(v / 100);
    });
    this.volumeLayout.addChild(this.masterSlider);

    this.bgmSlider = new VolumeSlider("Music");
    this.bgmSlider.onUpdate.connect((v) => {
      userSettings.setBgmVolume(v / 100);
    });
    this.volumeLayout.addChild(this.bgmSlider);

    this.sfxSlider = new VolumeSlider("SFX");
    this.sfxSlider.onUpdate.connect((v) => {
      userSettings.setSfxVolume(v / 100);
    });
    this.volumeLayout.addChild(this.sfxSlider);

    // Done button - positioned at bottom of panel
    this.doneButton = new Button({ text: "OK", width: 150, height: 50 });
    this.doneButton.y = this.panelBase.boxHeight * 0.5 - 45;
    this.doneButton.onPress.connect(() => engine().navigation.dismissPopup());
    this.panel.addChild(this.doneButton);

    // Version label
    this.versionLabel = new Label({
      text: `Version ${APP_VERSION}`,
      style: {
        fill: 0xffffff,
        fontSize: 10,
      },
    });
    this.versionLabel.alpha = 0.5;
    this.versionLabel.y = this.panelBase.boxHeight * 0.5 - 10;
    this.panel.addChild(this.versionLabel);
  }

  /** Resize the popup, fired whenever window size changes */
  public resize(width: number, height: number) {
    this.bg.width = width;
    this.bg.height = height;
    this.panel.x = width * 0.5;
    this.panel.y = height * 0.5;
  }

  /** Set things up just before showing the popup */
  public prepare() {
    // Load volume settings
    this.masterSlider.value = userSettings.getMasterVolume() * 100;
    this.bgmSlider.value = userSettings.getBgmVolume() * 100;
    this.sfxSlider.value = userSettings.getSfxVolume() * 100;
  }

  /** Present the popup, animated */
  public async show() {
    const currentEngine = engine();
    if (currentEngine.navigation.currentScreen) {
      currentEngine.navigation.currentScreen.filters = [
        new BlurFilter({ strength: 4 }),
      ];
    }

    this.bg.alpha = 0;
    this.panel.pivot.y = -400;
    animate(this.bg, { alpha: 0.8 }, { duration: 0.2, ease: "linear" });
    await animate(
      this.panel.pivot,
      { y: 0 },
      { duration: 0.3, ease: "backOut" },
    );
  }

  /** Dismiss the popup, animated */
  public async hide() {
    const currentEngine = engine();
    if (currentEngine.navigation.currentScreen) {
      currentEngine.navigation.currentScreen.filters = [];
    }
    animate(this.bg, { alpha: 0 }, { duration: 0.2, ease: "linear" });
    await animate(
      this.panel.pivot,
      { y: -500 },
      {
        duration: 0.3,
        ease: "backIn",
      },
    );
  }
}
