import { Classes } from "@blueprintjs/core";

export const APP_THEME_CLASS = Classes.DARK;

export function applyApplicationTheme() {
  document.documentElement.classList.add(APP_THEME_CLASS);
}
