/** @type {import('next').NextConfig} */
const nextConfig = {
  output: "export",
  basePath: "/OpenCubeOS",
  assetPrefix: "/OpenCubeOS/",
  images: { unoptimized: true },
  trailingSlash: true,
  // Isolate Turbopack from the outer Next.js workspace (it picks the outer
  // bun.lock otherwise and fails to resolve ./globals.css and "@/lib/site")
  turbopack: { root: __dirname },
};

module.exports = nextConfig;
