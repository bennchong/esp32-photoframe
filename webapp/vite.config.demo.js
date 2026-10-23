import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";
import vuetify from "vite-plugin-vuetify";
import { resolve } from "path";
import { rename } from "fs/promises";

// Plugin to rename index-demo.html to index.html after build
function renameHtmlPlugin() {
  return {
    name: "rename-html",
    closeBundle: async () => {
      const outDir = resolve(__dirname, "../demo");
      try {
        await rename(`${outDir}/index-demo.html`, `${outDir}/index.html`);
      } catch (_e) {
        // File might not exist or already renamed
      }
    },
  };
}

// Plugin to serve index-demo.html at the base URL in dev, matching the build's
// rename (otherwise the dev server falls back to the firmware's index.html)
function serveDemoHtmlPlugin() {
  return {
    name: "serve-demo-html",
    configureServer(server) {
      const base = server.config.base;
      server.middlewares.use((req, _res, next) => {
        const [path, query] = req.url.split("?");
        if (path === base || path === `${base}index.html`) {
          req.url = `${base}index-demo.html${query ? `?${query}` : ""}`;
        }
        next();
      });
    },
  };
}

// Demo build config - outputs to demo folder for GitHub Pages
export default defineConfig({
  plugins: [vue(), vuetify({ autoImport: true }), renameHtmlPlugin(), serveDemoHtmlPlugin()],
  base: "/esp32-photoframe/",
  publicDir: resolve(__dirname, "../demo"), // Serve demo folder as public (for sample.jpg, manifests)
  build: {
    outDir: resolve(__dirname, "../demo"),
    emptyOutDir: false, // Don't delete existing demo files (manifests, sample.jpg, etc.)
    rollupOptions: {
      input: resolve(__dirname, "index-demo.html"),
      output: {
        entryFileNames: "assets/[name]-[hash].js",
        chunkFileNames: "assets/[name]-[hash].js",
        assetFileNames: "assets/[name]-[hash].[ext]",
      },
    },
  },
  server: {
    open: "/esp32-photoframe/index.html",
    fs: {
      allow: [resolve(__dirname, "..")],
    },
  },
});
