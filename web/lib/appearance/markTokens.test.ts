import { describe, expect, it } from "vitest";

import { MARK_TOKEN_VARS } from "./markTokens";
import { THEMES } from "./themes";
import { effectiveTokens } from "./themeStylesheet";

// Reads the real globals.css (via themeStylesheet's cascade-aware parser, the
// same one themes.drift.test.ts uses) rather than hardcoding token names, so
// this fails if components/Mark.tsx comes to depend on a variable a theme
// doesn't define.
describe("Mark's token list exists in every theme", () => {
  for (const theme of THEMES) {
    it(`${theme.id} defines every variable Mark reads`, () => {
      const tokens = effectiveTokens(theme.id);
      for (const variable of MARK_TOKEN_VARS) {
        const name = variable.replace(/^--color-/, "");
        expect(tokens, `${theme.id} is missing ${variable}`).toHaveProperty(name);
      }
    });
  }
});
