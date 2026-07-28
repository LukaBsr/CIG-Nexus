import path from "node:path";

import { defineConfig } from "vitest/config";

export default defineConfig({
  resolve: {
    alias: {
      "@": path.resolve(__dirname, ".")
    }
  },
  test: {
    environment: "node",
    globalSetup: ["./test/global-setup.ts"],
    // All test files share one testcontainer Postgres instance (started
    // once in globalSetup); running files in parallel causes concurrent
    // TRUNCATE/insert deadlocks across files, so they're serialized instead.
    fileParallelism: false,
    testTimeout: 30000,
    hookTimeout: 60000
  }
});
